@echo off
rem Configures and builds a Release binary with Visual Studio's x64 toolchain.
setlocal
set "VS=C:\Program Files\Microsoft Visual Studio\18\Community"
set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
rem Put Visual Studio's CMake and Ninja ahead of any MSYS2 copies on PATH.
set "PATH=%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"
cmake -S "%~dp0." -B "%~dp0build" -G Ninja -DCMAKE_BUILD_TYPE=Release || exit /b 1
cmake --build "%~dp0build" || exit /b 1
echo.
echo Built: %~dp0build\Composer.exe
