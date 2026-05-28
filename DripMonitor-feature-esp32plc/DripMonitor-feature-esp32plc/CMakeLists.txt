# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# Tell Zephyr to look here (in addition to ZEPHYR_BASE) for board definitions
# and devicetree bindings.  Required because we keep the custom board for the
# Industrial Shields ESP32 PLC 21 in this repo, not in the Zephyr tree.
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
list(APPEND DTS_ROOT   ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(DripMonitor)

# Create a custom command to generate version information at build time
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/version_info.h
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/generate_version.cmake
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Generating version information"
    VERBATIM
)

# Create a custom target that depends on the version header
add_custom_target(generate_version_header ALL
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/version_info.h
)

# Make sure the generated header is available to the compiler
target_include_directories(app PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

# Make the app depend on the version generation
add_dependencies(app generate_version_header)

target_sources(app PRIVATE
    src/main.cpp
    src/dripper.cpp
    src/display.cpp
    src/cloud.cpp
    src/storage.cpp
    src/modbus_rtu.cpp
    include/network.cpp
    include/LK204.cpp
    include/PID.cpp
    include/mqtt_broker.cpp
)
