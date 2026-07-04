cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(sp1-looper)
include(${ZEPHYR_BASE}/samples/subsys/usb/common/common.cmake)
target_sources(app PRIVATE src/main.c src/sp1_emmc.c)

# -fno-strict-aliasing: the firmware type-puns static uint8_t[] storage through
# int16_t* / struct* lvalues (the batchbuf copies and the meta-block cast). This
# removes the TBAA assumption so those puns stay correct under -O2 -- the same
# idiom the Linux kernel and Zephyr core build with. Purely subtractive; it can
# only remove an optimizer assumption, never miscompile correct code.
target_compile_options(app PRIVATE -fno-strict-aliasing)

# NOTE: -O2 on sp1_emmc.c was REVERTED. It sped up reads but BROKE the timing-
# sensitive flash WRITE bit-bang -- ~half of writes failed CRC and retried, which
# overflowed the record ring under record+play load (corruption/glitching). The
# eMMC driver stays at the global -Os, where the write timing is correct.
