# DOS port bring-up

The DOS build links the full machine into `QUASI88.EXE`. It has an optional VGA
640x480 16-color display, BIOS-polled keyboard, and DOS mouse-driver path, as
well as bounded headless startup checks. `Q88TEST.EXE` separately exercises
the Z80 core and file backend. The original Hello World test remains available.

## Headless machine startup (milestone 2)

From the repository root in PowerShell:

```powershell
.\dos\build.ps1 -Target Machine -WatcomRoot D:\watcom
.\dos\machtest.ps1 -DosBoxX D:\DOSBox-X\dosbox-x.exe
.\dos\machtest.ps1 -DosBoxX D:\DOSBox-X\dosbox-x.exe -RomDirectory D:\88rom
.\dos\machtest.ps1 -DosBoxX D:\DOSBox-X\dosbox-x.exe -RomDirectory D:\88rom -Mk2srDirectory D:\88rom\pc8801mk2sr -Cycles 12000
```

The first test uses generated synthetic ROM programs. The second copies only
the `.ROM` files from your chosen directory into a fresh ignored test directory.
Original files remain outside the DOSBox-X mount; no disk images are mounted.
Each run tests missing and truncated main ROM rejection, then starts the machine
for three frames and checks a clean exit to DOS. Logs are retained as
`MISSING.OUT`, `SHORT.OUT`, `MACHINE.OUT`, and `RESULT.TXT` in the printed test
directory. Use `-Cycles 12000` to repeat at the second tested CPU speed.

For the user's split model set, `-Mk2srDirectory` overlays `N88_1.ROM`,
`N88_2.ROM`, `N88_3.ROM`, and `KANJI2.ROM` onto the base ROM copy and renames
`mk2sr_n88.rom` to `N88.ROM` in that copy only. Each extension is 8192 bytes,
the model main ROM is 32768 bytes, and `KANJI2.ROM` is 131072 bytes. The model
main ROM differs from the base directory's `N88.ROM`; selecting it explicitly
keeps this model test reproducible and gives the DOS copy an 8.3 name.

The synthetic main and sub ROMs write distinct RAM markers. Their test uses the
existing `-cpu 2` option because these tiny programs do not perform the PIO
handshake that normally schedules the sub CPU. Real-ROM tests use the core's
default scheduling. Both paths execute the existing PC-8801 machine code;
neither substitutes CPU or device implementations.

To inspect the VGA path interactively, mount a test directory containing the
executable and its `ROM` directory:

```dos
mount c "D:\QUASI88\build-dos\machine-<printed identifier>"
c:
QUASI88 -noconfig -nosaveconfig -v2 -romdir ROM -verbose 1 -dosvga -dosframes 0
```

`-dosvga` selects BIOS mode 12h and converts the core's 8-bit rendered frame to
the standard 16-color VGA palette, then writes the four planes through VGA
memory. `-dosframes 0` runs until QUASI88 exits; the default remains three
frames for repeatable headless tests. BIOS keyboard polling maps ASCII and
selected special keys into the core and is intended for menu/BASIC interaction.
F12 maps to the PC-88 system menu, and F11 maps to system status. Input uses
the enhanced BIOS keyboard services (INT 16h AH=10h/11h/12h) so F11/F12 are
available on an enhanced AT keyboard. Both common scan-code forms for those
keys are accepted. Input is
polled once per emulated frame, so simultaneous-key gaming input is not yet
supported. Normal exit restores the prior BIOS video mode. Avoid Ctrl-C or
forced process termination if you want the display mode restored.

For physical keyboard and mouse diagnosis, run `RUN_Q88.BAT` from the folder
containing `QUASI88.EXE` and `ROM`. The batch contains only the emulator command
and `PAUSE`. It records BIOS keyboard events in `KEYS.LOG` and DOS mouse-driver
availability, pointer coordinates, and button changes in `MOUSE.LOG`. The mouse path polls the
installed INT 33h driver while VGA is active; without a driver, keyboard
operation continues normally. Left, right, and middle buttons map to QUASI88's
existing mouse events, including toolbar clicks. Ctrl+Alt+Q requests an
emergency normal shutdown to DOS.

