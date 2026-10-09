# Compat.cmake: helpers qjs-modules uses that cmake/Functions.cmake and
# cmake/Checks.cmake (shared with shish) do not have.
include(CheckCCompilerFlag)
include(CheckCXXCompilerFlag)
include(CheckFunctionExists)
include(CheckIncludeFileCXX)

#
# canonicalize <OUTPUT-VARIABLE> <STR>
#
# Store STR in OUTPUT-VARIABLE as an upper-case identifier: a leading -W
# becomes WARN_ and '-' becomes '_'.
#
function(canonicalize OUTPUT_VAR STR)
  string(REGEX REPLACE "^-W" "WARN_" TMP_STR "${STR}")
  string(REGEX REPLACE "-" "_" TMP_STR "${TMP_STR}")
  string(TOUPPER "${TMP_STR}" TMP_STR)
  set("${OUTPUT_VAR}" "${TMP_STR}" PARENT_SCOPE)
endfunction()

#
# decamelize <OUTPUT-VARIABLE> <STR>
#
function(decamelize OUTPUT_VAR STR)
  string(REGEX REPLACE "([^;])([A-Z])" "\\1;\\2" STR "${STR}")
  string(REGEX REPLACE "([^;])([A-Z])" "\\1;\\2" STR "${STR}")

  set(OUT "")
  foreach(PART ${STR})
    list(APPEND OUT "${PART}")
  endforeach()

  if(${ARGC} GREATER 2)
    set(SEP ${ARGV2})
  else()
    set(SEP "_")
  endif()
  list(JOIN OUT "${SEP}" STR)

  string(TOLOWER "${STR}" STR)
  set("${OUTPUT_VAR}" "${STR}" PARENT_SCOPE)
endfunction()

#
# ucfirst <OUTPUT-VARIABLE> <STR>
#
function(ucfirst OUTPUT_VAR STR)
  string(SUBSTRING "${STR}" 0 1 FIRST)
  string(TOUPPER "${FIRST}" FIRST)
  string(SUBSTRING "${STR}" 1 -1 REST)
  string(TOLOWER "${REST}" REST)
  set("${OUTPUT_VAR}" "${FIRST}${REST}" PARENT_SCOPE)
endfunction()

#
# camelize <OUTPUT-VARIABLE> <STR>
#
function(camelize OUTPUT_VAR STR)
  string(REGEX REPLACE "[^0-9A-Za-z]" ";" STR "${STR}")
  set(OUT "")
  foreach(PART ${STR})
    ucfirst(PART "${PART}")
    set(OUT "${OUT}${PART}")
  endforeach()
  set("${OUTPUT_VAR}" "${OUT}" PARENT_SCOPE)
endfunction()

#
# dirname <OUTPUT-VARIABLE> <STR>
#
# Store STR without its last path component (and without the extension in
# ARGN, if given) in OUTPUT-VARIABLE.
#
function(dirname OUTPUT_VAR STR)
  string(REGEX REPLACE "/[^/]+/*$" "" TMP_STR "${STR}")

  if(ARGN)
    string(REGEX REPLACE "\\${ARGN}\$" "" TMP_STR "${TMP_STR}")
  endif()

  set("${OUTPUT_VAR}" "${TMP_STR}" PARENT_SCOPE)
endfunction()

#
# addprefix <OUTPUT-VARIABLE> <PREFIX>
#
# Store the remaining arguments, each with PREFIX prepended, in
# OUTPUT-VARIABLE.
#
function(addprefix OUTPUT_VAR PREFIX)
  set(OUTPUT "")

  foreach(ARG ${ARGN})
    list(APPEND OUTPUT "${PREFIX}${ARG}")
  endforeach()

  set("${OUTPUT_VAR}" "${OUTPUT}" PARENT_SCOPE)
endfunction()

#
# addsuffix <OUTPUT-VARIABLE> <PREFIX>
#
# Store the remaining arguments, each with SUFFIX appended, in
# OUTPUT-VARIABLE.
#
function(addsuffix OUTPUT_VAR SUFFIX)
  set(OUTPUT "")

  foreach(ARG ${ARGN})
    list(APPEND OUTPUT "${ARG}${SUFFIX}")
  endforeach()

  set("${OUTPUT_VAR}" "${OUTPUT}" PARENT_SCOPE)
endfunction()

#
# relative_path <OUTPUT-VARIABLE> <RELATIVE_TO>
#
# Store the remaining arguments, made relative to RELATIVE_TO, in
# OUTPUT-VARIABLE.
#
function(relative_path OUT_VAR RELATIVE_TO)
  set(LIST "")

  foreach(ARG ${ARGN})
    file(RELATIVE_PATH ARG "${RELATIVE_TO}" "${ARG}")
    list(APPEND LIST "${ARG}")
  endforeach()

  set("${OUT_VAR}" "${LIST}" PARENT_SCOPE)
endfunction()

#
# check_include_cxx_def <INCLUDE> [RESULT-VARIABLE] [PREPROCESSOR-DEFINITION]
#
# Check for the C++ header INCLUDE; on success cache RESULT-VARIABLE and add
# -DPREPROCESSOR-DEFINITION=1 (names default to HAVE_<INCLUDE>) (a macro).
#
macro(check_include_cxx_def INC)
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
endmacro()

