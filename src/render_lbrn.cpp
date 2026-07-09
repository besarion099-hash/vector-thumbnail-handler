// LightBurn-Miniaturen (.lbrn / .lbrn2): die Projektdatei ist XML mit einem
// eingebetteten Vorschau-PNG im Attribut <Thumbnail Source="iVBORw0K…">
// (roher base64, ohne data:-Praefix). Extraktion + Dekodierung via WIC.

#include <windows.h>
#include <wincrypt.h>

#include <cstring>
#include <vector>

#include "render_common.h"
#include "renderers.h"

namespace {

const char* FindBytes(const char* hay, size_t hayLen, const char* needle,
                      size_t needleLen)
{
    if (needleLen == 0 || hayLen < needleLen)
        return nullptr;
    const char* end = hay + (hayLen - needleLen);
    for (const char* p = hay; p <= end; ++p) {
        if (p[0] == needle[0] && memcmp(p, needle, needleLen) == 0)
            return p;
    }
    return nullptr;
}

bool DecodeBase64(const char* b64, size_t b64Len, std::vector<unsigned char>& out)
{
    if (b64Len == 0 || b64Len > 0x7FFFFFFF)
        return false;
    DWORD binLen = 0;
    if (!CryptStringToBinaryA(b64, static_cast<DWORD>(b64Len), CRYPT_STRING_BASE64,
                              nullptr, &binLen, nullptr, nullptr))
        return false;
    out.resize(binLen);
    if (!CryptStringToBinaryA(b64, static_cast<DWORD>(b64Len), CRYPT_STRING_BASE64,
                              out.data(), &binLen, nullptr, nullptr))
        return false;
    out.resize(binLen);
    return !out.empty();
}

} // namespace

HRESULT RenderLbrnToHBitmap(const char* data, size_t len, UINT cx, HBITMAP* phbmp)
{
    if (!phbmp || !data || len == 0 || cx == 0)
        return E_INVALIDARG;
    *phbmp = nullptr;

    static const char kKey[] = "<Thumbnail Source=\"";
    const char* key = FindBytes(data, len, kKey, sizeof(kKey) - 1);
    if (!key)
        return E_FAIL;
    const char* b64 = key + (sizeof(kKey) - 1);
    const size_t remaining = len - static_cast<size_t>(b64 - data);
    const char* close = static_cast<const char*>(memchr(b64, '"', remaining));
    if (!close || close == b64)
        return E_FAIL;

    std::vector<unsigned char> image;
    if (!DecodeBase64(b64, static_cast<size_t>(close - b64), image))
        return E_FAIL;
    return DecodeImageBytesToHBitmap(image.data(), image.size(), cx, phbmp);
}
