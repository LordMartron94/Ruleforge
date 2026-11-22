add_library(stb STATIC "${PROJECT_SOURCE_DIR}/external/stb/stb_sprintf_impl.c")
add_library(External::stb ALIAS stb)
target_include_directories(stb SYSTEM PUBLIC "${PROJECT_SOURCE_DIR}/external/stb")

add_library(xxhash STATIC "${PROJECT_SOURCE_DIR}/external/xxHash/xxhash.c")
add_library(External::xxhash ALIAS xxhash)
target_include_directories(xxhash SYSTEM PUBLIC "${PROJECT_SOURCE_DIR}/external/xxHash")

find_package(Threads REQUIRED)

set_target_properties(stb xxhash PROPERTIES
    C_EXTENSIONS ON
    POSITION_INDEPENDENT_CODE ON
)
