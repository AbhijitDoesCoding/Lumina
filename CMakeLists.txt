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

# Statically link the MSVC runtime so the shipped binary is self-contained.
# This MUST be set globally (before any target) so every object — lumina_core,
# lumina, and the tests — agrees on the runtime; mixing /MT and /MD produces
# multiply-defined-symbol link errors (LNK1169).
if(MSVC)
    cmake_policy(SET CMP0091 NEW)  # honor CMAKE_MSVC_RUNTIME_LIBRARY
    set(CMAKE_MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>")
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

# Statically link the GCC/Clang runtime so distributed binaries are
# self-contained (no external libstdc++/libgcc DLLs to ship). The MSVC runtime
# is handled globally via CMAKE_MSVC_RUNTIME_LIBRARY above.
if(MINGW)
    target_link_options(lumina PRIVATE -static -static-libgcc -static-libstdc++)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT APPLE)
    target_link_options(lumina PRIVATE -static-libgcc -static-libstdc++)
endif()

# ---- Tests (round-trip harness, built with ctest) ----------------------------
enable_testing()
add_executable(lumina_tests tests/roundtrip_tests.cpp)
target_link_libraries(lumina_tests PRIVATE lumina_core)
add_test(NAME roundtrip COMMAND lumina_tests)

install(TARGETS lumina RUNTIME DESTINATION bin)
install(FILES README.md LICENSE DESTINATION .)

# ---- Packaging (CPack): produce distributable archives -----------------------
set(CPACK_PACKAGE_NAME "lumina")
set(CPACK_PACKAGE_VENDOR "AbhijitDoesCoding")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_CONTACT "abalpande1804@gmail.com")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")

# Fold the target OS + architecture into the archive file name.
if(WIN32)
    set(CPACK_GENERATOR "ZIP")
    set(CPACK_SYSTEM_NAME "windows-${CMAKE_SYSTEM_PROCESSOR}")
else()
    set(CPACK_GENERATOR "TGZ")
    set(CPACK_SYSTEM_NAME "${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
endif()
set(CPACK_PACKAGE_FILE_NAME
    "lumina-${PROJECT_VERSION}-${CPACK_SYSTEM_NAME}")

include(CPack)
