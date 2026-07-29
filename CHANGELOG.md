# Changelog

All notable changes to the SP-1 Tape Looper.
Versions 1.0.0-1.2.4 below were the 2.0 development line (developed as a
fork by marc, never announced) — kept for the honest record. The classic
4-song firmware lives on the `v1` branch. Base: 1.x at commit c60941c.

## [2.5.0] - 2026-07-30

Timing truth. luuuciano reported that loops recorded over a track he was
mixing never quite sat on it and drifted apart from each other. A
click-track bench — 120.000 BPM over USB, recorded, exported and measured
per sample — cleared the engine (clicks landed 24000 samples apart with
zero in-loop drift) and found five separate causes. All are fixed here.
An eight-beat take now comes back as exactly 192000 samples: eight beats
to the sample, with 0.02 ms of drift across the loop. The same series
measured 7.94, 7.77, 7.02 and 6.94 beats before.

### Fixed
- **Loop lengths rounded once, not per beat.** Lengths live in whole flash
  blocks and the code rounded EACH BEAT before multiplying: at 120 BPM a
  beat is 93.75 blocks, forced to 94, so even a perfect tap recorded a loop
  that played at 119.68 — 1.33 ms per beat, compounding every lap, about
  160 ms of slide per minute. The metronome runs off the unquantized grid
  clock, which is exactly why the lights looked locked while the audio
  slid. The whole take is rounded once now, and later takes reference the
  base, so lengths track the true tempo AND stay exact multiples of each
  other.
- **A freshly tapped grid is the truth.** The first take used to re-anchor
  the song's downbeat to wherever recording happened to start, discarding
  the phase you had just tapped in against the music (bench: 21–82 ms off).
  When a grid was tapped this session, the first take now punches ON it
  like an overdub. Songs whose grid came from a first take keep the classic
  feel: your take IS the "1".
- **Stops round to the nearest beat.** First takes trimmed back past a 16%
  window, so releasing a hair early lost a whole beat — an intended 8-beat
  loop became 7 and phased against its 8-beat siblings forever. The window
  is half a beat now, matching overdubs.
- **Punch on the beat, not the bar, and schedule from the press.** The
  count-in waited up to a bar (~2 s). Worse, a 100 ms hold filter ran
  before the punch was scheduled, so pressing exactly ON the beat was the
  worst possible moment: the line had passed, and the take waited a full
  beat. Punches now key off the instant your finger lands.
- **Tap tempo is timed on the PRESS, not the release.** A tap cannot be
  classified until the button comes up, but the moment it names is the
  press — so the grid was being planted late by however long your finger
  rested on the button, tens of milliseconds and different every tap. The
  beat spacing also comes from the sample clock now instead of being
  rounded through a millisecond counter and back from BPM.
- **The estimator no longer runs forever.** `tempo_finish()` is the only
  thing that clears the onset estimator's active flag and the gridded stop
  path never called it, so since 1.0 it kept sampling through every later
  overdub for an answer nobody reads.

### Added
- **The first take teaches the tempo.** The onset estimator has run through
  every first take since 1.0; on tapped-grid songs its answer was thrown
  away. It now REFINES the tapped beat from the audio just recorded — a
  coarse pass over ~4 beats, then a fine pass over up to 24 counted with
  the coarse result as the ruler, plus a subdivision sanity check.
  Accepted only within 5% of the tap; the grid, metronome and MIDI clock
  retune with it. Human tapping is 0.2–1% off and that error is what walks
  a loop off its source — the recording knows better. Ambiguous or
  non-percussive material keeps the tapped value.
- **Pre-roll: a punch pressed late still starts on the beat.** The record
  ring sat empty whenever nothing was capturing; it now holds the input at
  all times, so the machine always remembers what it just heard. An armed
  punch takes the NEAREST grid line — press before it and it waits as
  before, press just after and the take begins on the line you passed, its
  head adopted straight out of the ring with the length, playhead and
  overdub phase anchor all backdated to match. Reach is a quarter beat,
  capped at 120 ms: half a beat was tried first and was too generous, since
  a press 250 ms after a line usually MEANT the next line, and reaching
  back made the take a beat too long. The ring fills on empty songs too, so
  the very first take of a song can reach back. Costs zero RAM — the buffer
  already existed.

