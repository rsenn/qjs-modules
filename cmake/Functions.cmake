include(CheckLibraryExists)
include(CheckCCompilerFlag)
include(CheckCSourceCompiles)

#
# add_cflags <ADD> [OUTPUT_VAR]
#
function(add_cflags ADD)
  if(${ARGC} LESS 2)
    set(OUTPUT_VAR CMAKE_C_FLAGS)
  else()
    set(OUTPUT_VAR "${ARGV1}")
  endif()
  
  #message_func("add_cflags" ${ADD} ${OUTPUT_VAR})

  set(RESULT "${${OUTPUT_VAR}}")
  string(REGEX REPLACE " +" ";" FLAGS "${RESULT}")
  string(REGEX REPLACE "^;+" "" FLAGS "${FLAGS}")
  list(REMOVE_DUPLICATES FLAGS) 
  if(NOT ADD IN_LIST FLAGS)
    list(APPEND RESULT ${ADD})
  endif()

  if(OUTPUT_VAR)
    set("${OUTPUT_VAR}" "${RESULT}" PARENT_SCOPE)
  endif()
endfunction()

#
# set_init <OUTPUT-VAR> [ITEMS...]
#
function(set_init OUTPUT_VAR)
  set(RESULT "")
  set_add(RESULT ${ARGN})
  if(OUTPUT_VAR)
    set("${OUTPUT_VAR}" "${RESULT}" PARENT_SCOPE)
  endif()
endfunction()

#
# set_add <OUTPUT-VAR> [ITEMS...]
#
function(set_add OUTPUT_VAR)
  set(RESULT "${${OUTPUT_VAR}}")
  foreach(ITEM ${ARGN})
    if(NOT ITEM IN_LIST RESULT)
      list(APPEND RESULT "${ITEM}")
    endif()
  endforeach()
  if(OUTPUT_VAR)
    set("${OUTPUT_VAR}" "${RESULT}" PARENT_SCOPE)
  endif()
endfunction()

#
# escape_string <OUTPUT-VAR> <STR>
#
function(escape_string OUTPUT_VAR STR)
  string(REPLACE "\\" "\\\\" RESULT "${STR}")

  string(REPLACE "\n" "\\n" RESULT "${RESULT}")
  string(REPLACE "\r" "\\r" RESULT "${RESULT}")
  string(REPLACE "\t" "\\t" RESULT "${RESULT}")
  string(REPLACE "${ANSI_ESCAPE}" "\\e" RESULT "${RESULT}")
  string(REPLACE "" "\\v" RESULT "${RESULT}")
  string(REPLACE "" "\\f" RESULT "${RESULT}")

  if(OUTPUT_VAR)
    set("${OUTPUT_VAR}" "${RESULT}" PARENT_SCOPE)
  endif(OUTPUT_VAR)
endfunction()

#
# unescape_string <OUTPUT-VAR> <STR>
#
function(unescape_string OUTPUT_VAR STR)
  string(REPLACE "\\n" "\n" RESULT "${STR}")
  string(REPLACE "\\r" "\r" RESULT "${RESULT}")
  string(REPLACE "\\t" "\t" RESULT "${RESULT}")
  string(REPLACE "\\x1b" "${ANSI_ESCAPE}" RESULT "${RESULT}")
  string(REPLACE "\\033" "${ANSI_ESCAPE}" RESULT "${RESULT}")
  string(REPLACE "\\e" "${ANSI_ESCAPE}"  RESULT "${RESULT}")
  string(REPLACE "\\v" ""  RESULT "${RESULT}")
  string(REPLACE "\\f" ""   RESULT "${RESULT}")

  string(REPLACE "\\\\" "\\" RESULT "${RESULT}")

  if(OUTPUT_VAR)
    set("${OUTPUT_VAR}" "${RESULT}" PARENT_SCOPE)
  endif(OUTPUT_VAR)
endfunction()

# ITEMS contains multiple items
#
# assign_items <ITEMS> [VAR0, ...]
#
macro(assign_items ITEMS)
  set(__L "${ITEMS}")
  set(__I 0)
  foreach(__A ${ARGN})
    list(GET __L "${__I}" "${__A}")
    math(EXPR __I "${__I} + 1")
 endforeach()
endmacro()

# LIST is the name of a list
#
# assign_list <LIST> [VAR0...]
#
macro(assign_list LIST)
  assign_items("${${LIST}}" ${ARGN})
