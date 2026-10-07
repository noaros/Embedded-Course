# Shared build helpers for every lab in the course.
#
#   cpu_h723 / cpu_l552   INTERFACE targets: CPU flags, device define, CMSIS includes
#   bsp_h723 / bsp_l552   INTERFACE targets: clock, UART, syscalls (+ startup unless CUSTOM_STARTUP)
#   course_firmware(...)  builds an .elf, then .bin/.hex, a map file and a size report
#
# Third-party code is downloaded with FetchContent at configure time and pinned
# to release tags. To work offline, point FetchContent at local checkouts, e.g.
#   -DFETCHCONTENT_SOURCE_DIR_CMSIS_CORE=/path/to/CMSIS_6

include(FetchContent)

# The CMSIS repositories have no CMakeLists.txt we want to run; pointing
# SOURCE_SUBDIR at a directory that does not exist makes
# FetchContent_MakeAvailable download them without add_subdirectory().
FetchContent_Declare(cmsis_core
  GIT_REPOSITORY https://github.com/ARM-software/CMSIS_6.git
  GIT_TAG        v6.2.0
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  _no_cmake_)
FetchContent_Declare(cmsis_device_h7
  GIT_REPOSITORY https://github.com/STMicroelectronics/cmsis_device_h7.git
  GIT_TAG        v1.10.7
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  _no_cmake_)
FetchContent_Declare(cmsis_device_l5
  GIT_REPOSITORY https://github.com/STMicroelectronics/cmsis_device_l5.git
  GIT_TAG        v1.0.7
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  _no_cmake_)
FetchContent_MakeAvailable(cmsis_core cmsis_device_h7 cmsis_device_l5)

set(COURSE_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)

# Code generation flags apply to everything linked into a firmware image,
# including third-party libraries (FreeRTOS). Warnings apply to course code only.
set(COURSE_CODEGEN_FLAGS -ffunction-sections -fdata-sections -fno-common -g3)
set(COURSE_WARNING_FLAGS -Wall -Wextra -Wshadow -Wdouble-promotion -Wformat=2)

# Vendor headers trigger warnings we cannot fix, so they are SYSTEM includes.
add_library(cmsis_headers INTERFACE)
target_include_directories(cmsis_headers SYSTEM INTERFACE
  ${cmsis_core_SOURCE_DIR}/CMSIS/Core/Include)

add_library(cpu_h723 INTERFACE)
target_compile_options(cpu_h723 INTERFACE
  -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard ${COURSE_CODEGEN_FLAGS})
target_link_options(cpu_h723 INTERFACE
  -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard)
target_compile_definitions(cpu_h723 INTERFACE STM32H723xx)
target_include_directories(cpu_h723 SYSTEM INTERFACE ${cmsis_device_h7_SOURCE_DIR}/Include)
target_link_libraries(cpu_h723 INTERFACE cmsis_headers)

add_library(cpu_l552 INTERFACE)
target_compile_options(cpu_l552 INTERFACE
  -mcpu=cortex-m33 -mthumb -mfpu=fpv5-sp-d16 -mfloat-abi=hard ${COURSE_CODEGEN_FLAGS})
target_link_options(cpu_l552 INTERFACE
  -mcpu=cortex-m33 -mthumb -mfpu=fpv5-sp-d16 -mfloat-abi=hard)
target_compile_definitions(cpu_l552 INTERFACE STM32L552xx)
target_include_directories(cpu_l552 SYSTEM INTERFACE ${cmsis_device_l5_SOURCE_DIR}/Include)
target_link_libraries(cpu_l552 INTERFACE cmsis_headers)

