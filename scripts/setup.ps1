#Requires -Version 5.1
[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$repoRoot = Split-Path -Parent $PSScriptRoot
$toolsRoot = Join-Path $env:LOCALAPPDATA 'Programs\GamePilotTools'
$lock = Get-Content -LiteralPath (Join-Path $repoRoot 'tools\toolchain.lock.json') -Raw | ConvertFrom-Json
New-Item -ItemType Directory -Path $toolsRoot -Force | Out-Null

foreach ($entry in $lock.PSObject.Properties) {
    $package = $entry.Value
    $installPath = Join-Path $toolsRoot $package.directory
    $marker = Join-Path $installPath '.gamepilot-sha256'
    if ((Test-Path -LiteralPath (Join-Path $installPath $package.executable)) -and
        (Test-Path -LiteralPath $marker) -and
        ((Get-Content -LiteralPath $marker -Raw).Trim() -eq $package.sha256)) {
        Write-Host "$($entry.Name) $($package.version) already installed."
        continue
    }
    if (Test-Path -LiteralPath $installPath) {
        throw "Incomplete or unmanaged installation at $installPath. Move it aside before retrying."
    }

    $archive = Join-Path $toolsRoot ($package.directory + '.zip')
    Write-Host "Downloading $($entry.Name) $($package.version)..."
    if (-not (Test-Path -LiteralPath $archive)) {
        $partial = $archive + '.partial'
        & curl.exe --fail --location --retry 3 --silent --show-error --output $partial $package.url
        if ($LASTEXITCODE -ne 0) { throw "Download failed: $($package.url)" }
        Move-Item -LiteralPath $partial -Destination $archive
    }
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $package.sha256) {
        # Exact file path, never recursive: allow a fresh download on the next run.
        Remove-Item -LiteralPath $archive
        throw "Checksum mismatch for $($entry.Name). Nothing was extracted."
    }

    Write-Host "Extracting verified $($entry.Name) archive..."
    if ($entry.Name -eq 'ninja') {
        New-Item -ItemType Directory -Path $installPath | Out-Null
        & tar.exe -xf $archive -C $installPath
    } else {
        & tar.exe -xf $archive -C $toolsRoot
    }
    if ($LASTEXITCODE -ne 0) { throw "Extraction failed for $($entry.Name)." }
    if (-not (Test-Path -LiteralPath (Join-Path $installPath $package.executable))) {
        throw "Expected executable missing after extraction: $($entry.Name)."
    }
    [IO.File]::WriteAllText($marker, $package.sha256)
    Remove-Item -LiteralPath $archive
}

. (Join-Path $PSScriptRoot 'enter-dev-shell.ps1')
# Keep machine-specific editor paths out of the shared settings and Git history.
$workspace = @{
    folders = @(@{ path = '.' })
    settings = @{
        'clangd.path' = (Join-Path $env:GAMEPILOT_LLVM_ROOT 'bin\clangd.exe')
        'clangd.arguments' = @('--background-index', '--clang-tidy', '--header-insertion=never',
            ('--query-driver=' + ($env:GAMEPILOT_LLVM_ROOT.Replace('\', '/') + '/bin/*clang++.exe')))
    }
} | ConvertTo-Json -Depth 5
[IO.File]::WriteAllText((Join-Path $repoRoot 'gamepilot.code-workspace'), $workspace + [Environment]::NewLine)
$zedDirectory = Join-Path $repoRoot '.zed'
$zedSettingsPath = Join-Path $zedDirectory 'settings.json'
if (-not (Test-Path -LiteralPath $zedSettingsPath)) {
    New-Item -ItemType Directory -Path $zedDirectory -Force | Out-Null
    $zedSettings = @{
        lsp = @{
            clangd = @{
                binary = @{
                    path = (Join-Path $env:GAMEPILOT_LLVM_ROOT 'bin\clangd.exe')
                    arguments = @('--background-index', '--clang-tidy',
                        ('--query-driver=' + ($env:GAMEPILOT_LLVM_ROOT.Replace('\', '/') + '/bin/*clang++.exe')))
                }
            }
        }
        languages = @{ 'C++' = @{ format_on_save = 'on'; tab_size = 4 } }
    } | ConvertTo-Json -Depth 8
    [IO.File]::WriteAllText($zedSettingsPath, $zedSettings + [Environment]::NewLine)
} else {
    Write-Host 'Existing .zed/settings.json preserved. Update its clangd path manually after a toolchain upgrade.'
}
Write-Host "Ready. Tools installed in $toolsRoot"
Write-Host 'Run .\scripts\build.ps1 or dot-source .\scripts\enter-dev-shell.ps1 for direct tool commands.'
Write-Host 'VS Code: open gamepilot.code-workspace and install the recommended extensions.'
Write-Host 'Zed: open this repository; build and debug tasks are in .zed/.'
