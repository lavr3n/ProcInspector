@echo off
setlocal
set "VSDEV=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VSDEV%" (
    echo vcvars64.bat not found. Edit build.bat and set the path to your Visual Studio install.
    exit /b 1
)
call "%VSDEV%" >nul
cd /d "%~dp0"
if not exist build mkdir build
if not exist app.ico powershell -ExecutionPolicy Bypass -File "%~dp0gen_icon.ps1"
rc /nologo /fo build\app.res app.rc
if errorlevel 1 (
    echo RESOURCE COMPILE FAILED
    exit /b 1
)
cl /nologo /EHsc /std:c++17 /W3 /O2 /utf-8 /DUNICODE /D_UNICODE /Fo:build\ /Fe:build\ProcInspector.exe main.cpp build\app.res /link /SUBSYSTEM:WINDOWS
if errorlevel 1 (
    echo BUILD FAILED
    exit /b 1
)
echo.
echo OK: %~dp0build\ProcInspector.exe
