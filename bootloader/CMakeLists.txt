# SPDX-License-Identifier: GPL-2.0-only
cmake_minimum_required(VERSION 3.22)
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
project(OpenPixelOSD_Bootloader C)
add_executable(OpenPixelOSD_Bootloader main.c ../src/bootloader/protocol.c)
target_compile_definitions(OpenPixelOSD_Bootloader PRIVATE STM32G431xx)
target_include_directories(OpenPixelOSD_Bootloader PRIVATE
    ../Drivers/CMSIS/Include ../Drivers/CMSIS/Device/ST/STM32G4xx/Include ../src/bootloader)
target_compile_options(OpenPixelOSD_Bootloader PRIVATE -mcpu=cortex-m4 -mthumb -mfloat-abi=soft -Os -g3
    -std=c11 -Wall -Wextra -Werror -ffreestanding -fno-builtin -ffunction-sections -fdata-sections)
target_link_options(OpenPixelOSD_Bootloader PRIVATE -mcpu=cortex-m4 -mthumb -mfloat-abi=soft -nostdlib
    -T${CMAKE_CURRENT_SOURCE_DIR}/boot.ld -Wl,--gc-sections -Wl,-Map=OpenPixelOSD_Bootloader.map)
set_target_properties(OpenPixelOSD_Bootloader PROPERTIES SUFFIX .elf LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/boot.ld)
add_custom_command(TARGET OpenPixelOSD_Bootloader POST_BUILD
    COMMAND arm-none-eabi-objcopy -O ihex --gap-fill=0xFF --pad-to=0x08002000 $<TARGET_FILE:OpenPixelOSD_Bootloader> OpenPixelOSD_Bootloader.hex
    COMMAND arm-none-eabi-objcopy -O binary --gap-fill=0xFF --pad-to=0x08002000 $<TARGET_FILE:OpenPixelOSD_Bootloader> OpenPixelOSD_Bootloader.bin
    COMMAND arm-none-eabi-size $<TARGET_FILE:OpenPixelOSD_Bootloader>)
