#pragma once
#include <cstddef>
#include <vector>

enum class VecFormat {
    Unknown,
    Svg,     // XML mit <svg>-Wurzel
    Pdf,     // %PDF (auch moderne .ai)
    DosEps,  // EPS mit Binaerheader (eingebettete TIFF/WMF-Vorschau)
    PsText,  // %!PS ohne Binaerheader (EPS/PS/altes .ai)
    Dxf,     // AutoCAD DXF (Text)
    Cdr,     // CorelDRAW (RIFF- oder ZIP-Container mit Vorschau-Bitmap)
    Xcs,     // xTool Studio Projekt (JSON mit eingebettetem Cover-PNG)
};

// Erkennt das Format am Inhalt (nicht am Dateinamen).
VecFormat SniffFormat(const std::vector<char>& data);

// Entpackt data an Ort und Stelle, falls es gzip-komprimiert ist (SVGZ).
// Rueckgabe false bei Entpackfehler; true sonst (auch wenn kein gzip).
bool GunzipIfNeeded(std::vector<char>& data, size_t maxOut);
