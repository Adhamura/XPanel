# Build the XPanel Windows installer: dist\XPanel-<version>-Windows-x64-Setup.exe
# Called by build-release.bat (repo root) and by GitHub Actions.
#
#   build-release.bat                          use Qt from $env:QT_ROOT_DIR / -Qt, MSVC, Ninja
#   build-release.bat -Qt C:\Qt\6.8.3\msvc2022_64 -Clean
#
# Needs: Qt 6 for MSVC (x64), Visual Studio 2022 Build Tools (C++), CMake, Ninja, Inno Setup 6.
# Windows support is experimental (see README).
param(
    [string]$Qt = $env:QT_ROOT_DIR,
    [switch]$Clean
)
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path "$PSScriptRoot\..\..").Path
Set-Location $Root

$Version = (Select-String -Path CMakeLists.txt -Pattern '^project\(XPanel VERSION ([0-9.]+)').Matches[0].Groups[1].Value
if (-not $Version) { throw 'Cannot read the version from CMakeLists.txt' }
if (-not $Qt -or -not (Test-Path "$Qt\bin\windeployqt.exe")) {
    throw "Qt for MSVC not found. Pass -Qt C:\Qt\<version>\msvc2022_64 or set QT_ROOT_DIR."
}

# MSVC environment: load vcvars64 when cl.exe is not on PATH yet.
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { throw 'Visual Studio C++ build tools not found' }
    cmd /c "`"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && set" | ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') { Set-Item "env:$($Matches[1])" $Matches[2] }
    }
}
foreach ($tool in 'cmake', 'ninja') {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { throw "Missing build tool: $tool" }
}
$Iscc = (Get-Command iscc.exe -ErrorAction SilentlyContinue).Source
if (-not $Iscc) { $Iscc = "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe" }
if (-not (Test-Path $Iscc)) { throw 'Inno Setup 6 (ISCC.exe) not found' }

$Build = 'build-release-win'
$Stage = "$Build\stage"
if ($Clean) { Remove-Item -Recurse -Force $Build -ErrorAction SilentlyContinue }
Remove-Item -Recurse -Force $Stage -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force dist, $Stage | Out-Null

Write-Host "==> Building XPanel $Version (Windows x64)"
cmake -S . -B $Build -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=$Qt" -DXPANEL_BUILD_TOOLS=OFF
if ($LASTEXITCODE) { throw 'cmake configure failed' }
cmake --build $Build
if ($LASTEXITCODE) { throw 'build failed' }

Write-Host '==> Collecting Qt (windeployqt)'
Copy-Item "$Build\bin\XPanel.exe", "$Build\bin\xpanel-daemon.exe" $Stage
# --svg: the QML icons are SVG; XPanel does not call QtSvg itself, so windeployqt would skip it.
& "$Qt\bin\windeployqt.exe" --release --qmldir qml --svg --no-translations --no-system-d3d-compiler --no-opengl-sw "$Stage\XPanel.exe"
if ($LASTEXITCODE) { throw 'windeployqt failed' }
& "$Qt\bin\windeployqt.exe" --release --no-translations "$Stage\xpanel-daemon.exe"
if ($LASTEXITCODE) { throw 'windeployqt (daemon) failed' }
foreach ($need in 'platforms\qwindows.dll', 'imageformats\qsvg.dll', 'iconengines\qsvgicon.dll', 'qml\QtQuick\Window') {
    if (-not (Test-Path "$Stage\$need")) { throw "The package is missing $need" }
}
Copy-Item LICENSE, README.md, CHANGELOG.md $Stage

Write-Host '==> Installer (Inno Setup)'
& $Iscc /Q "/DVersion=$Version" "/DSourceDir=$Root\$Stage" "/DOutputDir=$Root\dist" packaging\windows\xpanel.iss
if ($LASTEXITCODE) { throw 'Inno Setup failed' }
$Setup = "dist\XPanel-$Version-Windows-x64-Setup.exe"
if (-not (Test-Path $Setup)) { throw "$Setup was not created" }
(Get-FileHash -Algorithm SHA256 $Setup).Hash.ToLower() + "  " + (Split-Path $Setup -Leaf) | Set-Content "$Setup.sha256" -Encoding ascii

Write-Host ''
Write-Host 'Release ready:'
Get-Item $Setup | Format-Table Name, Length