endmacro()

#
# eat_line <BUFFER-VAR> <RESULT-VAR>
#
function(eat_line BUFFER_VAR RESULT_VAR)
  set(BUF "${${BUFFER_VAR}}")
  string(FIND "${BUF}" "\n" NL_POS)
  string(LENGTH "${BUF}" LEN)
  if(${NL_POS} EQUAL -1)
    set(NL_POS "${LEN}")
    set(NEXT_POS "${NL_POS}")
  else()
    math(EXPR NEXT_POS "${NL_POS} + 1")
  endif()
  string(SUBSTRING "${BUF}" "0" "${NL_POS}" RESULT)
  string(SUBSTRING "${BUF}"  "${NEXT_POS}"  -1 REST)
  set("${RESULT_VAR}" "${RESULT}" PARENT_SCOPE)
  set("${BUFFER_VAR}" "${REST}" PARENT_SCOPE)
endfunction()

#
# add_prefix <OUTPUT-VAR> <PREFIX> [ARGS...]
#
macro(add_prefix OUTPUT_VAR PREFIX)
  unset("${OUTPUT_VAR}" PARENT_SCOPE)
  foreach(ARG ${ARGN})
    list(APPEND "${OUTPUT_VAR}" "${PREFIX}${ARG}")
  endforeach()
endmacro()

#
# add_suffix <OUTPUT-VAR> <SUFFIX> [ARGS...]
#
macro(add_suffix OUTPUT_VAR SUFFIX)
  unset("${OUTPUT_VAR}" PARENT_SCOPE)
  foreach(ARG ${ARGN})
    list(APPEND "${OUTPUT_VAR}" "${ARG}${SUFFIX}")
  endforeach()
endmacro()

#
# assign_named_prefix <PREFIX> [ARGS...]
#
macro(assign_named_prefix PREFIX)
  set(ARGUMENTS "${ARGN}")

  while(NOT "${ARGUMENTS}" STREQUAL "")
    eat_line(ARGUMENTS NAME)
    eat_line(ARGUMENTS VALUE)

   set("${PREFIX}${NAME}" "${VALUE}" CACHE STRING "Color value")
  endwhile()
endmacro()

#
# assign_named_items [ARGS...]
#
macro(assign_named_items)
 unset(__N)
  foreach(__A ${ARGN})
     if(NOT DEFINED __N)
      set(__N "${__A}")
     else()
      set(__V "${__A}")
      endif()
     if(DEFINED __V)
         set("${__N}" "${__V}")
       unset(__N)
       unset(__V)
     endif()
  endforeach()
endmacro()

#
# assign_named_var <VAR_NAME>
#
macro(assign_named_var VAR_NAME)
  assign_named_items(${${VAR_NAME}})
endmacro()

#
# isin_var <OUTPUT-VAR> <ITEM> <VAR-NAME>
#
function(isin_var OUTPUT_VAR ITEM VAR_NAME)
  if(ITEM IN_LIST "${VAR_NAME}")
    set(RESULT TRUE)
  else()
    set(RESULT FALSE)
  endif()

  if(OUTPUT_VAR)
    set("${OUTPUT_VAR}" "${RESULT}" PARENT_SCOPE)
  endif()
endfunction()

#
# isin_list <OUTPUT-VAR> <ITEM> [LIST...]
#
function(isin_list OUTPUT_VAR ITEM)
  set(LIST "${ARGN}")
  isin_var("${OUTPUT_VAR}" "${ITEM}" LIST)
endfunction()

#
# absolute_paths <OUTPUT-VAR> [PATHS...]
#
function(absolute_paths OUTPUT_VAR)
  set(RESULT "")
  foreach(ARG ${ARGN})
      cmake_path(ABSOLUTE_PATH ARG OUTPUT_VARIABLE VALUE)
      list(APPEND RESULT "${VALUE}")
  endforeach()

  if(OUTPUT_VAR)
    set("${OUTPUT_VAR}" "${RESULT}" PARENT_SCOPE)
  endif()
endfunction()

