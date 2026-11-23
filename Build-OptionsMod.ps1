[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$SourceExe,
    [Parameter(Mandatory)][string]$OutputExe,
    [string]$ModDll = (Join-Path $PSScriptRoot 'TheocracyOptions.dll')
)

$ErrorActionPreference = 'Stop'
$source = (Resolve-Path -LiteralPath $SourceExe).ProviderPath
$output = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputExe)
if (Test-Path -LiteralPath $output) { throw 'Choose a fresh output path.' }
$stockHash = 'D922D2F114B5EF33A1D2B10B5F1EBEBCE2BFC28D9EC933CD8DB4B7F20E1D78E2'
if ((Get-FileHash -LiteralPath $source).Hash -ne $stockHash) {
    throw 'Unsupported executable. Use the clean build described in SPIKE.md.'
}

[byte[]]$stock = [IO.File]::ReadAllBytes($source)
if (!(Test-Path -LiteralPath $ModDll -PathType Leaf)) { throw 'Missing TheocracyOptions.dll.' }

# The stock executable uses a fixed image base and has no relocation section.
# Discard its certificate overlay from the generated test executable.
$pe = [BitConverter]::ToInt32($stock, 60)
$optional = $pe + 24
$sectionCount = [BitConverter]::ToUInt16($stock, ($pe + 6))
$sections = $optional + [BitConverter]::ToUInt16($stock, ($pe + 20))
$newHeader = $sections + 40 * $sectionCount
$raw = [BitConverter]::ToInt32($stock, ($optional + 128))
$rva = [BitConverter]::ToInt32($stock, ($optional + 56))
$baseAddress = [BitConverter]::ToInt32($stock, ($optional + 28))
$rawSize = [BitConverter]::ToInt32($stock, ($optional + 36))
$virtualSize = [BitConverter]::ToInt32($stock, ($optional + 32))

$patched = [byte[]]::new($raw + $rawSize)
[Array]::Copy($stock, $patched, $raw)
function Set-Int32([int]$At, [int]$Value) {
    [BitConverter]::GetBytes($Value).CopyTo($patched, $At)
}
[BitConverter]::GetBytes([uint16]($sectionCount + 1)).CopyTo($patched, ($pe + 6))
Set-Int32 ($optional + 56) ($rva + $virtualSize)
Set-Int32 ($optional + 64) 0
Set-Int32 ($optional + 128) 0
Set-Int32 ($optional + 132) 0
[Text.Encoding]::ASCII.GetBytes('.topt').CopyTo($patched, $newHeader)
Set-Int32 ($newHeader + 8) $rawSize
Set-Int32 ($newHeader + 12) $rva
Set-Int32 ($newHeader + 16) $rawSize
Set-Int32 ($newHeader + 20) $raw
Set-Int32 ($newHeader + 36) 0xE0000060
$shared = $baseAddress + $rva + 0xf00
Set-Int32 ($raw + 0xf04) 100
Set-Int32 ($raw + 0xf08) 100

foreach ($axis in @(
    @{ Hook = 0x166442; Offset = 0; Position = 0x23 },
    @{ Hook = 0x166457; Offset = 64; Position = 0x27 }
)) {
    # .text uses identical raw and virtual offsets in this build.
    $hook = $axis.Hook
    $offset = $axis.Offset
    $remainderAddress = $shared + 12 + (4 * ($offset / 64))
    $code = [Collections.Generic.List[byte]]::new()
    # mov edx,[edi]; push eax; push ecx; mov eax,edx; imul eax,[mouse_percent]
    $code.AddRange([byte[]](0x8B,0x17,0x50,0x51,0x89,0xD0,0x0F,0xAF,0x05))
    $code.AddRange([BitConverter]::GetBytes([int]($shared + 4)))
    # add eax,[remainder]; cdq; mov ecx,100; idiv ecx
    $code.AddRange([byte[]](0x03,0x05))
    $code.AddRange([BitConverter]::GetBytes([int]$remainderAddress))
    $code.AddRange([byte[]](0x99,0xB9,100,0,0,0,0xF7,0xF9))
    # mov [remainder],edx; mov edx,eax; pop ecx; pop eax
    $code.AddRange([byte[]](0x89,0x15))
    $code.AddRange([BitConverter]::GetBytes([int]$remainderAddress))
    $code.AddRange([byte[]](0x89,0xC2,0x59,0x58))
    # Replay the original position load and resume at the next instruction.
    $code.AddRange([byte[]](0x8B,0x46,$axis.Position,0xE9))
    $code.AddRange([BitConverter]::GetBytes(
        [int](($hook + 5) - ($rva + $offset + $code.Count + 4))))
    $code.CopyTo($patched, ($raw + $offset))
    $patched[$hook] = 0xE9
    Set-Int32 ($hook + 1) (($rva + $offset) - ($hook + 5))
}




