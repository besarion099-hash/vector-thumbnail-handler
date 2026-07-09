// xTool-Studio-Miniaturen: die .xcs-Datei ist JSON mit einem eingebetteten
// Cover-PNG im Feld "cover":"data:image/png;base64,...". Extraktion via
// ExtractCoverImage (identisch zum eigenstaendigen xcs-thumbnail-handler).

#include <windows.h>

#include <vector>

#include "render_common.h"
#include "renderers.h"
#include "xcs_cover.h"

HRESULT RenderXcsToHBitmap(const char* data, size_t len, UINT cx, HBITMAP* phbmp)
{
    if (!phbmp || !data || len == 0 || cx == 0)
        return E_INVALIDARG;
    *phbmp = nullptr;

    std::vector<unsigned char> image;
    if (!ExtractCoverImage(data, len, image))
        return E_FAIL;
    return DecodeImageBytesToHBitmap(image.data(), image.size(), cx, phbmp);
}
