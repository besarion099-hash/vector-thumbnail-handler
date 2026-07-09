#include "render_common.h"

#include <shlwapi.h>

using Microsoft::WRL::ComPtr;

HRESULT GetWicFactory(IWICImagingFactory** out)
{
    return CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                            IID_PPV_ARGS(out));
}

void FitSize(UINT srcW, UINT srcH, UINT cx, UINT* dstW, UINT* dstH)
{
    *dstW = srcW;
    *dstH = srcH;
    if (cx == 0 || srcW == 0 || srcH == 0)
        return;
    if (srcW > cx || srcH > cx) {
        if (srcW >= srcH) {
            *dstW = cx;
            *dstH = (srcH * cx + srcW / 2) / srcW;
        } else {
            *dstH = cx;
            *dstW = (srcW * cx + srcH / 2) / srcH;
        }
        if (*dstW == 0) *dstW = 1;
        if (*dstH == 0) *dstH = 1;
    }
}

HRESULT WicSourceToHBitmap(IWICImagingFactory* factory, IWICBitmapSource* src,
                           UINT cx, HBITMAP* phbmp)
{
    if (!factory || !src || !phbmp)
        return E_INVALIDARG;
    *phbmp = nullptr;

    UINT srcW = 0, srcH = 0;
    HRESULT hr = src->GetSize(&srcW, &srcH);
    if (FAILED(hr) || srcW == 0 || srcH == 0)
        return FAILED(hr) ? hr : E_FAIL;

    UINT dstW, dstH;
    FitSize(srcW, srcH, cx, &dstW, &dstH);

    ComPtr<IWICBitmapSource> scaled;
    if (dstW != srcW || dstH != srcH) {
        ComPtr<IWICBitmapScaler> scaler;
        hr = factory->CreateBitmapScaler(&scaler);
        if (FAILED(hr))
            return hr;
        hr = scaler->Initialize(src, dstW, dstH, WICBitmapInterpolationModeFant);
        if (FAILED(hr))
            return hr;
        scaled = scaler;
    } else {
        scaled = src;
    }

    ComPtr<IWICFormatConverter> converter;
    hr = factory->CreateFormatConverter(&converter);
    if (FAILED(hr))
        return hr;
    hr = converter->Initialize(scaled.Get(), GUID_WICPixelFormat32bppPBGRA,
                               WICBitmapDitherTypeNone, nullptr, 0.0,
                               WICBitmapPaletteTypeCustom);
    if (FAILED(hr))
        return hr;

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth = static_cast<LONG>(dstW);
    bmi.bmiHeader.biHeight = -static_cast<LONG>(dstH); // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP hbmp = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!hbmp || !bits) {
        if (hbmp)
            DeleteObject(hbmp);
        return E_OUTOFMEMORY;
    }

    const UINT stride = dstW * 4;
    hr = converter->CopyPixels(nullptr, stride, stride * dstH,
                               static_cast<BYTE*>(bits));
    if (FAILED(hr)) {
        DeleteObject(hbmp);
        return hr;
    }

    *phbmp = hbmp;
    return S_OK;
}

HRESULT DecodeImageBytesToHBitmap(const unsigned char* data, size_t len,
                                  UINT cx, HBITMAP* phbmp)
{
    if (!phbmp || !data || len == 0 || len > 0x7FFFFFFF)
        return E_INVALIDARG;
    *phbmp = nullptr;

    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = GetWicFactory(&factory);
    if (FAILED(hr))
        return hr;

    ComPtr<IStream> stream(SHCreateMemStream(data, static_cast<UINT>(len)));
    if (!stream)
        return E_OUTOFMEMORY;

    ComPtr<IWICBitmapDecoder> decoder;
    hr = factory->CreateDecoderFromStream(stream.Get(), nullptr,
                                          WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr))
        return hr;

    ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr))
        return hr;

    return WicSourceToHBitmap(factory.Get(), frame.Get(), cx, phbmp);
}

HRESULT CreateWicRenderBitmap(IWICImagingFactory* factory, UINT w, UINT h,
                              IWICBitmap** out)
{
    if (!factory || !out || w == 0 || h == 0 || w > 8192 || h > 8192)
        return E_INVALIDARG;
    return factory->CreateBitmap(w, h, GUID_WICPixelFormat32bppPBGRA,
                                 WICBitmapCacheOnDemand, out);
}

bool IsMostlyBlankBitmap(HBITMAP hbmp)
{
    if (!hbmp)
        return true;
    DIBSECTION ds = {};
    if (GetObjectW(hbmp, sizeof(ds), &ds) != sizeof(ds) || !ds.dsBm.bmBits ||
        ds.dsBm.bmBitsPixel != 32)
        return false; // unbekanntes Format: nicht als leer werten

    const size_t count = static_cast<size_t>(ds.dsBm.bmWidth) *
                         static_cast<size_t>(abs(ds.dsBm.bmHeight));
    const unsigned char* px = static_cast<const unsigned char*>(ds.dsBm.bmBits);
    size_t nonBlank = 0;
    const size_t threshold = count / 2000 + 1; // >0,05 % sichtbare Pixel
    for (size_t i = 0; i < count; ++i) {
        const unsigned char b = px[i * 4 + 0];
        const unsigned char g = px[i * 4 + 1];
        const unsigned char r = px[i * 4 + 2];
        const unsigned char a = px[i * 4 + 3];
        // sichtbar = nicht transparent und nicht (nahezu) weiss
        if (a > 16 && (r < 240 || g < 240 || b < 240)) {
            if (++nonBlank >= threshold)
                return false;
        }
    }
    return true;
}
