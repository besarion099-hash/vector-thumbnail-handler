// xTool-Studio-v2-Miniaturen (.xs): ZIP-Container mit einem eingebetteten
// Vorschau-PNG unter resources/project-cover.png (im Feld "cover" der
// project.json referenziert). Extraktion via miniz -> WIC.

#include <windows.h>

#include <cstring>

#include "miniz.h"
#include "render_common.h"
#include "renderers.h"

HRESULT RenderXsToHBitmap(const unsigned char* data, size_t len, UINT cx,
                          HBITMAP* phbmp)
{
    if (!phbmp || !data || len < 16 || len > 0x7FFFFFFF || cx == 0)
        return E_INVALIDARG;
    *phbmp = nullptr;

    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    if (!mz_zip_reader_init_mem(&zip, data, len, 0))
        return E_FAIL;

    HRESULT hr = E_FAIL;
    const char* candidates[] = {
        "resources/project-cover.png",
        "resources/cover.png",
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
