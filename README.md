# SP-1 Tape Looper

A four-track, tape-machine-style looper for the Teenage Engineering SP-1.

This is custom firmware that turns the SP-1 stem player into a hands-on tool for
building layered loops. You feed audio in over USB-C (the SP-1 appears on your
computer or phone as a USB sound card / speaker), record loops with the four
track buttons, and they play back layered together through the speaker or
headphones. The rocker changes playback speed and pitch together, like tape.
Loops are saved to the SP-1's internal flash, so they survive power-off and
re-flashing the firmware.

## What's new in this release

- **48 kHz.** The main build now records and plays at the full 48 kHz — earlier
  releases ran at 24 kHz for reliability, and that reliability work is done.
  (A 24 kHz build of the same firmware is still included.)
- **Hands-free (latched) recording.** Hold a track button to start a take, then
  let go — it keeps recording until you tap the same button again. Long takes
  no longer mean holding a button down for their whole length.
- **Independent loops.** Every take is exactly as long as you play it, and each
  track cycles on its own length: a 3-second phrase on track 4 free-runs over a
  17-second bed on track 1. No quantization to the first track, no silence
  padding while you hunt for the loop point.
- **Loop transfer fixed.** The upload/download website could not connect to the
  previous release (a USB receive bug on the device side, issue #1). It
  connects now, and it understands independent track lengths.
- Reliability throughout: full four-track recording at 48 kHz without crackle,
  and a scheduling bug that could make everything (lights included) slow down
  and eventually reboot the device is fixed.

## Loop transfer tool

Move loops between your computer and the SP-1 as WAV or MP3:

### → https://chattock.github.io/sp1-tape-looper/

Open it in Chrome or Edge with the SP-1 plugged in and powered on **normally**
(no button combo — not the bootloader mode), click Connect, and pick the
`SP-1 Audio` port. The looper switches itself into transfer mode while the page
is connected (playback pauses, the four track lights blink together) and
returns to normal when you disconnect. Download any track as a WAV, or upload a
WAV/MP3 into a chosen song and track — clips are resampled to the device's rate
(mono), and every track keeps its own length, exactly like recording on the
device.

---

## 1. What's in this folder

```
sp1-tape-looper/
├── README.md               you are here
├── sp1_looper.bin          the main build — 48 kHz (flash this one)
├── sp1_looper_24kHz.bin    the same firmware at 24 kHz (warm / lo-fi)
└── firmware/               full source code (for reading / rebuilding)
    ├── src/
    │   ├── main.c          the whole looper: audio engine, controls, power, USB
    │   ├── sp1_emmc.c      the flash-memory driver (stores/loads the loops)
    │   └── sp1_emmc.h
    ├── app.overlay         hardware pin map (ADC channels for buttons/faders)
    ├── prj.conf            Zephyr build configuration
    ├── CMakeLists.txt
    └── Kconfig
```

For normal use, flash `sp1_looper.bin` (Sections 2–3). To read or change how it
works, start with `firmware/src/main.c`, which opens with a full architecture
overview.

Both binaries are the same firmware. The 24 kHz build trades the top octave of
detail (a warm, slightly lo-fi sound) for double the storage headroom. Loops
recorded by one build are not readable by the other — switching between them
reformats the loop storage once.

---

## 2. Flashing it onto the SP-1

The SP-1 is flashed with the Solderless updater (the same tool used for any
SP-1 custom firmware) — no soldering or opening the device required:

1. Open the Solderless SP-1 update tool: <https://solderless.engineering>
2. Connect the SP-1 to your computer with a USB-C cable.
3. Put the SP-1 into firmware-loading mode (hold **Track 1 + Track 4** while
   powering on) and select the `.bin` file.
4. Flash, then unplug and replug.

> Note: loops you record survive re-flashing the same build. The first time you
> install it (coming from the stock firmware, an older release, or the other
> sample-rate build) it reformats the loop storage once, so anything previously
> on the device is cleared.

---

## 3. Controls

### Turning it on and off
- The device boots into a quiet charging/standby state. Plugging in USB or
  finishing a flash does not start playback.
- Hold the FUNCTION button (the lower button) for about 0.6 s to turn it on.
- Hold FUNCTION for about 2.5 s to turn it off (the four centre LEDs fill as a
  countdown; release early to cancel).

### The four track buttons

| Gesture | Action |
|---|---|
| Hold (~0.2 s), then let go | **Arms and records, hands-free.** Capture begins on the first sound it hears (a count-in is never recorded), and keeps going after you release. |
| Tap the same track while it records | **Ends the take** — it starts looping immediately, exactly as long as you played it. A tap before any sound has arrived cancels the arm instead. |
| Quick tap (playing track) | Mutes / unmutes that track (content kept). |
| Double-tap | Deletes that track. |

- **Every track loops at its own length.** Takes are never stretched, padded or
  snapped to another track's length.
- The **first take of a song** also sets the beat grid for the LEDs and the
  MIDI clock; every take after that is free.
- Only one track records at a time. Recording onto a non-empty track replaces
  it.
- A take that reaches the per-track maximum (about 5 minutes at 48 kHz, about
  10 minutes at 24 kHz) finalizes itself.

### Playback
- PLAY button, tap: play / stop (with a tape-style speed glide).
- PLAY button, hold (about 0.5 s): jump to the start of the loop and play.

### Mixing and tempo
- The four faders set the volume of each track (fader 1 = track 1, and so on).
- The VOL +/- buttons set the overall (master) volume — a smooth, gradual
  control that steps all the way down to fully muted. Hold to sweep quickly.
- The FWD / RWD buttons change the tempo / playback speed, one BPM per press;
  hold to sweep faster. This is a tape-style speed change — faster also means
  higher pitch — and it bends all four loops together.

### Songs
- There are four song slots. The four side LEDs show which song is selected
  (LED 1 = song 1 … LED 4 = song 4).
- Tap the FUNCTION button to move to the next song. Each song remembers its own
  tracks, lengths and tempo.

### Headphones
- Plug headphones into the headphone jack (the one nearest the headphone
  symbol, not the second/sync jack). The speaker mutes automatically while
  headphones are connected and returns when you unplug them.

### MIDI clock out (the second / sync jack)
- The sync jack sends a MIDI clock to external gear, locked to the looper's
  tempo: MIDI Start/Stop plus a 24-PPQN clock.
- Use a 3.5 mm TRS-to-MIDI-DIN adapter appropriate for your gear.
- The MIDI timing is generated by a hardware timer, so driving external gear
  does not disturb the audio. If a device reads the signal inverted, it is a
  one-line firmware flag (`MIDI_INVERT`) to flip.

### If it ever locks up
- Hold Track 1 and Track 4 together for about 1.2 s to return to
  firmware-loading mode, so you can always re-flash.

---

## 4. Audio quality

- Tracks are mono, 16-bit, at 48 kHz (main build) or 24 kHz. Mono (rather than
  the stock player's stereo) is a storage-bandwidth choice.
- Every playing track is a separate stream read off the flash in real time,
  while the track you are recording is written back. The engine reads a third
  of a second ahead per track into RAM, every flash operation is time-bounded
  and self-correcting (CRC-checked with retry), and the USB input can no longer
  be starved by the audio engine — the fixes behind this release's full-rate,
  four-track reliability.
- The flash occasionally pauses for its own housekeeping; the read-ahead hides
  it. Nothing is damaged and the loops on disk stay intact.

---

## 5. How it works

The audio path, end to end:

```
USB-C in ──► [USB ring buffer] ──► audio engine ──► I2S bus ──► speaker / headphones
                                       │   ▲
                               record  ▼   │  play
                                  [ flash memory: one region per track ]
```

Clocking. The SP-1's on-board 3.072 MHz oscillator drives the audio bus, and
the headphone codec (a Cirrus CS42L42) generates a true 48 kHz frame from it.
The main chip (an nRF52840) and the speaker amplifier (a TI TAS2505) follow as
clock slaves. Running the board exactly as Teenage Engineering designed it is
what keeps both the speaker and the headphones clean.

Threads, in priority order:

- Audio engine — runs on every I2S block. It mixes the four playback tracks
  plus the live USB input and, when recording, writes the live input into the
  one track being recorded. A soft limiter keeps stacked tracks from clipping.
  It outranks every other thread, with one deliberate exception: the USB stack
  may interrupt it for its ~0.1 ms service moments — that detail is what makes
  full-rate recording glitch-free.
- Main loop — buttons, faders, LEDs, power, and the serial status line. It sits
  above the streamer so the controls stay instant no matter how busy the flash
  is.
- Streamer — the only thread that touches the flash. It writes the in-progress
  recording out and reads the playing tracks ahead of the playhead.

Storage. Tracks are independent; there is no mixdown. Each track has its own
fixed region on the flash (block 0 holds a small metadata header, then four
track regions per song, for four songs) and its own length in that header.
Recording a track writes only that track's audio to its own region; the others
are never rewritten, only read. Mixing happens live in the audio engine and is
never stored. The flash is driven through the nRF's hardware SPI engine with a
CRC check and retry on every block, and write bursts are aligned to the flash's
8 KB internal pages. This is not the stock Teenage Engineering album/stem
format; it is a purpose-built layout for live looping.

The status line. Open the SP-1's USB-serial port (115200 baud) and you will see
a repeating status report:

```
LOOPER 48000Hz song=1 PLAY ... trk[PLY PLY REC ---] rec=2 ovr=0 rerr=0 werr=0 ...
EMMC48 ... low=...ms hiw=...ms rr=2 flt=ffffffff@0 ...
USBIN ... nb=0 ... zp=0
```

Healthy signs: `ovr=0` (no record-buffer overflow), `rerr=0 werr=0` (clean
flash bus), `zp=0` (no silence patched into the live input), and
`flt=ffffffff@0` (no crash recorded). After any unexpected reboot the `flt=`
and `rr=` fields carry the crash details — including them makes a bug report
immediately actionable.

---

## 6. Building from source

The firmware is a [Zephyr](https://www.zephyrproject.org/) application built
against a custom board definition for the SP-1. With a Zephyr workspace and the
nRF SDK set up, point a `west build` at the `firmware/` folder, then convert
the resulting ELF to a bootloader-friendly `.bin` (the application is linked to
load at `0x20000`).

Build knobs:

- `SP1_BUILD_24K` in `firmware/src/sp1_emmc.h` — `0` builds 48 kHz (default),
  `1` builds 24 kHz. It selects the sample rate and the matching flash-bus
  clock.
- `SP1_XFER_ENABLE` in `firmware/src/main.c` — `1` includes the loop-transfer
  mode (as the shipped builds do); `0` compiles it out entirely.

The most important rule when modifying the firmware: the SP-1 has no hardware
reset, so the firmware must always feed the watchdog and must always offer a
path back to the bootloader (here: hold FUNCTION to power off, or the
Track 1 + Track 4 recovery combo). Do not remove those.

---

## Disclaimer

This is unofficial, community-made firmware, not affiliated with or endorsed by
Teenage Engineering. Flashing custom firmware is at your own risk. It has been
tested and works well, but no warranty is implied. If in doubt, make sure you
understand the [Solderless](https://solderless.engineering) update process and
the recovery options above before flashing. The Track 1 + Track 4 combo always
returns the device to firmware-loading mode.

## Credits and licence

- Built on the SP-1 hardware reverse-engineering and board support documented
  in Tim Knapen's [SP-1-dev](https://github.com/timknapen/SP-1-dev) project —
  the pin map, the codec and clock notes, and the eMMC protocol all came from
  there.
- Flashing is made possible by the [Solderless](https://solderless.engineering)
  SP-1 updater.
- Runs on the [Zephyr RTOS](https://www.zephyrproject.org/).

Released under the MIT License (see `LICENSE`). Contributions and forks
welcome.
