include(CheckFunctionExists)

##
## var2define <VARIABLE NAMES...>
##
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

##
## var2define <NAME> [DEFINED_VALUE] [VAR_NAME]
##
function(VAR2DEFINE NAME)
  if("${ARGC}" GREATER 2)
    list(GET ARGN 1 VAR_NAME)
  else("${ARGC}" GREATER 2)
    set(VAR_NAME "${NAME}")
  endif("${ARGC}" GREATER 2)

  set(VALUE "${${VAR_NAME}}")

  if("${ARGC}" LESS_EQUAL 1)
    if("${VALUE}")
      add_definitions(-D${NAME}=1)
    else("${VALUE}")
      add_definitions(-D${NAME}=0)
    endif("${VALUE}")
  else("${ARGC}" LESS_EQUAL 1)
    if("${VALUE}")
      list(GET ARGN 0 DEFINED_VALUE)
      add_definitions(-D${NAME}=${DEFINED_VALUE})
    endif("${VALUE}")
  endif("${ARGC}" LESS_EQUAL 1)

endfunction(VAR2DEFINE NAME)

##
## canonicalize <OUTPUT VARIABLE> <STR>
##
function(CANONICALIZE OUTPUT_VAR STR)
  string(REGEX REPLACE "^-W" "WARN_" TMP_STR "${STR}")

  string(REGEX REPLACE "-" "_" TMP_STR "${TMP_STR}")
  string(TOUPPER "${TMP_STR}" TMP_STR)

  set("${OUTPUT_VAR}" "${TMP_STR}" PARENT_SCOPE)
endfunction(CANONICALIZE OUTPUT_VAR STR)

##
## basename <OUTPUT VARIABLE> <STR>
##
function(BASENAME OUTPUT_VAR STR)
  string(REGEX REPLACE ".*/" "" TMP_STR "${STR}")
  if(ARGN)
    string(REGEX REPLACE "\\${ARGN}\$" "" TMP_STR "${TMP_STR}")
  endif(ARGN)

  set("${OUTPUT_VAR}" "${TMP_STR}" PARENT_SCOPE)
endfunction(BASENAME OUTPUT_VAR FILE)

##
## dirname <OUTPUT VARIABLE> <STR>
##
function(DIRNAME OUTPUT_VAR STR)
  string(REGEX REPLACE "/[^/]+/*$" "" TMP_STR "${STR}")
  if(ARGN)
    string(REGEX REPLACE "\\${ARGN}\$" "" TMP_STR "${TMP_STR}")
  endif(ARGN)

  set("${OUTPUT_VAR}" "${TMP_STR}" PARENT_SCOPE)
endfunction(DIRNAME OUTPUT_VAR FILE)

##
## addprefix <OUTPUT VARIABLE> <PREFIX>
##
function(ADDPREFIX OUTPUT_VAR PREFIX)
  set(OUTPUT "")
  foreach(ARG ${ARGN})
    list(APPEND OUTPUT "${PREFIX}${ARG}")
  endforeach(ARG ${ARGN})
  set("${OUTPUT_VAR}" "${OUTPUT}" PARENT_SCOPE)
endfunction(ADDPREFIX OUTPUT_VAR PREFIX)

##
## addsuffix <OUTPUT VARIABLE> <PREFIX>
##
function(ADDSUFFIX OUTPUT_VAR SUFFIX)
  set(OUTPUT "")
  foreach(ARG ${ARGN})
    list(APPEND OUTPUT "${ARG}${SUFFIX}")
  endforeach(ARG ${ARGN})
  set("${OUTPUT_VAR}" "${OUTPUT}" PARENT_SCOPE)
endfunction(ADDSUFFIX OUTPUT_VAR SUFFIX)

##
## relative_path <OUTPUT VARIABLE> <RELATIVE_TO>
##
function(RELATIVE_PATH OUT_VAR RELATIVE_TO)
  set(LIST "")

  foreach(ARG ${ARGN})
    file(RELATIVE_PATH ARG "${RELATIVE_TO}" "${ARG}")
    list(APPEND LIST "${ARG}")
  endforeach(ARG ${ARGN})

  set("${OUT_VAR}" "${LIST}" PARENT_SCOPE)
endfunction(RELATIVE_PATH RELATIVE_TO OUT_VAR)

##
## check_function_def <FUNCTION NAME> [RESULT VARIABLE] [PREPROC_DEF]
##
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
      set("${RESULT_VAR}" TRUE CACHE INTERNAL "Define this if you have the '${FUNC}' function")
    else(${_${RESULT_VAR}})
      set("${RESULT_VAR}" FALSE CACHE INTERNAL "Define this if you have the '${FUNC}' function")
    endif(${_${RESULT_VAR}})
  endif(NOT DEFINED ${RESULT_VAR})

  set(DEFINE FALSE)

  if(${${RESULT_VAR}})
    if(NOT "${PREPROC_DEF}" STREQUAL "")
      set("${PREPROC_DEF}" "1")
      var2define("${PREPROC_DEF}" 1)
    endif(NOT "${PREPROC_DEF}" STREQUAL "")
  endif(${${RESULT_VAR}})

  #message("${RESULT_VAR}: ${${RESULT_VAR}}")

  list(APPEND CHECKED_FUNCTIONS "${FUNC}")
