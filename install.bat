@echo off
rem Installiert den Vector-Thumbnail-Handler (SVG/SVGZ/AI/EPS/PS/DXF).
rem Maschinenweit, fragt nach Admin-Rechten.
setlocal
cd /d "%~dp0"

net session >nul 2>&1
if errorlevel 1 (
    echo Fordere Administratorrechte an - bitte die UAC-Abfrage bestaetigen ...
    powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b
)

rem Die DLL MUSS unter "Program Files" liegen: fuer Ordner ausserhalb des
rem Benutzerprofils extrahiert Windows 11 Miniaturen in einem Sandbox-Prozess,
rem der DLLs aus AppData nicht lesen darf.
set "TARGET=%ProgramFiles%\VectorThumbnail"
set "DLL=build\VectorThumbnailHandler.dll"
if not exist "%DLL%" set "DLL=VectorThumbnailHandler.dll"
if not exist "%DLL%" (
    echo FEHLER: VectorThumbnailHandler.dll nicht gefunden. Erst build.bat ausfuehren.
    pause
    exit /b 1
)

echo Kopiere DLL nach %TARGET% ...
if not exist "%TARGET%" mkdir "%TARGET%"
copy /y "%DLL%" "%TARGET%\VectorThumbnailHandler.dll" >nul
if errorlevel 1 (
    echo Datei ist in Benutzung - beende Explorer und versuche es erneut ...
    taskkill /f /im explorer.exe >nul 2>&1
    ping -n 3 127.0.0.1 >nul
    copy /y "%DLL%" "%TARGET%\VectorThumbnailHandler.dll" >nul
    if errorlevel 1 (
        echo FEHLER: Kopieren fehlgeschlagen.
        start explorer.exe
        pause
        exit /b 1
    )
)

echo Registriere Shell-Extension ...
regsvr32 /s "%TARGET%\VectorThumbnailHandler.dll"
if errorlevel 1 (
    echo FEHLER: Registrierung fehlgeschlagen.
    pause
    exit /b 1
)

echo Leere Miniaturansichten-Cache und starte Explorer neu ...
taskkill /f /im explorer.exe >nul 2>&1
del /f /q "%LOCALAPPDATA%\Microsoft\Windows\Explorer\thumbcache_*.db" >nul 2>&1
start explorer.exe

echo.
echo Fertig! Der Explorer zeigt jetzt Miniaturansichten fuer
echo SVG, SVGZ, AI, EPS, PS und DXF.
echo (Ansicht auf "Mittelgrosse Symbole" oder groesser stellen.)
echo.
echo Hinweis: Fuer .ai ohne PDF-Inhalt und EPS/PS ohne eingebettete
echo Vorschau wird Ghostscript empfohlen (kostenlos, ghostscript.com).
pause
endlocal
