// EPS/PS-Miniaturen:
//  1. DOS-EPS-Binaerheader -> eingebettete TIFF- oder WMF-Vorschau
//  2. EPSI (%%BeginPreview) -> 1-Bit-Hex-Vorschau
//  3. Ghostscript (falls installiert) -> Seite als PNG rendern

#include <windows.h>
#include <shlwapi.h>
#include <strsafe.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "render_common.h"
#include "renderers.h"

namespace {

DWORD ReadLE32(const unsigned char* p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<DWORD>(p[3]) << 24);
}

// --- WMF-Vorschau: in DIB rendern -------------------------------------------

HRESULT RenderWmfToHBitmap(const unsigned char* wmf, size_t wmfLen, UINT cx,
                           HBITMAP* phbmp)
{
    HENHMETAFILE emf = SetWinMetaFileBits(static_cast<UINT>(wmfLen), wmf,
                                          nullptr, nullptr);
    if (!emf)
        return HRESULT_FROM_WIN32(GetLastError());

    ENHMETAHEADER hdr = {};
    if (GetEnhMetaFileHeader(emf, sizeof(hdr), &hdr) == 0 ||
        hdr.rclFrame.right <= hdr.rclFrame.left ||
        hdr.rclFrame.bottom <= hdr.rclFrame.top) {
        DeleteEnhMetaFile(emf);
        return E_FAIL;
    }

    const double frameW = hdr.rclFrame.right - hdr.rclFrame.left; // 0,01 mm
    const double frameH = hdr.rclFrame.bottom - hdr.rclFrame.top;
    UINT dstW, dstH;
    if (frameW >= frameH) {
        dstW = cx;
        dstH = static_cast<UINT>(cx * frameH / frameW + 0.5);
    } else {
        dstH = cx;
        dstW = static_cast<UINT>(cx * frameW / frameH + 0.5);
    }
    if (dstW == 0) dstW = 1;
    if (dstH == 0) dstH = 1;

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth = static_cast<LONG>(dstW);
    bmi.bmiHeader.biHeight = -static_cast<LONG>(dstH);
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP hbmp = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!hbmp) {
        DeleteEnhMetaFile(emf);
        return E_OUTOFMEMORY;
    }

    HDC hdc = CreateCompatibleDC(nullptr);
    HGDIOBJ old = SelectObject(hdc, hbmp);
    RECT rc = { 0, 0, static_cast<LONG>(dstW), static_cast<LONG>(dstH) };
    FillRect(hdc, &rc, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
    BOOL ok = PlayEnhMetaFile(hdc, emf, &rc);
    SelectObject(hdc, old);
    DeleteDC(hdc);
    DeleteEnhMetaFile(emf);

    if (!ok) {
        DeleteObject(hbmp);
        return E_FAIL;
    }

    // Alpha auf opak setzen (GDI laesst den Alphakanal auf 0)
    unsigned char* px = static_cast<unsigned char*>(bits);
    for (size_t i = 3; i < static_cast<size_t>(dstW) * dstH * 4; i += 4)
        px[i] = 0xFF;

    *phbmp = hbmp;
    return S_OK;
}

// --- EPSI-Vorschau (%%BeginPreview: w h depth lines) -------------------------

const char* FindLine(const char* hay, size_t len, const char* needle)
{
    size_t n = strlen(needle);
    if (len < n)
        return nullptr;
    for (size_t i = 0; i + n <= len; ++i) {
        if (hay[i] == needle[0] && memcmp(hay + i, needle, n) == 0)
            return hay + i;
    }
    return nullptr;
}

HRESULT RenderEpsiToHBitmap(const char* data, size_t len, UINT cx, HBITMAP* phbmp)
{
    const char* begin = FindLine(data, len, "%%BeginPreview:");
    if (!begin)
        return E_FAIL;
    int w = 0, h = 0, depth = 0, lines = 0;
    if (sscanf_s(begin, "%%%%BeginPreview: %d %d %d %d", &w, &h, &depth, &lines) < 3 ||
        w <= 0 || h <= 0 || w > 4096 || h > 4096 || depth != 1)
        return E_FAIL;

    const char* end = data + len;
    const char* p = begin;
    while (p < end && *p != '\n') ++p; // hinter die BeginPreview-Zeile
    ++p;

    const size_t rowBytes = (static_cast<size_t>(w) + 7) / 8;
    std::vector<unsigned char> bits1(rowBytes * h, 0);
    size_t byteIdx = 0;
    int hi = -1;
    while (p < end && byteIdx < bits1.size()) {
        char c = *p++;
        int v;
        if (c >= '0' && c <= '9') v = c - '0';
        else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
        else {
            if (c == '%' && p < end && *p == '%')
                break; // %%EndPreview
            continue;
        }
        if (hi < 0) {
            hi = v;
        } else {
            bits1[byteIdx++] = static_cast<unsigned char>((hi << 4) | v);
            hi = -1;
        }
    }
    if (byteIdx < bits1.size() / 2)
        return E_FAIL; // zu wenig Daten

    // 1-Bit (1 = schwarz) -> 32-Bit-BGRA
    std::vector<unsigned char> rgba(static_cast<size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            bool black = (bits1[y * rowBytes + x / 8] >> (7 - (x & 7))) & 1;
            unsigned char* o = &rgba[(static_cast<size_t>(y) * w + x) * 4];
            o[0] = o[1] = o[2] = black ? 0 : 0xFF;
            o[3] = 0xFF;
        }
    }

    // In HBITMAP kopieren (mit Skalierung ueber WIC)
    Microsoft::WRL::ComPtr<IWICImagingFactory> wic;
    HRESULT hr = GetWicFactory(&wic);
    if (FAILED(hr))
        return hr;
    Microsoft::WRL::ComPtr<IWICBitmap> bmp;
    hr = wic->CreateBitmapFromMemory(w, h, GUID_WICPixelFormat32bppPBGRA,
                                     w * 4, static_cast<UINT>(rgba.size()),
                                     rgba.data(), &bmp);
    if (FAILED(hr))
        return hr;
    return WicSourceToHBitmap(wic.Get(), bmp.Get(), cx, phbmp);
}

