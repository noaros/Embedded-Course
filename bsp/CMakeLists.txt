# Board support packages.
#   bsp_<board>_core : clock, UART, GPIO/DWT helpers, newlib syscalls
#   bsp_<board>      : core + reference startup file (most labs use this)
# INTERFACE libraries compile the sources inside each firmware target, so
# every lab gets exactly the CPU flags and defines of its board.

foreach(board h723 l552)
  add_library(bsp_${board}_core INTERFACE)
  target_sources(bsp_${board}_core INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/${board}/board.c
    ${CMAKE_CURRENT_SOURCE_DIR}/common/syscalls.c)
  target_include_directories(bsp_${board}_core INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/${board}
    ${CMAKE_CURRENT_SOURCE_DIR}/common)
  target_link_libraries(bsp_${board}_core INTERFACE cpu_${board})

  add_library(bsp_${board} INTERFACE)
  target_sources(bsp_${board} INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/${board}/startup_${board}.c)
  target_link_libraries(bsp_${board} INTERFACE bsp_${board}_core)
endforeach()
