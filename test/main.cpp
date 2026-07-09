// vecthumb-test.exe — prueft die Render-Pipeline ohne Explorer/COM-Registrierung.
// Aufruf: vecthumb-test.exe <datei> <out.png> [groesse]

#include <windows.h>
#include <shlwapi.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cstdio>
#include <vector>

#include "../src/renderers.h"

using Microsoft::WRL::ComPtr;

static bool ReadFileBytes(const wchar_t* path, std::vector<unsigned char>& out)
{
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(h, &size) || size.QuadPart <= 0 ||
        size.QuadPart > 0x7FFFFFFF) {
        CloseHandle(h);
        return false;
    }
    out.resize(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    BOOL ok = ReadFile(h, out.data(), static_cast<DWORD>(out.size()), &read, nullptr);
    CloseHandle(h);
    return ok && read == out.size();
}

static HRESULT SaveHBitmapAsPng(HBITMAP hbmp, const wchar_t* path)
{
    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                  CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) return hr;
    ComPtr<IWICBitmap> bitmap;
    hr = factory->CreateBitmapFromHBITMAP(hbmp, nullptr,
                                          WICBitmapUsePremultipliedAlpha, &bitmap);
    if (FAILED(hr)) return hr;
    ComPtr<IWICStream> stream;
    hr = factory->CreateStream(&stream);
    if (FAILED(hr)) return hr;
    hr = stream->InitializeFromFilename(path, GENERIC_WRITE);
    if (FAILED(hr)) return hr;
    ComPtr<IWICBitmapEncoder> encoder;
    hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    if (FAILED(hr)) return hr;
    hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (FAILED(hr)) return hr;
    ComPtr<IWICBitmapFrameEncode> frame;
    hr = encoder->CreateNewFrame(&frame, nullptr);
    if (FAILED(hr)) return hr;
    hr = frame->Initialize(nullptr);
    if (FAILED(hr)) return hr;
    hr = frame->WriteSource(bitmap.Get(), nullptr);
    if (FAILED(hr)) return hr;
    hr = frame->Commit();
    if (FAILED(hr)) return hr;
    return encoder->Commit();
}

int wmain(int argc, wchar_t** argv)
{
    if (argc < 3) {
        wprintf(L"Aufruf: %s <datei> <out.png> [groesse]\n", argv[0]);
        return 2;
    }

    std::vector<unsigned char> content;
    if (!ReadFileBytes(argv[1], content)) {
        wprintf(L"FEHLER: Datei nicht lesbar: %s\n", argv[1]);
        return 1;
    }
    wprintf(L"Datei gelesen: %zu Bytes\n", content.size());

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) {
        wprintf(L"FEHLER: CoInitializeEx 0x%08X\n", hr);
        return 1;
    }

    int rc = 0;
    UINT cx = (argc >= 4) ? static_cast<UINT>(_wtoi(argv[3])) : 256;
    if (cx == 0)
        cx = 256;

    HBITMAP hbmp = nullptr;
    hr = RenderVectorThumbnail(content.data(), content.size(), cx, &hbmp);
    if (FAILED(hr)) {
        wprintf(L"FEHLER: RenderVectorThumbnail 0x%08X\n", hr);
        rc = 1;
    } else {
        BITMAP bm = {};
        GetObjectW(hbmp, sizeof(bm), &bm);
        wprintf(L"HBITMAP erzeugt: %ldx%ld\n", bm.bmWidth, bm.bmHeight);
        hr = SaveHBitmapAsPng(hbmp, argv[2]);
        if (FAILED(hr)) {
            wprintf(L"FEHLER: PNG-Export 0x%08X\n", hr);
            rc = 1;
        } else {
            wprintf(L"Gespeichert: %s\n", argv[2]);
        }
        DeleteObject(hbmp);
    }

    CoUninitialize();
    return rc;
}
