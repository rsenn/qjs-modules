# Compat.cmake: helpers qjs-modules uses that cmake/Functions.cmake and
# cmake/Checks.cmake (shared with shish) do not have.
include(CheckCCompilerFlag)
include(CheckCXXCompilerFlag)
include(CheckFunctionExists)
include(CheckIncludeFileCXX)

##
## canonicalize <OUTPUT-VARIABLE> <STR>
##
function(CANONICALIZE OUTPUT_VAR STR)
  string(REGEX REPLACE "^-W" "WARN_" TMP_STR "${STR}")

  string(REGEX REPLACE "-" "_" TMP_STR "${TMP_STR}")
  string(TOUPPER "${TMP_STR}" TMP_STR)

  set("${OUTPUT_VAR}" "${TMP_STR}" PARENT_SCOPE)
endfunction(CANONICALIZE OUTPUT_VAR STR)

##
## dirname <OUTPUT-VARIABLE> <STR>
##
function(DIRNAME OUTPUT_VAR STR)
  string(REGEX REPLACE "/[^/]+/*$" "" TMP_STR "${STR}")
  if(ARGN)
    string(REGEX REPLACE "\\${ARGN}\$" "" TMP_STR "${TMP_STR}")
  endif(ARGN)

  set("${OUTPUT_VAR}" "${TMP_STR}" PARENT_SCOPE)
endfunction(DIRNAME OUTPUT_VAR FILE)

##
## addprefix <OUTPUT-VARIABLE> <PREFIX>
##
function(ADDPREFIX OUTPUT_VAR PREFIX)
  set(OUTPUT "")
  foreach(ARG ${ARGN})
    list(APPEND OUTPUT "${PREFIX}${ARG}")
  endforeach(ARG ${ARGN})
  set("${OUTPUT_VAR}" "${OUTPUT}" PARENT_SCOPE)
endfunction(ADDPREFIX OUTPUT_VAR PREFIX)

##
## addsuffix <OUTPUT-VARIABLE> <PREFIX>
##
function(ADDSUFFIX OUTPUT_VAR SUFFIX)
  set(OUTPUT "")
  foreach(ARG ${ARGN})
    list(APPEND OUTPUT "${ARG}${SUFFIX}")
  endforeach(ARG ${ARGN})
  set("${OUTPUT_VAR}" "${OUTPUT}" PARENT_SCOPE)
endfunction(ADDSUFFIX OUTPUT_VAR SUFFIX)

##
## relative_path <OUTPUT-VARIABLE> <RELATIVE_TO>
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
## check_include_cxx_def <INCLUDE> [RESULT-VARIABLE] [PREPROCESSOR-DEFINITION]
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
## append_parent <VARIABLE-NAME>
##
macro(APPEND_PARENT VAR)
  set(LIST "${${VAR}}")
  list(APPEND LIST ${ARGN})
  set("${VAR}" "${LIST}" PARENT_SCOPE)
endmacro(APPEND_PARENT VAR)

##
## contains <LIST-NAME> <VALUE> <OUTPUT-VARIABLE>
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
## add_unique <LIST-NAME> <VALUES...>
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
## symlink <TARGET> <SYMLINK-PATH>
##
macro(SYMLINK TARGET LINK_NAME)
  install(
    CODE "message(\"Create symlink '$ENV{DESTDIR}${LINK_NAME}' to '${TARGET}'\")\nexecute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink ${TARGET} $ENV{DESTDIR}${LINK_NAME})"
  )
endmacro(SYMLINK TARGET LINK_NAME)

##
## rpath_append <VARIABLE-NAME>
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
## try_code <FILENAME> <CODE> <RESULT-VARIABLE> <OUTPUT-VARIABLE> <LIBS> <LINKER-FLAGS>
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
## check_external <NAME> <LIBS> <LINKER-FLAGS> <OUTPUT-VARIABLE>
##
function(CHECK_EXTERNAL NAME LIBS LDFLAGS OUTPUT_VAR)
  try_code("test-${NAME}.c" "\n  extern int ${NAME}(void);\n  int main() {\n    ${NAME}();\n    return 0;\n  }\n  "
           "${OUTPUT_VAR}" OUT "${LIBS}" "${LDFLAGS}")
  #dump(OUTPUT_VAR OUT)
endfunction(CHECK_EXTERNAL NAME LIBS LDFLAGS OUTPUT_VAR)