endmacro(CHECK_FUNCTION_DEF FUNC)

##
## check_functions <FUNCTION NAMES...>
##
macro(CHECK_FUNCTIONS)
  foreach(FUNC ${ARGN})
    string(TOUPPER "HAVE_${FUNC}" RESULT_VAR)
    check_function_def("${FUNC}" "${RESULT_VAR}")
  endforeach(FUNC ${ARGN})
endmacro(CHECK_FUNCTIONS)

##
## check_functions_def <FUNCTION NAMES...>
##
macro(CHECK_FUNCTIONS_DEF)
  foreach(FUNC ${ARGN})
    check_function_def("${FUNC}")
  endforeach(FUNC ${ARGN})
endmacro(CHECK_FUNCTIONS_DEF)

##
## clean_name <STRING> <OUTPUT VAR>
##
function(CLEAN_NAME STR OUTPUT_VAR)
  string(TOUPPER "${STR}" STR)
  string(REGEX REPLACE "[^A-Za-z0-9_]" "_" STR "${STR}")
  set("${OUTPUT_VAR}" "${STR}" PARENT_SCOPE)
endfunction(CLEAN_NAME STR OUTPUT_VAR)

##
## check_include_def <INCLUDE> [RESULT VARIABLE] [PREPROC_DEF]
##
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
    set("${RESULT_VAR}" TRUE CACHE INTERNAL "Define this if you have the '${INC}' header file")

    if(NOT "${PREPROC_DEF}" STREQUAL "")
      var2define("${PREPROC_DEF}" 1)
    endif(NOT "${PREPROC_DEF}" STREQUAL "")
  endif(${${RESULT_VAR}})

  list(APPEND CHECKED_INCLUDES "${INC}")
endmacro(CHECK_INCLUDE_DEF INC)

##
## check_includes <INCLUDE FILES...>
##
macro(CHECK_INCLUDES)
  foreach(INC ${ARGN})
    clean_name("HAVE_${INC}" RESULT_VAR)
    check_include_def("${INC}" "${RESULT_VAR}")
  endforeach(INC ${ARGN})
endmacro(CHECK_INCLUDES)

##
## check_includes_def <INCLUDE FILES...>
##
macro(CHECK_INCLUDES_DEF)
  foreach(INC ${ARGN})
    check_include_def("${INC}")
  endforeach(INC ${ARGN})
endmacro(CHECK_INCLUDES_DEF)

##
## check_function_and_include <FUNCTION> <INCLUDE>
##
macro(CHECK_FUNCTION_AND_INCLUDE FUNC INC)
  clean_name("HAVE_${INC}" INC_RESULT)
  clean_name("HAVE_${FUNC}" FUNC_RESULT)

  check_include_def("${INC}" "${INC_RESULT}" "${INC_RESULT}")

  if(${${INC_RESULT}})
    check_function_def("${FUNC}" "${FUNC_RESULT}" "${FUNC_RESULT}")
  endif(${${INC_RESULT}})
endmacro(CHECK_FUNCTION_AND_INCLUDE FUNC INC)

##
## check_include_cxx_def <INCLUDE> [RESULT VARIABLE] [PREPROC_DEF]
##
macro(CHECK_INCLUDE_CXX_DEF INC)
  if(ARGC GREATER_EQUAL 2)
    set(RESULT_VAR "${ARGV1}")
    set(PREPROC_DEF "${ARGV2}")
  else(ARGC GREATER_EQUAL 2)
    clean_name("${INC}" INC_D)
    string(TOUPPER "HAVE_${INC_D}" RESULT_VAR)
    string(TOUPPER "HAVE_${INC_D}" PREPROC_DEF)
  endif(ARGC GREATER_EQUAL 2)

  check_include_file_cxx("${INC}" "${RESULT_VAR}")

  if(${${RESULT_VAR}})
    set("${RESULT_VAR}" TRUE CACHE INTERNAL "Define this if you have the '${INC}' header file")

    if(NOT "${PREPROC_DEF}" STREQUAL "")
      var2define("${PREPROC_DEF}" 1)
    endif(NOT "${PREPROC_DEF}" STREQUAL "")
  endif(${${RESULT_VAR}})
endmacro(CHECK_INCLUDE_CXX_DEF INC)

