# QUASI88 for MS-DOS

An MS-DOS port of [QUASI88](../README.md), the PC-8801 emulator. It is a
32-bit DOS program built with Open Watcom v2 and the CauseWay DOS extender,
which is embedded in the executable. It runs on real MS-DOS hardware and in
DOSBox-X.

This repository is a fork of [bubio/QUASI88](https://github.com/bubio/QUASI88),
the SDL2 port of QUASI88. The DOS port lives in `dos/`, `src/sysdepend/dos/`,
and `src/osdepend/dos/`; the emulation core and the desktop build are
unchanged from upstream commit `c5f959a` (2026-06-19).

- PC-8801 emulation core, unchanged from the desktop port: Z80 main and sub
  CPUs, disk (D88 images), save states, screenshots, menus.
- Display: VGA 640x480 with the PC-8801's own palette, or an optional VESA
  640x480 256-color mode.
- Sound: Sound Blaster family (16-bit stereo on a Sound Blaster 16, 8-bit
  mono on older cards) and 16-bit stereo on the Aztech AZT2320's Windows
  Sound System codec.
- Input: keyboard, mouse (through a DOS mouse driver), one game-port
  joystick.

You supply the PC-8801 ROM images and disk images. None are included.

For the history of the port, test records, and hardware findings, see
[DEVLOG.md](DEVLOG.md).

## Requirements

| | |
|---|---|
| CPU | 386 or better. Developed and tested on a Celeron with an Intel i740, where Ys I runs at more than twice full speed. Slower CPUs have not been tested. |
| Memory | 16 MB was used in testing; a smaller minimum is not established. |
| Video | VGA. Optional: a VESA BIOS with a 640x480 256-color mode. |
| Sound | Optional. Sound Blaster, Sound Blaster Pro, Sound Blaster 16 or compatible; Aztech AZT2320 (WSS mode). |
| DOS | MS-DOS, or Windows 95/98 started with F8 > "Command prompt only". |

## Getting it running

1. Build a release folder (see [Building](#building)), or use a released
   `Q88DOS.ZIP`.
2. Copy the folder to the DOS PC, for example `C:\Q88`.
3. Put your ROM images in `C:\Q88\ROM`. `N88.ROM` (32,768 bytes) is required.
   The full list is in [`dist/ROMS.TXT`](dist/ROMS.TXT).
4. Start it:

   ```
   QUASI88                        start N88-BASIC
   QUASI88 -diskimage GAME.D88    boot a disk image
   ```

| Key | Action |
|---|---|
| F12 | Menu: disks, settings, save states, reset, quit |
| F11 | Show or hide the toolbar and status line |
| Ctrl+Q | Quit to DOS immediately |

File names must be DOS 8.3 names. `README.TXT` in the release folder is the
full user guide, including troubleshooting.

## Options

`QUASI88 -help` lists every option. The common ones:

| Option | Meaning |
|---|---|
| `-v2` `-v1h` `-v1s` `-n` | BASIC mode |
| `-sd` `-sd2` | Sound Board I (OPN) or II (OPNA) |
| `-4mhz` `-8mhz` | CPU clock |
| `-diskimage <file>` | Disk image for drive 1 |
| `-ro` | Open disk images read-only |
| `-romdir <dir>` | ROM directory (default `ROM`) |
| `-joystick` | Game-port joystick as the PC-88 joystick |
| `-english` | English menus |
| `-frameskip <n>` | Draw one frame in n |
| `-saveconfig` | Save settings to `QUASI88.INI` on exit |
| `-noconfig` | Do not read `QUASI88.INI` |

DOS-specific options:

| Option | Meaning | Saved in INI |
|---|---|---|
| `-dosvesa` / `-nodosvesa` | VESA 640x480 256 colors; falls back to VGA | yes |
| `-doswss` / `-nodoswss` | AZT2320 16-bit WSS output | yes |
| `-keyboard <1\|2>` | Extra keys for a Japanese 106-key (1, default) or US/ISO (2) keyboard | yes |
| `-dossb44k` | 44,100 Hz output on 16-bit cards | no |
| `-dossb8` | Force 8-bit output on a Sound Blaster 16 | no |
| `-dosmono` | Mono instead of stereo | no |
| `-dosnosound` | No sound | no |
| `-dosnovga`, `-dosframes <n>` | Headless, bounded runs for the test scripts | no |

### Sound

QUASI88 reads `BLASTER` (`A` port, `I` IRQ, `D` DMA, `H` high DMA). Without
it, it tries port 220h, IRQ 5, DMA 1, high DMA 5.

| Card | Output |
|---|---|
| Sound Blaster 16 (DSP 4.xx) | 16-bit stereo, 22,050 Hz |
| Older Sound Blasters and compatibles | 8-bit mono, 22,222 Hz |
| Aztech AZT2320 with `-doswss` | 16-bit stereo, 22,050 Hz |
| None | Runs silently |

`-doswss` switches an AZT2320 from Sound Blaster mode to its Windows Sound
System codec and back on exit. It uses Aztech-specific commands, so do not
use it with other cards. `SET Q88WSS=A534 I5 D1` overrides the codec port,
IRQ, and DMA.

Windows 98 "Restart in MS-DOS mode" leaves an AZT2320 silent for Sound
Blaster programs. `AZTSB.EXE` restores it; add it to
`C:\WINDOWS\DOSSTART.BAT`.

The output rate is adjusted slightly to follow the sound card's real clock,
so a card that plays a few percent fast or slow does not crackle.

### Video

The default is standard VGA mode 12h (640x480, 16 colors). The 16 palette
entries are reprogrammed to the PC-8801's palette, so the emulated screen
shows exact colors; menu colors use the nearest available entry.

`-dosvesa` uses a VESA 640x480 256-color mode, where every color is exact.
On the test PC it was about 18% faster. It is an option rather than the
default because it depends on the video card's VESA BIOS.

### Keyboard

Keys map by position to the PC-8801's Japanese-layout keyboard, as in the
desktop port. On other layouts some symbol keys produce a different
character than their label.

| PC-8801 key | PC key |
|---|---|
| STOP | ScrollLock or Pause |
| COPY | PrintScreen |
| HOME CLR / HELP | Home / End |
| ROLL UP / ROLL DOWN | Page Down / Page Up |
| GRPH / KANA | Left Alt / Right Alt |
| Yen, `_` (106-key) | Yen key, ro key |
| Yen, `_` (`-keyboard 2`) | Key left of `1`, Right Ctrl |

## Building

You need Windows, PowerShell, and [Open Watcom v2](https://github.com/open-watcom/open-watcom-v2)
(which includes the CauseWay extender). Pass the Watcom directory with
`-WatcomRoot`, or set the `WATCOM` environment variable, or have
`wcl386.exe` on `PATH`.

```powershell
# Build QUASI88.EXE into build-dos\
.\dos\build.ps1 -WatcomRoot D:\watcom

# Build everything and assemble build-dos\dist\Q88DOS\ and Q88DOS.ZIP
.\dos\package.ps1 -WatcomRoot D:\watcom
```

`build.ps1 -Target` selects `Machine` (the emulator, default), `AztSb`,
`WssTest`, or the small bring-up tests `PortTest`, `WaitTest`, and `Hello`.
The source list is [`sources.txt`](sources.txt). Build output goes to
`build-dos\`, which is ignored by git.

The release folder contains `QUASI88.EXE`, `AZTSB.EXE`, `WSSTEST.EXE`,
`README.TXT`, `LICENSE.TXT`, `MAME.TXT`, and an empty `ROM` directory. All
names are DOS 8.3 names.

The desktop (SDL2/CMake) build is separate and unchanged; see the
[main README](../README.md).

## Testing

The regression tests run the DOS executable in
[DOSBox-X](https://dosbox-x.com/). Give the path to `dosbox-x.exe`:

```powershell
.\dos\machtest.ps1 -DosBoxX D:\DOSBox-X\dosbox-x.exe            # startup, synthetic ROMs
.\dos\machtest.ps1 -DosBoxX D:\DOSBox-X\dosbox-x.exe -VgaTest   # VGA readback
```

Other switches: `-StateTest`, `-SnapshotTest`, `-ConfigTest`,
`-JoystickMode`, and, with your own ROMs through `-RomDirectory`,
`-DiskTest`, `-SoundTest`, `-Sound44kTest`, `-Sound8Test`, and
`-WssFallbackTest`. Each run uses a fresh directory under `build-dos\` and
mounts only that directory.

Use `core=normal` in DOSBox-X. Its dynamic core miscomputes floating point
in CauseWay programs after a few launches in one session (details in
[DEVLOG.md](DEVLOG.md)). DOSBox-X does not emulate the AZT2320, so the WSS
path is tested only on real hardware.

## Limits

- 8.3 ASCII file names only; no long or Japanese file names.
- One combined ROM file is not supported; use the separate ROM files.
- Sound is tested on an Aztech AZT2320 and in DOSBox-X (Sound Blaster Pro 2
  and 16). A physical Sound Blaster 16 has not been tested.
- The fmgen sound core of the desktop port is not built; the MAME core is
  used.
- No master volume control; the per-chip levels in the menu work.
- One joystick. Keypad `=` and `,` have no PC key.
- Frame pacing busy-waits on the timer, so the CPU is always busy.

## Source layout

| Path | Contents |
|---|---|
| `src/sysdepend/dos/` | DOS platform layer: `main.c`, `graph.c` (VGA/VESA), `audio.c` (Sound Blaster/WSS), `event.c` and `keyboard_irq.asm` (keyboard, mouse, joystick), `wait.c` (timer) |
| `src/osdepend/dos/` | DOS file access |
| `dos/` | Build, packaging and test scripts, `aztsb.c`, `wsstest.c`, release text files |

## License

QUASI88 is under the BSD 3-Clause license ([LICENSE](../LICENSE)). The MS-DOS
port is copyright (c) 2026 Kai Wang, under the same license. The sound
emulation comes from MAME/XMAME under the
[MAME license](../src/snddrv/xmame/license.txt): it may not be sold or used
commercially, and modified versions must be distributed with complete source
code. A binary release of this port therefore has to stay non-commercial and
point to this source.

Original QUASI88 by Shozo Fukunaga; SDL2 port by Bubio; MS-DOS port by Kai
Wang. Source: <https://github.com/kaiwang27/quasi88dos>.
