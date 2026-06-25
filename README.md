# SP-1 Tape Looper

A four-track, tape-machine-style looper for the Teenage Engineering SP-1.

This is custom firmware that turns the SP-1 stem player into a hands-on tool for
building layered loops. You feed audio in over USB-C (the SP-1 appears on your
computer as a USB sound card / speaker), record loops by holding the four track
buttons, and they play back layered together through the speaker or headphones.
The rocker changes playback speed and pitch together, like tape. Loops are saved
to the SP-1's internal flash, so they survive power-off and re-flashing the
firmware.

## Loop transfer tool (BETA)

Move loops between your computer and the SP-1 as WAV or MP3:

### → https://chattock.github.io/sp1-tape-looper/

> **Beta.** This works only with the **48 kHz beta build** (`sp1_looper_48kHz_beta.bin`),
> not the default 24 kHz build. The transfer feature is still being optimised — its
> USB handling costs a little CPU under a full four-track load, so on the beta build
> the last track can struggle. The stable default build does **not** include
> upload/download yet.

Open it in Chrome or Edge with the SP-1 (on the beta build) plugged in and powered
on normally, click Connect, and pick the `SP-1 Audio` port. No button combo or
special mode is needed — the looper switches itself into transfer mode the moment
the page selects that port (it pauses and the four track lights blink together).
You can download any track as a WAV, or upload a WAV/MP3 into a chosen song and
track — clips are resampled to the device's rate (mono) and snapped up to a whole
multiple of the song's first track (the difference is filled with silence, matching
how the looper layers takes).

## Features

- Four independent loop tracks, each with its own fader for level.
- Hold-to-record with auto-start: hold a track button and recording begins on the
  first sound it hears (no clipped attacks). The first recording sets the base
  loop length; later ones overdub in time with it.
- Different-length loops: keep holding an overdub past the end of the loop and it
  adds another bar, and another, in whole multiples of the base length — so a
  two-bar melody can sit over a one-bar drum loop, all locked in time.
- Tap to mute / unmute a track; double-tap to erase it.
- Tape-style tempo: the FWD/RWD rocker changes playback speed and pitch together,
  one BPM per press.
- Tempo detection: the first loop's rhythm is analysed to estimate its BPM, which
  drives the beat LEDs and the MIDI clock.
- MIDI clock out on the sync jack: MIDI Start/Stop plus a 24-PPQN clock to drive
  external MIDI gear, locked to the looper's tempo.
- Four song slots, each remembering its own tracks and tempo; loops persist
  across power-off and across re-flashing the firmware.
- Speaker and headphone output, both clean; the speaker mutes automatically when
  headphones are plugged in.
- Real power behaviour: charge/standby on plug-in, hold to switch on and off,
  battery-friendly sleep.
- Self-correcting flash storage (hardware CRC-checked with retry), plus a watchdog
  and a recovery key-combo, so the device can always be re-flashed.

---

## 1. What's in this folder

```
sp1-tape-looper/
├── README.md                you are here
├── sp1_looper.bin           DEFAULT build — 24 kHz, rock-solid (flash this one)
├── sp1_looper_48kHz_beta.bin BETA — 48 kHz + loop transfer (see notes below)
└── firmware/                full source code (for reading / rebuilding)
    ├── src/
    │   ├── main.c       the whole looper: audio engine, controls, power, USB
    │   ├── sp1_emmc.c   the flash-memory driver (stores/loads the loops)
    │   └── sp1_emmc.h
    ├── app.overlay      hardware pin map (ADC channels for buttons/faders)
    ├── prj.conf         Zephyr build configuration
    ├── CMakeLists.txt
    └── Kconfig
```

For normal use, flash `sp1_looper.bin` (Sections 2–3). To read or change how it
works, start with `firmware/src/main.c`, which opens with a full architecture
overview.

**`sp1_looper.bin` — the default.** Runs at 24 kHz, built for reliability under a
heavy load. Recording mono at half the rate halves the flash bandwidth and leaves
roughly twice the headroom, so all four tracks stay solid while you record and
overdub — at the cost of the top octave of high-frequency detail (a warm, slightly
lo-fi sound). This is the proven, rock-solid build. It does **not** include the
loop transfer tool.

