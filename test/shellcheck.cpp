// shellcheck.exe — fordert die Miniatur ueber die Windows-Shell an
// (IShellItemImageFactory, derselbe Mechanismus wie der Explorer) und
// speichert sie als PNG. Beweist, dass die Registrierung greift.
// Aufruf: shellcheck.exe <datei.xcs> <out.png>

#include <windows.h>
#include <shobjidl.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <cstdio>

using Microsoft::WRL::ComPtr;

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
        wprintf(L"Aufruf: %s <datei.xcs> <out.png>\n", argv[0]);
        return 2;
    }
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) return 1;

    int rc = 1;
    {
        ComPtr<IShellItemImageFactory> factory;
        hr = SHCreateItemFromParsingName(argv[1], nullptr, IID_PPV_ARGS(&factory));
        if (FAILED(hr)) {
            wprintf(L"FEHLER: SHCreateItemFromParsingName 0x%08X\n", hr);
        } else {
            SIZE size = { 256, 256 };
            HBITMAP hbmp = nullptr;
            // THUMBNAILONLY: nur echter Thumbnail-Handler, kein Icon-Fallback.
            hr = factory->GetImage(size, SIIGBF_THUMBNAILONLY | SIIGBF_BIGGERSIZEOK,
                                   &hbmp);
            if (FAILED(hr)) {
                wprintf(L"FEHLER: GetImage 0x%08X (kein Thumbnail-Handler aktiv?)\n", hr);
            } else {
                BITMAP bm = {};
                GetObjectW(hbmp, sizeof(bm), &bm);
                wprintf(L"Shell lieferte Thumbnail: %ldx%ld\n", bm.bmWidth, bm.bmHeight);
                hr = SaveHBitmapAsPng(hbmp, argv[2]);
                if (SUCCEEDED(hr)) {
                    wprintf(L"Gespeichert: %s\n", argv[2]);
                    rc = 0;
                } else {
                    wprintf(L"FEHLER: PNG-Export 0x%08X\n", hr);
                }
                DeleteObject(hbmp);
            }
        }
    }
    CoUninitialize();
    return rc;
}
