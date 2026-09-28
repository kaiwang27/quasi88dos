# QUASI88 MS-DOS port

## Project objective

Port QUASI88 to MS-DOS while preserving the existing emulation core and desktop
ports. The DOS port must run on real MS-DOS hardware; DOSBox-X is the development
and regression test environment. Passing in DOSBox-X alone does not establish
real-hardware compatibility.

## Development environment

- Workspace: `D:\QUASI88` on Windows; use PowerShell for host commands.
- Compiler: Open Watcom v2, installed at `D:\watcom`.
- `wcl386` is available on PATH. Target 32-bit DOS, not Win32.
- DOS extender: CauseWay. Use it for the Open Watcom DOS target.
- Use the installed Watcom tools and documentation to verify compiler, linker,
  and DOS runtime options. Do not replace the toolchain with GCC or DJGPP.
- Keep machine-specific tool paths configurable in build scripts.
- DOSBox-X is the intended development test runner. Discover its installation
  before scripting launches; do not assume an executable path.
- The user has a physical MS-DOS PC for hardware testing. Its CPU, memory,
  graphics, sound hardware, and DOS version are not yet specified.

## Porting approach

- Inspect the existing platform interfaces under `src/osdepend` and
  `src/sysdepend` before implementing DOS support. Keep DOS-specific code behind
  those interfaces where practical.
- Reuse the CPU, PC-8801, disk, video, and sound emulation code. Avoid unrelated
  core rewrites or changes to emulation behavior as compiler workarounds.
- The current desktop build uses SDL2 and CMake. Preserve that build and provide
  an explicit DOS build entry point with reproducible commands.
- Use CauseWay as the DOS extender. Verify its linker configuration against the
  installed Watcom documentation, and document any required runtime files and
  how they are packaged.
- Do not assume SDL2, POSIX services, Windows APIs, long filenames, or a modern
  C/C++ standard library are available on the DOS target.
- Use DOS-compatible names and paths for distributed files, including 8.3 names
  where required. Keep generated objects and binaries out of source directories.
- Preserve existing file encodings, especially legacy Japanese comments; avoid
  incidental reformatting or encoding conversions.
- Build incrementally: establish a runnable DOS executable, then bring up file
  access, graphics, keyboard input, timing, and sound in testable steps. Record
  unsupported features and temporary stubs explicitly.

## Hardware-facing code

- Document CPU, RAM, graphics, and sound requirements as they become established;
  do not infer the user's hardware specifications from DOSBox-X defaults.
- Keep interrupt handlers and hardware access small and auditable. Observe the
  CauseWay's rules for protected-mode memory, interrupts, and DMA.
- Restore modified interrupt vectors, timers, graphics modes, and sound hardware
  on normal exit and on initialization failures where possible.
- Handle missing optional hardware gracefully and retain a no-sound mode.
- Avoid CPU-speed-dependent delay loops. Validate timing at more than one
  DOSBox-X CPU/cycle setting when changing timing-sensitive code.

## Build and validation

- For code changes, compile the affected DOS target with Open Watcom and review
  relevant warnings. Record the exact command and result; do not claim a build
  or runtime test that was not performed.
- Keep a reproducible DOSBox-X configuration and launch procedure once the first
  runnable target exists. Mount only the required development/test directory.
- Smoke-test startup, ROM loading, display, keyboard input, and clean return to
  DOS as those features become available. Test disk access, sound, and state
  handling when relevant to the change.
- Use copies of writable disk images for tests. Keep user ROMs, disk images,
  saves, local settings, and generated build artifacts out of commits.
- Distinguish compile checks, DOSBox-X results, and user-reported hardware
  results. For hardware testing, provide the executable/runtime files, exact
  steps, expected behavior, and useful diagnostics for the user to report.
- When a test cannot run, state the missing prerequisite and what remains
  unverified. Documentation-only changes do not require an emulator run.

## Working conventions

- Keep changes focused on the requested milestone and preserve unrelated work.
- Update build and run documentation alongside changes to the DOS workflow.
- Preserve upstream license notices and attribution.
- Treat the video backend, sound backend, and minimum hardware target
  as open decisions until supported by repository investigation and testing or
  explicit user direction.
