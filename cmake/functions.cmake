function(DUMP)
  foreach(VAR ${ARGN})
    if("${SEPARATOR}" STREQUAL "")
      set(SEPARATOR "\n    ")
    endif("${SEPARATOR}" STREQUAL "")
    set("${VAR}" ${${VAR}})
    string(REGEX REPLACE "[ \t\n]+" "\n" A "${${VAR}}")
    string(REGEX REPLACE "\n" ";" A "${A}")
    string(REGEX REPLACE ";" "${SEPARATOR}" A "${A}")
    message("  ${VAR} = ${A}")
  endforeach(VAR ${ARGN})
endfunction(DUMP)

function(CANONICALIZE OUTPUT_VAR STR)
  string(REGEX REPLACE "^-W" "WARN_" TMP_STR "${STR}")

  string(REGEX REPLACE "-" "_" TMP_STR "${TMP_STR}")
  string(TOUPPER "${TMP_STR}" TMP_STR)

  set("${OUTPUT_VAR}" "${TMP_STR}" PARENT_SCOPE)
endfunction(CANONICALIZE OUTPUT_VAR STR)

function(BASENAME OUTPUT_VAR STR)
  string(REGEX REPLACE ".*/" "" TMP_STR "${STR}")
  if(ARGN)
    string(REGEX REPLACE "\\${ARGN}\$" "" TMP_STR "${TMP_STR}")
  endif(ARGN)

  set("${OUTPUT_VAR}" "${TMP_STR}" PARENT_SCOPE)
endfunction(BASENAME OUTPUT_VAR FILE)

function(DIRNAME OUTPUT_VAR STR)
  string(REGEX REPLACE "/[^/]+/*$" "" TMP_STR "${STR}")
  if(ARGN)
    string(REGEX REPLACE "\\${ARGN}\$" "" TMP_STR "${TMP_STR}")
  endif(ARGN)

  set("${OUTPUT_VAR}" "${TMP_STR}" PARENT_SCOPE)
endfunction(DIRNAME OUTPUT_VAR FILE)

function(ADDPREFIX OUTPUT_VAR PREFIX)
  set(OUTPUT "")
  foreach(ARG ${ARGN})
    list(APPEND OUTPUT "${PREFIX}${ARG}")
  endforeach(ARG ${ARGN})
  set("${OUTPUT_VAR}" "${OUTPUT}" PARENT_SCOPE)
endfunction(ADDPREFIX OUTPUT_VAR PREFIX)

function(ADDSUFFIX OUTPUT_VAR SUFFIX)
  set(OUTPUT "")
  foreach(ARG ${ARGN})
    list(APPEND OUTPUT "${ARG}${SUFFIX}")
  endforeach(ARG ${ARGN})
  set("${OUTPUT_VAR}" "${OUTPUT}" PARENT_SCOPE)
endfunction(ADDSUFFIX OUTPUT_VAR SUFFIX)

function(RELATIVE_PATH OUT_VAR RELATIVE_TO)
  set(LIST "")

  foreach(ARG ${ARGN})
    file(RELATIVE_PATH ARG "${RELATIVE_TO}" "${ARG}")
    list(APPEND LIST "${ARG}")
  endforeach(ARG ${ARGN})

  set("${OUT_VAR}" "${LIST}" PARENT_SCOPE)
endfunction(RELATIVE_PATH RELATIVE_TO OUT_VAR)

include(CheckFunctionExists)