### Changed
- The any-time grid clear is far easier to hit: tap FUNCTION, then press
  again and hold ~1 s (a double-tap where you hold the second press). The
  window went from 1.5 s to 3 s — the gesture was real but nearly
  impossible to land.

### Known limits
- The downbeat itself is still placed by your hand, and hands anticipate a
  beat they are following by a few tens of milliseconds. Invisible playing
  alone, since every track punches on the same grid; it does offset the
  MIDI clock against the music by that much. Measured and understood.
- The MIDI clock's tick interval is a truncating divide, so it runs
  slightly fast and accumulates against external gear over a long session.
  Both of these are next.

## [2.4.0] - 2026-07-27

The resample release — heads mode learns to print.

### Added
- The BOUNCE: in heads mode, hold an EMPTY track (~0.4 s) and the whole
  heads performance — positions, directions, per-head levels and mutes,
  the window, reversed or not — prints into it as ONE loop. It is not a
  recording: it's an offline render of exactly one audible cycle, so the
  seam is perfect by construction and the print lands IN PHASE with its
  source. The destination fast-blinks while it prints (a few seconds),
  then heads mode exits by itself with the new loop playing — you hear
  exactly what you just built. Print it, re-head it, print again: the
  resample spiral. (Live filter sweeps and tape speed are NOT baked in —
  they stay live tools on top of the print, like on any track.)
- Heads on ANY track: the triple-tap engages on the lowest playing
  unmuted track, and holding any LOADED track inside heads mode makes IT
  the tape (your head positions and directions carry over).
- You can see what's printable: a head with nothing underneath chases at
  a faint ghost intensity — faint = empty = a bounce target; full bright
  = loaded = a source you can grab.

### Changed
- Heads-mode mutes are performance state now: entering heads un-mutes all
  four heads (a muted track no longer sits silent in the canon), taps
  mute freely without ever touching the song's saved mutes, and the
  original mutes return exactly on exit.
- The muted-track ghost glow is a touch fainter in dim mode, so muted
  reads clearly darker than playing; full-brightness mode is unchanged.

## [2.3.1] - 2026-07-26

### Fixed
- Pocket-proof power-on: waking from full power-off used to skip the hold
  check entirely — one accidental press in a bag switched the device fully
  on ("playing breakbeats in your pants", community report) and drained
  the battery. Every power-on now requires the same deliberate hold,
  lengthened to a stock-like ~1.5 s, with the side row filling as a
  countdown (the power-off animation, mirrored). Releasing early drops
  straight back to off; an accidental blip costs milliseconds and shows
  one dim LED for an instant. Power-off is unchanged (~2.5 s, countdown).
  Watchdog/crash recovery still boots directly, preserving forensics.

## [2.3.0] - 2026-07-26

The fader release — while FUNCTION is held, the four faders become a
performance surface. Everything here is session-state: songs on flash are
never touched, and letting go of FUNCTION always returns the faders to
volume duty without a jump.