function Install-Hook([int]$Hook, [int]$Length, [int]$Offset,
                      [Collections.Generic.List[byte]]$Code) {
    $Code.Add(0xE9)
    $Code.AddRange([BitConverter]::GetBytes(
        [int](($Hook + $Length) - ($rva + $Offset + $Code.Count + 4))))
    $Code.CopyTo($patched, ($raw + $Offset))
    $patched[$Hook] = 0xE9
    Set-Int32 ($Hook + 1) (($rva + $Offset) - ($Hook + 5))
    for ($i = 5; $i -lt $Length; $i++) { $patched[$Hook + $i] = 0x90 }
}

# Preserve the stock imports and append the mod DLL to the loader's list.
$oldImportRva = [BitConverter]::ToInt32($stock, ($optional + 104))
# In this stock build the descriptors are in the last section (.rsrc).
$lastHeader = $newHeader - 40
$oldImports = $oldImportRva - [BitConverter]::ToInt32($stock, ($lastHeader + 12)) +
    [BitConverter]::ToInt32($stock, ($lastHeader + 20))
$importOffset = 0x500
$newImports = $raw + $importOffset
$at = 0
while ([BitConverter]::ToInt32($stock, ($oldImports + $at + 12)) -ne 0) {
    [Array]::Copy($stock, ($oldImports + $at), $patched, ($newImports + $at), 20)
    $at += 20
}
$moduleName = 0x800
$symbolName = 0x900
$lookupTable = 0xc00
$addressTable = 0xc40
$modulePath = 'Theocracy Improvements Mod\TheocracyOptions.dll'
[Text.Encoding]::ASCII.GetBytes($modulePath).CopyTo($patched, ($raw + $moduleName))
[Text.Encoding]::ASCII.GetBytes('TheocracyOptions').CopyTo($patched, ($raw + $symbolName + 2))
$cameraSymbol = 0x960
[Text.Encoding]::ASCII.GetBytes('TheocracyCameraHeld').CopyTo($patched, ($raw + $cameraSymbol + 2))
Set-Int32 ($raw + $lookupTable + 12) ($rva + $cameraSymbol)
Set-Int32 ($raw + $addressTable + 12) ($rva + $cameraSymbol)
$keySymbol = 0x940
[Text.Encoding]::ASCII.GetBytes('TheocracyKeyEvent').CopyTo($patched, ($raw + $keySymbol + 2))
Set-Int32 ($raw + $lookupTable + 8) ($rva + $keySymbol)
Set-Int32 ($raw + $addressTable + 8) ($rva + $keySymbol)
$menuSymbol = 0x920
[Text.Encoding]::ASCII.GetBytes('TheocracyMenuAction').CopyTo($patched, ($raw + $menuSymbol + 2))
Set-Int32 ($raw + $lookupTable + 4) ($rva + $menuSymbol)
Set-Int32 ($raw + $addressTable + 4) ($rva + $menuSymbol)
Set-Int32 ($raw + $lookupTable) ($rva + $symbolName)
Set-Int32 ($raw + $addressTable) ($rva + $symbolName)
Set-Int32 ($newImports + $at) ($rva + $lookupTable)
Set-Int32 ($newImports + $at + 12) ($rva + $moduleName)
Set-Int32 ($newImports + $at + 16) ($rva + $addressTable)
Set-Int32 ($optional + 104) ($rva + $importOffset)
Set-Int32 ($optional + 108) ($at + 40)

