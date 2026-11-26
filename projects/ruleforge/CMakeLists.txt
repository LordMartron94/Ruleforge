include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/RuleforgeEmbed.cmake")

# The grammar and its library are compiled into the engine, so no data files are needed at run time.
set(ruleforge_embedded_grammar "${CMAKE_CURRENT_BINARY_DIR}/r_grammar_data.c")
# Ruleforge's own grammar, and the standard grammars the Logos library ships (General, JSON, TOML, CSV).
set(ruleforge_logos_stdlib "${PROJECT_SOURCE_DIR}/libs/logos/stdlib")
set(ruleforge_grammars
    "${ruleforge_logos_stdlib}/General.gen"
    "${CMAKE_CURRENT_SOURCE_DIR}/spec/ruleforge.gen"
    "${CMAKE_CURRENT_SOURCE_DIR}/spec/PoEItemText.gen"
    "${ruleforge_logos_stdlib}/CSVRelaxed.gen"
    "${ruleforge_logos_stdlib}/JSON.gen"
    "${ruleforge_logos_stdlib}/TOML.gen"
)
ruleforge_embed("${ruleforge_embedded_grammar}" "ruleforge_data" ${ruleforge_grammars})
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${ruleforge_grammars})

# The standard library is Ruleforge: std/<name>.rf is the module std.<name>, embedded so the engine needs no files at run time.
file(GLOB ruleforge_std_modules CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/std/*.rf")
set(ruleforge_embedded_std "${CMAKE_CURRENT_BINARY_DIR}/r_std_data.c")
ruleforge_embed_modules("${ruleforge_embedded_std}" "ruleforge_std" ${ruleforge_std_modules})
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${ruleforge_std_modules})

# The typed syntax bindings are generated from the grammar by `genesis bind`, never written by hand.
include("${PROJECT_SOURCE_DIR}/libs/logos/cmake/LogosGenesis.cmake")
# Ruleforge's own syntax, and the formats its datasets and settings are written in, all read through generated views.
logos_genesis_bindings(ruleforge_syntax_bindings
    GRAMMAR "${CMAKE_CURRENT_SOURCE_DIR}/spec/ruleforge.gen"
    INCLUDE "${ruleforge_logos_stdlib}"
    PREFIX rf
    OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/generated/ruleforge_bindings")
logos_genesis_bindings(ruleforge_json_bindings
    GRAMMAR "${ruleforge_logos_stdlib}/JSON.gen"
    PREFIX json
    OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/generated/ruleforge_bindings")
logos_genesis_bindings(ruleforge_toml_bindings
    GRAMMAR "${ruleforge_logos_stdlib}/TOML.gen"
    INCLUDE "${ruleforge_logos_stdlib}"
    PREFIX toml
    OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/generated/ruleforge_bindings")
logos_genesis_bindings(ruleforge_csv_bindings
    GRAMMAR "${ruleforge_logos_stdlib}/CSVRelaxed.gen"
    PREFIX csv
    OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/generated/ruleforge_bindings")
logos_genesis_bindings(ruleforge_item_bindings
    GRAMMAR "${CMAKE_CURRENT_SOURCE_DIR}/spec/PoEItemText.gen"
    PREFIX poe
    OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/generated/ruleforge_bindings")
set(ruleforge_bindings_directory "${CMAKE_BINARY_DIR}/generated/ruleforge_bindings")

# The Sublime Text syntax of Ruleforge, generated from the same grammar and mapping, so it cannot differ from the language. It is rebuilt
# whenever spec/ruleforge.gen, spec/ruleforge.mapping or a grammar it imports changes, and written to LOGOS_SUBLIME_PACKAGE_DIRECTORY
# (for example ~/.config/sublime-text/Packages/User) when that is set, so a build keeps the editor current.
logos_genesis_sublime(ruleforge_sublime
    GRAMMAR "${CMAKE_CURRENT_SOURCE_DIR}/spec/ruleforge.gen"
    INCLUDE "${ruleforge_logos_stdlib}"
    INSTALL_DIRECTORY "${LOGOS_SUBLIME_PACKAGE_DIRECTORY}")
# The game's emitted format has its own grammar and editor mapping.
logos_genesis_sublime(poe_filter_sublime
    GRAMMAR "${CMAKE_CURRENT_SOURCE_DIR}/spec/PoEFilter.gen"
    INSTALL_DIRECTORY "${LOGOS_SUBLIME_PACKAGE_DIRECTORY}")
# Genesis writes the syntax; the comment shortcuts of the editor (Ctrl+/) are this file next to the grammar, kept current the same way.
if(LOGOS_SUBLIME_PACKAGE_DIRECTORY)
    set(ruleforge_comments_source "${CMAKE_CURRENT_SOURCE_DIR}/spec/Comments (Ruleforge).tmPreferences")
    add_custom_target(ruleforge_sublime_comments ALL
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${ruleforge_comments_source}" "${LOGOS_SUBLIME_PACKAGE_DIRECTORY}/Comments (Ruleforge).tmPreferences"
        DEPENDS "${ruleforge_comments_source}"
        COMMENT "Installing the Ruleforge comment settings for Sublime Text"
        VERBATIM)
endif()

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" ruleforge_version LIMIT_COUNT 1)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")

file(GLOB_RECURSE ruleforge_sources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)
add_library(ruleforge STATIC ${ruleforge_sources} "${ruleforge_embedded_grammar}" "${ruleforge_embedded_std}")
add_library(Ruleforge::Ruleforge ALIAS ruleforge)
add_dependencies(ruleforge ruleforge_syntax_bindings ruleforge_json_bindings ruleforge_toml_bindings ruleforge_csv_bindings ruleforge_item_bindings)
target_compile_definitions(ruleforge PRIVATE RULEFORGE_VERSION="${ruleforge_version}")
target_include_directories(ruleforge PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(ruleforge PRIVATE "${ruleforge_bindings_directory}")
target_link_libraries(ruleforge PRIVATE MergeSource::Options Blaze::Blaze PUBLIC Nexus::Nexus Echo::Echo Logos::Logos)

# The command line: `ruleforge parse|check|compile|simulate`.
file(GLOB ruleforge_cli_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/apps/ruleforge/*.c")
add_executable(ruleforge_cli ${ruleforge_cli_sources})
set_target_properties(ruleforge_cli PROPERTIES OUTPUT_NAME ruleforge)
add_dependencies(ruleforge_cli poe_filter_sublime)
target_link_libraries(ruleforge_cli PRIVATE MergeSource::Options Ruleforge::Ruleforge)

# Tests: golden compile + simulation of the example project, and the diagnostics of every error case.
if(BUILD_TESTING)
    add_test(NAME ruleforge_item_text
        COMMAND "${CMAKE_COMMAND}" -DRULEFORGE=$<TARGET_FILE:ruleforge_cli>
            -DCASE=${CMAKE_CURRENT_SOURCE_DIR}/tests/items/rare.txt
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/items
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_item_case.cmake")
    set(ruleforge_golden_directory "${CMAKE_CURRENT_SOURCE_DIR}/tests/golden")
    add_test(NAME ruleforge_golden_currency
        COMMAND "${CMAKE_COMMAND}" -DRULEFORGE=$<TARGET_FILE:ruleforge_cli>
            -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/examples/currency
            -DGOLDEN=${ruleforge_golden_directory}
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/golden
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_golden_case.cmake")
    # The themes the editor draws come from the engine's typed interpretation, not from walking the syntax tree.
    add_test(NAME ruleforge_themes_currency
        COMMAND ruleforge_cli themes entry.rf)
    set_tests_properties(ruleforge_themes_currency PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/examples/currency"
        PASS_REGULAR_EXPRESSION "\"format\":\"ruleforge-themes\".*\"name\":\"DefaultTheme\".*\"kind\":\"color\"")
    # A theme that implements another schema serves a script written against this one when an adapter maps its styles.
    add_test(NAME ruleforge_adapter_theme
        COMMAND ruleforge_cli compile "${CMAKE_CURRENT_SOURCE_DIR}/tests/adapters/adapter.rf")
    set_tests_properties(ruleforge_adapter_theme PROPERTIES
        PASS_REGULAR_EXPRESSION "Exalted Orb\"[^#]*SetBackgroundColor 255 0 0.*Scroll of Wisdom\"[^#]*SetBackgroundColor 0 255 0")
    # The standard library is Ruleforge; each function of it is checked through `ruleforge eval` against known results.
    add_test(NAME ruleforge_std_cases
        COMMAND "${CMAKE_COMMAND}" -DRULEFORGE=$<TARGET_FILE:ruleforge_cli>
            -DDIRECTORY=${CMAKE_CURRENT_SOURCE_DIR}/tests/std
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_std_cases.cmake")
    # A condition declared in a project is a condition like the built-in ones, and is placed in a block by its declared `order`.
    add_test(NAME ruleforge_condition_custom
        COMMAND ruleforge_cli compile "${CMAKE_CURRENT_SOURCE_DIR}/tests/conditions/custom.rf")
    set_tests_properties(ruleforge_condition_custom PROPERTIES
        PASS_REGULAR_EXPRESSION "DelveDepth >= 3[^#]*League == Settlers[^#]*BaseType == \"Exalted Orb\"")
    file(GLOB ruleforge_error_cases CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/errors/*.rf")
    foreach(case IN LISTS ruleforge_error_cases)
        get_filename_component(case_name "${case}" NAME_WE)
        get_filename_component(case_directory "${case}" DIRECTORY)
        add_test(NAME ruleforge_error_${case_name}
            COMMAND "${CMAKE_COMMAND}" -DRULEFORGE=$<TARGET_FILE:ruleforge_cli>
                -DCASE=${case_directory}/${case_name}
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_error_case.cmake")
    endforeach()
endif()

if(BUILD_TESTING)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME ruleforge_poe_filter_grammar
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/features/verify_poe_filter.py" $<TARGET_FILE:genesis>)
        add_test(NAME ruleforge_language_server
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/features/verify_lsp.py" $<TARGET_FILE:genesis>)
        add_test(NAME ruleforge_structured_rules_and_history
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/features/verify.py" $<TARGET_FILE:ruleforge_cli>)
        add_test(NAME ruleforge_property_markers
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/features/verify_markers.py" $<TARGET_FILE:ruleforge_cli>)
    endif()
endif()
