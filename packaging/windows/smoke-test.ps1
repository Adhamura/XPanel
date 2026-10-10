# Start the packaged XPanel.exe without a screen (offscreen platform) and fail on QML or Qt
# plugin errors, like packaging/appimage/smoke-test.sh does for Linux.
#   packaging/windows/smoke-test.ps1 build-release-win\stage\XPanel.exe
param([Parameter(Mandatory = $true)][string]$Exe)
$ErrorActionPreference = 'Stop'
$Exe = (Resolve-Path $Exe).Path
$env:QT_QPA_PLATFORM = 'offscreen'
$env:QT_QUICK_BACKEND = 'software'
$env:QT_FORCE_STDERR_LOGGING = '1'
$Temp = if ($env:RUNNER_TEMP) { $env:RUNNER_TEMP } else { $env:TEMP }
$Logs = New-Item -ItemType Directory -Force (Join-Path $Temp 'xpanel-smoke')
Remove-Item "$Logs\*" -ErrorAction SilentlyContinue

function Run([string[]]$ArgList, [string]$Name, [switch]$NoWait) {
    $p = Start-Process $Exe -ArgumentList $ArgList -PassThru -NoNewWindow `
        -RedirectStandardOutput "$Logs\$Name.out" -RedirectStandardError "$Logs\$Name.err"
    if (-not $NoWait) { $p.WaitForExit(20000) | Out-Null }
    return $p
}

$main = Run @('--open') 'main' -NoWait
Start-Sleep 10
$i = 0
foreach ($page in 'library', 'settings/performance', 'settings/lighting') {
    Run @('--app', $page) "page$i" | Out-Null
    Start-Sleep 3
    $i++
}
Run @('--quit') 'quit' | Out-Null
Start-Sleep 2
if (-not $main.HasExited) { Stop-Process -Id $main.Id -Force }
Get-Process xpanel-daemon -ErrorAction SilentlyContinue | Stop-Process -Force

$log = Get-ChildItem $Logs | ForEach-Object { Get-Content $_.FullName -Raw } | Out-String
Write-Host $log
$bad = 'Unsupported image format|qrc:/.*: (Error|.*is not a type|.*ReferenceError|.*TypeError)|Could not load the Qt platform plugin|module .* is not installed|Cannot load library'
if ($log -match $bad) { throw "smoke test FAILED: $($Matches[0])" }
if ($log -notmatch "sent 'app settings/lighting'") { throw 'smoke test FAILED: XPanel did not start' }
Write-Host 'smoke test OK'