**`sp1_looper_48kHz_beta.bin` — beta.** Runs at 48 kHz (full fidelity) and adds the
USB **loop transfer** feature (upload/download via the website). It's labelled beta
because the transfer code costs a little CPU, so the last track can struggle under
a full four-track load. Use it if you want the upload/download feature; use the
default for the most reliable looping. The two builds save in different on-flash
formats, so switching between them clears any saved loops.

---

## 2. Flashing it onto the SP-1

The SP-1 is flashed with the Solderless updater (the same tool used for any SP-1
custom firmware) — no soldering or opening the device required:

1. Open the Solderless SP-1 update tool: <https://solderless.engineering>
2. Connect the SP-1 to your computer with a USB-C cable.
3. Put the SP-1 into firmware-loading mode and select the `.bin` file.
4. Flash, then unplug and replug.

> Note: loops you record survive re-flashing this firmware. The first time you
> install it (coming from the stock firmware) it reformats the loop storage, so
> anything previously on the device is cleared.

---

## 3. Controls

### Turning it on and off
- The device boots into a quiet charging/standby state. Plugging in USB or
  finishing a flash does not start playback.
- Hold the FUNCTION button (the lower button) for about 0.6 s to turn it on.
- Hold FUNCTION for about 2.5 s to turn it off (the four centre LEDs fill as a
  countdown; release early to cancel).

### The four track buttons
Each track button does three things, depending on how you press it:

| Gesture | Action |
|---|---|
| Hold (and keep holding) | Records into that track. Capture begins on the first sound it hears, so a held breath or count-in is not recorded. The first recording sets the base loop length. For overdubs, release after one loop for a same-length layer, or keep holding to extend the track to 2, 3, … bars (whole multiples of the base) — so tracks can be different lengths yet stay locked in time. |
| Quick tap | Mutes / unmutes that track (its content is kept; tap again to bring it back). |
| Double-tap | Deletes that track. |

Only one track records at a time. Starting a new recording on a track replaces
whatever was there.

### Playback
- PLAY button, tap: play / stop (with a tape-style speed glide).
- PLAY button, hold (about 0.5 s): jump to the start of the loop and play.

### Mixing and tempo
- The four faders set the volume of each track (fader 1 = track 1, and so on).
- The VOL +/- buttons set the overall (master) volume — a smooth, gradual control
  that steps all the way down to fully muted. Hold to sweep quickly.
- The FWD / RWD buttons change the tempo / playback speed, one BPM per press; hold
  to sweep faster. This is a tape-style speed change, so faster also means higher
  pitch.

### Songs
- There are four song slots. The four side LEDs show which song is selected
  (LED 1 = song 1 … LED 4 = song 4).
- Tap the FUNCTION button to move to the next song. Each song remembers its own
  tracks and tempo.

### Headphones
- Plug headphones into the headphone jack (the one nearest the headphone symbol,
  not the second/sync jack). The speaker mutes automatically while headphones are
  connected and returns when you unplug them.

### MIDI clock out (the second / sync jack)
- The sync jack sends a MIDI clock to external gear, locked to the looper's tempo:
  MIDI Start/Stop plus a 24-PPQN clock.
- Use a 3.5 mm TRS-to-MIDI-DIN adapter appropriate for your gear.
- The MIDI timing is generated by a hardware timer, so driving external gear does
  not disturb the audio. If a device reads the signal inverted, it is a one-line
  firmware flag (`MIDI_INVERT`) to flip.

### If it ever locks up
- Hold Track 1 and Track 4 together for about 1.2 s to return to firmware-loading
  mode, so you can always re-flash.

---

## 4. Audio quality

