# Theocracy Improvements Mod

Theocracy Improvements Mod adds mouse speed and intro-video settings,
editable controls, control-group camera focus, mouse-wheel zoom,
optional higher frame rates, and a widescreen province viewport.

## Install on Windows

1. Install the Steam release of Theocracy and close the game. In Steam,
   open **Properties → Installed Files → Browse** to locate the game folder.
   If you already have the mod installed, uninstall that version first
   using the instructions below.
2. Download this repository and build the package from its root. Use
   PowerShell 7 and [Zig](https://ziglang.org/download/).

```powershell
New-Item -ItemType Directory -Force build | Out-Null
zig cc -target x86-windows-gnu -O2 -Wall -shared TheocracyOptions.c -o build/TheocracyOptions.dll -luser32
Copy-Item Build-OptionsMod.ps1, Install-OptionsMod.ps1, New-ControlsBackground.ps1 build
Copy-Item keybinds.example.cfg build/keybinds.cfg
```

3. Create a folder named `Theocracy Improvements Mod` alongside
   `theocracy-0.6.85.exe` in the Steam game folder. Copy the contents
   of `build` into it. When upgrading, keep your existing `Options.ini`
   and `keybinds.cfg`.
4. Open a PowerShell terminal in the **game folder** and run the installer.
   The execution-policy option applies only to the PowerShell process
   running this command.

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File '.\Theocracy Improvements Mod\Install-OptionsMod.ps1'
```

5. Launch Theocracy through Steam. You can also run the patched
   `theocracy-0.6.85.exe` directly from the game folder while Steam is
   running. The installer generates it from your verified stock executable
   and keeps a stock backup. It uses the mod DLL and assets in that
   installation.

## Uninstall or upgrade

Close Theocracy and run this command from the game folder before
replacing the mod files. It restores the verified stock executable.

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File '.\Theocracy Improvements Mod\Install-OptionsMod.ps1' -Uninstall
```

## Use Wide Viewport

To use Wide Viewport, turn it on under Game Settings, press OK, and
restart the game. Province gameplay uses the wider canvas. The side
panel opened with Space anchors to its far-right edge.

## Development

The separate Theocracy harness stages an isolated copy with
`--package <path-to-build>`. See [PACKAGING.md](PACKAGING.md) for the
distribution manifest and [NativeTrace.md](NativeTrace.md) for verified
game addresses.