#
# contains <LIST-NAME> <VALUE> <OUTPUT-VARIABLE>
#
# Store TRUE in OUTPUT-VARIABLE if VALUE is an element of the list LIST-NAME,
# otherwise FALSE.
#
function(contains LIST VALUE OUTPUT)
  if(VALUE IN_LIST "${LIST}")
    set(RESULT TRUE)
  else()
    set(RESULT FALSE)
  endif()

  set("${OUTPUT}" "${RESULT}" PARENT_SCOPE)
endfunction()

#
# symlink <TARGET> <SYMLINK-PATH>
#
# At install time, create the symbolic link SYMLINK-PATH pointing to TARGET,
# honoring DESTDIR (a macro).
#
macro(symlink TARGET LINK_NAME)
  install(
    CODE "message(\"Create symlink '$ENV{DESTDIR}${LINK_NAME}' to '${TARGET}'\")\nexecute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink ${TARGET} $ENV{DESTDIR}${LINK_NAME})"
  )
endmacro()

#
# rpath_append <VARIABLE-NAME>
#
# Append the remaining arguments to the colon-separated rpath list in
# VARIABLE-NAME (a macro).
#
macro(rpath_append VAR)
  foreach(VALUE ${ARGN})
    if("${${VAR}}" STREQUAL "")
      set(${VAR} "${VALUE}")
    else("${${VAR}}" STREQUAL "")
      set(${VAR} "${CMAKE_INSTALL_RPATH}:${VALUE}")
    endif("${${VAR}}" STREQUAL "")
  endforeach(VALUE ${ARGN})
endmacro()

#
# check_external <NAME> <LIBS> <LINKER-FLAGS> <OUTPUT-VARIABLE>
#
# Check that a program using the external function NAME compiles and links
# against LIBS, storing the result in OUTPUT-VARIABLE.
#
function(check_external NAME LIBS LDFLAGS OUTPUT_VAR)
  try_code("test-${NAME}.c" "\n  extern int ${NAME}(void);\n  int main() {\n    ${NAME}();\n    return 0;\n  }\n  "
           "${OUTPUT_VAR}" OUT "${LIBS}" "${LDFLAGS}")
endfunction()

##
## try_code <FILENAME> <CODE> <RESULT-VARIABLE> <OUTPUT-VARIABLE> <LIBS> <LINKER-FLAGS>
##
function(try_code FILE CODE RESULT_VAR OUTPUT_VAR LIBS LDFLAGS)
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

#
# run_code <FILENAME> <CODE> <RESULT-VARIABLE> <OUTPUT-VARIABLE> <LIBS> <LINKER-FLAGS>
#
function(run_code FILE CODE RESULT_VAR OUTPUT_VAR LIBS LDFLAGS)
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

#
# libname <OUTPUT-VARIABLE> <FILENAME>
#
# Store FILENAME without its directory, 'lib' prefix and extension (e.g.
# /usr/lib/libz.so gives z) in OUTPUT-VARIABLE.
#
function(libname OUT_VAR FILENAME)
  string(REGEX REPLACE ".*/(lib|)" "" LIBNAME "${FILENAME}")
  string(REGEX REPLACE "\.[^/.]+$" "" LIBNAME "${LIBNAME}")
  set(${OUT_VAR} "${LIBNAME}" PARENT_SCOPE)
endfunction()


#
# append_vars <STR> <VARS...>
#
# Append STR to each of the space-separated variables VARS unless it is already
# in them (a macro).
#
macro(append_vars STR)
  foreach(L ${ARGN})
    set(LIST "${${L}}")

    if(NOT LIST MATCHES "(^${STR}$|^${STR} | ${STR}$| ${STR} )")
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
# check_flag <FLAG> <VAR> [FLAG-VARS...]
#
# If the C compiler accepts FLAG, cache the result in VAR (derived from FLAG if
# empty) and append FLAG to each of FLAG-VARS.
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

#
# check_flags <FLAGS>
#
# Run check_flag for every flag in the list FLAGS, appending the supported
# ones to the variables in ARGN (a macro).
#
macro(check_flags FLAGS)
  foreach(FLAG ${FLAGS})
    check_flag(${FLAG} "" ${ARGN})
  endforeach()
endmacro()

#
# nowarn_flag <FLAG>
#
# Add the warning-suppression flag FLAG to the C and C++ flags if the compiler
# supports it, silently (a macro).
#
macro(nowarn_flag FLAG)
  canonicalize(VARNAME "${FLAG}")
  set(CMAKE_REQUIRED_QUIET ON)
  check_c_compiler_flag("${FLAG}" "${VARNAME}")
  set(CMAKE_REQUIRED_QUIET OFF)

  if(${VARNAME})
    append_vars("${FLAG}" CMAKE_C_FLAGS CMAKE_CXX_FLAGS)
  endif()
endmacro()

#
# add_nowarn_flags
#
# Remove -Wall from the C and C++ flags and add the -Wno-* flags this project
# needs, for the compilers that support them (a macro).
#
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
