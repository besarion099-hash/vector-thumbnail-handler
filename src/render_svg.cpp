// SVG-Rendering ueber die Windows-eigene Direct2D-SVG-Engine (Win10 1703+).

#include <windows.h>
#include <d2d1_3.h>
#include <shlwapi.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cmath>
#include <cstddef>

#include "render_common.h"
#include "renderers.h"

using Microsoft::WRL::ComPtr;

namespace {

// Liest die natuerliche Groesse des SVG aus width/height bzw. viewBox.
void GetSvgIntrinsicSize(ID2D1SvgDocument* doc, float* w, float* h)
{
    *w = 0;
    *h = 0;

    ComPtr<ID2D1SvgElement> root;
    doc->GetRoot(&root);
    if (!root)
        return;

    D2D1_SVG_LENGTH len = {};
    if (SUCCEEDED(root->GetAttributeValue(L"width", &len)) && len.value > 0 &&
        len.units == D2D1_SVG_LENGTH_UNITS_NUMBER)
        *w = len.value;
    if (SUCCEEDED(root->GetAttributeValue(L"height", &len)) && len.value > 0 &&
        len.units == D2D1_SVG_LENGTH_UNITS_NUMBER)
        *h = len.value;

    if (*w <= 0 || *h <= 0) {
        D2D1_SVG_VIEWBOX vb = {};
        if (SUCCEEDED(root->GetAttributeValue(
                L"viewBox", D2D1_SVG_ATTRIBUTE_POD_TYPE_VIEWBOX,
                &vb, sizeof(vb))) &&
            vb.width > 0 && vb.height > 0) {
            if (*w <= 0 && *h <= 0) {
                *w = vb.width;
                *h = vb.height;
            } else if (*w <= 0) {
                *w = *h * vb.width / vb.height;
            } else {
                *h = *w * vb.height / vb.width;
            }
        }
    }
}

} // namespace

HRESULT RenderSvgToHBitmap(const char* data, size_t len, UINT cx, HBITMAP* phbmp)
{
    if (!phbmp || !data || len == 0 || len > 0x7FFFFFFF || cx == 0)
        return E_INVALIDARG;
    *phbmp = nullptr;

    ComPtr<IWICImagingFactory> wic;
    HRESULT hr = GetWicFactory(&wic);
    if (FAILED(hr))
        return hr;

    ComPtr<ID2D1Factory> d2dFactory;
    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                           d2dFactory.GetAddressOf());
    if (FAILED(hr))
        return hr;

    // Erst mit Minimalziel ein SVG-Dokument erzeugen, um die natuerliche
    // Groesse zu ermitteln; dann in der passenden Zielgroesse rendern.
    ComPtr<IStream> stream(SHCreateMemStream(
        reinterpret_cast<const BYTE*>(data), static_cast<UINT>(len)));
    if (!stream)
        return E_OUTOFMEMORY;

    ComPtr<IWICBitmap> probeBmp;
    hr = CreateWicRenderBitmap(wic.Get(), 16, 16, &probeBmp);
    if (FAILED(hr))
        return hr;

    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_SOFTWARE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));

    ComPtr<ID2D1RenderTarget> probeRt;
    hr = d2dFactory->CreateWicBitmapRenderTarget(probeBmp.Get(), props, &probeRt);
    if (FAILED(hr))
        return hr;

    ComPtr<ID2D1DeviceContext5> probeDc;
    hr = probeRt.As(&probeDc);
    if (FAILED(hr))
        return hr; // Windows zu alt fuer D2D-SVG

    ComPtr<ID2D1SvgDocument> probeDoc;
    hr = probeDc->CreateSvgDocument(stream.Get(), D2D1::SizeF(512, 512), &probeDoc);
    if (FAILED(hr))
        return hr;

    float svgW = 0, svgH = 0;
    GetSvgIntrinsicSize(probeDoc.Get(), &svgW, &svgH);
    if (svgW <= 0 || svgH <= 0) {
        svgW = 512;
        svgH = 512;
    }

    // Zielgroesse: einpassen in cx*cx (Vektor -> Hochskalieren erlaubt)
    UINT dstW, dstH;
    if (svgW >= svgH) {
        dstW = cx;
        dstH = static_cast<UINT>(cx * svgH / svgW + 0.5f);
    } else {
        dstH = cx;
        dstW = static_cast<UINT>(cx * svgW / svgH + 0.5f);
    }
    if (dstW == 0) dstW = 1;
    if (dstH == 0) dstH = 1;

    ComPtr<IWICBitmap> bmp;
    hr = CreateWicRenderBitmap(wic.Get(), dstW, dstH, &bmp);
    if (FAILED(hr))
        return hr;

    ComPtr<ID2D1RenderTarget> rt;
    hr = d2dFactory->CreateWicBitmapRenderTarget(bmp.Get(), props, &rt);
    if (FAILED(hr))
        return hr;

    ComPtr<ID2D1DeviceContext5> dc;
    hr = rt.As(&dc);
    if (FAILED(hr))
        return hr;

    // Stream zuruecksetzen und Dokument in Zielaufloesung neu laden.
    LARGE_INTEGER zero = {};
    stream->Seek(zero, STREAM_SEEK_SET, nullptr);
    ComPtr<ID2D1SvgDocument> doc;
    hr = dc->CreateSvgDocument(stream.Get(), D2D1::SizeF(svgW, svgH), &doc);
    if (FAILED(hr))
        return hr;

    dc->BeginDraw();
    dc->Clear(D2D1::ColorF(0, 0, 0, 0)); // transparent
    dc->SetTransform(D2D1::Matrix3x2F::Scale(dstW / svgW, dstH / svgH));
    dc->DrawSvgDocument(doc.Get());
    hr = dc->EndDraw();
    if (FAILED(hr))
        return hr;

    return WicSourceToHBitmap(wic.Get(), bmp.Get(), 0, phbmp);
}
