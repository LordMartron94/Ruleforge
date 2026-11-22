# Runs `ruleforge compile` on an error case and checks the exit status (1) and that the diagnostics hold the expected text.
#   cmake -DRULEFORGE=<exe> -DCASE=<dir>/<name> -P run_error_case.cmake
# <name>.rf is the entry, <name>.expect holds the text that must appear, <name>.args (optional) extra arguments.
get_filename_component(case_directory "${CASE}" DIRECTORY)
file(READ "${CASE}.expect" expected)
string(STRIP "${expected}" expected)
set(extra_arguments "")
if(EXISTS "${CASE}.args")
    file(READ "${CASE}.args" args_text)
    string(STRIP "${args_text}" args_text)
    separate_arguments(extra_arguments NATIVE_COMMAND "${args_text}")
endif()
execute_process(
    COMMAND "${RULEFORGE}" compile "${CASE}.rf" ${extra_arguments}
    WORKING_DIRECTORY "${case_directory}"
    RESULT_VARIABLE status
    OUTPUT_VARIABLE standard_output
    ERROR_VARIABLE standard_error)
if(NOT status EQUAL 1)
    message(FATAL_ERROR "expected exit status 1, got ${status}\n${standard_error}")
endif()
string(FIND "${standard_error}" "${expected}" found)
if(found EQUAL -1)
    message(FATAL_ERROR "the diagnostics do not contain \"${expected}\":\n${standard_error}")
endif()
