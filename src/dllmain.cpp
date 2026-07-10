// VectorThumbnailHandler.dll
// Windows-Explorer-Miniaturansichten fuer SVG/SVGZ, AI, EPS/PS und DXF.
// Registrierung maschinenweit (HKLM, Fallback HKCU); die DLL selbst gehoert
// nach "C:\Program Files" (Sandbox-Prozesse duerfen AppData nicht lesen).

#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <strsafe.h>
#include <new>

#include "thumbnail_provider.h"

#ifdef VTH_DEBUG_LOG
void VthLog(const char* fmt, ...);
#else
#define VthLog(...) ((void)0)
#endif

static HINSTANCE g_hInst = nullptr;
static volatile LONG g_objCount = 0;

void DllAddRef()
{
    InterlockedIncrement(&g_objCount);
}

void DllRelease()
{
    InterlockedDecrement(&g_objCount);
}

static const wchar_t kClsidString[] = L"{26CB6E50-6E37-40FD-BAC2-D8130CF9E549}";
// Kategorie "Thumbnail Provider" (fest von Windows vorgegeben).
static const wchar_t kThumbnailCategory[] = L"{E357FCCD-A995-4576-B01F-234630154E96}";
static const wchar_t kDescription[] =
    L"Vector Thumbnail Handler (SVG/AI/EPS/DXF/PDF/XCS/CDR/LightBurn)";
// Formate: SVG, SVGZ, AI, EPS, PS, DXF, PDF, XCS, XS, CDR, LBRN, LBRN2

static const wchar_t* kExtensions[] = {
    L".svg", L".svgz", L".ai", L".eps", L".ps", L".dxf",
    L".pdf", L".xcs", L".cdr", L".lbrn", L".lbrn2", L".xs",
};

