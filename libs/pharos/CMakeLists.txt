file(GLOB_RECURSE pharos_sources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)
add_library(pharos STATIC ${pharos_sources})
add_library(Pharos::Pharos ALIAS pharos)
target_include_directories(pharos PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(pharos PRIVATE MergeSource::Options PUBLIC Nexus::Nexus Echo::Echo)

file(GLOB pharos_test_sources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.c"
)
if(BUILD_TESTING AND pharos_test_sources)
    add_executable(pharos_tests ${pharos_test_sources})
    target_link_libraries(pharos_tests PRIVATE MergeSource::Options Pharos::Pharos)
    add_test(NAME pharos_tests COMMAND pharos_tests)
endif()