// --- Ghostscript -------------------------------------------------------------

// Sucht gswin64c.exe: Registry (GPL/Artifex Ghostscript) und %ProgramFiles%\gs.
bool FindGhostscript(std::wstring& exeOut)
{
    for (const wchar_t* base : { L"SOFTWARE\\GPL Ghostscript",
                                 L"SOFTWARE\\Artifex Ghostscript" }) {
        HKEY hkey = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, base, 0,
                          KEY_READ | KEY_WOW64_64KEY, &hkey) != ERROR_SUCCESS)
            continue;
        wchar_t sub[128];
        for (DWORD i = 0;; ++i) {
            DWORD cch = ARRAYSIZE(sub);
            if (RegEnumKeyExW(hkey, i, sub, &cch, nullptr, nullptr, nullptr,
                              nullptr) != ERROR_SUCCESS)
                break;
            HKEY hver = nullptr;
            if (RegOpenKeyExW(hkey, sub, 0, KEY_READ | KEY_WOW64_64KEY, &hver) !=
                ERROR_SUCCESS)
                continue;
            wchar_t dllPath[MAX_PATH];
            DWORD cb = sizeof(dllPath);
            if (RegQueryValueExW(hver, L"GS_DLL", nullptr, nullptr,
                                 reinterpret_cast<BYTE*>(dllPath),
                                 &cb) == ERROR_SUCCESS) {
                std::wstring exe(dllPath);
                size_t slash = exe.find_last_of(L'\\');
                if (slash != std::wstring::npos) {
                    exe = exe.substr(0, slash + 1) + L"gswin64c.exe";
                    if (GetFileAttributesW(exe.c_str()) != INVALID_FILE_ATTRIBUTES) {
                        exeOut = exe;
                        RegCloseKey(hver);
                        RegCloseKey(hkey);
                        return true;
                    }
                }
            }
            RegCloseKey(hver);
        }
        RegCloseKey(hkey);
    }

    // Fallback: %ProgramFiles%\gs\gs*\bin\gswin64c.exe
    wchar_t pf[MAX_PATH];
    if (GetEnvironmentVariableW(L"ProgramFiles", pf, ARRAYSIZE(pf)) == 0)
        return false;
    std::wstring pattern = std::wstring(pf) + L"\\gs\\gs*";
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW(pattern.c_str(), &fd);
    if (find == INVALID_HANDLE_VALUE)
        return false;
    bool found = false;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            std::wstring exe = std::wstring(pf) + L"\\gs\\" + fd.cFileName +
                               L"\\bin\\gswin64c.exe";
            if (GetFileAttributesW(exe.c_str()) != INVALID_FILE_ATTRIBUTES) {
                exeOut = exe;
                found = true;
            }
        }
    } while (FindNextFileW(find, &fd));
    FindClose(find);
    return found;
}

} // namespace

