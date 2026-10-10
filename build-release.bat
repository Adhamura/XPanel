@echo off
rem Build the XPanel Linux release from Windows, inside WSL (Windows Subsystem for Linux).
rem XPanel itself runs on Linux only (on Windows, use Armoury Crate); this only builds the
rem Linux x86_64 archive into dist\. Needs WSL with Docker/Podman, or Qt 6 >= 6.5 + cmake + ninja.
rem   build-release.bat              build with the Qt 6 inside WSL
rem   build-release.bat --container  build in a debian:trixie container (same as CI)
setlocal
cd /d "%~dp0"
where wsl >nul 2>nul || (
    echo WSL is not installed. Run "wsl --install" first, or build on Linux with ./build-release.sh
    exit /b 1
)
wsl -- bash ./build-release.sh %*
exit /b %ERRORLEVEL%
