course_firmware(lab02_solution BOARD h723 CUSTOM_STARTUP
  SOURCES main.c startup.c fw_header.c
  LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/stm32h723.ld)
# Stretch goal: a 20-byte SHA-1 build ID that the firmware header points at.
target_link_options(lab02_solution PRIVATE -Wl,--build-id=sha1)
