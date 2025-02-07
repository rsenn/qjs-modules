include(CheckCCompilerFlag)

macro(append_vars STR)
  foreach(L ${ARGN})
    set(LIST "${${L}}")
    if(NOT LIST MATCHES ".*${STR}.*")
      if("${LIST}" STREQUAL "")
        set(LIST "${STR}")
      else("${LIST}" STREQUAL "")
        set(LIST "${LIST} ${STR}")
      endif("${LIST}" STREQUAL "")

    endif(NOT LIST MATCHES ".*${STR}.*")
    string(REPLACE ";" " " LIST "${LIST}")
    # message("New value for ${L}: ${LIST}")
    set("${L}" "${LIST}" PARENT_SCOPE)
  endforeach(L ${ARGN})
endmacro(append_vars STR)

function(check_flag FLAG VAR)
  if(NOT VAR OR VAR STREQUAL "")
    string(TOUPPER "${FLAG}" TMP)
    string(REGEX REPLACE "[^0-9A-Za-z]" _ VAR "${TMP}")
  endif(NOT VAR OR VAR STREQUAL "")
  set(CMAKE_REQUIRED_QUIET ON)
  check_c_compiler_flag("${FLAG}" "${VAR}")
  set(CMAKE_REQUIRED_QUIET OFF)

  set(RESULT "${${VAR}}")
  if(RESULT)
    append_vars(${FLAG} ${ARGN})

    message(STATUS "Compiler flag ${FLAG} ... supported")

  endif(RESULT)
endfunction(check_flag FLAG VAR)

macro(check_flags FLAGS)
  # message("Checking flags ${FLAGS} ${ARGN}")
  foreach(FLAG ${FLAGS})
    check_flag(${FLAG} "" ${ARGN})
  endforeach(FLAG ${FLAGS})
endmacro(check_flags FLAGS)

macro(NOWARN_FLAG FLAG)
  canonicalize(VARNAME "${FLAG}")
  check_c_compiler_flag("${FLAG}" "${VARNAME}")

  if(${VARNAME})
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${FLAG}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${FLAG}")

  endif(${VARNAME})
endmacro(NOWARN_FLAG FLAG)

macro(ADD_NOWARN_FLAGS)
  string(REGEX REPLACE " -Wall" "" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
  string(REGEX REPLACE " -Wall" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

  nowarn_flag(-Wno-unused-value)
  nowarn_flag(-Wno-unused-variable)

  if("${CMAKE_CXX_COMPILER_ID}" MATCHES ".*Clang.*")
    nowarn_flag(-Wno-deprecated-anon-enum-enum-conversion)
    nowarn_flag(-Wno-extern-c-compat)
    nowarn_flag(-Wno-implicit-int-float-conversion)
    nowarn_flag(-Wno-deprecated-enum-enum-conversion)
  endif("${CMAKE_CXX_COMPILER_ID}" MATCHES ".*Clang.*")

endmacro(ADD_NOWARN_FLAGS)

#[[
check_c_compiler_flag("-Wno-unused-value" WARN_NO_UNUSED_VALUE)
if(WARN_NO_UNUSED_VALUE)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unused-value" )
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-value" )
endif(WARN_NO_UNUSED_VALUE)


check_c_compiler_flag("-Wno-unused-variable" WARN_NO_UNUSED_VARIABLE)
if(WARN_NO_UNUSED_VARIABLE)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unused-variable" )
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-variable" )
endif(WARN_NO_UNUSED_VARIABLE)
]]
