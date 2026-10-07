course_firmware(lab05_solution BOARD h723 SOURCES main.c spi_dma.c)

# The fix must hold under LTO too.
course_firmware(lab05_solution_lto BOARD h723 SOURCES main.c spi_dma.c)
target_compile_options(lab05_solution_lto PRIVATE -flto)
target_link_options(lab05_solution_lto PRIVATE -flto)

# Alternative: DMA buffers in an MPU non-cacheable region, no maintenance.
course_firmware(lab05_solution_mpu BOARD h723 SOURCES main.c spi_dma.c
  DEFINES SPI_USE_MPU_NOCACHE=1)
