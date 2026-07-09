@echo off
rem Entfernt den Vector-Thumbnail-Handler vollstaendig (fragt nach Admin-Rechten).
setlocal

net session >nul 2>&1
if errorlevel 1 (
    echo Fordere Administratorrechte an - bitte die UAC-Abfrage bestaetigen ...
    powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b
)

set "TARGET=%ProgramFiles%\VectorThumbnail"

if exist "%TARGET%\VectorThumbnailHandler.dll" (
    echo Deregistriere Shell-Extension ...
    rem DllUnregisterServer entfernt nur Eintraege, die auf unsere CLSID zeigen -
    rem Vorschau-Handler anderer Programme bleiben unangetastet.
    regsvr32 /s /u "%TARGET%\VectorThumbnailHandler.dll"
) else (
    echo Hinweis: DLL nicht gefunden, entferne CLSID-Eintraege.
    for %%R in (HKCU HKLM) do (
        reg delete "%%R\Software\Classes\CLSID\{26CB6E50-6E37-40FD-BAC2-D8130CF9E549}" /f >nul 2>&1
        reg delete "%%R\Software\Classes\AppID\{26CB6E50-6E37-40FD-BAC2-D8130CF9E549}" /f >nul 2>&1
    )
)

echo Starte Explorer neu, damit die DLL freigegeben wird ...
taskkill /f /im explorer.exe >nul 2>&1
del /f /q "%LOCALAPPDATA%\Microsoft\Windows\Explorer\thumbcache_*.db" >nul 2>&1
start explorer.exe

ping -n 3 127.0.0.1 >nul
if exist "%TARGET%" rd /s /q "%TARGET%" 2>nul

echo.
echo Deinstallation abgeschlossen.
pause
endlocal