macro(CHECK_FUNCTION_DEF FUNC)
  if(${ARGC} GREATER 1)
    set(RESULT_VAR "${ARGV1}")
  else(${ARGC} GREATER 1)
    string(TOUPPER "HAVE_${FUNC}" RESULT_VAR)
  endif(${ARGC} GREATER 1)
  
  if(${ARGC} GREATER 2)
    set(PREPROC_DEF "${ARGV2}")
  else(${ARGC} GREATER 2)
    string(TOUPPER "HAVE_${FUNC}" PREPROC_DEF)
  endif(${ARGC} GREATER 2)

  if(NOT DEFINED ${RESULT_VAR})
    check_function_exists("${FUNC}" "_${RESULT_VAR}")

    if(${_${RESULT_VAR}})
      #unset("${RESULT_VAR}")
      #unset("${RESULT_VAR}" CACHE)
      #set("${RESULT_VAR}" TRUE)
      set("${RESULT_VAR}" TRUE CACHE BOOL "Define this if you have the '${FUNC}' function")
    else(${_${RESULT_VAR}})
      #unset("${RESULT_VAR}")
      #unset("${RESULT_VAR}" CACHE)
      #set("${RESULT_VAR}" FALSE)
      set("${RESULT_VAR}" FALSE CACHE BOOL "Define this if you have the '${FUNC}' function")
    endif(${_${RESULT_VAR}})
  endif(NOT DEFINED ${RESULT_VAR})

  if(${${RESULT_VAR}})
      if(NOT "${PREPROC_DEF}" STREQUAL "")
        add_definitions(-D${PREPROC_DEF})
      endif(NOT "${PREPROC_DEF}" STREQUAL "")
  endif(${${RESULT_VAR}})
  #message("${RESULT_VAR}: ${${RESULT_VAR}}")
endmacro(CHECK_FUNCTION_DEF FUNC)

macro(CHECK_FUNCTIONS)
  foreach(FUNC ${ARGN})
    string(TOUPPER "HAVE_${FUNC}" RESULT_VAR)
    check_function_def("${FUNC}" "${RESULT_VAR}")
  endforeach(FUNC ${ARGN})
endmacro(CHECK_FUNCTIONS)

macro(CHECK_FUNCTIONS_DEF)
  foreach(FUNC ${ARGN})
    check_function_def("${FUNC}")
  endforeach(FUNC ${ARGN})
endmacro(CHECK_FUNCTIONS_DEF)

function(CLEAN_NAME STR OUTPUT_VAR)
  string(TOUPPER "${STR}" STR)
  string(REGEX REPLACE "[^A-Za-z0-9_]" "_" STR "${STR}")
  set("${OUTPUT_VAR}" "${STR}" PARENT_SCOPE)
endfunction(CLEAN_NAME STR OUTPUT_VAR)

macro(CHECK_INCLUDE_DEF INC)
  if(ARGC GREATER_EQUAL 2)
    set(RESULT_VAR "${ARGV1}")
    set(PREPROC_DEF "${ARGV2}")
  else(ARGC GREATER_EQUAL 2)
    clean_name("${INC}" INC_D)
    string(TOUPPER "HAVE_${INC_D}" RESULT_VAR)
    string(TOUPPER "HAVE_${INC_D}" PREPROC_DEF)
  endif(ARGC GREATER_EQUAL 2)
  check_include_file("${INC}" "${RESULT_VAR}")
  if(${${RESULT_VAR}})
    set("${RESULT_VAR}" TRUE CACHE BOOL "Define this if you have the '${INC}' header file")
    if(NOT "${PREPROC_DEF}" STREQUAL "")
      add_definitions(-D${PREPROC_DEF})
    endif(NOT "${PREPROC_DEF}" STREQUAL "")
  endif(${${RESULT_VAR}})
endmacro(CHECK_INCLUDE_DEF INC)

macro(CHECK_INCLUDES)
  foreach(INC ${ARGN})
    clean_name("HAVE_${INC}" RESULT_VAR)
    check_include_def("${INC}" "${RESULT_VAR}")
  endforeach(INC ${ARGN})
endmacro(CHECK_INCLUDES)

macro(CHECK_INCLUDES_DEF)
  foreach(INC ${ARGN})
    check_include_def("${INC}")
  endforeach(INC ${ARGN})
endmacro(CHECK_INCLUDES_DEF)

macro(CHECK_FUNCTION_AND_INCLUDE FUNC INC)
  clean_name("HAVE_${INC}" INC_RESULT)
  clean_name("HAVE_${FUNC}" FUNC_RESULT)

  check_include_def("${INC}" "${INC_RESULT}" "${INC_RESULT}")

  if(${${INC_RESULT}})
    check_function_def("${FUNC}" "${FUNC_RESULT}" "${FUNC_RESULT}")
  endif(${${INC_RESULT}})
endmacro(CHECK_FUNCTION_AND_INCLUDE FUNC INC)