#
# absolute_paths_in <OUTPUT-VAR> <BASE-DIRECTORY> [PATHS...]
#
function(absolute_paths_in OUTPUT_VAR BASE_DIRECTORY)
  set(RESULT "")
  foreach(ARG ${ARGN})
      cmake_path(ABSOLUTE_PATH ARG BASE_DIRECTORY "${BASE_DIRECTORY}" OUTPUT_VARIABLE VALUE)
      list(APPEND RESULT "${VALUE}")
  endforeach()

  if(OUTPUT_VAR)
    set("${OUTPUT_VAR}" "${RESULT}" PARENT_SCOPE)
  endif()
endfunction()

#
# relative_paths <OUTPUT-VAR> <BASE-DIRECTORY> [PATHS...]
#
function(relative_paths OUTPUT_VAR BASE_DIRECTORY)
  set(RESULT "")
  foreach(ARG ${ARGN})
      cmake_path(RELATIVE_PATH ARG BASE_DIRECTORY "${BASE_DIRECTORY}" OUTPUT_VARIABLE VALUE)
      list(APPEND RESULT "${VALUE}")
  endforeach()

  if(OUTPUT_VAR)
    set("${OUTPUT_VAR}" "${RESULT}" PARENT_SCOPE)
  endif()
endfunction()

#
# get_cwd <OUTPUT-VAR>
#
function(get_cwd OUTPUT_VAR)
  set(ARG ".")
  cmake_path(ABSOLUTE_PATH ARG OUTPUT_VARIABLE RESULT)
  string(REGEX REPLACE "[/\\\\]\\.$" "" RESULT "${RESULT}")

  if(OUTPUT_VAR)
    set("${OUTPUT_VAR}" "${RESULT}" PARENT_SCOPE)
  endif()
endfunction()

#
# is_relative <PATH> <OUTPUT-VAR>
#
function(is_relative PATH OUTPUT_VAR)
  if(OUTPUT_VAR)
    cmake_path(IS_RELATIVE PATH "${OUTPUT_VAR}")
  endif()
endfunction()

#
# is_absolute <PATH> <OUTPUT-VAR>
#
function(is_absolute PATH OUTPUT_VAR)
  if(OUTPUT_VAR)
    cmake_path(IS_ABSOLUTE PATH "${OUTPUT_VAR}")
  endif()
endfunction()

#
# concat <OUTPUT-VAR> <SEPARATOR> [ARGUMENTS...]
#
function(concat OUTPUT_VAR SEPARATOR)
  set(RESULT "")
  foreach(ARG ${ARGN})
    if(NOT "${RESULT}" STREQUAL "")
      set(RESULT "${RESULT}${SEPARATOR}${ARG}")
    else()
      set(RESULT "${ARG}")
    endif()
  endforeach()

  if(OUTPUT_VAR)
    set("${OUTPUT_VAR}" "${RESULT}" PARENT_SCOPE)
  endif()
endfunction()

#
# basename <OUTPUT-VAR> <STR> [EXT_NAME]
#
function(basename OUTPUT_VAR STR)
  string(REGEX REPLACE ".*/" "" RESULT "${STR}")
  if(ARGN)
    string(REGEX REPLACE "\\${ARGN}\$" "" RESULT "${RESULT}")
  endif()

  if(OUTPUT_VAR)
    set("${OUTPUT_VAR}" "${RESULT}" PARENT_SCOPE)
  endif()
endfunction()

#
# var2define <NAME> [DEFINED_VALUE] [VAR_NAME]
#
function(var2define NAME)
  if(${ARGC} GREATER_EQUAL 3)
    set(VAR_NAME "${ARGV1}")
  else()
    set(VAR_NAME "${NAME}")
  endif()

  set(VALUE "${${VAR_NAME}}")
  if(${ARGC} LESS_EQUAL 1 AND ${ARGC} GREATER_EQUAL 0)
    if(VALUE)
      add_definitions(-D${NAME}=1)
    else()
      add_definitions(-D${NAME}=0)
    endif()
  else()
    if(VALUE)
      add_definitions(-D${NAME}=${ARGV1})
    endif()
  endif()
endfunction()