##
## append_parent <VARIABLE NAME>
##
macro(APPEND_PARENT VAR)
  set(LIST "${${VAR}}")
  list(APPEND LIST ${ARGN})
  set("${VAR}" "${LIST}" PARENT_SCOPE)
endmacro(APPEND_PARENT VAR)

##
## contains <LIST NAME> <VALUE> <OUTPUT VARIABE>
##
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

##
## add_unique <LIST NAME> <VALUES...>
##
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

##
## symlink <TARGET> <SYMLINK PATH>
##
macro(SYMLINK TARGET LINK_NAME)
  install(
    CODE "message(\"Create symlink '$ENV{DESTDIR}${LINK_NAME}' to '${TARGET}'\")\nexecute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink ${TARGET} $ENV{DESTDIR}${LINK_NAME})"
  )
endmacro(SYMLINK TARGET LINK_NAME)

##
## rpath_append <VARIABLE NAME>
##
macro(RPATH_APPEND VAR)
  foreach(VALUE ${ARGN})
    if("${${VAR}}" STREQUAL "")
      set(${VAR} "${VALUE}")
    else("${${VAR}}" STREQUAL "")
      set(${VAR} "${CMAKE_INSTALL_RPATH}:${VALUE}")
    endif("${${VAR}}" STREQUAL "")
  endforeach(VALUE ${ARGN})
endmacro(RPATH_APPEND VAR)

##
## try_code <FILENAME> <CODE> <RESULT VARIABLE> <OUTPUT VARIABLE> <LIBS> <LINKER FLAGS>
##
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

##
## check_external <NAME> <LIBS> <LINKER FLAGS> <OUTPUT VARIABLE>
##
function(CHECK_EXTERNAL NAME LIBS LDFLAGS OUTPUT_VAR)
  try_code("test-${NAME}.c" "\n  extern int ${NAME}(void);\n  int main() {\n    ${NAME}();\n    return 0;\n  }\n  "
           "${OUTPUT_VAR}" OUT "${LIBS}" "${LDFLAGS}")
  #dump(OUTPUT_VAR OUT)
endfunction(CHECK_EXTERNAL NAME LIBS LDFLAGS OUTPUT_VAR)

##
## run_code <FILENAME> <CODE> <RESULT VARIABLE> <OUTPUT VARIABLE> <LIBS> <LINKER FLAGS>
##
function(RUN_CODE FILE CODE RESULT_VAR OUTPUT_VAR LIBS LDFLAGS)
  string(RANDOM LENGTH 8 RND)
  set(FN "${CMAKE_CURRENT_BINARY_DIR}/${RND}-${FILE}")
  file(WRITE "${FN}" "${CODE}")
  string(REGEX REPLACE "\.[^./]+$" ".log" LOG "${FN}")

  try_run(RUN_RESULT COMPILE_RESULT SOURCES "${FN}" COMPILE_OUTPUT_VARIABLE COMPILE_OUTPUT
          RUN_OUTPUT_VARIABLE RUN_OUTPUT CMAKE_FLAGS "${CMAKE_REQUIRED_FLAGS}"
          COMPILE_DEFINITIONS "${CMAKE_REQUIRED_DEFINITIONS}" LINK_OPTIONS "${LDFLAGS}" LINK_LIBRARIES "${LIBS}")

  file(WRITE "${LOG}" "Compile output:\n${COMPILE_OUTPUT}\n\nRun output:\n${RUN_OUTPUT}\n")
  unset(LOG)

  set(${RESULT_VAR} "${COMPILE_RESULT}" PARENT_SCOPE)
  set(${OUTPUT_VAR} "${COMPILE_OUTPUT}" PARENT_SCOPE)

  file(REMOVE "${FN}")

  if(COMPILE_RESULT)
    if(NOT "${RUN_RESULT}" STREQUAL "")
      set(${RESULT_VAR} "${RUN_RESULT}" PARENT_SCOPE)
    endif(NOT "${RUN_RESULT}" STREQUAL "")
    if(NOT "${RUN_OUTPUT}" STREQUAL "")
      set(${OUTPUT_VAR} "${RUN_OUTPUT}" PARENT_SCOPE)
    endif(NOT "${RUN_OUTPUT}" STREQUAL "")
  endif(COMPILE_RESULT)

  file(REMOVE "${FN}")
  unset(FN)
  unset(RND)
endfunction()

##
## libname <OUTPUT VARIABLE> <FILENAME>
##
function(LIBNAME OUT_VAR FILENAME)
  string(REGEX REPLACE ".*/(lib|)" "" LIBNAME "${FILENAME}")
  string(REGEX REPLACE "\.[^/.]+$" "" LIBNAME "${LIBNAME}")

  set(${OUT_VAR} "${LIBNAME}" PARENT_SCOPE)
endfunction(LIBNAME OUT_VAR FILENAME)

##
## check_flag <FLAG> <VARIABLE>
##
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
