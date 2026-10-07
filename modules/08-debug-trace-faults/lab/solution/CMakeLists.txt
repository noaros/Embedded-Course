# One image per crash case: lab08_solution_case1 ... lab08_solution_case5
foreach(n RANGE 1 5)
  course_firmware(lab08_solution_case${n} BOARD h723
    SOURCES main.c crash.c crash_report.c
    DEFINES CRASH_CASE=${n})
endforeach()