# course_firmware(<name>
#     BOARD h723|l552
#     SOURCES <files...>
#     [LINKER_SCRIPT <file>]     default: the board's bsp/<board>/<board>.ld
#     [CUSTOM_STARTUP]           do not link the BSP startup file (Lab 2, Lab 9)
#     [NO_BSP]                   link nothing from the BSP at all (bootloaders)
#     [INCLUDES <dirs...>] [DEFINES <defs...>] [LIBS <targets...>])
function(course_firmware name)
  cmake_parse_arguments(FW "CUSTOM_STARTUP;NO_BSP" "BOARD;LINKER_SCRIPT"
                        "SOURCES;INCLUDES;DEFINES;LIBS" ${ARGN})
  if(NOT FW_BOARD)
    message(FATAL_ERROR "course_firmware(${name}): BOARD is required")
  endif()

  add_executable(${name} ${FW_SOURCES})
  target_compile_options(${name} PRIVATE ${COURSE_WARNING_FLAGS})
  target_include_directories(${name} PRIVATE ${FW_INCLUDES})
  target_compile_definitions(${name} PRIVATE ${FW_DEFINES})

  if(FW_NO_BSP)
    target_link_libraries(${name} PRIVATE cpu_${FW_BOARD})
  elseif(FW_CUSTOM_STARTUP)
    target_link_libraries(${name} PRIVATE bsp_${FW_BOARD}_core)
  else()
    target_link_libraries(${name} PRIVATE bsp_${FW_BOARD})
  endif()
  target_link_libraries(${name} PRIVATE ${FW_LIBS})

  if(NOT FW_LINKER_SCRIPT)
    set(FW_LINKER_SCRIPT ${COURSE_ROOT}/bsp/${FW_BOARD}/${FW_BOARD}.ld)
  endif()
  get_filename_component(FW_LINKER_SCRIPT ${FW_LINKER_SCRIPT} ABSOLUTE)

  target_link_options(${name} PRIVATE
    -L${COURSE_ROOT}/bsp/${FW_BOARD}
    -T${FW_LINKER_SCRIPT}
    --specs=nano.specs
    -Wl,--gc-sections
    -Wl,-Map=$<TARGET_FILE_DIR:${name}>/${name}.map,--cref
    -Wl,--print-memory-usage
    -Wl,--no-warn-rwx-segments)
  set_property(TARGET ${name} APPEND PROPERTY LINK_DEPENDS ${FW_LINKER_SCRIPT})

  add_custom_command(TARGET ${name} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${name}> $<TARGET_FILE_DIR:${name}>/${name}.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:${name}> $<TARGET_FILE_DIR:${name}>/${name}.hex
    COMMAND ${CMAKE_SIZE} --format=berkeley $<TARGET_FILE:${name}>
    VERBATIM)
endfunction()

# Adds starter/ and, when present, solution/ under a lab directory.
# Solutions live on the `solutions` branch only.
function(course_lab dir)
  add_subdirectory(${dir}/starter)
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${dir}/solution/CMakeLists.txt)
    add_subdirectory(${dir}/solution)
  endif()
endfunction()

# course_use_freertos(<dir with FreeRTOSConfig.h> <board> <FreeRTOS port>)
# Fetches the FreeRTOS kernel once and builds `freertos_kernel` for that
# board. Only one FreeRTOS configuration can exist per build tree.
function(course_use_freertos config_dir board port)
  if(TARGET freertos_kernel)
    return()
  endif()
  add_library(freertos_config INTERFACE)
  target_include_directories(freertos_config SYSTEM INTERFACE ${config_dir})
  target_link_libraries(freertos_config INTERFACE cpu_${board})
  set(FREERTOS_PORT ${port} CACHE STRING "FreeRTOS port" FORCE)
  set(FREERTOS_HEAP 4 CACHE STRING "FreeRTOS heap implementation" FORCE)
  FetchContent_Declare(freertos_kernel
    GIT_REPOSITORY https://github.com/FreeRTOS/FreeRTOS-Kernel.git
    GIT_TAG        V11.2.0
    GIT_SHALLOW    TRUE)
  FetchContent_MakeAvailable(freertos_kernel)
endfunction()

# course_use_monocypher(<board>): Monocypher 4 (Ed25519 verify + SHA-512) as
# the static library monocypher_<board>, for the Lab 10 bootloader.
function(course_use_monocypher board)
  if(TARGET monocypher_${board})
    return()
  endif()
  FetchContent_Declare(monocypher
    GIT_REPOSITORY https://github.com/LoupVaillant/Monocypher.git
    GIT_TAG        4.0.3
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  _no_cmake_)
  FetchContent_MakeAvailable(monocypher)
  add_library(monocypher_${board} STATIC
    ${monocypher_SOURCE_DIR}/src/monocypher.c
    ${monocypher_SOURCE_DIR}/src/optional/monocypher-ed25519.c)
  target_include_directories(monocypher_${board} SYSTEM PUBLIC
    ${monocypher_SOURCE_DIR}/src ${monocypher_SOURCE_DIR}/src/optional)
  target_compile_options(monocypher_${board} PRIVATE -O2)
  target_link_libraries(monocypher_${board} PUBLIC cpu_${board})
endfunction()
