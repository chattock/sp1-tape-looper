// Current field-guide reference; instructions, not device observations.
export const cards=[
  {
    "id": "connect",
    "category": 1,
    "group": "Connect & wake",
    "title": "Connect your sound",
    "gesture": "USB-C or Bluetooth audio in",
    "body": "Select SP-1 as your computer or phone’s USB audio output, or pair it over Bluetooth. Both inputs can play together.",
    "note": "The headphone jack is an output. Bluetooth audio input is separate from the companion’s Bluetooth control connection."
  },
  {
    "id": "power",
    "category": 1,
    "group": "Connect & wake",
    "title": "Wake. Power off.",
    "gesture": "Hold FN · 1.5 s / 2.5 s",
    "body": "Hold FN about 1.5 seconds to wake. To power off, stop playback and hold FN about 2.5 seconds.",
    "note": "USB power can keep the charging gauge active."
  },
  {
    "id": "first-loop",
    "category": 1,
    "group": "Your first loop",
    "title": "Make your first loop",
    "gesture": "Arm → send sound → tap to finish",
    "body": "Tap an empty track to arm it. Start your audio to begin recording. Tap the same track again to finish the loop, then raise its fader.",
    "note": "Without a beat grid, the armed track waits for sound. With a grid, it waits for the scheduled punch-in."
  },
  {
    "id": "transport",
    "category": 1,
    "group": "Everyday controls",
    "title": "Play. Stop. Restart.",
    "gesture": "Tap PLAY · hold to restart",
    "body": "Tap PLAY to start or stop. Hold PLAY, then release to restart.",
    "note": ""
  },
  {
    "id": "levels",
    "category": 1,
    "group": "Everyday controls",
    "title": "Mix four tracks",
    "gesture": "F1–F4 = track levels",
    "body": "Raise a fader to bring its track forward. Lower it to pull that track back.",
    "note": ""
  },
  {
    "id": "volume",
    "category": 1,
    "group": "Everyday controls",
    "title": "Set the listening level",
    "gesture": "VOL− / VOL+",
    "body": "Use the two round buttons on the top edge to change master volume.",
    "note": ""
  },
  {
    "id": "mute",
    "category": 1,
    "group": "Everyday controls",
    "title": "Mute. Restore.",
    "gesture": "Tap a loaded track",
    "body": "Tap a loaded track to mute it. Tap again to restore it.",
    "note": "Several track buttons can toggle their mutes together. Muting keeps the recorded audio."
  },
  {
    "id": "takes-v4",
    "category": 1,
    "group": "Everyday controls",
    "title": "Keep more than one take",
    "gesture": "TRACK + DOWN = saved takes · TRACK + UP = New",
    "body": "Hold a track and click DOWN to cycle its recorded takes. UP selects an empty New lane. Record normally there; your earlier takes stay saved. Each song and track has its own lane.",
    "note": "Switch while playing or stopped. A New lane breathes when it has saved takes. Hold FN during playback to count the total takes on each track; the counts repeat together."
  },
  {
    "id": "replace",
    "category": 1,
    "group": "Everyday controls",
    "title": "Overdub the current take",
    "gesture": "Hold TRACK · release · tap TRACK",
    "body": "Hold a loaded track for 0.6 seconds to prepare overdub. Release, then tap it within 10 seconds to arm. Tap again to finish.",
    "note": "Overdub replaces this take after saving; other takes stay intact. To keep this version, choose New before recording. Tap FN to cancel preparation. Muted input cannot start ordinary recording."
  },
  {
    "id": "delete",
    "category": 1,
    "group": "Everyday controls",
    "title": "Delete a take",
    "gesture": "Double-tap; hold the second press",
    "body": "Double-tap the track, holding the second press until deletion is acknowledged. This removes the selected take; other takes remain.",
    "note": "If the track is still saving, wait for saving to finish and try again. TRACK + DOWN finds the remaining takes."
  },
  {
    "id": "songs",
    "category": 1,
    "group": "Songs & settings",
    "title": "Find a song",
    "gesture": "Hold FN + tap T1–T4",
    "body": "Choose a bank with T1–T4. Repeat the same button to step through that bank’s four songs.",
    "note": "Use this from normal track mode. A longer FN + track hold opens a page instead."
  },
  {
    "id": "loop-mode",
    "category": 1,
    "group": "Songs & settings",
    "title": "Fixed or variable loops",
    "gesture": "Briefly hold FN + PLAY; release",
    "body": "Briefly hold FN + PLAY, then release both to switch loop length mode.",
    "note": "Fixed rounds each take to a whole multiple of the base loop, keeping tracks aligned. Variable preserves each recorded length, so tracks can cycle independently. The side lights confirm your choice."
  },
  {
    "id": "brightness",
    "category": 1,
    "group": "Songs & settings",
    "title": "Adjust LED brightness",
    "gesture": "Hold FN + PLAY · 5 s",
    "body": "Keep FN + PLAY held through five seconds. The lights change while you hold.",
    "note": "This longer hold leaves loop length mode unchanged."
  },
  {
    "id": "speed",
    "category": 2,
    "group": "Speed & pitch",
    "title": "Change speed & pitch",
    "gesture": "Rocker: click / double-click / hold",
    "body": "Click for a speed step, double-click for a semitone, or hold to sweep. Pitch follows speed.",
    "note": "Tape speed ranges from 0.25× to 2×."
  },
  {
    "id": "normal-speed",
    "category": 2,
    "group": "Speed & pitch",
    "title": "Return to 1×",
    "gesture": "Hold FN + double-tap PLAY",
    "body": "Hold FN and tap PLAY twice. The tape returns to its normal speed.",
    "note": "Also restores rocker tape transformations, including Magnet and Scissors. Recordings and effect-page settings stay intact."
  },
  {
    "id": "octave",
    "category": 2,
    "group": "Speed & pitch",
    "title": "Jump an octave",
    "gesture": "Hold FN + PLAY + rocker",
    "body": "With FN and PLAY held, click down for an octave lower or up for an octave higher.",
    "note": "A step outside the tape-speed range does nothing."
  },
  {
    "id": "chop",
    "category": 2,
    "group": "Chop & window",
    "title": "Shorten the loop window",
    "gesture": "Hold FN + rocker",
    "body": "Up halves the window. Down doubles it. Hold to keep stepping.",
    "note": "The chop stays inside your fader-set region."
  },
  {
    "id": "move-window",
    "category": 2,
    "group": "Chop & window",
    "title": "Move the window",
    "gesture": "Hold FN + VOL− / VOL+",
    "body": "Step the chop earlier or later through your region.",
    "note": "With a page open, this same chord walks pages instead."
  },
  {
    "id": "four-faders",
    "category": 2,
    "group": "Chop & window",
    "title": "One window. Four faders.",
    "gesture": "FN + F1 / F2 / F3 / F4",
    "body": "F1 = start. F2 = end. F3 = position. F4 = size / zoom.",
    "note": "Edits start from the audible window. F4 down tightens; top restores the region set by F1–F3. Re-gripping FN preserves that region."
  },
  {
    "id": "cross-edges",
    "category": 2,
    "group": "Chop & window",
    "title": "Reverse the window",
    "gesture": "FN + cross F1 past F2",
    "body": "Hold FN and move F1—the start—past the end set by F2 to reverse the window.",
    "note": "Crossing edges during a beat repeat does not reverse it."
  },
  {
    "id": "reset-window",
    "category": 2,
    "group": "Chop & window",
    "title": "Whole loop or home",
    "gesture": "FN + PLAY + VOL− / VOL+",
    "body": "VOL− resets to the whole loop, forwards. VOL+ returns home while keeping chop size and direction.",
    "note": "Use outside a repeat. Reset clears window reverse; per-track reverse stays. During repeat, the volume buttons move its window."
  },
  {
    "id": "reverse",
    "category": 2,
    "group": "Reverse or isolate",
    "title": "Reverse one track",
    "gesture": "FN + track first → tap PLAY",
    "body": "Hold FN + the track button first, then tap PLAY before its page opens. Repeat to reverse back.",
    "note": "Press order matters. FN + PLAY first, then the track, isolates instead."
  },
  {
    "id": "isolate",
    "category": 2,
    "group": "Reverse or isolate",
    "title": "Momentarily isolate",
    "gesture": "FN + PLAY first → track",
    "body": "Hold FN + PLAY first. Then press the track you want to hear alone.",
    "note": "Release the track to restore the previous mutes."
  },
  {
    "id": "heads",
    "category": 2,
    "group": "Four heads, one loop",
    "title": "Enter four-head mode",
    "gesture": "Hold FN + triple-tap PLAY",
    "body": "Start with a loop playing in normal track mode. Hold FN and triple-tap PLAY. Repeat to exit.",
    "note": "Faders set head levels, track taps mute, and FN + faders set head positions."
  },
  {
    "id": "head-source",
    "category": 2,
    "group": "Four heads, one loop",
    "title": "Move heads. Change source.",
    "gesture": "FN + faders · hold a loaded track",
    "body": "In heads mode, FN + faders positions the heads. Hold another loaded track to make it the source.",
    "note": ""
  },
  {
    "id": "grid",
    "category": 1,
    "group": "Tap a beat grid",
    "title": "Tap a beat grid",
    "gesture": "Tap FN 4+ times",
    "body": "From normal track mode, tap FN at least four times in time. Start on the downbeat.",
    "note": "Fresh taps align recording to beats. The saved grid follows the loop; tape speed changes its tempo."
  },
  {
    "id": "clear-grid",
    "category": 1,
    "group": "Tap a beat grid",
    "title": "Clear the beat grid",
    "gesture": "Tap FN → press again and hold · ~1 s",
    "body": "Tap FN, then quickly press it again and hold about one second to clear the beat grid.",
    "note": "Your recorded audio stays in place."
  },
  {
    "id": "round-grid",
    "category": 1,
    "group": "Tap a beat grid",
    "title": "Round to the nearest whole BPM",
    "gesture": "Tap FN 4+ times → hold FN · ~1 s",
    "body": "Tap FN at least four times in time, then press and hold FN about one second to round the tapped tempo to the nearest whole BPM. For example: 119.8 to 120 BPM.",
    "note": "Sweep = rounding succeeded. Shrug = no rounding applied; the tempo stays unchanged."
  },
  {
    "id": "open-page",
    "category": 3,
    "group": "Pages & pickup",
    "title": "Open an effects page",
    "gesture": "Hold FN + a track button",
    "body": "Hold FN with a track button. Release both after the page opens. The page stays open.",
    "note": "T1–T4 opens Pages 1–4. Tap FN to close."
  },
  {
    "id": "walk-pages",
    "category": 3,
    "group": "Pages & pickup",
    "title": "Move between pages",
    "gesture": "Page open · VOL− / VOL+",
    "body": "With any page open, press VOL− or VOL+ to move between Pages 1–8. Tap FN to close.",
    "note": "The same controls move between rocker modes when the rocker page is open."
  },
  {
    "id": "pickup",
    "category": 3,
    "group": "Pages & pickup",
    "title": "Make the fader respond",
    "gesture": "Cross its stored value",
    "body": "Pickup prevents a parameter jump when you enter, leave or reset a page. Move through the stored value to take control.",
    "note": "Move through the stored value before a fader takes control. FN window edits respond to deliberate movement instead."
  },
  {
    "id": "fx-1-1",
    "category": 3,
    "group": "Page 1 / FX 1",
    "title": "Filter",
    "gesture": "F1 · T1 bypass",
    "body": "Move F1 for the filter. Center is neutral. Tap T1 to turn it off.",
    "note": ""
  },
  {
    "id": "fx-1-2",
    "category": 3,
    "group": "Page 1 / FX 1",
    "title": "Chorus",
    "gesture": "F2 · T2 dry",
    "body": "Move F2 for chorus. Tap T2 to turn it off.",
    "note": "Tap a rate with FN + T2, at least twice."
  },
  {
    "id": "fx-1-3",
    "category": 3,
    "group": "Page 1 / FX 1",
    "title": "Drive",
    "gesture": "F3 · T3 cycles type",
    "body": "Tap T3 to cycle soft → hard → fold. F3 sets the amount. Lower F3 fully to turn it off.",
    "note": "Fader down gives clean audio; raising it restores the selected drive type."
  },
  {
    "id": "fx-1-4",
    "category": 3,
    "group": "Page 1 / FX 1",
    "title": "Gate",
    "gesture": "F4 · T4 cycles pattern",
    "body": "Tap T4 to cycle gallop → halves. F4 steps through rhythmic subdivisions; bottom turns the gate off.",
    "note": "Tap a rate with FN + T4, at least twice."
  },
  {
    "id": "fx-2-1",
    "category": 3,
    "group": "Page 2 / FX 2",
    "title": "Bitcrush",
    "gesture": "F1 · T1 off",
    "body": "Move F1 for bitcrush. Tap T1 to turn it off.",
    "note": ""
  },
  {
    "id": "fx-2-2",
    "category": 3,
    "group": "Page 2 / FX 2",
    "title": "Ring mod",
    "gesture": "F2 · T2 off",
    "body": "Move F2 for ring modulation. Tap T2 to turn it off.",
    "note": ""
  },
  {
    "id": "fx-2-3",
    "category": 3,
    "group": "Page 2 / FX 2",
    "title": "Auto-wah",
    "gesture": "F3 · T3 off",
    "body": "Move F3 for auto-wah. Tap T3 to turn it off.",
    "note": "Hold T3 and move F3 for relative resonance: down sharper, up softer."
  },
  {
    "id": "fx-2-4",
    "category": 3,
    "group": "Page 2 / FX 2",
    "title": "Echo",
    "gesture": "F4 · T4 cycles division",
    "body": "Tap T4 to cycle 1/16 → dotted → 1/8. F4 sets the amount. Lower F4 fully to turn it off.",
    "note": "Hold T4 and move F4 for feedback. Tap a rate with FN + T4, at least twice."
  },
  {
    "id": "fx-3-1",
    "category": 3,
    "group": "Page 3 / FX 3",
    "title": "Phaser",
    "gesture": "F1 · T1 cycles division",
    "body": "Tap T1 to cycle beat → half → quarter. F1 sets the amount. Lower F1 fully to turn it off.",
    "note": "Tap a rate with FN + T1, at least twice."
  },
  {
    "id": "fx-3-2",
    "category": 3,
    "group": "Page 3 / FX 3",
    "title": "Sweep",
    "gesture": "F2 · T2 cycles division",
    "body": "Tap T2 to cycle beat → half → quarter. F2 sets the amount. Lower F2 fully to turn it off.",
    "note": "Tap a rate with FN + T2, at least twice."
  },
  {
    "id": "fx-3-3",
    "category": 3,
    "group": "Page 3 / FX 3",
    "title": "Tremolo",
    "gesture": "F3 · T3 cycles division",
    "body": "Tap T3 to cycle beat → half → quarter. F3 sets the amount. Lower F3 fully to turn it off.",
    "note": "Hold T3 and move F3 for shape; hold T3 and move F4 for stereo spread. Tap a rate with FN + T3."
  },
  {
    "id": "fx-3-4",
    "category": 3,
    "group": "Page 3 / FX 3",
    "title": "Reverb",
    "gesture": "F4 · T4 dry",
    "body": "F4 controls wet amount and decay. Tap T4 to turn it off.",
    "note": "Hold T4 and move F4 for damping: dark to bright. Echo and reverb can run together, echo into reverb."
  },
  {
    "id": "fx-4-1",
    "category": 3,
    "group": "Page 4 / Tape",
    "title": "Tape drive",
    "gesture": "F1 · T1 off",
    "body": "Move F1 for tape drive. Tap T1 to turn it off.",
    "note": ""
  },
  {
    "id": "fx-4-2",
    "category": 3,
    "group": "Page 4 / Tape",
    "title": "Tape tone",
    "gesture": "F2 · T2 flat",
    "body": "Move F2 for tape tone. Center is neutral. Tap T2 for flat.",
    "note": ""
  },
  {
    "id": "fx-4-3",
    "category": 3,
    "group": "Page 4 / Tape",
    "title": "Noise",
    "gesture": "F3 · T3 off",
    "body": "F3 up adds hiss; down adds vinyl crackle. Center is off. Tap T3 to turn it off.",
    "note": ""
  },
  {
    "id": "fx-4-4",
    "category": 3,
    "group": "Page 4 / Tape",
    "title": "Wobble",
    "gesture": "F4 · T4 off",
    "body": "Move F4 for tape wobble. Tap T4 to turn it off and clear its tapped rate.",
    "note": "Tap a rate with FN + T4, at least twice."
  },
  {
    "id": "tap-rate",
    "category": 3,
    "group": "Pages & pickup",
    "title": "Tap or clear an FX rate",
    "gesture": "Hold FN + tap the lane 2+ times",
    "body": "On its page, hold FN and tap the rate-enabled lane at least twice.",
    "note": "Clear a tapped rate on its page with FN + PLAY + that track. Supported lanes: P1 T2/T4; P2 T4; P3 T1/T2/T3; P4 T4."
  },
  {
    "id": "secondaries",
    "category": 3,
    "group": "Secondary controls",
    "title": "Hold a lane. Shape it.",
    "gesture": "Hold track + move fader",
    "body": "P2: T3 + F3 = wah resonance; T4 + F4 = echo feedback. P3: T3 + F3 = tremolo shape; T3 + F4 = stereo spread; T4 + F4 = reverb damping.",
    "note": "Move before releasing the track button to avoid its tap action. After release, cross the main value again before changing it."
  },
  {
    "id": "eq",
    "category": 3,
    "group": "Page 5 / Global EQ",
    "title": "Shape the whole mix",
    "gesture": "F1 100 Hz · F2 350 Hz · F3 1.6 kHz · F4 4 kHz",
    "body": "Down cuts; up boosts. Center or a track-button tap returns that band to flat.",
    "note": "F1 and F4 are low/high shelves. EQ always affects the whole mix; lights show cut/boost amount."
  },
  {
    "id": "routing",
    "category": 3,
    "group": "FX routing & reset",
    "title": "Choose what gets FX",
    "gesture": "PLAY + VOL− / VOL+ / double-click",
    "body": "VOL− = INPUT. VOL+ = TRACKS. Double-click either volume button with PLAY held = BOTH.",
    "note": "INPUT records FX into ordinary takes. TRACKS leaves live input dry. BOTH processes the heard mix; ordinary takes stay dry. Set routing before a repeat."
  },
  {
    "id": "monitor",
    "category": 3,
    "group": "FX routing & reset",
    "title": "Mute the input monitor",
    "gesture": "Hold PLAY + both VOL",
    "body": "Toggle monitoring with PLAY and both volume buttons. Works on any page.",
    "note": "Muted monitoring blocks ordinary recording. Unmute input before arming. On Pages 1–4, a blinking page number also marks muted monitoring."
  },
  {
    "id": "reset-fx",
    "category": 3,
    "group": "FX routing & reset",
    "title": "Reset the effects",
    "gesture": "On an FX page: FN + T1 + T4",
    "body": "Restore the effects to their defaults, flatten EQ and set routing to BOTH.",
    "note": "Resets FX on every page. On Page 6 it also centers placement. The four TRACK lights briefly turn solid."
  },
  {
    "id": "placement",
    "category": 4,
    "group": "Page 6 / Stereo placement",
    "title": "Stereo track placement",
    "gesture": "Faders 1–4 = tracks 1–4",
    "body": "Down moves left. Up moves right. Center keeps the original balance.",
    "note": "A mono take pans; a stereo take leans. Lights are dark at center and brighter farther to either side."
  },
  {
    "id": "center-placement",
    "category": 4,
    "group": "Page 6 / Stereo placement",
    "title": "Center one or all tracks",
    "gesture": "Tap a track button · FN + T1 + T4",
    "body": "With no take recording, tap a track to center its placement. FN + T1 + T4 centers all four on Page 6.",
    "note": "Placement saves per song after about two seconds. Replacing or deleting a take keeps placement. A bounce prints the placed mix."
  },
  {
    "id": "offset",
    "category": 4,
    "group": "Page 7 / Take & timing",
    "title": "Move the downbeat",
    "gesture": "F1 = grid offset",
    "body": "Center is no offset. Up moves later; down moves earlier, by up to about half a beat.",
    "note": "Beat lights and future grid-aligned punches follow the offset."
  },
  {
    "id": "take-length",
    "category": 4,
    "group": "Page 7 / Take & timing",
    "title": "Choose a take length",
    "gesture": "F2 = OFF / 1 / 2 / 4 / 8",
    "body": "Move F2 from bottom to top to select the preset length.",
    "note": "Variable mode counts bars and needs a grid. Fixed mode counts base loops and needs a first take."
  },
  {
    "id": "preset",
    "category": 4,
    "group": "Page 7 / Take & timing",
    "title": "Choose a length. Record a take.",
    "gesture": "Leave Page 7 → arm a track",
    "body": "On Page 7, choose a take length with F2. Tap FN to close the page, tap an empty track and let recording finish automatically.",
    "note": "The take stops automatically. Tap the recording track to finish earlier. Bounces do not use the preset."
  },
  {
    "id": "countdown",
    "category": 4,
    "group": "Page 7 / Take & timing",
    "title": "Read the take countdown",
    "gesture": "Open Page 7 during a preset take",
    "body": "Up to four track lights show the units remaining. The last unit blinks.",
    "note": "The countdown is capped at four lights; an eight-unit take stays at four until only three units remain."
  },
  {
    "id": "nudge",
    "category": 4,
    "group": "Page 7 / Nudge a loop",
    "title": "Slide one track in time",
    "gesture": "Hold a track button + move its fader",
    "body": "On Page 7, hold a track button and move its fader. Up moves the track later; down moves it earlier. The edit is relative to the fader position when you held the button.",
    "note": "Nudge works without a grid. A recording track cannot be nudged. In heads mode, nudging the source moves all its heads."
  },
  {
    "id": "nudge-lights",
    "category": 4,
    "group": "Page 7 / Nudge a loop",
    "title": "Read the nudge lights",
    "gesture": "Amount while held · dim glow after",
    "body": "While a track is held, its light shows the nudge amount. After release, nudged tracks glow dim between beats.",
    "note": "The 1–2–3–4 chase shows the grid. Settings save per song after about two seconds."
  },
  {
    "id": "reset-timing",
    "category": 4,
    "group": "Page 7 / Nudge a loop",
    "title": "Reset timing controls",
    "gesture": "Hold FN + PLAY · press VOL−",
    "body": "With no take recording, tap a track button to center its nudge. If already centered, T1 clears grid offset and T2 switches take length off.",
    "note": "FN + PLAY + VOL− resets the window, timing and track trims. FN + T1 + T4 resets FX."
  },
  {
    "id": "format",
    "category": 4,
    "group": "Page 8 / Next take format",
    "title": "Choose mono or stereo",
    "gesture": "Tap a track button on Page 8",
    "body": "Before recording, tap a track button to choose the format of its next take.",
    "note": "Solid light = mono. Blinking = stereo. Existing audio stays unchanged. Faders still control track levels."
  },
  {
    "id": "catch",
    "category": 5,
    "group": "Catch & resize",
    "title": "Catch, reshape, release",
    "gesture": "Hold PLAY + rocker UP",
    "body": "Hold PLAY and click the rocker up to catch a beat. Use the rocker to resize it, then release PLAY to return to the song.",
    "note": ""
  },
  {
    "id": "resize-repeat",
    "category": 5,
    "group": "Catch & resize",
    "title": "Make it shorter or longer",
    "gesture": "Hold PLAY → click rocker UP → resize",
    "body": "Hold PLAY and click the rocker up to catch a beat. UP halves the repeat; DOWN doubles it. Hold the rocker to keep stepping. Release PLAY to return to the song.",
    "note": "Release the rocker but keep PLAY held to repeat at the same length."
  },
  {
    "id": "repeat-chop",
    "category": 5,
    "group": "Catch & resize",
    "title": "Repeat inside a chop",
    "gesture": "PLAY + rocker inside your chop",
    "body": "The repeat only uses your chopped audio. It cannot reach outside the audible cycle.",
    "note": "If a requested beat would fill the chop: UP starts with a quarter of it; DOWN with half, down to the minimum size."
  },
  {
    "id": "release-repeat",
    "category": 5,
    "group": "Catch & resize",
    "title": "Release: hold the tape",
    "gesture": "Release PLAY with FN released",
    "body": "On a gridded song, playback resumes from the repeat, with the tape shift rounded to the nearest bar.",
    "note": "A short stutter may leave the position unchanged. Without a grid, or during a bounce, release returns to the running tape position."
  },
  {
    "id": "repeat-no-grid",
    "category": 5,
    "group": "Catch & resize",
    "title": "Repeat without a grid",
    "gesture": "Hold PLAY + rocker: UP = 1/8 · DOWN = 1/4",
    "body": "Without a grid, hold PLAY and click the rocker up for one eighth of the loop, or down for one quarter. Chop limits still apply.",
    "note": "On release, return to the running tape position. There is no bar-based tape hold."
  },
  {
    "id": "move-repeat",
    "category": 5,
    "group": "Shape the repeat",
    "title": "Move the repeating window",
    "gesture": "Keep PLAY held + VOL− / VOL+",
    "body": "VOL− moves earlier. VOL+ moves later. Each press shifts by one current window length inside the audible cycle.",
    "note": "During a repeat these controls move its window, not FX routing. Set routing before catching the repeat."
  },
  {
    "id": "repeat-octave",
    "category": 5,
    "group": "Shape the repeat",
    "title": "Jump an octave in the repeat",
    "gesture": "Keep PLAY held + FN + rocker",
    "body": "Add FN and click the rocker. Down is an octave lower; up is an octave higher.",
    "note": ""
  },
  {
    "id": "repeat-faders",
    "category": 5,
    "group": "Shape the repeat",
    "title": "Shape the repeat with faders",
    "gesture": "Keep PLAY held + FN + F1–F4",
    "body": "F1 = start. F2 = end. F3 = position. F4 = size / zoom. Move deliberately to engage a fader.",
    "note": "F3 keeps width. F4 tightens around the center; top restores the region set by F1–F3. Re-gripping FN preserves that region."
  },
  {
    "id": "wah-resonance",
    "category": 3,
    "group": "Secondary controls",
    "title": "Wah resonance",
    "gesture": "Page 2: hold T3 and move F3",
    "body": "While holding T3, move F3 down for a sharper peak or up for a softer peak.",
    "note": "Move before releasing the track button; releasing without movement triggers its usual tap action. Then cross the main value to regain control."
  },
  {
    "id": "echo-feedback",
    "category": 3,
    "group": "Secondary controls",
    "title": "Echo feedback",
    "gesture": "Page 2: hold T4 and move F4",
    "body": "Change the amount fed back into the echo.",
    "note": "Move before releasing the track button; releasing without movement triggers its usual tap action. Then cross the main value to regain control."
  },
  {
    "id": "tremolo-shape",
    "category": 3,
    "group": "Secondary controls",
    "title": "Tremolo shape",
    "gesture": "Page 3: hold T3 and move F3",
    "body": "Move from softer to harder tremolo shapes.",
    "note": "Move before releasing the track button; releasing without movement triggers its usual tap action. Then cross the main value to regain control."
  },
  {
    "id": "tremolo-spread",
    "category": 3,
    "group": "Secondary controls",
    "title": "Tremolo stereo spread",
    "gesture": "Page 3: hold T3 and move F4",
    "body": "Change the spread between the two channels. F4 does not change reverb while T3 is held.",
    "note": "Move before releasing the track button; releasing without movement triggers its usual tap action. Then cross the main value to regain control."
  },
  {
    "id": "reverb-damping",
    "category": 3,
    "group": "Secondary controls",
    "title": "Reverb damping",
    "gesture": "Page 3: hold T4 and move F4",
    "body": "Down is darker; up is brighter.",
    "note": "Move before releasing the track button; releasing without movement triggers its usual tap action. Then cross the main value to regain control."
  },
  {
    "id": "track-lights",
    "category": 1,
    "group": "Everyday controls",
    "title": "Read the track lights",
    "gesture": "Dark · dim · blinking · solid",
    "body": "Dark means empty; dim means muted. Recording is solid. Playback pulses with the grid, or when each loop wraps.",
    "note": "When stopped, an unmuted loaded track stays solid. Examples use 120 BPM and selected loop lengths. A refusal shrug can mean saving is still busy: wait for completion before deleting or recording again. Alternating T1+T3 / T2+T4 indicates a fault; stop testing and keep the device connected for diagnostics."
  },
  {
    "id": "status-lights",
    "category": 1,
    "group": "Songs & settings",
    "title": "Read the side lights",
    "gesture": "Playing: level · FN held: song",
    "body": "During playback, the meter fills from the bottom. Hold FN to see the song: solid = bank, blinking = position.",
    "note": "If bank and position share a light, that light blinks. With a running grid the song blink follows the beat."
  },
  {
    "id": "grid-record-lights",
    "category": 4,
    "group": "Page 7 / Take & timing",
    "title": "Waiting or finishing on the grid",
    "gesture": "Half-beat flashes",
    "body": "An armed track flashes at half-beat rate while waiting for punch-in. The same quicker pulse marks a recording finishing its beat.",
    "note": "The recording continues until its aligned stop; it is not stuck."
  },
  {
    "id": "tape-copy",
    "category": 5,
    "group": "Bounce what you hear",
    "title": "Bounce what you hear",
    "gesture": "Hold PLAY → briefly tap destination",
    "body": "Record the playing mix onto a spare track. Hold PLAY and tap the destination; tap it again to finish. This is a tape copy.",
    "note": "A loaded destination is replaced. Tape copy follows tape speed with its source. A grid may add count-out; fixed mode and chop can extend the stop to a full cycle."
  },
  {
    "id": "print-repeat",
    "category": 5,
    "group": "Bounce what you hear",
    "title": "Bounce a beat-repeat performance",
    "gesture": "Hold PLAY → click rocker UP → tap destination",
    "body": "Hold PLAY and click the rocker up to catch a repeat. Keep PLAY held and tap a spare track to start the bounce. Release PLAY to let the song flow back in; tap the destination again to finish.",
    "note": "Keep PLAY held to resize while recording. A loaded destination is replaced."
  },
  {
    "id": "sampler",
    "category": 5,
    "group": "Bounce what you hear",
    "title": "Make a sampler print",
    "gesture": "Hold PLAY → hold destination",
    "body": "Hold PLAY, then hold the destination track to record a sampler print. Release the track to finish.",
    "note": "The heard speed is baked for playback at 1×. Use a spare destination: a loaded track is replaced."
  },
  {
    "id": "stop-page",
    "category": 5,
    "group": "Bounce what you hear",
    "title": "Stop a bounce from a page",
    "gesture": "PLAY + destination track",
    "body": "Use PLAY + the destination track again to stop a bounce while a page is open.",
    "note": ""
  },
  {
    "id": "flow-back",
    "category": 5,
    "group": "Bounce what you hear",
    "title": "Let the song flow back in",
    "gesture": "After starting the copy, release PLAY",
    "body": "The repeat ends and the tape copy continues from the running tape position. Tap the destination again to stop.",
    "note": ""
  },
  {
    "id": "print-speed",
    "category": 5,
    "group": "Bounce what you hear",
    "title": "Sweep speed into a take",
    "gesture": "During a bounce: release PLAY → rocker",
    "body": "Use the rocker while a tape copy or sampler print is recording. Your speed moves become part of the performance.",
    "note": "Tape copy records speed moves relative to starting speed. Sampler print bakes the performance for playback at 1×."
  },
  {
    "id": "held-print",
    "category": 5,
    "group": "Bounce what you hear",
    "title": "Perform inside a sampler print",
    "gesture": "Keep the destination held · PLAY changes the rocker",
    "body": "Keep the destination held. With PLAY released, the rocker changes speed. With PLAY held, it controls beat repeat. Add FN for octave jumps.",
    "note": "Release the destination to end the print. Release PLAY too to end the repeat."
  },
  {
    "id": "scissors386",
    "category": 2,
    "group": "Rocker modes",
    "title": "Scissors / glue",
    "gesture": "Slot 7 · DOWN marks, DOWN cuts · UP undoes",
    "body": "While playing, tap DOWN at the start and again at the end of a section to skip it. With the rocker page open, hold the same TRACK for both marks to cut only that track. Bare DOWN targets the loaded tracks.",
    "note": "Make up to eight cuts. UP cancels a pending mark or undoes the last cut. Cuts remain when leaving the rocker page or slot. FN + double-tap PLAY restores the tape. Cuts are non-destructive session edits; changing songs, takes, recording or rebooting clears them. Use full forward loops, without trim, Heads, repeat or recording."
  },
  {
    "id": "bluetooth-v4",
    "category": 1,
    "group": "Connect & wake",
    "title": "Record from Bluetooth",
    "gesture": "Tap FN twice, then hold the third press",
    "body": "Tap FN twice, then hold FN on the third press for about one second to enable Bluetooth audio discovery. Pair SP-1 from your phone or computer, select it as the audio output, then record normally.",
    "note": "The gesture also works during playback and USB audio. USB and Bluetooth input mix together. This release focuses on Bluetooth input; outgoing audio to speakers or keyboards is deferred."
  },
  {
    "id": "rocker-v4",
    "category": 2,
    "group": "Rocker modes",
    "title": "Choose a rocker mode",
    "gesture": "FN + both VOL · VOL− / VOL+ to choose",
    "body": "Hold FN and both volume buttons to open the rocker page. Release FN, then use VOL− / VOL+ to select a mode. Tap FN to close; the selected mode stays active.",
    "note": "1 Pitch · 2 Wind · 3 Bender · 4 Battery · 5 Sandpaper · 6 Magnet · 7 Scissors / glue · 8 Manual tape head. Keep the rocker page open for per-track Magnet or Scissors gestures. Holding TRACK previews the normal track lights."
  },
  {
    "id": "wind-v4",
    "category": 2,
    "group": "Rocker modes",
    "title": "Wind",
    "gesture": "Slot 2 · hold UP / DOWN",
    "body": "Hold UP to fast-forward or DOWN to rewind. Release to let the tape settle back.",
    "note": ""
  },
  {
    "id": "bender-v4",
    "category": 2,
    "group": "Rocker modes",
    "title": "Bender",
    "gesture": "Slot 3 · hold UP / DOWN",
    "body": "Hold DOWN to drag and stretch the tape. Hold UP for repeated pulling surges. Release either direction for a spring-like recovery.",
    "note": "Longer DOWN holds stretch further. This changes playback; it does not rewrite your recordings."
  },
  {
    "id": "battery-v4",
    "category": 2,
    "group": "Rocker modes",
    "title": "Battery",
    "gesture": "Slot 4 · hold DOWN / UP",
    "body": "Hold DOWN to increase battery-drain sag, wobble and stalls. Hold UP to recover toward normal.",
    "note": "The wear stays when you release. FN + double-tap PLAY restores the tape."
  },
  {
    "id": "sandpaper-v4",
    "category": 2,
    "group": "Rocker modes",
    "title": "Sandpaper",
    "gesture": "Slot 5 · hold DOWN / UP",
    "body": "Hold DOWN to add roughness, breakup and tape drag. Hold UP to reduce the wear.",
    "note": "The wear stays when you release. FN + double-tap PLAY restores the tape."
  },
  {
    "id": "magnet-v4",
    "category": 2,
    "group": "Rocker modes",
    "title": "Magnet",
    "gesture": "Slot 6 · hold DOWN to erase · UP to undo",
    "body": "Hold DOWN while playing to silence and mark the section passing under the head. Release to leave a gap without shortening the loop. Tap UP to undo. With the rocker page open, hold TRACK to target only that track.",
    "note": "Unlike Scissors, Magnet keeps the loop’s length. FN + double-tap PLAY restores all Magnet gaps. Song changes and new recordings can clear the affected temporary gaps."
  },
  {
    "id": "manual-head-v4",
    "category": 2,
    "group": "Rocker modes",
    "title": "Manual tape head",
    "gesture": "Slot 8 · hold DOWN + move faders",
    "body": "Hold UP to make contact with the running tape. Hold DOWN for four-head contact; move F1–F4 to position the heads. Release to lift contact and silence the tape.",
    "note": "PLAY starts or stops the tape. While playing, the transport keeps moving when contact is lifted. Leave this rocker mode to return to ordinary playback. This is separate from latching Heads with FN + triple-tap PLAY."
  },
  {
    "id": "restore-tape-v4",
    "category": 2,
    "group": "Rocker modes",
    "title": "Restore the tape",
    "gesture": "Hold FN + double-tap PLAY",
    "body": "Restore normal rocker tape behavior: clear Scissors cuts and Magnet changes, release wind and reset speed.",
    "note": "Your recordings and effect-page settings stay intact."
  }
];
