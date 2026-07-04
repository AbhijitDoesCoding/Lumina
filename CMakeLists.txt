cmake_minimum_required(VERSION 3.16)

project(Lumina
    VERSION 1.0.0
    DESCRIPTION "High-performance file compression CLI (Huffman + LZW), std-lib only"
    LANGUAGES CXX)

# ---- Language standard -------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to Release if the user did not pick a build type.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
endif()

# ---- Library target: all codec logic lives here, main() just drives it -------
add_library(lumina_core STATIC
    src/Crc32.cpp
    src/BitIO.cpp
    src/Format.cpp
    src/Huffman.cpp
    src/Lzw.cpp
    src/Cli.cpp
)

target_include_directories(lumina_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

# Warnings: strict but portable across GCC/Clang/MSVC.
if(MSVC)
    target_compile_options(lumina_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(lumina_core PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion)
endif()

# ---- Executable --------------------------------------------------------------
add_executable(lumina src/main.cpp)
target_link_libraries(lumina PRIVATE lumina_core)

# ---- Tests (round-trip harness, built with ctest) ----------------------------
enable_testing()
add_executable(lumina_tests tests/roundtrip_tests.cpp)
target_link_libraries(lumina_tests PRIVATE lumina_core)
add_test(NAME roundtrip COMMAND lumina_tests)

install(TARGETS lumina RUNTIME DESTINATION bin)
