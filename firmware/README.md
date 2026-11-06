# Firmware source layout

The v4 runtime is in `tools/capacity422/runtime`, the filesystem/USB-drive image in `tools/maintenance425`, and the Bluetooth programming bridge in `tools/clean608/radio`. This directory contains shared eMMC source used by those builds.

These directory names preserve the tested build's relative include paths. They are source components, not a collection of historical BINs. See [BUILDING.md](../BUILDING.md) for the entry points, pinned dependencies and excluded external inputs. Older releases remain in Git tags.
