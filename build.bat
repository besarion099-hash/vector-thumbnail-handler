@echo off
rem Baut VectorThumbnailHandler.dll und vecthumb-test.exe (64-bit, MSVC).
setlocal
cd /d "%~dp0"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo FEHLER: vswhere.exe nicht gefunden - sind die VS Build Tools installiert?
    exit /b 1
)

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if not defined VSPATH (
    echo FEHLER: keine VS-Installation mit C++-Tools gefunden.
    exit /b 1
)

call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (
    echo FEHLER: vcvars64.bat fehlgeschlagen.
    exit /b 1
)

if not exist build mkdir build

if not exist build\miniz.obj (
    echo === Baue miniz ===
    cl /nologo /O2 /MT /c /Fobuild\miniz.obj src\miniz.c
    if errorlevel 1 exit /b 1
)

set CXXFLAGS=/nologo /std:c++17 /O2 /W4 /EHsc /MT /DUNICODE /D_UNICODE /DNOMINMAX %VTH_EXTRA%
set SOURCES=src\dllmain.cpp src\thumbnail_provider.cpp src\render_dispatch.cpp ^
    src\sniff.cpp src\render_common.cpp src\render_svg.cpp src\render_pdf.cpp ^
    src\render_eps.cpp src\render_dxf.cpp src\render_aithumb.cpp
set LIBS=build\miniz.obj shlwapi.lib windowscodecs.lib ole32.lib advapi32.lib ^
    shell32.lib user32.lib gdi32.lib d2d1.lib shcore.lib windowsapp.lib runtimeobject.lib

echo === Baue VectorThumbnailHandler.dll ===
cl %CXXFLAGS% /Fobuild\ %SOURCES% /LD /Fe:build\VectorThumbnailHandler.dll ^
   /link /DEF:src\exports.def %LIBS%
if errorlevel 1 exit /b 1

echo === Baue vecthumb-test.exe ===
cl %CXXFLAGS% /Fobuild\ test\main.cpp src\render_dispatch.cpp src\sniff.cpp ^
   src\render_common.cpp src\render_svg.cpp src\render_pdf.cpp ^
   src\render_eps.cpp src\render_dxf.cpp src\render_aithumb.cpp ^
   /Fe:build\vecthumb-test.exe /link %LIBS%
if errorlevel 1 exit /b 1

echo === Baue shellcheck.exe ===
cl %CXXFLAGS% /Fobuild\ test\shellcheck.cpp /Fe:build\shellcheck.exe ^
   /link shlwapi.lib windowscodecs.lib ole32.lib gdi32.lib shell32.lib
if errorlevel 1 exit /b 1

echo.
echo Build erfolgreich: build\VectorThumbnailHandler.dll, build\vecthumb-test.exe
endlocal
