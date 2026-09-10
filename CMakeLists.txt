cmake_minimum_required(VERSION 3.22)

set(TARGET_MCU "STM32G431" CACHE STRING "Target MCU")
set_property(CACHE TARGET_MCU PROPERTY STRINGS STM32G431 STM32G474)
if(NOT TARGET_MCU MATCHES "^STM32G(431|474)$")
    message(FATAL_ERROR "Unsupported MCU type: ${TARGET_MCU}")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()
if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/cmake/gcc-arm-none-eabi.cmake")
endif()
project(OpenPixelOSD LANGUAGES C ASM)
add_executable(${PROJECT_NAME})

if(TARGET_MCU STREQUAL "STM32G431")
    set(MCU_DEFINITIONS STM32G431xx LOW_RAM)
    set(LINKER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/stm32g431cbux_flash.ld")
    set(STARTUP_ASSEMBLY_FILE src/stm32g4xx/startup_stm32g431xx.s)
else()
    set(MCU_DEFINITIONS STM32G474xx HIGH_RAM)
    set(LINKER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/stm32g474ceux_flash.ld")
    set(STARTUP_ASSEMBLY_FILE src/stm32g4xx/startup_stm32g474xx.s)
    set(MCU_SOURCES src/fonts/font_system.c)
endif()

target_compile_definitions(${PROJECT_NAME} PRIVATE
    ${MCU_DEFINITIONS} MCU_TYPE="${TARGET_MCU}" USE_FULL_LL_DRIVER USE_HAL_DRIVER
)

# Git metadata works on Windows and Unix, and when building outside the source tree.
find_package(Git QUIET)
set(GIT_BRANCH "")
set(GIT_HASH "")
set(GIT_TAG "unknown")
if(Git_FOUND)
    foreach(kind IN ITEMS BRANCH HASH TAG)
        if(kind STREQUAL "BRANCH")
            set(args rev-parse --abbrev-ref HEAD)
        elseif(kind STREQUAL "HASH")
            set(args rev-parse --short=8 HEAD)
        else()
            set(args describe --tags --abbrev=0)
        endif()
        execute_process(COMMAND "${GIT_EXECUTABLE}" ${args}
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            OUTPUT_VARIABLE value RESULT_VARIABLE result
            OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        if(result EQUAL 0)
            if(kind STREQUAL "BRANCH")
                string(SUBSTRING "${value}" 0 10 value)
            endif()
            set(GIT_${kind} "${value}")
        endif()
    endforeach()
endif()
target_compile_definitions(${PROJECT_NAME} PRIVATE
    GIT_BRANCH="${GIT_BRANCH}" GIT_HASH="${GIT_HASH}" GIT_TAG="${GIT_TAG}"
)

add_subdirectory(cmake/stm32)
target_sources(${PROJECT_NAME} PRIVATE
        ${STARTUP_ASSEMBLY_FILE}
        ${MCU_SOURCES}
        ./src/stm32g4xx/stm32g4xx_it.c
        ./src/stm32g4xx/system_stm32g4xx.c
        ./src/stm32g4xx/sysmem.c
        ./src/stm32g4xx/syscalls.c
        ./src/stm32g4xx/system.c
        ./src/msp/msp.c
        ./src/hardware/gpio.c
        ./src/hardware/dac.c
        ./src/hardware/adc.c
        ./src/hardware/dma.c
        ./src/hardware/opamp.c
        ./src/hardware/tim.c
        ./src/hardware/comp.c
        ./src/hardware/uart.c
        ./src/hardware/usb.c
        ./src/render/video_gen.c
        ./src/render/video_overlay.c
        ./src/render/canvas_char.c
        ./src/render/msp_displayport.c
        ./src/fonts/update_font.c
        ./src/render/video_graphics.c
        ./src/main.c

)

target_include_directories(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/stm32g4xx"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/msp"
)
target_link_libraries(${PROJECT_NAME} PRIVATE stm32cubemx)
target_link_options(${PROJECT_NAME} PRIVATE
    "-T${LINKER_SCRIPT}" "-Wl,-Map=${PROJECT_BINARY_DIR}/${PROJECT_NAME}.map"
)
set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY LINK_DEPENDS "${LINKER_SCRIPT}")

set(HEX_FILE "${PROJECT_NAME}_${TARGET_MCU}.hex")
set(BIN_FILE "${PROJECT_NAME}_${TARGET_MCU}.bin")
set(MAP_FILE "${PROJECT_NAME}.map")
set(ZIP_NAME "${PROJECT_NAME}_${TARGET_MCU}_${CMAKE_BUILD_TYPE}")
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND "${CMAKE_OBJCOPY}" -Oihex $<TARGET_FILE:${PROJECT_NAME}> "${HEX_FILE}"
    COMMAND "${CMAKE_OBJCOPY}" -Obinary $<TARGET_FILE:${PROJECT_NAME}> "${BIN_FILE}"
    COMMAND "${CMAKE_SIZE}" $<TARGET_FILE:${PROJECT_NAME}>
    COMMAND "${CMAKE_COMMAND}" -E tar cf "${ZIP_NAME}.zip" --format=zip
        "${HEX_FILE}" "${BIN_FILE}" "${MAP_FILE}" "$<TARGET_FILE_NAME:${PROJECT_NAME}>"
    WORKING_DIRECTORY "${PROJECT_BINARY_DIR}"
    VERBATIM
)
add_custom_target(flash
    COMMAND dfu-util -a 0 -s 0x08000000:leave -D "${PROJECT_BINARY_DIR}/${BIN_FILE}" -R
    DEPENDS ${PROJECT_NAME}
    VERBATIM
)
