#Requires -Version 5.1
[CmdletBinding()]
param(
    [ValidateSet('debug', 'release')][string]$Preset = 'debug',
    [switch]$Run
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'enter-dev-shell.ps1')
Push-Location $gamepilotRepoRoot
try {
    & cmake --preset $Preset
    if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
    & cmake --build --preset $Preset
    if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
    if ($Run) {
        # This window is explicitly requested by -Run.
        Start-Process -FilePath (Join-Path $gamepilotRepoRoot "build\$Preset\gamepilot.exe")
    }
} finally {
    Pop-Location
}
