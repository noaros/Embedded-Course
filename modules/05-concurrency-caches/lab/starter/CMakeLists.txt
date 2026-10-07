course_firmware(lab05_starter BOARD h723 SOURCES main.c spi_dma.c)

# Bug 3 hides until the optimiser can see across files. Build this variant
# too: -flto lets GCC inline app_idle_hook() into the wait loop.
course_firmware(lab05_starter_lto BOARD h723 SOURCES main.c spi_dma.c)
target_compile_options(lab05_starter_lto PRIVATE -flto)
target_link_options(lab05_starter_lto PRIVATE -flto)
