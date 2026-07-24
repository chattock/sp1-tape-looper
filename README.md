# SP-1 Tape Looper

A tape-machine-style looper for the Teenage Engineering SP-1: four loop
tracks per song, sixteen remembered songs in four banks.

This is custom firmware that turns the SP-1 stem player into a hands-on tool
for building layered loops. You feed audio in over USB-C (the SP-1 appears on
your computer or phone as a USB sound card / speaker), record loops with the
four track buttons, and they play back layered together through the speaker or
headphones. The rocker changes playback speed and pitch together, like tape.
Everything — loops, tempo/pitch, chop and loop mode — persists per song on the
SP-1's internal flash, surviving power-off and re-flashing.

## What's new in 2.0

Version 2.0 is a ground-up expansion of the looper, developed by marc
(marcabisamra) and merged here: 16 songs, loop chop, tapped grids with
quantized recording and beatmatch, 8-minute takes, dimmable lights, a real
battery gauge, and a long list of fixes — everything is under Features
below and per release in CHANGELOG.md. chattock's original firmware (and
the dim-LED build 2.0 absorbed) remains the foundation; the classic 4-song
1.x line lives on the `v1` branch.

**Updating from 1.x is a format break** (the 2.0 index re-arranges loop
storage): export your songs as WAVs first, flash, then re-upload. The
transfer page reads the device's layout, so the one page handles 1.x and
2.0 automatically: https://chattock.github.io/sp1-tape-looper/

Build it yourself: Zephyr v4.3.1 + SDK 0.17.4, apply
zephyr-patches/uac2-windows-fs-feedback.patch to the Zephyr tree, then
`west build -p -b stem_player firmware -- -DBOARD_ROOT=$(pwd)`.
Flash: hold Track 1 + Track 4 while plugging in USB, then use
solderless.engineering with sp1_looper.bin.

## Features

What 2.0 adds on top of the 1.x looper:

- **16 songs in 4 banks**, each a fully remembered performance state — loops,
  tempo/pitch, chop window, loop mode, mutes and grid tempo all persist per
  song across power-off. Hold FUNCTION and press a track button to jump to
  that bank; press the same track again to step through its four songs — all
  16 songs under one hold.
- **Tapped grid + quantized capture** — tap FUNCTION 4+ times along any
  music and that song gets a tempo grid: the lights become a metronome, the
  bank light blinks on the beat, and the sync jack sends MIDI clock at your
  tapped tempo (even while stopped). Recording then quantizes itself: the
  first take starts the moment you arm and places the downbeat, stops snap
  to whole beats (a loop can never contain silence), and overdubs count in
  and punch exactly on the next bar line — every loop stays locked with no
  timing precision required from you.
- **Loop chop** — a live performance window over every playing track: shrink,
  grow, slide or reset it from the FUNCTION layer (see Controls).
  Non-destructive, and saved with the song.
- **Press-accurate loop capture** — the stop lands on your press-down (the
  variable release latency is gone, and the remaining constant is backdated
  out of the take), so seams land where you play them.
- **Two-layer loop-length mode** — the fixed/variable toggle now has a global
  default plus a per-song memory (see Controls).
- **Dimmable lights** — LEDs run dim by default; FUNCTION + double-tap PLAY
  toggles full brightness.
- **Battery gauge** — 1–4 LEDs while charging in standby, top LED blinking
  until the charger reports full. It appears whether you plug in an off
  device or power off while still plugged in.
- **Full-scale headphones** — the −19 dB output pad in the codec init is
  gone; max headphone volume matches the stock firmware.
- **Long takes** — up to 8 minutes per track at 1.0x (about 16 with the
  tape at half speed), on all 16 songs.
- **Transfer page 2.0** — 16-song aware, with one-click Download
  all, and WAVs round-trip at the
  pitch you actually hear on the device (fixes the upstream export-pitch
  bug): <https://chattock.github.io/sp1-tape-looper/>

