file(GLOB_RECURSE echo_sources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)
add_library(echo STATIC ${echo_sources})
add_library(Echo::Echo ALIAS echo)
target_include_directories(echo PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(echo PRIVATE MergeSource::Options PUBLIC Nexus::Nexus)

# Preserve the archived workspace override in Echo and every ABI consumer.
target_compile_definitions(echo PUBLIC ECHO_SYSTEM_LABEL_LENGTH_MAX=64)

file(GLOB echo_test_sources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.c"
)
if(BUILD_TESTING AND echo_test_sources)
    add_executable(echo_tests ${echo_test_sources})
    target_link_libraries(echo_tests PRIVATE MergeSource::Options Echo::Echo)
    add_test(NAME echo_tests COMMAND echo_tests)
    set_tests_properties(echo_tests PROPERTIES TIMEOUT 15)
endif()
