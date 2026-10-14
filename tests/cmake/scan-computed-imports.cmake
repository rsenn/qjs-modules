# cmake -P: qjsm_scan_computed_imports() must keep each import(<expr>) call whole, even when
# the expression contains ';' (CMake would split the match into several list items)
file(READ "${CMAKE_CURRENT_LIST_DIR}/../../cmake/qjsm.cmake" _src)
string(REGEX MATCH "function\\(qjsm_scan_computed_imports.*\nendfunction\\(\\)" _fn "${_src}")
if(NOT _fn)
  message(FATAL_ERROR "qjsm_scan_computed_imports() not found in cmake/qjsm.cmake")
endif()
cmake_language(EVAL CODE "${_fn}")

set(_js "${CMAKE_CURRENT_BINARY_DIR}/scan-computed-imports.js")
file(WRITE "${_js}" "import('./static.js');\nconst a = await import(path + '.js');\nawait import(f(x; y));\nawait import(`t`);\n")

qjsm_scan_computed_imports(_calls "${_js}")
list(LENGTH _calls _n)

if(NOT _n EQUAL 1)
  message(FATAL_ERROR "expected 1 computed import (path + '.js'), got ${_n}: ${_calls}")
endif()
if(NOT _calls STREQUAL "import(path + '.js')")
  message(FATAL_ERROR "unexpected call text: ${_calls}")
endif()
message(STATUS "scan-computed-imports: OK")
