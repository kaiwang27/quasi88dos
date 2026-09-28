# DOS port bring-up

The first port milestone builds the existing Z80 core and a DOS implementation
of the file interface. `Q88TEST.EXE` exercises them with synthetic data; it is
not yet a bootable PC-8801 emulator. The original Hello World test remains
available independently.

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

The full PC-8801 machine, its ROM loading sequence, disk emulation, display,
keyboard, timing, menus, sound, snapshots, and save states are not integrated
into this executable. The test's synthetic memory/I/O/interrupt callbacks are
a harness, not implementations of PC-8801 devices. `SUPPORT_8BPP` is a provisional
compile-time setting; it does not select or implement a graphics hardware mode.
Desktop CMake/SDL2 sources and all existing core files remain unchanged.

Next: compile and link the rest of the machine with explicit no-sound support,
bring up real ROM loading and bounded startup, then select and implement DOS
graphics, keyboard input, timing, and sound in separate tested milestones.
Graphics and sound hardware requirements remain open.

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
