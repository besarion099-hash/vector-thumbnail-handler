// Dekodiert die in .ai-Dateien eingebettete Vorschau (%AI7_Thumbnail).
// Format lt. Adobe "AI7 File Format": Breite Hoehe Bittiefe, dann Hex-Daten:
// bei 8 Bit zuerst eine 256er-RGB-Palette (768 Bytes), danach entweder
// "RLE" + komprimierte Pixel (FD-Escape) oder rohe Pixelindizes.

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cstdio>
#include <cstring>
#include <vector>

#include "render_common.h"
#include "renderers.h"

using Microsoft::WRL::ComPtr;

namespace {

const unsigned char* FindMarker(const unsigned char* hay, size_t len,
                                const char* needle)
{
    const size_t n = strlen(needle);
    if (len < n)
        return nullptr;
    for (size_t i = 0; i + n <= len; ++i) {
        if (hay[i] == static_cast<unsigned char>(needle[0]) &&
            memcmp(hay + i, needle, n) == 0)
            return hay + i;
    }
    return nullptr;
}

int HexVal(unsigned char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// FD-Escape-RLE: FD FD -> literal FD; FD n v -> n-mal v
bool RleDecode(const unsigned char* src, size_t len, std::vector<unsigned char>& out,
               size_t maxOut)
{
    size_t i = 0;
    while (i < len && out.size() < maxOut) {
        unsigned char b = src[i];
        if (b == 0xFD) {
            if (i + 1 >= len)
                break;
            if (src[i + 1] == 0xFD) {
                out.push_back(0xFD);
                i += 2;
            } else {
                if (i + 2 >= len)
                    break;
                const unsigned char n = src[i + 1];
                const unsigned char v = src[i + 2];
                for (unsigned char k = 0; k < n && out.size() < maxOut; ++k)
                    out.push_back(v);
                i += 3;
            }
        } else {
            out.push_back(b);
            ++i;
        }
    }
    return out.size() >= maxOut;
}

} // namespace

HRESULT RenderAi7ThumbToHBitmap(const unsigned char* data, size_t len, UINT cx,
                                HBITMAP* phbmp)
{
    if (!phbmp || !data || len == 0 || cx == 0)
        return E_INVALIDARG;
    *phbmp = nullptr;

    const unsigned char* marker = FindMarker(data, len, "%AI7_Thumbnail:");
    if (!marker)
        return E_FAIL;
    const size_t rest = len - static_cast<size_t>(marker - data);

    int w = 0, h = 0, depth = 0;
    if (sscanf_s(reinterpret_cast<const char*>(marker),
                 "%%AI7_Thumbnail: %d %d %d", &w, &h, &depth) != 3 ||
        w <= 0 || h <= 0 || w > 2048 || h > 2048 || depth != 8)
        return E_FAIL;

    const unsigned char* begin = FindMarker(marker, rest, "%%BeginData");
    const unsigned char* end = FindMarker(marker, rest, "%%EndData");
    if (!begin || !end || end <= begin)
        return E_FAIL;
    while (begin < end && *begin != '\n')
        ++begin;

    // Hex-Bytes einsammeln (Zeilenprefix '%', Whitespace usw. ignorieren)
    std::vector<unsigned char> raw;
    raw.reserve(static_cast<size_t>(end - begin) / 2);
    int hi = -1;
    for (const unsigned char* p = begin; p < end; ++p) {
        const int v = HexVal(*p);
        if (v < 0)
            continue;
        if (hi < 0) {
            hi = v;
        } else {
            raw.push_back(static_cast<unsigned char>((hi << 4) | v));
            hi = -1;
        }
    }
    if (raw.size() < 768 + 3)
        return E_FAIL;

    // 256er-Palette (roh), danach optional "RLE" + komprimierte Indizes
    const unsigned char* pal = raw.data();
    const unsigned char* px = raw.data() + 768;
    size_t pxLen = raw.size() - 768;
    const size_t total = static_cast<size_t>(w) * h;

    std::vector<unsigned char> indices;
    if (pxLen >= 3 && memcmp(px, "RLE", 3) == 0) {
        indices.reserve(total);
        RleDecode(px + 3, pxLen - 3, indices, total);
    } else {
        indices.assign(px, px + (pxLen < total ? pxLen : total));
    }
    if (indices.size() < total / 2)
        return E_FAIL; // zu unvollstaendig
    indices.resize(total, 0);

    // Indizes -> BGRA
    std::vector<unsigned char> bgra(total * 4);
    for (size_t i = 0; i < total; ++i) {
        const unsigned char idx = indices[i];
        bgra[i * 4 + 0] = pal[idx * 3 + 2];
        bgra[i * 4 + 1] = pal[idx * 3 + 1];
        bgra[i * 4 + 2] = pal[idx * 3 + 0];
        bgra[i * 4 + 3] = 0xFF;
    }

    ComPtr<IWICImagingFactory> wic;
    HRESULT hr = GetWicFactory(&wic);
    if (FAILED(hr))
        return hr;
    ComPtr<IWICBitmap> bmp;
    hr = wic->CreateBitmapFromMemory(w, h, GUID_WICPixelFormat32bppPBGRA, w * 4,
                                     static_cast<UINT>(bgra.size()), bgra.data(),
                                     &bmp);
    if (FAILED(hr))
        return hr;
    return WicSourceToHBitmap(wic.Get(), bmp.Get(), cx, phbmp);
}