`-dosvideochk` checks rendered VGA planes before exit and requires `-dosvga`.
The `-VgaTest` test-script switch runs that check with synthetic or copied real
ROMs. `-doscheck` remains exclusively for generated fixtures. The backend uses
standard VGA BIOS mode 12h and no i740-specific registers. DOSBox-X success does
not establish compatibility with the user's physical Celeron 600/i740 machine,
which remains untested.

### Scope and ROM checks

The standard machine core, ROM loader, main/sub CPUs, video renderer, disk
controller code, and menu code are linked. Sound and monitor support are
disabled using existing build options. Startup, frame execution, and shutdown
use the normal core lifecycle. DOS-specific code remains under `sysdepend/dos`
and `osdepend/dos`; desktop build files and existing core sources are unchanged.

The DOS entry point requires a 32768-byte `N88.ROM`, or `N88N.ROM`/`N80.ROM`
when `-n` selects N-BASIC, before starting the core. This avoids silently running
with a missing or truncated main ROM. Combined ROM files are not supported in
this milestone. Other ROMs retain the upstream loader's behavior: missing data
can be filled with `FF`, and the built-in font can substitute for `FONT.ROM`.
Passing startup therefore does not prove ROM completeness or a working BASIC
prompt. Use `-verbose 1` to see each ROM loading result.

Joystick and audio output are not implemented. Mouse input requires an installed
DOS INT 33h driver and is polled once per emulated frame. Frame pacing reads the
BIOS tick and PIT channel 0 counter without reprogramming the timer or hooking
interrupts. It busy-polls the hardware timer for sub-frame waits, so it uses
the CPU while waiting; DOS has no portable high-resolution sleep path in this
backend. The VGA backend changes the BIOS mode and restores it on normal exit.
The QUASI88 toolbar is visible on the reported physical PC. DOS mouse input
polls the standard INT 33h driver while VGA is active and forwards absolute
pointer movement and button transitions through the existing screen/UI event
path. The user confirmed physical pointer movement and clicks, and reported
cursor residue while crossing toolbar icons. The VGA backend now hides the
driver cursor around planar screen updates to avoid stale saved-background
pixels; this cleanup passes the DOSBox-X VGA readback test and needs physical
reconfirmation. No interrupt vectors, PIT, DMA, or sound registers are modified.
D88 image mounting, FDC sector read/write, and image-file writes now have a
focused DOSBox-X fixture test using the core's FDC port interface. Guest-driven
disk commands, save states, configuration saving, and snapshots remain
unvalidated as machine features. Keep writable user media outside test mounts.

### Build and validation record

2026-09-29: `.\dos\build.ps1 -Target Machine -WatcomRoot D:\watcom` compiled
93 translation units and linked `build-dos\QUASI88.EXE` (826408 bytes) with
Open Watcom v2 and CauseWay. `dos/sources.txt` is the explicit machine source
manifest. Objects and per-source warning files go under `build-dos/machine`.
The script invokes `wcl386 -y -c -bt=dos -3r -j -w4` with its explicit include
paths and object names; new DOS sources additionally use `-we`. It writes
`MACHINE.LNK` and invokes `wlink @MACHINE.LNK` with `system causeway`, a map
file, and a 128 KiB stack. The embedded extender needs no separate runtime EXE.

`-j` is Watcom's documented signed-plain-char option. It preserves the core's
use of `-1` in `char` fields (notably recorded disk-image selection), removing
an always-false comparison warning without changing core source. Remaining
upstream warnings were reviewed: 217 unused-parameter warnings, 30 unused-symbol
warnings, 7 expressions made ineffectual by no-sound macros, 2 unreachable-code
warnings, and one missing-return warning in `menu_volume`. The latter function
unconditionally returns its no-sound widget with this build's
`xmame_has_sound() == FALSE`; no fall-through is reachable. New DOS sources have
zero warnings; the build has zero errors. These warnings remain visible.

DOSBox-X 2026.08.31, normal core, emulated 386 and 16 MB RAM: synthetic tests
and copied real-ROM startup both passed at 3000 and 12000 fixed cycles. Each
run rejected missing and one-byte main ROMs with exit code 1, completed the
three-frame success case with exit code 0, and returned cleanly to DOS.
Synthetic tests verified main RAM marker `5A` and sub RAM marker `A5`.
This checks startup at two speeds, not timing accuracy or sustained operation.

