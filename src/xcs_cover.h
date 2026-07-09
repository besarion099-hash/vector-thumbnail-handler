#pragma once
#include <cstddef>
#include <vector>

// Sucht im rohen JSON-Inhalt einer .xcs-Datei das Top-Level-Feld
//   "cover":"data:image/png;base64,...."
// und liefert die dekodierten Bilddaten (PNG/JPEG-Bytes) zurueck.
// Gibt false zurueck, wenn kein brauchbares Cover gefunden wurde.
bool ExtractCoverImage(const char* data, size_t len, std::vector<unsigned char>& imageOut);
