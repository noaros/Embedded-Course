# Shared build logic for the Lab 10 starter and solution.
#   lab10_build(<prefix> <dir with boot/ and app/>)
# builds <prefix>_boot, <prefix>_app_A, <prefix>_app_B, and a never-confirming
# <prefix>_app_bad_B for the revert test. When Python with the
# 'cryptography' package is available, each app is signed after linking:
# <prefix>_app_X.signed.bin in the build directory.

set(LAB10_COMMON ${CMAKE_CURRENT_LIST_DIR})
set(LAB10_APP_VERSION 1.0.0 CACHE STRING "Lab 10 application version (major.minor.patch)")
set(LAB10_APP_COUNTER 1 CACHE STRING "Lab 10 application security (anti-rollback) counter")
set(LAB10_SIGNING_KEY ${COURSE_ROOT}/tools/keys/demo_ed25519.pem CACHE FILEPATH "Lab 10 signing key")

find_package(Python3 COMPONENTS Interpreter QUIET)
set(LAB10_CAN_SIGN OFF)
if(Python3_FOUND)
  execute_process(COMMAND ${Python3_EXECUTABLE} -c "import cryptography"
                  RESULT_VARIABLE _crypto_missing OUTPUT_QUIET ERROR_QUIET)
  if(_crypto_missing EQUAL 0)
    set(LAB10_CAN_SIGN ON)
  endif()
endif()
if(NOT LAB10_CAN_SIGN)
  message(STATUS "Lab 10: python3 + cryptography not found; sign images by hand with tools/fw_sign.py")
endif()

function(lab10_sign target slot version counter)
  if(LAB10_CAN_SIGN)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${Python3_EXECUTABLE} ${COURSE_ROOT}/tools/fw_sign.py sign
              --key ${LAB10_SIGNING_KEY}
              --bin $<TARGET_FILE_DIR:${target}>/${target}.bin
              --version ${version} --counter ${counter} --slot ${slot}
              --out $<TARGET_FILE_DIR:${target}>/${target}.signed.bin
      VERBATIM)
  endif()
endfunction()

function(lab10_build prefix dir)
  course_use_monocypher(h723)
  set(common_src ${LAB10_COMMON}/flash_h7.c ${LAB10_COMMON}/boot_state.c)

  course_firmware(${prefix}_boot BOARD h723
    SOURCES ${dir}/boot/main.c ${common_src}
    INCLUDES ${LAB10_COMMON}
    LIBS monocypher_h723
    LINKER_SCRIPT ${LAB10_COMMON}/boot.ld)

  foreach(slot A B)
    course_firmware(${prefix}_app_${slot} BOARD h723
      SOURCES ${dir}/app/main.c ${common_src}
      INCLUDES ${LAB10_COMMON}
      DEFINES APP_VERSION_STR="${LAB10_APP_VERSION}"
      LINKER_SCRIPT ${LAB10_COMMON}/app_slot_${slot}.ld)
    lab10_sign(${prefix}_app_${slot} ${slot} ${LAB10_APP_VERSION} ${LAB10_APP_COUNTER})
  endforeach()

  # A "bad release" for slot B: runs, but never confirms itself.
  course_firmware(${prefix}_app_bad_B BOARD h723
    SOURCES ${dir}/app/main.c ${common_src}
    INCLUDES ${LAB10_COMMON}
    DEFINES APP_VERSION_STR="9.9.9" APP_NEVER_CONFIRM=1
    LINKER_SCRIPT ${LAB10_COMMON}/app_slot_B.ld)
  lab10_sign(${prefix}_app_bad_B B 9.9.9 ${LAB10_APP_COUNTER})
endfunction()
