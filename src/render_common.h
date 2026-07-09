#pragma once
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <cstddef>

// Gemeinsame Helfer fuer alle Renderer.

// Dekodiert Bilddaten (PNG/JPEG/TIFF/BMP...) via WIC, skaliert proportional in
// cx*cx (kein Hochskalieren) und liefert ein 32-Bit-PBGRA-HBITMAP (top-down).
HRESULT DecodeImageBytesToHBitmap(const unsigned char* data, size_t len,
                                  UINT cx, HBITMAP* phbmp);

// Skaliert eine beliebige WIC-Quelle proportional in cx*cx und liefert ein
// HBITMAP. cx==0: keine Skalierung.
HRESULT WicSourceToHBitmap(IWICImagingFactory* factory, IWICBitmapSource* src,
                           UINT cx, HBITMAP* phbmp);

// Erzeugt eine leere 32bpp-PBGRA-WIC-Bitmap als Direct2D-Zeichenziel.
HRESULT CreateWicRenderBitmap(IWICImagingFactory* factory, UINT w, UINT h,
                              IWICBitmap** out);

// Liefert die (prozessweite) WIC-Factory.
HRESULT GetWicFactory(IWICImagingFactory** out);

// Berechnet die Zielgroesse: (srcW,srcH) proportional eingepasst in cx*cx,
// ohne Hochskalieren kleiner Quellen.
void FitSize(UINT srcW, UINT srcH, UINT cx, UINT* dstW, UINT* dstH);
