# DOS port bring-up

The DOS build links the full machine into `QUASI88.EXE`. It has an optional VGA
640x480 16-color display, IRQ1 scan-code keyboard, and DOS mouse-driver path, as
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
directory. Generated batch command lines are capped at 120 characters to stay
below DOS command-tail limits. Use `-Cycles 12000` to repeat at the second
tested CPU speed.

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
QUASI88 -saveconfig -v2 -romdir ROM -verbose 1 -dosvga -dosframes 0
```

`-dosvga` selects BIOS mode 12h and converts the core's 8-bit rendered frame to
the standard 16-color VGA palette, then writes the four planes through VGA
memory. `-dosframes 0` runs until QUASI88 exits; the default remains three
frames for repeatable headless tests. The protected-mode IRQ1 handler captures
raw AT make/break scan codes into a locked ring buffer; scan translation and
QUASI88 event delivery happen in the main loop. Held keys remain down until
their break code arrives, without BIOS typematic-buffer polling. F1-F12,
modifiers, navigation, and Ctrl+Q shutdown are translated from scan codes.
While QUASI88 is active it owns IRQ1, so DOS BIOS keyboard input is unavailable
to other programs; the previous vector is restored on normal exit. One standard
PC game-port joystick is detected and its X/Y axis timers are measured against
the read-only PIT channel 0 counter. The centered stick position is sampled at
startup; keep the stick centered while launching. In `-joystick` mode, analog
movement maps to PC-88 directions with a calibrated dead zone, and its two
active-low buttons map to pad A/B. Reads are bounded to 5 ms and do not
reprogram the system timer. PCs without a responding joystick continue
normally.
Normal exit restores the prior BIOS video mode. Avoid Ctrl-C or forced process
termination if you want the display mode restored.

For physical keyboard and mouse diagnosis, run `RUN_Q88.BAT` from the folder
containing `QUASI88.EXE` and `ROM`. The batch contains only the emulator command
and `PAUSE`. It records translated IRQ1 make/break events in `KEYS.LOG` and DOS mouse-driver
availability, pointer coordinates, and button changes in `MOUSE.LOG`. The mouse path polls the
installed INT 33h driver while VGA is active; without a driver, keyboard
operation continues normally. Left, right, and middle buttons map to QUASI88's
existing mouse events, including toolbar clicks. Ctrl+Alt+Q requests an
emergency normal shutdown to DOS. Ctrl+Q is also accepted as a fallback for
BIOSes that do not report the Alt modifier consistently.

`-dosvideochk` checks rendered VGA planes before exit and requires `-dosvga`.
The `-VgaTest` test-script switch runs that check with synthetic or copied real
ROMs. `-doscheck` remains exclusively for generated fixtures. The backend uses
standard VGA BIOS mode 12h and no i740-specific registers. DOSBox-X success does
not establish compatibility with the user's physical Celeron 600/i740 machine,
which remains untested.

The DOS configuration file is `QUASI88.INI` in the current directory. The
interactive launcher loads it when present and writes current settings on a
normal exit. Keep a backup if you are testing settings you may want to undo.
To check the existing config load/save path in DOSBox-X, run
`.\dos\machtest.ps1 -DosBoxX D:\DOSBox-X\dosbox-x.exe -ConfigTest`. This creates
a fresh test directory with `-speed 77` in `QUASI88.INI`, verifies the loaded
value in the executable, enables `-saveconfig`, and verifies the setting was
written back. The synthetic fixture is used so no user ROMs or settings are
changed.

### Game-port joystick input

One standard PC game-port joystick is detected by measuring the X/Y axis RC
timers at port `201h` against the read-only PIT channel 0 counter. It samples
and averages the centered stick position at startup; keep the stick centered
while launching. In `-joystick` mode, axis movement maps to PC-88 directions
with a calibrated dead zone, and its two active-low buttons map to pad A/B.
Reads are bounded to 5 ms per sample and do not reprogram the system timer.
For the physical game port, a shorter Y timer maps to up. DOSBox-X passes the
host joystick's Y value directly into its emulated timer, and that host input
was observed to have the opposite polarity; do not use it to override the
physical-port mapping. The DOSBox-X machine test sets `joysticktype=none` and
checks that missing optional hardware does not block startup. A second
controller and additional buttons are not supported.

Check joystick-mode startup without a host joystick using the synthetic ROMs:

```powershell
.\dos\machtest.ps1 -DosBoxX D:\DOSBox-X\dosbox-x.exe -JoystickMode
```

This confirms the `-joystick` mode is selected and the emulator still returns
cleanly when no game-port device is detected; it does not simulate stick axes.

2026-09-29: Open Watcom/CauseWay machine build succeeded. DOSBox-X tests with
the virtual joystick disabled passed at 3,000 and 12,000 fixed cycles; each
reported no joystick and returned cleanly to DOS. The 12,000-cycle VGA test
also passed plane readback and video-mode restoration. Physical game-port
input was tested, and an attempted Y-axis reversal made up/down wrong on the
physical PC. The mapping has been returned to shorter-timer-is-up; center the
stick before launch, run with `-joystick`, and recheck all directions and A/B.

### Scope and ROM checks

The standard machine core, ROM loader, main/sub CPUs, video renderer, disk
controller code, and menu code are linked. The XMAME sound synthesis core is
enabled and its output is sent to a Sound Blaster-compatible DSP using 8-bit
mono auto-init DMA at 22,222 Hz. The DSP's integer time constant 211 yields
22,222 Hz; the mixer uses the same rate to prevent long-term DMA drift. The
backend reads `BLASTER` (`A`, `I`, and `D`)
when present; otherwise it tries the common defaults `220h`, IRQ 5, DMA 1.
The DMA buffer is allocated in conventional memory and the selected IRQ handler
acknowledges DSP interrupts. The backend saves and unmasks the configured PIC
IRQ line (including IRQ2 for slave-PIC lines), then restores its prior mask and
interrupt vector on shutdown. If no DSP responds, the emulator continues silently;
use `-dosnosound` to disable sound explicitly. Volume control and FMGEN are not
available in this DOS milestone. The backend has compiled and passed the
DOSBox-X DMA/IRQ transfer check. A 60-frame DOSBox-X run at 12,000 cycles moved
DMA data and serviced 14 Sound Blaster IRQs. The DOS backend sets
the core sample rate before the YM devices initialize, and the smoke test checks
that audio samples reach the DMA stream. A DOSBox-X run without a responding
emulated SB device reports `Sound Blaster not detected`. Startup, frame execution,
and shutdown use the normal core lifecycle.
DOS-specific code remains under `sysdepend/dos` and `osdepend/dos`; desktop
build files and existing core sources are unchanged.

### DOSBox-X audio comparison

All audio tests documented here use DOSBox-X's emulated Sound Blaster, not an
AZT2320 or another physical sound card. The interactive logs report base
`220h`, IRQ 7, DMA 1. Run `BASIC.BAT`, enter `NEW CMD`, then type
`CMD PLAY "T32L1C"` at the BASIC prompt. This plays the long sustained tone
used for comparison. Exit with Ctrl+Q to write `Q88.LOG`. `BASIC.BAT` selects
OPN (Sound Board I), matching the Windows test. The Ys I game launchers remain
on OPNA (Sound Board II).

The user reports that Windows QUASI88 works fine and sounds normal, with a
clean fade and a shorter tone. Windows uses MAME OPN, 44,100 Hz, and an SDL
buffer of 2,048 samples. In DOSBox-X, the DOS build sounds longer or dragging
and noisy, like a weak radio signal, with a rough fade. The user heard the
same problem on OPN and OPNA. Correcting the original board mismatch (the
first DOS BASIC test used OPNA via `-sd2`) did not improve it.

The DOS backend defaults to 22,222 Hz 8-bit mono using DSP time constant 211.
The experimental `-dossb44k` option selects 43,478 Hz 8-bit mono using time
constant 233; the integer time constant cannot produce exactly 44,100 Hz.
Both modes downmix the MAME core's stereo 16-bit output to mono and reduce it
to unsigned 8-bit PCM. `BASIC44.BAT` repeats the same `T32L1C` test at the
higher rate; it does not change the output bit depth.

The latest unfiltered OPN logs show zero underruns in both modes. The
22,222 Hz run reports 399,915 samples, 88,273 non-silent samples, peak 1,020,
minimum DMA lead 13,387, and zero low-lead/underrun frames over 997 emulated
frames. The 43,478 Hz run reports 671,005 samples, 172,592 non-silent samples,
the same peak 1,020, minimum lead 8,481, and zero low-lead/underrun frames over
855 frames. The non-silent counts correspond to about 3.97 seconds of active
tone at both rates. A peak of 1,020 maps to roughly four
8-bit steps on either side of silence, only about eight output values total.
The higher-rate run therefore produced about twice as many samples without
increasing the tone's quantized amplitude, which matches the user's report
that it sounds the same. Coarse 8-bit quantization of this quiet tone is a
likely source of the noisy fade, but it does not yet explain the longer tone.

An earlier filtered-tone run was reported as less hissy but shattered. Its
`FILTONE.LOG` reports peak 504 and 1,305 underrun frames over 30,436 frames,
so it is not a clean filter-quality comparison. A separate DOSBox-X stress
test at 12,000 cycles also reports underruns because the emulated machine
runs more slowly than the independent DSP clock. Neither result describes
the latest unfiltered pair, which has zero underruns.

Current evidence rules out the OPN/OPNA mismatch and underruns in the latest
tone runs as explanations for the sound difference. The DOS path's 8-bit mono
conversion is a leading noise-quality suspect. The longer/dragging tone remains
unexplained; the two DOS logs have different run lengths and there is no
corresponding Windows PCM log for duration comparison. Continue this
investigation in DOSBox-X, examining PCM scaling/quantization and tone timing
separately. Do not infer physical sound-card behavior from these tests.

2026-09-30: the 8-bit conversion truncated with `mono >> 8`, which floors every
negative sample in -255..-1 to -1 LSB while positives up to 255 stay at center.
The 22,222 Hz log shows this: 88,273 non-silent input samples but only 53,723
non-center DMA bytes. A quiet or decaying tone therefore became a lopsided
half-wave pulse train that lasted until the 16-bit input reached exactly zero,
which matches the reported longer tone, rough fade, and radio-like noise. The
backend now rounds to the nearest 8-bit step and clamps at +127. The Machine
target rebuilt with no new warnings, and the 60-frame `-SoundTest` DOSBox-X
regression at 12,000 cycles passed. That ROM run was silent, so the audible
`T32L1C` comparison still has to be repeated by ear. The tone peaks near
four 8-bit steps, and the Ys I run peaked at 24,244 with the current gain, so
raising the gain would clip game music. Some quantization noise on very quiet
tones is inherent to 8-bit output.

The user confirmed that rounding fixed the lengthened, distorted tone, but it
still sounded hissy. The fade was also stepped rather than gradual, as expected
from about four 8-bit levels.

### SB16 16-bit output

When the DSP reports version 4.xx (SB16) and the `BLASTER` `H` value is 5-7
(default 5), the backend plays signed 16-bit mono PCM. It uses auto-init DMA on
that high channel and command `B6h` mode `10h`. The rate is set directly with
DSP command `41h`: 22,050 Hz by default and 44,100 Hz with `-dossb44k`. The
16-bit IRQ is acknowledged at base+`0Fh`. The 32 KiB ring holds 16,384 samples
and starts with an 8,192-sample lead. The 16-bit controller takes a word
address; the 64 KiB-aligned buffer cannot cross a 128 KiB DMA page. Older DSPs,
a missing or invalid `H` value, or `-dossb8` select the existing 8-bit path at
22,222 or 43,478 Hz. Shutdown pauses the active DSP mode with `D5h` or `D0h`,
then masks its DMA channel.
The core's 16-bit samples are written unchanged apart from the existing mono
downmix and gain. Stereo output is not implemented yet.

2026-09-30: the Machine target built with no new warnings. The DOSBox-X test
configuration now sets `hdma=5`. At 12,000 cycles with the split real-ROM set,
`-SoundTest` passed with `DSP 4.05 ... DMA 5, 22050 Hz 16-bit mono`, and
`-Sound44kTest` passed at 44,100 Hz. `-Sound8Test` forced
`DMA 1, 22222 Hz 8-bit mono` and also passed. Each run advanced DMA and
serviced IRQs. These ROM runs are silent. A 3-frame VGA run at 3,000 cycles
also selected the 16-bit path, serviced IRQs, and exited cleanly. A 60-frame
sound run at 3,000 cycles exceeded the script's 45-second timeout; this is a
test-duration limit, not an observed audio failure. The user then compared
`T32L1C` in DOSBox-X and reported that 16-bit output sounds like the Windows
version, and `-dossb8` sounds worse. No physical SB16 or SB Pro-class card
has been tested.

If the core's sound setup fails after the Sound Blaster has started, the core
does not call `osd_stop_audio_stream()`. The backend therefore registers an
`atexit` cleanup that stops the DSP and DMA, then restores the PIC mask and IRQ
vector. Without it, the card could keep interrupting into the exited program.
In a DOSBox-X run whose sound setup failed, the shutdown statistics line now
appears after `...FAILED, abort`, which shows the cleanup ran.

### Physical AZT2320 results and DMA rate control

2026-09-30, user-reported physical PC results for `YS1HW.BAT` (Ys I, OPNA).
`BLASTER` was unset, so the defaults `220h`, IRQ 5, DMA 1 were used:

- Booted to the DOS prompt with F8: the card reported DSP 3.01, so the 8-bit
  path was used. Sound played. The user heard it as much better than before,
  but with crackling and background hiss. The log showed 1,126 underrun
  frames out of 2,579 and a minimum DMA lead of 0.
- Windows 98 "Restart in MS-DOS mode": the DSP answered with version 3.01, but
  DMA never moved (count `7FFF`, 0 IRQs), so there was no sound. The card was
  probably not configured for DOS in that mode.
- DOSBox-X on the development PC (dynamic core): 16-bit at 22,050 Hz with 0
  underruns. The user heard slight crackling and less hiss.

The producer was open-loop: it wrote exactly the nominal samples per emulated
frame, while the card played at its own clock. Frame pacing drops time after
10 late frames. Any difference between the emulator's real-time speed and the
card's clock therefore drained the initial lead. Playback then read stale
ring data, heard as crackling. The backend now:

- Tracks unwrapped write and playback cursors. Each frame it adjusts the
  samples requested from the core by at most 5%, holding the DMA lead near a
  quarter of the ring (8,192 samples for 8-bit, 4,096 for 16-bit). The error
  is low-pass filtered to avoid audible pitch flutter.
- Resyncs when playback has passed the writer, or when the writer would
  overwrite unplayed audio. It restarts the writer a target lead ahead of
  playback and fills the gap with silence, so a dropout is a short gap
  instead of stale data.
- Waits up to 200 ms after starting DMA for the 8237 count to move. If it does
  not, it prints `DMA n did not start; check BLASTER I/D/H and the card's DOS
  setup` and continues silently.
- Prints an `audio timing` line at exit. It shows the card's measured
  playback rate and emulation speed as a percentage of real time, both
  against the PIT. It also shows the target lead, underrun and overrun
  resync counts, and the rate-scale range used.

DOSBox-X checks with Ys I from a disk-image copy, 1,500 frames, `-dosvga`:

- Dynamic core at maximum cycles: 99.6% of real time. The 16-bit and 8-bit
  runs each had 0 underrun frames and 0 resyncs, with rate scale up to
  1.0199 and 1.0118.
- Normal core at 30,000 fixed cycles: only 23% of real time, so underruns
  cannot be avoided. The resync handled them (404 at 16-bit, 230 at 8-bit)
  instead of letting playback read stale ring data.
- A wrong `BLASTER` DMA channel (`D3`) produced the new DMA-start message and
  a silent, clean run.

The 60-frame `-SoundTest`, `-Sound44kTest`, and `-Sound8Test` regressions at
12,000 cycles still pass. Those runs are at about 10% of real time, which
explains their long-standing underrun counts. The physical-PC effect of
these changes is not yet tested. Remaining 8-bit hiss on DSP 3.xx cards is
inherent to 8-bit output. The AZT2320's 16-bit Windows Sound System codec
would need a separate backend.

The next physical run of that build had no sound. The log reported
`DMA 5 did not start`, so the card had been treated as an SB16. The same card
reported DSP 3.01 before. A second log reported `DMA 1 did not start`.
`BLASTER` was unset in both logs. The reset pulse was an empty 1,000-iteration
loop, which is CPU-speed dependent and may be removed by the compiler. On a
fast CPU it can be shorter than the DSP's 3 us minimum. The detection path
was changed as follows:

- The reset pulse is now 16 ISA port reads at base+6. Each read takes about
  1 us of bus time, independent of CPU speed.
- The DSP version is logged before DMA starts: `Sound Blaster DSP x.yy at
  ... DMA d HDMA h`.
- Only DSP major version 4 selects 16-bit. Other replies use 8-bit, including
  implausible ones.
- If 16-bit DMA does not start, the DSP is reset and the 8-bit path is tried.
  A failed check logs the channel, the DMA count before and after, and the IRQ
  count.

DOSBox-X at 30,000 cycles, normal core: an SB16 selected 16-bit. An SB16
with `BLASTER` `H7`, while the card used HDMA 5, logged `16-bit DMA 7 did not
start (count 3FFF -> 3FFF, IRQs 0)`, then played 8-bit. `sbtype=sbpro2`
(DSP 3.02) selected 8-bit directly. The 12,000-cycle `-SoundTest` and
`-Sound8Test` regressions pass.

Physical result, user-reported 2026-09-30: after a full power cycle and an
F8 command-prompt boot, `YS1HW.BAT` detected DSP 3.01 at `220h`, IRQ 5, DMA 1
with `BLASTER` unset. It played 8-bit mono at 22,222 Hz nominal. Over 64.3 s
and 3,556 frames, the log showed 0 underrun frames and 0 resyncs, minimum
DMA lead 3,206 samples, emulation at 99.8% of real time, and rate scale
1.0000 to 1.0251. The card's measured playback rate was 22,755 Hz, about 2.4%
above nominal. That clock offset explains the earlier open-loop underruns;
rate control now absorbs it. The user reports very good sound, with the hiss
expected of 8-bit output. Under Windows 98 "Restart in MS-DOS mode", even
with `BLASTER=A220 I5 D1 T4`, DSP 3.01 answered but DMA did not move
(`7FFF -> 7FFF`, 0 IRQs). QUASI88 now reports this and continues silently.
The card needs its DOS initialization in that mode, for example from
`DOSSTART.BAT`.

### WSS bring-up tool (`WSSTEST.EXE`)

The next goal is a 16-bit Windows Sound System (WSS) backend for the user's
AZT2320. In Sound Blaster mode that card reports DSP 3.01 and plays only
8-bit. The Linux ALSA `azt2320` driver enables WSS by writing DSP commands
`09h`, `00h` to the SB port at +`0Ch`, then waiting 5 ms. It passes the
card's Plug and Play WSS port directly to the AD1848/CS4231 codec driver.
No command to return to SB mode was found in that driver.

`.\dos\build.ps1 -Target WssTest -WatcomRoot D:\watcom` builds
`build-dos\WSSTEST.EXE`. It links only the DOS PIT timer (`wait.c`) and
compiles with warnings treated as errors. The tool:

- Reads `BLASTER`, plus an optional `Q88WSS` in the same format (`A` codec
  port, `I` IRQ, `D` 8-bit DMA channel).
- Probes 530/534, 604/608, E80/E84, and F40/F44 for a codec. The index
  register must hold the written index, I1 must keep a test value (restored
  afterwards), and I12 must report ID `Ah`.
- With `/AZT`, if no codec was found, sends the AZT2320 WSS switch and probes
  again.
- Reports I12, MODE2/I25 support, and I0-I11. It then sets 16-bit mono
  22,050 Hz (I8 `47h`) with single-channel DMA and autocalibration, and
  plays a 689 Hz tone for 3 s. The codec interrupts twice per 16,384-sample
  ring.
- Measures DMA progress, codec IRQs, and the playback rate against the PIT.
- Restores the DAC and pin registers, the DMA mask, the IRQ vector, and the
  PIC masks, then checks whether the Sound Blaster DSP still answers.

DOSBox-X has no AZT2320 or standalone WSS emulation. With an SB16, the tool
found no codec, `/AZT` failed cleanly, and the DSP still answered. A
`gustype=max` scan found a false positive at base+104h (the GF1 register
select/data pair; I12 reads `00h`). The I12 ID check now rejects it.
DOSBox-X's GUS MAX codec was not located, so codec playback is untested
until the physical run.

Physical AZT2320 result, user-reported 2026-09-30 from an F8 command
prompt. Windows 98 Device Manager showed I/O `220-22F`, `388-38F`, and
`534-537`, IRQ 5, and DMA 1 and 0. `CONFIG.SYS` and `AUTOEXEC.BAT` load no
Aztech software. Before the switch, no codec answered at any candidate port.
After `09h, 00h`, a codec answered at `534`: I12 `CAh` (ID `Ah`), MODE2
supported, I25 `80h`, which is CS4231-class. Autocalibration completed
(I8 `47h`, I9 `0Ch`). The user heard the tone. There were 8 codec IRQs in
3 s at 8,192 samples per IRQ, about 21,800 Hz, consistent with 22,050 Hz.
The reported DMA rate of 169,412 Hz was a measurement error. The test read
the 8237 count thousands of times per second, and a low/high byte tear
looked like almost a full extra ring pass. Afterwards the DSP still
answered, but QUASI88's 8-bit SB DMA did not move (`7FFF -> 7FFF`) until
power-off. The card stays in WSS mode.

Changes after that run:

- `WSSTEST` and QUASI88 read the 8237 count until two reads agree within
  2, so a torn byte pair cannot be used. QUASI88 reads it once per frame,
  where a tear was rare but could cause a false resync.
- With `/AZT`, `WSSTEST` sends `09h, 01h` after the tone to return to Sound
  Blaster mode. That is `GALAXY_COMMAND_SB8MODE` in the ALSA Aztech Sound
  Galaxy (AZT1605/AZT2316) driver. That driver defines the command but never
  sends it, and does not cover the AZT2320, so its effect on this card needs
  the physical test. `WSSTEST` then resets the DSP, starts a single-cycle
  8-bit SB transfer of silence, and reports whether the 8237 count moves. It
  pauses and resets the DSP before the block ends, so no SB IRQ is raised.
- `WSSTEST` also reports a playback rate derived from the codec IRQ count.

DOSBox-X `sbtype=sbpro2` (DSP 3.02, no WSS codec): `WSSTEST /AZT` found no
codec, sent both mode commands, and then reported `Sound Blaster 8-bit DMA 1
test: moves`. The QUASI88 12,000-cycle `-SoundTest` and `-Sound8Test`
regressions pass with the new count read.

Physical follow-up, user-reported: after an F8 boot, `WSSTEST /AZT` played
the tone and returned the card to SB mode with `09h, 01h`, and Ys I had
sound. After Windows 98 "Restart in MS-DOS mode", `WSSTEST` found the codec
without the switch, so the Windows 98 WDM driver leaves the card in WSS
mode. That explains the earlier SB DMA stall in that mode. After
`WSSTEST /AZT`, QUASI88 reported SB DMA and IRQs running (22,753 Hz, 11
IRQs), but Ys I was silent. The analog path is probably muted.
`WSSTEST /SB` (`SBMODE.BAT`) was added for this. It logs the SB Pro mixer
registers (`04h`, `0Ah`, `0Ch`, `0Eh`, `22h`, `26h`, `28h`, `2Eh`). If the
codec answers, it also logs I0-I7 and sets I2-I5 to `0Ch` and I6/I7 to
`08h`, the unmuted values observed after an F8 boot. It then sends
`09h, 01h`, logs the SB Pro mixer again, and runs the SB DMA check. `/AZT`
performs the same restore after the tone. In DOSBox-X with `sbtype=sbpro2`
and `sb16`, `/SB` reported `RESULT: PASS (Sound Blaster mode restored)`.

Physical result, user-reported: after Windows 98 "Restart in MS-DOS mode",
`WSSTEST /SB` restored Sound Blaster mode and Ys I had sound. It ran for
45.3 s with 0 underruns, and the card played at 22,735 Hz. The logs show
what the Windows driver changed. The codec had I4/I5 (aux 2) `93h`, muted
and attenuated, and I6/I7 (DAC) `87h`, muted. After an F8 boot they were
`0Ch` and `08h`. On this card the SB output evidently passes through the
codec, so those mutes silenced SB mode. The SB Pro mixer read identically
in both boots (`04h`, `22h`, `26h`, `28h`, and `2Eh` all `DDh`). The test
build also reset that mixer. The reset lowered voice/master to `99h` and
set CD/line to `00h`, so it was removed; `/SB` now only logs the SB Pro
mixer. For Windows 98 MS-DOS mode, `WSSTEST /SB` can be added to
`C:\WINDOWS\DOSSTART.BAT`.

### WSS output in QUASI88 (`-doswss`)

`audio.c` has three output modes that share the ring buffer, rate control,
resync, DMA start check, statistics, and exit cleanup:
- SB 8-bit
- SB16 16-bit on high DMA
- WSS 16-bit

WSS moves each 16-bit sample as two bytes on an 8-bit DMA channel. The ring
position is therefore the byte position divided by two. The IRQ handler
acknowledges WSS by writing the codec status register and SB by reading the
DSP acknowledge port.

`-doswss` enables WSS. It is opt-in because the AZT2320 mode switch is a
vendor DSP command. Settings:

- `Q88WSS` (`A` codec port, `I` IRQ, `D` 8-bit DMA) overrides the defaults.
  Otherwise IRQ and DMA come from `BLASTER`, or default to 5 and 1.
- The codec is probed at 534h first, the AZT2320 location, then at the
  other candidates.
- If no codec answers and an SB DSP is present, `09h, 00h` is sent and the
  codec probed again. If it still does not answer, the switch is undone with
  `09h, 01h` and a DSP reset.

When WSS starts:

- MODE2 is cleared. Format I8 is `47h` (22,050 Hz), or `4Bh` (44,100 Hz)
  with `-dossb44k`. I9 is set to ACAL and SDC, and calibration must finish.
- Aux inputs I2-I5 are muted (`8Ch`): they carry the SB DSP and FM output,
  which WSS does not use. The DAC (I6/I7) is set to 0 dB (`00h`).
- The playback count is set to one IRQ per ring pass. IEN and then PEN are
  enabled, and the DMA start check must pass.

On exit, or if WSS start fails, I2-I5 are set to `0Ch` and I6/I7 to `08h`
(the F8 values), then `09h, 01h` and a DSP reset return the card to Sound
Blaster mode. A failed WSS start falls back to SB16 or SB 8-bit output.

The target DMA lead is now a fixed 8,192 samples in every mode. For SB16 it
was a quarter ring (4,096 samples). On the physical AZT2320 the lead once
dipped about 5,000 samples below target, which a 4,096-sample target would
not absorb.

2026-09-30 validation:

- The Machine build has no new warnings.
- 12,000 cycles: `-SoundTest`, `-Sound44kTest`, `-Sound8Test`, and the new
  `-WssFallbackTest` pass. The last sets `-doswss` and requires
  `no WSS codec found; using Sound Blaster output`, then SB16.
- Ys I, 1,500 frames, `core=dynamic`, maximum cycles: at 99.4% of real time,
  SB16 had 0 underruns and 0 resyncs (minimum lead 4,061 samples). A second
  run on a busier host reached only 79-97% of real time and had underrun
  resyncs, as expected below real time.
- DOSBox-X cannot exercise WSS playback.

Physical AZT2320 result, user-reported, F8 boot, `YS1WSS.BAT`: both runs
found the codec at 534h (I12 `CAh`, I25 `80h`) and played 16-bit mono.

| Run | Length | Card rate | Resyncs | Emulation | Rate scale | Min. lead |
|---|---|---|---|---|---|---|
| 1 | 19.6 s | 22,053 Hz | 0 | 99.6% | up to 1.0151 | 4,562 |
| 2 | 75.7 s | 22,048 Hz | 0 | 99.6% | 0.9998 to 1.0377 | 357 |

The codec's crystal-derived rate is close to nominal. In SB mode the same
card played at 22,787 Hz. `YS1HW` afterwards had Sound Blaster sound, so the
return to SB mode works. The user reports a huge improvement over 8-bit and
much less hiss. They also heard a slight pop, like a plosive into a
microphone, on heavier notes of the opening melody, not at startup. The
log shows `clipped=0` and no resyncs, so the pop is probably in the analog
output chain. The user considers it normal for their setup, and it is left
unchanged. The DAC plays at 0 dB, 12 dB above the power-on level, which
could be lowered if needed.

#### DOSBox-X dynamic core: use `core=normal`

With `core=dynamic` or `core=auto`, DOSBox-X 2026.08.31 gives wrong
floating-point results in CauseWay programs after four launches in the same
session. A ten-line Open Watcom/CauseWay test computing `22050.0 / 55.4f`
printed 398 for runs 1-4, then 0, then 399, in a repeating pattern. The same
program printed 398 for all 12 runs with `core=normal`. The x87 status and tag
words were clean at each startup, and `_fpreset()` did not help. The build from
commit `2b220f4`, before the SB16 work, reproduced it too. In QUASI88 the
refresh rate reads as 0, so the core aborts with `...FAILED, abort` during
sound setup. This is a DOSBox-X dynamic-core problem, not a QUASI88 fault and
not evidence about real hardware. Use `core=normal` for DOS testing, or
restart DOSBox-X between launches. Killing DOSBox-X during a run leaves a
CauseWay swap file (a random extensionless name) in the current directory.
Delete it only when no CauseWay program is running.

The active backend uses a 32 KiB DMA ring with a 16 KiB initial lead and carries
fractional samples across frames to keep its producer rate aligned with the
selected DSP rate. `PCMZERO.BAT` sends unsigned 8-bit center samples while
keeping DSP/DMA/IRQ active; `NOSND.BAT` disables the DSP. `FILTONE.BAT` and
`FILTER.BAT` enable the optional experimental high-frequency roll-off for OPN
tone and OPNA game tests, respectively. The user heard less hiss but a
shattered tone with the filter, so it remains off by default.

To check that DOSBox-X's Sound Blaster transfer and IRQ path both advance, run
at least 60 frames with the split real-ROM set:

```powershell
.\dos\build.ps1 -Target Machine -WatcomRoot D:\watcom
.\dos\machtest.ps1 -DosBoxX D:\DOSBox-X\dosbox-x.exe -Cycles 12000 -RomDirectory D:\88rom -Mk2srDirectory D:\88rom\pc8801mk2sr -Frames 60 -SoundTest
```

The sound check requires a changed DMA count and at least one IRQ in
`MACHINE.OUT`. It validates DMA/IRQ delivery even when the ROM produces silence;
the BASIC `CMD PLAY "T32L1C"` check is still needed to confirm audible
synthesized audio. The test configuration emulates an SB16 with `hdma=5`, so
it checks the 16-bit path by default. Add `-Sound44kTest` to check the
44,100 Hz rate, or add `-Sound8Test` to force and check the 8-bit DSP path.

The DOS entry point requires a 32768-byte `N88.ROM`, or `N88N.ROM`/`N80.ROM`
when `-n` selects N-BASIC, before starting the core. This avoids silently running
with a missing or truncated main ROM. Combined ROM files are not supported in
this milestone. Other ROMs retain the upstream loader's behavior: missing data
can be filled with `FF`, and the built-in font can substitute for `FONT.ROM`.
Passing startup therefore does not prove ROM completeness or a working BASIC
prompt. Use `-verbose 1` to see each ROM loading result.

Game-port joystick support maps one controller's X/Y axes and buttons A/B;
calibration is sampled at startup. Mouse input requires an installed DOS INT
33h driver and is polled once per emulated frame. Frame pacing reads the
BIOS tick and PIT channel 0 counter without reprogramming the timer or hooking
interrupts. It busy-polls the hardware timer for sub-frame waits, so it uses
the CPU while waiting; DOS has no portable high-resolution sleep path in this
backend. The VGA backend changes the BIOS mode and restores it on normal exit.
The QUASI88 toolbar is visible on the reported physical PC. DOS mouse input
polls the standard INT 33h driver while VGA is active and forwards absolute
pointer movement and button transitions through the existing screen/UI event
path. The user confirmed physical pointer movement and toolbar clicks, then
confirmed the cursor residue is gone after the planar redraw fix. The sound
backend owns its selected IRQ vector and DMA channel only while audio is active.
D88 image mounting, FDC sector read/write, and image-file writes have a focused
DOSBox-X fixture test
using the core's FDC port interface. Save-state serialization now has a DOSBox-X
round-trip test over synthetic CPU markers and the DOS file backend; interactive
state-menu use on physical hardware remains unverified. Guest-driven disk
commands and interactive screenshot use remain unvalidated as machine features.
Configuration load/save is covered by a synthetic DOSBox-X round-trip test;
physical hardware behavior remains unverified. Keep writable user media outside
test mounts.

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
memory managers, ROM filenames/sizes, and the complete console output. The
user has confirmed visible VGA game output on the physical PC; sustained
performance and i740-specific behavior remain unverified.

### Physical game and keyboard follow-up

2026-09-29: the user reported that the Ys1 disk starts and displays its game
screen on the physical DOS PC. Holding a key had a long initial repeat delay,
followed by a beep from the PC when held for a long time. Replacing BIOS
typematic-buffer polling, the machine now captures raw IRQ1 make/break scan
codes in a 128-byte locked ring buffer; the interrupt handler is an 81-byte,
call-free assembly routine. Event translation runs outside interrupt context,
and the old interrupt vector is restored on normal exit. If memory locking
fails, the prior BIOS polling path remains as a fallback. The Open Watcom
machine build compiled and linked successfully with no warnings in the changed
DOS C or assembly sources.
`dos/machtest.ps1 -DosBoxX D:\DOSBox-X\dosbox-x.exe -Cycles 12000 -RomDirectory D:\88pseudorom -VgaTest`
passed ROM preflight, three bounded frames, VGA readback, and text-mode
restoration with the IRQ1 handler installed and then removed. This check does
not synthesize keyboard IRQs or emulate a held key, so key mapping, repeat
behavior, and the beep remain physical-hardware checks.

The user then verified F11/F12 in DOSBox-X and found keypad directions did not
move the Ys1 character. The raw DOS mapper had assigned non-extended keypad
scans to cursor keys instead of `KEY88_KP_1` through `KEY88_KP_9`; these now
match the SDL port, while E0-prefixed dedicated arrow keys retain cursor-key
codes. The rebuilt machine passed the same ROM/VGA startup regression. Actual
in-game movement then passed DOSBox-X and physical-machine testing. The user
reports that the IRQ keyboard path now works perfectly on the physical PC.

### Analog game-port joystick

The DOS event backend now measures game-port X/Y RC timers against PIT channel 0,
averages a centered stick position at startup, applies a dead zone, and sends
direction plus A/B pad transitions while `-joystick` mode is selected. Timer
reads do not reprogram PIT channel 0, and each sample is capped at 5 ms. The
Ys1 test batch now selects `-joystick`. Open Watcom compilation passed without
warnings in DOS sources. DOSBox-X passed synthetic startup with `-JoystickMode`
and no host joystick, plus the real-ROM VGA regression at 12,000 cycles. Those
tests do not emulate analog game-port movement. The user tested through a
DOSBox-X host joystick/gamepad and reported that Y movement was inverted. The
host-gamepad path reports the opposite Y polarity from the physical game port.
An attempted reversal fixed the host-gamepad test but inverted Y on the
physical PC, so the code was restored to the physical-port polarity. The user
confirmed that build works correctly on the physical PC and reported that
save/load also passes. DOSBox-X's no-device check cannot validate analog
direction.

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

### State-file round-trip milestone

Run the synthetic marker save/load check:

```powershell
.\dos\build.ps1 -Target Machine -WatcomRoot D:\watcom
.\dos\machtest.ps1 -DosBoxX D:\DOSBox-X\dosbox-x.exe -Cycles 12000 -StateTest
```

`-StateTest` uses the synthetic CPU ROM fixture and requires its `-doscheck`
marker test. After three frames, QUASI88 writes `QUASI88.STA`, changes the main
CPU RAM marker, restores the state through the regular serializer and DOS file
backend, and checks that both CPU markers match their saved values. The state
file stays in the fresh ignored test directory. This verifies serialization and
file I/O in DOSBox-X; it does not yet validate using state save/load from the
interactive PC-88 menus or on physical DOS hardware.

2026-09-29: Open Watcom machine build succeeded with warning level 4 and
warnings as errors for DOS-specific sources. The state round-trip passed in
DOSBox-X at 12,000 fixed cycles; `QUASI88.STA` was 178,407 bytes and both CPU
markers were restored. Physical state-menu behavior remains untested.

### Screenshot file-output milestone

Run the synthetic BMP output check:

```powershell
.\dos\build.ps1 -Target Machine -WatcomRoot D:\watcom
.\dos\machtest.ps1 -DosBoxX D:\DOSBox-X\dosbox-x.exe -Cycles 12000 -SnapshotTest
```

`-SnapshotTest` uses the synthetic CPU ROM fixture, saves a BMP through the
normal screen snapshot code, then checks the DOS output file's signature,
dimensions, and byte length. The file remains in the fresh ignored test
directory as `SAVE0000.BMP`. The synthetic screen is black; this test validates
the screenshot writer and DOS file output, not colors from a real ROM or the
interactive menu action on physical DOS.

2026-09-29: The screenshot test passed under DOSBox-X at 3,000 fixed cycles.
It wrote the expected 640x400 24-bit BMP (768,054 bytes). The combined VGA,
state, and screenshot tests also passed at 12,000 cycles. Interactive screenshot
quality and physical DOS output remain unverified.

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
write mode afterward. DOSBox-X plane readback passes with this change. The user
confirmed that mouse movement and clicks still work and the toolbar residue is
gone.

2026-09-29 launcher follow-up: The user confirmed `RUN_Q88.BAT` now works on the
physical PC. It is a two-line DOS batch with CRLF line endings. DOSBox-X also
accepted the batch syntax and created the keyboard and mouse logs.

2026-09-29 keyboard follow-up: The DOS BIOS mapper now forwards F6-F10 to the
existing PC-88 function-key codes. This compiles with Open Watcom; physical
behavior remains for the next hardware test.

2026-09-29 physical feature pass: The user reports all planned checks worked
except the joystick test, since no joystick is available. The DOS PC ran BASIC,
keyboard shortcuts and menus, mouse/toolbar input, normal exit, INI persistence,
and BMP snapshots. The desktop `QUASI88.INI` is 3,950 bytes and includes
`-saveconfig`; `SAVE0000.BMP` and `SAVE0001.BMP` are both valid 640x400 24-bit
BMPs of 768,054 bytes. The first image visibly captures the BASIC screen. The
PC game-port button path remains untested on hardware.

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