##
## run_code <FILENAME> <CODE> <RESULT-VARIABLE> <OUTPUT-VARIABLE> <LIBS> <LINKER-FLAGS>
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
## libname <OUTPUT-VARIABLE> <FILENAME>
##
function(LIBNAME OUT_VAR FILENAME)
  string(REGEX REPLACE ".*/(lib|)" "" LIBNAME "${FILENAME}")
  string(REGEX REPLACE "\.[^/.]+$" "" LIBNAME "${LIBNAME}")

  set(${OUT_VAR} "${LIBNAME}" PARENT_SCOPE)
endfunction(LIBNAME OUT_VAR FILENAME)


#
# append_vars <STR> <VARS...>: append STR to each space-separated variable
#
macro(append_vars STR)
  foreach(L ${ARGN})
    set(LIST "${${L}}")
    if(NOT LIST MATCHES ".*${STR}.*")
      if("${LIST}" STREQUAL "")
        set(LIST "${STR}")
      else()
        set(LIST "${LIST} ${STR}")
      endif()
    endif()
    string(REPLACE ";" " " LIST "${LIST}")
    set("${L}" "${LIST}" PARENT_SCOPE)
  endforeach()
endmacro()

#
# check_flag <FLAG> <VAR> [FLAG-VARS...]: if the compiler takes FLAG, add it to FLAG-VARS
#
function(check_flag FLAG VAR)
  if(NOT VAR OR VAR STREQUAL "")
    string(TOUPPER "${FLAG}" TMP)
    string(REGEX REPLACE "[^0-9A-Za-z]" _ VAR "${TMP}")
  endif()

  set(CMAKE_REQUIRED_QUIET ON)
  check_c_compiler_flag("${FLAG}" "${VAR}")
  set(CMAKE_REQUIRED_QUIET OFF)

  if(${VAR})
    append_vars(${FLAG} ${ARGN})
    message(STATUS "Compiler flag ${FLAG}: supported")
  else()
    message(STATUS "Compiler flag ${FLAG}: not supported")
  endif()
endfunction()

macro(check_flags FLAGS)
  foreach(FLAG ${FLAGS})
    check_flag(${FLAG} "" ${ARGN})
  endforeach()
endmacro()

#
# nowarn_flag <FLAG>: add a -Wno-* flag to C and C++ flags when supported (silently)
#
macro(nowarn_flag FLAG)
  canonicalize(VARNAME "${FLAG}")
  set(CMAKE_REQUIRED_QUIET ON)
  check_c_compiler_flag("${FLAG}" "${VARNAME}")
  set(CMAKE_REQUIRED_QUIET OFF)

  if(${VARNAME})
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${FLAG}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${FLAG}")
  endif()
endmacro()

macro(add_nowarn_flags)
  string(REGEX REPLACE " -Wall" "" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
  string(REGEX REPLACE " -Wall" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

  nowarn_flag(-Wno-unused-value)
  nowarn_flag(-Wno-unused-variable)

  if("${CMAKE_CXX_COMPILER_ID}" MATCHES ".*Clang.*")
    nowarn_flag(-Wno-deprecated-anon-enum-enum-conversion)
    nowarn_flag(-Wno-extern-c-compat)
    nowarn_flag(-Wno-implicit-int-float-conversion)
    nowarn_flag(-Wno-deprecated-enum-enum-conversion)
  endif()
endmacro()

#
# message_table <TITLE> [KEY VALUE]...
#
# One status line for TITLE, then the rows aligned under it; rows with an
# empty value are left out, a list value goes one item to a line.
#
#   -- QuickJS
#   --   interpreter  /usr/local/bin/qjs
#   --   library      /usr/local/lib/libquickjs.so
#
function(message_table TITLE)
  set(WIDTH 0)
  math(EXPR LAST "${ARGC} - 1")

  foreach(I RANGE 1 ${LAST} 2)
    string(LENGTH "${ARGV${I}}" LEN)
    if(LEN GREATER WIDTH)
      set(WIDTH ${LEN})
    endif()
  endforeach()

  message(STATUS "${TITLE}")

  foreach(I RANGE 1 ${LAST} 2)
    math(EXPR J "${I} + 1")
    set(KEY "${ARGV${I}}")
    set(VALUE "${ARGV${J}}")

    if(NOT VALUE STREQUAL "")
      string(LENGTH "${KEY}" LEN)
      while(LEN LESS WIDTH)
        set(KEY "${KEY} ")
        math(EXPR LEN "${LEN} + 1")
      endwhile()

      set(PAD "")
      string(REGEX REPLACE "." " " PAD "${KEY}")

      set(FIRST TRUE)
      foreach(ITEM ${VALUE})
        if(FIRST)
          message(STATUS "  ${KEY}  ${ITEM}")
          set(FIRST FALSE)
        else()
          message(STATUS "  ${PAD}  ${ITEM}")
        endif()
      endforeach()
    endif()
  endforeach()
endfunction()