HRESULT RenderViaGhostscript(const unsigned char* data, size_t len, UINT cx,
                             GsInput kind, HBITMAP* phbmp)
{
    if (!phbmp || !data || len == 0 || cx == 0)
        return E_INVALIDARG;
    *phbmp = nullptr;

    std::wstring gs;
    if (!FindGhostscript(gs))
        return E_FAIL;

    wchar_t tempDir[MAX_PATH];
    if (!GetTempPathW(ARRAYSIZE(tempDir), tempDir))
        return E_FAIL;

    wchar_t inFile[MAX_PATH], outFile[MAX_PATH];
    if (!GetTempFileNameW(tempDir, L"vth", 0, inFile))
        return E_FAIL;
    if (!GetTempFileNameW(tempDir, L"vtp", 0, outFile)) {
        DeleteFileW(inFile);
        return E_FAIL;
    }

    HRESULT hr = E_FAIL;
    HANDLE hIn = CreateFileW(inFile, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (hIn != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(hIn, data, static_cast<DWORD>(len), &written, nullptr);
        CloseHandle(hIn);
        if (written == len) {
            const wchar_t* extra = L"";
            if (kind == GsInput::Eps)
                extra = L"-dEPSCrop";
            else if (kind == GsInput::Pdf)
                extra = L"-dUseCropBox";
            wchar_t cmd[2048];
            StringCchPrintfW(cmd, ARRAYSIZE(cmd),
                             L"\"%s\" -dSAFER -dBATCH -dNOPAUSE -dQUIET "
                             L"-sDEVICE=png16m -r96 %s -dFirstPage=1 -dLastPage=1 "
                             L"-o \"%s\" \"%s\"",
                             gs.c_str(), extra, outFile, inFile);

            STARTUPINFOW si = { sizeof(si) };
            si.dwFlags = STARTF_USESHOWWINDOW;
            si.wShowWindow = SW_HIDE;
            PROCESS_INFORMATION pi = {};
            if (CreateProcessW(nullptr, cmd, nullptr, nullptr, FALSE,
                               CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
                if (WaitForSingleObject(pi.hProcess, 5000) != WAIT_OBJECT_0)
                    TerminateProcess(pi.hProcess, 1);
                DWORD exitCode = 1;
                GetExitCodeProcess(pi.hProcess, &exitCode);
                CloseHandle(pi.hThread);
                CloseHandle(pi.hProcess);

                if (exitCode == 0) {
                    HANDLE hOut = CreateFileW(outFile, GENERIC_READ,
                                              FILE_SHARE_READ, nullptr,
                                              OPEN_EXISTING, 0, nullptr);
                    if (hOut != INVALID_HANDLE_VALUE) {
                        LARGE_INTEGER size = {};
                        if (GetFileSizeEx(hOut, &size) && size.QuadPart > 0 &&
                            size.QuadPart < 64ll * 1024 * 1024) {
                            std::vector<unsigned char> png(
                                static_cast<size_t>(size.QuadPart));
                            DWORD read = 0;
                            if (ReadFile(hOut, png.data(),
                                         static_cast<DWORD>(png.size()), &read,
                                         nullptr) && read == png.size()) {
                                hr = DecodeImageBytesToHBitmap(
                                    png.data(), png.size(), cx, phbmp);
                            }
                        }
                        CloseHandle(hOut);
                    }
                }
            }
        }
    }
    DeleteFileW(inFile);
    DeleteFileW(outFile);
    return hr;
}

HRESULT RenderEpsToHBitmap(const unsigned char* data, size_t len, UINT cx,
                           HBITMAP* phbmp)
{
    if (!phbmp || !data || len < 32 || len > 0x7FFFFFFF || cx == 0)
        return E_INVALIDARG;
    *phbmp = nullptr;

    const unsigned char* psData = data;
    size_t psLen = len;
    bool isEps = true;

    // DOS-EPS-Binaerheader: TIFF- oder WMF-Vorschau bevorzugen
    if (data[0] == 0xC5 && data[1] == 0xD0 && data[2] == 0xD3 && data[3] == 0xC6) {
        const DWORD psOff = ReadLE32(data + 4);
        const DWORD psL = ReadLE32(data + 8);
        const DWORD wmfOff = ReadLE32(data + 12);
        const DWORD wmfLen = ReadLE32(data + 16);
        const DWORD tifOff = ReadLE32(data + 20);
        const DWORD tifLen = ReadLE32(data + 24);

        if (tifLen > 8 && tifOff + static_cast<ULONGLONG>(tifLen) <= len) {
            if (SUCCEEDED(DecodeImageBytesToHBitmap(data + tifOff, tifLen, cx, phbmp)))
                return S_OK;
        }
        if (wmfLen > 8 && wmfOff + static_cast<ULONGLONG>(wmfLen) <= len) {
            if (SUCCEEDED(RenderWmfToHBitmap(data + wmfOff, wmfLen, cx, phbmp)))
                return S_OK;
        }
        // Vorschau fehlt/kaputt -> eingebettetes PostScript fuer Ghostscript
        if (psL > 0 && psOff + static_cast<ULONGLONG>(psL) <= len) {
            psData = data + psOff;
            psLen = psL;
        }
    } else {
        // EPSI-Vorschau im Klartext?
        if (SUCCEEDED(RenderEpsiToHBitmap(reinterpret_cast<const char*>(data),
                                          len, cx, phbmp)))
            return S_OK;
        // reines PS (keine BoundingBox-Zuschneidung)?
        if (!FindLine(reinterpret_cast<const char*>(data),
                      len < 4096 ? len : 4096, "EPSF-"))
            isEps = false;
    }

    HRESULT hr = RenderViaGhostscript(psData, psLen, cx,
                                      isEps ? GsInput::Eps : GsInput::Ps, phbmp);
    if (SUCCEEDED(hr))
        return hr;
    // klassisches .ai (PostScript) ohne Ghostscript bzw. mit GS-Fehler:
    // eingebettete %AI7_Thumbnail-Vorschau versuchen
    return RenderAi7ThumbToHBitmap(data, len, cx, phbmp);
}
