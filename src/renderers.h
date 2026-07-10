#pragma once
#include <windows.h>
#include <cstddef>

// Format-Renderer. Alle liefern ein 32-Bit-PBGRA-HBITMAP (top-down) oder
// einen Fehler-HRESULT (-> Explorer zeigt das Standardsymbol).

HRESULT RenderSvgToHBitmap(const char* data, size_t len, UINT cx, HBITMAP* phbmp);
HRESULT RenderPdfToHBitmap(const unsigned char* data, size_t len, UINT cx, HBITMAP* phbmp);
HRESULT RenderEpsToHBitmap(const unsigned char* data, size_t len, UINT cx, HBITMAP* phbmp);
HRESULT RenderDxfToHBitmap(const char* data, size_t len, UINT cx, HBITMAP* phbmp);
HRESULT RenderCdrToHBitmap(const unsigned char* data, size_t len, UINT cx, HBITMAP* phbmp);
HRESULT RenderXcsToHBitmap(const char* data, size_t len, UINT cx, HBITMAP* phbmp);
HRESULT RenderLbrnToHBitmap(const char* data, size_t len, UINT cx, HBITMAP* phbmp);
HRESULT RenderXsToHBitmap(const unsigned char* data, size_t len, UINT cx, HBITMAP* phbmp);

// Eingebettete %AI7_Thumbnail-Vorschau aus .ai-Dateien (Fallback, wenn die
// PDF-Seite leer ist, z. B. bei CorelDRAW-Exporten oder "ohne PDF-Inhalt").
HRESULT RenderAi7ThumbToHBitmap(const unsigned char* data, size_t len, UINT cx,
                                HBITMAP* phbmp);

// Ghostscript-Fallback (falls installiert): rendert PS/EPS/PDF als PNG.
enum class GsInput { Ps, Eps, Pdf };
HRESULT RenderViaGhostscript(const unsigned char* data, size_t len, UINT cx,
                             GsInput kind, HBITMAP* phbmp);

// true, wenn das Bitmap praktisch nur aus Weiss/Transparenz besteht
// (z. B. wenn die Windows-PDF-Engine Illustrator-Ebenen nicht zeichnet).
bool IsMostlyBlankBitmap(HBITMAP hbmp);

// Zentrale Weiche: gunzip + sniff + Dispatch an den passenden Renderer.
// Wird von der COM-Klasse und der Test-EXE gemeinsam benutzt.
HRESULT RenderVectorThumbnail(const unsigned char* data, size_t len, UINT cx,
                              HBITMAP* phbmp);
