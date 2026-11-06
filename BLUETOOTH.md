# Bluetooth components

Tape Looper 4.0 supports Bluetooth audio input and Live Companion connectivity. Setup checks the radio and updates it when needed.

The release BIN includes a vendor-derived programming helper; browser Setup supplies the compiled radio application separately. These components use Infineon/Cypress inputs with separate terms and are not relicensed under Tape Looper's MIT license.

The public source omits vendor SDK sources/libraries, vendor-derived application profiles/templates, programming-helper payloads, generated radio binaries and private device backups. See [the pinned external inputs](external-dependencies.lock.json), [build limitations](BUILDING.md) and [component notices](LICENSES).

An SP-1 Bluetooth SDK and accompanying agent skill are currently in development to share integration code, protocol documentation, build instructions and tested examples.
