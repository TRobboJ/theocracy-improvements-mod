[CmdletBinding()]
param(
    [string]$GameRoot = (Split-Path -Parent $PSScriptRoot),
    [string]$Output = (Join-Path $PSScriptRoot 'ControlsBackground.raw')
)
$ErrorActionPreference = 'Stop'
$source = Join-Path $GameRoot 'data\realm\panel\options.raw'
$width = 563
$height = 397
$pixelOffset = 800
[byte[]]$stock = [IO.File]::ReadAllBytes($source)
if ($stock.Length -ne ($pixelOffset + $width * $height) -or
    ([Text.Encoding]::ASCII.GetString($stock, 0, 6) -ne 'mhwanh') -or
    ($stock[8] * 256 + $stock[9]) -ne $width -or
    ($stock[10] * 256 + $stock[11]) -ne $height) {
    throw 'Unexpected Game Settings background format.'
}
[byte[]]$clean = $stock.Clone()

# Copy unmarked stone from the same rows. The patterned outer border,
# title plate, palette and transparency are retained byte for byte.
foreach ($patch in @(
    @{ Left = 228; Top = 43; Width = 56; Height = 320; SourceLeft = 290 },
    @{ Left = 34; Top = 285; Width = 24; Height = 25; SourceLeft = 63 }
)) {
    for ($y = $patch.Top; $y -lt $patch.Top + $patch.Height; ++$y) {
        $row = $pixelOffset + $width * $y
        [Array]::Copy($stock, ($row + $patch.SourceLeft),
                     $clean, ($row + $patch.Left), $patch.Width)
    }
}
$outPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Output)
[IO.File]::WriteAllBytes($outPath, $clean)