### Added
- Heads mode scrub: hold FUNCTION and the faders move the four heads'
  positions on the loop, live — all four, track 1's own phase included.
  Grabbing jumps the head to the fader (it's a scrub); each step is
  declicked per head; the light chase follows.
- Heads mode reverse: double-tap a track and that head plays BACKWARD
  (the gesture was free — delete is blocked in heads mode). Double-tap
  again for forward; everything resets forward at heads entry; the light
  chase walks backward with the audio.
- Window faders: hold FUNCTION outside heads mode — fader 1 = window
  START, fader 2 = END, free fractions of the chop period at any width
  (down to a ~1/128 sliver), not just the stepped halves; fader 3 =
  SHIFT (width and direction kept). CROSS start past end and the window
  plays in REVERSE. Any chop button returns to the stepped window.
- DJ filter: FUNCTION + fader 4 (outside heads mode) — center = clean,
  slide down = low-pass sweep (to ~80 Hz), slide up = high-pass sweep
  (to ~4.5 kHz). Sits on the master mix, so it colors everything,
  including the live input. Latches where you leave it; neutral at
  power-on.
- Chop length glide: holding FUNCTION+FWD/RWD now auto-repeats the
  halve/double — steady declicked steps, full range in ~2.3 s.

### Fixed
- FUNCTION+PLAY triple-tap (the heads toggle) registers reliably: fast
  taps no longer merge, unhurried triples still count, and a missed
  triple can no longer flip the fixed/variable loop mode. The double-tap
  1.0x snap now waits out the triple window (~0.6 s), so a completed
  triple never touches the tape speed — heads engage at the speed you
  are playing.

## [2.2.0] - 2026-07-25

### Added
- Heads mode: with track 1 playing, hold FUNCTION and TRIPLE-tap PLAY —
  tracks 2-4 stop playing their own loops and become three extra tape heads
  on track 1's loop, offset by quarters of its cycle. One loop in four
  phases, like a canon: each head has its own fader and mute, the four
  lights chase in sequence, and chop moves all four heads at once. Playback
  only — recording and delete are blocked while it's on (taps still mute);
  triple-tap again to exit and tracks 2-4 return untouched. Switching songs
  or powering off also exits (like speed, mutes and chop, every song opens
  as itself). The second tap of the triple is still the 1.0x snap, so heads
  always enter at native speed.

### Changed
- Songs without a grid: each playing light now pulses when ITS OWN loop
  wraps (chop-aware) instead of all four pulsing one shared beat —
  different-length loops paint their polyrhythm on the panel; long loops
  flash ~2 beats at each wrap. Gridded songs keep the shared grid pulse
  and metronome chase — except in heads mode, where the canon chase takes
  over on gridded songs too.

## [2.1.0] - 2026-07-24

### Added
- Hold FUNCTION+Vol to glide the chop window: the shift auto-repeats after
  ~450 ms, accelerating from ~250 ms to ~125 ms per step, each step
  declicked — a tape-scrub across the loop on the existing buttons.

### Fixed
- Chop edits no longer click: a short declick envelope (dip ~5 ms, recover
  ~27 ms) masks the window-jump discontinuity — steps, halve/double and
  double-click reset alike.
- The power-off pop is gone: the mix fades over ~85 ms, then both output
  stages are soft-muted (CS42L42 headphone mute, TAS2505 Class-D mute)
  before power-down.

### Notes
- Recording follows tape speed (since 2.0): a slowed tape stores
  proportionally less bandwidth — classic tape behavior. Snap to exactly
  1.0x (FUNCTION+PLAY double-tap) before critical takes for full fidelity.

## [2.0.0] - 2026-07-23

The big one — 2.0 is everything the development line built, merged into the
official repo as one release.

### Added
- 16 songs in 4 banks of 4 (was 4). FUNCTION+Track N jumps to bank N; the
  same track again (still held) steps through the bank. Song row: solid =
  position, blinking = bank.
- Per-song memory: loops, tape speed/pitch, chop window, loop-length mode
  and mutes all persist across power-off.
- 8-minute takes: 643-beat regions (8:00 at 1.0x; one beat = 35,840
  samples; 44 MB per track, 76.4% of the 7,553,024-block card, ~912 MB
  spare). Recording follows tape speed — a half-speed tape holds ~16 min.
- Loop chop: FUNCTION+FWD/RWD halves/doubles the playing window,
  FUNCTION+Vol shifts it, rocker double-click resets. Per song, persistent.
- Tapped grids: 4+ FUNCTION-taps set tempo (50-200 BPM) and downbeat;
  lights become a metronome; grid persists per song. Quantized capture
  (instant first punch, bar-line overdub punch-in with count-in, stops
  snapped to whole beats), tap-to-beatmatch (0.5-1.5x vinyl-style) with
  FUNCTION+PLAY double-tap 1.0x snap, MIDI clock out on the sync jack.
- Dim-by-default lights (merged from chattock's dim build, tuned two-row)
  with FUNCTION+PLAY 5 s hold OR a hidden #settings page switch for full
  brightness; ghost glow distinguishes muted-with-content from empty.
- Charge-standby battery gauge (smoothed, hysteresis); powering off while
  plugged lands in the gauge instead of going dark.
- Transfer page 2.0: 16-song aware, Download all, speed-compensated WAV
  round-trip, serves 1.x and 2.0 devices from the same URL.
- In-repo board definition — the repo builds with stock Zephyr v4.3.1 +
  SDK 0.17.4, one west command.

### Fixed
- Headphone/line output pad (-19 dB) removed — full-scale output.
- Loop seams: stops act on the press; release latency no longer smears
  loop lengths.
- Export pitch: sub-80-BPM songs no longer export fast/high.
- Torn-index: the song index writes magic-last, surviving power cuts.
- A press that stops a take can no longer silently re-arm a re-record.
- Double-tap delete on gridded songs: a development-line trim of the
  gridded re-record hold (120 ms) sat inside the real 50-150 ms tap band
  and could turn a delete tap into an armed re-record — the hold is the
  full 180 ms everywhere again (empty tracks keep the instant arm).

### BREAKING — export your songs before flashing
- New on-flash layout (magic 'S816'). First boot reformats loop storage:
  on your CURRENT firmware, export everything (Download all), flash, then
  re-upload on the same page. Grids survive; tape speeds reset to 1.0x
  (songs still sound the same — uploads are rate-compensated). Max take
  for 1.x owners changes ~10 min -> 8:00 (slow the tape for more).

## [1.2.4] - 2026-07-23

### Added
- Powering off while plugged into USB now lands in the charging gauge —
  the same standby you get plugging in an off device — instead of going
  dark until a replug. (SYSTEM_OFF with VBUS already high has no wake
  edge, so the firmware soft-resets into charge-standby instead.)
  Unplugging from that gauge powers fully off; power-off on battery is
  unchanged.

## [1.2.3] - 2026-07-23

### Fixed
- The brightness setting now covers every light the device shows. The
  charging gauge, the power-ON sweep and the FUNCTION-hold power-off
  sequence all ran dim regardless of the saved mode, because they lit
  before the index holding the setting had been read. The index reader now
  starts ahead of standby and the saved mode is applied before each of
  those moments (including button wake-ups, which skip standby entirely).

## [1.2.2] - 2026-07-23

### Added
- The on-device brightness toggle returns, without spending a new gesture:
  hold FUNCTION+PLAY through 5 seconds and dim/full flips on the spot (no
  release needed). The transfer page's #settings switch remains; both
  persist.

### Changed
- The fixed/variable mode toggle now fires on RELEASE of the FUNCTION+PLAY
  chord (350 ms tap-filter up to 5 s, either finger first — previously only
  a PLAY-first release fired it, so the natural both-together release
  silently aborted). Release detection is ladder-debounced so a stray dip
  can't restart the 5 s clock.
- Dim levels tuned: track row 52 us (a hair below the classic 60); the
  song/status row runs its own 66 us window (slightly brighter side lights).

## [1.2.1] - 2026-07-23

### Fixed
- Battery gauge flicker while charging near a level boundary: the display
  took one raw ADC sample per pass with no smoothing or hysteresis, so ADC
  noise plus charger ripple could flip the level many times a second and
  strobe the boundary LED between off and blinking. The reading is now
  averaged over ~10 passes and the level only moves once the average clears
  a threshold by a +/-18-count margin.

## [1.2.0] - 2026-07-23

### Added
- Launch-quantized mutes: on gridded songs, mute/unmute waits for the next
  bar line (fast-blinking while pending; tap again to cancel) — layer moves
  land exactly on the "1", Ableton-style. Ungridded songs mute instantly.
- Tap-to-beatmatch: a tap run over existing loops retunes the tape to the
  tapped tempo (clamped 0.5-1.5x, vinyl-style pitch) and restarts the loops
  on the tapped downbeat at the next bar — tempo and phase matched in one
  gesture. Works from a tapped grid or a first-take-derived tempo.
- FUNCTION + double-tap PLAY now snaps the tape to exactly 1.0x / 80 BPM —
  "tap to match, double-tap to come home". (This replaces the dim toggle.)
- Transfer page: hidden settings panel (open with #settings) with a full-LED-
  brightness switch for direct sunlight; the firmware adopts the setting at
  transfer commit. The device itself is always-dim now.
- Tapped grid (phase 1): tap FUNCTION 4+ times to give a song a tempo grid —
  first tap = downbeat, lights become a metronome, the bank light blinks on
  the beat, MIDI clock locks to the tapped tempo (runs while stopped). Tempo
  persists per song in a self-validating flash-block-2 extension (no format
  break; the transfer page is unaffected). Hold FUNCTION after tapping to
  clear. Groundwork for quantized capture (phase 2).

- Quantized capture on gridded songs: the first take punches in the moment
  you arm and places the downbeat; stops snap to the last whole beat (no
  silence in loops, ever; near-miss stops run on to the line with a
  double-blink cue); overdubs count in (fast-blinking armed light) and punch
  exactly on the next bar line, lengths snapping to shared beat multiples.
  Songs without a grid record exactly as before.

### Fixed
- A long, firm stop press could silently re-arm a re-record that overwrote
  the loop it had just captured (the stop fires at press-down since the
  perfect-loop work, so the finger is still on the button while the take
  flushes). A press that stops a take is now spent — arming requires a
  fresh press. Latent since v1.0.0 for long stop presses with live input.

### Changed
- Navigation: FUNCTION-tap no longer advances songs. Hold FUNCTION and press
  a track to jump to its bank; press the same track again to step through
  that bank's songs. FN-taps are the tap-tempo surface (1-3 taps are inert).
- Re-record hold on gridded songs trimmed 180 ms -> 120 ms (the punch waits
  for the grid anyway; the shorter hold only shrinks the felt constant).
- Stopped transport: tracks with content now read solid instead of freezing
  dark like an empty track.

## [1.1.0] - 2026-07-22

### Added
- Ghost LED class: a muted track that has content glows faintly (one frame in
  five of the dim window), so dark still means empty and a glance shows what
  is sleeping under a song. Community request.
- Per-song mute memory: a song's muted tracks come back muted — across song
  switches, power-off and transfers. A fresh take, a delete or a site upload
  un-mutes that track. Stored in spare index bits: same SE16, no reformat.
  Note: builds older than 1.1.0 misread the loop-mode stamp on songs with
  saved mutes — flash forward only.

## [1.0.0] - 2026-07-21

### Added
- 16 song slots in 4 banks (up from 4 songs). FUNCTION cycles banks;
  FUNCTION + Track N jumps straight to bank N (power-off-safe). Song shown
  with two lights: position solid, bank blinking 2 Hz.
- Loop regions up to ~5 minutes at 1.0x.
- Per-song memory: tape speed (upstream) is now joined by the chop window
  and the fixed/variable loop mode. Mode is two-layer: a global preference
  plus a per-song stamp - empty songs inherit the global, the first take
  stamps the song, delete-to-empty unstamps.
- Global loop chop: FUNCTION+FWD/RWD halves/doubles the playback window,
  FUNCTION+Vol shifts it, rocker double-click resets. Non-destructive.
- Perfect-loop capture: the first-take stop fires on the press-commit edge
  (~24 ms constant) instead of at release (+50-165 ms variable), and the
  constant is backdated out of the take. (A beat-snap experiment lives on
  the experiments/beat-snap branch.)
- Always-dim LEDs via soft-PWM with a zero-latency ISR; FUNCTION+PLAY
  double-tap toggles dim/full, persisted. Credit: Technics (also the
  upstream author).
- Battery gauge while charging: 1-4 LEDs from pack voltage, top LED blinks
  until the charger reports done; thresholds anchored to a measured full
  reading.
- Reconstructed board definition - the repo now builds with stock Zephyr
  v4.3.1 (see README build notes).
- Transfer page: 16-song / 2-block index support.

### Fixed
- Export pitch bug (upstream, affects all firmwares): exported WAVs are now
  stamped at the song's heard rate and uploads are inverse-compensated, so
  files sound like the device in both directions.
- Torn-index hazard: 2-block index writes are magic-last, so an interrupted
  save can never half-apply.
- Headphone max volume: removed the -19 dB codec mixer pad (~1/9th of stock
  loudness); the digital chain already soft-limits before the codec.

### Changed
- Index format is SE16 (2 blocks). FORMAT BREAK from stock SE4A: the first
  boot reformats loop storage - export songs first. Between SE16 builds,
  flashing preserves songs. Stock firmware is restorable anytime via the
  Track 1 + Track 4 bootloader.

### Known deviations
- USB VID enumerates 0x2fe3 (Zephyr v4.3.1 lacks SAMPLE_USBD_VID plumbing);
  cosmetic.
- CONFIG_PM / CONFIG_STACK_SENTINEL from prj.conf are inert on mainline
  v4.3.1 (MPU stack guard active; idle is plain WFI).

### Credits
Technics (chattock on GitHub) wrote both the upstream looper this repo
forks and the dim-LED build it merges. Also: Tim Knapen (SP-1-dev wiki),
ericlewis (sp1-midi board reference).
