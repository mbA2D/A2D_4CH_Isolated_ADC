cmake_minimum_required(VERSION 3.21)
project(A2D_4CH_Isolated_ADC VERSION 5.0.1 LANGUAGES C ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CUBE "${CMAKE_CURRENT_SOURCE_DIR}/vendor/STM32CubeF1")
set(HAL "${CUBE}/Drivers/STM32F1xx_HAL_Driver")
set(CMSIS "${CUBE}/Drivers/CMSIS")
set(USB "${CUBE}/Middlewares/ST/STM32_USB_Device_Library")
add_library(stm32_hal OBJECT)
foreach(module hal hal_cortex hal_rcc hal_rcc_ex hal_gpio hal_flash hal_flash_ex hal_i2c hal_uart hal_dma hal_pcd hal_pcd_ex ll_usb)
  target_sources(stm32_hal PRIVATE "${HAL}/Src/stm32f1xx_${module}.c")
endforeach()
target_sources(stm32_hal PRIVATE
  "${CMSIS}/Device/ST/STM32F1xx/Source/Templates/system_stm32f1xx.c"
  "${USB}/Core/Src/usbd_core.c" "${USB}/Core/Src/usbd_ctlreq.c" "${USB}/Core/Src/usbd_ioreq.c"
  "${USB}/Class/CDC/Src/usbd_cdc.c")
target_include_directories(stm32_hal PUBLIC . hal/include "${HAL}/Inc" "${HAL}/Inc/Legacy"
  "${CMSIS}/Include" "${CMSIS}/Device/ST/STM32F1xx/Include" "${USB}/Core/Inc" "${USB}/Class/CDC/Inc")
target_compile_definitions(stm32_hal PUBLIC USE_HAL_DRIVER STM32F103xB USER_VECT_TAB_ADDRESS VECT_TAB_OFFSET=0x2000U)
target_compile_options(stm32_hal PUBLIC -mcpu=cortex-m3 -mthumb -Os -ffunction-sections -fdata-sections -fno-common -flto)
add_executable(firmware
  A2D_4CH_Isolated_ADC.c A2D_Application.c A2D_Calibration.c A2D_Dispatcher.c
  A2D_Flash.c A2D_Measurement.c A2D_Protocol.c A2D_SettingsStore.c A2D_Transport.c
  A2D_USB_Bootloader.c A2D_IO.c firmware/app.c lib/A2D_Scpi/src/A2D_Scpi.c lib/A2D_Scpi/src/A2D_ScpiLineReader.c lib/A2D_Scpi/src/A2D_ScpiErrorQueue.c
  hal/src/main.c hal/src/uart.c hal/src/usb.c hal/src/usbd_conf.c hal/src/usbd_desc.c hal/src/syscalls.c
  "${CMSIS}/Device/ST/STM32F1xx/Source/Templates/gcc/startup_stm32f103xb.s")
set_target_properties(firmware PROPERTIES SUFFIX ".elf")
target_include_directories(firmware PRIVATE lib/A2D_Scpi/src)
target_link_libraries(firmware PRIVATE stm32_hal m)
target_compile_options(firmware PRIVATE -Wall -Wextra -Werror -pedantic)
target_link_options(firmware PRIVATE -mcpu=cortex-m3 -mthumb -Os -flto --specs=nano.specs --specs=nosys.specs
  "-T${CMAKE_CURRENT_SOURCE_DIR}/hal/STM32F103C8_Maple.ld" -Wl,--gc-sections
  "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/firmware.map" -Wl,--print-memory-usage)
set_property(TARGET firmware APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/hal/STM32F103C8_Maple.ld")
add_custom_command(TARGET firmware POST_BUILD
  COMMAND "${CMAKE_OBJCOPY}" -O binary "$<TARGET_FILE:firmware>" "${CMAKE_CURRENT_BINARY_DIR}/firmware.bin"
  COMMAND "${CMAKE_OBJCOPY}" -O ihex "$<TARGET_FILE:firmware>" "${CMAKE_CURRENT_BINARY_DIR}/firmware.hex"
  COMMAND "${CMAKE_SIZE}" "$<TARGET_FILE:firmware>")
