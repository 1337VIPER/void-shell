@echo off
rem Builds build\void-shell.exe with the Visual C++ build tools. No other dependencies.
setlocal

set "ROOT=%~dp0"
set "OUT=%ROOT%build"
set "GEN=%OUT%\shaders"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo vswhere.exe not found. Install Visual Studio Build Tools with the "Desktop development with C++" workload.
    exit /b 1
)
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if not defined VSPATH (
    echo Visual C++ x64 build tools not found.
    exit /b 1
)
set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1

if not exist "%GEN%" mkdir "%GEN%"

call :shader void   vs_5_0 VSMain   VoidVS   || exit /b 1
call :shader void   ps_5_0 PSMain   VoidPS   || exit /b 1
call :shader void   ps_5_0 NebulaPS NebulaPS || exit /b 1
call :shader world  vs_5_0 VSMain   WorldVS  || exit /b 1
call :shader world  ps_5_0 PSMain   WorldPS  || exit /b 1
call :shader body   vs_5_0 VSMain   BodyVS   || exit /b 1
call :shader body   ps_5_0 PSMain   BodyPS   || exit /b 1
call :shader glow   vs_5_0 VSMain   GlowVS   || exit /b 1
call :shader glow   ps_5_0 PSMain   GlowPS   || exit /b 1
call :shader sprite vs_5_0 VSMain   SpriteVS || exit /b 1
call :shader sprite ps_5_0 PSMain   SpritePS || exit /b 1
call :shader sprite ps_5_0 BadgePS  BadgePS  || exit /b 1

rem A running copy locks void-shell.exe; Windows still lets it be renamed, so move it aside.
if exist "%OUT%\void-shell.old.exe" del "%OUT%\void-shell.old.exe" >nul 2>nul
if exist "%OUT%\void-shell.exe" move /y "%OUT%\void-shell.exe" "%OUT%\void-shell.old.exe" >nul 2>nul

pushd "%OUT%"
cl /nologo /std:c++20 /O2 /W4 /EHsc /MT /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /I"%GEN%" "%ROOT%src\*.cpp" ^
   /Fe:void-shell.exe ^
   /link /SUBSYSTEM:WINDOWS /MANIFEST:EMBED /MANIFESTINPUT:"%ROOT%src\app.manifest" ^
   d3d11.lib dxgi.lib dwmapi.lib user32.lib gdi32.lib shell32.lib ole32.lib windowscodecs.lib advapi32.lib runtimeobject.lib shcore.lib taskschd.lib secur32.lib oleaut32.lib wtsapi32.lib version.lib uuid.lib pdh.lib wbemuuid.lib
set "ERR=%errorlevel%"
popd
if not "%ERR%"=="0" exit /b %ERR%

echo Built %OUT%\void-shell.exe
exit /b 0

:shader
fxc /nologo /O3 /T %2 /E %3 /Vn g_%4 /Fh "%GEN%\%4.h" "%ROOT%shaders\%1.hlsl" >nul
if errorlevel 1 (
    echo Shader %1.hlsl %3 failed to compile.
    exit /b 1
)
exit /b 0