$code = [Collections.Generic.List[byte]]::new()
# Move Credits and Intro down one normal row before the stock registration.
foreach ($displacement in 0x380, 0x540) {
    $code.AddRange([byte[]](0x83,0x84,0x24))
    $code.AddRange([BitConverter]::GetBytes([int]$displacement))
    $code.Add(30)
}
$code.AddRange([byte[]]$stock[0x0adf7c..0x0adf82])
Install-Hook 0x0adf7c 7 0x420 $code

$menuIat = [BitConverter]::GetBytes([int]($baseAddress + $rva + $addressTable + 4))
$code = [Collections.Generic.List[byte]]::new()
# pushfd; pushad; compute the three controls from the stock menu's stack.
$code.AddRange([byte[]](0x9C,0x60))
foreach ($displacement in 0x378,0x29c,0xc0) {
    $code.AddRange([byte[]](0x8D,0x84,0x24))
    $code.AddRange([BitConverter]::GetBytes([int]$displacement))
    $code.Add(0x50)
}
$code.AddRange([byte[]](0x6A,0,0xFF,0x15))
$code.AddRange($menuIat)
$code.AddRange([byte[]](0x83,0xC4,0x10,0x61,0x9D))
$code.AddRange([byte[]]$stock[0x0ae030..0x0ae035])
Install-Hook 0x0ae030 6 0x100 $code

$code = [Collections.Generic.List[byte]]::new()
# cmp ax,12; jne stock range check; invoke the handler and keep the menu open.
$code.AddRange([byte[]](0x66,0x83,0xF8,0x0C,0x75,0x1A,0x9C,0x60,
    0x6A,0,0x6A,0,0x6A,0,0x6A,1,0xFF,0x15))
$code.AddRange($menuIat)
$code.AddRange([byte[]](0x83,0xC4,0x10,0x61,0x9D,0xE9))
$code.AddRange([BitConverter]::GetBytes([int](0x0ae150 - ($rva + 0x140 + $code.Count + 4))))
# Replay the original compare and jump on above-or-equal.
$code.AddRange([byte[]](0x66,0x83,0xF8,0x0C,0x0F,0x83))
$code.AddRange([BitConverter]::GetBytes([int](0x0ae150 - ($rva + 0x140 + $code.Count + 4))))
Install-Hook 0x0ae13e 6 0x140 $code

$code = [Collections.Generic.List[byte]]::new()
$code.AddRange([byte[]](0x9C,0x60,0x6A,0,0x6A,0,0x6A,0,0x6A,2,0xFF,0x15))
$code.AddRange($menuIat)
$code.AddRange([byte[]](0x83,0xC4,0x10,0x61,0x9D))
$code.AddRange([byte[]]$stock[0x0ae19c..0x0ae1a1])
Install-Hook 0x0ae19c 6 0x3c0 $code

# Move the stock CD Tracks label along with its gem.
Set-Int32 0x0c76c1 228

# Add mod controls at the end of the stock Game Settings constructor.
$code = [Collections.Generic.List[byte]]::new()
$code.AddRange([byte[]](0x9C,0x60,0x6A,0,0x6A,0,0x56,0x6A,3,0xFF,0x15))
$code.AddRange($menuIat)
$code.AddRange([byte[]](0x83,0xC4,0x10,0x61,0x9D))
$code.AddRange([byte[]]$stock[0x0c6f6c..0x0c6f71])
Install-Hook 0x0c6f6c 6 0x180 $code

# Transform keyboard events before the game updates held state and dispatches them.
$keyIat = [BitConverter]::GetBytes([int]($baseAddress + $rva + $addressTable + 8))
$code = [Collections.Generic.List[byte]]::new()
$code.AddRange([byte[]](0x9C,0x60,0x56,0x57,0xFF,0x15))
$code.AddRange($keyIat)
$code.AddRange([byte[]](0x83,0xC4,0x08,0x61,0x9D))
$code.AddRange([byte[]]$stock[0x14fb65..0x14fb6a])
Install-Hook 0x14fb65 6 0x1c0 $code