- Tracks are mono, 16-bit. The main build records and plays at 24 kHz; the backup
  build at 48 kHz. Mono (rather than the stock player's stereo) is a
  storage-bandwidth choice.
- Every playing track is a separate stream read off the flash in real time, while
  the track you are recording is written back. The flash has a hard ceiling on how
  many of those it can sustain at once. The 24 kHz default build needs only half the
  bandwidth per track, so it holds a full four-track session with headroom to
  spare; the 48 kHz beta trades some of that headroom for full fidelity.
- The flash occasionally pauses for its own housekeeping; the firmware reads ahead
  to hide this, so even during a busy session you would at most rarely hear a tiny
  hiccup. Nothing is damaged and the loops on disk stay intact.

---

## 5. How it works

The audio path, end to end:

```
USB-C in ──► [USB ring buffer] ──► audio engine ──► I2S bus ──► speaker / headphones
                                       │   ▲
                               record  ▼   │  play
                                  [ flash memory: one region per track ]
```

Clocking. The SP-1's on-board 3.072 MHz oscillator drives the audio bus, and the
headphone codec (a Cirrus CS42L42) generates a true 48 kHz frame from it. The main
chip (an nRF52840) and the speaker amplifier (a TI TAS2505) follow as clock
slaves. Running the board exactly as Teenage Engineering designed it is what keeps
both the speaker and the headphones clean.

Threads, in priority order:

- Audio engine — runs on every I2S block. It mixes the four playback tracks plus
  the live USB input (for the output only), and, when recording, writes the live
  input into the one track being recorded. A soft limiter keeps stacked tracks
  from clipping.
- Streamer — the only thread that touches the flash. It writes the in-progress
  recording out to flash and reads the playing tracks back into memory buffers,
  always staying ahead of the playhead.
- Main loop — reads the buttons and faders, drives the LEDs, handles power, and
  prints a one-line status to the USB-serial console.

Storage. Tracks are independent; there is no mixdown. Each track has its own fixed
region on the flash (block 0 holds a small metadata header, then four track
regions per song, for four songs). Recording a track writes only that track's
audio to its own region; the others are never rewritten, only read for monitoring.
Mixing happens live in the audio engine and is never stored. An overdub is one
region being written while the others are read, with no read-modify-write of a
combined file. The flash is driven through the nRF's hardware SPI engine with a
CRC check and retry on every block, and write bursts are aligned to the flash's
8 KB internal pages, so the bus is both fast and self-correcting. This is not the
stock Teenage Engineering album/stem format; it is a purpose-built layout for live
looping.

The status line. If you open the SP-1's USB-serial port you will see a line like:

```
LOOPER 24000Hz song=1 PLAY hp=1 usb=1 chg=1 batt=2230 bpm=80 vol=48 trk[PLY PLY --- ---] rec=-1 ovr=0 rerr=0 werr=0
```

The healthy signs are `ovr=0` (no recording-buffer overflow) and `rerr=0 werr=0`
(the flash bus is error-free).

---

## 6. Building from source

The firmware is a [Zephyr](https://www.zephyrproject.org/) application built
against a custom board definition for the SP-1. With a Zephyr workspace and the
nRF SDK set up, point a `west build` at the `firmware/` folder, then convert the
resulting ELF to a bootloader-friendly `.bin` (the application is linked to load
at `0x20000`). The source builds the stable 24 kHz default by default. Two
compile-time switches in `firmware/src/main.c` select the variant: `DECIM`
(`2` = 24 kHz, `1` = 48 kHz) and `SP1_XFER_ENABLE` (`0` = no transfer — the
default; `1` = include the USB loop-transfer mode). The beta build is `DECIM 1` +
`SP1_XFER_ENABLE 1`. With `SP1_XFER_ENABLE 0` the transfer code is compiled out
entirely, leaving the proven engine byte-for-byte.

The most important rule when modifying the firmware: the SP-1 has no hardware
reset, so the firmware must always feed the watchdog and must always offer a path
back to the bootloader (here: hold FUNCTION to power off, or the Track 1 + Track 4
recovery combo). Do not remove those.

---

## Disclaimer

This is unofficial, community-made firmware, not affiliated with or endorsed by
Teenage Engineering. Flashing custom firmware is at your own risk. It has been
tested and works well, but no warranty is implied. If in doubt, make sure you
understand the [Solderless](https://solderless.engineering) update process and the
recovery options above before flashing. The Track 1 + Track 4 combo always returns
the device to firmware-loading mode.

## Credits and licence

- Built on the SP-1 hardware reverse-engineering and board support documented in
  Tim Knapen's [SP-1-dev](https://github.com/timknapen/SP-1-dev) project — the pin
  map, the codec and clock notes, and the eMMC protocol all came from there.
- Flashing is made possible by the [Solderless](https://solderless.engineering)
  SP-1 updater.
- Runs on the [Zephyr RTOS](https://www.zephyrproject.org/).

Released under the MIT License (see `LICENSE`). Contributions and forks welcome.
