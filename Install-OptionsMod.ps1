[CmdletBinding()]
param([switch]$Uninstall)

$ErrorActionPreference = 'Stop'
$modRoot = $PSScriptRoot
$gameRoot = Split-Path -Parent $modRoot
$game = Join-Path $gameRoot 'theocracy-0.6.85.exe'
$backup = Join-Path $modRoot 'theocracy-0.6.85.stock.exe'
$marker = Join-Path $modRoot 'installed.sha256'
$stockHash = 'D922D2F114B5EF33A1D2B10B5F1EBEBCE2BFC28D9EC933CD8DB4B7F20E1D78E2'
if ((Split-Path -Leaf $modRoot) -ne 'Theocracy Improvements Mod') {
    throw 'Keep these files inside the Theocracy Improvements Mod folder under the game folder.'
}
if (Get-Process -Name 'theocracy-0.6.85' -ErrorAction SilentlyContinue) {
    throw 'Close Theocracy before installing or uninstalling.'
}

$current = (Get-FileHash -LiteralPath $game).Hash
if ($Uninstall) {
    if ($current -eq $stockHash) { 'The stock executable is already installed.'; return }
    if (!(Test-Path -LiteralPath $marker) -or $current -ne (Get-Content -LiteralPath $marker)) {
        throw 'The installed executable does not match this mod install.'
    }
    if ((Get-FileHash -LiteralPath $backup).Hash -ne $stockHash) {
        throw 'The stock backup did not pass verification.'
    }
} else {
    if ($current -ne $stockHash) { throw 'Expected the supported stock Steam executable.' }
    if ((Test-Path -LiteralPath $backup) -and (Get-FileHash -LiteralPath $backup).Hash -ne $stockHash) {
        throw 'The existing stock backup did not pass verification.'
    }
    if (!(Test-Path -LiteralPath $backup)) {
        Copy-Item -LiteralPath $game -Destination $backup
    }
}

$temp = Join-Path $modRoot ([Guid]::NewGuid().ToString() + '.tmp')
try {
    if ($Uninstall) {
        Copy-Item -LiteralPath $backup -Destination $temp
    } else {
        & (Join-Path $modRoot 'New-ControlsBackground.ps1') -GameRoot $gameRoot
        & (Join-Path $modRoot 'Build-OptionsMod.ps1') -SourceExe $backup -OutputExe $temp
        (Get-FileHash -LiteralPath $temp).Hash | Set-Content -LiteralPath $marker -Encoding ascii
    }
    if ((Get-FileHash -LiteralPath $game).Hash -ne $current) {
        throw 'The game executable changed during installation.'
    }
    [IO.File]::Replace($temp, $game, [NullString]::Value)
} finally {
    if (Test-Path -LiteralPath $temp) { Remove-Item -LiteralPath $temp }
}
if ($Uninstall) { 'Restored the stock executable.' }
else { 'Installed Theocracy Improvements Mod.' }