# Get number of columns the terminal supports
#
# get_columns <OUTPUT-VAR> [DEFAULT]
#
function(get_columns RESULT_VAR)
  if(${ARGC} GREATER_EQUAL 2)
    set(DEFAULT_VALUE ${ARGV1})
  else()
    set(DEFAULT_VALUE 80)
  endif()

  set(RESULT "$ENV{COLUMNS}")
  if(RESULT GREATER_EQUAL 1)
    # message("Got COLUMNS (${RESULT}) from environment")
  else()
    execute_process(COMMAND tput cols OUTPUT_VARIABLE TPUT_COLS ERROR_QUIET ERROR_VARIABLE TPUT_ERROR)
    if(NOT TPUT_ERROR AND TPUT_COLS)
      set(RESULT ${TPUT_COLS})
    else()
      set(SOURCE_NAME ttysize.c)
      set(SOURCE_CODE "#include <unistd.h>\n#include <fcntl.h>\n#include <termios.h>\n#include <sys/ioctl.h>\n#include <stdio.h>\n\nint\nmain() {\n\tstruct winsize sz;\n\tint fd = isatty(0) ? dup(0) : open(\"/dev/tty\", O_RDWR);\n\n\tif(!isatty(fd)) {\n\t\tfputs(\"not a tty\\n\", stderr);\n\t\tfflush(stderr);\n\t\treturn 1;\n\t}\n\n\tif(ioctl(fd, TIOCGWINSZ, &sz) == -1) {\n\t\tperror(\"ioctl\");\n\t\treturn 1;\n\t}\n\n\tclose(fd);\n\n\tprintf(\"%u\\n\", sz.ws_col);\n\treturn 0;\n}\n")
      message(CHECK_START "Trying to compile ${SOURCE_NAME}")
      try_run(RUN_RESULT COMPILE_RESULT SOURCE_FROM_CONTENT "${SOURCE_NAME}" "${SOURCE_CODE}" RUN_OUTPUT_STDOUT_VARIABLE RUN_OUTPUT COMPILE_OUTPUT_VARIABLE COMPILE_OUTPUT NO_CACHE)
      if(NOT COMPILE_RESULT)
        message(CHECK_FAIL "failed to compile:\n${COMPILE_OUTPUT}")
      else()
        if(RUN_RESULT STREQUAL 0)
          message(CHECK_PASS "ok")
          set(RESULT "${RUN_OUTPUT}")
        else()
          message(CHECK_FAIL "failed to run:\n${RUN_OUTPUT}")
        endif()
      endif()
    endif()
  endif()
  
  if(NOT RESULT GREATER_EQUAL 1)
    set(RESULT "${DEFAULT_VALUE}")
  endif()

  if(RESULT_VAR)
    set("${RESULT_VAR}" "${RESULT}" PARENT_SCOPE)
  endif()
endfunction()

#
# named_dump <MSG> [VAR-NAMES...]
#
function(named_dump MSG)
  assign_named_var(DUMP_FORMAT)
  set(INDEX 0)
  math(EXPR LAST "${ARGC} - 2")
  set(RESULT "${MSG}${START}")

  foreach(VAR_NAME ${ARGN})
    set(VALUE "${${VAR_NAME}}")
    set(LINE "${INDENT}${VAR_NAME}${PROP}${QUOTE}${VALUE}${QUOTE}")
    if(INDEX LESS LAST)
      set(LINE "${LINE}${COMMA}")
    endif()
    set(RESULT "${RESULT}${NEWLINE}${LINE}")
    math(EXPR INDEX "${INDEX} + 1")
  endforeach()
  if(NOT END STREQUAL "")
    set(RESULT "${RESULT}${NEWLINE}${END}")
  endif()
  
  message("${RESULT}")
endfunction()

set(DUMP_FORMAT INDENT "  " START " {" END "}" PROP ": " QUOTE "'" COMMA "," NEWLINE "\\n")

#
# dump [VAR-NAMES...]
#
function(dump)
  named_dump("Variable dump" ${ARGN})
endfunction()

#
# dump_list <VAR-NAME>
#
function(dump_list VAR_NAME)
  assign_named_var(DUMP_FORMAT)
  message("List dump of ${VAR_NAME}:")
  list(LENGTH "${VAR_NAME}" NUM_ITEMS)
  set(INDEX 0)
  while(${INDEX} LESS ${NUM_ITEMS})
    list(GET "${VAR_NAME}" "${INDEX}" ITEM)
    message("${INDENT}${INDEX}: ${ITEM}")
    math(EXPR INDEX "${INDEX} + 1")
  endwhile()
endfunction()

