#pragma once
#include <windows.h>
#include <propsys.h>
#include <thumbcache.h>

// {26CB6E50-6E37-40FD-BAC2-D8130CF9E549}
// CLSID des Vector-Thumbnail-Handlers (SVG/SVGZ/AI/EPS/PS/DXF).
extern const CLSID CLSID_VectorThumbnailProvider;

void DllAddRef();
void DllRelease();

HRESULT CreateVectorThumbnailProvider(REFIID riid, void** ppv);
