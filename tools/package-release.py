#!/usr/bin/env python3
"""Assemble Tape Looper 4.0 from three linked ELFs, without older BINs.

Source packaging verification. Never flashes or publishes.
Requires pyelftools and the matching arm-zephyr-eabi binutils.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import zlib
from elftools.elf.elffile import ELFFile

EXPECTED = 'e73e3786c1a2b33cf92685f92533404f5eec78f4f9384607725bf7c2140badbf'
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--runtime', required=True, type=Path)
p.add_argument('--maintenance', required=True, type=Path)
p.add_argument('--radio', required=True, type=Path)
p.add_argument('--dispatch', required=True, type=Path)
p.add_argument('--toolchain-prefix', required=True)
p.add_argument('--output', required=True, type=Path)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
parts, reports = {}, {}
specs = [
    ('runtime', a.runtime, 0x20000, 0x7c000, 262044, {}),
    ('maintenance', a.maintenance, 0x80000, 0xa0000, 255300,
     {'.converter469': (0xb4000, 0xce000), '.fresh529': (0xce000, 0xdc000)}),
    ('radio', a.radio, 0xa0000, 0xb3c00, 56768, {'.exit524': (0xdc000, 0xde000)}),
]
for name, path, low, high, ram, sections in specs:
    with path.open('rb') as stream:
        elf = ELFFile(stream)
        symbols = {s.name: s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
        assert symbols['_image_ram_size'] == ram, (name, 'RAM changed')
        ranges = [(low, high), *sections.values()]
        for seg in elf.iter_segments():
            if seg['p_type'] == 'PT_LOAD' and seg['p_filesz']:
                assert any(start <= seg['p_paddr'] and seg['p_paddr'] + seg['p_filesz'] <= end
                           for start, end in ranges), (name, 'load outside allowed flash')
        for section, (start, end) in sections.items():
            data = elf.get_section_by_name(section)
            assert data and data['sh_addr'] == start and start + data['sh_size'] <= end
            parts[start] = data.data()
    dest = a.output / (name + '.bin')
    subprocess.run([a.toolchain_prefix + 'objcopy', '-O', 'binary',
                    *(['--gap-fill', '0xff', '--remove-section=.comment', '--remove-section=COMMON']
                      if name == 'runtime' else []),
                    *['--remove-section=' + s for s in sections], str(path), str(dest)], check=True)
    parts[low] = dest.read_bytes()
    assert low + len(parts[low]) <= high
    reports[name] = dict(ram=ram, main_bytes=len(parts[low]),
                         main_sha256=hashlib.sha256(parts[low]).hexdigest())

reset = struct.unpack_from('<I', parts[0x20000], 4)[0]
dispatch_obj, dispatch_elf, dispatch_bin = [a.output / ('dispatch.' + ext) for ext in ('o', 'elf', 'bin')]
for command in [
    ['as', '-mcpu=cortex-m4', '-mthumb', '--defsym', f'NORMAL_RESET={reset}', str(a.dispatch), '-o', str(dispatch_obj)],
    ['ld', '-Ttext=0x7f000', '-e', 'dispatch', str(dispatch_obj), '-o', str(dispatch_elf)],
    ['objcopy', '-O', 'binary', str(dispatch_elf), str(dispatch_bin)],
]:
    subprocess.run([a.toolchain_prefix + command[0], *command[1:]], check=True)
parts[0x7f000] = dispatch_bin.read_bytes()
assert len(parts[0x7f000]) < 4096
end = (0xdc000 + len(parts[0xdc000]) + 3) & ~3
image = bytearray(b'\xff' * (end - 0x20000))
for address, data in parts.items():
    image[address - 0x20000:address - 0x20000 + len(data)] = data
struct.pack_into('<I', image, 4, 0x7f001)
# Preserving build: no erase request. The wake marker requests automatic startup.
struct.pack_into('<I', image, 0x7c014 - 0x20000, 0x35334b57)
converter = parts[0xb4000]
struct.pack_into('<5I', image, 0xb3c00 - 0x20000,
                 0x36344d49, 0x00315639, 0xb4000, len(converter), zlib.crc32(converter))
digest = hashlib.sha256(image).hexdigest()
report = dict(bytes=len(image), sha256=digest, matches_release=digest == EXPECTED,
              preserving=True, historical_bins_used=False, parts=reports)
(a.output / 'package-report.json').write_text(json.dumps(report, indent=2) + '\n')
if digest != EXPECTED:
    raise SystemExit('Rebuild differs from approved v4 release; report saved, no release BIN emitted')
(a.output / 'TAPE-LOOPER-V4.bin').write_bytes(image)
print(json.dumps(report, indent=2))
