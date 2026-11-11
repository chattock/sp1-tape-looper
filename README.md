# SP-1 Tape Looper 4.0

A hands-on tape looper for the Teenage Engineering SP-1. Four tracks, sixteen songs, unlimited takes, Bluetooth recording and eight rocker modes for shaping your loops.

Record from USB or Bluetooth, layer tracks, add effects and manipulate playback like tape. Save recordings on the device and manage them through Finder or the browser Library.

**[3D Interactive Guide](https://tapelooper.club/#guide) · [Printable Guide](docs/sp1-4.0-reference.pdf) · [Pocket Guide](docs/sp1-4.0-pocket-guide.pdf) · [Live Companion](https://tapelooper.club/#live) · [Library](https://tapelooper.club/#transfer) · [Setup v4](https://tapelooper.club/#setup)**
**[Changelog](CHANGELOG.md)**

## What's new in v4

- **Unlimited takes per track.** Keep earlier recordings and switch between takes without replacing them.
- **Bluetooth recording.** Stream audio from your phone or computer. USB and Bluetooth inputs can be mixed.
- **USB Library.** Add, replace, delete and download loops through Finder or the browser. Batch imports, automatic audio conversion and a waveform clip picker for long files.
- **Eight rocker modes.** Pitch, Wind, Bender, Battery, Sandpaper, Magnet, Scissors/glue and Manual tape head.
- **More effects and tape color.** Flanger, Slow gear, Lo-fi, Radio, Reverb Freeze, compression, expanded noise types and separate Wow/Flutter choices.
- **More timing control.** Playback up to **2×**, per-track trim and slide, and **3 / 4 / 5 / 7 beats per bar** options for grids.
- **More responsive controls.** Improved recording and take switching, safer overdub gestures, clearer LED feedback and recovery gestures for stuck controls.
- **Updated website.** Interactive 3D walkthroughs, live device feedback, guided setup and printable v4 guides.

See the [changelog](CHANGELOG.md) for all changes and gestures since 3.1.1.

## Install

Install everything from [Setup v4](https://tapelooper.club/#setup). No separate download or flashing tool is needed.

1. Open [Setup v4](https://tapelooper.club/#setup) in desktop Chrome or Edge and connect your SP-1.
2. Select **Install v4**. When asked, turn the SP-1 off, hold **T1 + T4** while connecting USB and release when T1 lights up.
3. Keep USB connected. Setup installs the firmware, restarts the SP-1, then checks the filesystem and Bluetooth firmware, skipping work that is already complete.

Existing recordings and storage are kept. If your computer switches its audio output to SP-1, select your usual speakers or headphones during setup.

**Prefer to flash yourself?** Download the BIN from [Releases](https://github.com/chattock/sp1-tape-looper/releases/tag/v4.0.0-official) and flash it with [Solderless](https://solderless.engineering). Then open [Setup v4](https://tapelooper.club/#setup) to finish the filesystem and Bluetooth steps.

## Learn the controls

The [3D Interactive Guide](https://tapelooper.club/#guide) demonstrates the gestures on an animated SP-1. The [Live Companion](https://tapelooper.club/#live) shows your connected device's tracks, takes, effects and timing.

Printable guides:

- [Full guide](docs/sp1-4.0-reference.pdf)
- [Pocket guide](docs/sp1-4.0-pocket-guide.pdf)

Print the pocket guide double-sided, flip on the short edge and fold in three.

**FN** means FUNCTION. **T1–T4** are the track buttons; **F1–F4** are their faders.

### Start here

The core of Tape Looper really just comes down to a few concepts:

- Recording tracks **(tap TRACK btn start/stop)**
- Chopping **(FN + ROCKER)**
- Speed + pitch **(ROCKER)**
- Adding FX **(HOLD FN + TRACK btn opens FX page, fader changes an FX, VOL-/+ switches FX pages, FN closes FX pages... the side LEDs indicate which page you are on)**
- Bouncing down the mix (everything you hear) into a take **(PLAY + TRACK btn start, TRACK btn stop)**

Also there are 4 banks of 4 songs each, you can cycle each bank with **(FN + TRACK btn tap)**. Each of the 4 tracks on each song allows for unlimited takes **(TRACK btn + ROCKER UP for a new take, TRACK btn + ROCKER DOWN to cycle through all of that track's takes)**

Faders use pickup: move through a control's saved value before changing it.

## Bluetooth

Tap FN 3 times and hold the last press until all four track lights fade in and out.

Choose the SP-1 in your phone or computer's Bluetooth audio settings, then record normally.

After restarting the SP-1, enable Bluetooth again and select it on your source device to reconnect. Bluetooth audio pairing and the [Live Companion](https://tapelooper.club/#live)'s Bluetooth connection are separate.

This release supports **Bluetooth audio input**. Sending audio to Bluetooth headphones or speakers is not included.

> **Bluetooth development note**
> An SP-1 Bluetooth SDK and accompanying agent skill are currently in development to share integration code, protocol documentation, build instructions and tested examples.
>
> Bluetooth support uses Infineon/Cypress components with separate license terms. Vendor SDK files and programming/radio payloads are not included in the public source package. The release BIN and browser Setup use compiled Bluetooth components; the public source package is not a self-contained build of every release component.

## Transfer loops

Stop playback and let saving finish. Hold **T2 + T3 for three seconds**, then release.

The **SP1 MUSIC** drive appears on your computer.

- **Finder / File Explorer:** open the drive and manage the song, track and take folders.
- **[Browser Library](https://tapelooper.club/#transfer):** select the **SP1 MUSIC drive itself** in the folder picker.

The Library prepares supported audio automatically. Recognized song/track filenames can be placed automatically; other files let you choose their destinations. Long files can be clipped before importing.

Use **Download all** to export your library.

When finished, eject **SP1 MUSIC** in Finder or File Explorer. Keep the SP-1 powered and USB-connected while it finishes importing. Tape Looper returns automatically; larger batches take longer.

## FX

Eight control pages cover effects, tape color and recording controls:

| Page | Controls |
|---|---|
| **1 · Color** | Filter, Chorus / Flanger, Distortion, Gate / Slow gear |
| **2 · Texture** | Crush / Lo-fi / Radio, Ring mod, Auto-wah, Echo |
| **3 · Motion & space** | Phaser, Sweep, Tremolo, Reverb / Freeze |
| **4 · Tape** | Drive, Tone / Compression, Noise, Wobble |
| **5 · EQ** | Four-band equalizer |
| **6 · Placement** | Per-track stereo placement |
| **7 · Timing** | Grid offset, take length, beats per bar and track timing |
| **8 · Next take** | Mono / stereo recording |

Hold **FN + TRACK** to open a page. Release both, then use **VOL− / VOL+** to navigate. Tap **FN** to close.

Faders adjust amounts; track buttons cycle the available types or divisions. On Page 4, tap **T2** to select Tone or Compression, then adjust **F2**. Both settings remain applied.

Effects stay set when you leave a page. The [3D Interactive Guide](https://tapelooper.club/#guide) covers each control and its secondary gestures; the [Live Companion](https://tapelooper.club/#live) shows your current settings.

## Rocker modes

| Mode | What it does |
|---|---|
| **Pitch** | Changes playback speed and pitch together, up to 2×. |
| **Wind** | Winds forward or backward, with momentary or latched speed choices. |
| **Bender** | Drags or surges the tape. |
| **Battery** | Adds sag and stalls. |
| **Sandpaper** | Adds tape wear. |
| **Magnet** | Makes undoable gaps without shortening the loop. |
| **Scissors / glue** | Cuts sections out and joins the remaining tape. |
| **Manual tape head** | Plays the tape through momentary contact and fader-controlled heads. |

Magnet and Scissors can affect the loaded tracks together, or one track using TRACK + rocker while the rocker page is open.

**Hold FN and double-tap PLAY** to restore the tape, including Magnet gaps and Scissors cuts, while keeping effects.

See the [3D Interactive Guide](https://tapelooper.club/#guide) for each mode's gestures.

## Audio

- Mono or stereo recording, selected per take.
- Internal recording rate: 24 kHz; output: 48 kHz stereo.
- Approximately eight minutes maximum per take at normal speed.
- Four tracks per song, sixteen songs across four banks.
- Recordings remain on the device after power-off.

Imported audio is converted to the device's recording format where needed.

## If controls seem stuck

Hold **FN + T1 + T2 for two seconds** to reset performance controls without deleting recordings.

This clears control latches, effects, timing offsets, trims and mutes. A waiting recording arm is cancelled; an active recording is finalized.

To return to firmware-loading mode, hold **T1 + T4 for three seconds**. If normal controls are unresponsive, follow the recovery procedure in [Solderless](https://solderless.engineering).

Avoid interrupting storage or Bluetooth updates. If Setup reports a problem, keep its recovery data and use **Help & diagnostics** to save a report.

## Reporting an issue

Include:

- Firmware version.
- Computer OS and browser.
- What you were doing immediately before the problem.
- The LED pattern.
- Diagnostics from Setup or the Library activity log.

Mention whether you were using USB audio, Bluetooth, the Companion or drive mode. Don't include recordings or private backups unless you intend to share them.

## Source and licenses

Tape Looper's original code is released under the MIT License. Third-party components retain their own licenses and notices in [LICENSES](LICENSES).

[Build instructions](BUILDING.md) identify the source layout and external inputs. The public source package does not include proprietary vendor inputs or private device data. Browser audio-decoder notices and matching source accompany the [website](https://tapelooper.club/licenses/) and release downloads.

## Credits

Built on chattock's Tape Looper and the SP-1 hardware research and board support documented by Tim Knapen's [SP-1-dev](https://github.com/timknapen/SP-1-dev).

Thanks to [Solderless](https://solderless.engineering), [Zephyr](https://www.zephyrproject.org/), and everyone in the SP-1 community who tested builds, shared findings and helped improve the release.

## Disclaimer

Unofficial community firmware, not affiliated with or endorsed by Teenage Engineering. Use at your own risk and back up recordings you need.
