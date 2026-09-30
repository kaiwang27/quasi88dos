# QUASI88 for MS-DOS

<p align="center">
  <img src="AppIcon.png" alt="QUASI88" width="128" height="128">
</p>

An MS-DOS port of QUASI88, the PC-8801 emulator originally created by Shozo Fukunaga. It runs on real retro PCs and in DOSBox-X.

<p align="center">
  <a href="https://github.com/kaiwang27/quasi88dos/releases/latest">
    <img src="https://img.shields.io/github/v/release/kaiwang27/quasi88dos" alt="Latest Release">
  </a>
  <a href="https://github.com/kaiwang27/quasi88dos/blob/main/LICENSE">
    <img src="https://img.shields.io/github/license/kaiwang27/quasi88dos" alt="License">
  </a>
</p>

> This repository is a fork of [bubio/QUASI88](https://github.com/bubio/QUASI88), the SDL2 port for macOS, Windows, and Linux. It adds the MS-DOS port. For the desktop versions, use the upstream project.

## Features

*   **PC-8801 emulation**: the QUASI88 core, unchanged. Z80 main and sub CPUs, PC-8801mkIISR-class memory, text and graphics VRAM, kanji ROMs, D88 disk images, save states, screenshots, and the built-in menu.
*   **Display**: VGA 640x480 using the PC-8801's own palette, or an optional VESA 640x480 256-color mode.
*   **Sound**: YM2203 (OPN), YM2608 (OPNA), and BEEP through the MAME/XMAME sound core.
    *   Sound Blaster 16: 16-bit stereo.
    *   Older Sound Blasters and compatibles: 8-bit mono.
    *   Aztech AZT2320: 16-bit stereo through its Windows Sound System codec.
*   **Input**: keyboard, mouse (with a DOS mouse driver), one game-port joystick.
*   **Self-contained**: a 32-bit DOS program with the CauseWay DOS extender built in.

## Requirements

*   A 386 or better CPU. It was developed and tested on a Celeron with an Intel i740; slower CPUs have not been tested.
*   VGA. A VESA BIOS is optional.
*   MS-DOS, or Windows 95/98 started with F8 > "Command prompt only".
*   Your own PC-8801 ROM images and disk images. None are included.

## Getting started

1.  Download `Q88DOS.ZIP` from the [Releases](https://github.com/kaiwang27/quasi88dos/releases) page, or [build it](#building).
2.  Extract it to a directory on the DOS PC, for example `C:\Q88`.
3.  Put your ROM images in the `ROM` directory. `N88.ROM` is required.
4.  Run it:

    ```
    QUASI88                        start N88-BASIC
    QUASI88 -diskimage GAME.D88    boot a disk image
    ```

| Key | Action |
|---|---|
| F12 | Menu: disks, settings, save states, reset, quit |
| F11 | Show or hide the toolbar and status line |
| Ctrl+Q | Quit to DOS |
| ScrollLock or Pause | STOP |

File names must be DOS 8.3 names. `README.TXT` in the release is the full user guide.

## Documentation

*   **[dos/README.md](dos/README.md)**: options, sound and video setup, keyboard mapping, building, testing, and limits.
*   **[dos/DEVLOG.md](dos/DEVLOG.md)**: development log with test records and hardware findings.
*   **[doc/manual.txt](doc/manual.txt)** and **[doc/faq.txt](doc/faq.txt)**: the original QUASI88 manual and FAQ (Japanese).

## Building

You need Windows, PowerShell, and [Open Watcom v2](https://github.com/open-watcom/open-watcom-v2).

```powershell
git clone https://github.com/kaiwang27/quasi88dos.git
cd quasi88dos

# Build QUASI88.EXE into build-dos\
.\dos\build.ps1 -WatcomRoot D:\watcom

# Build everything and assemble build-dos\dist\Q88DOS\ and Q88DOS.ZIP
.\dos\package.ps1 -WatcomRoot D:\watcom
```

See [dos/README.md](dos/README.md#building) for the build targets and the DOSBox-X regression tests.

## Desktop versions

The source tree still contains the SDL2/CMake desktop build, unchanged from upstream. This fork does not publish desktop packages. For macOS, Windows, Linux, and Raspberry Pi builds and their instructions, see [bubio/QUASI88](https://github.com/bubio/QUASI88).

## License

QUASI88 is distributed under the BSD 3-Clause license; see [LICENSE](LICENSE). The MS-DOS port is copyright (c) 2026 Kai Wang, under the same license.

The sound emulation comes from MAME/XMAME under its own license ([src/snddrv/xmame/license.txt](src/snddrv/xmame/license.txt)), which does not permit selling it or using it commercially and requires complete source code with modified versions. The source tree also includes fmgen, which the DOS build does not use.

## Acknowledgements

*   Original QUASI88: **Shozo Fukunaga**.
*   SDL2 port: **Bubio** ([bubio/QUASI88](https://github.com/bubio/QUASI88)).
*   MS-DOS port: **Kai Wang**.
*   Thanks to the contributors of MAME, fmgen, Open Watcom, CauseWay, and DOSBox-X.
