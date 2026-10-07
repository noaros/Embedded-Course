cmake_minimum_required(VERSION 3.25)

if(NOT CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_LIST_DIR}/cmake/arm-none-eabi.cmake)
endif()

project(embedded_course LANGUAGES C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON) # GNU extensions: inline asm, attributes
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()
set(CMAKE_C_FLAGS_RELWITHDEBINFO "-O2 -g3")
set(CMAKE_C_FLAGS_DEBUG "-Og -g3")

include(cmake/course.cmake)

add_subdirectory(bsp)
add_subdirectory(examples)

# Each lab directory has starter/ and (on the solutions branch) solution/.
set(COURSE_LABS
  modules/01-cortex-m-architecture/lab
  modules/02-boot-startup-linker/lab
  modules/03-exceptions-nvic/lab
  modules/04-dma-throughput/lab
  modules/05-concurrency-caches/lab
  modules/06-rtos-internals/lab
  modules/07-low-power/lab
  modules/08-debug-trace-faults/lab
  modules/09-security/lab
  modules/10-bootloaders-updates/lab
)
foreach(lab ${COURSE_LABS})
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${lab}/starter/CMakeLists.txt)
    course_lab(${lab})
  endif()
endforeach()

# Module 11's host tests are a separate CMake project built with the native
# compiler: see modules/11-testing-ci/lab/README.md.
