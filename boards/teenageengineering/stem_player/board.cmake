# SP-1 has no SWD header populated; flashing is normally done via the
# Track1+Track4 USB bootloader and solderless.engineering. These runners
# only matter if you ever wire up SWD.
board_runner_args(jlink "--device=nRF52840_xxAA" "--speed=4000")
include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
