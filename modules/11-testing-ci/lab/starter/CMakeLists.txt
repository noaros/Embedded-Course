# Lab 11: host-side unit tests for the logic extracted from the Lab 4 logger.
# A separate CMake project, built with the NATIVE compiler:
#
#   cmake -S modules/11-testing-ci/lab/solution -B build-host
#   cmake --build build-host && ctest --test-dir build-host --output-on-failure
#
# Options: LAB11_SANITIZE (ASan + UBSan, default ON), LAB11_COVERAGE (gcov).
cmake_minimum_required(VERSION 3.25)
project(lab11_host LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

option(LAB11_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" ON)
option(LAB11_COVERAGE "Instrument the library for gcov" OFF)

include(FetchContent)
FetchContent_Declare(unity
  GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
  GIT_TAG        v2.6.1
  GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(unity)

set(SANITIZE_FLAGS -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)

# The code under test: the same files the firmware uses.
add_library(logger_lib STATIC lib/fir.c lib/frame.c lib/cmd_parser.c)
target_include_directories(logger_lib PUBLIC lib)
target_compile_options(logger_lib PRIVATE -Wall -Wextra -Wshadow -Wconversion -Werror)
if(LAB11_COVERAGE)
  target_compile_options(logger_lib PRIVATE --coverage -O0)
  target_link_options(logger_lib PUBLIC --coverage)
endif()
if(LAB11_SANITIZE)
  target_compile_options(logger_lib PUBLIC ${SANITIZE_FLAGS})
  target_link_options(logger_lib PUBLIC ${SANITIZE_FLAGS})
endif()

enable_testing()
foreach(t fir frame cmd_parser)
  add_executable(test_${t} tests/test_${t}.c)
  target_link_libraries(test_${t} PRIVATE logger_lib unity)
  target_compile_options(test_${t} PRIVATE -Wall -Wextra)
  add_test(NAME ${t} COMMAND test_${t})
endforeach()
