# Secure image: built with -mcmse; produces the import library the
# Non-secure image links against.
set(LAB09_STARTER_NSCLIB ${CMAKE_CURRENT_BINARY_DIR}/lab09_starter_nsclib.o PARENT_SCOPE)

course_firmware(lab09_starter_s BOARD l552 CUSTOM_STARTUP
  SOURCES main_s.c startup_s.c sha256.c
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../../shared
  LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/l552_s.ld)
target_compile_options(lab09_starter_s PRIVATE -mcmse)
target_link_options(lab09_starter_s PRIVATE
  -Wl,--cmse-implib,--out-implib=${CMAKE_CURRENT_BINARY_DIR}/lab09_starter_nsclib.o)
