# Theocracy Improvements Mod

This folder is the development source for the Theocracy mod. The
separate theocracy-harness folder handles isolated tests.

The source and patch builder are based on the supported Steam x86
executable. The builder verifies that input by SHA-256. The development
source includes control-group camera focus. See PACKAGING.md for
the files needed to build or distribute a release, and NativeTrace.md
for verified game addresses.

Build the DLL with a 32-bit Windows toolchain. For example, with Zig
available on PATH:

```powershell
New-Item -ItemType Directory -Force build
zig cc -target x86-windows-gnu -O2 -Wall -shared TheocracyOptions.c -o build/TheocracyOptions.dll -luser32
Copy-Item Build-OptionsMod.ps1, Install-OptionsMod.ps1, New-ControlsBackground.ps1 build
Copy-Item keybinds.example.cfg build/keybinds.cfg
```

Point the harness at build with its --package option to stage an
isolated copy. For a Steam install, keep the DLL, builder, installer
and asset generator in the game's Theocracy Improvements Mod folder.
Keep your own Options.ini and keybinds.cfg local. The stock executable
and game assets are supplied by the local game installation.
