@echo off
rem Configures and builds a Release binary with Visual Studio's x64 toolchain.
setlocal
set "VS=C:\Program Files\Microsoft Visual Studio\18\Community"
set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
rem Put Visual Studio's CMake and Ninja ahead of any MSYS2 copies on PATH.
set "PATH=%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"
if not exist "%~dp0third_party\vst3sdk\public.sdk" (
    git clone --depth 1 --branch v3.8.1_build_84 --recurse-submodules --shallow-submodules https://github.com/steinbergmedia/vst3sdk.git "%~dp0third_party\vst3sdk" || exit /b 1
)
cmake -S "%~dp0." -B "%~dp0build" -G Ninja -DCMAKE_BUILD_TYPE=Release || exit /b 1
cmake --build "%~dp0build" || exit /b 1
echo.
echo Built: %~dp0build\Composer.exe
