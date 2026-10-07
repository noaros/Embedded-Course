# CUSTOM_STARTUP: the BSP's startup file is NOT linked; yours is.
course_firmware(lab02_starter BOARD h723 CUSTOM_STARTUP
  SOURCES main.c startup.c
  LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/stm32h723.ld)
