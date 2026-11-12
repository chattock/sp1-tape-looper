export const categories = [
  {
    "id": 1,
    "title": "Start & record",
    "short": "Start & record",
    "subtitle": ""
  },
  {
    "id": 2,
    "title": "Shape the tape",
    "short": "Shape the tape",
    "subtitle": "Change speed, reshape a loop, or explore four heads."
  },
  {
    "id": 3,
    "title": "Effects",
    "short": "Effects",
    "subtitle": "Eight control pages. Every lane, gesture and route."
  },
  {
    "id": 4,
    "title": "Place & time",
    "short": "Place & time",
    "subtitle": "Stereo placement, take length, grid offset and nudge."
  },
  {
    "id": 5,
    "title": "Repeat & bounce",
    "short": "Repeat & bounce",
    "subtitle": "Catch a moment, then capture what you make of it."
  }
];
export const cards = [
  {
    "id": "connect",
    "category": 1,
    "group": "Connect & wake",
    "title": "Audio inputs",
    "gesture": "USB-C or Bluetooth audio in",
    "body": "Select SP-1 as your computer or phone’s USB audio output, or pair it over Bluetooth. Both inputs can play together.",
    "note": "The headphone jack is an output. Bluetooth audio input is separate from the companion’s Bluetooth control connection.",
    "steps": [
      {
        "label": "Audio enters through USB-C",
        "detail": "Select SP-1 as your host’s USB audio output. Bluetooth audio can be paired separately.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "ports",
        "lights": [],
        "pattern": null,
        "duration": 3
      },
      {
        "label": "Listen through headphones",
        "detail": "Or use the built-in speaker.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "ports",
        "lights": [],
        "pattern": null,
        "duration": 3
      }
    ]
  },
  {
    "id": "power",
    "category": 1,
    "group": "Connect & wake",
    "title": "Power",
    "gesture": "Hold FN · 1.5 s / 2.5 s",
    "body": "Hold FN about 1.5 seconds to wake. To power off, stop playback and hold FN about 2.5 seconds.",
    "note": "USB power can keep the charging gauge active.",
    "steps": [
      {
        "label": "Hold FN to wake",
        "detail": "The side row fills during the 1.5-second hold. Track lights stay dark.",
        "hold": [
          "FN"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [],
        "pattern": "wake-hold",
        "duration": 0.75
      },
      {
        "label": "Release FN",
        "detail": "After release and startup, the side row fills briefly, then clears.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": "power-on",
        "duration": 2.5
      },
      {
        "label": "To power off, stop playback first",
        "detail": "Keep FN held: side lights fill over 2.5 seconds, then clear. Track lights stay dark.",
        "hold": [
          "FN"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [],
        "pattern": "shutdown-hold",
        "duration": 1.65
      },
      {
        "label": "Release FN",
        "detail": "The player is off. On battery, every light stays dark.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": null,
        "duration": 2.5
      },
      {
        "label": "While charging over USB",
        "detail": "Example: three levels of charge. The last lit side LED blinks while charging.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": "charging",
        "duration": 2.5
      }
    ]
  },
  {
    "id": "first-loop",
    "category": 1,
    "group": "Your first loop",
    "title": "Recording",
    "gesture": "Arm → send sound → tap to finish",
    "body": "Tap an empty track to arm it. Start your audio to begin recording. Tap the same track again to finish the loop, then raise its fader.",
    "note": "While armed, tap PLAY to cancel and pause. Once recording starts, tap its track to finish.",
    "steps": [
      {
        "label": "Tap T1 to arm it",
        "detail": "Use an empty track for your first recording.",
        "hold": [],
        "tap": [
          "T1"
        ],
        "faders": {},
        "rock": 0,
        "view": "tracks",
        "lights": [],
        "pattern": null,
        "duration": 0.5,
        "context": {
          "playing": false,
          "loaded": [],
          "grid": false
        }
      },
      {
        "label": "Blinking means ready to record",
        "detail": "T1 is armed and waiting for sound.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "trackLights",
        "lights": [],
        "pattern": "arm",
        "duration": 2.5,
        "context": {
          "playing": false,
          "loaded": [],
          "grid": false
        }
      },
      {
        "label": "Start your audio",
        "detail": "Recording starts when input sound arrives. T1’s light turns solid.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [
          "T1"
        ],
        "pattern": null,
        "duration": 3,
        "context": {
          "playing": false,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Tap T1 to finish",
        "detail": "The new take becomes a loop.",
        "hold": [],
        "tap": [
          "T1"
        ],
        "faders": {},
        "rock": 0,
        "view": "tracks",
        "lights": [],
        "pattern": "arm",
        "duration": 2.5,
        "beatSeconds": 2,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Raise fader 1",
        "detail": "Bring the new track into your mix.",
        "hold": [],
        "tap": [],
        "faders": {
          "1": 1
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": "arm",
        "duration": 2.5,
        "beatSeconds": 2,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      }
    ]
  },
  {
    "id": "transport",
    "category": 1,
    "group": "Everyday controls",
    "title": "Playback controls",
    "gesture": "Tap PLAY · hold to restart",
    "body": "Tap PLAY to start or stop. Hold PLAY, then release to restart.",
    "note": "",
    "steps": [
      {
        "label": "Tap PLAY",
        "detail": "Start or stop playback.",
        "hold": [],
        "tap": [
          "PLAY"
        ],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [],
        "pattern": null,
        "duration": 2,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true
        }
      },
      {
        "label": "Release",
        "detail": "Start or stop playback.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [],
        "pattern": null,
        "duration": 1.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true
        }
      },
      {
        "label": "Hold PLAY for a restart",
        "detail": "Keep PLAY held, then release to restart from the beginning.",
        "hold": [
          "PLAY"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [],
        "pattern": null,
        "duration": 0.4,
        "context": {
          "playing": false,
          "loaded": [
            1
          ],
          "grid": true
        }
      },
      {
        "label": "Release PLAY",
        "detail": "Playback restarts.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true
        }
      }
    ]
  },
  {
    "id": "levels",
    "category": 1,
    "group": "Everyday controls",
    "title": "Track levels",
    "gesture": "F1–F4 = track levels",
    "body": "Raise a fader to bring its track forward. Lower it to pull that track back.",
    "note": "",
    "steps": [
      {
        "label": "Find fader 1",
        "detail": "Faders 1–4 always correspond to tracks 1–4 in the normal mix.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": null,
        "duration": 1.6,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2,
            3,
            4
          ],
          "grid": false
        }
      },
      {
        "label": "Lower track 1",
        "detail": "Lower F1 to turn track 1 down.",
        "hold": [],
        "tap": [],
        "faders": {
          "1": 0.12
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2,
            3,
            4
          ],
          "grid": false
        }
      },
      {
        "label": "Move back up",
        "detail": "Raise F1 to bring track 1 back into the mix.",
        "hold": [],
        "tap": [],
        "faders": {
          "1": 0.9
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2,
            3,
            4
          ],
          "grid": false
        }
      },
      {
        "label": "Release",
        "detail": "",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": null,
        "duration": 1.4,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2,
            3,
            4
          ],
          "grid": false
        }
      }
    ]
  },
  {
    "id": "volume",
    "category": 1,
    "group": "Everyday controls",
    "title": "Master volume",
    "gesture": "VOL− / VOL+",
    "body": "Use the two round buttons on the top edge to change master volume.",
    "note": "",
    "steps": [
      {
        "label": "Press VOL+",
        "detail": "Raise the output level.",
        "hold": [],
        "tap": [
          "VOL+"
        ],
        "faders": {},
        "rock": 0,
        "view": "top",
        "lights": [],
        "pattern": null,
        "duration": 2,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Release",
        "detail": "The new listening level stays set.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "top",
        "lights": [],
        "pattern": null,
        "duration": 1.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Press VOL−",
        "detail": "Lower the output level.",
        "hold": [],
        "tap": [
          "VOL−"
        ],
        "faders": {},
        "rock": 0,
        "view": "top",
        "lights": [],
        "pattern": null,
        "duration": 2,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Release",
        "detail": "The new listening level stays set.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "top",
        "lights": [],
        "pattern": null,
        "duration": 1.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      }
    ]
  },
  {
    "id": "mute",
    "category": 1,
    "group": "Everyday controls",
    "title": "Track mute",
    "gesture": "Tap a loaded track",
    "body": "Tap a loaded track to mute it. Tap again to restore it.",
    "note": "Several track buttons can toggle their mutes together. Muting keeps the recorded audio.",
    "steps": [
      {
        "label": "Tap T1",
        "detail": "Mute its loaded take.",
        "hold": [],
        "tap": [
          "T1"
        ],
        "faders": {},
        "rock": 0,
        "view": "tracks",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false,
          "muted": [
            1
          ]
        }
      },
      {
        "label": "The muted track stays loaded",
        "detail": "Its light becomes a dim ghost.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "trackLights",
        "lights": [],
        "pattern": "muted",
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false,
          "muted": [
            1
          ]
        }
      },
      {
        "label": "Tap T1 again",
        "detail": "Restore the track.",
        "hold": [],
        "tap": [
          "T1"
        ],
        "faders": {},
        "rock": 0,
        "view": "tracks",
        "lights": [],
        "pattern": "arm",
        "duration": 2.5,
        "beatSeconds": 2,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false,
          "muted": []
        }
      }
    ]
  },
  {
    "id": "takes-v4",
    "category": 1,
    "group": "Everyday controls",
    "title": "Unlimited takes",
    "gesture": "TRACK + ROCKER DOWN = saved takes · TRACK + ROCKER UP = New",
    "body": "Hold a track and click ROCKER DOWN to cycle its recorded takes. ROCKER UP selects an empty New lane. Record normally there; your earlier takes stay saved. Each song and track has its own lane. Takes are limited by available storage.",
    "note": "Switch while playing or stopped. A New lane breathes when it has saved takes. Hold FN during playback to count the total takes on each track; the counts repeat together.",
    "steps": [
      {
        "label": "Choose New",
        "detail": "In normal track mode, hold T1 and click ROCKER UP. Your current take stays saved.",
        "hold": [
          "T1"
        ],
        "tap": [],
        "faders": {},
        "rock": -1,
        "view": "left",
        "lights": [],
        "pattern": null,
        "duration": 3,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2
          ],
          "grid": true
        }
      },
      {
        "label": "Record another version",
        "detail": "Release both controls, then tap T1 to arm the empty lane. Tap again to finish.",
        "hold": [],
        "tap": [
          "T1"
        ],
        "faders": {},
        "rock": 0,
        "view": "tracks",
        "lights": [],
        "pattern": null,
        "duration": 3,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2
          ],
          "grid": true
        }
      },
      {
        "label": "Browse saved takes",
        "detail": "Hold T1 and click ROCKER DOWN for each take. Switching follows the running song position.",
        "hold": [
          "T1"
        ],
        "tap": [],
        "faders": {},
        "rock": 1,
        "view": "left",
        "lights": [],
        "pattern": null,
        "duration": 3,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2
          ],
          "grid": true
        }
      },
      {
        "label": "Preview the totals",
        "detail": "During playback, hold FN. Each track LED counts its total saved takes, including when on New.",
        "hold": [
          "FN"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "trackLights",
        "lights": [],
        "pattern": null,
        "duration": 3,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2
          ],
          "grid": true
        }
      }
    ],
    "since": "4.0"
  },
  {
    "id": "replace",
    "category": 1,
    "group": "Everyday controls",
    "title": "Overdub",
    "gesture": "Hold TRACK · release · tap TRACK",
    "body": "Hold a loaded track for 0.6 seconds to prepare overdub. Release, then tap it within 10 seconds to arm. Tap again to finish.",
    "note": "Overdub replaces this take after saving; other takes stay intact. To keep this version, choose New before recording. Tap FN to cancel preparation. Muted input cannot start ordinary recording.",
    "steps": [
      {
        "label": "Hold the loaded track",
        "detail": "Hold for 0.6 seconds. This prepares overdub; it does not start recording.",
        "hold": [
          "T1"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "tracks",
        "lights": [],
        "pattern": null,
        "duration": 3,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2
          ],
          "grid": true
        },
        "seconds": 1.8
      },
      {
        "label": "Release the track",
        "detail": "The confirmation window lasts 10 seconds.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "trackLights",
        "lights": [],
        "pattern": null,
        "duration": 3,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2
          ],
          "grid": true
        },
        "seconds": 1.2
      },
      {
        "label": "Tap T1 to arm",
        "detail": "Send sound, or wait for the grid punch-in.",
        "hold": [],
        "tap": [
          "T1"
        ],
        "faders": {},
        "rock": 0,
        "view": "tracks",
        "lights": [],
        "pattern": null,
        "duration": 3,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2
          ],
          "grid": true
        },
        "seconds": 3
      },
      {
        "label": "Tap T1 to finish",
        "detail": "The saved overdub replaces the selected take, keeping its place in the lane.",
        "hold": [],
        "tap": [
          "T1"
        ],
        "faders": {},
        "rock": 0,
        "view": "tracks",
        "lights": [],
        "pattern": null,
        "duration": 3,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2
          ],
          "grid": true
        },
        "seconds": 1.5
      }
    ],
    "since": "4.0"
  },
  {
    "id": "delete",
    "category": 1,
    "group": "Everyday controls",
    "title": "Take deletion",
    "gesture": "Double-tap; hold the second press",
    "body": "Double-tap the track, holding the second press until deletion is acknowledged. This removes the selected take; other takes remain.",
    "note": "If the track is still saving, wait for saving to finish and try again. TRACK + ROCKER DOWN finds the remaining takes.",
    "steps": [
      {
        "label": "Double-tap T1; hold the second press",
        "detail": "The track light goes dark when the take is deleted.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "tracks",
        "lights": [],
        "pattern": "deleteGesture",
        "duration": 2,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "The track is empty",
        "detail": "Its light goes dark.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "trackLights",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [],
          "grid": false
        }
      }
    ]
  },
  {
    "id": "songs",
    "category": 1,
    "group": "Songs & settings",
    "title": "Banks & songs",
    "gesture": "Hold FN + tap T1–T4",
    "body": "Choose a bank with T1–T4. Repeat the same button to step through that bank’s four songs.",
    "note": "Use this from normal track mode. A longer FN + track hold opens a page instead.",
    "steps": [
      {
        "label": "Hold FN",
        "detail": "Keep FN held.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [],
        "pattern": null,
        "duration": 0.6,
        "context": {
          "playing": false,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Tap T1 briefly",
        "detail": "Select bank 1. Repeat to step through its four songs.",
        "hold": [
          "FN"
        ],
        "tap": [
          "T1"
        ],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [],
        "pattern": null,
        "duration": 0.6,
        "context": {
          "playing": false,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Release the controls",
        "detail": "",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [],
        "pattern": null,
        "duration": 1.5,
        "context": {
          "playing": false,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Read the song lights",
        "detail": "With playback stopped, the side lights show the song: solid is the bank; blinking is its position.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": "song",
        "duration": 0.6,
        "context": {
          "playing": false,
          "loaded": [
            1
          ],
          "grid": false
        }
      }
    ]
  },
  {
    "id": "loop-mode",
    "category": 1,
    "group": "Songs & settings",
    "title": "Fixed & variable loops",
    "gesture": "Briefly hold FN + PLAY; release",
    "body": "Briefly hold FN + PLAY, then release both to switch loop length mode.",
    "note": "Fixed rounds each take to a whole multiple of the base loop, keeping tracks aligned. Variable preserves each recorded length, so tracks can cycle independently. The side lights confirm your choice.",
    "steps": [
      {
        "label": "Hold FN + PLAY",
        "detail": "Hold briefly, then release both to switch modes.",
        "hold": [
          "FN",
          "PLAY"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [],
        "pattern": null,
        "duration": 0.5,
        "context": {
          "playing": false,
          "loaded": [],
          "grid": false
        }
      },
      {
        "label": "Fixed: two flashes",
        "detail": "All four side lights flash together twice. Shown slowed down.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": "fixed",
        "duration": 2,
        "context": {
          "playing": false,
          "loaded": [],
          "grid": false
        },
        "slowConfirmation": {
          "scale": 2,
          "length": 0.54,
          "start": 1.8
        },
        "seconds": 4
      },
      {
        "label": "Repeat the same gesture",
        "detail": "Hold FN + PLAY, then release both to switch back.",
        "hold": [
          "FN",
          "PLAY"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [],
        "pattern": null,
        "duration": 0.5,
        "context": {
          "playing": false,
          "loaded": [],
          "grid": false
        }
      },
      {
        "label": "Variable: out and back",
        "detail": "The side lights run 1–2–3–4–3–2–1. Shown slowed down.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": "variable",
        "duration": 2,
        "context": {
          "playing": false,
          "loaded": [],
          "grid": false
        },
        "slowConfirmation": {
          "scale": 2,
          "length": 0.71,
          "start": 1.8
        },
        "seconds": 4.02
      }
    ]
  },
  {
    "id": "brightness",
    "category": 1,
    "group": "Songs & settings",
    "title": "LED brightness",
    "gesture": "Hold FN + PLAY · 5 s",
    "body": "Keep FN + PLAY held through five seconds. The lights change while you hold.",
    "note": "This longer hold leaves loop length mode unchanged.",
    "steps": [
      {
        "label": "Keep FN + PLAY held",
        "detail": "Wait through five seconds.",
        "hold": [
          "FN",
          "PLAY"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [],
        "pattern": "brightness",
        "duration": 5.2
      },
      {
        "label": "Watch the light level change",
        "detail": "The selected brightness is saved.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": "brightness",
        "duration": 3,
        "patternTimeOffset": 5,
        "loaded": [
          1
        ]
      }
    ]
  },
  {
    "id": "speed",
    "category": 2,
    "group": "Speed & pitch",
    "title": "Speed & pitch",
    "gesture": "Rocker: click / double-click / hold",
    "body": "Click for a speed step, double-click for a semitone, or hold to sweep. Pitch follows speed.",
    "note": "Tape speed ranges from 0.25× to 2×.",
    "steps": [
      {
        "label": "Click the rocker up",
        "detail": "One speed step.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "left",
        "lights": [],
        "pattern": "rockTap",
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Double-click",
        "detail": "One semitone.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "left",
        "lights": [],
        "pattern": "rockDouble",
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Hold to sweep",
        "detail": "Let go to stop changing speed.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": -1,
        "view": "left",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Release",
        "detail": "The rocker returns to center.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "left",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      }
    ]
  },
  {
    "id": "normal-speed",
    "category": 2,
    "group": "Speed & pitch",
    "title": "Speed reset",
    "gesture": "Hold FN + double-tap PLAY",
    "body": "Hold FN and tap PLAY twice. The tape returns to its normal speed.",
    "note": "Also restores rocker tape transformations, including Magnet and Scissors. Recordings and effect-page settings stay intact.",
    "steps": [
      {
        "label": "Hold FN",
        "detail": "Keep FN held for both taps.",
        "hold": [
          "FN"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [],
        "pattern": null,
        "duration": 1.2,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Double-tap PLAY",
        "detail": "Return the tape to normal speed.",
        "hold": [
          "FN"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [],
        "pattern": "playDouble",
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Release",
        "detail": "Playback returns to 1×.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      }
    ]
  },
  {
    "id": "octave",
    "category": 2,
    "group": "Speed & pitch",
    "title": "Octave jumps",
    "gesture": "Hold FN + PLAY + rocker",
    "body": "With FN and PLAY held, click down for an octave lower or up for an octave higher.",
    "note": "A step outside the tape-speed range does nothing.",
    "steps": [
      {
        "label": "Hold FN + PLAY",
        "detail": "Keep both held.",
        "hold": [
          "FN",
          "PLAY"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [],
        "pattern": null,
        "duration": 0.6,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Press the rocker down briefly",
        "detail": "1× → 0.5×: one octave down.",
        "hold": [
          "FN",
          "PLAY"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "left",
        "lights": [],
        "pattern": "rockTap",
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        },
        "rockDirection": "DOWN"
      },
      {
        "label": "Press the rocker up briefly",
        "detail": "0.5× → 1×: back up one octave.",
        "hold": [
          "FN",
          "PLAY"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "left",
        "lights": [],
        "pattern": "rockTap",
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        },
        "rockDirection": "UP"
      },
      {
        "label": "Release",
        "detail": "",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      }
    ]
  },
  {
    "id": "chop",
    "category": 2,
    "group": "Chop & window",
    "title": "Loop window length",
    "gesture": "Hold FN + rocker",
    "body": "Up halves the window. Down doubles it. Hold to keep stepping.",
    "note": "The chop stays inside your fader-set region.",
    "steps": [
      {
        "label": "Hold FN",
        "detail": "Keep it held while using the rocker.",
        "hold": [
          "FN"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Up halves",
        "detail": "Click or hold.",
        "hold": [
          "FN"
        ],
        "tap": [],
        "faders": {},
        "rock": -1,
        "view": "left",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Down doubles",
        "detail": "Click or hold.",
        "hold": [
          "FN"
        ],
        "tap": [],
        "faders": {},
        "rock": 1,
        "view": "left",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Release",
        "detail": "Your chop remains.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      }
    ]
  },
  {
    "id": "move-window",
    "category": 2,
    "group": "Chop & window",
    "title": "Loop window position",
    "gesture": "Hold FN + VOL− / VOL+",
    "body": "Step the chop earlier or later through your region.",
    "note": "With a page open, this same chord walks pages instead.",
    "steps": [
      {
        "label": "Hold FN",
        "detail": "A quarter-loop chop is already playing. Keep FN held.",
        "hold": [
          "FN"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "top",
        "lights": [],
        "pattern": null,
        "duration": 1.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Press VOL+",
        "detail": "Move the chop later. VOL− moves it earlier.",
        "hold": [
          "FN"
        ],
        "tap": [
          "VOL+"
        ],
        "faders": {},
        "rock": 0,
        "view": "top",
        "lights": [],
        "pattern": null,
        "duration": 2.8,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Release the controls",
        "detail": "",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "top",
        "lights": [],
        "pattern": null,
        "duration": 1.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      }
    ]
  },
  {
    "id": "four-faders",
    "category": 2,
    "group": "Chop & window",
    "title": "Loop window controls",
    "gesture": "FN + F1 / F2 / F3 / F4",
    "body": "F1 = start. F2 = end. F3 = position. F4 = size / zoom.",
    "note": "Edits start from the audible window. F4 down tightens; top restores the region set by F1–F3. Re-gripping FN preserves that region.",
    "steps": [
      {
        "label": "Hold FN",
        "detail": "Move deliberately to engage a window fader.",
        "hold": [
          "FN"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "F1: start · F2: end",
        "detail": "Set the two edges.",
        "hold": [
          "FN"
        ],
        "tap": [],
        "faders": {
          "1": 0.25,
          "2": 0.8
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "F3: position",
        "detail": "Slide the region without changing its width.",
        "hold": [
          "FN"
        ],
        "tap": [],
        "faders": {
          "3": 0.6
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "F4: size / zoom",
        "detail": "Down tightens around the center.",
        "hold": [
          "FN"
        ],
        "tap": [],
        "faders": {
          "4": 0.1
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "F4 back to the top",
        "detail": "Restore the region set by F1–F3.",
        "hold": [
          "FN"
        ],
        "tap": [],
        "faders": {
          "4": 1
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Release FN",
        "detail": "Cross the saved track level to resume volume control.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      }
    ]
  },
  {
    "id": "cross-edges",
    "category": 2,
    "group": "Chop & window",
    "title": "Loop window reverse",
    "gesture": "FN + cross F1 past F2",
    "body": "Hold FN and move F1 (the start) past the end set by F2 to reverse the window.",
    "note": "Crossing edges during a beat repeat does not reverse it.",
    "steps": [
      {
        "label": "Hold FN",
        "detail": "",
        "hold": [
          "FN"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Cross the edges",
        "detail": "Move F1 beyond the end set by F2.",
        "hold": [
          "FN"
        ],
        "tap": [],
        "faders": {
          "1": 0.85,
          "2": 0.3
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Release",
        "detail": "The window plays in reverse.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      }
    ]
  },
  {
    "id": "reset-window",
    "category": 2,
    "group": "Chop & window",
    "title": "Loop window reset",
    "gesture": "FN + PLAY + VOL− / VOL+",
    "body": "Hold FN + PLAY, then press VOL− for the whole loop, forwards. Press VOL+ instead to move the window home, keeping its size and direction.",
    "note": "Use outside a repeat. Reset clears window reverse; per-track reverse stays. During repeat, the volume buttons move its window.",
    "steps": [
      {
        "label": "Hold FN + PLAY",
        "detail": "A reversed quarter-loop window is already playing. Keep FN + PLAY held.",
        "hold": [
          "FN",
          "PLAY"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "top",
        "lights": [],
        "pattern": null,
        "duration": 1.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Press VOL−",
        "detail": "Whole loop, forwards.",
        "hold": [
          "FN",
          "PLAY"
        ],
        "tap": [
          "VOL−"
        ],
        "faders": {},
        "rock": 0,
        "view": "top",
        "lights": [],
        "pattern": null,
        "duration": 2.8,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Release the controls",
        "detail": "",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "top",
        "lights": [],
        "pattern": null,
        "duration": 1.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Hold FN + PLAY",
        "detail": "Use VOL+ to move a chopped section home without changing its size.",
        "hold": [
          "FN",
          "PLAY"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "top",
        "lights": [],
        "pattern": null,
        "duration": 1.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Press VOL+",
        "detail": "Home, with chop size kept.",
        "hold": [
          "FN",
          "PLAY"
        ],
        "tap": [
          "VOL+"
        ],
        "faders": {},
        "rock": 0,
        "view": "top",
        "lights": [],
        "pattern": null,
        "duration": 2.8,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Release the controls",
        "detail": "",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "top",
        "lights": [],
        "pattern": null,
        "duration": 1.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      }
    ]
  },
  {
    "id": "reverse",
    "category": 2,
    "group": "Reverse or isolate",
    "title": "Track reverse",
    "gesture": "FN + track first → tap PLAY",
    "body": "Hold FN + the track button first, then tap PLAY before its page opens. Repeat to reverse back.",
    "note": "Press order matters. FN + PLAY first, then the track, isolates instead.",
    "steps": [
      {
        "label": "FN + T1, then PLAY",
        "detail": "Tap PLAY promptly, before the page opens.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [],
        "pattern": "reverseGesture",
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Release",
        "detail": "Repeat the gesture to reverse back.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      }
    ]
  },
  {
    "id": "isolate",
    "category": 2,
    "group": "Reverse or isolate",
    "title": "Track isolation",
    "gesture": "FN + PLAY first → track",
    "body": "Hold FN + PLAY first. Then press the track you want to hear alone.",
    "note": "Release the track to restore the previous mutes.",
    "steps": [
      {
        "label": "Hold FN + PLAY first",
        "detail": "Four recorded parts are already playing. Order chooses isolate.",
        "hold": [
          "FN",
          "PLAY"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [],
        "pattern": null,
        "duration": 0.6,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2,
            3,
            4
          ],
          "grid": false
        }
      },
      {
        "label": "Add T1",
        "detail": "Hear track 1 alone.",
        "hold": [
          "FN",
          "PLAY",
          "T1"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "tracks",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2,
            3,
            4
          ],
          "grid": false,
          "muted": [
            2,
            3,
            4
          ]
        }
      },
      {
        "label": "Release T1",
        "detail": "Previous mutes return.",
        "hold": [
          "FN",
          "PLAY"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "tracks",
        "lights": [],
        "pattern": null,
        "duration": 0.5,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2,
            3,
            4
          ],
          "grid": false
        }
      },
      {
        "label": "Release",
        "detail": "",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2,
            3,
            4
          ],
          "grid": false
        }
      }
    ]
  },
  {
    "id": "heads",
    "category": 2,
    "group": "Four heads, one loop",
    "title": "Four-head mode",
    "gesture": "Hold FN + triple-tap PLAY",
    "body": "Hold FN and triple-tap PLAY while a loop plays. Hold FN and move a fader to position its head; release FN to mix head levels.",
    "note": "Faders set head levels, track taps mute, and FN + faders set head positions.",
    "steps": [
      {
        "label": "Hold FN",
        "detail": "Keep it held through three taps.",
        "hold": [
          "FN"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Triple-tap PLAY",
        "detail": "Enter or exit heads mode.",
        "hold": [
          "FN"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [],
        "pattern": "playTriple",
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Release",
        "detail": "Example: quarter-spaced heads. Empty destination heads pulse dimly.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "trackLights",
        "lights": [],
        "pattern": "heads",
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false,
          "heads": true
        }
      },
      {
        "label": "Hold FN",
        "detail": "Keep FN held to position the heads.",
        "hold": [
          "FN"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false,
          "heads": true
        }
      },
      {
        "label": "Move F2",
        "detail": "Keep FN held and move F2 to shift head 2 through the recording.",
        "hold": [
          "FN"
        ],
        "tap": [],
        "faders": {
          "2": 0.12
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false,
          "heads": true
        }
      },
      {
        "label": "Release FN",
        "detail": "Faders now control head levels again. Tap a track to mute its head.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false,
          "heads": true
        }
      }
    ]
  },
  {
    "id": "head-source",
    "category": 2,
    "group": "Four heads, one loop",
    "title": "Head positions & source",
    "gesture": "FN + faders · hold a loaded track",
    "body": "In heads mode, FN + faders positions the heads. Hold another loaded track to make it the source.",
    "note": "",
    "steps": [
      {
        "label": "Hold FN",
        "detail": "Two different recorded tracks are loaded; four-head mode is already on. FN + F2 sets its position.",
        "hold": [
          "FN"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": null,
        "duration": 1.6,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2
          ],
          "grid": false,
          "heads": true
        }
      },
      {
        "label": "Move head 2",
        "detail": "FN + F2 sets its position.",
        "hold": [
          "FN"
        ],
        "tap": [],
        "faders": {
          "2": 0.12
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2
          ],
          "grid": false,
          "heads": true
        }
      },
      {
        "label": "Move back up",
        "detail": "FN + F2 sets its position.",
        "hold": [
          "FN"
        ],
        "tap": [],
        "faders": {
          "2": 0.9
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2
          ],
          "grid": false,
          "heads": true
        }
      },
      {
        "label": "Release",
        "detail": "",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": null,
        "duration": 1.4,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2
          ],
          "grid": false,
          "heads": true
        }
      },
      {
        "label": "Hold another loaded track",
        "detail": "The other heads now read from this track.",
        "hold": [
          "T2"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "tracks",
        "lights": [],
        "pattern": null,
        "duration": 0.5,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2
          ],
          "grid": false,
          "heads": true
        }
      },
      {
        "label": "Release",
        "detail": "All four heads now use that source.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2
          ],
          "grid": false,
          "heads": true
        }
      }
    ]
  },
  {
    "id": "grid",
    "category": 1,
    "group": "Tap a beat grid",
    "title": "Beat grid",
    "gesture": "Tap FN 4+ times",
    "body": "From normal track mode, tap FN at least four times in time. Start on the downbeat.",
    "note": "Fresh taps align recording to beats. The saved grid follows the loop; tape speed changes its tempo.",
    "steps": [
      {
        "label": "Tap FN in time",
        "detail": "Start on the downbeat; at least four taps.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [],
        "pattern": "gridGesture",
        "duration": 3,
        "context": {
          "playing": true,
          "loaded": [],
          "grid": true
        }
      },
      {
        "label": "Follow the beat lights",
        "detail": "Retap a loaded loop to match tempo.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "trackLights",
        "lights": [],
        "pattern": "beat",
        "duration": 3,
        "context": {
          "playing": true,
          "loaded": [],
          "grid": true
        }
      }
    ]
  },
  {
    "id": "clear-grid",
    "category": 1,
    "group": "Tap a beat grid",
    "title": "Beat grid reset",
    "gesture": "Tap FN → press again and hold · ~1 s",
    "body": "Tap FN, then quickly press it again and hold about one second to clear the beat grid.",
    "note": "Your recorded audio stays in place.",
    "steps": [
      {
        "label": "Clear the grid",
        "detail": "Tap, quickly press again, then hold about 1 second.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [],
        "pattern": "clearGrid",
        "duration": 2.5
      }
    ]
  },
  {
    "id": "round-grid",
    "category": 1,
    "group": "Tap a beat grid",
    "title": "Whole-BPM rounding",
    "gesture": "Tap FN 4+ times → hold FN · ~1 s",
    "body": "Tap FN at least four times in time, then press and hold FN about one second to round the tapped tempo to the nearest whole BPM. For example: 119.8 to 120 BPM.",
    "note": "Sweep = rounding succeeded. Shrug = no rounding applied; the tempo stays unchanged.",
    "steps": [
      {
        "label": "Tap a new tempo",
        "detail": "Tap FN at least four times in time. This example rounds 119.8 to 120 BPM.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [],
        "pattern": "gridGesture",
        "duration": 2.5
      },
      {
        "label": "Hold FN to round",
        "detail": "After tapping, press and hold FN about one second to round to the nearest whole BPM.",
        "hold": [
          "FN"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [],
        "pattern": null,
        "duration": 1.2
      },
      {
        "label": "Release",
        "detail": "",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": null,
        "duration": 2.5
      },
      {
        "label": "Sweep: rounding succeeded",
        "detail": "The track lights sweep out and back: 119.8 BPM rounded to 120 BPM. Shown slowed down.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "trackLights",
        "lights": [],
        "pattern": "snap",
        "duration": 2.5,
        "seconds": 5.072,
        "slowConfirmation": {
          "scale": 8,
          "length": 0.384,
          "start": 1.2
        }
      },
      {
        "label": "If rounding is not applied",
        "detail": "All four track lights blink together: the tempo stays unchanged. Shown slowed down.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "trackLights",
        "lights": [],
        "pattern": "no-action",
        "duration": 2.5,
        "seconds": 5.0,
        "slowConfirmation": {
          "scale": 8,
          "length": 0.16,
          "start": 1.2
        }
      }
    ]
  },
  {
    "id": "open-page",
    "category": 3,
    "group": "Pages & pickup",
    "title": "FX pages",
    "gesture": "Hold FN + a track button",
    "body": "Hold FN with a track button. Release both after the page opens. The page stays open.",
    "note": "T1–T4 opens Pages 1–4. Tap FN to close.",
    "steps": [
      {
        "label": "Hold FN + T1",
        "detail": "Keep both held until Page 1 opens.",
        "hold": [
          "FN",
          "T1"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [],
        "pattern": null,
        "duration": 0.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "The track lights announce Page 1",
        "detail": "They walk across and return to T1. Shown slowed down.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "trackLights",
        "lights": [],
        "pattern": "page-entry",
        "duration": 2.5,
        "page": 1,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        },
        "confirmationReplay": true,
        "slowConfirmation": {
          "scale": 8,
          "length": 0.25,
          "start": 1.8
        },
        "seconds": 5.0
      },
      {
        "label": "Release both",
        "detail": "The side lights identify the page.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": "page1",
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Tap FN to close",
        "detail": "Return to normal track controls.",
        "hold": [],
        "tap": [
          "FN"
        ],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "The track lights walk back out",
        "detail": "Normal track indications resume. Shown slowed down.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "trackLights",
        "lights": [],
        "pattern": "page-exit",
        "duration": 2.5,
        "page": 1,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        },
        "confirmationReplay": true,
        "slowConfirmation": {
          "scale": 8,
          "length": 0.25,
          "start": 1.8
        },
        "seconds": 5.0
      }
    ]
  },
  {
    "id": "walk-pages",
    "category": 3,
    "group": "Pages & pickup",
    "title": "FX page navigation",
    "gesture": "Hold FN + T4 → release → hold FN + tap VOL+",
    "body": "Hold FN + T4 until Page 4 opens, then release both. Hold FN again and tap VOL+ to advance, or VOL− to go back. Release FN to stay on that page.",
    "note": "From Page 4: one click = Page 5; two = Page 6; three = Page 7; four = Page 8. Tap FN to close.",
    "steps": [
      {
        "label": "Open Page 4",
        "detail": "Hold FN + T4 until Page 4 opens, then release both.",
        "hold": [
          "FN",
          "T4"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [],
        "pattern": null,
        "duration": 0.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Release both",
        "detail": "Page 4 stays open.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": "page4",
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Hold FN; click VOL+",
        "detail": "One click advances to Page 5.",
        "hold": [
          "FN"
        ],
        "tap": [
          "VOL+"
        ],
        "faders": {},
        "rock": 0,
        "view": "top",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Count five on the track row",
        "detail": "1–2–3–4, a gap, then 1. Shown slowed down.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "trackLights",
        "lights": [],
        "pattern": "page-entry",
        "duration": 2.5,
        "page": 5,
        "walk": true,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        },
        "confirmationReplay": true,
        "slowConfirmation": {
          "scale": 8,
          "length": 0.25,
          "start": 1.8
        },
        "seconds": 5.0
      },
      {
        "label": "Release FN",
        "detail": "Page 5 is four lights, then one.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": "page5",
        "duration": 4,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      }
    ]
  },
  {
    "id": "pickup",
    "category": 3,
    "group": "Pages & pickup",
    "title": "Fader pickup",
    "gesture": "Cross its stored value",
    "body": "Pickup prevents a parameter jump when you enter, leave or reset a page. Move through the stored value to take control.",
    "note": "Move through the stored value before a fader takes control. FN window edits respond to deliberate movement instead.",
    "steps": [
      {
        "label": "Find fader 1",
        "detail": "Move the fader through the stored value to take control.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": null,
        "duration": 1.6,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        },
        "page": 1
      },
      {
        "label": "Cross the stored value",
        "detail": "Move through the center to take control of the effect.",
        "hold": [],
        "tap": [],
        "faders": {
          "1": 0.12
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        },
        "page": 1
      },
      {
        "label": "Move back up",
        "detail": "The effect now follows the fader.",
        "hold": [],
        "tap": [],
        "faders": {
          "1": 0.9
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        },
        "page": 1
      },
      {
        "label": "Release",
        "detail": "",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": null,
        "duration": 1.4,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        },
        "page": 1
      }
    ]
  },
  {
    "id": "fx-1-1",
    "category": 3,
    "group": "Page 1 / FX 1",
    "title": "Filter",
    "gesture": "F1 · T1 bypass",
    "body": "Move F1 for the filter. Center is neutral. Tap T1 to turn it off.",
    "note": "",
    "steps": [
      {
        "label": "Hold FN + T1",
        "detail": "Keep both held until Page 1 opens.",
        "hold": [
          "FN",
          "T1"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": null,
        "duration": 0.6,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Release both",
        "detail": "Page 1 stays open.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": "page1",
        "duration": 1,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Page 1 is open",
        "detail": "The side lights identify Page 1.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": "page1",
        "duration": 2.5,
        "page": 1,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Find fader 1",
        "detail": "Fader 1 controls the filter. Center is neutral.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": null,
        "duration": 1.6,
        "page": 1,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Move F1",
        "detail": "Move F1 down through its stored setting to take control.",
        "hold": [],
        "tap": [],
        "faders": {
          "1": 0.12
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 1,
        "lane": 1,
        "amount": 0.76,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Move back up",
        "detail": "Move F1 up to change the sound.",
        "hold": [],
        "tap": [],
        "faders": {
          "1": 0.9
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 1,
        "lane": 1,
        "amount": 0.8,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Release",
        "detail": "",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 1.4,
        "page": 1,
        "lane": 1,
        "amount": 0.8,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Tap T1 to reset",
        "detail": "The lane light goes dark at neutral.",
        "hold": [],
        "tap": [
          "T1"
        ],
        "faders": {},
        "rock": 0,
        "view": "trackLights",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 1,
        "lane": 1,
        "amount": 0,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      }
    ]
  },
  {
    "id": "fx-1-2",
    "category": 3,
    "group": "Page 1 / FX 1",
    "title": "Chorus",
    "gesture": "F2 · T2 dry",
    "body": "Move F2 for chorus. Tap T2 to turn it off.",
    "note": "Tap a rate with FN + T2, at least twice.",
    "steps": [
      {
        "label": "Hold FN + T1",
        "detail": "Keep both held until Page 1 opens.",
        "hold": [
          "FN",
          "T1"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": null,
        "duration": 0.6,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Release both",
        "detail": "Page 1 stays open.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": "page1",
        "duration": 1,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Page 1 is open",
        "detail": "The side lights identify Page 1.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": "page1",
        "duration": 2.5,
        "page": 1,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Find fader 2",
        "detail": "Fader 2 controls chorus.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": null,
        "duration": 1.6,
        "page": 1,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Move F2",
        "detail": "Move F2 down through its stored setting to take control.",
        "hold": [],
        "tap": [],
        "faders": {
          "2": 0.12
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 1,
        "lane": 2,
        "amount": 0.12,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Move back up",
        "detail": "Move F2 up to change the sound.",
        "hold": [],
        "tap": [],
        "faders": {
          "2": 0.9
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 1,
        "lane": 2,
        "amount": 0.9,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Release",
        "detail": "",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 1.4,
        "page": 1,
        "lane": 2,
        "amount": 0.9,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Tap T2 to reset",
        "detail": "The lane light goes dark at neutral.",
        "hold": [],
        "tap": [
          "T2"
        ],
        "faders": {},
        "rock": 0,
        "view": "trackLights",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 1,
        "lane": 2,
        "amount": 0,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      }
    ]
  },
  {
    "id": "fx-1-3",
    "category": 3,
    "group": "Page 1 / FX 1",
    "title": "Drive",
    "gesture": "F3 · T3 cycles type",
    "body": "Tap T3 to cycle soft → hard → fold. F3 sets the amount. Lower F3 fully to turn it off.",
    "note": "Fader down gives clean audio; raising it restores the selected drive type.",
    "steps": [
      {
        "label": "Hold FN + T1",
        "detail": "Keep both held until Page 1 opens.",
        "hold": [
          "FN",
          "T1"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": null,
        "duration": 0.6,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Release both",
        "detail": "Page 1 stays open.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": "page1",
        "duration": 1,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Page 1 is open",
        "detail": "The side lights identify Page 1.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": "page1",
        "duration": 2.5,
        "page": 1,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Find fader 3",
        "detail": "Fader 3 controls drive.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": null,
        "duration": 1.6,
        "page": 1,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Move F3",
        "detail": "Move F3 down through its stored setting to take control.",
        "hold": [],
        "tap": [],
        "faders": {
          "3": 0.12
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 1,
        "lane": 3,
        "amount": 0.12,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Move back up",
        "detail": "Move F3 up to change the sound.",
        "hold": [],
        "tap": [],
        "faders": {
          "3": 0.9
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 1,
        "lane": 3,
        "amount": 0.9,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Release",
        "detail": "",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 1.4,
        "page": 1,
        "lane": 3,
        "amount": 0.9,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Tap T3: hard",
        "detail": "T3 confirms the selected setting. Shown slowed down.",
        "hold": [],
        "tap": [
          "T3"
        ],
        "faders": {},
        "rock": 0,
        "view": "trackLights",
        "lights": [],
        "pattern": "lane-count",
        "duration": 2.5,
        "page": 1,
        "lane": 3,
        "count": 2,
        "amount": 0.9,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        },
        "slowConfirmation": {
          "scale": 4,
          "length": 0.16,
          "start": 1.8
        },
        "seconds": 5.0
      },
      {
        "label": "Tap T3: fold",
        "detail": "T3 confirms the selected setting. Shown slowed down.",
        "hold": [],
        "tap": [
          "T3"
        ],
        "faders": {},
        "rock": 0,
        "view": "trackLights",
        "lights": [],
        "pattern": "lane-count",
        "duration": 2.5,
        "page": 1,
        "lane": 3,
        "count": 3,
        "amount": 0.9,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        },
        "slowConfirmation": {
          "scale": 4,
          "length": 0.24,
          "start": 1.8
        },
        "seconds": 5.0
      },
      {
        "label": "Tap T3: soft",
        "detail": "T3 confirms the selected setting. Shown slowed down.",
        "hold": [],
        "tap": [
          "T3"
        ],
        "faders": {},
        "rock": 0,
        "view": "trackLights",
        "lights": [],
        "pattern": "lane-count",
        "duration": 2.5,
        "page": 1,
        "lane": 3,
        "count": 1,
        "amount": 0.9,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        },
        "slowConfirmation": {
          "scale": 4,
          "length": 0.08,
          "start": 1.8
        },
        "seconds": 5.0
      },
      {
        "label": "Lower F3 to turn it off",
        "detail": "Move through the stored value and bring the fader fully down.",
        "hold": [],
        "tap": [],
        "faders": {
          "3": 0
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 1,
        "lane": 3,
        "amount": 0,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      }
    ]
  },
  {
    "id": "fx-1-4",
    "category": 3,
    "group": "Page 1 / FX 1",
    "title": "Gate",
    "gesture": "F4 · T4 cycles pattern",
    "body": "Tap T4 to cycle gallop → halves. F4 steps through rhythmic subdivisions; bottom turns the gate off.",
    "note": "Tap a rate with FN + T4, at least twice.",
    "steps": [
      {
        "label": "Hold FN + T1",
        "detail": "Keep both held until Page 1 opens.",
        "hold": [
          "FN",
          "T1"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": null,
        "duration": 0.6,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Release both",
        "detail": "Page 1 stays open.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": "page1",
        "duration": 1,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Page 1 is open",
        "detail": "The side lights identify Page 1.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": "page1",
        "duration": 2.5,
        "page": 1,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Find fader 4",
        "detail": "Fader 4 controls gate.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": null,
        "duration": 1.6,
        "page": 1,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Move F4",
        "detail": "Move F4 down through its stored setting to take control.",
        "hold": [],
        "tap": [],
        "faders": {
          "4": 0.12
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 1,
        "lane": 4,
        "amount": 0.12,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Move back up",
        "detail": "Move F4 up to change the rhythmic subdivision.",
        "hold": [],
        "tap": [],
        "faders": {
          "4": 0.9
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 1,
        "lane": 4,
        "amount": 0.9,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Release",
        "detail": "",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 1.4,
        "page": 1,
        "lane": 4,
        "amount": 0.9,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Tap T4: halves",
        "detail": "T4 confirms the selected pattern. Shown slowed down.",
        "hold": [],
        "tap": [
          "T4"
        ],
        "faders": {},
        "rock": 0,
        "view": "trackLights",
        "lights": [],
        "pattern": "lane-count",
        "duration": 2.5,
        "page": 1,
        "lane": 4,
        "count": 2,
        "amount": 0.9,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        },
        "slowConfirmation": {
          "scale": 4,
          "length": 0.16,
          "start": 1.8
        },
        "seconds": 5.0
      },
      {
        "label": "Tap T4: gallop",
        "detail": "T4 confirms the selected pattern. Shown slowed down.",
        "hold": [],
        "tap": [
          "T4"
        ],
        "faders": {},
        "rock": 0,
        "view": "trackLights",
        "lights": [],
        "pattern": "lane-count",
        "duration": 2.5,
        "page": 1,
        "lane": 4,
        "count": 1,
        "amount": 0.9,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        },
        "slowConfirmation": {
          "scale": 4,
          "length": 0.08,
          "start": 1.8
        },
        "seconds": 5.0
      },
      {
        "label": "Lower F4 to turn it off",
        "detail": "Move through the stored value and bring the fader fully down.",
        "hold": [],
        "tap": [],
        "faders": {
          "4": 0
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 1,
        "lane": 4,
        "amount": 0,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      }
    ]
  },
  {
    "id": "fx-2-1",
    "category": 3,
    "group": "Page 2 / FX 2",
    "title": "Bitcrush",
    "gesture": "F1 · T1 off",
    "body": "Move F1 for bitcrush. Tap T1 to turn it off.",
    "note": "",
    "steps": [
      {
        "label": "Hold FN + T2",
        "detail": "Keep both held until Page 2 opens.",
        "hold": [
          "FN",
          "T2"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": null,
        "duration": 0.6,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Release both",
        "detail": "Page 2 stays open.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": "page2",
        "duration": 1,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Page 2 is open",
        "detail": "The side lights identify Page 2.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": "page2",
        "duration": 2.5,
        "page": 2,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Find fader 1",
        "detail": "Fader 1 controls bitcrush.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": null,
        "duration": 1.6,
        "page": 2,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Move F1",
        "detail": "Move F1 down through its stored setting to take control.",
        "hold": [],
        "tap": [],
        "faders": {
          "1": 0.12
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 2,
        "lane": 1,
        "amount": 0.12,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Move back up",
        "detail": "Move F1 up to change the sound.",
        "hold": [],
        "tap": [],
        "faders": {
          "1": 0.9
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 2,
        "lane": 1,
        "amount": 0.9,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Release",
        "detail": "",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 1.4,
        "page": 2,
        "lane": 1,
        "amount": 0.9,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Tap T1 to reset",
        "detail": "The lane light goes dark at neutral.",
        "hold": [],
        "tap": [
          "T1"
        ],
        "faders": {},
        "rock": 0,
        "view": "trackLights",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 2,
        "lane": 1,
        "amount": 0,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      }
    ]
  },
  {
    "id": "fx-2-2",
    "category": 3,
    "group": "Page 2 / FX 2",
    "title": "Ring mod",
    "gesture": "F2 · T2 off",
    "body": "Move F2 for ring modulation. Tap T2 to turn it off.",
    "note": "",
    "steps": [
      {
        "label": "Hold FN + T2",
        "detail": "Keep both held until Page 2 opens.",
        "hold": [
          "FN",
          "T2"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": null,
        "duration": 0.6,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Release both",
        "detail": "Page 2 stays open.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": "page2",
        "duration": 1,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Page 2 is open",
        "detail": "The side lights identify Page 2.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": "page2",
        "duration": 2.5,
        "page": 2,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Find fader 2",
        "detail": "Fader 2 controls ring mod.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": null,
        "duration": 1.6,
        "page": 2,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Move F2",
        "detail": "Move F2 down through its stored setting to take control.",
        "hold": [],
        "tap": [],
        "faders": {
          "2": 0.12
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 2,
        "lane": 2,
        "amount": 0.12,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Move back up",
        "detail": "Move F2 up to change the sound.",
        "hold": [],
        "tap": [],
        "faders": {
          "2": 0.9
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 2,
        "lane": 2,
        "amount": 0.9,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Release",
        "detail": "",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 1.4,
        "page": 2,
        "lane": 2,
        "amount": 0.9,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Tap T2 to reset",
        "detail": "The lane light goes dark at neutral.",
        "hold": [],
        "tap": [
          "T2"
        ],
        "faders": {},
        "rock": 0,
        "view": "trackLights",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 2,
        "lane": 2,
        "amount": 0,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      }
    ]
  },
  {
    "id": "fx-2-3",
    "category": 3,
    "group": "Page 2 / FX 2",
    "title": "Auto-wah",
    "gesture": "F3 · T3 off",
    "body": "Move F3 for auto-wah. Tap T3 to turn it off.",
    "note": "Hold T3 and move F3 for relative resonance: down sharper, up softer.",
    "steps": [
      {
        "label": "Hold FN + T2",
        "detail": "Keep both held until Page 2 opens.",
        "hold": [
          "FN",
          "T2"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": null,
        "duration": 0.6,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Release both",
        "detail": "Page 2 stays open.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": "page2",
        "duration": 1,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Page 2 is open",
        "detail": "The side lights identify Page 2.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": "page2",
        "duration": 2.5,
        "page": 2,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Find fader 3",
        "detail": "Fader 3 controls auto-wah.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": null,
        "duration": 1.6,
        "page": 2,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Move F3",
        "detail": "Move F3 down through its stored setting to take control.",
        "hold": [],
        "tap": [],
        "faders": {
          "3": 0.12
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 2,
        "lane": 3,
        "amount": 0.12,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Move back up",
        "detail": "Move F3 up to change the sound.",
        "hold": [],
        "tap": [],
        "faders": {
          "3": 0.9
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 2,
        "lane": 3,
        "amount": 0.9,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Release",
        "detail": "",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 1.4,
        "page": 2,
        "lane": 3,
        "amount": 0.9,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Tap T3 to reset",
        "detail": "The lane light goes dark at neutral.",
        "hold": [],
        "tap": [
          "T3"
        ],
        "faders": {},
        "rock": 0,
        "view": "trackLights",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 2,
        "lane": 3,
        "amount": 0,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      }
    ]
  },
  {
    "id": "fx-2-4",
    "category": 3,
    "group": "Page 2 / FX 2",
    "title": "Echo",
    "gesture": "F4 · T4 cycles division",
    "body": "Tap T4 to cycle 1/16 → dotted → 1/8. F4 sets the amount. Lower F4 fully to turn it off.",
    "note": "Hold T4 and move F4 for feedback. Tap a rate with FN + T4, at least twice.",
    "steps": [
      {
        "label": "Hold FN + T2",
        "detail": "Keep both held until Page 2 opens.",
        "hold": [
          "FN",
          "T2"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": null,
        "duration": 0.6,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Release both",
        "detail": "Page 2 stays open.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": "page2",
        "duration": 1,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Page 2 is open",
        "detail": "The side lights identify Page 2.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": "page2",
        "duration": 2.5,
        "page": 2,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Find fader 4",
        "detail": "Fader 4 controls echo.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": null,
        "duration": 1.6,
        "page": 2,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Move F4",
        "detail": "Move F4 down through its stored setting to take control.",
        "hold": [],
        "tap": [],
        "faders": {
          "4": 0.12
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 2,
        "lane": 4,
        "amount": 0.12,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Move back up",
        "detail": "Move F4 up to change the sound.",
        "hold": [],
        "tap": [],
        "faders": {
          "4": 0.9
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 2,
        "lane": 4,
        "amount": 0.9,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Release",
        "detail": "",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 1.4,
        "page": 2,
        "lane": 4,
        "amount": 0.9,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Tap T4: dotted",
        "detail": "T4 confirms the selected setting. Shown slowed down.",
        "hold": [],
        "tap": [
          "T4"
        ],
        "faders": {},
        "rock": 0,
        "view": "trackLights",
        "lights": [],
        "pattern": "lane-count",
        "duration": 2.5,
        "page": 2,
        "lane": 4,
        "count": 2,
        "amount": 0.9,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        },
        "slowConfirmation": {
          "scale": 4,
          "length": 0.16,
          "start": 1.8
        },
        "seconds": 5.0
      },
      {
        "label": "Tap T4: 1/8",
        "detail": "T4 confirms the selected setting. Shown slowed down.",
        "hold": [],
        "tap": [
          "T4"
        ],
        "faders": {},
        "rock": 0,
        "view": "trackLights",
        "lights": [],
        "pattern": "lane-count",
        "duration": 2.5,
        "page": 2,
        "lane": 4,
        "count": 3,
        "amount": 0.9,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        },
        "slowConfirmation": {
          "scale": 4,
          "length": 0.24,
          "start": 1.8
        },
        "seconds": 5.0
      },
      {
        "label": "Tap T4: 1/16",
        "detail": "T4 confirms the selected setting. Shown slowed down.",
        "hold": [],
        "tap": [
          "T4"
        ],
        "faders": {},
        "rock": 0,
        "view": "trackLights",
        "lights": [],
        "pattern": "lane-count",
        "duration": 2.5,
        "page": 2,
        "lane": 4,
        "count": 1,
        "amount": 0.9,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        },
        "slowConfirmation": {
          "scale": 4,
          "length": 0.08,
          "start": 1.8
        },
        "seconds": 5.0
      },
      {
        "label": "Lower F4 to turn it off",
        "detail": "Move through the stored value and bring the fader fully down.",
        "hold": [],
        "tap": [],
        "faders": {
          "4": 0
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 2,
        "lane": 4,
        "amount": 0,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      }
    ]
  },
  {
    "id": "fx-3-1",
    "category": 3,
    "group": "Page 3 / FX 3",
    "title": "Phaser",
    "gesture": "F1 · T1 cycles division",
    "body": "Tap T1 to cycle beat → half → quarter. F1 sets the amount. Lower F1 fully to turn it off.",
    "note": "Tap a rate with FN + T1, at least twice.",
    "steps": [
      {
        "label": "Hold FN + T3",
        "detail": "Keep both held until Page 3 opens.",
        "hold": [
          "FN",
          "T3"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": null,
        "duration": 0.6,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Release both",
        "detail": "Page 3 stays open.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": "page3",
        "duration": 1,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Page 3 is open",
        "detail": "The side lights identify Page 3.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": "page3",
        "duration": 2.5,
        "page": 3,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Find fader 1",
        "detail": "Fader 1 controls phaser.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": null,
        "duration": 1.6,
        "page": 3,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Move F1",
        "detail": "Move F1 down through its stored setting to take control.",
        "hold": [],
        "tap": [],
        "faders": {
          "1": 0.12
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 3,
        "lane": 1,
        "amount": 0.12,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Move back up",
        "detail": "Move F1 up to change the sound.",
        "hold": [],
        "tap": [],
        "faders": {
          "1": 0.9
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 3,
        "lane": 1,
        "amount": 0.9,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Release",
        "detail": "",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 1.4,
        "page": 3,
        "lane": 1,
        "amount": 0.9,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Tap T1: half",
        "detail": "T1 confirms the selected setting. Shown slowed down.",
        "hold": [],
        "tap": [
          "T1"
        ],
        "faders": {},
        "rock": 0,
        "view": "trackLights",
        "lights": [],
        "pattern": "lane-count",
        "duration": 2.5,
        "page": 3,
        "lane": 1,
        "count": 2,
        "amount": 0.9,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        },
        "slowConfirmation": {
          "scale": 4,
          "length": 0.16,
          "start": 1.8
        },
        "seconds": 5.0
      },
      {
        "label": "Tap T1: quarter",
        "detail": "T1 confirms the selected setting. Shown slowed down.",
        "hold": [],
        "tap": [
          "T1"
        ],
        "faders": {},
        "rock": 0,
        "view": "trackLights",
        "lights": [],
        "pattern": "lane-count",
        "duration": 2.5,
        "page": 3,
        "lane": 1,
        "count": 3,
        "amount": 0.9,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        },
        "slowConfirmation": {
          "scale": 4,
          "length": 0.24,
          "start": 1.8
        },
        "seconds": 5.0
      },
      {
        "label": "Tap T1: beat",
        "detail": "T1 confirms the selected setting. Shown slowed down.",
        "hold": [],
        "tap": [
          "T1"
        ],
        "faders": {},
        "rock": 0,
        "view": "trackLights",
        "lights": [],
        "pattern": "lane-count",
        "duration": 2.5,
        "page": 3,
        "lane": 1,
        "count": 1,
        "amount": 0.9,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        },
        "slowConfirmation": {
          "scale": 4,
          "length": 0.08,
          "start": 1.8
        },
        "seconds": 5.0
      },
      {
        "label": "Lower F1 to turn it off",
        "detail": "Move through the stored value and bring the fader fully down.",
        "hold": [],
        "tap": [],
        "faders": {
          "1": 0
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 3,
        "lane": 1,
        "amount": 0,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      }
    ]
  },
  {
    "id": "fx-3-2",
    "category": 3,
    "group": "Page 3 / FX 3",
    "title": "Sweep",
    "gesture": "F2 · T2 cycles division",
    "body": "Tap T2 to cycle beat → half → quarter. F2 sets the amount. Lower F2 fully to turn it off.",
    "note": "Tap a rate with FN + T2, at least twice.",
    "steps": [
      {
        "label": "Hold FN + T3",
        "detail": "Keep both held until Page 3 opens.",
        "hold": [
          "FN",
          "T3"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": null,
        "duration": 0.6,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Release both",
        "detail": "Page 3 stays open.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": "page3",
        "duration": 1,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Page 3 is open",
        "detail": "The side lights identify Page 3.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": "page3",
        "duration": 2.5,
        "page": 3,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Find fader 2",
        "detail": "Fader 2 controls sweep.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": null,
        "duration": 1.6,
        "page": 3,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Move F2",
        "detail": "Move F2 down through its stored setting to take control.",
        "hold": [],
        "tap": [],
        "faders": {
          "2": 0.12
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 3,
        "lane": 2,
        "amount": 0.12,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Move back up",
        "detail": "Move F2 up to change the sound.",
        "hold": [],
        "tap": [],
        "faders": {
          "2": 0.9
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 3,
        "lane": 2,
        "amount": 0.9,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Release",
        "detail": "",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 1.4,
        "page": 3,
        "lane": 2,
        "amount": 0.9,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Tap T2: half",
        "detail": "T2 confirms the selected setting. Shown slowed down.",
        "hold": [],
        "tap": [
          "T2"
        ],
        "faders": {},
        "rock": 0,
        "view": "trackLights",
        "lights": [],
        "pattern": "lane-count",
        "duration": 2.5,
        "page": 3,
        "lane": 2,
        "count": 2,
        "amount": 0.9,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        },
        "slowConfirmation": {
          "scale": 4,
          "length": 0.16,
          "start": 1.8
        },
        "seconds": 5.0
      },
      {
        "label": "Tap T2: quarter",
        "detail": "T2 confirms the selected setting. Shown slowed down.",
        "hold": [],
        "tap": [
          "T2"
        ],
        "faders": {},
        "rock": 0,
        "view": "trackLights",
        "lights": [],
        "pattern": "lane-count",
        "duration": 2.5,
        "page": 3,
        "lane": 2,
        "count": 3,
        "amount": 0.9,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        },
        "slowConfirmation": {
          "scale": 4,
          "length": 0.24,
          "start": 1.8
        },
        "seconds": 5.0
      },
      {
        "label": "Tap T2: beat",
        "detail": "T2 confirms the selected setting. Shown slowed down.",
        "hold": [],
        "tap": [
          "T2"
        ],
        "faders": {},
        "rock": 0,
        "view": "trackLights",
        "lights": [],
        "pattern": "lane-count",
        "duration": 2.5,
        "page": 3,
        "lane": 2,
        "count": 1,
        "amount": 0.9,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        },
        "slowConfirmation": {
          "scale": 4,
          "length": 0.08,
          "start": 1.8
        },
        "seconds": 5.0
      },
      {
        "label": "Lower F2 to turn it off",
        "detail": "Move through the stored value and bring the fader fully down.",
        "hold": [],
        "tap": [],
        "faders": {
          "2": 0
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 3,
        "lane": 2,
        "amount": 0,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      }
    ]
  },
  {
    "id": "fx-3-3",
    "category": 3,
    "group": "Page 3 / FX 3",
    "title": "Tremolo",
    "gesture": "F3 · T3 cycles division",
    "body": "Tap T3 to cycle beat → half → quarter. F3 sets the amount. Lower F3 fully to turn it off.",
    "note": "Hold T3 and move F3 for shape; hold T3 and move F4 for stereo spread. Tap a rate with FN + T3.",
    "steps": [
      {
        "label": "Hold FN + T3",
        "detail": "Keep both held until Page 3 opens.",
        "hold": [
          "FN",
          "T3"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": null,
        "duration": 0.6,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Release both",
        "detail": "Page 3 stays open.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": "page3",
        "duration": 1,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Page 3 is open",
        "detail": "The side lights identify Page 3.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": "page3",
        "duration": 2.5,
        "page": 3,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Find fader 3",
        "detail": "Fader 3 controls tremolo.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": null,
        "duration": 1.6,
        "page": 3,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Move F3",
        "detail": "Move F3 down through its stored setting to take control.",
        "hold": [],
        "tap": [],
        "faders": {
          "3": 0.12
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 3,
        "lane": 3,
        "amount": 0.12,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Move back up",
        "detail": "Move F3 up to change the sound.",
        "hold": [],
        "tap": [],
        "faders": {
          "3": 0.9
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 3,
        "lane": 3,
        "amount": 0.9,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Release",
        "detail": "",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 1.4,
        "page": 3,
        "lane": 3,
        "amount": 0.9,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Tap T3: half",
        "detail": "T3 confirms the selected setting. Shown slowed down.",
        "hold": [],
        "tap": [
          "T3"
        ],
        "faders": {},
        "rock": 0,
        "view": "trackLights",
        "lights": [],
        "pattern": "lane-count",
        "duration": 2.5,
        "page": 3,
        "lane": 3,
        "count": 2,
        "amount": 0.9,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        },
        "slowConfirmation": {
          "scale": 4,
          "length": 0.16,
          "start": 1.8
        },
        "seconds": 5.0
      },
      {
        "label": "Tap T3: quarter",
        "detail": "T3 confirms the selected setting. Shown slowed down.",
        "hold": [],
        "tap": [
          "T3"
        ],
        "faders": {},
        "rock": 0,
        "view": "trackLights",
        "lights": [],
        "pattern": "lane-count",
        "duration": 2.5,
        "page": 3,
        "lane": 3,
        "count": 3,
        "amount": 0.9,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        },
        "slowConfirmation": {
          "scale": 4,
          "length": 0.24,
          "start": 1.8
        },
        "seconds": 5.0
      },
      {
        "label": "Tap T3: beat",
        "detail": "T3 confirms the selected setting. Shown slowed down.",
        "hold": [],
        "tap": [
          "T3"
        ],
        "faders": {},
        "rock": 0,
        "view": "trackLights",
        "lights": [],
        "pattern": "lane-count",
        "duration": 2.5,
        "page": 3,
        "lane": 3,
        "count": 1,
        "amount": 0.9,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        },
        "slowConfirmation": {
          "scale": 4,
          "length": 0.08,
          "start": 1.8
        },
        "seconds": 5.0
      },
      {
        "label": "Lower F3 to turn it off",
        "detail": "Move through the stored value and bring the fader fully down.",
        "hold": [],
        "tap": [],
        "faders": {
          "3": 0
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 3,
        "lane": 3,
        "amount": 0,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      }
    ]
  },
  {
    "id": "fx-3-4",
    "category": 3,
    "group": "Page 3 / FX 3",
    "title": "Reverb",
    "gesture": "F4 · T4 dry",
    "body": "F4 controls wet amount and decay. Tap T4 to turn it off.",
    "note": "Hold T4 and move F4 for damping: dark to bright. Echo and reverb can run together, echo into reverb.",
    "steps": [
      {
        "label": "Hold FN + T3",
        "detail": "Keep both held until Page 3 opens.",
        "hold": [
          "FN",
          "T3"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": null,
        "duration": 0.6,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Release both",
        "detail": "Page 3 stays open.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": "page3",
        "duration": 1,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Page 3 is open",
        "detail": "The side lights identify Page 3.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": "page3",
        "duration": 2.5,
        "page": 3,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Find fader 4",
        "detail": "Fader 4 controls reverb.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": null,
        "duration": 1.6,
        "page": 3,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Move F4",
        "detail": "Move F4 down through its stored setting to take control.",
        "hold": [],
        "tap": [],
        "faders": {
          "4": 0.12
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 3,
        "lane": 4,
        "amount": 0.12,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Move back up",
        "detail": "Move F4 up to change the sound.",
        "hold": [],
        "tap": [],
        "faders": {
          "4": 0.9
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 3,
        "lane": 4,
        "amount": 0.9,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Release",
        "detail": "",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 1.4,
        "page": 3,
        "lane": 4,
        "amount": 0.9,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Tap T4 to reset",
        "detail": "The lane light goes dark at neutral.",
        "hold": [],
        "tap": [
          "T4"
        ],
        "faders": {},
        "rock": 0,
        "view": "trackLights",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 3,
        "lane": 4,
        "amount": 0,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      }
    ]
  },
  {
    "id": "fx-4-1",
    "category": 3,
    "group": "Page 4 / Tape",
    "title": "Tape drive",
    "gesture": "F1 · T1 off",
    "body": "Move F1 for tape drive. Tap T1 to turn it off.",
    "note": "",
    "steps": [
      {
        "label": "Hold FN + T4",
        "detail": "Keep both held until Page 4 opens.",
        "hold": [
          "FN",
          "T4"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": null,
        "duration": 0.6,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Release both",
        "detail": "Page 4 stays open.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": "page4",
        "duration": 1,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Page 4 is open",
        "detail": "The side lights identify Page 4.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": "page4",
        "duration": 2.5,
        "page": 4,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Find fader 1",
        "detail": "Fader 1 controls tape drive.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": null,
        "duration": 1.6,
        "page": 4,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Move F1",
        "detail": "Move F1 down through its stored setting to take control.",
        "hold": [],
        "tap": [],
        "faders": {
          "1": 0.12
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 4,
        "lane": 1,
        "amount": 0.12,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Move back up",
        "detail": "Move F1 up to change the sound.",
        "hold": [],
        "tap": [],
        "faders": {
          "1": 0.9
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 4,
        "lane": 1,
        "amount": 0.9,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Release",
        "detail": "",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 1.4,
        "page": 4,
        "lane": 1,
        "amount": 0.9,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Tap T1 to reset",
        "detail": "The lane light goes dark at neutral.",
        "hold": [],
        "tap": [
          "T1"
        ],
        "faders": {},
        "rock": 0,
        "view": "trackLights",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 4,
        "lane": 1,
        "amount": 0,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      }
    ]
  },
  {
    "id": "fx-4-2",
    "category": 3,
    "group": "Page 4 / Tape",
    "title": "Tape tone",
    "gesture": "F2 · T2 flat",
    "body": "Move F2 for tape tone. Center is neutral. Tap T2 for flat.",
    "note": "",
    "steps": [
      {
        "label": "Hold FN + T4",
        "detail": "Keep both held until Page 4 opens.",
        "hold": [
          "FN",
          "T4"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": null,
        "duration": 0.6,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Release both",
        "detail": "Page 4 stays open.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": "page4",
        "duration": 1,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Page 4 is open",
        "detail": "The side lights identify Page 4.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": "page4",
        "duration": 2.5,
        "page": 4,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Find fader 2",
        "detail": "Fader 2 controls tape tone. Center is neutral.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": null,
        "duration": 1.6,
        "page": 4,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Move F2",
        "detail": "Move F2 down through its stored setting to take control.",
        "hold": [],
        "tap": [],
        "faders": {
          "2": 0.12
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 4,
        "lane": 2,
        "amount": 0.76,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Move back up",
        "detail": "Move F2 up to change the sound.",
        "hold": [],
        "tap": [],
        "faders": {
          "2": 0.9
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 4,
        "lane": 2,
        "amount": 0.8,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Release",
        "detail": "",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 1.4,
        "page": 4,
        "lane": 2,
        "amount": 0.8,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Tap T2 to reset",
        "detail": "The lane light goes dark at neutral.",
        "hold": [],
        "tap": [
          "T2"
        ],
        "faders": {},
        "rock": 0,
        "view": "trackLights",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 4,
        "lane": 2,
        "amount": 0,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      }
    ]
  },
  {
    "id": "fx-4-3",
    "category": 3,
    "group": "Page 4 / Tape",
    "title": "Noise",
    "gesture": "F3 · T3 off",
    "body": "F3 up adds hiss; down adds vinyl crackle. Center is off. Tap T3 to turn it off.",
    "note": "",
    "steps": [
      {
        "label": "Hold FN + T4",
        "detail": "Keep both held until Page 4 opens.",
        "hold": [
          "FN",
          "T4"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": null,
        "duration": 0.6,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Release both",
        "detail": "Page 4 stays open.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": "page4",
        "duration": 1,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Page 4 is open",
        "detail": "The side lights identify Page 4.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": "page4",
        "duration": 2.5,
        "page": 4,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Find fader 3",
        "detail": "Fader 3 controls noise.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": null,
        "duration": 1.6,
        "page": 4,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Move F3",
        "detail": "Move F3 down through its saved level to take control.",
        "hold": [],
        "tap": [],
        "faders": {
          "3": 0.12
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 4,
        "lane": 3,
        "amount": 0.76,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Move back up",
        "detail": "Raise F3 to add more of the selected noise.",
        "hold": [],
        "tap": [],
        "faders": {
          "3": 0.9
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 4,
        "lane": 3,
        "amount": 0.8,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Release",
        "detail": "",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 1.4,
        "page": 4,
        "lane": 3,
        "amount": 0.8,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Tap T3 to reset",
        "detail": "The lane light goes dark at neutral.",
        "hold": [],
        "tap": [
          "T3"
        ],
        "faders": {},
        "rock": 0,
        "view": "trackLights",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 4,
        "lane": 3,
        "amount": 0,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      }
    ]
  },
  {
    "id": "fx-4-4",
    "category": 3,
    "group": "Page 4 / Tape",
    "title": "Wobble",
    "gesture": "F4 · T4 off",
    "body": "Move F4 for tape wobble. Tap T4 to turn it off and clear its tapped rate.",
    "note": "Tap a rate with FN + T4, at least twice.",
    "steps": [
      {
        "label": "Hold FN + T4",
        "detail": "Keep both held until Page 4 opens.",
        "hold": [
          "FN",
          "T4"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": null,
        "duration": 0.6,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Release both",
        "detail": "Page 4 stays open.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": "page4",
        "duration": 1,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Page 4 is open",
        "detail": "The side lights identify Page 4.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": "page4",
        "duration": 2.5,
        "page": 4,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Find fader 4",
        "detail": "Fader 4 controls wobble.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": null,
        "duration": 1.6,
        "page": 4,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Move F4",
        "detail": "Move F4 down through its stored setting to take control.",
        "hold": [],
        "tap": [],
        "faders": {
          "4": 0.12
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 4,
        "lane": 4,
        "amount": 0.12,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Move back up",
        "detail": "Move F4 up to change the sound.",
        "hold": [],
        "tap": [],
        "faders": {
          "4": 0.9
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 4,
        "lane": 4,
        "amount": 0.9,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Release",
        "detail": "",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 1.4,
        "page": 4,
        "lane": 4,
        "amount": 0.9,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Tap T4 to reset",
        "detail": "The lane light goes dark at neutral.",
        "hold": [],
        "tap": [
          "T4"
        ],
        "faders": {},
        "rock": 0,
        "view": "trackLights",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 4,
        "lane": 4,
        "amount": 0,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      }
    ]
  },
  {
    "id": "tap-rate",
    "category": 3,
    "group": "Pages & pickup",
    "title": "FX tap tempo",
    "gesture": "Hold FN + tap the lane 2+ times",
    "body": "On its page, hold FN and tap the rate-enabled lane at least twice.",
    "note": "Clear a tapped rate on its page with FN + PLAY + that track. Supported lanes: P1 T2/T4; P2 T4; P3 T1/T2/T3; P4 T4.",
    "steps": [
      {
        "label": "Hold FN",
        "detail": "Example: Page 2 echo, T4.",
        "hold": [
          "FN"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        },
        "page": 2
      },
      {
        "label": "Tap T4 in time",
        "detail": "At least two taps.",
        "hold": [
          "FN"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "tracks",
        "lights": [],
        "pattern": "rateT4",
        "duration": 3,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        },
        "page": 2
      },
      {
        "label": "To clear: FN + PLAY, then T4",
        "detail": "Use on that effect’s page.",
        "hold": [
          "FN",
          "PLAY"
        ],
        "tap": [
          "T4"
        ],
        "faders": {},
        "rock": 0,
        "view": "tracks",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        },
        "page": 2
      },
      {
        "label": "Release",
        "detail": "",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        },
        "page": 2
      }
    ]
  },
  {
    "id": "secondaries",
    "category": 3,
    "group": "Secondary controls",
    "title": "Secondary FX controls",
    "gesture": "Hold track + move fader",
    "body": "P2: T3 + F3 = wah resonance; T4 + F4 = echo feedback. P3: T3 + F3 = tremolo shape; T3 + F4 = stereo spread; T4 + F4 = reverb damping.",
    "note": "Move before releasing the track button to avoid its tap action. After release, cross the main value again before changing it.",
    "steps": [
      {
        "label": "Hold T4",
        "detail": "Example: hold T4 on Page 2.",
        "hold": [
          "T4"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": null,
        "duration": 1.6,
        "page": 2,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Change echo feedback",
        "detail": "Example: hold T4 on Page 2.",
        "hold": [
          "T4"
        ],
        "tap": [],
        "faders": {
          "4": 0.12
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 2,
        "lane": 4,
        "amount": 0.12,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Move back up",
        "detail": "Example: hold T4 on Page 2.",
        "hold": [
          "T4"
        ],
        "tap": [],
        "faders": {
          "4": 0.9
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 2,
        "lane": 4,
        "amount": 0.9,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Release",
        "detail": "",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 1.4,
        "page": 2,
        "lane": 4,
        "amount": 0.6,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      }
    ],
    "initialFaders": [
      0.5,
      0.5,
      0.5,
      0.5
    ]
  },
  {
    "id": "eq",
    "category": 3,
    "group": "Page 5 / Global EQ",
    "title": "Master EQ",
    "gesture": "F1 100 Hz · F2 350 Hz · F3 1.6 kHz · F4 4 kHz",
    "body": "Down cuts; up boosts. Center or a track-button tap returns that band to flat.",
    "note": "F1 and F4 are low/high shelves. EQ always affects the whole mix; lights show cut/boost amount.",
    "steps": [
      {
        "label": "Hold FN + T4",
        "detail": "Keep both held until Page 4 opens.",
        "hold": [
          "FN",
          "T4"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": null,
        "duration": 0.6,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Release both",
        "detail": "Page 4 stays open.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": "page4",
        "duration": 1,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Hold FN; tap VOL+",
        "detail": "Advance to Page 5.",
        "hold": [
          "FN"
        ],
        "tap": [
          "VOL+"
        ],
        "faders": {},
        "rock": 0,
        "view": "top",
        "lights": [],
        "pattern": null,
        "duration": 1.2,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Page 5 is open",
        "detail": "The side lights identify Page 5.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": "page5",
        "duration": 4,
        "page": 5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Find fader 2",
        "detail": "Center is flat; up boosts.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": null,
        "duration": 1.6,
        "page": 5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Cut the 350 Hz band",
        "detail": "Center is flat; up boosts.",
        "hold": [],
        "tap": [],
        "faders": {
          "2": 0.12
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 5,
        "lane": 2,
        "amount": 0.76,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Move back up",
        "detail": "Center is flat; up boosts.",
        "hold": [],
        "tap": [],
        "faders": {
          "2": 0.9
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 5,
        "lane": 2,
        "amount": 0.8,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Release",
        "detail": "",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 1.4,
        "page": 5,
        "lane": 2,
        "amount": 0.8,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Tap T2 for flat",
        "detail": "Its light goes dark at neutral.",
        "hold": [],
        "tap": [
          "T2"
        ],
        "faders": {},
        "rock": 0,
        "view": "trackLights",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "page": 5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      }
    ]
  },
  {
    "id": "routing",
    "category": 3,
    "group": "FX routing & reset",
    "title": "FX routing",
    "gesture": "PLAY + VOL− / VOL+ / double-click",
    "body": "Hold PLAY and press VOL− for INPUT or VOL+ for TRACKS. Hold PLAY and double-click either volume button for BOTH.",
    "note": "",
    "steps": [
      {
        "label": "Hold PLAY",
        "detail": "Echo is already active, making the route change audible. Keep PLAY held.",
        "hold": [
          "PLAY"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "top",
        "lights": [],
        "pattern": null,
        "duration": 1.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Choose INPUT",
        "detail": "One confirmation flash.",
        "hold": [
          "PLAY"
        ],
        "tap": [
          "VOL−"
        ],
        "faders": {},
        "rock": 0,
        "view": "top",
        "lights": [],
        "pattern": null,
        "duration": 2.8,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Release the controls",
        "detail": "",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "top",
        "lights": [],
        "pattern": null,
        "duration": 1.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "INPUT",
        "detail": "Ordinary takes record input FX. Shown slowed down.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": "one",
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        },
        "confirmationReplay": true,
        "slowConfirmation": {
          "scale": 4,
          "length": 0.128,
          "start": 1.8
        },
        "seconds": 5.0
      },
      {
        "label": "Hold PLAY",
        "detail": "Keep PLAY held.",
        "hold": [
          "PLAY"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "top",
        "lights": [],
        "pattern": null,
        "duration": 1.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Choose TRACKS",
        "detail": "Two confirmation flashes.",
        "hold": [
          "PLAY"
        ],
        "tap": [
          "VOL+"
        ],
        "faders": {},
        "rock": 0,
        "view": "top",
        "lights": [],
        "pattern": null,
        "duration": 2.8,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Release the controls",
        "detail": "",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "top",
        "lights": [],
        "pattern": null,
        "duration": 1.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "TRACKS",
        "detail": "Live input stays dry. Shown slowed down.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": "two",
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        },
        "confirmationReplay": true,
        "slowConfirmation": {
          "scale": 4,
          "length": 0.256,
          "start": 1.8
        },
        "seconds": 5.0
      },
      {
        "label": "PLAY + double-click either VOL",
        "detail": "Choose BOTH.",
        "hold": [
          "PLAY"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "top",
        "lights": [],
        "pattern": "volDouble",
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "BOTH",
        "detail": "Three confirmation flashes; ordinary takes stay dry. Shown slowed down.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": "three",
        "duration": 3,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        },
        "confirmationReplay": true,
        "slowConfirmation": {
          "scale": 4,
          "length": 0.384,
          "start": 1.8
        },
        "seconds": 6
      }
    ]
  },
  {
    "id": "monitor",
    "category": 3,
    "group": "FX routing & reset",
    "title": "Input monitoring",
    "gesture": "Hold PLAY + both VOL",
    "body": "Toggle monitoring with PLAY and both volume buttons. Works on any page.",
    "note": "Muted monitoring blocks ordinary recording. Unmute input before arming. On Pages 1–4, a blinking page number also marks muted monitoring.",
    "steps": [
      {
        "label": "Hold PLAY + both VOL",
        "detail": "Only live input is playing; there are no recorded tracks. Toggle the live input monitor.",
        "hold": [
          "PLAY"
        ],
        "tap": [
          "VOL−",
          "VOL+"
        ],
        "faders": {},
        "rock": 0,
        "view": "top",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "4→3→2→1: muted",
        "detail": "Recording still captures input. Shown slowed down.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": "sweep-down",
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        },
        "confirmationReplay": true,
        "slowConfirmation": {
          "scale": 4,
          "length": 0.384,
          "start": 1.8
        },
        "seconds": 5.0
      },
      {
        "label": "1→2→3→4: live",
        "detail": "Repeat PLAY + both volume buttons to restore monitoring. Shown slowed down.",
        "hold": [
          "PLAY"
        ],
        "tap": [
          "VOL−",
          "VOL+"
        ],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": "sweep-up",
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        },
        "slowConfirmation": {
          "scale": 4,
          "length": 0.384,
          "start": 1.8
        },
        "seconds": 5.0
      }
    ]
  },
  {
    "id": "reset-fx",
    "category": 3,
    "group": "FX routing & reset",
    "title": "FX reset",
    "gesture": "On an FX page: FN + T1 + T4",
    "body": "Restore the effects to their defaults, flatten EQ and set routing to BOTH.",
    "note": "On Page 6 this chord resets placement; on Page 7 it resets timing instead.",
    "steps": [
      {
        "label": "On an FX page, hold FN",
        "detail": "Echo and an EQ boost are already active. Keep it held.",
        "hold": [
          "FN"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        },
        "page": 2
      },
      {
        "label": "Tap T1 + T4 together",
        "detail": "Reset effects to defaults.",
        "hold": [
          "FN"
        ],
        "tap": [
          "T1",
          "T4"
        ],
        "faders": {},
        "rock": 0,
        "view": "tracks",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        },
        "page": 2
      },
      {
        "label": "Release",
        "detail": "EQ is flat; routing is BOTH.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        },
        "page": 2
      }
    ]
  },
  {
    "id": "placement",
    "category": 4,
    "group": "Page 6 / Stereo placement",
    "title": "Stereo track placement",
    "gesture": "Faders 1–4 = tracks 1–4",
    "body": "Down moves left. Up moves right. Center keeps the original balance.",
    "note": "A mono take pans; a stereo take leans. Lights are dark at center and brighter farther to either side.",
    "steps": [
      {
        "label": "Hold FN + T4",
        "detail": "Keep both held until Page 4 opens.",
        "hold": [
          "FN",
          "T4"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": null,
        "duration": 0.6,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Release both",
        "detail": "Page 4 stays open.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": "page4",
        "duration": 1,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Hold FN; tap VOL+",
        "detail": "Advance to Page 5.",
        "hold": [
          "FN"
        ],
        "tap": [
          "VOL+"
        ],
        "faders": {},
        "rock": 0,
        "view": "top",
        "lights": [],
        "pattern": null,
        "duration": 1.2,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Hold FN; tap VOL+",
        "detail": "Advance to Page 6.",
        "hold": [
          "FN"
        ],
        "tap": [
          "VOL+"
        ],
        "faders": {},
        "rock": 0,
        "view": "top",
        "lights": [],
        "pattern": null,
        "duration": 1.2,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Page 6 is open",
        "detail": "The side lights identify Page 6.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": "page6",
        "duration": 4,
        "page": 6,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Find fader 1",
        "detail": "Down = left; up = right.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": null,
        "duration": 1.6,
        "page": 6,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Move track 1 left",
        "detail": "Down = left; up = right.",
        "hold": [],
        "tap": [],
        "faders": {
          "1": 0.12
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 6,
        "lane": 1,
        "amount": 0.76,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Move back up",
        "detail": "Down = left; up = right.",
        "hold": [],
        "tap": [],
        "faders": {
          "1": 0.9
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 6,
        "lane": 1,
        "amount": 0.8,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Release",
        "detail": "",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 1.4,
        "page": 6,
        "lane": 1,
        "amount": 0.8,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Read the placement light",
        "detail": "It is dark at center, brighter farther from center.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "trackLights",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 6,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        },
        "lane": 1,
        "amount": 0.8
      }
    ]
  },
  {
    "id": "center-placement",
    "category": 4,
    "group": "Page 6 / Stereo placement",
    "title": "Stereo placement reset",
    "gesture": "Tap a track button · FN + T1 + T4",
    "body": "With no take recording, tap a track to center its placement. FN + T1 + T4 centers all four on Page 6.",
    "note": "Placement saves per song after about two seconds. Replacing or deleting a take keeps placement. A bounce prints the placed mix.",
    "steps": [
      {
        "label": "Tap T1 on Page 6",
        "detail": "Tracks start away from the center. Center that track.",
        "hold": [],
        "tap": [
          "T1"
        ],
        "faders": {},
        "rock": 0,
        "view": "tracks",
        "lights": [],
        "pattern": null,
        "duration": 2,
        "page": 6,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Release",
        "detail": "Center that track.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "tracks",
        "lights": [],
        "pattern": null,
        "duration": 1.5,
        "page": 6,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Or reset all four",
        "detail": "Hold FN and tap T1 + T4 together.",
        "hold": [
          "FN"
        ],
        "tap": [
          "T1",
          "T4"
        ],
        "faders": {},
        "rock": 0,
        "view": "tracks",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "page": 6,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Release",
        "detail": "Allow about two seconds for saving.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "page": 6,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      }
    ]
  },
  {
    "id": "offset",
    "category": 4,
    "group": "Page 7 / Take & timing",
    "title": "Grid offset",
    "gesture": "F1 = grid offset",
    "body": "Center is no offset. Up moves later; down moves earlier, by up to about half a beat.",
    "note": "Beat lights and future grid-aligned punches follow the offset.",
    "steps": [
      {
        "label": "Hold FN + T4",
        "detail": "Keep both held until Page 4 opens.",
        "hold": [
          "FN",
          "T4"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": null,
        "duration": 0.6,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true
        }
      },
      {
        "label": "Release both",
        "detail": "Page 4 stays open.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": "page4",
        "duration": 1,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true
        }
      },
      {
        "label": "Hold FN; tap VOL+",
        "detail": "Advance to Page 5.",
        "hold": [
          "FN"
        ],
        "tap": [
          "VOL+"
        ],
        "faders": {},
        "rock": 0,
        "view": "top",
        "lights": [],
        "pattern": null,
        "duration": 1.2,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true
        }
      },
      {
        "label": "Hold FN; tap VOL+",
        "detail": "Advance to Page 6.",
        "hold": [
          "FN"
        ],
        "tap": [
          "VOL+"
        ],
        "faders": {},
        "rock": 0,
        "view": "top",
        "lights": [],
        "pattern": null,
        "duration": 1.2,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true
        }
      },
      {
        "label": "Hold FN; tap VOL+",
        "detail": "Advance to Page 7.",
        "hold": [
          "FN"
        ],
        "tap": [
          "VOL+"
        ],
        "faders": {},
        "rock": 0,
        "view": "top",
        "lights": [],
        "pattern": null,
        "duration": 1.2,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true
        }
      },
      {
        "label": "Page 7 is open",
        "detail": "The side lights identify Page 7.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": "page7",
        "duration": 4,
        "page": 7,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true
        }
      },
      {
        "label": "Find fader 1",
        "detail": "Up later; down earlier.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": "beat",
        "duration": 1.6,
        "page": 7,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true
        }
      },
      {
        "label": "Move the grid",
        "detail": "Up later; down earlier.",
        "hold": [],
        "tap": [],
        "faders": {
          "1": 0.12
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": "beat",
        "duration": 2.5,
        "page": 7,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true
        }
      },
      {
        "label": "Move back up",
        "detail": "Up later; down earlier.",
        "hold": [],
        "tap": [],
        "faders": {
          "1": 0.9
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": "beat",
        "duration": 2.5,
        "page": 7,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true
        }
      },
      {
        "label": "Release",
        "detail": "",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": "beat",
        "duration": 1.4,
        "page": 7,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true
        }
      },
      {
        "label": "The beat lights follow",
        "detail": "",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "trackLights",
        "lights": [],
        "pattern": "beat",
        "duration": 2.5,
        "page": 7,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true
        }
      }
    ]
  },
  {
    "id": "take-length",
    "category": 4,
    "group": "Page 7 / Take & timing",
    "title": "Take length & beats per bar",
    "gesture": "F2 = OFF / 1 / 2 / 4 / 8",
    "body": "Move F2 from bottom to top to select the preset length.",
    "note": "Variable mode counts bars and needs a grid. Fixed mode counts base loops and needs a first take.",
    "steps": [
      {
        "label": "Select OFF",
        "detail": "T2 gives one long blink for OFF. Shown slowed down.",
        "hold": [],
        "tap": [],
        "faders": {
          "2": 0
        },
        "rock": 0,
        "view": "trackLights",
        "lights": [],
        "pattern": "length",
        "duration": 1.5,
        "count": 0,
        "page": 7,
        "slowConfirmation": {
          "scale": 4,
          "length": 0.24,
          "start": 1.8
        },
        "seconds": 3.56
      },
      {
        "label": "Select 1",
        "detail": "T2 flashes 1 time. Shown slowed down.",
        "hold": [],
        "tap": [],
        "faders": {
          "2": 0.25
        },
        "rock": 0,
        "view": "trackLights",
        "lights": [],
        "pattern": "length",
        "duration": 1.5,
        "count": 1,
        "page": 7,
        "slowConfirmation": {
          "scale": 4,
          "length": 0.08,
          "start": 1.8
        },
        "seconds": 3.0
      },
      {
        "label": "Select 2",
        "detail": "T2 flashes 2 times. Shown slowed down.",
        "hold": [],
        "tap": [],
        "faders": {
          "2": 0.5
        },
        "rock": 0,
        "view": "trackLights",
        "lights": [],
        "pattern": "length",
        "duration": 1.5,
        "count": 2,
        "page": 7,
        "slowConfirmation": {
          "scale": 4,
          "length": 0.16,
          "start": 1.8
        },
        "seconds": 3.24
      },
      {
        "label": "Select 4",
        "detail": "T2 flashes 3 times. Shown slowed down.",
        "hold": [],
        "tap": [],
        "faders": {
          "2": 0.75
        },
        "rock": 0,
        "view": "trackLights",
        "lights": [],
        "pattern": "length",
        "duration": 1.5,
        "count": 3,
        "page": 7,
        "slowConfirmation": {
          "scale": 4,
          "length": 0.24,
          "start": 1.8
        },
        "seconds": 3.56
      },
      {
        "label": "Select 8",
        "detail": "T2 flashes 4 times. Shown slowed down.",
        "hold": [],
        "tap": [],
        "faders": {
          "2": 1
        },
        "rock": 0,
        "view": "trackLights",
        "lights": [],
        "pattern": "length",
        "duration": 1.5,
        "count": 4,
        "page": 7,
        "slowConfirmation": {
          "scale": 4,
          "length": 0.32,
          "start": 1.8
        },
        "seconds": 3.88
      }
    ]
  },
  {
    "id": "preset",
    "category": 4,
    "group": "Page 7 / Take & timing",
    "title": "Preset recording length",
    "gesture": "Leave Page 7 → arm a track",
    "body": "On Page 7, choose a take length with F2. Tap FN to close the page, tap an empty track and let recording finish automatically.",
    "note": "The take stops automatically. Tap the recording track to finish earlier. Bounces do not use the preset.",
    "steps": [
      {
        "label": "Choose one bar on Page 7",
        "detail": "With a beat grid, set F2 to 1 for a one-bar take. Shown slowed down.",
        "hold": [],
        "tap": [],
        "faders": {
          "2": 0.25
        },
        "rock": 0,
        "view": "trackLights",
        "lights": [],
        "pattern": "length",
        "duration": 1.5,
        "count": 1,
        "page": 7,
        "context": {
          "playing": true,
          "loaded": [],
          "grid": true
        },
        "slowConfirmation": {
          "scale": 4,
          "length": 0.08,
          "start": 1.8
        },
        "seconds": 3.0
      },
      {
        "label": "Tap FN",
        "detail": "Leave Page 7.",
        "hold": [],
        "tap": [
          "FN"
        ],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [],
        "pattern": null,
        "duration": 2,
        "context": {
          "playing": true,
          "loaded": [],
          "grid": true
        }
      },
      {
        "label": "Release",
        "detail": "Leave Page 7.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [],
        "pattern": null,
        "duration": 1.5,
        "context": {
          "playing": true,
          "loaded": [],
          "grid": true
        }
      },
      {
        "label": "Tap T1 to arm it",
        "detail": "Use an empty track.",
        "hold": [],
        "tap": [
          "T1"
        ],
        "faders": {},
        "rock": 0,
        "view": "tracks",
        "lights": [],
        "pattern": null,
        "duration": 0.5,
        "context": {
          "playing": true,
          "loaded": [],
          "grid": true
        }
      },
      {
        "label": "Recording follows the grid",
        "detail": "The take starts at its scheduled punch-in and uses your chosen length.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "trackLights",
        "lights": [],
        "pattern": "grid-wait",
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [],
          "grid": true
        }
      },
      {
        "label": "The take finishes itself",
        "detail": "A tap can finish it earlier.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": "grid-playing",
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true
        }
      }
    ]
  },
  {
    "id": "countdown",
    "category": 4,
    "group": "Page 7 / Take & timing",
    "title": "Recording countdown",
    "gesture": "Open Page 7 during a preset take",
    "body": "Up to four track lights show the units remaining. The last unit blinks.",
    "note": "The countdown is capped at four lights; an eight-unit take stays at four until only three units remain.",
    "steps": [
      {
        "label": "Count down to the end of the take",
        "detail": "Four bars remain. The last bar blinks; after recording stops, the grid chase returns.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "trackLights",
        "lights": [],
        "pattern": "countdown",
        "duration": 4.5,
        "page": 7,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true
        }
      }
    ]
  },
  {
    "id": "nudge",
    "category": 4,
    "group": "Page 7 / Nudge a loop",
    "title": "Track timing offset",
    "gesture": "Hold a track button + move its fader",
    "body": "On Page 7, hold a track button and move its fader. Up moves the track later; down moves it earlier. The edit is relative to the fader position when you held the button.",
    "note": "Nudge works without a grid. A recording track cannot be nudged. In heads mode, nudging the source moves all its heads.",
    "steps": [
      {
        "label": "Hold T1",
        "detail": "Two recorded portions of the input loop are loaded so you can hear their timing change. Hold T1 on Page 7.",
        "hold": [
          "T1"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": null,
        "duration": 1.6,
        "page": 7,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true
        }
      },
      {
        "label": "Nudge track 1",
        "detail": "Example: T1 + F1 on Page 7.",
        "hold": [
          "T1"
        ],
        "tap": [],
        "faders": {
          "1": 0.12
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 7,
        "lane": 1,
        "amount": 0.76,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true
        }
      },
      {
        "label": "Move back up",
        "detail": "Example: T1 + F1 on Page 7.",
        "hold": [
          "T1"
        ],
        "tap": [],
        "faders": {
          "1": 0.9
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 7,
        "lane": 1,
        "amount": 0.8,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true
        }
      },
      {
        "label": "Release",
        "detail": "",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": "nudged",
        "duration": 1.4,
        "page": 7,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true
        }
      },
      {
        "label": "Release the track",
        "detail": "You stay on Page 7. F1 returns to grid offset; F2 to take length.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": "nudged",
        "duration": 2.5,
        "page": 7,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true
        }
      }
    ]
  },
  {
    "id": "nudge-lights",
    "category": 4,
    "group": "Page 7 / Nudge a loop",
    "title": "Timing offset lights",
    "gesture": "Amount while held · dim glow after",
    "body": "While a track is held, its light shows the nudge amount. After release, nudged tracks glow dim between beats.",
    "note": "The 1–2–3–4 chase shows the grid. Settings save per song after about two seconds.",
    "steps": [
      {
        "label": "While T1 is held",
        "detail": "Track 1 is already nudged. Move its fader while held before releasing. Its light shows the nudge amount.",
        "hold": [
          "T1"
        ],
        "tap": [],
        "faders": {
          "1": 0.55
        },
        "rock": 0,
        "view": "trackLights",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 7,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true
        },
        "lane": 1,
        "amount": 0.5
      },
      {
        "label": "After release",
        "detail": "Nudged tracks glow dim between the beats.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "trackLights",
        "lights": [],
        "pattern": "nudged",
        "duration": 4,
        "page": 7,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true
        }
      }
    ]
  },
  {
    "id": "reset-timing",
    "category": 4,
    "group": "Page 7 / Nudge a loop",
    "title": "Timing reset",
    "gesture": "Tap a track button · FN + T1 + T4",
    "body": "With no take recording, tap a track button to center its nudge. If already centered, T1 clears grid offset and T2 switches take length off.",
    "note": "On Page 7, FN + T1 + T4 resets grid offset, take length and all track nudges.",
    "steps": [
      {
        "label": "Tap T1 on Page 7",
        "detail": "Track nudge, grid offset and take length have already been changed. Center its nudge, or clear offset if already centered.",
        "hold": [],
        "tap": [
          "T1"
        ],
        "faders": {},
        "rock": 0,
        "view": "tracks",
        "lights": [],
        "pattern": null,
        "duration": 2,
        "page": 7,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true
        }
      },
      {
        "label": "Release",
        "detail": "Center its nudge, or clear offset if already centered.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "tracks",
        "lights": [],
        "pattern": null,
        "duration": 1.5,
        "page": 7,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true
        }
      },
      {
        "label": "Reset the whole timing page",
        "detail": "Hold FN and tap T1 + T4 together.",
        "hold": [
          "FN"
        ],
        "tap": [
          "T1",
          "T4"
        ],
        "faders": {},
        "rock": 0,
        "view": "tracks",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "page": 7,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true
        }
      },
      {
        "label": "Release",
        "detail": "Offset 0; length OFF; all nudges centered.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "page": 7,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true
        }
      }
    ]
  },
  {
    "id": "format",
    "category": 4,
    "group": "Page 8 / Next take format",
    "title": "Mono & stereo recording",
    "gesture": "Tap a track button on Page 8",
    "body": "Before recording, tap a track button to choose the format of its next take.",
    "note": "Solid light = mono. Blinking = stereo. Existing audio stays unchanged. Faders still control track levels.",
    "steps": [
      {
        "label": "Hold FN + T4",
        "detail": "All four tracks start with mono selected for their next take. Hold FN + T4 until Page 4 opens.",
        "hold": [
          "FN",
          "T4"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": null,
        "duration": 0.6
      },
      {
        "label": "Release both",
        "detail": "Page 4 stays open.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": "page4",
        "duration": 1
      },
      {
        "label": "Hold FN; tap VOL+",
        "detail": "Advance to Page 5.",
        "hold": [
          "FN"
        ],
        "tap": [
          "VOL+"
        ],
        "faders": {},
        "rock": 0,
        "view": "top",
        "lights": [],
        "pattern": null,
        "duration": 1.2
      },
      {
        "label": "Hold FN; tap VOL+",
        "detail": "Advance to Page 6.",
        "hold": [
          "FN"
        ],
        "tap": [
          "VOL+"
        ],
        "faders": {},
        "rock": 0,
        "view": "top",
        "lights": [],
        "pattern": null,
        "duration": 1.2
      },
      {
        "label": "Hold FN; tap VOL+",
        "detail": "Advance to Page 7.",
        "hold": [
          "FN"
        ],
        "tap": [
          "VOL+"
        ],
        "faders": {},
        "rock": 0,
        "view": "top",
        "lights": [],
        "pattern": null,
        "duration": 1.2
      },
      {
        "label": "Hold FN; tap VOL+",
        "detail": "Advance to Page 8.",
        "hold": [
          "FN"
        ],
        "tap": [
          "VOL+"
        ],
        "faders": {},
        "rock": 0,
        "view": "top",
        "lights": [],
        "pattern": null,
        "duration": 1.2
      },
      {
        "label": "Page 8 is open",
        "detail": "The side lights identify Page 8.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": "page8",
        "duration": 4,
        "page": 8
      },
      {
        "label": "Tap T1 to switch",
        "detail": "Choose its next take format.",
        "hold": [],
        "tap": [
          "T1"
        ],
        "faders": {},
        "rock": 0,
        "view": "tracks",
        "lights": [],
        "pattern": "format",
        "duration": 2.5,
        "page": 8,
        "mono": [
          2,
          3,
          4
        ]
      },
      {
        "label": "Blinking: stereo",
        "detail": "Example: T1 stereo blinks; T2–T4 mono stay solid.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "trackLights",
        "lights": [],
        "pattern": "format",
        "duration": 3,
        "mono": [
          2,
          3,
          4
        ],
        "page": 8
      }
    ]
  },
  {
    "id": "catch",
    "category": 5,
    "group": "Catch & resize",
    "title": "Beat repeat",
    "gesture": "Hold PLAY + ROCKER UP",
    "body": "Hold PLAY and click the rocker up to catch a beat. Use the rocker to resize it, then release PLAY to return to the song.",
    "note": "",
    "steps": [
      {
        "label": "Hold PLAY",
        "detail": "Keep it down through the repeat.",
        "hold": [
          "PLAY"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true
        }
      },
      {
        "label": "Click the rocker up",
        "detail": "Catch the previous beat.",
        "hold": [
          "PLAY"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "left",
        "lights": [],
        "pattern": "rockTap",
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true
        }
      },
      {
        "label": "Keep PLAY held",
        "detail": "The captured window repeats.",
        "hold": [
          "PLAY"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true
        }
      },
      {
        "label": "ROCKER UP halves",
        "detail": "Click again or hold to keep stepping.",
        "hold": [
          "PLAY"
        ],
        "tap": [],
        "faders": {},
        "rock": -1,
        "view": "left",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true
        }
      },
      {
        "label": "ROCKER DOWN doubles",
        "detail": "Within the audible window.",
        "hold": [
          "PLAY"
        ],
        "tap": [],
        "faders": {},
        "rock": 1,
        "view": "left",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true
        }
      },
      {
        "label": "Release PLAY",
        "detail": "The tape shift rounds to the nearest bar on a gridded song.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true
        }
      },
      {
        "label": "Playback continues",
        "detail": "Short stutters may leave the position unchanged.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true
        }
      }
    ]
  },
  {
    "id": "resize-repeat",
    "category": 5,
    "group": "Catch & resize",
    "title": "Beat repeat length",
    "gesture": "Hold PLAY → click ROCKER UP → resize",
    "body": "Hold PLAY and click the rocker up to catch a beat. ROCKER UP halves the repeat; ROCKER DOWN doubles it. Hold the rocker to keep stepping. Release PLAY to return to the song.",
    "note": "Release the rocker but keep PLAY held to repeat at the same length.",
    "steps": [
      {
        "label": "Hold PLAY",
        "detail": "Keep it down through the repeat.",
        "hold": [
          "PLAY"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true
        }
      },
      {
        "label": "Click the rocker up",
        "detail": "Catch the previous beat.",
        "hold": [
          "PLAY"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "left",
        "lights": [],
        "pattern": "rockTap",
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true
        }
      },
      {
        "label": "Keep PLAY held",
        "detail": "The captured window repeats.",
        "hold": [
          "PLAY"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true
        }
      },
      {
        "label": "ROCKER UP halves",
        "detail": "Click again or hold to keep stepping.",
        "hold": [
          "PLAY"
        ],
        "tap": [],
        "faders": {},
        "rock": -1,
        "view": "left",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true
        }
      },
      {
        "label": "ROCKER DOWN doubles",
        "detail": "Within the audible window.",
        "hold": [
          "PLAY"
        ],
        "tap": [],
        "faders": {},
        "rock": 1,
        "view": "left",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true
        }
      },
      {
        "label": "Release PLAY",
        "detail": "The tape shift rounds to the nearest bar on a gridded song.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true
        }
      },
      {
        "label": "Playback continues",
        "detail": "Short stutters may leave the position unchanged.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true
        }
      }
    ]
  },
  {
    "id": "repeat-chop",
    "category": 5,
    "group": "Catch & resize",
    "title": "Beat repeat within a chop",
    "gesture": "PLAY + rocker inside your chop",
    "body": "The repeat only uses your chopped audio. It cannot reach outside the audible cycle.",
    "note": "If a requested beat would fill the chop: ROCKER UP starts with a quarter of it; ROCKER DOWN with half, down to the minimum size.",
    "steps": [
      {
        "label": "With a chop playing, hold PLAY",
        "detail": "A 1/16-loop chop is already playing. One grid beat is longer than this chop.",
        "hold": [
          "PLAY"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true
        }
      },
      {
        "label": "Click ROCKER UP",
        "detail": "The beat cannot fit, so ROCKER UP captures one quarter of the audible chop.",
        "hold": [
          "PLAY"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "left",
        "lights": [],
        "pattern": "rockTap",
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true
        }
      },
      {
        "label": "Keep PLAY held",
        "detail": "The repeat stays inside your chop.",
        "hold": [
          "PLAY"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true
        }
      }
    ]
  },
  {
    "id": "release-repeat",
    "category": 5,
    "group": "Catch & resize",
    "title": "Beat repeat release",
    "gesture": "Release PLAY with FN released",
    "body": "On a gridded song, playback resumes from the repeat, with the tape shift rounded to the nearest bar.",
    "note": "A short stutter may leave the position unchanged. Without a grid, or during a bounce, release returns to the running tape position.",
    "steps": [
      {
        "label": "The repeat is active",
        "detail": "Keep FN released for this gesture.",
        "hold": [
          "PLAY"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true
        }
      },
      {
        "label": "Release PLAY",
        "detail": "The tape shift rounds to the nearest bar on a gridded song.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true
        }
      },
      {
        "label": "Playback continues",
        "detail": "Short stutters may leave the position unchanged.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true
        }
      }
    ]
  },
  {
    "id": "repeat-no-grid",
    "category": 5,
    "group": "Catch & resize",
    "title": "Beat repeat without a grid",
    "gesture": "Hold PLAY + rocker: ROCKER UP = 1/8 · ROCKER DOWN = 1/4",
    "body": "Without a grid, hold PLAY and click the rocker up for one eighth of the loop, or down for one quarter. Chop limits still apply.",
    "note": "On release, return to the running tape position. There is no bar-based tape hold.",
    "steps": [
      {
        "label": "With no grid, hold PLAY",
        "detail": "",
        "hold": [
          "PLAY"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Click ROCKER UP",
        "detail": "Start at 1/8 of the loop, limited by the chop.",
        "hold": [
          "PLAY"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "left",
        "lights": [],
        "pattern": "rockTap",
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Release PLAY",
        "detail": "Return to the running tape position.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      }
    ]
  },
  {
    "id": "move-repeat",
    "category": 5,
    "group": "Shape the repeat",
    "title": "Beat repeat position",
    "gesture": "Keep PLAY held + VOL− / VOL+",
    "body": "VOL− moves earlier. VOL+ moves later. Each press shifts by one current window length inside the audible cycle.",
    "note": "During a repeat these controls move its window, not FX routing. Set routing before catching the repeat.",
    "steps": [
      {
        "label": "Keep PLAY held",
        "detail": "",
        "hold": [
          "PLAY"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true
        }
      },
      {
        "label": "Tap VOL+",
        "detail": "Move later by one window length.",
        "hold": [
          "PLAY"
        ],
        "tap": [
          "VOL+"
        ],
        "faders": {},
        "rock": 0,
        "view": "top",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true
        }
      },
      {
        "label": "Tap VOL−",
        "detail": "Move earlier.",
        "hold": [
          "PLAY"
        ],
        "tap": [
          "VOL−"
        ],
        "faders": {},
        "rock": 0,
        "view": "top",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true
        }
      }
    ]
  },
  {
    "id": "repeat-octave",
    "category": 5,
    "group": "Shape the repeat",
    "title": "Beat repeat octave jumps",
    "gesture": "Keep PLAY held + FN + rocker",
    "body": "Add FN and click the rocker. Down is an octave lower; up is an octave higher.",
    "note": "",
    "steps": [
      {
        "label": "Keep PLAY held; add FN",
        "detail": "",
        "hold": [
          "PLAY",
          "FN"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [],
        "pattern": null,
        "duration": 0.6,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true
        }
      },
      {
        "label": "Click the rocker",
        "detail": "Down halves the tape speed; the repeat stays active.",
        "hold": [
          "PLAY",
          "FN"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "left",
        "lights": [],
        "pattern": "rockTap",
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true
        },
        "rockDirection": "DOWN"
      },
      {
        "label": "Lift FN; keep PLAY held",
        "detail": "The rocker resizes the repeat again.",
        "hold": [
          "PLAY"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true
        }
      }
    ]
  },
  {
    "id": "repeat-faders",
    "category": 5,
    "group": "Shape the repeat",
    "title": "Beat repeat fader controls",
    "gesture": "Keep PLAY held + FN + F1–F4",
    "body": "F1 = start. F2 = end. F3 = position. F4 = size / zoom. Move deliberately to engage a fader.",
    "note": "F3 keeps width. F4 tightens around the center; top restores the region set by F1–F3. Re-gripping FN preserves that region.",
    "steps": [
      {
        "label": "Keep PLAY held; add FN",
        "detail": "",
        "hold": [
          "PLAY",
          "FN"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true
        }
      },
      {
        "label": "Set edges with F1 / F2",
        "detail": "Work inside the audible cycle.",
        "hold": [
          "PLAY",
          "FN"
        ],
        "tap": [],
        "faders": {
          "1": 0.2,
          "2": 0.75
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true
        }
      },
      {
        "label": "F3 slides the repeat",
        "detail": "The width stays the same.",
        "hold": [
          "PLAY",
          "FN"
        ],
        "tap": [],
        "faders": {
          "3": 0.6
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true
        }
      },
      {
        "label": "F4 tightens",
        "detail": "Move back to the top to restore the region.",
        "hold": [
          "PLAY",
          "FN"
        ],
        "tap": [],
        "faders": {
          "4": 0.1
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true
        }
      },
      {
        "label": "Lift FN; keep PLAY held",
        "detail": "The rocker now resizes again.",
        "hold": [
          "PLAY"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true
        }
      }
    ]
  },
  {
    "id": "wah-resonance",
    "category": 3,
    "group": "Secondary controls",
    "title": "Wah resonance",
    "gesture": "Page 2: hold T3 and move F3",
    "body": "While holding T3, move F3 down for a sharper peak or up for a softer peak.",
    "note": "Move before releasing the track button; releasing without movement triggers its usual tap action. Then cross the main value to regain control.",
    "steps": [
      {
        "label": "Hold FN + T2",
        "detail": "Keep both held until Page 2 opens.",
        "hold": [
          "FN",
          "T2"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": null,
        "duration": 0.6,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Release both",
        "detail": "Page 2 stays open.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": "page2",
        "duration": 1,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Page 2 is open",
        "detail": "The side lights identify Page 2.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": "page2",
        "duration": 2.5,
        "page": 2,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Hold T3",
        "detail": "Relative edits: down sharper; up softer.",
        "hold": [
          "T3"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": null,
        "duration": 1.6,
        "page": 2,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Change wah resonance",
        "detail": "Relative edits: down sharper; up softer.",
        "hold": [
          "T3"
        ],
        "tap": [],
        "faders": {
          "3": 0.12
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 2,
        "lane": 3,
        "amount": 0.12,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Move back up",
        "detail": "Relative edits: down sharper; up softer.",
        "hold": [
          "T3"
        ],
        "tap": [],
        "faders": {
          "3": 0.9
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 2,
        "lane": 3,
        "amount": 0.9,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Release",
        "detail": "",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 1.4,
        "page": 2,
        "lane": 3,
        "amount": 0.6,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      }
    ],
    "initialFaders": [
      0.5,
      0.5,
      0.5,
      0.5
    ]
  },
  {
    "id": "echo-feedback",
    "category": 3,
    "group": "Secondary controls",
    "title": "Echo feedback",
    "gesture": "Page 2: hold T4 and move F4",
    "body": "Change the amount fed back into the echo.",
    "note": "Move before releasing the track button; releasing without movement triggers its usual tap action. Then cross the main value to regain control.",
    "steps": [
      {
        "label": "Hold FN + T2",
        "detail": "Keep both held until Page 2 opens.",
        "hold": [
          "FN",
          "T2"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": null,
        "duration": 0.6,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Release both",
        "detail": "Page 2 stays open.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": "page2",
        "duration": 1,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Page 2 is open",
        "detail": "The side lights identify Page 2.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": "page2",
        "duration": 2.5,
        "page": 2,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Hold T4",
        "detail": "Change the amount fed back into the echo.",
        "hold": [
          "T4"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": null,
        "duration": 1.6,
        "page": 2,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Change echo feedback",
        "detail": "Change the amount fed back into the echo.",
        "hold": [
          "T4"
        ],
        "tap": [],
        "faders": {
          "4": 0.12
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 2,
        "lane": 4,
        "amount": 0.12,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Move back up",
        "detail": "Change the amount fed back into the echo.",
        "hold": [
          "T4"
        ],
        "tap": [],
        "faders": {
          "4": 0.9
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 2,
        "lane": 4,
        "amount": 0.9,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Release",
        "detail": "",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 1.4,
        "page": 2,
        "lane": 4,
        "amount": 0.6,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      }
    ],
    "initialFaders": [
      0.5,
      0.5,
      0.5,
      0.5
    ]
  },
  {
    "id": "tremolo-shape",
    "category": 3,
    "group": "Secondary controls",
    "title": "Tremolo shape",
    "gesture": "Page 3: hold T3 and move F3",
    "body": "Move from softer to harder tremolo shapes.",
    "note": "Move before releasing the track button; releasing without movement triggers its usual tap action. Then cross the main value to regain control.",
    "steps": [
      {
        "label": "Hold FN + T3",
        "detail": "Keep both held until Page 3 opens.",
        "hold": [
          "FN",
          "T3"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": null,
        "duration": 0.6,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Release both",
        "detail": "Page 3 stays open.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": "page3",
        "duration": 1,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Page 3 is open",
        "detail": "The side lights identify Page 3.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": "page3",
        "duration": 2.5,
        "page": 3,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Hold T3",
        "detail": "Move from softer to harder tremolo shapes.",
        "hold": [
          "T3"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": null,
        "duration": 1.6,
        "page": 3,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Change tremolo shape",
        "detail": "Move from softer to harder tremolo shapes.",
        "hold": [
          "T3"
        ],
        "tap": [],
        "faders": {
          "3": 0.12
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 3,
        "lane": 3,
        "amount": 0.12,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Move back up",
        "detail": "Move from softer to harder tremolo shapes.",
        "hold": [
          "T3"
        ],
        "tap": [],
        "faders": {
          "3": 0.9
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 3,
        "lane": 3,
        "amount": 0.9,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Release",
        "detail": "",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 1.4,
        "page": 3,
        "lane": 3,
        "amount": 0.6,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      }
    ],
    "initialFaders": [
      0.5,
      0.5,
      0.5,
      0.5
    ]
  },
  {
    "id": "tremolo-spread",
    "category": 3,
    "group": "Secondary controls",
    "title": "Tremolo stereo spread",
    "gesture": "Page 3: hold T3 and move F4",
    "body": "Change the spread between the two channels. F4 does not change reverb while T3 is held.",
    "note": "Move before releasing the track button; releasing without movement triggers its usual tap action. Then cross the main value to regain control.",
    "steps": [
      {
        "label": "Hold FN + T3",
        "detail": "Keep both held until Page 3 opens.",
        "hold": [
          "FN",
          "T3"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": null,
        "duration": 0.6,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Release both",
        "detail": "Page 3 stays open.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": "page3",
        "duration": 1,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Page 3 is open",
        "detail": "The side lights identify Page 3.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": "page3",
        "duration": 2.5,
        "page": 3,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Hold T3",
        "detail": "Change the spread between the two channels. F4 does not change reverb while T3 is held.",
        "hold": [
          "T3"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": null,
        "duration": 1.6,
        "page": 3,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Change tremolo stereo spread",
        "detail": "Change the spread between the two channels. F4 does not change reverb while T3 is held.",
        "hold": [
          "T3"
        ],
        "tap": [],
        "faders": {
          "4": 0.12
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 3,
        "lane": 3,
        "amount": 0.12,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Move back up",
        "detail": "Change the spread between the two channels. F4 does not change reverb while T3 is held.",
        "hold": [
          "T3"
        ],
        "tap": [],
        "faders": {
          "4": 0.9
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 3,
        "lane": 3,
        "amount": 0.9,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Release",
        "detail": "",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 1.4,
        "page": 3,
        "lane": 3,
        "amount": 0.6,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      }
    ],
    "initialFaders": [
      0.5,
      0.5,
      0.5,
      0.5
    ]
  },
  {
    "id": "reverb-damping",
    "category": 3,
    "group": "Secondary controls",
    "title": "Reverb damping",
    "gesture": "Page 3: hold T4 and move F4",
    "body": "Down is darker; up is brighter.",
    "note": "Move before releasing the track button; releasing without movement triggers its usual tap action. Then cross the main value to regain control.",
    "steps": [
      {
        "label": "Hold FN + T3",
        "detail": "Keep both held until Page 3 opens.",
        "hold": [
          "FN",
          "T3"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": null,
        "duration": 0.6,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Release both",
        "detail": "Page 3 stays open.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": "page3",
        "duration": 1,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Page 3 is open",
        "detail": "The side lights identify Page 3.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": "page3",
        "duration": 2.5,
        "page": 3,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Hold T4",
        "detail": "Down is darker; up is brighter.",
        "hold": [
          "T4"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": null,
        "duration": 1.6,
        "page": 3,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Change reverb damping",
        "detail": "Down is darker; up is brighter.",
        "hold": [
          "T4"
        ],
        "tap": [],
        "faders": {
          "4": 0.12
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 3,
        "lane": 4,
        "amount": 0.12,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Move back up",
        "detail": "Down is darker; up is brighter.",
        "hold": [
          "T4"
        ],
        "tap": [],
        "faders": {
          "4": 0.9
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 2.5,
        "page": 3,
        "lane": 4,
        "amount": 0.9,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Release",
        "detail": "",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": "lane-depth",
        "duration": 1.4,
        "page": 3,
        "lane": 4,
        "amount": 0.6,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      }
    ],
    "initialFaders": [
      0.5,
      0.5,
      0.5,
      0.5
    ]
  },
  {
    "category": 1,
    "group": "Everyday controls",
    "id": "track-lights",
    "title": "Track lights",
    "gesture": "Dark · dim · blinking · solid",
    "body": "Dark means empty; dim means muted. Recording is solid. Playback pulses with the grid, or when each loop wraps.",
    "note": "When stopped, an unmuted loaded track stays solid. Examples use 120 BPM and selected loop lengths. A refusal shrug can mean saving is still busy: wait for completion before deleting or recording again. Alternating T1+T3 / T2+T4 indicates a fault; stop testing and keep the device connected for diagnostics. In USB drive mode, T2 + T3 stay lit. After eject: T1 checks, T2 plans, T3 imports, T4 saves. Long imports can take several minutes; keep USB connected until Tape Looper returns. Repeated all-four flashes in drive mode indicate an error, not import progress.",
    "steps": [
      {
        "label": "Empty",
        "detail": "No audio on this track.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "trackLights",
        "lights": [],
        "pattern": null,
        "duration": 2.5
      },
      {
        "label": "Muted",
        "detail": "Loaded audio is kept at a dim glow.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "trackLights",
        "lights": [],
        "pattern": "muted",
        "duration": 2.5
      },
      {
        "label": "Armed",
        "detail": "Waiting for sound; short beat pulses.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "trackLights",
        "lights": [],
        "pattern": "arm",
        "duration": 2.5
      },
      {
        "label": "Recording",
        "detail": "The recording track stays solid.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "trackLights",
        "lights": [
          "T1"
        ],
        "pattern": null,
        "duration": 2.5
      },
      {
        "label": "Playing without a grid",
        "detail": "Example: loop lengths of 2, 3, 4 and 6 seconds pulse at their own wraps.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "trackLights",
        "lights": [],
        "pattern": "wraps",
        "duration": 3
      },
      {
        "label": "Playing with a grid",
        "detail": "Loaded tracks pulse together on each beat.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "trackLights",
        "lights": [],
        "pattern": "grid-playing",
        "duration": 2.5
      },
      {
        "label": "Stopped",
        "detail": "Loaded, unmuted tracks stay solid.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "trackLights",
        "lights": [
          "T1",
          "T2",
          "T3",
          "T4"
        ],
        "pattern": null,
        "duration": 2.5
      }
    ]
  },
  {
    "category": 1,
    "group": "Songs & settings",
    "id": "status-lights",
    "title": "Side lights",
    "gesture": "Playing: level · FN held: song",
    "body": "During playback, the meter fills from the bottom. Hold FN to see the song: solid = bank, blinking = position.",
    "note": "If bank and position share a light, that light blinks. With a running grid the song blink follows the beat.",
    "steps": [
      {
        "label": "Audio level",
        "detail": "Example signal: the meter fills bottom-up.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": "vu",
        "duration": 2.5
      },
      {
        "label": "Hold FN for the song",
        "detail": "Example: bank 1, position 2.",
        "hold": [
          "FN"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "sideLights",
        "lights": [],
        "pattern": "song",
        "duration": 2.5
      }
    ]
  },
  {
    "category": 4,
    "group": "Page 7 / Take & timing",
    "id": "grid-record-lights",
    "title": "Grid recording indicators",
    "gesture": "Half-beat flashes",
    "body": "An armed track flashes at half-beat rate while waiting for punch-in. The same quicker pulse marks a recording finishing its beat.",
    "note": "The recording continues until its aligned stop; it is not stuck.",
    "steps": [
      {
        "label": "Waiting, then recording",
        "detail": "T2 flashes at half-beat rate until the next punch-in, then stays solid while recording.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "trackLights",
        "lights": [],
        "pattern": "grid-wait",
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true
        }
      },
      {
        "label": "Finishing, then looping",
        "detail": "T2 flashes at half-beat rate until the next beat ends the take, then pulses with playback.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "trackLights",
        "lights": [],
        "pattern": "grid-finish",
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true
        }
      }
    ]
  },
  {
    "id": "tape-copy",
    "category": 5,
    "group": "Bounce what you hear",
    "title": "Tape copy / bounce",
    "gesture": "Hold PLAY → briefly tap destination",
    "body": "Record the playing mix onto a spare track. Hold PLAY and tap the destination; tap it again to finish. This is a tape copy.",
    "note": "A loaded destination is replaced. Tape copy follows tape speed with its source. A grid may add count-out; fixed mode and chop can extend the stop to a full cycle.",
    "steps": [
      {
        "label": "Hold PLAY",
        "detail": "Keep PLAY held.",
        "hold": [
          "PLAY"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [],
        "pattern": null,
        "duration": 1.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Briefly tap T2",
        "detail": "T2 is the destination in this example.",
        "hold": [
          "PLAY"
        ],
        "tap": [
          "T2"
        ],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [],
        "pattern": null,
        "duration": 2.8,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false,
          "recording": [
            2
          ]
        }
      },
      {
        "label": "Release the controls",
        "detail": "",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [],
        "pattern": null,
        "duration": 1.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false,
          "recording": [
            2
          ]
        }
      },
      {
        "label": "Tap T2 again",
        "detail": "Stop the copy.",
        "hold": [],
        "tap": [
          "T2"
        ],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": null,
        "duration": 2,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2
          ],
          "grid": false
        }
      },
      {
        "label": "Release",
        "detail": "The copy is now a separate recording. Your source track is still there.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": null,
        "duration": 1.5,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2
          ],
          "grid": false
        }
      },
      {
        "label": "Mute the original",
        "detail": "Tap T1 to hear the copy without doubling the source.",
        "hold": [],
        "tap": [
          "T1"
        ],
        "faders": {},
        "rock": 0,
        "view": "tracks",
        "lights": [],
        "pattern": null,
        "duration": 1.5,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2
          ],
          "grid": false
        }
      },
      {
        "label": "Bring up the copy",
        "detail": "Fader 2 controls your new recording. You can now reshape or layer it.",
        "hold": [],
        "tap": [],
        "faders": {
          "2": 1
        },
        "rock": 0,
        "view": "faders",
        "lights": [],
        "pattern": null,
        "duration": 1.5,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2
          ],
          "grid": false
        }
      }
    ]
  },
  {
    "id": "print-repeat",
    "category": 5,
    "group": "Bounce what you hear",
    "title": "Beat repeat bounce",
    "gesture": "Hold PLAY → click ROCKER UP → tap destination",
    "body": "Hold PLAY and click the rocker up to catch a repeat. Keep PLAY held and tap a spare track to start the bounce. Release PLAY to let the song flow back in; tap the destination again to finish.",
    "note": "Keep PLAY held to resize while recording. A loaded destination is replaced.",
    "steps": [
      {
        "label": "Hold PLAY and catch a repeat",
        "detail": "Click the rocker up.",
        "hold": [
          "PLAY"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "left",
        "lights": [],
        "pattern": "rockTap",
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true
        }
      },
      {
        "label": "Tap spare destination T2",
        "detail": "Start a tape copy of the stutter.",
        "hold": [
          "PLAY"
        ],
        "tap": [
          "T2"
        ],
        "faders": {},
        "rock": 0,
        "view": "tracks",
        "lights": [
          "T2"
        ],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true,
          "recording": [
            2
          ]
        }
      },
      {
        "label": "Keep PLAY held",
        "detail": "You can keep resizing while recording.",
        "hold": [
          "PLAY"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true,
          "recording": [
            2
          ]
        }
      },
      {
        "label": "Release PLAY",
        "detail": "The song flows back in; the copy keeps recording.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [
          "T2"
        ],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true,
          "recording": [
            2
          ]
        }
      },
      {
        "label": "Tap T2 to finish",
        "detail": "End the copy.",
        "hold": [],
        "tap": [
          "T2"
        ],
        "faders": {},
        "rock": 0,
        "view": "tracks",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2
          ],
          "grid": true
        }
      }
    ]
  },
  {
    "id": "sampler",
    "category": 5,
    "group": "Bounce what you hear",
    "title": "Sampler print",
    "gesture": "Hold PLAY → hold destination",
    "body": "Hold PLAY, then hold the destination track to record a sampler print. Release the track to finish.",
    "note": "The heard speed is baked for playback at 1×. Use a spare destination: a loaded track is replaced.",
    "steps": [
      {
        "label": "Hold PLAY",
        "detail": "Then add your destination track.",
        "hold": [
          "PLAY"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        }
      },
      {
        "label": "Hold T2",
        "detail": "Keep the destination held while you record.",
        "hold": [
          "PLAY",
          "T2"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "tracks",
        "lights": [
          "T2"
        ],
        "pattern": null,
        "duration": 3,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false,
          "recording": [
            2
          ]
        }
      },
      {
        "label": "Release T2",
        "detail": "The sampler print ends.",
        "hold": [
          "PLAY"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "tracks",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2
          ],
          "grid": false
        }
      },
      {
        "label": "Release PLAY",
        "detail": "",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2
          ],
          "grid": false
        }
      }
    ]
  },
  {
    "id": "stop-page",
    "category": 5,
    "group": "Bounce what you hear",
    "title": "Bounce controls in FX pages",
    "gesture": "PLAY + destination track",
    "body": "Use PLAY + the destination track again to stop a bounce while a page is open.",
    "note": "",
    "steps": [
      {
        "label": "Hold PLAY",
        "detail": "Keep PLAY held.",
        "hold": [
          "PLAY"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [],
        "pattern": null,
        "duration": 1.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false,
          "recording": [
            2
          ]
        },
        "page": 2
      },
      {
        "label": "Tap the destination",
        "detail": "Stop the copy.",
        "hold": [
          "PLAY"
        ],
        "tap": [
          "T2"
        ],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [],
        "pattern": null,
        "duration": 2.8,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        },
        "page": 2
      },
      {
        "label": "Release the controls",
        "detail": "",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [],
        "pattern": null,
        "duration": 1.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false
        },
        "page": 2
      }
    ]
  },
  {
    "id": "flow-back",
    "category": 5,
    "group": "Bounce what you hear",
    "title": "Bounce after beat repeat",
    "gesture": "After starting the copy, release PLAY",
    "body": "The repeat ends and the tape copy continues from the running tape position. Tap the destination again to stop.",
    "note": "",
    "steps": [
      {
        "label": "Your repeat is being copied",
        "detail": "Example destination: T2.",
        "hold": [
          "PLAY"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [
          "T2"
        ],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true,
          "recording": [
            2
          ]
        }
      },
      {
        "label": "Release PLAY",
        "detail": "The song flows back in; the copy keeps recording.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [
          "T2"
        ],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true,
          "recording": [
            2
          ]
        }
      },
      {
        "label": "Tap T2 to finish",
        "detail": "End the copy.",
        "hold": [],
        "tap": [
          "T2"
        ],
        "faders": {},
        "rock": 0,
        "view": "tracks",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2
          ],
          "grid": true
        }
      }
    ]
  },
  {
    "id": "print-speed",
    "category": 5,
    "group": "Bounce what you hear",
    "title": "Recorded speed changes",
    "gesture": "During a bounce: release PLAY → rocker",
    "body": "Use the rocker while a tape copy or sampler print is recording. Your speed moves become part of the performance.",
    "note": "Tape copy records speed moves relative to starting speed. Sampler print bakes the performance for playback at 1×.",
    "steps": [
      {
        "label": "During a tape copy, leave PLAY released",
        "detail": "Use the rocker for speed.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [
          "T2"
        ],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false,
          "recording": [
            2
          ]
        }
      },
      {
        "label": "Sweep the tape",
        "detail": "Hold the rocker to change speed.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": -1,
        "view": "left",
        "lights": [
          "T2"
        ],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false,
          "recording": [
            2
          ]
        }
      },
      {
        "label": "Release the rocker",
        "detail": "The copy continues.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [
          "T2"
        ],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": false,
          "recording": [
            2
          ]
        }
      }
    ]
  },
  {
    "id": "held-print",
    "category": 5,
    "group": "Bounce what you hear",
    "title": "Sampler print controls",
    "gesture": "Keep the destination held · PLAY changes the rocker",
    "body": "Keep the destination held. With PLAY released, the rocker changes speed. With PLAY held, it controls beat repeat. Add FN for octave jumps.",
    "note": "Release the destination to end the print. Release PLAY too to end the repeat.",
    "steps": [
      {
        "label": "Keep destination T2 held",
        "detail": "This keeps the sampler print recording.",
        "hold": [
          "T2"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "tracks",
        "lights": [
          "T2"
        ],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true,
          "recording": [
            2
          ]
        }
      },
      {
        "label": "With PLAY up: rocker = speed",
        "detail": "",
        "hold": [
          "T2"
        ],
        "tap": [],
        "faders": {},
        "rock": -1,
        "view": "left",
        "lights": [
          "T2"
        ],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true,
          "recording": [
            2
          ]
        }
      },
      {
        "label": "Add PLAY: rocker = repeat",
        "detail": "",
        "hold": [
          "T2",
          "PLAY"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "left",
        "lights": [
          "T2"
        ],
        "pattern": "rockTap",
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true,
          "recording": [
            2
          ]
        }
      },
      {
        "label": "Add FN for an octave",
        "detail": "Down halves the tape speed; the repeat stays active.",
        "hold": [
          "T2",
          "PLAY",
          "FN"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "left",
        "lights": [
          "T2"
        ],
        "pattern": "rockTap",
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1
          ],
          "grid": true,
          "recording": [
            2
          ]
        },
        "rockDirection": "DOWN"
      },
      {
        "label": "Release T2 and PLAY",
        "detail": "End the print and repeat.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": null,
        "duration": 2.5,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2
          ],
          "grid": true
        }
      }
    ]
  },
  {
    "id": "scissors386",
    "category": 2,
    "group": "Rocker modes",
    "title": "Cut / glue",
    "gesture": "Slot 7 · ROCKER DOWN marks, ROCKER DOWN cuts · ROCKER UP undoes",
    "body": "While playing, tap ROCKER DOWN at the start and again at the end of a section to skip it. With the rocker page open, hold the same TRACK for both marks to cut only that track. Bare ROCKER DOWN targets the loaded tracks.",
    "note": "Make up to eight cuts. ROCKER UP cancels a pending mark or undoes the last cut. Cuts remain when leaving the rocker page or slot. FN + double-tap PLAY restores the tape. Cuts are non-destructive session edits; changing songs, takes, recording or rebooting clears them. Use full forward loops, without trim, Heads, repeat or recording.",
    "steps": [
      {
        "label": "Open the rocker page",
        "detail": "Hold FN and both volume buttons, then release.",
        "hold": [
          "FN",
          "VOL−",
          "VOL+"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "comboTop",
        "lights": [],
        "pattern": null,
        "duration": 3,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2
          ],
          "grid": true
        },
        "seconds": 3.5
      },
      {
        "label": "Select Scissors / glue",
        "detail": "From Magnet in slot 6, press VOL+ to select slot 7.",
        "hold": [],
        "tap": [
          "VOL+"
        ],
        "faders": {},
        "rock": 0,
        "view": "top",
        "lights": [],
        "pattern": null,
        "duration": 3,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2
          ],
          "grid": true
        },
        "seconds": 3.5
      },
      {
        "label": "Mark the start on the full tape",
        "detail": "With playback running, tap ROCKER DOWN. The mark applies to all loaded tracks.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 1,
        "view": "left",
        "lights": [],
        "pattern": null,
        "duration": 3,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2
          ],
          "grid": true
        },
        "seconds": 3.5
      },
      {
        "label": "Make the cut",
        "detail": "Tap ROCKER DOWN again to skip the section between the marks.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 1,
        "view": "left",
        "lights": [],
        "pattern": null,
        "duration": 3,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2
          ],
          "grid": true
        },
        "seconds": 3.5
      },
      {
        "label": "Undo the cut",
        "detail": "Tap ROCKER UP to put the section back.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": -1,
        "view": "left",
        "lights": [],
        "pattern": null,
        "duration": 3,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2
          ],
          "grid": true
        },
        "seconds": 3.5
      },
      {
        "label": "Mark a cut on Track 1",
        "detail": "With the rocker page still open, hold T1 and tap ROCKER DOWN.",
        "hold": [
          "T1"
        ],
        "tap": [],
        "faders": {},
        "rock": 1,
        "view": "left",
        "lights": [],
        "pattern": null,
        "duration": 3,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2
          ],
          "grid": true
        },
        "seconds": 3.5
      },
      {
        "label": "Cut only Track 1",
        "detail": "Keep holding T1 and tap ROCKER DOWN again. The other tracks keep their full length.",
        "hold": [
          "T1"
        ],
        "tap": [],
        "faders": {},
        "rock": 1,
        "view": "left",
        "lights": [],
        "pattern": null,
        "duration": 3,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2
          ],
          "grid": true
        },
        "seconds": 3.5
      },
      {
        "label": "Restore the tape",
        "detail": "Hold FN and double-tap PLAY to clear the remaining cut.",
        "hold": [
          "FN"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [],
        "pattern": "playDouble",
        "duration": 3,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2
          ],
          "grid": true
        },
        "seconds": 3.5
      }
    ],
    "since": "4.0"
  },
  {
    "id": "bluetooth-v4",
    "category": 1,
    "group": "Connect & wake",
    "title": "Bluetooth Support",
    "gesture": "Triple-tap FN and hold the last press until all four track lights start fading in and out, then release",
    "body": "Triple-tap FN and hold the last press until all four track lights start fading in and out, then release to enable Bluetooth audio discovery. Pair SP-1 from your phone or computer, select it as the audio output, then record normally.",
    "note": "The gesture also works during playback and USB audio. USB and Bluetooth input mix together. This release focuses on Bluetooth input; outgoing audio to speakers or keyboards is deferred.",
    "steps": [
      {
        "label": "Enable Bluetooth audio",
        "detail": "Triple-tap FN and hold the last press until all four track lights start fading in and out, then release. The track lights fill T1 to T4, then fade together.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "comboHold",
        "lights": [],
        "pattern": null,
        "duration": 3,
        "context": {
          "playing": false,
          "loaded": [],
          "grid": false
        }
      },
      {
        "label": "Pair from your audio source",
        "detail": "All four track lights fade together while pairing. Choose SP-1 in Bluetooth settings. The lights sweep once when connected.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "trackLights",
        "lights": [],
        "pattern": null,
        "duration": 3,
        "context": {
          "playing": false,
          "loaded": [],
          "grid": false
        }
      },
      {
        "label": "Record the sound",
        "detail": "Tap empty Track 3 and start Bluetooth audio. Its light turns solid when recording starts.",
        "hold": [],
        "tap": [
          "T3"
        ],
        "faders": {},
        "rock": 0,
        "view": "tracks",
        "lights": [],
        "pattern": null,
        "duration": 3,
        "context": {
          "playing": false,
          "loaded": [],
          "grid": false
        }
      },
      {
        "label": "Finish the take",
        "detail": "Tap Track 3 again to finish. The take joins the loop.",
        "hold": [],
        "tap": [
          "T3"
        ],
        "faders": {},
        "rock": 0,
        "view": "tracks",
        "lights": [],
        "pattern": null,
        "duration": 3,
        "context": {
          "playing": false,
          "loaded": [],
          "grid": false
        }
      }
    ],
    "since": "4.0"
  },
  {
    "id": "rocker-v4",
    "category": 2,
    "group": "Rocker modes",
    "title": "Rocker modes",
    "gesture": "FN + both VOL · VOL− / VOL+ to choose",
    "body": "Hold FN and both volume buttons to open the rocker page. Release FN, then use VOL− / VOL+ to select a mode. Tap FN to close; the selected mode stays active.",
    "note": "1 Pitch / speed · 2 FF / RW · 3 Tape bending · 4 Battery drain · 5 Sandpaper · 6 Magnet erase · 7 Cut / glue · 8 Manual heads. Keep the rocker page open for per-track Magnet or Scissors gestures. Holding TRACK previews the normal track lights.",
    "steps": [
      {
        "label": "Open the rocker page",
        "detail": "Hold FN + VOL− + VOL+.",
        "hold": [
          "FN",
          "VOL−",
          "VOL+"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [],
        "pattern": null,
        "duration": 3,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2
          ],
          "grid": true
        }
      },
      {
        "label": "Choose a mode",
        "detail": "Release FN. VOL+ moves forward; VOL− moves back.",
        "hold": [],
        "tap": [
          "VOL+"
        ],
        "faders": {},
        "rock": 0,
        "view": "trackLights",
        "lights": [],
        "pattern": null,
        "duration": 3,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2
          ],
          "grid": true
        }
      },
      {
        "label": "Return to tracks",
        "detail": "Tap FN. The selected rocker mode stays active.",
        "hold": [],
        "tap": [
          "FN"
        ],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [],
        "pattern": null,
        "duration": 3,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2
          ],
          "grid": true
        }
      }
    ],
    "since": "4.0"
  },
  {
    "id": "wind-v4",
    "category": 2,
    "group": "Rocker modes",
    "title": "FF / RW",
    "gesture": "Slot 2 · hold ROCKER UP / ROCKER DOWN",
    "body": "Hold ROCKER UP to fast-forward or ROCKER DOWN to rewind. Release to let the tape settle back.",
    "note": "",
    "steps": [
      {
        "label": "Hold ROCKER DOWN, then release",
        "detail": "Hold ROCKER UP to fast-forward or ROCKER DOWN to rewind. Release to let the tape settle back.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 1,
        "view": "left",
        "lights": [],
        "pattern": null,
        "duration": 3,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2
          ],
          "grid": true
        }
      },
      {
        "label": "Release or restore",
        "detail": "Release the rocker. For a complete rocker reset, hold FN and double-tap PLAY.",
        "hold": [
          "FN"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [],
        "pattern": "playDouble",
        "duration": 3,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2
          ],
          "grid": true
        }
      }
    ],
    "since": "4.0"
  },
  {
    "id": "bender-v4",
    "category": 2,
    "group": "Rocker modes",
    "title": "Tape bending",
    "gesture": "Slot 3 · hold ROCKER UP / ROCKER DOWN",
    "body": "Hold ROCKER DOWN to drag and stretch the tape. Hold ROCKER UP for repeated pulling surges. Release either direction for a spring-like recovery.",
    "note": "Longer ROCKER DOWN holds stretch further. This changes playback; it does not rewrite your recordings.",
    "steps": [
      {
        "label": "Hold ROCKER DOWN, then release",
        "detail": "Hold ROCKER DOWN to drag and stretch the tape. Hold ROCKER UP for repeated pulling surges. Release either direction for a spring-like recovery.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 1,
        "view": "left",
        "lights": [],
        "pattern": null,
        "duration": 3,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2
          ],
          "grid": true
        }
      },
      {
        "label": "Release or restore",
        "detail": "Release the rocker. For a complete rocker reset, hold FN and double-tap PLAY.",
        "hold": [
          "FN"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [],
        "pattern": "playDouble",
        "duration": 3,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2
          ],
          "grid": true
        }
      }
    ],
    "since": "4.0"
  },
  {
    "id": "battery-v4",
    "category": 2,
    "group": "Rocker modes",
    "title": "Battery drain",
    "gesture": "Slot 4 · hold ROCKER DOWN / ROCKER UP",
    "body": "Hold ROCKER DOWN to increase battery-drain sag, wobble and stalls. Hold ROCKER UP to recover toward normal.",
    "note": "The wear stays when you release. FN + double-tap PLAY restores the tape.",
    "steps": [
      {
        "label": "Hold ROCKER DOWN, then release",
        "detail": "Hold ROCKER DOWN to increase battery-drain sag, wobble and stalls. Hold ROCKER UP to recover toward normal.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 1,
        "view": "left",
        "lights": [],
        "pattern": null,
        "duration": 3,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2
          ],
          "grid": true
        }
      },
      {
        "label": "Release or restore",
        "detail": "Release the rocker. For a complete rocker reset, hold FN and double-tap PLAY.",
        "hold": [
          "FN"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [],
        "pattern": "playDouble",
        "duration": 3,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2
          ],
          "grid": true
        }
      }
    ],
    "since": "4.0"
  },
  {
    "id": "sandpaper-v4",
    "category": 2,
    "group": "Rocker modes",
    "title": "Sandpaper",
    "gesture": "Slot 5 · hold ROCKER DOWN / ROCKER UP",
    "body": "Hold ROCKER DOWN to add roughness, breakup and tape drag. Hold ROCKER UP to reduce the wear.",
    "note": "The wear stays when you release. FN + double-tap PLAY restores the tape.",
    "steps": [
      {
        "label": "Hold ROCKER DOWN, then release",
        "detail": "Hold ROCKER DOWN to add roughness, breakup and tape drag. Hold ROCKER UP to reduce the wear.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 1,
        "view": "left",
        "lights": [],
        "pattern": null,
        "duration": 3,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2
          ],
          "grid": true
        }
      },
      {
        "label": "Release or restore",
        "detail": "Release the rocker. For a complete rocker reset, hold FN and double-tap PLAY.",
        "hold": [
          "FN"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [],
        "pattern": "playDouble",
        "duration": 3,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2
          ],
          "grid": true
        }
      }
    ],
    "since": "4.0"
  },
  {
    "id": "magnet-v4",
    "category": 2,
    "group": "Rocker modes",
    "title": "Magnet erase",
    "gesture": "Slot 6 · hold ROCKER DOWN to erase · ROCKER UP to undo",
    "body": "Hold ROCKER DOWN while playing to silence and mark the section passing under the head. Release to leave a gap without shortening the loop. Tap ROCKER UP to undo. With the rocker page open, hold TRACK to target only that track.",
    "note": "Unlike Scissors, Magnet keeps the loop’s length. FN + double-tap PLAY restores all Magnet gaps. Song changes and new recordings can clear the affected temporary gaps.",
    "steps": [
      {
        "label": "Hold ROCKER DOWN, then release",
        "detail": "Hold ROCKER DOWN to erase a section across the loaded tracks. Release to leave a gap without shortening the loop.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 1,
        "view": "left",
        "lights": [],
        "pattern": null,
        "duration": 3,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2
          ],
          "grid": true
        }
      },
      {
        "label": "Release or restore",
        "detail": "Release the rocker. For a complete rocker reset, hold FN and double-tap PLAY.",
        "hold": [
          "FN"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [],
        "pattern": "playDouble",
        "duration": 3,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2
          ],
          "grid": true
        }
      }
    ],
    "since": "4.0"
  },
  {
    "id": "manual-head-v4",
    "category": 2,
    "group": "Rocker modes",
    "title": "Manual heads",
    "gesture": "Slot 8 · hold ROCKER DOWN + move faders",
    "body": "Hold ROCKER UP to make contact with the running tape. Hold ROCKER DOWN for four-head contact; move F1–F4 to position the heads. Release to lift contact and silence the tape.",
    "note": "The transport keeps moving while contact is lifted. Leave this rocker mode to return to ordinary playback. This is separate from latching Heads with FN + triple-tap PLAY.",
    "steps": [
      {
        "label": "Hold ROCKER UP for tape contact",
        "detail": "The transport keeps moving. Hold ROCKER UP to hear the tape.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": -1,
        "view": "left",
        "lights": [],
        "pattern": null,
        "duration": 3,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2
          ],
          "grid": true
        },
        "seconds": 3
      },
      {
        "label": "Hold ROCKER DOWN for four heads",
        "detail": "Keep ROCKER DOWN held and move F1–F4 to position the four heads.",
        "hold": [],
        "tap": [],
        "faders": {
          "1": 0.1,
          "2": 0.8,
          "3": 0.3,
          "4": 0.95
        },
        "rock": 1,
        "view": "comboRocker",
        "lights": [],
        "pattern": null,
        "duration": 3,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2
          ],
          "grid": true
        },
        "seconds": 3
      },
      {
        "label": "Release to lift contact",
        "detail": "The tape becomes silent while transport keeps moving.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "left",
        "lights": [],
        "pattern": null,
        "duration": 3,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2
          ],
          "grid": true
        },
        "seconds": 3
      },
      {
        "label": "Leave Manual tape head",
        "detail": "With the rocker page open, use VOL− to choose another slot and return to ordinary playback.",
        "hold": [],
        "tap": [
          "VOL−"
        ],
        "faders": {},
        "rock": 0,
        "view": "left",
        "lights": [],
        "pattern": null,
        "duration": 3,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2
          ],
          "grid": true
        },
        "seconds": 3
      }
    ],
    "since": "4.0"
  },
  {
    "id": "restore-tape-v4",
    "category": 2,
    "group": "Rocker modes",
    "title": "Tape reset",
    "gesture": "Hold FN + double-tap PLAY",
    "body": "Restore normal rocker tape behavior: clear Scissors cuts and Magnet changes, release wind and reset speed.",
    "note": "Your recordings and effect-page settings stay intact.",
    "steps": [
      {
        "label": "Hold FN",
        "detail": "Keep FN held.",
        "hold": [
          "FN"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [],
        "pattern": null,
        "duration": 3,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2
          ],
          "grid": true
        }
      },
      {
        "label": "Double-tap PLAY",
        "detail": "Restore the tape. Release the controls.",
        "hold": [
          "FN"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "right",
        "lights": [],
        "pattern": "playDouble",
        "duration": 3,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2
          ],
          "grid": true
        }
      }
    ],
    "since": "4.0"
  },
  {
    "id": "track-trim-v4",
    "category": 2,
    "group": "Track trim",
    "title": "Track trim",
    "gesture": "Hold FN + PLAY · move that track’s fader",
    "body": "Hold FN and PLAY, then move F1–F4 to shorten or lengthen the matching track. Lower the fader to shorten; raise it to lengthen. Release both to keep the trim.",
    "note": "The first small move catches the current length without jumping. With a grid, lengths step through straight and dotted musical values. Other tracks keep playing; the original recording stays intact.",
    "since": "4.0",
    "initialFaders": [
      0.85,
      0.8,
      0.8,
      0.8
    ],
    "steps": [
      {
        "label": "Hold FN + PLAY",
        "detail": "Keep both buttons held. Each fader targets its own track.",
        "hold": [
          "FN",
          "PLAY"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": null,
        "duration": 2,
        "seconds": 2.2,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2
          ],
          "grid": true
        }
      },
      {
        "label": "Shorten Track 1",
        "detail": "Keep FN + PLAY held and lower F1. Only Track 1 loops over a shorter piece.",
        "hold": [
          "FN",
          "PLAY"
        ],
        "tap": [],
        "faders": {
          "1": 0.6
        },
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": null,
        "duration": 2,
        "seconds": 2.2,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2
          ],
          "grid": true
        }
      },
      {
        "label": "Keep the trim",
        "detail": "Release both buttons. The shorter loop stays; faders return to level control with pickup.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": null,
        "duration": 2,
        "seconds": 2.2,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2
          ],
          "grid": true
        }
      }
    ]
  },
  {
    "id": "track-slide-v4",
    "category": 2,
    "group": "Track trim",
    "title": "Trim position",
    "gesture": "Hold PLAY · move the trimmed track’s fader",
    "body": "Choose which part of a trimmed recording plays, without changing its length. Hold PLAY and move the track’s fader: raise it toward the recording’s start; lower it toward the end.",
    "note": "The first small move catches the current position. With a grid, movement follows sixteenth-note steps. Release PLAY to keep the position; moving the fader spends the PLAY press, so it does not pause or restart playback.",
    "since": "4.0",
    "initialFaders": [
      0.8,
      0.8,
      0.8,
      0.8
    ],
    "steps": [
      {
        "label": "Start with a trimmed track",
        "detail": "Trim the track first, then choose which part plays.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": null,
        "duration": 2,
        "seconds": 2.2,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2
          ],
          "grid": true
        }
      },
      {
        "label": "Hold PLAY and slide",
        "detail": "Hold PLAY, then lower F1 to move the kept section later in Track 1.",
        "hold": [
          "PLAY"
        ],
        "tap": [],
        "faders": {
          "1": 0.35
        },
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": null,
        "duration": 2,
        "seconds": 2.2,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2
          ],
          "grid": true
        }
      },
      {
        "label": "Keep its position",
        "detail": "Release PLAY. The section keeps its length and new position.",
        "hold": [],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": null,
        "duration": 2,
        "seconds": 2.2,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2
          ],
          "grid": true
        }
      }
    ]
  },
  {
    "id": "track-trim-reset-v4",
    "category": 4,
    "group": "Page 7 / Timing",
    "title": "Restore a track’s full length",
    "gesture": "On Page 7 · tap the track",
    "body": "Open Page 7 and tap a trimmed track to restore its full recording. Other track trims stay set.",
    "note": "Page 7: hold FN + T4 to open Page 4, release, then hold FN and tap VOL+ three times. A trimmed track’s tap clears its trim first; later taps can reset its nudge or that lane’s timing setting.",
    "since": "4.0",
    "steps": [
      {
        "label": "Open Page 7",
        "detail": "Hold FN + T4 for Page 4, release, then hold FN and tap VOL+ three times.",
        "hold": [
          "FN",
          "T4"
        ],
        "tap": [],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": null,
        "duration": 2,
        "seconds": 2.2,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2
          ],
          "grid": true
        }
      },
      {
        "label": "Restore Track 1",
        "detail": "Release FN, then tap T1. Its full recording plays again; Track 2 keeps its trim.",
        "hold": [],
        "tap": [
          "T1"
        ],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": null,
        "duration": 2,
        "seconds": 2.2,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2
          ],
          "grid": true
        },
        "page": 7
      },
      {
        "label": "Return to tracks",
        "detail": "Tap FN to close the timing page.",
        "hold": [],
        "tap": [
          "FN"
        ],
        "faders": {},
        "rock": 0,
        "view": "front",
        "lights": [],
        "pattern": null,
        "duration": 2,
        "seconds": 2.2,
        "context": {
          "playing": true,
          "loaded": [
            1,
            2
          ],
          "grid": true
        }
      }
    ]
  }
];

