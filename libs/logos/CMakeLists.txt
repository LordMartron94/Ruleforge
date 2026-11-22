file(GLOB_RECURSE logos_sources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)
add_library(logos STATIC ${logos_sources})
add_library(Logos::Logos ALIAS logos)
target_include_directories(logos PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(logos PRIVATE MergeSource::Options PUBLIC Nexus::Nexus Echo::Echo Pharos::Pharos)
target_compile_definitions(logos PRIVATE LOGOS_GENESIS_GRAMMAR_DIR="${CMAKE_CURRENT_SOURCE_DIR}/genesis" LOGOS_GENESIS_STDLIB_DIR="${CMAKE_CURRENT_SOURCE_DIR}/stdlib")

# The genesis command line tool: export, bindings, editor syntaxes and the language server.
file(GLOB genesis_tool_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/apps/genesis/*.c")
add_executable(genesis ${genesis_tool_sources})
target_link_libraries(genesis PRIVATE MergeSource::Options Logos::Logos Pharos::Pharos)
target_compile_definitions(genesis PRIVATE _POSIX_C_SOURCE=200809L LOGOS_TOOL_STDLIB_DIR="${CMAKE_CURRENT_SOURCE_DIR}/stdlib")

# The generated bindings of the JSON example, checked against a real parse.
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/LogosGenesis.cmake")
logos_genesis_bindings(logos_genesis_json_bindings
    GRAMMAR "${CMAKE_CURRENT_SOURCE_DIR}/stdlib/JSON.gen")
add_executable(genesis_bindings_test apps/genesis/tests/bindings_test.c)
add_dependencies(genesis_bindings_test logos_genesis_json_bindings)
target_include_directories(genesis_bindings_test PRIVATE "${logos_genesis_json_bindings_OUTPUT_DIRECTORY}")
target_compile_definitions(genesis_bindings_test PRIVATE JSON_GRAMMAR_FILE="${CMAKE_CURRENT_SOURCE_DIR}/stdlib/JSON.gen")
target_link_libraries(genesis_bindings_test PRIVATE MergeSource::Options Logos::Logos)
add_test(NAME genesis_bindings_test COMMAND genesis_bindings_test)

# Sublime syntaxes of the examples (name;test file suffix), compared with the checked-in expected output.
foreach(example IN ITEMS "JSON;json;stdlib" "Calculator;calc;genesis")
    list(GET example 0 example_name)
    list(GET example 1 example_suffix)
    list(GET example 2 example_directory)
    string(TOLOWER "${example_name}" example_lower)
    logos_genesis_sublime(logos_genesis_${example_lower}_sublime
        GRAMMAR "${CMAKE_CURRENT_SOURCE_DIR}/${example_directory}/${example_name}.gen"
        INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/stdlib")
    set(sublime_output "${logos_genesis_${example_lower}_sublime_OUTPUT_DIRECTORY}")
    add_test(NAME genesis_sublime_${example_lower}_syntax
        COMMAND ${CMAKE_COMMAND} -E compare_files "${sublime_output}/${example_name}.sublime-syntax"
                "${CMAKE_CURRENT_SOURCE_DIR}/apps/genesis/tests/golden/${example_name}.sublime-syntax")
    add_test(NAME genesis_sublime_${example_lower}_tests
        COMMAND ${CMAKE_COMMAND} -E compare_files "${sublime_output}/tests/syntax_test_${example_suffix}.${example_suffix}"
                "${CMAKE_CURRENT_SOURCE_DIR}/apps/genesis/tests/golden/syntax_test_${example_suffix}.${example_suffix}")
endforeach()

# The languages of Genesis and of mapping files, written from their definitions in C, their Sublime syntaxes, and a check
# that the Genesis language parses every grammar of the repository and the mapping language every mapping file.
logos_genesis_builtin(logos_genesis_language NAME genesis)
logos_genesis_builtin(logos_genesis_mapping_language NAME mapping)
file(GLOB logos_example_grammars CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/genesis/*.gen" "${CMAKE_CURRENT_SOURCE_DIR}/stdlib/*.gen")
file(GLOB logos_example_mappings CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/genesis/*.mapping" "${CMAKE_CURRENT_SOURCE_DIR}/stdlib/*.mapping")
add_test(NAME genesis_language_parses_grammars
    COMMAND genesis check "${logos_genesis_language_GRAMMAR}" ${logos_example_grammars}
            "${logos_genesis_language_GRAMMAR}" "${logos_genesis_mapping_language_GRAMMAR}" -q)
add_test(NAME genesis_mapping_language_parses_mappings
    COMMAND genesis check "${logos_genesis_mapping_language_GRAMMAR}" ${logos_example_mappings}
            "${logos_genesis_language_MAPPING}" "${logos_genesis_mapping_language_MAPPING}" -q)

# Set LOGOS_SUBLIME_PACKAGE_DIRECTORY (for example to ~/.config/sublime-text/Packages/User) to keep the syntaxes of Genesis
# and of mapping files installed in Sublime Text; they are rewritten only when their text changes.
set(LOGOS_SUBLIME_PACKAGE_DIRECTORY "" CACHE PATH "Where the syntaxes of Genesis and of mapping files are installed for Sublime Text")
logos_genesis_sublime(logos_genesis_language_sublime
    GRAMMAR "${logos_genesis_language_GRAMMAR}" MAPPING "${logos_genesis_language_MAPPING}"
    INSTALL_DIRECTORY "${LOGOS_SUBLIME_PACKAGE_DIRECTORY}")
logos_genesis_sublime(logos_genesis_mapping_language_sublime
    GRAMMAR "${logos_genesis_mapping_language_GRAMMAR}" MAPPING "${logos_genesis_mapping_language_MAPPING}"
    INSTALL_DIRECTORY "${LOGOS_SUBLIME_PACKAGE_DIRECTORY}")
foreach(language IN ITEMS "logos_genesis_language;Genesis;genesis" "logos_genesis_mapping_language;GenesisMapping;genesis-mapping")
    list(GET language 0 language_target)
    list(GET language 1 language_name)
    list(GET language 2 language_suffix)
    set(sublime_output "${${language_target}_sublime_OUTPUT_DIRECTORY}")
    add_test(NAME ${language_target}_syntax
        COMMAND ${CMAKE_COMMAND} -E compare_files "${sublime_output}/${language_name}.sublime-syntax"
                "${CMAKE_CURRENT_SOURCE_DIR}/apps/genesis/tests/golden/${language_name}.sublime-syntax")
endforeach()

# The tool's own settings file, read with the Genesis TOML grammar.
add_test(NAME genesis_config COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/apps/genesis/tests/config_test.sh" $<TARGET_FILE:genesis>)

add_test(NAME genesis_lsp_cache COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/apps/genesis/tests/lsp_cache_test.sh" $<TARGET_FILE:genesis>)

# General's $_semver against the valid and invalid versions of Semantic Versioning 2.0.0.
add_test(NAME stdlib_general_semver COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/apps/genesis/tests/semver_test.sh" $<TARGET_FILE:genesis> "${CMAKE_CURRENT_SOURCE_DIR}/stdlib")

# The language server, driven by a scripted client over stdio.
add_test(NAME genesis_lsp_calculator
    COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/apps/genesis/tests/lsp_test.sh" $<TARGET_FILE:genesis>
            "${CMAKE_CURRENT_SOURCE_DIR}/genesis/Calculator.gen" "${CMAKE_CURRENT_SOURCE_DIR}/genesis/Calculator.mapping" "${CMAKE_CURRENT_SOURCE_DIR}/stdlib")

# The standard library of grammars (stdlib): each is checked against its sample file and gets a Sublime syntax. General is a
# library; the languages that import it find it in the same folder.
foreach(stdlib_language IN ITEMS "JSON;json" "TOML;toml" "SQL;sql" "INI;ini" "CSV;csv" "XML;xml" "HTML;html" "YAML;yaml")
    list(GET stdlib_language 0 stdlib_name)
    list(GET stdlib_language 1 stdlib_extension)
    string(TOLOWER "${stdlib_name}" stdlib_lower)
    add_test(NAME stdlib_${stdlib_lower}_parses_sample
        COMMAND genesis check "${CMAKE_CURRENT_SOURCE_DIR}/stdlib/${stdlib_name}.gen"
                "${CMAKE_CURRENT_SOURCE_DIR}/stdlib/samples/sample.${stdlib_extension}" -I "${CMAKE_CURRENT_SOURCE_DIR}/stdlib" -q)
    file(GLOB stdlib_invalid CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/stdlib/samples/invalid/*.${stdlib_extension}")
    foreach(stdlib_invalid_file IN LISTS stdlib_invalid)
        get_filename_component(stdlib_invalid_name "${stdlib_invalid_file}" NAME_WE)
        add_test(NAME stdlib_${stdlib_lower}_rejects_${stdlib_invalid_name}
            COMMAND genesis check "${CMAKE_CURRENT_SOURCE_DIR}/stdlib/${stdlib_name}.gen" "${stdlib_invalid_file}"
                    -I "${CMAKE_CURRENT_SOURCE_DIR}/stdlib" -q)
        set_tests_properties(stdlib_${stdlib_lower}_rejects_${stdlib_invalid_name} PROPERTIES WILL_FAIL TRUE)
    endforeach()
    logos_genesis_sublime(stdlib_${stdlib_lower}_sublime
        GRAMMAR "${CMAKE_CURRENT_SOURCE_DIR}/stdlib/${stdlib_name}.gen" INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/stdlib")
endforeach()

# CSVRelaxed reads real snapshots: LF or CR endings and any UTF-8 in a field (CSV.gen, the strict RFC 4180, refuses both), but it keeps
# the quoting rules, so the quoting mistakes are still rejected.
add_test(NAME stdlib_csvrelaxed_parses_sample
    COMMAND genesis check "${CMAKE_CURRENT_SOURCE_DIR}/stdlib/CSVRelaxed.gen" "${CMAKE_CURRENT_SOURCE_DIR}/stdlib/samples/relaxed.csv"
            -I "${CMAKE_CURRENT_SOURCE_DIR}/stdlib" -q)
foreach(stdlib_relaxed_invalid IN ITEMS quote-in-field text-after-quote unterminated)
    add_test(NAME stdlib_csvrelaxed_rejects_${stdlib_relaxed_invalid}
        COMMAND genesis check "${CMAKE_CURRENT_SOURCE_DIR}/stdlib/CSVRelaxed.gen"
                "${CMAKE_CURRENT_SOURCE_DIR}/stdlib/samples/invalid/${stdlib_relaxed_invalid}.csv" -I "${CMAKE_CURRENT_SOURCE_DIR}/stdlib" -q)
    set_tests_properties(stdlib_csvrelaxed_rejects_${stdlib_relaxed_invalid} PROPERTIES WILL_FAIL TRUE)
endforeach()
logos_genesis_sublime(stdlib_csvrelaxed_sublime
    GRAMMAR "${CMAKE_CURRENT_SOURCE_DIR}/stdlib/CSVRelaxed.gen" INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/stdlib")

# The language server on Genesis sources and mapping files: names across the file, an import and a mapping.
add_test(NAME genesis_lsp_genesis_sources
    COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/apps/genesis/tests/lsp_genesis_test.sh" $<TARGET_FILE:genesis>)

# Conformance of the standard grammars against the official test suites of their languages. The suites are not part of the
# repository: fetch them with stdlib/conformance/fetch.sh DIR and configure with -DLOGOS_CONFORMANCE_SUITES=DIR.
set(LOGOS_CONFORMANCE_SUITES "" CACHE PATH "Folder holding the official test suites (see stdlib/conformance/fetch.sh)")

if(LOGOS_CONFORMANCE_SUITES)
    find_package(Python3 COMPONENTS Interpreter)
    set(logos_conformance_runner "${CMAKE_CURRENT_SOURCE_DIR}/stdlib/conformance/run.py")
    set(logos_conformance_toml "${LOGOS_CONFORMANCE_SUITES}/toml-test/tests")

    if(Python3_FOUND AND EXISTS "${logos_conformance_toml}")
        add_test(NAME conformance_toml
            COMMAND "${Python3_EXECUTABLE}" "${logos_conformance_runner}" $<TARGET_FILE:genesis> "${CMAKE_CURRENT_SOURCE_DIR}/stdlib/TOML.gen"
                    "${logos_conformance_toml}" --list "${logos_conformance_toml}/files-toml-1.1.0" --suffix .toml --accept valid --reject invalid
                    --expect-unrejected "${CMAKE_CURRENT_SOURCE_DIR}/stdlib/conformance/toml-1.1.0.semantic" -I "${CMAKE_CURRENT_SOURCE_DIR}/stdlib")
    endif()

    if(Python3_FOUND AND EXISTS "${LOGOS_CONFORMANCE_SUITES}/xmlconf")
        add_test(NAME conformance_xml
            COMMAND "${Python3_EXECUTABLE}" "${logos_conformance_runner}" $<TARGET_FILE:genesis> "${CMAKE_CURRENT_SOURCE_DIR}/stdlib/XML.gen"
                    "${LOGOS_CONFORMANCE_SUITES}/xmlconf" --list-script "${CMAKE_CURRENT_SOURCE_DIR}/stdlib/conformance/xmlconf_list.py"
                    --strip-prefix --accept accept/ --reject reject/
                    --expect-unrejected "${CMAKE_CURRENT_SOURCE_DIR}/stdlib/conformance/xml-1.0.semantic" -I "${CMAKE_CURRENT_SOURCE_DIR}/stdlib")
        set_tests_properties(conformance_xml PROPERTIES TIMEOUT 1800)
    endif()

    if(Python3_FOUND AND EXISTS "${LOGOS_CONFORMANCE_SUITES}/JSONTestSuite/test_parsing")
        add_test(NAME conformance_json
            COMMAND "${Python3_EXECUTABLE}" "${logos_conformance_runner}" $<TARGET_FILE:genesis> "${CMAKE_CURRENT_SOURCE_DIR}/stdlib/JSON.gen"
                    "${LOGOS_CONFORMANCE_SUITES}/JSONTestSuite/test_parsing" --accept "y_*.json" --reject "n_*.json")
    endif()
endif()

# The Earley engine: a left-recursive, ambiguous grammar parses; an incomplete input does not.
add_test(NAME genesis_earley_accepts COMMAND genesis check "${CMAKE_CURRENT_SOURCE_DIR}/genesis/Ambiguous.gen" "${CMAKE_CURRENT_SOURCE_DIR}/stdlib/samples/sum.txt" -q)
add_test(NAME genesis_earley_rejects COMMAND genesis check "${CMAKE_CURRENT_SOURCE_DIR}/genesis/Ambiguous.gen" "${CMAKE_CURRENT_SOURCE_DIR}/stdlib/samples/invalid/sum.txt" -q)
set_tests_properties(genesis_earley_rejects PROPERTIES WILL_FAIL TRUE)

# Grammars of other formats, converted to Genesis, parse what the original accepts.
add_test(NAME genesis_import_abnf_uri
    COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/apps/genesis/tests/import_test.sh" $<TARGET_FILE:genesis> abnf
            "${CMAKE_CURRENT_SOURCE_DIR}/stdlib/formats/samples/uri.abnf"
            "${CMAKE_CURRENT_SOURCE_DIR}/stdlib/formats/samples/uri-ok" "${CMAKE_CURRENT_SOURCE_DIR}/stdlib/formats/samples/uri-bad")

add_test(NAME genesis_import_antlr_calc
    COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/apps/genesis/tests/import_test.sh" $<TARGET_FILE:genesis> antlr
            "${CMAKE_CURRENT_SOURCE_DIR}/stdlib/formats/samples/calc.g4"
            "${CMAKE_CURRENT_SOURCE_DIR}/stdlib/formats/samples/calc-ok" "${CMAKE_CURRENT_SOURCE_DIR}/stdlib/formats/samples/calc-bad")

add_test(NAME genesis_import_bison_calc
    COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/apps/genesis/tests/import_test.sh" $<TARGET_FILE:genesis> bison
            "${CMAKE_CURRENT_SOURCE_DIR}/stdlib/formats/samples/bison-calc.y ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/formats/samples/bison-calc.l"
            "${CMAKE_CURRENT_SOURCE_DIR}/stdlib/formats/samples/bison-calc-ok" "${CMAKE_CURRENT_SOURCE_DIR}/stdlib/formats/samples/bison-calc-bad")

add_test(NAME genesis_import_bison_modes
    COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/apps/genesis/tests/import_test.sh" $<TARGET_FILE:genesis> bison
            "${CMAKE_CURRENT_SOURCE_DIR}/stdlib/formats/samples/bison-modes.y ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/formats/samples/bison-modes.l"
            "${CMAKE_CURRENT_SOURCE_DIR}/stdlib/formats/samples/bison-modes-ok" "${CMAKE_CURRENT_SOURCE_DIR}/stdlib/formats/samples/bison-modes-bad")

foreach(ebnf_dialect IN ITEMS iso w3c)
    add_test(NAME genesis_import_ebnf_${ebnf_dialect}
        COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/apps/genesis/tests/import_test.sh" $<TARGET_FILE:genesis> ebnf
                "${CMAKE_CURRENT_SOURCE_DIR}/stdlib/formats/samples/ebnf-${ebnf_dialect}.ebnf"
                "${CMAKE_CURRENT_SOURCE_DIR}/stdlib/formats/samples/ebnf-${ebnf_dialect}-ok" "${CMAKE_CURRENT_SOURCE_DIR}/stdlib/formats/samples/ebnf-${ebnf_dialect}-bad")
endforeach()

# The SQL dialects converted from the ANTLR grammars of grammars-v4: each parses a script and rejects a broken one.
foreach(dialect IN ITEMS SQLite TransactSQL MySQL)
    string(TOLOWER "${dialect}" dialect_lower)
    add_test(NAME stdlib_sql_${dialect_lower}_parses_sample
        COMMAND genesis check "${CMAKE_CURRENT_SOURCE_DIR}/stdlib/sql/${dialect}.gen" "${CMAKE_CURRENT_SOURCE_DIR}/stdlib/sql/samples/${dialect_lower}.sql" -q)
    add_test(NAME stdlib_sql_${dialect_lower}_rejects_broken
        COMMAND genesis check "${CMAKE_CURRENT_SOURCE_DIR}/stdlib/sql/${dialect}.gen" "${CMAKE_CURRENT_SOURCE_DIR}/stdlib/sql/samples/invalid/${dialect_lower}.sql" -q)
    set_tests_properties(stdlib_sql_${dialect_lower}_rejects_broken PROPERTIES WILL_FAIL TRUE)
endforeach()