namespace {

class ClassFactory : public IClassFactory
{
public:
    ClassFactory() : m_ref(1) { DllAddRef(); }

    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv)
    {
        if (!ppv)
            return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *ppv = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    IFACEMETHODIMP_(ULONG) AddRef()
    {
        return InterlockedIncrement(&m_ref);
    }

    IFACEMETHODIMP_(ULONG) Release()
    {
        ULONG ref = InterlockedDecrement(&m_ref);
        if (ref == 0)
            delete this;
        return ref;
    }

    IFACEMETHODIMP CreateInstance(IUnknown* punkOuter, REFIID riid, void** ppv)
    {
        if (punkOuter)
            return CLASS_E_NOAGGREGATION;
        return CreateVectorThumbnailProvider(riid, ppv);
    }

    IFACEMETHODIMP LockServer(BOOL fLock)
    {
        if (fLock)
            DllAddRef();
        else
            DllRelease();
        return S_OK;
    }

private:
    ~ClassFactory() { DllRelease(); }

    volatile LONG m_ref;
};

HRESULT SetRegValue(HKEY root, const wchar_t* subkey, const wchar_t* name,
                    const wchar_t* value)
{
    HKEY hkey = nullptr;
    LONG rc = RegCreateKeyExW(root, subkey, 0, nullptr, REG_OPTION_NON_VOLATILE,
                              KEY_SET_VALUE, nullptr, &hkey, nullptr);
    if (rc != ERROR_SUCCESS)
        return HRESULT_FROM_WIN32(rc);
    rc = RegSetValueExW(hkey, name, 0, REG_SZ,
                        reinterpret_cast<const BYTE*>(value),
                        static_cast<DWORD>((wcslen(value) + 1) * sizeof(wchar_t)));
    RegCloseKey(hkey);
    return HRESULT_FROM_WIN32(rc);
}

HRESULT SetRegDword(HKEY root, const wchar_t* subkey, const wchar_t* name,
                    DWORD value)
{
    HKEY hkey = nullptr;
    LONG rc = RegCreateKeyExW(root, subkey, 0, nullptr, REG_OPTION_NON_VOLATILE,
                              KEY_SET_VALUE, nullptr, &hkey, nullptr);
    if (rc != ERROR_SUCCESS)
        return HRESULT_FROM_WIN32(rc);
    rc = RegSetValueExW(hkey, name, 0, REG_DWORD,
                        reinterpret_cast<const BYTE*>(&value), sizeof(value));
    RegCloseKey(hkey);
    return HRESULT_FROM_WIN32(rc);
}

// Liest den Standardwert eines Schluessels (leerer String, wenn nicht da).
void GetRegDefault(HKEY root, const wchar_t* subkey, wchar_t* buf, DWORD cchBuf)
{
    buf[0] = 0;
    HKEY hkey = nullptr;
    if (RegOpenKeyExW(root, subkey, 0, KEY_QUERY_VALUE, &hkey) != ERROR_SUCCESS)
        return;
    DWORD cb = cchBuf * sizeof(wchar_t);
    DWORD type = 0;
    if (RegQueryValueExW(hkey, nullptr, nullptr, &type,
                         reinterpret_cast<BYTE*>(buf), &cb) != ERROR_SUCCESS ||
        type != REG_SZ) {
        buf[0] = 0;
    }
    buf[cchBuf - 1] = 0;
    RegCloseKey(hkey);
}

// Registriert die ShellEx-Verknuepfung fuer eine Erweiterung unter root:
// an der Erweiterung selbst, an SystemFileAssociations und - falls die
// Erweiterung eine ProgId hat - auch an der ProgId.
HRESULT RegisterExtension(HKEY root, const wchar_t* ext)
{
    wchar_t key[512];

    StringCchPrintfW(key, ARRAYSIZE(key), L"Software\\Classes\\%s\\ShellEx\\%s",
                     ext, kThumbnailCategory);
    HRESULT hr = SetRegValue(root, key, nullptr, kClsidString);
    if (FAILED(hr))
        return hr;

    StringCchPrintfW(key, ARRAYSIZE(key),
                     L"Software\\Classes\\SystemFileAssociations\\%s\\ShellEx\\%s",
                     ext, kThumbnailCategory);
    hr = SetRegValue(root, key, nullptr, kClsidString);
    if (FAILED(hr))
        return hr;

    // ProgId der Erweiterung (merged view via HKCR lesen)
    wchar_t progId[256];
    GetRegDefault(HKEY_CLASSES_ROOT, ext, progId, ARRAYSIZE(progId));
    if (progId[0]) {
        StringCchPrintfW(key, ARRAYSIZE(key),
                         L"Software\\Classes\\%s\\ShellEx\\%s", progId,
                         kThumbnailCategory);
        SetRegValue(root, key, nullptr, kClsidString); // best effort
    }
    return S_OK;
}

// Loescht einen ShellEx-Eintrag NUR, wenn er auf unsere CLSID zeigt -
// Eintraege anderer Programme bleiben unangetastet.
void UnregisterShellExIfOurs(HKEY root, const wchar_t* keyPath)
{
    wchar_t value[64];
    GetRegDefault(root, keyPath, value, ARRAYSIZE(value));
    if (_wcsicmp(value, kClsidString) == 0)
        SHDeleteKeyW(root, keyPath);
}

void UnregisterExtension(HKEY root, const wchar_t* ext)
{
    wchar_t key[512];

    StringCchPrintfW(key, ARRAYSIZE(key), L"Software\\Classes\\%s\\ShellEx\\%s",
                     ext, kThumbnailCategory);
    UnregisterShellExIfOurs(root, key);

    StringCchPrintfW(key, ARRAYSIZE(key),
                     L"Software\\Classes\\SystemFileAssociations\\%s\\ShellEx\\%s",
                     ext, kThumbnailCategory);
    UnregisterShellExIfOurs(root, key);

    wchar_t progId[256];
    GetRegDefault(HKEY_CLASSES_ROOT, ext, progId, ARRAYSIZE(progId));
    if (progId[0]) {
        StringCchPrintfW(key, ARRAYSIZE(key),
                         L"Software\\Classes\\%s\\ShellEx\\%s", progId,
                         kThumbnailCategory);
        UnregisterShellExIfOurs(root, key);
    }
}

HRESULT RegisterUnderRoot(HKEY root, const wchar_t* modulePath)
{
    wchar_t key[512];

    StringCchPrintfW(key, ARRAYSIZE(key), L"Software\\Classes\\CLSID\\%s",
                     kClsidString);
    HRESULT hr = SetRegValue(root, key, nullptr, kDescription);
    if (FAILED(hr))
        return hr;

    StringCchPrintfW(key, ARRAYSIZE(key),
                     L"Software\\Classes\\CLSID\\%s\\InprocServer32", kClsidString);
    hr = SetRegValue(root, key, nullptr, modulePath);
    if (FAILED(hr))
        return hr;
    hr = SetRegValue(root, key, L"ThreadingModel", L"Apartment");
    if (FAILED(hr))
        return hr;

    if (root == HKEY_LOCAL_MACHINE) {
        StringCchPrintfW(key, ARRAYSIZE(key), L"Software\\Classes\\CLSID\\%s",
                         kClsidString);
        hr = SetRegValue(root, key, L"AppID", kClsidString);
        if (FAILED(hr))
            return hr;
        StringCchPrintfW(key, ARRAYSIZE(key), L"Software\\Classes\\AppID\\%s",
                         kClsidString);
        hr = SetRegValue(root, key, L"DllSurrogate", L"");
        if (FAILED(hr))
            return hr;
    }

    StringCchPrintfW(key, ARRAYSIZE(key), L"Software\\Classes\\CLSID\\%s",
                     kClsidString);
    hr = SetRegDword(root, key, L"DisableProcessIsolation", 1);
    if (FAILED(hr))
        return hr;

    for (const wchar_t* ext : kExtensions) {
        hr = RegisterExtension(root, ext);
        if (FAILED(hr))
            return hr;
    }
    return S_OK;
}

void UnregisterUnderRoot(HKEY root)
{
    for (const wchar_t* ext : kExtensions)
        UnregisterExtension(root, ext);

    wchar_t key[512];
    StringCchPrintfW(key, ARRAYSIZE(key), L"Software\\Classes\\CLSID\\%s",
                     kClsidString);
    SHDeleteKeyW(root, key);
    StringCchPrintfW(key, ARRAYSIZE(key), L"Software\\Classes\\AppID\\%s",
                     kClsidString);
    SHDeleteKeyW(root, key);
}

} // namespace

BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, void*)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_hInst = hInst;
        DisableThreadLibraryCalls(hInst);
        VthLog("DllMain: PROCESS_ATTACH");
    }
    return TRUE;
}

STDAPI DllCanUnloadNow()
{
    return (g_objCount == 0) ? S_OK : S_FALSE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv)
{
    if (!ppv)
        return E_POINTER;
    *ppv = nullptr;
    if (rclsid != CLSID_VectorThumbnailProvider)
        return CLASS_E_CLASSNOTAVAILABLE;

    ClassFactory* factory = new (std::nothrow) ClassFactory();
    if (!factory)
        return E_OUTOFMEMORY;
    HRESULT hr = factory->QueryInterface(riid, ppv);
    factory->Release();
    return hr;
}

STDAPI DllRegisterServer()
{
    wchar_t modulePath[MAX_PATH];
    if (GetModuleFileNameW(g_hInst, modulePath, ARRAYSIZE(modulePath)) == 0)
        return HRESULT_FROM_WIN32(GetLastError());

    HRESULT hr = RegisterUnderRoot(HKEY_LOCAL_MACHINE, modulePath);
    if (hr == E_ACCESSDENIED ||
        hr == HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED))
        hr = RegisterUnderRoot(HKEY_CURRENT_USER, modulePath);
    if (FAILED(hr))
        return hr;

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return S_OK;
}

STDAPI DllUnregisterServer()
{
    UnregisterUnderRoot(HKEY_CURRENT_USER);
    UnregisterUnderRoot(HKEY_LOCAL_MACHINE); // scheitert ohne Admin still

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return S_OK;
}
