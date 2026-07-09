#include "xcs_cover.h"

#include <windows.h>
#include <wincrypt.h>

#include <cstring>

namespace {

const char* FindBytes(const char* hay, size_t hayLen, const char* needle, size_t needleLen)
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

const char* SkipWs(const char* p, const char* end)
{
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n'))
        ++p;
    return p;
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

bool ExtractCoverImage(const char* data, size_t len, std::vector<unsigned char>& imageOut)
{
    static const char kKey[] = "\"cover\"";
    static const char kDataPrefix[] = "data:image/";
    static const char kBase64Marker[] = ";base64,";

    const char* end = data + len;
    const char* search = data;
    size_t remaining = len;

    // "cover" kann theoretisch auch als Text im Canvas vorkommen; deshalb alle
    // Vorkommen pruefen und nur ein Feld akzeptieren, dessen Wert eine
    // data:image-URL ist.
    while (remaining > 0) {
        const char* key = FindBytes(search, remaining, kKey, sizeof(kKey) - 1);
        if (!key)
            return false;

        const char* p = SkipWs(key + sizeof(kKey) - 1, end);
        if (p < end && *p == ':') {
            p = SkipWs(p + 1, end);
            if (p < end && *p == '"') {
                ++p; // Wertbeginn
                size_t valMax = static_cast<size_t>(end - p);
                if (valMax > sizeof(kDataPrefix) - 1 &&
                    memcmp(p, kDataPrefix, sizeof(kDataPrefix) - 1) == 0) {
                    // Ende des JSON-Strings suchen (Base64-Daten enthalten kein ").
                    const char* close = static_cast<const char*>(memchr(p, '"', valMax));
                    if (close) {
                        const char* b64 = FindBytes(p, static_cast<size_t>(close - p),
                                                    kBase64Marker, sizeof(kBase64Marker) - 1);
                        if (b64) {
                            b64 += sizeof(kBase64Marker) - 1;
                            if (DecodeBase64(b64, static_cast<size_t>(close - b64), imageOut))
                                return true;
                        }
                    }
                }
            }
        }

        search = key + 1;
        remaining = static_cast<size_t>(end - search);
    }
    return false;
}
