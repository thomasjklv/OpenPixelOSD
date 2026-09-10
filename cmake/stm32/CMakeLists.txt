cmake_minimum_required(VERSION 3.22)

add_library(stm32cubemx INTERFACE)

# Enable CMake support for ASM and C languages
enable_language(C ASM)

target_compile_definitions(stm32cubemx INTERFACE
    $<$<CONFIG:Debug>:DEBUG>
)

target_include_directories(stm32cubemx INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}/../../USB_Device/App"
    "${CMAKE_CURRENT_LIST_DIR}/../../USB_Device/Target"
    "${CMAKE_CURRENT_LIST_DIR}/../../Drivers/STM32G4xx_HAL_Driver/Inc"
    "${CMAKE_CURRENT_LIST_DIR}/../../Drivers/STM32G4xx_HAL_Driver/Inc/Legacy"
    "${CMAKE_CURRENT_LIST_DIR}/../../Middlewares/ST/STM32_USB_Device_Library/Core/Inc"
    "${CMAKE_CURRENT_LIST_DIR}/../../Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc"
    "${CMAKE_CURRENT_LIST_DIR}/../../Drivers/CMSIS/Device/ST/STM32G4xx/Include"
    "${CMAKE_CURRENT_LIST_DIR}/../../Drivers/CMSIS/Include"
)

target_sources(stm32cubemx INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}/../../Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_ll_utils.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_ll_exti.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_ll_gpio.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_pcd.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_pcd_ex.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_ll_usb.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_rcc.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_rcc_ex.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_flash.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_flash_ex.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_flash_ramfunc.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_gpio.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_exti.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_dma.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_dma_ex.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_pwr.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_pwr_ex.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_cortex.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_ll_comp.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_ll_rcc.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_ll_dac.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_ll_dma.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_ll_opamp.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_ll_tim.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_ll_usart.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_ll_adc.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_core.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ctlreq.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ioreq.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Src/usbd_cdc.c"
	"${CMAKE_CURRENT_LIST_DIR}/../../USB_Device/App/usb_device.c"
	"${CMAKE_CURRENT_LIST_DIR}/../../USB_Device/App/usbd_desc.c"
	"${CMAKE_CURRENT_LIST_DIR}/../../USB_Device/App/usbd_cdc_if.c"
	"${CMAKE_CURRENT_LIST_DIR}/../../USB_Device/Target/usbd_conf.c"
)

# Validate that STM32CubeMX code is compatible with C standard
if(CMAKE_C_STANDARD LESS 11)
    message(FATAL_ERROR "Generated code requires C11 or higher")
endif()


