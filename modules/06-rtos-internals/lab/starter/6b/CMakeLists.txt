course_use_freertos(${CMAKE_CURRENT_SOURCE_DIR}/../../freertos h723 GCC_ARM_CM4F)

course_firmware(lab06b_starter BOARD h723
  SOURCES main.c ../../freertos/trace.c
  INCLUDES ../../freertos
  LIBS freertos_kernel)
