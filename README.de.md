# Vector Thumbnail Handler

**Windows-Explorer-Miniaturansichten für Vektordateien: SVG, SVGZ, AI, EPS, PS und DXF**

🇬🇧 [English version → README.md](README.md)

Der Windows Explorer zeigt für die meisten Vektorformate nur ein leeres Blatt (oder das generische Symbol des zugeordneten Programms). Diese Shell-Extension bringt den Explorer dazu, eine echte Vorschau des Dateiinhalts anzuzeigen — dieselbe Idee wie der Thumbnail-Handler von LightBurn für `.lbrn`, aber für die Vektorformate aus Lasergravur, Schneiden und Design.

| Vorher | Nachher |
|:---:|:---:|
| ![Explorer mit generischen Symbolen](screenshots/explorer-before.png) | ![Explorer mit echten Vektor-Miniaturen](screenshots/explorer-after.png) |

## Unterstützte Formate

| Format | Wie die Vorschau entsteht |
|---|---|
| **SVG** | Direkt mit der eingebauten Direct2D-SVG-Engine gerendert |
| **SVGZ** | Transparent entpackt (gzip), dann wie SVG |
| **AI** (Adobe Illustrator) | Moderne `.ai` sind intern PDF → mit der Windows-PDF-Engine gerendert. CorelDRAW-/„ohne PDF-Inhalt"-Exporte nutzen die eingebettete `%AI7_Thumbnail`-Vorschau oder Ghostscript |
| **EPS** | Eingebettete TIFF/WMF-Vorschau (DOS-EPS) oder EPSI-Vorschau; sonst Ghostscript |
| **PS** (PostScript) | Mit Ghostscript gerendert |
| **DXF** | Von einem eingebauten 2D-Zeichner: Linien, Polylinien (inkl. Bogensegmente), Kreise, Bögen, Ellipsen, Splines und Blockreferenzen |

> **Ghostscript ist optional.** SVG, SVGZ, DXF, die meisten AI und EPS-mit-Vorschau funktionieren sofort. Nur `.ps` sowie `.ai`/`.eps` *ohne* eingebettete Vorschau brauchen [Ghostscript](https://ghostscript.com/releases/gsdnld.html) (kostenlos). Ist es installiert, findet und nutzt der Handler es automatisch.

- Keine Abhängigkeiten außer Windows selbst (und optional Ghostscript)
- Funktioniert mit alten und neuen Dateivarianten
- Kaputte oder riesige Dateien (> 256 MB) werden sauber ignoriert — Standardsymbol, kein Absturz
- Deine **„Öffnen mit"**-Verknüpfungen bleiben unangetastet: Doppelklick öffnet weiterhin genau das Programm wie bisher — unabhängig davon, ob überhaupt ein Programm zugeordnet ist

## Installation

1. Die neueste Release-ZIP von der [Releases-Seite](../../releases) herunterladen und entpacken.
2. **`install.bat`** doppelklicken und die Administrator-Abfrage (UAC) bestätigen.
   Sie kopiert die DLL nach `C:\Program Files\VectorThumbnail`, registriert sie, leert den Miniaturansichten-Cache und startet den Explorer neu.
3. Einen Ordner mit Vektordateien öffnen und die Ansicht auf **„Mittelgroße Symbole"** oder größer stellen.

### Deinstallation

**`uninstall.bat`** doppelklicken (fragt ebenfalls nach UAC). Sie entfernt nur die *eigenen* Registry-Einträge — Vorschau-Handler anderer Programme (Adobe, Inkscape, PowerToys …) bleiben unangetastet — und löscht dann die DLL.

## Selbst kompilieren

Benötigt die Visual Studio 2022 Build Tools mit C++-Workload (oder ein volles Visual Studio). Dann:

```
build.bat
```

Das erzeugt `build\VectorThumbnailHandler.dll` sowie zwei Testwerkzeuge:

- `vecthumb-test.exe <datei> <out.png> [groesse]` — durchläuft die Render-Pipeline für jedes unterstützte Format und schreibt ein PNG, ohne den Explorer.
- `shellcheck.exe <datei> <out.png>` — fordert die Miniatur über die **Windows-Shell selbst** an (`IShellItemImageFactory`, exakt der Codepfad des Explorers) und beweist, dass die Registrierung greift.

## Technische Details

| | |
|---|---|
| CLSID | `{26CB6E50-6E37-40FD-BAC2-D8130CF9E549}` |
| Schnittstellen | `IInitializeWithStream`, `IThumbnailProvider` |
| Registrierung | `HKLM\Software\Classes\<ext>\ShellEx\{e357fccd-…}` je Erweiterung, zusätzlich ProgId und `SystemFileAssociations` |
| Formaterkennung | Inhalts-Sniffer (Magic Bytes), nicht die Dateiendung |
| Bildpfad | Direct2D + Windows Imaging Component (WIC), Fant-Skalierung |
| Mitgeliefert | [miniz](https://github.com/richgel999/miniz) für SVGZ-Entpacken (MIT, siehe `src/miniz-LICENSE.txt`) |

### Zwei Dinge, die Windows 11 schwer gemacht hat (hart erarbeitet)

1. **Registrierung nur für den Benutzer (HKCU) reicht nicht.** `IShellItemImageFactory` startet einen HKCU-registrierten Handler bereitwillig — der Explorer selbst aktiviert ihn aber nie. Der Handler muss maschinenweit (HKLM) registriert werden; deshalb braucht die Installation eine UAC-Bestätigung.
2. **Die DLL darf nicht im Benutzerprofil liegen.** Für Ordner außerhalb des Benutzerprofils (z. B. Laufwerk `D:`) extrahiert Windows 11 Miniaturen in einem Sandbox-Prozess, der DLLs unter `AppData` nicht lesen darf (`PATH NOT FOUND`, obwohl die Datei existiert). Die DLL nach `C:\Program Files` zu legen, löst das.

## Fehlerbehebung

- **Keine Miniaturen:** Ansicht muss „Mittelgroße Symbole" oder größer sein; in den Explorer-Optionen darf „Immer Symbole statt Miniaturansichten anzeigen" **nicht** aktiviert sein.
- **`.ps` / manche `.ai` / `.eps` zeigen ein leeres Blatt:** [Ghostscript](https://ghostscript.com/releases/gsdnld.html) installieren, dann `install.bat` erneut ausführen (leert den Cache).
- **Alte Symbole bleiben:** `install.bat` erneut ausführen oder ab- und wieder anmelden.
- **SmartScreen-Warnung:** Skripte und DLL sind nicht signiert — „Weitere Informationen" → „Trotzdem ausführen" wählen oder selbst aus dem Quellcode bauen.

## Lizenz

[MIT](LICENSE)
