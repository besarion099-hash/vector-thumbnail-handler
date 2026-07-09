#include "thumbnail_provider.h"

#include <shlwapi.h>
#include <new>
#include <vector>

#include "renderers.h"

#ifdef VTH_DEBUG_LOG
#include <cstdio>
#include <strsafe.h>
void VthLog(const char* fmt, ...)
{
    // LocalLow, damit auch Low-Integrity-Prozesse schreiben duerfen.
    const wchar_t* profile = _wgetenv(L"USERPROFILE");
    if (!profile)
        return;
    wchar_t path[MAX_PATH];
    StringCchPrintfW(path, MAX_PATH, L"%s\\AppData\\LocalLow\\vecthumb.log", profile);
    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"a") != 0 || !f)
        return;
    wchar_t exe[MAX_PATH] = L"?";
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const wchar_t* base = wcsrchr(exe, L'\\');
    fwprintf(f, L"[pid %lu %s] ", GetCurrentProcessId(), base ? base + 1 : exe);
    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);
    fputc('\n', f);
    fclose(f);
}
#else
#define VthLog(...) ((void)0)
#endif

// Groessere Dateien ignorieren statt Speicher zu strapazieren.
static const ULONGLONG kMaxFileSize = 256ull * 1024 * 1024;

const CLSID CLSID_VectorThumbnailProvider =
    { 0x26cb6e50, 0x6e37, 0x40fd, { 0xba, 0xc2, 0xd8, 0x13, 0x0c, 0xf9, 0xe5, 0x49 } };

namespace {

class VectorThumbnailProvider : public IInitializeWithStream,
                                public IThumbnailProvider
{
public:
    VectorThumbnailProvider() : m_ref(1), m_stream(nullptr) { DllAddRef(); }

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv)
    {
        if (!ppv)
            return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IInitializeWithStream)
            *ppv = static_cast<IInitializeWithStream*>(this);
        else if (riid == IID_IThumbnailProvider)
            *ppv = static_cast<IThumbnailProvider*>(this);
        else {
            *ppv = nullptr;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
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

    // IInitializeWithStream
    IFACEMETHODIMP Initialize(IStream* pstream, DWORD /*grfMode*/)
    {
        VthLog("Initialize");
        if (!pstream)
            return E_POINTER;
        if (m_stream)
            return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
        m_stream = pstream;
        m_stream->AddRef();
        return S_OK;
    }

    // IThumbnailProvider
    IFACEMETHODIMP GetThumbnail(UINT cx, HBITMAP* phbmp, WTS_ALPHATYPE* pdwAlpha)
    {
        VthLog("GetThumbnail(cx=%u)", cx);
        if (!phbmp || !pdwAlpha)
            return E_POINTER;
        *phbmp = nullptr;
        *pdwAlpha = WTSAT_UNKNOWN;
        if (!m_stream)
            return E_UNEXPECTED;

        STATSTG stat = {};
        HRESULT hr = m_stream->Stat(&stat, STATFLAG_NONAME);
        if (FAILED(hr))
            return hr;
        if (stat.cbSize.QuadPart == 0 || stat.cbSize.QuadPart > kMaxFileSize)
            return E_FAIL;

        const size_t size = static_cast<size_t>(stat.cbSize.QuadPart);
        std::vector<unsigned char> content;
        try {
            content.resize(size);
        } catch (const std::bad_alloc&) {
            return E_OUTOFMEMORY;
        }

        LARGE_INTEGER zero = {};
        hr = m_stream->Seek(zero, STREAM_SEEK_SET, nullptr);
        if (FAILED(hr))
            return hr;

        size_t total = 0;
        while (total < size) {
            const ULONG chunk = static_cast<ULONG>(
                (size - total > 1 << 20) ? (1 << 20) : (size - total));
            ULONG read = 0;
            hr = m_stream->Read(content.data() + total, chunk, &read);
            if (FAILED(hr))
                return hr;
            if (read == 0)
                break;
            total += read;
        }
        if (total == 0)
            return E_FAIL;

        hr = RenderVectorThumbnail(content.data(), total, cx, phbmp);
        VthLog("GetThumbnail: hr=0x%08lX", (unsigned long)hr);
        if (FAILED(hr))
            return hr;

        *pdwAlpha = WTSAT_ARGB;
        return S_OK;
    }

private:
    ~VectorThumbnailProvider()
    {
        if (m_stream)
            m_stream->Release();
        DllRelease();
    }

    volatile LONG m_ref;
    IStream* m_stream;
};

} // namespace

HRESULT CreateVectorThumbnailProvider(REFIID riid, void** ppv)
{
    VectorThumbnailProvider* provider = new (std::nothrow) VectorThumbnailProvider();
    if (!provider)
        return E_OUTOFMEMORY;
    HRESULT hr = provider->QueryInterface(riid, ppv);
    provider->Release();
    return hr;
}