# Reuse the stock held-key call ABI at the eight map panning queries.
$cameraOffset = 0x220
$cameraIat = $baseAddress + $rva + $addressTable + 12
$code = [Collections.Generic.List[byte]]::new()
# push ecx; push [esp+8]; call [camera IAT]; add esp,8; ret 4
$code.AddRange([byte[]](0x51,0xFF,0x74,0x24,8,0xFF,0x15))
$code.AddRange([BitConverter]::GetBytes([int]$cameraIat))
$code.AddRange([byte[]](0x83,0xC4,8,0xC2,4,0))
$code.CopyTo($patched, ($raw + $cameraOffset))
foreach ($site in 0x0dd23e,0x0dd27b,0x0dd2b7,0x0dd2fc,0x121864,0x121890,0x1218bb,0x1218e5) {
    $target = $site + 5 + [BitConverter]::ToInt32($stock, ($site + 1))
    if ($stock[$site] -ne 0xe8 -or $target -ne 0x04f330) {
        throw ('Unexpected held-key call at RVA 0x{0:X}' -f $site)
    }
    Set-Int32 ($site + 1) (($rva + $cameraOffset) - ($site + 5))
}

# Skip only the startup movie batch. The Intro menu command has a separate call.
# Its original caller cleans up the last movie's four arguments at 0x93DD9.
$code = [Collections.Generic.List[byte]]::new()
$code.AddRange([byte[]](0x9C,0x60,0xFF,0x15)) # pushfd; pushad; call [IAT]
$code.AddRange([BitConverter]::GetBytes([int]($baseAddress + $rva + $addressTable)))
$code.AddRange([byte[]](0x85,0xC0,0x74,0x0A)) # test eax,eax; jz stock sequence
$code.AddRange([byte[]](0x61,0x9D,0x83,0xEC,0x10,0xE9))
$code.AddRange([BitConverter]::GetBytes([int](0x93DD2 - ($rva + 0x380 + $code.Count + 4))))
$code.AddRange([byte[]](0x61,0x9D))
$code.AddRange([byte[]]$stock[0x93D6C..0x93D73])
Install-Hook 0x93D6C 8 0x380 $code

