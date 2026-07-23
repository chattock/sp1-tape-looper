# Changelog

All notable changes in this fork of Technics' sp1-tape-looper
(chattock on GitHub).
Base: upstream commit c60941c (2026-07-14).

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
