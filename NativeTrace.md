# Native integration trace

These are RVAs for the executable hash in SPIKE.md, decoded as 32-bit x86.
No executable patch is applied by this trace.

## Title menu

- 0x000AD734 loads the menu config path.
- The title menu constructs button objects on the stack. The constructor
  sequence beginning near 0x000AD857 binds a config key to a rectangle and
  passes command 2 to the control constructor. Near 0x000AD933 and
  0x000ADA0E the same pattern passes command 3 and command 4.
- The calls to RVA 0x0004BED0 between 0x000ADF8B and 0x000AE02B register
  the constructed controls. Trace the command returned by the event loop
  beginning near 0x000AE0A2 before allocating a new command. Adding a key
  to menu.cfg alone cannot construct a new button.

## Mouse motion

In the buffered DirectInput path, ESI is the mouse object and EDI points
to the current event's data. The X and Y handlers are separate:

- X (0x00166442): load signed delta from [EDI] into EDX, load position from
  [ESI+0x23], then add the delta at 0x0016644A.
- Y (0x00166457): load signed delta from [EDI] into EDX, load position from
  [ESI+0x27], then add the delta at 0x0016645C.
- Both paths converge at 0x0016646A. Scaling belongs before those adds.
  The button handlers occupy separate branches in the same event loop.

Before release, verify these paths at runtime, identify any alternate
input backend, and locate the edge-pan velocity update. The existing
gamesettings_scroll reference alone does not identify that update.

## Edge scrolling

The province camera path reads its scroll mode from 0x006A3F64. The
branches from RVA 0x000DD3F8 check cursor bounds of x=0 or 799 and y=0
or 599. Mode 0 doubles a preexisting movement component when the
pointer is at an edge. Mode 1 assigns signed half-speed components
on the edge and reuses stored values between checks. At 0x000DD521
the path combines these components with another movement vector.
Scale at the individual edge branches so keyboard motion does not get
scaled just because it reaches the same camera update. Verify which
modes the shipped UI exposes and trace the realm map independently.

## Title-menu insertion constraints

The stock button keys are single, multi, intro, load, credits, help, exit,
tutorial, and scenario. Nine calls register stack-allocated controls.
The dispatch at 0x000AE0B6 reads a 16-bit command from control offset
0x38. Command 7 invokes the existing help path without terminating the
menu. Other positive commands below 12 exit the loop through 0x000AE149.
A new command needs its own branch before this range check, so dismissing
Options can return to the same menu. A tenth control also needs storage
whose lifetime spans registration, drawing, event handling, and teardown.

### Mouse-wheel zoom investigation

The province viewport draws the world at VA 0x004FA687 through 0x004D5240,
then draws its foreground art. Keep the zoom render and composite inside
that call boundary. The world renderer uses the live viewport rectangle
at control + 0x28 and the province camera position at +0x40DA7.
The requested values represent camera distance: 70% is closer, 100% is
stock, and 150% is farther. At 150%, a 640 × 546 viewport needs a
960 × 819 world sample. Shrinking the stock frame leaves the extra
world area unavailable.

The viewport event dispatcher at 0x004FA730 uses 0x0054F160 for local
pointer positions. That function samples the engine cursor through
0x004E2C40, subtracts the control origin, and returns the point through a
caller-supplied buffer. To preserve HUD interaction, apply any inverse
zoom transform at the viewport-specific call sites after verifying the
other world picking and drag paths. Stock rendering and hit tests must
remain paired at 100%.

Pending: identify a scratch render target compatible with terrain, units,
and the alternate view modes, and verify which input path supplies the
wheel in an active province. No executable or mod DLL changes for zoom
have been staged.

### Zoom implementation and live check

The live normal-view world draw uses the 16-bit software surface vtable
at 0x005898E8. Width and height are at +0x04/+0x08, pixels at +0x0C,
and row pitch at +0x14. The test DLL temporarily redirects the surface
and expands or shrinks the province rectangle, then restores both
before compositing the world and returning to the stock foreground draw.
The DirectInput event switch delivers wheel deltas at mouse offset 8.
The player confirmed wheel zoom and fixed HUD in the isolated game.

### Camera cadence investigation

Province keyboard panning runs at VA 0x004DD230, reached from the
province input handler for event 0x40 at VA 0x004DD3F3. It adds integer
deltas from the stock scroll speed (horizontal 32 and vertical 20
pixels scaled by the setting as a percentage). The keyboard and edge
components are applied together at VA 0x004DD52B in Normal View. A
test-only DLL under camera-cadence records the draw timer and original
camera coordinates at the existing world-draw hook. Read-Cadence.ps1
summarizes world draw intervals, camera advances, and typical pixel
steps. World draw calls are not a measurement of displayed frames.

Live diagnostic at 100% camera distance with the OpenGL wrapper measured
a median 83.95 ms between world draws and 83.97 ms between camera
advances. The most common straight horizontal step was 31 pixels.
An observed diagonal step was 31 by 19 pixels. The trace establishes
the world update cadence and step size for this test, and leaves the
main-loop limiter and display presentation cadence to be isolated.