# Route the province world pass and its local mouse coordinates through the mod.
$zoomImports = @(
    @{ Name = 'TheocracyZoomDraw'; Symbol = 0x9c0; Offset = 0xb00; Index = 16 },
    @{ Name = 'TheocracyZoomPoint'; Symbol = 0x9e0; Offset = 0xb40; Index = 20 },
    @{ Name = 'TheocracyZoomWheel'; Symbol = 0xa00; Offset = 0xb80; Index = 24 }
    @{ Name = 'TheocracyFocusLocation'; Symbol = 0xa20; Offset = 0xd00; Index = 28 }
    @{ Name = 'TheocracyRecallGroup'; Symbol = 0xa40; Offset = 0xd40; Index = 32 }
)
foreach ($entry in $zoomImports) {
    [Text.Encoding]::ASCII.GetBytes($entry.Name).CopyTo($patched, ($raw + $entry.Symbol + 2))
    Set-Int32 ($raw + $lookupTable + $entry.Index) ($rva + $entry.Symbol)
    Set-Int32 ($raw + $addressTable + $entry.Index) ($rva + $entry.Symbol)
}
function Install-CallRedirect([int]$Site, [int]$StockTarget, [int]$Offset) {
    if ($stock[$Site] -ne 0xe8 -or
        ($Site + 5 + [BitConverter]::ToInt32($stock, ($Site + 1))) -ne $StockTarget) {
        throw ('Unexpected call at RVA 0x{0:X}' -f $Site)
    }
    Set-Int32 ($Site + 1) (($rva + $Offset) - ($Site + 5))
}
foreach ($index in 0,1) {
    $entry = $zoomImports[$index]
    $code = [Collections.Generic.List[byte]]::new()
    # Duplicate thiscall arguments in cdecl order and return with stock cleanup.
    if ($index -eq 0) { $code.AddRange([byte[]](0xff,0x74,0x24,8)) }
    $code.AddRange([byte[]](0xff,0x74,0x24,(4 + 4 * (1 - $index)),0x51,0xff,0x15))
    $code.AddRange([BitConverter]::GetBytes([int]($baseAddress + $rva + $addressTable + $entry.Index)))
    $code.AddRange([byte[]](0x83,0xc4,(12 - 4 * $index),0xc2,(8 - 4 * $index),0))
    $code.CopyTo($patched, ($raw + $entry.Offset))
}
Install-CallRedirect 0xfa687 0xd5240 0xb00
foreach ($site in 0xfa772,0xfa7a7,0xfa851,0xfa881,0xfa8ad) {
    Install-CallRedirect $site 0x14f160 0xb40
}
$code = [Collections.Generic.List[byte]]::new()
# DirectInput mouse Z offset is 8. Preserve the stock switch and event loop.
$code.AddRange([byte[]](0x8b,0x47,0xfc,0x83,0xf8,8,0x75,0x0d,0x9c,0x60,0xff,0x37,0xff,0x15))
$code.AddRange([BitConverter]::GetBytes([int]($baseAddress + $rva + $addressTable + 24)))
$code.AddRange([byte[]](0x83,0xc4,4,0x61,0x9d,0x83,0xf8,0x0e))
$code[7] = 15
Install-Hook 0x1663a7 6 0xb80 $code
foreach ($index in 3,4) {
    $entry = $zoomImports[$index]
    # Wrap a thiscall with two stack arguments in the matching cdecl export.
    $code = [Collections.Generic.List[byte]]::new()
    $code.AddRange([byte[]](0xff,0x74,0x24,8,0xff,0x74,0x24,8,0x51,0xff,0x15))
    $code.AddRange([BitConverter]::GetBytes([int]($baseAddress + $rva + $addressTable + $entry.Index)))
    $code.AddRange([byte[]](0x83,0xc4,12,0xc2,8,0))
    $code.CopyTo($patched, ($raw + $entry.Offset))
}
Install-CallRedirect 0xfaa64 0xdd170 0xd00
Install-CallRedirect 0xd7f23 0xc8c70 0xd40
foreach ($hook in @(
    @{Name='TheocracyProbeWait'; Symbol=0xa60; Index=36; Site=0xd5cf3; Target=0x15b9e0; Stub=0xd80},
    @{Name='TheocracyProbePresent'; Symbol=0xa80; Index=40; Site=0xf90d9; Target=0x14f3e0; Stub=0xdc0},
    @{Name='TheocracyProbeFlip'; Symbol=0xaa0; Index=44; Site=0x150237; Target=0x14b4a0; Stub=0xe00}
)){
    [Text.Encoding]::ASCII.GetBytes($hook.Name).CopyTo($patched, ($raw+$hook.Symbol+2))
    Set-Int32 ($raw+$lookupTable+$hook.Index) ($rva+$hook.Symbol)
    Set-Int32 ($raw+$addressTable+$hook.Index) ($rva+$hook.Symbol)
    $iat=[BitConverter]::GetBytes([int]($baseAddress+$rva+$addressTable+$hook.Index))
    $code=[Collections.Generic.List[byte]]::new()
    if ($hook.Index -eq 36) {
        $code.AddRange([byte[]](0xff,0x25))
        $code.AddRange($iat)
    } else {
        $code.AddRange([byte[]](0x51,0xff,0x15))
        $code.AddRange($iat)
        $code.AddRange([byte[]](0x83,0xc4,4,0xc3))
    }
    $code.CopyTo($patched, ($raw+$hook.Stub))
    Install-CallRedirect $hook.Site $hook.Target $hook.Stub
}
[IO.File]::WriteAllBytes($output, $patched)
