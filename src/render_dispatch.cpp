// Zentrale Weiche: gunzip (SVGZ) + Formaterkennung + Dispatch.

#include <windows.h>

#include <vector>

#include "renderers.h"
#include "sniff.h"

static const size_t kMaxInflated = 256ull * 1024 * 1024;

HRESULT RenderVectorThumbnail(const unsigned char* data, size_t len, UINT cx,
                              HBITMAP* phbmp)
{
    if (!phbmp || !data || len == 0 || cx == 0)
        return E_INVALIDARG;
    *phbmp = nullptr;

    std::vector<char> buf(reinterpret_cast<const char*>(data),
                          reinterpret_cast<const char*>(data) + len);
    if (!GunzipIfNeeded(buf, kMaxInflated))
        return E_FAIL;

    switch (SniffFormat(buf)) {
        case VecFormat::Svg:
            return RenderSvgToHBitmap(buf.data(), buf.size(), cx, phbmp);
        case VecFormat::Pdf: {
            // Windows-PDF-Engine zuerst; Illustrator-Dateien mit Ebenen (OCG)
            // rendert sie oft leer -> dann Ghostscript-Fallback.
            HRESULT hr = RenderPdfToHBitmap(
                reinterpret_cast<const unsigned char*>(buf.data()), buf.size(),
                cx, phbmp);
            if (SUCCEEDED(hr) && IsMostlyBlankBitmap(*phbmp)) {
                DeleteObject(*phbmp);
                *phbmp = nullptr;
                hr = E_FAIL;
            }
            if (FAILED(hr)) {
                HRESULT hrGs = RenderViaGhostscript(
                    reinterpret_cast<const unsigned char*>(buf.data()),
                    buf.size(), cx, GsInput::Pdf, phbmp);
                if (SUCCEEDED(hrGs) && IsMostlyBlankBitmap(*phbmp)) {
                    DeleteObject(*phbmp);
                    *phbmp = nullptr;
                    hrGs = E_FAIL;
                }
                if (SUCCEEDED(hrGs))
                    return hrGs;
                // letzter Ausweg: eingebettete AI-Vorschau (CorelDRAW-Exporte,
                // "ohne PDF-Inhalt" gespeicherte Illustrator-Dateien)
                HRESULT hrThumb = RenderAi7ThumbToHBitmap(
                    reinterpret_cast<const unsigned char*>(buf.data()),
                    buf.size(), cx, phbmp);
                if (SUCCEEDED(hrThumb))
                    return hrThumb;
            }
            return hr;
        }
        case VecFormat::DosEps:
        case VecFormat::PsText:
            return RenderEpsToHBitmap(
                reinterpret_cast<const unsigned char*>(buf.data()), buf.size(),
                cx, phbmp);
        case VecFormat::Dxf:
            return RenderDxfToHBitmap(buf.data(), buf.size(), cx, phbmp);
        case VecFormat::Cdr:
            return RenderCdrToHBitmap(
                reinterpret_cast<const unsigned char*>(buf.data()), buf.size(),
                cx, phbmp);
        case VecFormat::Xcs:
            return RenderXcsToHBitmap(buf.data(), buf.size(), cx, phbmp);
        case VecFormat::Lbrn:
            return RenderLbrnToHBitmap(buf.data(), buf.size(), cx, phbmp);
        case VecFormat::Xs:
            return RenderXsToHBitmap(
                reinterpret_cast<const unsigned char*>(buf.data()), buf.size(),
                cx, phbmp);
        default:
            return E_FAIL;
    }
}
