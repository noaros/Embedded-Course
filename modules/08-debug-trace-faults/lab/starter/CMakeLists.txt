# One image per crash case: lab08_starter_case1 ... lab08_starter_case5
foreach(n RANGE 1 5)
  course_firmware(lab08_starter_case${n} BOARD h723
    SOURCES main.c crash.c crash_report.c
    DEFINES CRASH_CASE=${n})
endforeach()
