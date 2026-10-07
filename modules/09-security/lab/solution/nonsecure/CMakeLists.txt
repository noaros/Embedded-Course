# Non-secure image: links against the Secure world's import library (veneer
# addresses only) and uses the course BSP startup.
course_firmware(lab09_solution_ns BOARD l552
  SOURCES main_ns.c
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../../shared
  LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/l552_ns.ld)
target_link_options(lab09_solution_ns PRIVATE ${LAB09_SOLUTION_NSCLIB})
add_dependencies(lab09_solution_ns lab09_solution_s)
