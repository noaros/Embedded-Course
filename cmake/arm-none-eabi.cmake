# Toolchain file for arm-none-eabi-gcc (Arm GNU Toolchain 13 or newer).
# CPU-specific flags are NOT set here: each board adds its own through the
# cpu_<board> interface targets in cmake/course.cmake, so one build tree can
# hold Cortex-M7 and Cortex-M33 firmware side by side.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# Linking a full executable during compiler checks would fail without a
# linker script, so only build a static library to test the compiler.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(TOOLCHAIN_PREFIX arm-none-eabi-)
if(DEFINED ENV{ARM_GCC_PATH})
  set(TOOLCHAIN_PREFIX $ENV{ARM_GCC_PATH}/arm-none-eabi-)
endif()

set(CMAKE_C_COMPILER   ${TOOLCHAIN_PREFIX}gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}g++)
set(CMAKE_ASM_COMPILER ${TOOLCHAIN_PREFIX}gcc)
set(CMAKE_OBJCOPY      ${TOOLCHAIN_PREFIX}objcopy CACHE FILEPATH "objcopy")
set(CMAKE_SIZE         ${TOOLCHAIN_PREFIX}size    CACHE FILEPATH "size")

set(CMAKE_EXECUTABLE_SUFFIX_C   .elf)
set(CMAKE_EXECUTABLE_SUFFIX_CXX .elf)
set(CMAKE_EXECUTABLE_SUFFIX_ASM .elf)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
