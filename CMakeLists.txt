cmake_minimum_required(VERSION 3.20)

project(Ruleforge LANGUAGES C)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "Use an out-of-source build: cmake -S . -B build")
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build configuration" FORCE)
endif()

set(CMAKE_C_STANDARD 90)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# The libraries are linked statically here, so a release is two self-contained programs (ruleforge and ruleforge-studio).
add_library(mergesource_options INTERFACE)
add_library(MergeSource::Options ALIAS mergesource_options)
target_compile_options(mergesource_options INTERFACE
    "$<$<COMPILE_LANG_AND_ID:C,GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror=vla;-Werror=switch;-Wc++-compat;-Wdeclaration-after-statement;-Wshadow;-Wcast-qual;-Wstrict-prototypes;-Wmissing-prototypes;-fwrapv;-fno-strict-aliasing;-fno-delete-null-pointer-checks;-fno-omit-frame-pointer>"
)

include(cmake/ExternalLibraries.cmake)
include(CTest)

add_subdirectory(libs/nexus)
add_subdirectory(libs/echo)
add_subdirectory(libs/pharos)
add_subdirectory(libs/logos)
add_subdirectory(projects/ruleforge)
