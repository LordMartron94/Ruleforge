file(GLOB_RECURSE nexus_sources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)
# accept4 is a Linux extension; expose it only in the networking backend.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set_source_files_properties(src/n_network.c PROPERTIES COMPILE_DEFINITIONS _GNU_SOURCE=1)
endif()
add_library(nexus STATIC ${nexus_sources})
add_library(Nexus::Nexus ALIAS nexus)
target_include_directories(nexus PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(nexus
    PRIVATE MergeSource::Options Threads::Threads ${CMAKE_DL_LIBS}
    PUBLIC nexus_c89_extensions External::stb External::xxhash
)
if(UNIX)
    target_link_libraries(nexus PRIVATE m)
endif()
# n_entropy.c reads the system random source through BCryptGenRandom on Windows.
if(WIN32)
    target_link_libraries(nexus PUBLIC bcrypt)
endif()

# These public-header switches must match in Nexus and its consumers.
target_compile_definitions(nexus PUBLIC
    NEXUS_DEVELOPER_MODE=$<IF:$<CONFIG:Debug>,1,0>
    NEXUS_MEMORY_DEBUG_MODE=$<IF:$<CONFIG:Debug>,1,0>
    NEXUS_MEMORY_TRACING=$<IF:$<CONFIG:Debug>,1,0>
    $<$<BOOL:${UNIX}>:_POSIX_C_SOURCE=200809L>
)
# Preserve the archived build's C89 extension flags for this public header.
# Keep them after the general warning policy, as in the archived build.
add_library(nexus_c89_extensions INTERFACE)
target_compile_options(nexus_c89_extensions INTERFACE
    "$<$<C_COMPILER_ID:GNU,Clang>:-Wno-long-long;-Wno-variadic-macros>"
    "$<$<C_COMPILER_ID:Clang>:-fdeclspec;-Wno-gnu-zero-variadic-macro-arguments>"
)

file(GLOB nexus_test_sources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.c"
)
if(BUILD_TESTING AND nexus_test_sources)
    list(REMOVE_ITEM nexus_test_sources
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/n_network_tests.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/n_mapping_tests.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/n_memory_guard_tests.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/n_filesystem_tests.c"
    )
    add_executable(nexus_tests ${nexus_test_sources})
    target_link_libraries(nexus_tests PRIVATE MergeSource::Options Nexus::Nexus)
    add_test(NAME nexus_tests COMMAND nexus_tests)

    add_executable(nexus_memory_guard_tests tests/n_memory_guard_tests.c)
    target_link_libraries(nexus_memory_guard_tests PRIVATE MergeSource::Options Nexus::Nexus)
    add_test(NAME nexus_memory_guard_tests COMMAND nexus_memory_guard_tests)

    # Filesystem behavior that differs between platforms. The second build runs the matcher Nexus carries for platforms without regex.h
    # (Windows) on every platform, so its behavior is checked wherever the tests run.
    add_executable(nexus_filesystem_tests tests/n_filesystem_tests.c)
    target_link_libraries(nexus_filesystem_tests PRIVATE MergeSource::Options Nexus::Nexus)
    add_test(NAME nexus_filesystem_tests COMMAND nexus_filesystem_tests)

    add_executable(nexus_filesystem_builtin_regex_tests tests/n_filesystem_tests.c ${nexus_sources})
    target_include_directories(nexus_filesystem_builtin_regex_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_definitions(nexus_filesystem_builtin_regex_tests PRIVATE
        NEXUS_FILESYSTEM_BUILTIN_REGEX=1 $<TARGET_PROPERTY:nexus,INTERFACE_COMPILE_DEFINITIONS>)
    target_link_libraries(nexus_filesystem_builtin_regex_tests PRIVATE
        MergeSource::Options Threads::Threads ${CMAKE_DL_LIBS} nexus_c89_extensions External::stb External::xxhash)
    if(UNIX)
        target_link_libraries(nexus_filesystem_builtin_regex_tests PRIVATE m)
    endif()
    if(WIN32)
        target_link_libraries(nexus_filesystem_builtin_regex_tests PRIVATE bcrypt)
    endif()
    add_test(NAME nexus_filesystem_builtin_regex_tests COMMAND nexus_filesystem_builtin_regex_tests)

    add_executable(nexus_network_tests tests/n_network_tests.c)
    target_link_libraries(nexus_network_tests PRIVATE MergeSource::Options Nexus::Nexus)
    add_test(NAME nexus_network_tests COMMAND nexus_network_tests)
    set_tests_properties(nexus_network_tests PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77)

    add_executable(nexus_mapping_tests tests/n_mapping_tests.c)
    target_link_libraries(nexus_mapping_tests PRIVATE MergeSource::Options Nexus::Nexus)
    add_test(NAME nexus_mapping_tests COMMAND nexus_mapping_tests)
    set_tests_properties(nexus_mapping_tests PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77)
endif()
