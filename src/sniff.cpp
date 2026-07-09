#include "sniff.h"

#include <cstring>

#include "miniz.h"

namespace {

bool StartsWith(const std::vector<char>& d, const char* prefix)
{
    size_t n = strlen(prefix);
    return d.size() >= n && memcmp(d.data(), prefix, n) == 0;
}

// Sucht needle in den ersten limit Bytes (case-sensitiv).
bool ContainsInHead(const std::vector<char>& d, const char* needle, size_t limit)
{
    size_t n = strlen(needle);
    size_t end = d.size() < limit ? d.size() : limit;
    if (end < n)
        return false;
    for (size_t i = 0; i + n <= end; ++i) {
        if (d[i] == needle[0] && memcmp(d.data() + i, needle, n) == 0)
            return true;
    }
    return false;
}

} // namespace

VecFormat SniffFormat(const std::vector<char>& data)
{
    if (data.size() < 8)
        return VecFormat::Unknown;

    const unsigned char* u = reinterpret_cast<const unsigned char*>(data.data());

    // DOS-EPS-Binaerheader
    if (u[0] == 0xC5 && u[1] == 0xD0 && u[2] == 0xD3 && u[3] == 0xC6)
        return VecFormat::DosEps;

    // CorelDRAW, alte Variante: RIFF....CDR*
    if (u[0] == 'R' && u[1] == 'I' && u[2] == 'F' && u[3] == 'F' &&
        data.size() >= 12 && u[8] == 'C' && u[9] == 'D' && u[10] == 'R')
        return VecFormat::Cdr;

    // CorelDRAW, neue Variante: ZIP-Container. Wie bei ODF steht ein
    // "mimetype"-Eintrag ganz vorne; sein Wert identifiziert CorelDRAW.
    // (Die metadata/-Eintraege liegen zu weit hinten fuer eine Kopf-Suche.)
    if (u[0] == 'P' && u[1] == 'K' && u[2] == 0x03 && u[3] == 0x04 &&
        (ContainsInHead(data, "corel.draw", 256) ||
         ContainsInHead(data, "metadata/thumbnails/", 1u << 20) ||
         ContainsInHead(data, "content/root.dat", 1u << 20)))
        return VecFormat::Cdr;

    // %PDF darf ein paar Junk-Bytes davor haben
    if (ContainsInHead(data, "%PDF-", 1024))
        return VecFormat::Pdf;

    if (StartsWith(data, "%!PS") || ContainsInHead(data, "%!PS-Adobe", 256))
        return VecFormat::PsText;

    // xTool Studio: JSON (beginnt mit '{') mit eingebettetem "cover"-Feld
    {
        size_t i = 0;
        while (i < data.size() && i < 8 &&
               (data[i] == ' ' || data[i] == '\t' || data[i] == '\r' ||
                data[i] == '\n' || static_cast<unsigned char>(data[i]) == 0xEF ||
                static_cast<unsigned char>(data[i]) == 0xBB ||
                static_cast<unsigned char>(data[i]) == 0xBF))
            ++i;
        // "cover" steht in .xcs oft erst nach dem grossen canvas-Array ->
        // gesamten Inhalt durchsuchen (nur wenn es ueberhaupt JSON ist).
        if (i < data.size() && data[i] == '{' &&
            ContainsInHead(data, "\"cover\"", data.size()))
            return VecFormat::Xcs;
    }

    // LightBurn: XML mit <LightBurnProject-Wurzel (vor der SVG-Pruefung,
    // da beides XML ist)
    if (ContainsInHead(data, "<LightBurnProject", 4096))
        return VecFormat::Lbrn;

    // SVG: <svg irgendwo im Kopf (nach XML-Deklaration/Kommentaren/BOM)
    if (ContainsInHead(data, "<svg", 4096))
        return VecFormat::Svg;

    // DXF (Text): Gruppencode 0 + "SECTION" im Kopf, z. B. "  0\r\nSECTION".
    if (ContainsInHead(data, "SECTION", 256))
        return VecFormat::Dxf;
    if (StartsWith(data, "AutoCAD Binary DXF"))
        return VecFormat::Unknown; // Binaer-DXF nicht unterstuetzt

    return VecFormat::Unknown;
}

bool GunzipIfNeeded(std::vector<char>& data, size_t maxOut)
{
    if (data.size() < 18)
        return true;
    const unsigned char* u = reinterpret_cast<const unsigned char*>(data.data());
    if (u[0] != 0x1F || u[1] != 0x8B)
        return true; // kein gzip
    if (u[2] != 8)
        return false; // nur deflate

    const unsigned char flags = u[3];
    size_t pos = 10;
    const size_t n = data.size();

    if (flags & 0x04) { // FEXTRA
        if (pos + 2 > n) return false;
        size_t xlen = u[pos] | (u[pos + 1] << 8);
        pos += 2 + xlen;
    }
    if (flags & 0x08) { // FNAME
        while (pos < n && u[pos] != 0) ++pos;
        ++pos;
    }
    if (flags & 0x10) { // FCOMMENT
        while (pos < n && u[pos] != 0) ++pos;
        ++pos;
    }
    if (flags & 0x02) // FHCRC
        pos += 2;
    if (pos >= n)
        return false;

    // 8 Byte Trailer (CRC32 + ISIZE) am Ende
    if (n < pos + 8)
        return false;
    size_t compLen = n - pos - 8;

    size_t outLen = 0;
    void* out = tinfl_decompress_mem_to_heap(data.data() + pos, compLen, &outLen, 0);
    if (!out)
        return false;
    if (outLen == 0 || outLen > maxOut) {
        mz_free(out);
        return false;
    }
    data.assign(static_cast<char*>(out), static_cast<char*>(out) + outLen);
    mz_free(out);
    return true;
}