Disabling the wrapper tick limiter gave 83.82 ms world draw and
83.88 ms camera advance medians. The test config was restored.
The native province loop at VA 0x004D5CB9 constructs an 83,333 µs
interval (0x14585), compares it against elapsed time, and sleeps for
the remaining time at 0x004D5CF3. The same loop then invokes province
and unit update routines. Keep the simulation interval intact while
investigating independent camera and render updates.

### Control-group camera focus

The battle button's call at RVA 0xFAA64 and the group recall at RVA
0xD7F23 are redirected through DLL exports. Both use the native province
focus routine at VA 0x004DD170, with Normal View world pixels. The latter
updates the existing camera scroll vector and applies native view scaling.
The stock box-selection routine at VA 0x004D7BF0 uses the same unit
rectangle getter at VA 0x00498B70 against a world-space selection rectangle.

The group dispatch first runs native recall at VA 0x004C8C70. It uses the
stock group member iterator and leader test at VA 0x00407860 after pruning.
A same-key double tap is tracked in the local input hook, and a province
and local player check prevents remote recall from consuming the gesture.
The gesture uses a 350 ms interval. Ctrl assignment and single taps
follow the original dispatch. Both hook thunks retain two-argument
thiscall stack cleanup.

## Bitmap font format

The stock data/fonts directory contains eight MVOSFONT .mft files.
The Steam x86 loader at VA 0x0054C170 checks the eight-byte magic
through 0x0054CA90 and handles single- and multi-palette variants.
The decoder at 0x0054CAE0 reads 256 glyph records at file offset 12,
each containing a 32-bit data offset and 16-bit width and height.
It registers each width and height with the native sprite/font system.
The existing mod uses the stock draw and measure functions. A same-size
replacement can preserve layout. Larger glyph records affect native
metrics. A sharper output-resolution font path needs separate raster
dimensions and logical measurements. No font assets were changed.

### Independent province rendering prototype

The province screen loop at VA 0x004F8D10 calls the tick worker
0x004D5BF0 at VA 0x004F9075, then the end-frame routine 0x0054F3E0
at VA 0x004F90D9. The tick worker waits at VA 0x004D5CF3 before incrementing
the province counter. Stock end-frame does more than draw. It calls
0x00550130, which handles queued keyboard and mouse input and dispatches
an engine event before presenting. Re-entering end-frame between ticks
would therefore also change the input and event cadence.

The isolated work/fps-probe build records the active UI controller at
the original end-frame call and leaves that call in place. During the
stock wait it tries the narrower paint/present path using root dirty
marking (VA 0x0054EEC0), tree paint (VA 0x0054EEF0), and graphics present
(VA 0x0054B4A0). It keeps the original wait deadline and leaves the tick
counter and all game update calls in their original positions. This
first experiment redraws snapshots. It does not interpolate the camera
or implement a fixed 30 FPS cap, 60 FPS, or a settings control.

The isolated straight-pan comparison recorded 11.796 world draws/s and
11.796 simulation ticks/s in stock. The probe recorded 34.846 world
draws/s and 11.803 simulation ticks/s. Camera movements still occurred
at about 11.8 Hz in both builds. The extra snapshot path is observable
without a measured change to the tick rate in this sample. Presentation
cadence at the display, gameplay equivalence beyond the tick counter,
steady frame pacing, and intermediate visual camera positions remain
unverified.

### Interpolated 30 Hz presentation probe

The isolated work/fps-interpolation build captures consecutive province
camera positions at the stock end-frame call. The scheduled paint pass
temporarily interpolates between those snapshots and restores the
simulation camera before presenting. The stock end-frame still runs its
input and event processing. Only its graphics present call at VA
0x00550237 is suppressed while the independent schedule is active.
The interpolation trails by about one simulation tick.

Straight and diagonal harness captures measured 29.991 and 30.008
graphics present calls/s and 11.883 and 11.890 simulation ticks/s.
Typical horizontal camera advances fell from 31 to 12 pixels.
A separate desktop pixel sample during a straight pan measured 11.33
visible changes/s in stock and 27.54 changes/s with interpolation.
The latter captures changes in a sampled region of the desktop image,
not hardware scanout. The prototype is limited to Normal View.
Hit testing with the interpolated camera, 60 Hz scheduling and the
native settings control remain outside this probe.

### Frame-rate selector and 144 Hz measurement

The development build uses the native settings panel and persists
FrameRate=0, 30, 60, or 144 in Options.ini. After zoom correction, the
viewport point hook adds the difference between the last submitted
camera and the authoritative camera. A live 60 FPS pan with mouse input
observed nonzero offsets up to 38 pixels. The stock simulation camera
is restored around every extra paint. The probe records submitted frame
positions at VA 0x006C3F80 for local profiling.

After selecting 144 FPS and restarting, the completed-present counter
measured about 142.17 submissions/s while the province tick counter
measured 11.74 ticks/s. The short run's median horizontal camera step
was two pixels. The 144 Hz trace counts skipped polling samples using
the wrapping submission counter. No hardware scanout rate is claimed.
