# Compiles an example project and compares the filter, and a simulation of its items, with the checked-in golden files.
#   cmake -DRULEFORGE=<exe> -DPROJECT=<dir> -DGOLDEN=<dir> -DOUTPUT=<dir> -P run_golden_case.cmake
file(MAKE_DIRECTORY "${OUTPUT}")
execute_process(
    COMMAND "${RULEFORGE}" compile entry.rf --settings settings.toml -o "${OUTPUT}/currency.filter" --model "${OUTPUT}/currency.model.json"
    WORKING_DIRECTORY "${PROJECT}"
    RESULT_VARIABLE status
    ERROR_VARIABLE standard_error)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "compile failed (${status}):\n${standard_error}")
endif()
execute_process(
    COMMAND "${RULEFORGE}" simulate entry.rf --settings settings.toml --items items.json
    WORKING_DIRECTORY "${PROJECT}"
    RESULT_VARIABLE status
    OUTPUT_FILE "${OUTPUT}/currency.simulation.json"
    ERROR_VARIABLE standard_error)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "simulate failed (${status}):\n${standard_error}")
endif()
foreach(product IN ITEMS currency.filter currency.simulation.json)
    # Line endings are not part of the contract: compare with CRLF folded to LF.
    file(READ "${OUTPUT}/${product}" actual)
    file(READ "${GOLDEN}/${product}" expected)
    string(REPLACE "\r\n" "\n" actual "${actual}")
    string(REPLACE "\r\n" "\n" expected "${expected}")
    if(NOT actual STREQUAL expected)
        message(FATAL_ERROR "${product} differs from ${GOLDEN}/${product}; compare ${OUTPUT}/${product}")
    endif()
endforeach()
