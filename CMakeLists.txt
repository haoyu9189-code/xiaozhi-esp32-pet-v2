# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

set(PROJECT_VER "2.1.0")

# Add this line to disable the specific warning
add_compile_options(-Wno-missing-field-initializers)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(xiaozhi)

# Custom target to flash animation assets to partition at 0x800000
# Usage: idf.py flash_anim -p COM10
# Or: idf.py flash_all -p COM10 (flashes firmware + animation)
set(ANIMATION_BIN "${CMAKE_SOURCE_DIR}/assets_build/animation_frames.bin")

add_custom_target(flash_anim
    COMMAND python -m esptool --port ${ESPPORT} write_flash 0x800000 ${ANIMATION_BIN}
    COMMENT "Flashing animation assets to 0x800000"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Combined target: flash firmware + animation
add_custom_target(flash_all
    DEPENDS flash flash_anim
    COMMENT "Flashing firmware and animation assets"
)