The user-supplied set contained `N88.ROM`, `N88_0.ROM`, `N80.ROM`, `DISK.ROM`
(2 KiB), `KANJI1.ROM`, and `FONT.ROM`; all six loaded successfully. Extension
ROMs 1 through 3 and the second kanji ROM were reported missing. No ROM data is
included in source control. Successful three-frame execution is not a verified
BASIC boot, display test, disk test, or real-hardware result.

Follow-up: the `pc8801mk2sr` overlay test at 12000 cycles loaded all ten ROMs
listed by the core with `OK`, including extensions 1-3 and the second kanji ROM.
It passed the same missing/truncated-main-ROM tests and completed three frames
with a clean shutdown. The earlier missing-ROM observations above describe only
the base-directory test; the overlay resolves those missing files. The original
ROM directories were not modified.

### VGA display and BIOS keyboard milestone

2026-09-29: `dos/build.ps1 -Target Machine -WatcomRoot D:\watcom` rebuilt the
full machine with the flat CauseWay memory model required for VGA memory at
physical `A0000h`; new DOS sources compiled with warnings treated as errors.
`dos/machtest.ps1 -DosBoxX D:\DOSBox-X\dosbox-x.exe -Cycles 3000 -VgaTest`
passed synthetic plane readback and text-mode restoration. The same test with
`-RomDirectory D:\88rom -Mk2srDirectory D:\88rom\pc8801mk2sr` passed after all
ten model ROMs loaded. These bounded tests establish the graphics-memory path
and normal mode restoration in DOSBox-X; visual quality, usable live input,
sustained operation, and physical VGA/i740 behavior still need validation.
The synthetic VGA smoke test also found DOSBox-X's INT 33h mouse driver through
the `-dosmouselog` diagnostic. It did not automate pointer movement or clicks.

For hardware testing, copy `QUASI88.EXE`, `LICENSE.TXT` (the repository license),
`VGA_TEST.BAT`, `RUN_Q88.BAT`, and your own ROMs in a `ROM` subdirectory to a
writable DOS directory. Run `VGA_TEST` for bounded video/readback and clean
return checks, then `RUN_Q88` for interactive use. Both pause after QUASI88
returns so the result remains visible. A 386-or-newer CPU is required; tested
emulated RAM is 16 MB, not an established minimum. Report CPU, RAM, DOS version,
memory managers, ROM filenames/sizes, and the complete console output. No
hardware result has been reported. The VGA display path is available with
`-dosvga`; its physical VGA/i740 performance remains unverified.

## Build and test the initial port

From the repository root in PowerShell:

```powershell
.\dos\build.ps1 -Target PortTest -WatcomRoot D:\watcom
.\dos\test.ps1 -DosBoxX D:\DOSBox-X\dosbox-x.exe
```

This builds `build-dos\Q88TEST.EXE` with the CauseWay extender embedded. The
test runner uses a new directory under `build-dos` for every run, copies only
the executable, license, and test batch file there, and mounts only that
directory. It records `PORT.OUT` and `RESULT.TXT`, checks the exit status and
test output, and closes DOSBox-X. Results are retained in the directory printed
by the runner. No user ROM or disk image is required or accessed.

To run manually in DOSBox-X:

```dos
mount c D:\QUASI88\build-dos
c:
Q88TEST
```

Expect these lines and a return to the DOS prompt:

```text
QUASI88 DOS bring-up: Z80 core + file backend
PASS: DOS file access, paths, directory listing, duplicate protection
PASS: unchanged Z80 core (1000 emulated states)
PASS: DOS bring-up
```

The test creates `IOCHK.TMP` in its current directory and removes it when done.
It refuses to overwrite an existing file of that name. A test failure prints
`FAIL` with a diagnostic and returns exit code 1.

### Implemented and tested

- `src/sysdepend/dos/config.h`: Watcom 386 DOS target checks, little-endian
  layout, and initial compile-time configuration. No SDL dependency.