macro(APPEND_PARENT VAR)
  set(LIST "${${VAR}}")
  list(APPEND LIST ${ARGN})
  set("${VAR}" "${LIST}" PARENT_SCOPE)
endmacro(APPEND_PARENT VAR)

function(CONTAINS LIST VALUE OUTPUT)
  list(FIND "${LIST}" "${VALUE}" INDEX)
  if(${INDEX} GREATER -1)
    set(RESULT TRUE)
  else(${INDEX} GREATER -1)
    set(RESULT FALSE)
  endif(${INDEX} GREATER -1)
  if(NOT RESULT)
    foreach(ITEM ${${LIST}})
      if("${ITEM}" STREQUAL "${VALUE}")
        set(RESULT TRUE)
      endif("${ITEM}" STREQUAL "${VALUE}")
    endforeach(ITEM ${${LIST}})
  endif(NOT RESULT)
  set("${OUTPUT}" "${RESULT}" PARENT_SCOPE)
endfunction(CONTAINS LIST VALUE OUTPUT)

function(ADD_UNIQUE LIST)
  set(RESULT "${${LIST}}")
  foreach(ITEM ${ARGN})
    contains(RESULT "${ITEM}" FOUND)
    if(NOT FOUND)
      list(APPEND RESULT "${ITEM}")
    endif(NOT FOUND)
  endforeach(ITEM ${ARGN})
  set("${LIST}" "${RESULT}" PARENT_SCOPE)
endfunction(ADD_UNIQUE LIST)

macro(SYMLINK TARGET LINK_NAME)
  install(
    CODE "message(\"Create symlink '$ENV{DESTDIR}${LINK_NAME}' to '${TARGET}'\")\nexecute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink ${TARGET} $ENV{DESTDIR}${LINK_NAME})"
  )
endmacro(SYMLINK TARGET LINK_NAME)

macro(RPATH_APPEND VAR)
  foreach(VALUE ${ARGN})
    if("${${VAR}}" STREQUAL "")
      set(${VAR} "${VALUE}")
    else("${${VAR}}" STREQUAL "")
      set(${VAR} "${CMAKE_INSTALL_RPATH}:${VALUE}")
    endif("${${VAR}}" STREQUAL "")
  endforeach(VALUE ${ARGN})
endmacro(RPATH_APPEND VAR)

function(TRY_CODE FILE CODE RESULT_VAR OUTPUT_VAR LIBS LDFLAGS)
  if(NOT DEFINED "${RESULT_VAR}" OR NOT DEFINED "${OUTPUT_VAR}")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/${FILE}" "${CODE}")

    try_compile(
      RESULT "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/${FILE}" CMAKE_FLAGS "${CMAKE_REQUIRED_FLAGS}"
      COMPILE_DEFINITIONS "${CMAKE_REQUIRED_DEFINITIONS}" LINK_OPTIONS "${LDFLAGS}" LINK_LIBRARIES "${LIBS}"
      OUTPUT_VARIABLE OUTPUT)

    set(${RESULT_VAR} "${RESULT}" PARENT_SCOPE)
    set(${OUTPUT_VAR} "${OUTPUT}" PARENT_SCOPE)
  endif(NOT DEFINED "${RESULT_VAR}" OR NOT DEFINED "${OUTPUT_VAR}")
endfunction()

function(CHECK_EXTERNAL NAME LIBS LDFLAGS OUTPUT_VAR)
  set(CMAKE_TRY_COMPILE_TARGET_TYPE EXECUTABLE)
  try_code(
    "test-${NAME}.c"
    "
  extern int ${NAME}(void);
  int main() {
    ${NAME}();
    return 0;
  }
  "
    "${OUTPUT_VAR}"
    OUT
    "${LIBS}"
    "${LDFLAGS}")
  #dump(${OUTPUT_VAR})
  set(${OUTPUT_VAR} "${${OUTPUT_VAR}}" PARENT_SCOPE)
endfunction(CHECK_EXTERNAL NAME LIBS LDFLAGS OUTPUT_VAR)

function(CHECK_FLAG FLAG VAR)
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
endfunction(CHECK_FLAG FLAG VAR)
