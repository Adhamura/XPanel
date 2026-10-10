@echo off
rem Build XPanel release files on Windows.
rem   build-release.bat                    Windows installer: dist\XPanel-<version>-Windows-x64-Setup.exe
rem                                        (needs Qt 6 for MSVC x64, Visual Studio 2022 C++ build tools,
rem                                         CMake, Ninja, Inno Setup 6; see packaging\windows\build-release.ps1)
rem   build-release.bat -Qt C:\Qt\6.8.3\msvc2022_64 -Clean
rem   build-release.bat --linux [options]  Linux AppImage + archive, built inside WSL with
rem                                        ./build-release.sh (e.g. --linux --container)
setlocal
cd /d "%~dp0"
if /I "%~1"=="--linux" goto linux
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0packaging\windows\build-release.ps1" %*
exit /b %ERRORLEVEL%

:linux
shift
where wsl >nul 2>nul || (
    echo WSL is not installed. Run "wsl --install" first, or build on Linux with ./build-release.sh
    exit /b 1
)
set ARGS=
:collect
if "%~1"=="" goto run
set ARGS=%ARGS% %1
shift
goto collect
:run
wsl -- bash ./build-release.sh%ARGS%
exit /b %ERRORLEVEL%
