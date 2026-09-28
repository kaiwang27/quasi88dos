# DOS port bring-up

The DOS build now links the full machine into `QUASI88.EXE` for bounded,
headless startup. `Q88TEST.EXE` separately exercises the Z80 core and file
backend. The original Hello World test remains available independently.

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

To run interactively, mount the test directory printed by the script:

```dos
mount c "D:\QUASI88\build-dos\machine-<printed identifier>"
c:
QUASI88 -noconfig -nosaveconfig -v2 -romdir ROM -verbose 1 -dosframes 3
```

Replace the directory placeholder with the actual path. Expect ROM loading
diagnostics, `Running QUASI88...`, then
`DOS: completed 3/3 frames; clean shutdown`. There is **no graphical output**:
the 16-bit renderer writes to allocated memory, leaving DOS text mode intact.
`-dosframes` accepts 1 through 600 and defaults to 3. Escape requests an early
exit, which returns nonzero if the requested frame count was not completed.
`-doscheck` is exclusively for the generated test fixtures, not real ROMs.

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

No hardware display, PC-88 keyboard mapping, mouse, joystick, audio output, or
real-time frame pacing exists yet. The wait backend deliberately runs without
throttling; Watcom `clock()` is used only for elapsed-time bookkeeping. No
interrupt vectors, PIT, DMA, sound registers, or video modes are modified.
Disk I/O, save states, configuration saving, and snapshots have not been
validated as machine features. Keep writable user media outside test mounts.

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

For hardware testing, copy `QUASI88.EXE`, `LICENSE.TXT` (the repository license),
and your own ROMs in a `ROM` subdirectory to a writable DOS directory. Run the
command above without the DOSBox-X mount commands, then immediately run
`IF ERRORLEVEL 1 ECHO FAIL`. A 386-or-newer CPU is required; tested emulated RAM
is 16 MB, not an established minimum. Report CPU, RAM, DOS version, memory
managers, ROM filenames/sizes, and the complete console output. No hardware
result has been reported. Next milestone: a visible DOS display backend and
keyboard input, selected with hardware requirements in mind.

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
No physical-hardware result has been reported.

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