- `src/osdepend/dos/file-op.c`: DOS paths, binary and text stream wrappers,
  file/directory attributes, directory enumeration, configurable search
  directories, and case-insensitive duplicate disk-handle protection.
  Runtime tests cover binary round trips, seek/rewind, EOF, missing files,
  directory listing, path bounds, and conflicting opens.
- The existing `src/pc88/z80.c` is compiled unchanged. The synthetic program
  checks arithmetic flags, memory access, stack balance, IX/CB instructions,
  I/O callbacks, and HALT. This is a smoke test, not exhaustive Z80 validation.

Watcom's DOS runtime provides `_fullpath`, `stat`, and the directory functions;
their declarations were checked in the installed headers and their use tested
under DOSBox-X. No POSIX operating system or Windows API is used in the target.

### Current limits and next milestones

Only ASCII 8.3 filenames are accepted. Long names are rejected rather than
silently truncated; Japanese filenames and long-filename services are not
supported. Search directories default to the process working directory, with
ROMs defaulting to its `ROM` subdirectory. Directory enumeration is unsorted.
Duplicate disk opens share one handle, following the existing core contract;
one close invalidates the shared handle. Other simultaneous opens of the same
normalized path are rejected. There is no cross-process file locking.

In the standalone `Q88TEST.EXE`, the full PC-8801 machine, its ROM loading sequence, disk emulation, display,
keyboard, timing, menus, sound, snapshots, and save states are not integrated
into this executable. The test's synthetic memory/I/O/interrupt callbacks are
a harness, not implementations of PC-8801 devices. `SUPPORT_8BPP` is a provisional
compile-time setting from the first milestone; the machine target now uses
`SUPPORT_16BPP` for its memory-only renderer, without selecting a hardware mode.
Desktop CMake/SDL2 sources and all existing core files remain unchanged.

Full-machine linking and bounded ROM startup are now covered by milestone 2
above. Graphics and sound hardware requirements remain open.

### BIOS/PIT frame pacing

`wait_vsync_update()` now schedules each frame against the BIOS 18.2 Hz tick
interpolated with a read-only latch of PIT channel 0. This provides sub-millisecond
timer counts without CPU-speed delay loops, changing the PIT rate, or installing
an interrupt handler. The DOS `-sleep` preference cannot yield during the final
high-resolution wait; this backend polls the timer instead.

Build and run the focused 60-frame timer test at two DOSBox-X cycle settings:

```powershell
.\dos\build.ps1 -Target WaitTest -WatcomRoot D:\watcom
.\dos\waittest.ps1 -DosBoxX D:\DOSBox-X\dosbox-x.exe -Cycles 3000
.\dos\waittest.ps1 -DosBoxX D:\DOSBox-X\dosbox-x.exe -Cycles 12000
```

Both runs reported 1,083 ms for 60 periods of 18,050 microseconds. This
validates the timer math in DOSBox-X at those settings, not frame-rate
performance of the full emulator on physical hardware.

### D88 and FDC I/O milestone

Run the full-machine disk fixture with the user's split model ROM set:

```powershell
.\dos\build.ps1 -Target Machine -WatcomRoot D:\watcom
.\dos\machtest.ps1 -DosBoxX D:\DOSBox-X\dosbox-x.exe -Cycles 12000 `
  -RomDirectory D:\88rom -Mk2srDirectory D:\88rom\pc8801mk2sr `
  -Frames 3 -DiskTest
```

`-DiskTest` creates a minimal D88 fixture containing one 256-byte sector and
separate writable, read-only, and malformed copies under a fresh ignored
`build-dos` test directory. The DOS executable exercises the FDC command,
data, and result phases to read the sector, write a pattern, and read it back.
On the read-only copy it verifies that a sector write is rejected and the
original sector remains readable. The writable run also uses the core's D88
append routine to write a second blank image, re-reads its header, and verifies
the resulting file size. A truncated D88 must be rejected. No user disk image
is copied or mounted by this test.

For a physical-machine check, use `build-dos\disk-hardware-test-20260929`.
Put your normal ROM set in its `ROM` subdirectory, copy the folder to a writable
DOS directory, and run `D88TEST.BAT`. It recreates only `RW.D88` and `RO.D88`
from the included disposable `BASE.D88`; the writable run tests FDC sector
read/write/read-back and appends a blank D88 image, while the read-only run
checks write protection and byte-for-byte preservation. Do not substitute a
valuable disk image. The test uses a synthetic one-sector fixture, not a
physical floppy drive or your own disk media.