import {updateFxGuide,finishGuideAudit} from './fx-guide552.mjs?v=624-copy';
updateFxGuide(cards);

finishGuideAudit(cards);

// Keep the full four-bar countdown at its original speed within six-second captions.
{
 const card=cards.find(card=>card.id==='countdown');
 if(card?.steps.length===1){
  const original=card.steps[0];
  card.steps=[{...original,label:'Watch the remaining bars',detail:'The track lights count down the four remaining bars.',seconds:4},
   {...original,label:'The last bar and finish',detail:'The last bar blinks. When recording finishes, the grid chase returns.',seconds:4.75}];
 }
}

// Minimal teaching fixtures: a second playing track is only used for comparison.
{
 const singleTrackLessons=new Set(['takes-v4','replace','rocker-v4','wind-v4','bender-v4','battery-v4','sandpaper-v4','magnet-v4','manual-head-v4','restore-tape-v4','track-trim-v4','track-slide-v4']);
 for(const card of cards)if(singleTrackLessons.has(card.id))for(const step of card.steps){
  if(step.context?.loaded)step.context={...step.context,loaded:[1]};
 }
}

// Walkthrough captions focus on the control and its immediate result.
{
 const captions={
  power:{1:'The side lights fill as SP-1 wakes.',2:'Release to finish startup.',3:'Hold FN for 2.5 seconds. The side lights fill, then clear.',4:'SP-1 is off.',5:'The side lights show charge; the last lit LED blinks while charging.'},
  transport:{2:'Playback stays in its new state.',3:'Release to restart from the beginning.'},
  levels:{1:'F1–F4 control tracks 1–4.',3:'Raise F1 to restore its level.'},
  'takes-v4':{1:'Hold T1 and click ROCKER UP. Your current take stays saved.',2:'Release both, then tap T1 to arm. Tap again to finish.',3:'Hold T1 and click ROCKER DOWN to cycle takes at the running song position.',4:'During playback, hold FN. Each track light counts its saved takes.'},
  replace:{1:'Hold for 0.6 seconds to prepare overdub.',2:'Tap the track within 10 seconds to arm overdub.',4:'The overdub becomes another take; the earlier take stays saved.'},
  delete:{2:'The deleted take’s light stays dark.'},
  songs:{4:'When stopped, solid shows the bank; blinking shows the song.'},
  brightness:{1:'Hold FN + PLAY for five seconds.'},
  octave:{2:'Halve the speed: one octave down.',3:'Double the speed: one octave up.'},
  'move-window':{1:'Keep FN held while moving the loop window.'},
  'four-faders':{3:'Slide the section without changing its length.',4:'Lower F4 to shorten the section around its center.',5:'Restore the section set by F1–F3.'},
  'reset-window':{1:'Keep both held while resetting the window.',4:'Keep both held while moving the window home.',5:'Move home, keeping the window’s size and direction.'},
  isolate:{1:'Hold FN + PLAY before pressing a track.',3:'Restore the previous track mutes.'},
  heads:{3:'Faders control head levels; empty heads pulse dimly.',5:'Keep FN held and move F2 to position head 2.'},
  'head-source':{1:'In four-head mode, keep FN held to position a head.',2:'Move F2 to position head 2.',3:'Raise F2 to move head 2 back.'},
  'round-grid':{1:'Tap FN at least four times in time.',2:'Hold FN for about one second to round to the nearest whole BPM.',4:'The track lights sweep out and back to confirm rounding.'},
  pickup:{2:'Cross the saved value to take control of the effect.'},
  'tap-rate':{1:'On the effect’s page, keep FN held.',3:'Hold FN + PLAY, then tap the effect’s track button.'},
  secondaries:{1:'On Page 2, hold T4 to adjust echo feedback.',2:'Keep T4 held and move F4 to change feedback.',3:'Keep T4 held and raise F4 to adjust feedback.'},
  routing:{1:'Keep PLAY held while choosing an FX route.'},
  monitor:{1:'Hold PLAY + VOL− + VOL+ to toggle input monitoring.'},
  'reset-fx':{1:'Keep FN held while resetting the page.'},
  'center-placement':{1:'Center track 1.',2:'The track stays centered.'},
  preset:{3:'Normal track controls return.'},
  nudge:{1:'On Page 7, hold T1 to adjust its timing.',2:'Keep T1 held and move F1 to shift its timing.',3:'Keep T1 held and raise F1 to shift it back.'},
  'nudge-lights':{1:'Move F1 while holding T1. Its light shows the timing offset.'},
  'reset-timing':{1:'Center track 1’s timing; if centered, clear the grid offset.',2:'The reset stays applied.'},
  format:{1:'Hold both until Page 4 opens.',8:'Choose mono or stereo for track 1’s next take.',9:'Stereo blinks; mono stays solid.'},
  'repeat-chop':{1:'Keep PLAY held to repeat part of the chop.',2:'If a full beat cannot fit, ROCKER UP catches one quarter of the chop.'},
  'tremolo-spread':{4:'Keep T3 held to adjust stereo spread.',5:'Move F4 to change the spread between channels.',6:'Keep T3 held and raise F4 to adjust the spread.'},
  'track-lights':{5:'Each track pulses when its loop wraps.'},
  'status-lights':{1:'The meter fills from bottom to top.',2:'Solid shows the bank; blinking shows the song.'},
  'tape-copy':{2:'Start a copy of the playing mix on T2.',5:'The copy is saved separately; the source stays saved.',7:'Raise F2 to bring the copy into the mix.'},
  'stop-page':{1:'Keep PLAY held on the FX page.'},
  'flow-back':{1:'T2 is recording the repeat.'},
  'scissors386':{2:'Use VOL− / VOL+ to choose Scissors / glue.'},
  'bluetooth-v4':{1:'Triple-tap FN and hold the last press until all four track lights fade, then release.',2:'Choose SP-1 in Bluetooth settings. The lights sweep when connected.',3:'Tap empty T3 and start Bluetooth audio. T3 turns solid as recording starts.',4:'Tap T3 again. The take joins the loop.'},
  'wind-v4':{1:'Hold ROCKER DOWN to rewind; release to let the tape settle back.',2:'Release the rocker. Hold FN and double-tap PLAY to reset.'},
  'bender-v4':{1:'Hold ROCKER DOWN to stretch the tape; release for a spring-like recovery.',2:'Release the rocker. Hold FN and double-tap PLAY to reset.'},
  'battery-v4':{1:'Hold ROCKER DOWN to add sag, wobble and stalls.',2:'Release the rocker. Hold FN and double-tap PLAY to reset.'},
  'sandpaper-v4':{1:'Hold ROCKER DOWN to add roughness, breakup and drag.',2:'Release the rocker. Hold FN and double-tap PLAY to reset.'},
  'magnet-v4':{1:'Hold ROCKER DOWN to erase a section; release to leave a gap.',2:'Release the rocker. Hold FN and double-tap PLAY to reset.'},
  'manual-head-v4':{1:'Hold ROCKER UP to hear the moving tape.',4:'Open the rocker page and use VOL− to select another mode.'},
  'track-trim-v4':{1:'Keep both held; each fader trims its own track.',2:'Lower F1 to shorten track 1’s loop.',3:'Release both. The trim stays; cross the saved level to mix again.'},
  'track-slide-v4':{1:'Trim a track before moving its section.',2:'Hold PLAY and lower F1 to move track 1’s section later.',3:'Release PLAY. The section keeps its length and position.'},
  'beats-per-bar-v4':{5:'Move F3 through its saved position to take control.',6:'Raise F3 to select 5. Bottom to top: 3, 4, 5, 7; T3 counts the choice.',7:'Tap FN to close. The song keeps its beats-per-bar setting.'},
  'escape-hatch-v4':{1:'Hold all three for two seconds; solid track lights confirm the reset.'}
 };
 for(const card of cards)for(const [index,detail]of Object.entries(captions[card.id]||{}))card.steps[Number(index)-1].detail=detail;
}
{
 const edits={
  eq:{5:'F2 controls the 350 Hz band.',6:'Lower F2 to cut the band.',7:'Raise F2; center is flat and above center boosts.'},
  placement:{6:'F1 controls track 1’s stereo position.',7:'Lower F1 to move track 1 left.',8:'Raise F1 to move track 1 right.'},
  offset:{7:'F1 controls the grid offset.',8:'Lower F1 to move the grid earlier.',9:'Raise F1 to move the grid later.'},
  'wah-resonance':{4:'Keep T3 held while moving F3.',5:'Lower F3 for sharper resonance.',6:'Raise F3 for softer resonance.'},
  'echo-feedback':{4:'Keep T4 held while moving F4.',5:'Move F4 to change echo feedback.',6:'Keep T4 held and raise F4 to adjust feedback.'},
  'tremolo-shape':{4:'Keep T3 held while moving F3.',5:'Move F3 to change the tremolo shape.',6:'Keep T3 held and raise F3 to adjust the shape.'},
  'reverb-damping':{4:'Keep T4 held while moving F4.',5:'Lower F4 for darker reverb.',6:'Raise F4 for brighter reverb.'},
  'filter-damping-v4':{4:'Keep T1 held while moving F1.',5:'Move F1 to change filter damping.',6:'Keep T1 held and raise F1 to adjust damping.'},
  'chorus-span-v4':{4:'Keep T2 held while moving F2.',5:'Move F2 to change chorus / flanger span.',6:'Keep T2 held and raise F2 to adjust the span.'},
  'phaser-feedback-v4':{4:'Keep T1 held while moving F1.',5:'Move F1 to change phaser feedback.',6:'Keep T1 held and raise F1 to adjust feedback.'}
 };
 for(const card of cards)for(const [index,detail]of Object.entries(edits[card.id]||{}))card.steps[Number(index)-1].detail=detail;
}
