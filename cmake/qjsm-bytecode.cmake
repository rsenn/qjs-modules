# qjsm-bytecode.cmake: how much bytecode qjsc generated for qjsm.
#
# include()d it defines qjsm_bytecode_size() and qjsm_format_bytes(); run as a script
# it prints the total for the files given, as the POST_BUILD step of qjsm does:
#
#   cmake -DFILES="modules/a.c|modules/b.c" -P cmake/qjsm-bytecode.cmake
#
# the same sum as `awk '{sum += $5} END {print sum}' modules/*.c`: every file qjsc writes
# has one line `const uint32_t qjsc_<name>_size = <bytes>;`.

#
# qjsm_bytecode_size <TOTAL-VAR> <COUNT-VAR> [FILES...]
#
# Add up the bytecode sizes in the generated C FILES. COUNT-VAR is the number of FILES
# that exist (qjsc writes them during the build, so right after configuring there are none).
#
function(qjsm_bytecode_size TOTAL_VAR COUNT_VAR)
  set(TOTAL 0)
  set(COUNT 0)

  foreach(FILE ${ARGN})
    if(EXISTS "${FILE}")
      math(EXPR COUNT "${COUNT} + 1")
      file(STRINGS "${FILE}" SIZE_LINES REGEX "^const uint32_t qjsc_[A-Za-z0-9_]+_size = [0-9]+;")

      foreach(SIZE_LINE ${SIZE_LINES})
        string(REGEX REPLACE "^.* = ([0-9]+);.*$" "\\1" BYTES "${SIZE_LINE}")
        math(EXPR TOTAL "${TOTAL} + ${BYTES}")
      endforeach()
    endif()
  endforeach()

  set("${TOTAL_VAR}" "${TOTAL}" PARENT_SCOPE)
  set("${COUNT_VAR}" "${COUNT}" PARENT_SCOPE)
endfunction()

#
# qjsm_format_bytes <OUTPUT-VAR> <BYTES>
#
#   qjsm_format_bytes(TEXT 656783)   ->   "641.4 KiB (656783 bytes)"
#
function(qjsm_format_bytes OUTPUT_VAR BYTES)
  math(EXPR TENTHS "${BYTES} * 10 / 1024")
  math(EXPR WHOLE "${TENTHS} / 10")
  math(EXPR FRACTION "${TENTHS} % 10")
  set("${OUTPUT_VAR}" "${WHOLE}.${FRACTION} KiB (${BYTES} bytes)" PARENT_SCOPE)
endfunction()

if(CMAKE_SCRIPT_MODE_FILE STREQUAL CMAKE_CURRENT_LIST_FILE)
  string(REPLACE "|" ";" FILES "${FILES}")
  qjsm_bytecode_size(TOTAL COUNT ${FILES})
  qjsm_format_bytes(TEXT ${TOTAL})
  message(STATUS "qjsm: ${TEXT} of bytecode compiled in, from ${COUNT} JS modules")
endif()
