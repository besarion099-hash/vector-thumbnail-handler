// CorelDRAW-Miniaturen aus der eingebetteten Vorschau.
//  - neue .cdr (X4+): ZIP-Container -> metadata/thumbnails/thumbnail.bmp
//  - alte .cdr: RIFF-Container -> DISP-Chunk (CF_DIB) mit Vorschau-Bitmap
// In beiden Faellen wird die Bitmap an WIC uebergeben; kein CDR-Rendering.

#include <windows.h>

#include <cstring>
#include <vector>

#include "miniz.h"
#include "render_common.h"
#include "renderers.h"

namespace {

DWORD ReadLE32(const unsigned char* p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<DWORD>(p[3]) << 24);
}

// Baut aus einer nackten DIB (BITMAPINFOHEADER + Palette + Pixel) eine
// vollstaendige BMP-Datei, damit WIC sie dekodieren kann.
bool DibToBmpFile(const unsigned char* dib, size_t dibLen,
                  std::vector<unsigned char>& out)
{
    if (dibLen < 40)
        return false;
    const DWORD headerSize = ReadLE32(dib);
    if (headerSize < 40 || headerSize > dibLen)
        return false;

    const WORD bitCount = dib[14] | (dib[15] << 8);
    DWORD clrUsed = ReadLE32(dib + 32);
    const DWORD compression = ReadLE32(dib + 16);

    // Farbtabelle bestimmen (nur unkomprimiert / BI_RGB relevant)
    if (clrUsed == 0 && bitCount <= 8)
        clrUsed = 1u << bitCount;
    DWORD paletteBytes = clrUsed * 4;
    // Bei BI_BITFIELDS stehen 3 DWORD-Masken vor den Pixeldaten
    if (compression == 3 /*BI_BITFIELDS*/)
        paletteBytes += 12;

    const DWORD pixelOffset = 14 + headerSize + paletteBytes;

    out.resize(14 + dibLen);
    out[0] = 'B';
    out[1] = 'M';
    const DWORD fileSize = 14 + static_cast<DWORD>(dibLen);
    out[2] = fileSize & 0xFF;
    out[3] = (fileSize >> 8) & 0xFF;
    out[4] = (fileSize >> 16) & 0xFF;
    out[5] = (fileSize >> 24) & 0xFF;
    out[6] = out[7] = out[8] = out[9] = 0;
    out[10] = pixelOffset & 0xFF;
    out[11] = (pixelOffset >> 8) & 0xFF;
    out[12] = (pixelOffset >> 16) & 0xFF;
    out[13] = (pixelOffset >> 24) & 0xFF;
    memcpy(out.data() + 14, dib, dibLen);
    return true;
}

// Chunk-Liste [start,end) nach einem DISP-Chunk (CF_DIB-Vorschau) durchsuchen;
// steigt in RIFF/LIST-Container hinab (max. Tiefe gegen Endlosschleifen).
HRESULT ScanRiffChunks(const unsigned char* data, size_t start, size_t end,
                       UINT cx, HBITMAP* phbmp, int depth)
{
    if (depth > 6)
        return E_FAIL;
    size_t pos = start;
    while (pos + 8 <= end) {
        const unsigned char* c = data + pos;
        const DWORD sz = ReadLE32(c + 4);
        const size_t body = pos + 8;
        if (body > end)
            break;
        const size_t avail = (sz <= end - body) ? sz : (end - body);

        if (memcmp(c, "DISP", 4) == 0 && avail > 4) {
            const DWORD cf = ReadLE32(data + body);
            if (cf == 8 /*CF_DIB*/ || cf == 17 /*CF_DIBV5*/) {
                std::vector<unsigned char> bmp;
                if (DibToBmpFile(data + body + 4, avail - 4, bmp) &&
                    SUCCEEDED(DecodeImageBytesToHBitmap(bmp.data(), bmp.size(),
                                                        cx, phbmp)))
                    return S_OK;
            }
        } else if ((memcmp(c, "LIST", 4) == 0 || memcmp(c, "RIFF", 4) == 0) &&
                   avail >= 4) {
            // Container: 4-Byte-Typkennung, dann Unterchunks
            if (SUCCEEDED(ScanRiffChunks(data, body + 4, body + avail, cx, phbmp,
                                         depth + 1)))
                return S_OK;
        }

        pos = body + sz + (sz & 1); // Chunks sind wortweise ausgerichtet
    }
    return E_FAIL;
}

HRESULT RenderRiffCdr(const unsigned char* data, size_t len, UINT cx,
                      HBITMAP* phbmp)
{
    // Hinter "RIFF"<size>"CDR?" beginnen die Unterchunks bei Offset 12.
    return ScanRiffChunks(data, 12, len, cx, phbmp, 0);
}

// ZIP-Container (X4+): bevorzugt thumbnail.bmp, sonst page1.bmp.
HRESULT RenderZipCdr(const unsigned char* data, size_t len, UINT cx,
                     HBITMAP* phbmp)
{
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    if (!mz_zip_reader_init_mem(&zip, data, len, 0))
        return E_FAIL;

    HRESULT hr = E_FAIL;
    const char* candidates[] = {
        "metadata/thumbnails/thumbnail.bmp",
        "metadata/thumbnails/page1.bmp",
        "previews/thumbnail.png",
    };
    for (const char* name : candidates) {
        int idx = mz_zip_reader_locate_file(&zip, name, nullptr, 0);
        if (idx < 0)
            continue;
        size_t outLen = 0;
        void* p = mz_zip_reader_extract_to_heap(&zip, idx, &outLen, 0);
        if (p) {
            if (outLen > 0 && outLen <= 64u * 1024 * 1024)
                hr = DecodeImageBytesToHBitmap(static_cast<unsigned char*>(p),
                                               outLen, cx, phbmp);
            mz_free(p);
        }
        if (SUCCEEDED(hr))
            break;
    }
    mz_zip_reader_end(&zip);
    return hr;
}

} // namespace

HRESULT RenderCdrToHBitmap(const unsigned char* data, size_t len, UINT cx,
                           HBITMAP* phbmp)
{
    if (!phbmp || !data || len < 16 || len > 0x7FFFFFFF || cx == 0)
        return E_INVALIDARG;
    *phbmp = nullptr;

    if (data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F')
        return RenderRiffCdr(data, len, cx, phbmp);
    if (data[0] == 'P' && data[1] == 'K')
        return RenderZipCdr(data, len, cx, phbmp);
    return E_FAIL;
}