Inherited from the upstream release — Technics' work, summarized (full notes
in the [upstream repo](https://github.com/chattock/sp1-tape-looper)):

- Full 48 kHz; four independent tracks per song, each looping at its own
  length; hands-free latched recording that starts on the first sound.
- Fixed and variable loop-length modes, switchable live.
- Tape-style tempo/pitch rocker: 1 BPM per click, double-click for an exact
  semitone.
- Aliasing-free recording at any tape speed, on a deeply hardened
  audio/storage engine (four tracks plus an overdub at 1.5× with zero
  dropouts).
- Works on Windows, macOS, iOS and Linux (the USB Audio 2.0 host quirk is
  auto-detected); iPhone-friendly 100 mA power claim.
- Phantom-tap-proof buttons (all five share one analogue sense line); MIDI
  clock out on the sync jack; loops survive power-off and re-flashing.

## Loop transfer tool

Move loops between your computer and the SP-1 as WAV or MP3:

### → https://chattock.github.io/sp1-tape-looper/

(Required for 16-song builds; the old 1.x page at
chattock.github.io reads at most 8 songs and stamps WAVs at a flat rate.)

Open it in Chrome or Edge with the SP-1 plugged in and powered on **normally**
(no button combo — not the bootloader mode), click Connect, and pick the
`SP-1 Audio` port. The looper switches itself into transfer mode while the page
is connected (playback pauses, the four track lights blink together) and
returns to normal when you disconnect. Download any track as a WAV, or upload a
WAV/MP3 into a chosen song and track — clips are resampled to the device's rate
(mono), and every track keeps its own length, exactly like recording on the
device.

### Connecting to phones

- **USB-C iPhone / iPad / Android:** a normal USB-C to USB-C cable works — the
  SP-1 shows up as an audio output (and the browser transfer tool works from
  Android Chrome).
- **Lightning iPhone / iPad:** a plain USB-C-to-Lightning cable does **not**
  work — Lightning only speaks USB *host* through Apple's own adapter. Use the
  Apple **Lightning to USB 3 Camera Adapter** (or "Camera Connection Kit"),
  then any USB-A/USB-C cable to the SP-1. Third-party adapters must be
  MFi-certified; most cheap ones aren't, and won't pass audio.

---

## 1. What's in this folder

```
sp1-tape-looper/
├── README.md               you are here
├── CHANGELOG.md            what each release adds/fixes
├── sp1_looper.bin          the firmware — flash this one
├── boards/                 the SP-1 board definition (makes the repo buildable)
├── zephyr-patches/         patch for the Zephyr tree (Windows USB fix)
├── docs/                   the loop-transfer web page (GitHub Pages)
└── firmware/               full source code (for reading / rebuilding)
    ├── src/
    │   ├── main.c          the whole looper: audio engine, controls, power, USB
    │   ├── sp1_emmc.c      the flash-memory driver (stores/loads the loops)
    │   └── sp1_emmc.h
    ├── app.overlay         hardware pin map (ADC channels for buttons/faders)
    ├── prj.conf            Zephyr build configuration
    ├── zephyr-patches/     small Zephyr patch needed for the Windows fix
    ├── CMakeLists.txt
    └── Kconfig
```

For normal use, flash `sp1_looper.bin` (Sections 2–3). To read or change how it
works, start with `firmware/src/main.c`, which opens with a full architecture
overview.

---

## 2. Flashing it onto the SP-1

The SP-1 is flashed with the Solderless updater (the same tool used for any
SP-1 custom firmware) — no soldering or opening the device required:

1. Open the Solderless SP-1 update tool: <https://solderless.engineering>
2. Connect the SP-1 to your computer with a USB-C cable.
3. Put the SP-1 into firmware-loading mode (hold **Track 1 + Track 4** while
   powering on) and select the `.bin` file.
4. Flash, then unplug and replug.

> Note: loops you record survive re-flashing this firmware. The first time you
> install it (coming from the stock firmware or an older release) it reformats
> the loop storage once, so anything previously on the device is cleared.
> Between 2.0-format builds, flashing preserves your songs.

---

## 3. Controls

### Turning it on and off
- The device boots into a quiet charging/standby state. Plugging in USB or
  finishing a flash does not start playback.
- Hold the FUNCTION button (the lower button) for about 0.6 s to turn it on.
- Hold FUNCTION for about 2.5 s to turn it off (the four centre LEDs fill as a
  countdown; release early to cancel).
- Powering off while plugged into USB lands in the charging gauge instead of
  going dark; unplug from there to power off fully.

### The four track buttons

| Gesture | Action |
|---|---|
| Hold (~0.2 s), then let go | **Arms and records, hands-free.** Capture begins on the first sound it hears (a count-in is never recorded), and keeps going after you release. |
| Tap the same track while it records | **Ends the take** — it starts looping immediately, exactly as long as you played it. A tap before any sound has arrived cancels the arm instead. |
| Quick tap (playing track) | Mutes / unmutes that track (content kept). |
| Double-tap | Deletes that track. |

- **Every track loops at its own length.** Takes are never stretched, padded or
  snapped to another track's length.
- **The stop is press-accurate** in 2.0: the take ends on your
  press-down rather than the release, and the button latency is backdated out
  of the recording — tap the stop exactly on the "1".
- The **first take of a song** also sets the beat grid for the LEDs and the
  MIDI clock; every take after that is free.
- Only one track records at a time. Recording onto a non-empty track replaces
  it.
- A take that reaches the per-track maximum (8 minutes at 1.0x — longer if
  the tape is slowed, since recording follows tape speed) finalizes itself.
- Because recording follows tape speed, a slowed tape stores proportionally
  less bandwidth — classic tape behavior. For full-fidelity takes, snap to
  exactly 1.0x first (FUNCTION+PLAY double-tap).

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
- **Double-click** FWD or RWD to jump a whole **semitone** (an exact musical
  pitch step) instead of 1 BPM. Keep clicking quickly to step further up or
  down the scale; the speed lands on exact semitones relative to 1.0x /
  80 BPM, so pitched material stays in key. A single press or a hold works
  exactly as before.

### Loop chop (hold FUNCTION)
- FUNCTION + FWD halves the playback window (down to 1/64th of the loop);
  FUNCTION + RWD doubles it back toward the full loop.
- FUNCTION + Vol +/− slides the window right/left. HOLD the chord and the
  slide auto-repeats, faster and faster, each step declicked — a tape scrub
  across the loop.
- FUNCTION + rocker double-click resets to the full loop.
- Non-destructive: recorded audio, loop lengths, the beat grid and the MIDI
  clock are untouched. The chop is saved with the song. In fixed mode it
  slices the shared bar across all layers together; in variable mode each
  track slices its own length.

### Lights and battery
- LEDs run dim, with the song/status row a touch brighter than the track
  row. For direct sunlight, hold **FUNCTION + PLAY through 5 seconds** —
  brightness flips right at the 5-second mark, no release needed — or use
  the transfer page's hidden settings panel (open the page with #settings).
  Both persist across power-off. The chosen brightness covers every light,
  including the charging gauge and the power-on/off sweeps.
- A muted track that HAS content glows faintly — dark = empty, faint = loaded
  but muted, pulsing = playing. No more guessing what's sleeping under a song.
- Charging in standby shows a battery gauge: 1–4 LEDs for the level, with the
  top LED blinking until the charger reports full. Mid-charge readings run
  slightly optimistic; "full" is authoritative.

### Songs and banks
- There are 16 song slots, in 4 banks of 4. Each song remembers its own
  tracks, lengths, tempo/pitch, chop window, loop mode and mutes — everything
  restores when you come back to it, even across power-off.
- Hold FUNCTION and press Track N to jump to bank N; press Track N again
  (still holding) to step through that bank's four songs, wrapping. Other
  tracks hop to their banks — all 16 songs under one FUNCTION hold.
- Tapping FUNCTION no longer changes songs — taps are TAP TEMPO now (below).
- The side LEDs show where you are with two lights: solid = position within
  the bank; blinking = which bank. One light blinking alone means both landed
  on the same LED (songs 1, 6, 11 and 16).

### Tap tempo — the grid (FUNCTION taps)

- Tap FUNCTION **4 or more times** in rhythm and the song gets a grid: tempo
  from your tap spacing, the downbeat from your **first tap** (start tapping
  on a "1"). 1–3 taps do nothing at all. More taps keep refining the tempo.
- The lights show it: on an empty song the track row runs a 1-2-3-4 metronome
  chase; on a loaded song the beat pulse follows the tapped tempo; the bank
  light blinks on the beat instead of its usual 2 Hz.
- The sync jack sends MIDI clock at the tapped tempo, even while stopped —
  tap along to your decks and the SP-1 clocks your gear to them.
- Hold FUNCTION for ~1 s right after tapping to **clear** the grid (that hold
  can never power the device off).
- **Recording on a gridded song is quantized.** The FIRST take starts the
  moment your arm-hold completes and places the song's downbeat — your
  loop is the "1". Stops snap to the last whole beat (instantly; the spare
  sliver stays on flash unplayed, so a loop can never contain silence) —
  stop within the last ~15% of a beat and it runs those few milliseconds
  to the line, with the REC light double-blinking. OVERDUBS arm any time
  and punch in exactly on the next bar line (the armed light fast-blinks
  the count-in); their stops snap to beat multiples of the same base, so
  every loop stays locked. A second tap during any count-out trims to a
  whole beat. Songs without a grid record exactly as before.
- **Tap-to-beatmatch:** once a song has loops, a fresh 4+ tap run means
  "match THIS": the tape retunes so your loops play at the tapped tempo
  (vinyl-style — pitch moves too, clamped to the 0.5–1.5x range) and the
  loops restart from their top on your tapped downbeat at the next bar —
  tempo and phase matched in one gesture. FUNCTION + double-tap PLAY snaps
  back to exactly 1.0x: tap to match, double-tap to come home.
- The grid's tempo is remembered per song across power-off; its downbeat
  re-anchors to your next tap run (or to your first take). Bars are fixed
  4/4; accepted range is roughly 50–200 BPM.

### Loop-length mode (FUNCTION + PLAY)
- Hold **FUNCTION and PLAY together** and release — any time between ~1/3 s
  and 5 s, either finger first — to toggle between **variable** and
  **fixed** loop lengths. (Holding through 5 s becomes the brightness
  toggle instead; the mode is left alone.)
- The lights confirm the new mode: **all four blink together twice = fixed**
  (every overdub snaps to a whole multiple of track 1, locked in sync); **a
  1→4 sweep = variable** (every take keeps its own length and free-runs).
- The mode only affects the *next* take you record — tracks you've already laid
  down keep their lengths, so you can freely mix free-running and locked loops
  in one song. It's remembered across power-off.
- In 2.0 the toggle is two-layer: on an **empty** song it sets your
  global default (all empty songs follow it); on a **recorded** song it
  changes only that song. A song's first take stamps the mode it was recorded
  in, and deleting all its tracks returns it to the global default.

### Headphones
- Plug headphones into the headphone jack (the one nearest the headphone
  symbol, not the second/sync jack). The speaker mutes automatically while
  headphones are connected and returns when you unplug them.
- Headphone output runs at full scale in 2.0 (the old −19 dB pad
  was removed) — mind your ears at maximum volume.

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

- Tracks are mono, 16-bit, 48 kHz. Mono (rather than the stock player's
  stereo) is a storage-bandwidth choice.
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
fixed region on the flash (blocks 0–1 hold the song index, then four
track regions per song, for 16 songs) and its own length in that header.
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
against a custom board definition for the SP-1 (it ships in this repo under
`boards/` — the build command above uses it via
`BOARD_ROOT`). With a Zephyr workspace and the
nRF SDK set up, point a `west build` at the `firmware/` folder, then convert
the resulting ELF to a bootloader-friendly `.bin` (the application is linked to
load at `0x20000`).

One extra step: the Windows compatibility fix lives partly in Zephyr itself.
Apply `firmware/zephyr-patches/uac2-windows-fs-feedback.patch` to your Zephyr
tree (it adds the `USBD_UAC2_FS_WINDOWS_WORKAROUND` option that `prj.conf`
turns on — recent upstream Zephyr already has an equivalent option). If you'd
rather build without it, delete that line from `prj.conf`; the device then
works everywhere except Windows, as before.

Build knobs:

- `SP1_XFER_ENABLE` in `firmware/src/main.c` — `1` includes the loop-transfer
  mode (as the shipped build does); `0` compiles it out entirely.

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
