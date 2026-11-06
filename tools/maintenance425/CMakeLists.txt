cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(sp1_music_restore377)
include(${ZEPHYR_BASE}/samples/subsys/usb/common/common.cmake)
target_include_directories(app PRIVATE ../../firmware/src .)
target_sources(app PRIVATE main.c ../../firmware/src/sp1_emmc.c ../commit421/runtime/src/pool.c ../commit421/runtime/src/take.c ../commit421/runtime/src/control.c ../commit421/runtime/src/controls.c ../commit421/runtime/src/snapshot.c ../commit421/runtime/src/geometry.c)
target_include_directories(app PRIVATE ../capacity422/maintenance ../capacity422/paged ../commit421/runtime/src ../library-writeback)
foreach(module store catalog allocate io music session preview enumerate manifest directory wav view)
 target_sources(app PRIVATE ../capacity422/paged/${module}.c)
endforeach()
foreach(module arena drive fat import publication discover editor)
 target_sources(app PRIVATE ../capacity422/maintenance/${module}.c)
endforeach()
target_sources(app PRIVATE ../library-writeback/overlay.c ../library-writeback/wav.c ../library-writeback/pack.c)
target_compile_options(app PRIVATE -fstack-usage -Werror=infinite-recursion)

# Replace only the SCSI object to acknowledge eject after durable flush.
get_target_property(msc_sources subsys__usb__device_next SOURCES)
list(FILTER msc_sources EXCLUDE REGEX "usbd_msc_scsi.c$")
set_property(TARGET subsys__usb__device_next PROPERTY SOURCES "${msc_sources}")
target_sources(app PRIVATE usbd_msc_scsi.c)
target_include_directories(app PRIVATE ${ZEPHYR_BASE}/subsys/usb/device_next/class)

target_compile_definitions(app PRIVATE SP1_BOUNDED_SYNC_DMA=1)

# Offline converter occupies its own checked controller-flash region. No runtime linkage.

zephyr_linker_sources(SECTIONS ../import469/converter-sections.ld)
add_library(converter469 OBJECT ../library-writeback/wav.c)
target_link_libraries(converter469 PRIVATE zephyr_interface)
target_compile_options(converter469 PRIVATE -fstack-usage)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/converter469.o
 COMMAND ${CMAKE_OBJCOPY} --prefix-alloc-sections=.conv469 $<TARGET_OBJECTS:converter469> ${CMAKE_CURRENT_BINARY_DIR}/converter469.o
 DEPENDS converter469 $<TARGET_OBJECTS:converter469> COMMAND_EXPAND_LISTS VERBATIM)
get_target_property(import469_sources app SOURCES)
list(FILTER import469_sources EXCLUDE REGEX "library-writeback/wav.c$")
set_property(TARGET app PROPERTY SOURCES "${import469_sources}")
target_sources(app PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/converter469.o)

target_compile_definitions(app PRIVATE IMPORT469=1)

#504: flash-only CRC acceleration is exclusive to offline maintenance.
target_sources(app PRIVATE ../import504/crc.c ../import504/crc513.c)
target_compile_definitions(app PRIVATE SP1_MAINTENANCE_CRC504=1)

# On-device initialization is offline only and borrows existing drive scratch.
zephyr_linker_sources(SECTIONS ../fresh529/sections.ld)
add_library(fresh529 OBJECT ../fresh529/fresh.c)
target_link_libraries(fresh529 PRIVATE zephyr_interface)
target_compile_options(fresh529 PRIVATE -fstack-usage)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/fresh529.o
 COMMAND ${CMAKE_OBJCOPY} --prefix-alloc-sections=.fresh529 $<TARGET_OBJECTS:fresh529> ${CMAKE_CURRENT_BINARY_DIR}/fresh529.o
 DEPENDS fresh529 $<TARGET_OBJECTS:fresh529> COMMAND_EXPAND_LISTS VERBATIM)
target_sources(app PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/fresh529.o)

#593: bounded native staging parser, no audio linkage or static storage.
target_sources(app PRIVATE ../native593/native.c)
