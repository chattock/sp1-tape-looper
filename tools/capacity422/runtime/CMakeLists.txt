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

target_sources_ifdef(CONFIG_SP1_BT_ID_PROBE app PRIVATE src/sp1_bt_probe.c)

# Integration296: maintained source-tree runtime; no build-time source swapping.
set(SP1_BT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/bt)
target_sources(app PRIVATE ${SP1_BT_DIR}/runtime/usb_midi.c)
file(GLOB SP1_SBC_SOURCES ${SP1_BT_DIR}/audio-pipeline/decoder/srce/*.c)
list(FILTER SP1_SBC_SOURCES EXCLUDE REGEX "(synthesis-dct8|decoder-private|synthesis-8-generated).c$")
target_sources(app PRIVATE ${SP1_BT_DIR}/runtime/synthesis-dct8.c ${SP1_BT_DIR}/runtime/decoder-private.c ${SP1_BT_DIR}/runtime/synthesis-8-generated.c ${SP1_SBC_SOURCES} ${SP1_BT_DIR}/runtime/audio_decode.c)
target_include_directories(app PRIVATE ${SP1_BT_DIR}/runtime ${SP1_BT_DIR}/audio-pipeline ${SP1_BT_DIR}/audio-pipeline/decoder/include ${SP1_BT_DIR}/audio-pipeline/decoder/srce)
set_source_files_properties(${SP1_SBC_SOURCES} ${SP1_BT_DIR}/runtime/synthesis-dct8.c ${SP1_BT_DIR}/runtime/decoder-private.c ${SP1_BT_DIR}/runtime/synthesis-8-generated.c PROPERTIES COMPILE_OPTIONS "-fwrapv")
# Candidate344: optimize only SBC decoding and its PCM mix wrapper for speed.
# Keep signed-wrap semantics for codec arithmetic. Never change timing-sensitive
# eMMC/USB/control compilation as a side effect of this experiment.
set_property(SOURCE ${SP1_SBC_SOURCES} ${SP1_BT_DIR}/runtime/synthesis-dct8.c ${SP1_BT_DIR}/runtime/decoder-private.c ${SP1_BT_DIR}/runtime/synthesis-8-generated.c ${SP1_BT_DIR}/runtime/audio_decode.c APPEND PROPERTY COMPILE_OPTIONS -O2)
get_target_property(SP1_CDC_SOURCES subsys__usb__device_next SOURCES)
list(FILTER SP1_CDC_SOURCES EXCLUDE REGEX "usbd_cdc_acm.c$")
set_property(TARGET subsys__usb__device_next PROPERTY SOURCES ${SP1_CDC_SOURCES})
target_sources(subsys__usb__device_next PRIVATE ${SP1_BT_DIR}/runtime/usbd_cdc_acm.c)
target_compile_options(app PRIVATE -fstack-usage)

target_sources(app PRIVATE src/storage373.c)

target_compile_definitions(app PRIVATE FS_FATFS_WINDOW_ALIGNMENT=4)
target_sources(app PRIVATE src/ff.c src/pool.c src/take.c src/session.c src/control.c src/edit.c src/runtime.c src/store_core.c src/admit.c src/backend.c src/audit.c)

target_sources(app PRIVATE src/handoff.c)

target_sources(app PRIVATE src/geometry.c)

target_sources(app PRIVATE src/engine.c src/controls.c src/snapshot.c)

target_sources(app PRIVATE src/cooperative.c)

# Pin414 USB class error-payload fix locally; do not alter the shared SDK.
get_target_property(SP1_UAC_SOURCES subsys__usb__device_next SOURCES)
list(FILTER SP1_UAC_SOURCES EXCLUDE REGEX "usbd_uac2.c$")
set_property(TARGET subsys__usb__device_next PROPERTY SOURCES ${SP1_UAC_SOURCES})
target_sources(subsys__usb__device_next PRIVATE ${SP1_BT_DIR}/runtime/usbd_uac2.c)

# Paged capacity candidate: all metadata buffers overlay the existing batch.
target_sources(app PRIVATE src/paged_bridge.c ../paged/store.c ../paged/catalog.c ../paged/allocate.c ../paged/io.c ../paged/music.c ../paged/session.c ../paged/preview.c)

set(SP1_CAPACITY_BUILD 422 CACHE STRING "Capacity diagnostic identity")
target_compile_definitions(app PRIVATE SP1_CAPACITY_BUILD=${SP1_CAPACITY_BUILD})
