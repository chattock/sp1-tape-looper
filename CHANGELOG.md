# Changelog

All notable changes in this fork of Technics' sp1-tape-looper
(chattock on GitHub).
Base: upstream commit c60941c (2026-07-14).

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
