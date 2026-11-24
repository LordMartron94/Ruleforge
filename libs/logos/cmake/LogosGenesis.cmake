# Generators driven by the genesis tool. A generated file is written only when its text changes, and the
# generators run again when the grammar, its mapping file, any grammar in an import folder or the tool itself
# (so the engine behind it) changes. Consumers depend on the custom target, not on a file timestamp, so a
# grammar edit that leaves the output unchanged rebuilds nothing.
#
#   logos_genesis_bindings(<target>
#       GRAMMAR <file.gen>
#       [MAPPING <file.mapping>]          # default: <grammar>.mapping next to it, when it exists
#       [INCLUDE <import folder>...]
#       [OUTPUT_DIRECTORY <dir>]          # default: ${CMAKE_BINARY_DIR}/generated/<target>
#       [PREFIX <identifier prefix>]
#       [INSTALL_DIRECTORY <dir>])       # sublime: also write the syntax there, e.g. Packages/User
#
# Sets <target>_OUTPUT_DIRECTORY in the caller's scope: add it to the include path of the consumers.
function(logos_genesis_generate kind target)
    cmake_parse_arguments(PARSE_ARGV 2 arg "" "GRAMMAR;MAPPING;OUTPUT_DIRECTORY;PREFIX;TESTS_DIRECTORY;INSTALL_DIRECTORY" "INCLUDE")
    if(NOT arg_GRAMMAR)
        message(FATAL_ERROR "logos_genesis_${kind}: GRAMMAR is required")
    endif()
    get_filename_component(grammar "${arg_GRAMMAR}" ABSOLUTE)
    if(NOT arg_OUTPUT_DIRECTORY)
        set(arg_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/generated/${target}")
    endif()
    set(mapping "")
    if(arg_MAPPING)
        get_filename_component(mapping "${arg_MAPPING}" ABSOLUTE)
    else()
        get_filename_component(grammar_directory "${grammar}" DIRECTORY)
        get_filename_component(grammar_stem "${grammar}" NAME_WE)
        if(EXISTS "${grammar_directory}/${grammar_stem}.mapping")
            set(mapping "${grammar_directory}/${grammar_stem}.mapping")
        endif()
    endif()

    set(tool_arguments ${kind} "${grammar}" -o "${arg_OUTPUT_DIRECTORY}" -q)
    if(kind STREQUAL "sublime" AND arg_INSTALL_DIRECTORY)
        list(APPEND tool_arguments --install "${arg_INSTALL_DIRECTORY}")
    endif()
    set(dependencies "${grammar}")
    if(mapping)
        list(APPEND tool_arguments -m "${mapping}")
        list(APPEND dependencies "${mapping}")
    endif()
    if(arg_PREFIX)
        list(APPEND tool_arguments --prefix "${arg_PREFIX}")
    endif()
    if(arg_TESTS_DIRECTORY)
        list(APPEND tool_arguments -t "${arg_TESTS_DIRECTORY}")
    endif()
    # Imports resolve next to the grammar and in the include folders; every grammar there may be an import.
    get_filename_component(grammar_directory "${grammar}" DIRECTORY)
    file(GLOB imported_grammars CONFIGURE_DEPENDS "${grammar_directory}/*.gen")
    list(APPEND dependencies ${imported_grammars})
    foreach(folder IN LISTS arg_INCLUDE)
        get_filename_component(folder "${folder}" ABSOLUTE)
        list(APPEND tool_arguments -I "${folder}")
        file(GLOB folder_grammars CONFIGURE_DEPENDS "${folder}/*.gen")
        list(APPEND dependencies ${folder_grammars})
    endforeach()
    list(REMOVE_DUPLICATES dependencies)

    set(stamp "${CMAKE_BINARY_DIR}/generated/stamps/${target}.stamp")
    add_custom_command(
        OUTPUT "${stamp}"
        COMMAND genesis ${tool_arguments}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/generated/stamps"
        COMMAND ${CMAKE_COMMAND} -E touch "${stamp}"
        DEPENDS genesis ${dependencies}
        COMMENT "Generating ${kind} for ${target}"
        VERBATIM)
    add_custom_target(${target} ALL DEPENDS "${stamp}")
    set(${target}_OUTPUT_DIRECTORY "${arg_OUTPUT_DIRECTORY}" PARENT_SCOPE)
endfunction()

function(logos_genesis_bindings target)
    logos_genesis_generate(bind ${target} ${ARGN})
    set(${target}_OUTPUT_DIRECTORY "${${target}_OUTPUT_DIRECTORY}" PARENT_SCOPE)
endfunction()

function(logos_genesis_sublime target)
    logos_genesis_generate(sublime ${target} ${ARGN})
    set(${target}_OUTPUT_DIRECTORY "${${target}_OUTPUT_DIRECTORY}" PARENT_SCOPE)
endfunction()

# Writes one of the languages the genesis tool knows by heart (the Genesis language itself, or the language of mapping
# files) as <OUTPUT_DIRECTORY>/<Name>.gen with its editor mapping <Name>.mapping next to it. They are written from the
# language definitions in C, so they cannot differ from the compiler or the tool they describe, and are rewritten only
# when their text changes.
#
#   logos_genesis_builtin(<target> NAME <genesis|mapping> [OUTPUT_DIRECTORY <dir>])
#
# Sets <target>_GRAMMAR and <target>_MAPPING to the two files.
function(logos_genesis_builtin target)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "NAME;OUTPUT_DIRECTORY" "")
    if(arg_NAME STREQUAL "genesis")
        set(stem "Genesis")
    elseif(arg_NAME STREQUAL "mapping")
        set(stem "GenesisMapping")
    else()
        message(FATAL_ERROR "logos_genesis_builtin: NAME must be genesis or mapping")
    endif()
    if(NOT arg_OUTPUT_DIRECTORY)
        set(arg_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/generated/${target}")
    endif()
    set(stamp "${CMAKE_BINARY_DIR}/generated/stamps/${target}.stamp")
    add_custom_command(
        OUTPUT "${stamp}"
        BYPRODUCTS "${arg_OUTPUT_DIRECTORY}/${stem}.gen" "${arg_OUTPUT_DIRECTORY}/${stem}.mapping"
        COMMAND genesis export --builtin ${arg_NAME} -o "${arg_OUTPUT_DIRECTORY}/${stem}.gen" -M "${arg_OUTPUT_DIRECTORY}/${stem}.mapping" -q
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/generated/stamps"
        COMMAND ${CMAKE_COMMAND} -E touch "${stamp}"
        DEPENDS genesis
        COMMENT "Writing the ${stem} language"
        VERBATIM)
    add_custom_target(${target} ALL DEPENDS "${stamp}")
    set(${target}_GRAMMAR "${arg_OUTPUT_DIRECTORY}/${stem}.gen" PARENT_SCOPE)
    set(${target}_MAPPING "${arg_OUTPUT_DIRECTORY}/${stem}.mapping" PARENT_SCOPE)
endfunction()