2026-09-29: The fixture passed in DOSBox-X at both 3,000 and 12,000 fixed
cycles with the split PC-8801mkIISR ROM set. Sector read/write/read-back,
read-only sector protection, D88 append/re-read, malformed-image rejection,
and clean shutdown after three frames passed. Physical-machine disk behavior
remains unverified; run the disposable package and report the output.

### Port validation record

2026-09-29: `.\dos\build.ps1 -Target PortTest -WatcomRoot D:\watcom`
succeeded with Open Watcom v2 (2026-09-28 build) and CauseWay. New code compiles
at warning level 4 with warnings treated as errors. The unchanged Z80 core
reports W202 at `z80.c:954`: the existing local variable `istate` is unused.
This warning is retained and visible; no core workaround or suppression was
added. No compiler or linker errors occurred.

The script compiles each of `dos/porttest.c`, `src/osdepend/dos/file-op.c`, and
`src/pc88/z80.c` separately with `wcl386 -y -c -bt=dos -3r -w4`, adding `-we`
for the new code, the DOS/core include directories, and explicit object paths.
It links with `wcl386 -y -bt=dos -l=causeway -fe=Q88TEST.EXE -fm=Q88TEST.MAP
PORTTEST.OBJ FILEOP.OBJ Z80.OBJ`. All commands run in `build-dos`; the build
script is the authoritative reproducible invocation.

`.\dos\test.ps1 -DosBoxX D:\DOSBox-X\dosbox-x.exe -Cycles 3000` passed under
DOSBox-X 2026.08.31: normal core, emulated 386, 3000 fixed cycles, 16 MB emulated
RAM. Every diagnostic above passed, the program returned zero to DOS, DOSBox-X
exited zero, and the scratch file was removed. These settings are a test
configuration, not established minimum hardware requirements.

For a physical DOS test, copy `Q88TEST.EXE` and the repository `LICENSE` renamed
to `LICENSE.TXT` to an otherwise empty writable directory on a 386-or-newer DOS
PC, run `Q88TEST`, then `IF ERRORLEVEL 1 ECHO FAIL`. Expect the same PASS lines
and prompt return. Report CPU, available conventional/extended RAM, DOS version,
memory managers, and all output. Actual RAM/DOS minima remain unverified.
2026-09-29: The user reports on a physical Celeron 600 PC with a possible
integrated i740: VGA output and the QUASI88 toolbar were visible, BASIC started
and ran, and F1-F5 worked.
F11/F12 did not work. After further key testing the display remained visible
but the machine stopped responding; it was power-cycled. Ctrl+Alt+Del recovery
was not tried. The first diagnostic log contained no F11/F12 scan codes, and
Ctrl+Alt+Q appeared as `scan=10 ascii=00` but did not exit. The prior build used
legacy BIOS keyboard services, which do not expose enhanced keys on some
BIOSes. The backend now uses enhanced BIOS keyboard services and logs modifier
status so these keys can be checked again. This updated path compiled and
passed the DOSBox-X VGA startup test, but is not yet verified on the physical
PC.

2026-09-29 follow-up: After changing to enhanced BIOS keyboard services, the
user reports F11, F12, and Ctrl+Alt+Q all worked on the physical PC. The new
log records F11 as scan `85` and F12 as scan `86`; Ctrl+Alt+Q returned to DOS.
The user reported no hang during this run. This validates interactive VGA,
BASIC entry, F1-F5, F11/F12 menus, and the emergency clean-exit path on the
reported machine. Exact chipset identity and longer-run stability remain
unverified.

2026-09-29 mouse test: The user reports a working pointer and toolbar clicks on
the physical PC. `MOUSE.LOG` reports `driver=installed`, hundreds of pointer
updates, and left-button transitions including toolbar coordinates. A photo
showed a cursor trail over toolbar graphics. The DOS backend now balances INT
33h cursor hide/show calls around planar VGA updates and reapplies its planar
write mode afterward. DOSBox-X plane readback passes with this change; physical
artifact removal is not yet confirmed.