#
# make_list <OUTPUT-VAR> <MAX_LINE_LEN>
#
function(make_list OUTPUT_VAR MAX_LINE_LEN)
  assign_named_var(LIST_FORMAT)
  set(RESULT "")
  set(LINE "")
  string(REPLACE " " ";" ARGS "${ARGN}")
  foreach(ITEM ${ARGS})
    string(LENGTH "${LINE}${SEP}${ITEM}" LEN)
    math(EXPR EFFECTIVE_LEN "${LEN} + 4")
    if(EFFECTIVE_LEN GREATER MAX_LINE_LEN)
      set(RESULT "${RESULT}\n${INDENT}${LINE}")
      set(LINE " ${ITEM}")
      string(LENGTH "${LINE}" LEN)
    else()
      set(LINE "${LINE}${SEP}${ITEM}")
    endif()
  endforeach()
  if(LINE)
    set(RESULT "${RESULT}\n${INDENT}${LINE}")
  endif()

  if(OUTPUT_VAR)
    set("${OUTPUT_VAR}" "${RESULT}" PARENT_SCOPE)
  endif()
endfunction()
 
set(LIST_FORMAT INDENT "    " SEP  " ")

#
# message_unescaped [STRINGS...]
#
function(message_unescaped)
  set(S "")
  foreach(ARG ${ARGN})
    set(S "${S}${ARG}")
  endforeach()
  unescape_string(S "${S}")
  message("${S}")
endfunction()

#
# message_func <FUNCTION-NAME> [STRINGS...]
#
function(message_func FUNC)
  set(S "${COLOR_LIGHTRED}${FUNC}${COLOR_NONE}")
  foreach(ARG ${ARGN})
    set(S "${S} ${ARG}")
  endforeach()
  unescape_string(S "${S}")
  message("${S}")
endfunction()

#
# init_colors
#
macro(init_colors)
  set(ANSI_ESCAPE "")
  set(SEMI "╎")
  set(COLORS
    NONE "${ANSI_ESCAPE}[0m"
    BLACK "${ANSI_ESCAPE}[0${SEMI}30m"
    RED "${ANSI_ESCAPE}[0${SEMI}31m"
    GREEN "${ANSI_ESCAPE}[0${SEMI}32m"
    BROWN "${ANSI_ESCAPE}[0${SEMI}33m"
    BLUE "${ANSI_ESCAPE}[0${SEMI}34m"
    MAGENTA "${ANSI_ESCAPE}[0${SEMI}35m"
    CYAN "${ANSI_ESCAPE}[0${SEMI}36m"
    LIGHTGRAY "${ANSI_ESCAPE}[0${SEMI}37m"
    DARKGRAY "${ANSI_ESCAPE}[1${SEMI}30m"
    LIGHTRED "${ANSI_ESCAPE}[1${SEMI}31m"
    LIGHTGREEN "${ANSI_ESCAPE}[1${SEMI}32m"
    YELLOW "${ANSI_ESCAPE}[1${SEMI}33m"
    LIGHTBLUE "${ANSI_ESCAPE}[1${SEMI}34m"
    LIGHTMAGENTA "${ANSI_ESCAPE}[1${SEMI}35m"
    LIGHTCYAN "${ANSI_ESCAPE}[1${SEMI}36m"
    WHITE "${ANSI_ESCAPE}[1${SEMI}37m" 
  )
  string(REPLACE ";" "\n" CMAP "${COLORS}")
  string(REPLACE "${SEMI}" ";" CMAP "${CMAP}")

  assign_named_prefix(COLOR_ ${CMAP})
endmacro()

#
# getenv <OUTPUT-VAR> <VAR-NAME>
#
function(getenv OUTPUT_VAR VAR_NAME)
  if(OUTPUT_VAR)
    if(DEFINED ENV{${VAR_NAME}})
      set("${OUTPUT_VAR}" "$ENV{${VAR_NAME}}" PARENT_SCOPE)
    else()
      unset("${OUTPUT_VAR}" PARENT_SCOPE)
    endif()
  endif()
endfunction()

#
# getenv_default <OUTPUT-VAR> <VAR-NAME> [DEFAULT-VALUE]
#
function(getenv_default OUTPUT_VAR VAR_NAME)
  if(DEFINED ENV{${VAR_NAME}})
    set(RESULT "$ENV{${VAR_NAME}}")
  else()
    set(RESULT "${ARGN}")
  endif()
  if(OUTPUT_VAR)
    set("${OUTPUT_VAR}" "${RESULT}" PARENT_SCOPE)
  endif()
endfunction()


init_colors()
