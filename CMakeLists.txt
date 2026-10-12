cmake_minimum_required(VERSION 3.16)
project(openlogi_c C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_compile_options(-Wall -Wextra -pedantic)
if(NOT MSVC)
    add_compile_options(-Werror)
endif()

include_directories(
    include
    src/hid
    vendor/yyjson
    vendor/tomlc99
)

# Vendored third-party libraries (without strict -Werror)
add_library(yyjson STATIC vendor/yyjson/yyjson.c)
set_target_properties(yyjson PROPERTIES COMPILE_FLAGS "-w")

add_library(tomlc99 STATIC vendor/tomlc99/toml.c)
set_target_properties(tomlc99 PROPERTIES COMPILE_FLAGS "-w")

set(PLATFORM_SRCS "")
set(PLATFORM_LIBS "")

if(APPLE)
    list(APPEND PLATFORM_SRCS src/platform/macos/hid_iokit.c)
    find_library(IOKIT_LIB IOKit REQUIRED)
    find_library(COREFOUNDATION_LIB CoreFoundation REQUIRED)
    list(APPEND PLATFORM_LIBS ${IOKIT_LIB} ${COREFOUNDATION_LIB})
endif()

# Core OpenLogi C library
add_library(openlogi_core STATIC
    src/hid/hidpp_packet.c
    src/hid/hidpp_features.c
    src/hid/hid_mock.c
    ${PLATFORM_SRCS}
)
target_link_libraries(openlogi_core PRIVATE yyjson tomlc99 ${PLATFORM_LIBS})

# Unit tests
enable_testing()

add_executable(test_hidpp_packet tests/test_hidpp_packet.c)
target_link_libraries(test_hidpp_packet PRIVATE openlogi_core ${PLATFORM_LIBS})
add_test(NAME test_hidpp_packet COMMAND test_hidpp_packet)

add_executable(test_device_probe tests/test_device_probe.c)
target_link_libraries(test_device_probe PRIVATE openlogi_core ${PLATFORM_LIBS})
add_test(NAME test_device_probe COMMAND test_device_probe)

# Diagnostic CLI tool
add_executable(openlogi_discover src/cli/openlogi_discover.c)
target_link_libraries(openlogi_discover PRIVATE openlogi_core ${PLATFORM_LIBS})
