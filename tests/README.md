# Control and telemetry checks

Run `node tests/tone-controls.mjs` and `node tests/armed-pause.mjs` (Node 22+). These exercise pickup, independent Tone/Compression values, reset, old/new snapshot decoding and armed-recording pause behavior. They do not access hardware.
