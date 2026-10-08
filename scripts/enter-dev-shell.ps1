#Requires -Version 5.1
# Dot-source this file to activate the pinned tools in the current terminal only.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$gamepilotRepoRoot = Split-Path -Parent $PSScriptRoot
$gamepilotToolsRoot = Join-Path $env:LOCALAPPDATA 'Programs\GamePilotTools'
$gamepilotLock = Get-Content -LiteralPath (Join-Path $gamepilotRepoRoot 'tools\toolchain.lock.json') -Raw | ConvertFrom-Json
$gamepilotBins = @()
foreach ($gamepilotEntry in $gamepilotLock.PSObject.Properties) {
    $gamepilotExe = Join-Path (Join-Path $gamepilotToolsRoot $gamepilotEntry.Value.directory) $gamepilotEntry.Value.executable
    if (-not (Test-Path -LiteralPath $gamepilotExe)) {
        throw "Missing $($gamepilotEntry.Name). Run .\scripts\setup.ps1 first."
    }
    $gamepilotBins += Split-Path -Parent $gamepilotExe
}
$env:GAMEPILOT_LLVM_ROOT = Join-Path $gamepilotToolsRoot $gamepilotLock.llvm.directory
$gamepilotExistingPaths = @($env:PATH.Split(';') | Where-Object { $_ -and $_ -notin $gamepilotBins })
$env:PATH = ($gamepilotBins + $gamepilotExistingPaths) -join ';'
