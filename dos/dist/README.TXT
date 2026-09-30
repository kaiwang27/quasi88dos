QUASI88 for MS-DOS
==================

QUASI88 is a PC-8801 emulator. This is its MS-DOS port: a 32-bit DOS
program (the CauseWay DOS extender is built in; no extra files needed).

You must supply your own PC-8801 ROM images and disk images. None are
included.


REQUIREMENTS
------------
  CPU     386 or better (32-bit DOS extender). It was developed and tested
          on a Celeron with an Intel i740, where it runs at more than twice
          full speed. Slower CPUs have not been tested.
  Memory  16 MB was used in testing. A smaller minimum is not established.
  Video   VGA. Optional: VESA BIOS with a 640x480 256-color mode.
  Sound   Optional. Sound Blaster, Sound Blaster Pro, Sound Blaster 16,
          or compatible; Aztech AZT2320 in 16-bit WSS mode.
  Input   Keyboard. Optional: mouse (with a DOS mouse driver loaded) and
          one game-port joystick.
  DOS     MS-DOS or the Windows 95/98 command prompt (boot with F8 and
          choose "Command prompt only"). Do not run it in a Windows DOS box.


FILES
-----
  QUASI88.EXE   The emulator.
  AZTSB.EXE     Returns an Aztech AZT2320 card to Sound Blaster mode.
  WSSTEST.EXE   Sound card test tool for WSS codecs (diagnostics).
  README.TXT    This file.
  LICENSE.TXT   QUASI88 license (BSD 3-Clause).
  MAME.TXT      License of the MAME/XMAME sound code (non-commercial).
  ROM\          Put your ROM images here.


ROM FILES
---------
Put these in the ROM directory. File names must be DOS 8.3 names.

  N88.ROM      required   N88-BASIC main ROM (32,768 bytes)
  N88_0.ROM    N88_1.ROM  N88_2.ROM  N88_3.ROM   extension ROMs
  N80.ROM                 N-BASIC ROM
  DISK.ROM                disk subsystem ROM
  KANJI1.ROM   KANJI2.ROM kanji ROMs
  FONT.ROM                font (a built-in font is used if missing)

Each file may also use the other QUASI88 name: N88EXT0-3.ROM, N88N.ROM,
N88SUB.ROM, N88KNJ1.ROM, N88KNJ2.ROM. JISYO.ROM or N88JISHO.ROM is the
optional dictionary ROM. One combined ROM file is not supported.


QUICK START
-----------
  1. Copy the ROM files into the ROM directory.
  2. Start BASIC:
         QUASI88
  3. Start a game from a disk image (D88 format, 8.3 file name):
         QUASI88 -diskimage GAME.D88

  F12        open the menu (disks, settings, save states, reset, quit)
  F11        show or hide the toolbar and status line
  Ctrl+Q     quit to DOS immediately

Useful options (QUASI88 -help lists all of them):

  -v2 / -v1h / -v1s / -n   BASIC mode (V2, V1H, V1S, N)
  -sd / -sd2               Sound Board I (OPN) / Sound Board II (OPNA)
  -4mhz / -8mhz            CPU clock
  -diskimage <file>        disk image for drive 1
  -ro                      open disk images read-only
  -romdir <dir>            ROM directory (default: ROM)
  -joystick                use the game-port joystick as the PC-88 joystick
  -english                 English menus
  -frameskip <n>           draw one frame in n (for slow PCs)
  -saveconfig              save the settings to QUASI88.INI when quitting
  -noconfig                do not read QUASI88.INI

Settings saved in QUASI88.INI are used on every later start.


SOUND
-----
QUASI88 reads the BLASTER environment variable. Without it, it tries
port 220, IRQ 5, DMA 1, high DMA 5. Example:

         SET BLASTER=A220 I5 D1 H5 T6

  Sound Blaster 16 (DSP 4.xx)   16-bit stereo, 22,050 Hz
  Older cards and compatibles   8-bit mono, 22,222 Hz
  No card found                 runs without sound

  -dossb44k     44,100 Hz (16-bit cards) instead of 22,050 Hz
  -dossb8       force 8-bit output on a Sound Blaster 16
  -dosmono      mono instead of stereo
  -dosnosound   no sound

Aztech AZT2320 cards: -doswss plays 16-bit stereo through the card's
Windows Sound System codec, which sounds much better than its 8-bit Sound
Blaster mode. The card returns to Sound Blaster mode when QUASI88 exits.
-doswss is an Aztech-specific option; do not use it with other cards.
If needed, set the codec port, IRQ and DMA:

         SET Q88WSS=A534 I5 D1

Windows 98 "Restart in MS-DOS mode" leaves an AZT2320 in a state where
Sound Blaster programs are silent. Run AZTSB to fix it, for example from
C:\WINDOWS\DOSSTART.BAT:

         C:\Q88\AZTSB


VIDEO
-----
The default is standard VGA 640x480 with 16 colors. The 16 colors are set
to the PC-8801's own palette, so games show their real colors.

  -dosvesa      VESA 640x480 with 256 colors: every color exact, including
                the menus, and faster on some cards. If the video card has
                no suitable VESA mode, standard VGA is used.


KEYBOARD
--------
Keys are mapped by position to the PC-8801 keyboard, which has a Japanese
layout. On other keyboards some symbol keys give a different character
than the one printed on them.

  STOP        ScrollLock or Pause       COPY       PrintScreen
  HOME CLR    Home                      HELP       End
  ROLL UP     Page Down                 ROLL DOWN  Page Up
  GRPH        Left Alt                  KANA       Right Alt

  -keyboard 1   Japanese 106-key keyboard (default): the yen and ro keys,
                Henkan, Muhenkan and the Application key are used.
  -keyboard 2   US and ISO keyboards: the key left of 1 is yen (and |
                with Shift), and Right Ctrl is the underscore key.


TROUBLESHOOTING
---------------
  "missing required main ROM"
        N88.ROM is not in the ROM directory, or it is not 32,768 bytes.
  No sound
        Check the BLASTER setting against the card's real port, IRQ and
        DMA. QUASI88 prints what it detected when it exits. A Plug and
        Play card must be set up for DOS first.
  Sound crackles or is too slow
        The PC is too slow for full speed. Try -frameskip 2 or -dosvesa.
  Garbled screen or wrong colors with -dosvesa
        Run without -dosvesa, or use -nodosvesa -saveconfig once.
  Long or Japanese file names
        Not supported. Rename disk images to 8.3 ASCII names.
  The PC stops after exiting abnormally
        Reboot. Always quit with the menu or Ctrl+Q so the video mode,
        keyboard and sound card are restored.

On exit QUASI88 prints diagnostic lines starting with "DOS:". Redirect
them to a file to keep them:

         QUASI88 -diskimage GAME.D88 > Q88.LOG


LICENSE
-------
QUASI88 is distributed under the BSD 3-Clause license (LICENSE.TXT). The
sound emulation comes from MAME/XMAME, whose license (MAME.TXT) does not
allow selling it or using it commercially, and requires the complete
source code to accompany modified versions. The source code of this port
is at:

         https://github.com/kaiwang27/quasi88dos

Original QUASI88 by Shozo Fukunaga. SDL2 port by Bubio. MS-DOS port
copyright (c) 2026 Kai Wang, under the same BSD 3-Clause license.
