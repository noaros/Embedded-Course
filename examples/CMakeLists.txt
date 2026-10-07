# Smoke test for each board: blinks the green LED and prints over the
# virtual COM port. Flash one of these first to check your setup.
course_firmware(blinky_h723 BOARD h723 SOURCES blinky/main.c)
course_firmware(blinky_l552 BOARD l552 SOURCES blinky/main.c)
