# External components

Original Tape Looper code and third-party components have separate licenses. Preserve the root LICENSE, the notices in LICENSES and upstream source headers.

## Firmware

Zephyr, Nordic HAL/nrfx, CMSIS, Wakes-derived storage code, FatFs, SBC decoding and dr_libs retain their own terms. Their pinned inputs and notices accompany the source.

## Bluetooth

Infineon/Cypress SDKs, vendor-derived templates, the programming helper and generated radio images are external to this source checkout. The release BIN and Setup contain compiled Bluetooth components with separate terms. See BLUETOOTH.md and external-dependencies.lock.json. A source link or the project MIT license does not replace third-party terms.

## Browser decoder

The shipped audio-only FFmpeg 5.1.4 configuration reports LGPL 2.1 or later; the ffmpeg.wasm wrapper uses MIT and Emscripten retains its notices. The matching source archives, configuration and build scripts are supplied as audio-decoder-source.tar.gz with the GitHub release and at https://sp1-tape-looper.vercel.app/licenses/ . Rebuild and replace the paired ffmpeg-core.js/ffmpeg-core.wasm to use a modified decoder. Debugging modifications to these open-source components is not restricted by this project.

## 3D viewer

Three.js uses MIT; its license is included under web/vendor/three/LICENSE and LICENSES.