## Hello World compiler smoke test

From the repository root in PowerShell:

```powershell
.\dos\build.ps1 -WatcomRoot D:\watcom
```

The toolchain path is configurable; without the argument the script uses
`WATCOM`, or discovers the installation from `wcl386.exe` on PATH. It restores
the caller's environment after building. All generated files go in the ignored
`build-dos` directory.

The compiler command, executed inside `build-dos`, is:

```text
wcl386 '-y' '-bt=dos' '-l=causeway' '-3r' '-w4' '-we' '-fe=HELLO.EXE' '-fo=HELLO.OBJ' '-fm=HELLO.MAP' D:\QUASI88\dos\hello.c
```

`-bt=dos` selects DOS compilation, `-l=causeway` selects the CauseWay linker
system, and `-3r` generates 386 code. Warnings at level 4 are errors.
The installed Watcom CauseWay manual, section 1.4, documents `-l=CauseWay`
and states that no additional runtime files are needed. The installed
`binnt64/wlink.lnk` CauseWay system uses `format os2 le` and
`op stub=cwstub.exe`. This produces a DOS executable with the extender embedded;
distribute `HELLO.EXE` alone, without a separate `DOS4GW.EXE` or `CWSTUB.EXE`.

## Run with DOSBox-X

Locate your installed DOSBox-X executable first. From the repository root:

```powershell
$DosBoxX = 'D:\DOSBox-X\dosbox-x.exe' # Adjust for your installation
Push-Location .\build-dos
try {
    & $DosBoxX -conf ..\dos\dosbox.conf
} finally {
    Pop-Location
}
```

The configuration mounts only `build-dos` as C:, runs the test, prints PASS
for exit code zero (FAIL otherwise), and leaves the DOS prompt visible.
Expected application output:

```text
Hello, world from 32-bit DOS!
```

For an automated smoke test that records output and exits DOSBox-X:

```powershell
Copy-Item .\dos\smoke.bat .\build-dos\SMOKE.BAT
Push-Location .\build-dos
try {
    & $DosBoxX -conf ..\dos\smoke.conf
    Get-Content HELLO.OUT
    Get-Content RESULT.TXT
} finally {
    Pop-Location
}
```

Expect the greeting in `HELLO.OUT` and `PASS` in `RESULT.TXT`. Check that
both files have fresh timestamps; stale files are not evidence of a new run.
Both configurations disable the working-directory prompt so that only the
intended `build-dos` directory is mounted.

## Real MS-DOS hardware

Copy `build-dos\HELLO.EXE` to a DOS-readable disk, change to its directory,
and run `HELLO`. Expect the line above and a clean return to the DOS prompt.
Run `IF ERRORLEVEL 1 ECHO FAIL` immediately afterward to check for failure.
The code targets a 386 or newer CPU. Exact minimum RAM and DOS version for
this executable remain unverified; this does not establish QUASI88's hardware
requirements. Report CPU, available RAM, DOS version, memory managers, output,
and any extender error if it fails.

## Validation

2026-09-29: `.\dos\build.ps1 -WatcomRoot D:\watcom` succeeded with Open Watcom
v2 (2026-09-28 build): zero compiler warnings, zero errors, and successful
CauseWay linking. `build-dos\HELLO.EXE` was 57,816 bytes. An initial build
attempt exposed PowerShell splitting unquoted filename options; quoting the
arguments in the script fixed it.

2026-09-29: After installation, DOSBox-X 2026.08.31 was discovered at
`D:\DOSBox-X\dosbox-x.exe`. Ran it with
`-conf D:\QUASI88\dos\smoke.conf`, working directory
`D:\QUASI88\build-dos`, normal core, 386 CPU, and 3000 fixed cycles.
`HELLO.OUT` contained the expected greeting and `RESULT.TXT` contained `PASS`,
confirming the program returned to the DOS shell with exit code zero. DOSBox-X
then exited with code zero. The first attempt timed out before producing
output; setting `working directory option=noprompt` allowed the test to finish.

No physical MS-DOS runtime test has been performed. This DOSBox-X result does
not establish real-hardware compatibility.
