@echo off
rem Builds HexFPS as a 32-bit version.dll proxy. Output: build\version.dll
rem Needs Visual Studio 2022 or newer with "Desktop development with C++" (MSVC x86 tools).
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (echo vswhere.exe not found - install Visual Studio & exit /b 1)
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (echo Visual Studio C++ tools not found & exit /b 1)
set "PATH=%PATH%;%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
call "%VSDIR%\VC\Auxiliary\Build\vcvars32.bat" >nul || exit /b 1
cd /d "%~dp0"
if not exist build mkdir build
cl /nologo /O2 /MD /EHsc /W3 /GS- /Fobuild\ /Fe:build\version.dll /LD src\proxy.cpp src\hexfps.cpp src\stats.cpp src\smooth.cpp src\patch.cpp src\ui.cpp src\camera.cpp ^
   /link /DEF:src\version.def /SAFESEH:NO user32.lib kernel32.lib || exit /b 1
echo BUILD OK
