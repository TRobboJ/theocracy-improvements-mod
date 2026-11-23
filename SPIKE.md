# Theocracy Options mod spike

Target: SNEG Steam release, app 4834790, theocracy-0.6.85.exe.
Executable SHA-256: D922D2F114B5EF33A1D2B10B5F1EBEBCE2BFC28D9EC933CD8DB4B7F20E1D78E2.

Run the read-only probe from this directory:

    powershell -File .\Find-OptionsAnchors.ps1

The script reports candidate x86 PUSH immediate references as executable RVAs.
Confirm instruction boundaries and control flow in an x86 disassembler before
using any address. These addresses are leads, not patch or hook addresses.

| Candidate RVA | Lead | What to verify |
| --- | --- | --- |
| 0x000AD734 | data/menu/menu.cfg | Follow to menu creation and command dispatch. |
| 0x000C7270 | gamesettings_title | Identify dialog creator and required gameplay state. |
| 0x000C73F0 | gamesettings_scroll | Trace control ID through update and application. |
| 0x000C6960 | gamesettings_scrolltype | Check what the existing scroll mode changes. |
| 0x0007E327, 0x0007E44C | .gamesettings | Follow read/write calls to determine storage and layout. |
| 0x00166372 | GetDeviceData failed (mouse) | Follow the successful path to relative cursor motion. |

The 156-byte menu.cfg starts with RSA4096 and the payload is opaque. Its
format and what the parser accepts are unverified. Start by tracing the menu
builder and dispatch rather than editing this file as text.

## Proposed implementation

1. Add an Options command to the title menu using the verified menu builder.
   Open an independent dialog if the existing game settings dialog requires
   an active match. Back or Escape should return to the title menu.
2. Add Mouse Speed and Mouse Border Scroll Speed. Start with proposed
   ranges of 25–200%, in 5% steps, with 100% as the default. Keep settings in
   a separate mod-owned file. Validate values before passing them to the
   scaling helpers. Apply saves the two values and Cancel discards edits.
3. In the successful mouse input path, scale signed relative X and Y deltas
   before the game clips cursor coordinates. Keep one MouseAxis per axis.
   Clear their remainders when changing profiles or acquiring input again.
   Pass buttons and wheel input through unchanged.
4. Trace from the cursor edge check to camera movement. Apply the edge
   multiplier only to edge-generated velocity, before rounding. If the game
   uses integer camera deltas, accumulate fractional movement independently
   for each camera axis. Keep the existing edge thresholds, timing, and map
   bounds.
5. Verify 100% against the clean build, then test 50% and 150% independently.
   Check slow motion, diagonal motion, window focus, map corners, keyboard
   panning, and settings persistence across restarts.

InputScaling.h implements the scaling math only. No native menu code,
runtime hooks, or installer are present. No game files need to be replaced
to run the probe.
