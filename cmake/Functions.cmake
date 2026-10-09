include(CheckLibraryExists)
include(CheckCCompilerFlag)
include(CheckCSourceCompiles)

#
# add_cflags <ADD> [OUTPUT_VAR]
#
# Append the flag ADD to OUTPUT_VAR (CMAKE_C_FLAGS by default) unless it is
# already there.
#
function(add_cflags ADD)
  if(${ARGC} LESS 2)
    set(OUTPUT_VAR CMAKE_C_FLAGS)
  else()
    set(OUTPUT_VAR "${ARGV1}")
  endif()

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
# Store the unique ITEMS, sorted, in OUTPUT-VAR.
#
function(set_init OUTPUT_VAR)
  set(RESULT "")
  set_add(RESULT ${ARGN})
  list(SORT RESULT)
  if(OUTPUT_VAR)
    set("${OUTPUT_VAR}" "${RESULT}" PARENT_SCOPE)
  endif()
endfunction()

#
# set_add <OUTPUT-VAR> [ITEMS...]
#
# Append the ITEMS not already present to the set in OUTPUT-VAR.
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
# set_symmetric_difference <NOT-IN-A> <NOT-IN-B> <SET-A> <SET_B>
#
# Store the items only in SET-B in NOT-IN-A and the items only in SET-A in
# NOT-IN-B.
#
function(set_symmetric_difference NOT_IN_A NOT_IN_B SET_A SET_B)
  set(B "")
  foreach(ITEM ${${SET_A}})
    if(NOT ITEM IN_LIST "${SET_B}")
      list(APPEND B "${ITEM}")
    endif()
  endforeach()

  set(A "")
    foreach(ITEM ${${SET_B}})
    if(NOT ITEM IN_LIST "${SET_A}")
      list(APPEND A "${ITEM}")
    endif()
  endforeach()

  set("${NOT_IN_A}" "${A}" PARENT_SCOPE)
  set("${NOT_IN_B}" "${B}" PARENT_SCOPE)
endfunction()

#
# set_difference <OUTPUT-VAR> <SET-A> <SET_B>
#
# Store the items of SET-A that are not in SET-B in OUTPUT-VAR.
#
function(set_difference OUTPUT_VAR SET_A SET_B)
  set(OUT "")
  foreach(ITEM ${${SET_A}})
    if(NOT ITEM IN_LIST "${SET_B}")
      list(APPEND OUT "${ITEM}")
    endif()
  endforeach()

  set("${OUTPUT_VAR}" "${OUT}" PARENT_SCOPE)
endfunction()

#
# set_intersection <OUTPUT-VAR> <SET-A> <SET_B>
#
# Store the items of SET-A that are also in SET-B in OUTPUT-VAR.
#
function(set_intersection OUTPUT_VAR SET_A SET_B)
  set(OUT "")
  foreach(ITEM ${${SET_A}})
    if(ITEM IN_LIST "${SET_B}")
      list(APPEND OUT "${ITEM}")
    endif()
  endforeach()

  set("${OUTPUT_VAR}" "${OUT}" PARENT_SCOPE)
endfunction()

#
# set_union <OUTPUT-VAR> <SET-A> <SET_B>
#
# Store the items that are in SET-A or in SET-B - without duplicates -
# in OUTPUT-VAR.
#
function(set_union OUTPUT_VAR SET_A SET_B)
  set(OUT "")
  foreach(ITEM ${${SET_A}} ${${SET_B}})
    if(NOT ITEM IN_LIST OUT)
      list(APPEND OUT "${ITEM}")
    endif()
  endforeach()

  set("${OUTPUT_VAR}" "${OUT}" PARENT_SCOPE)
endfunction()

#
# set_retain <SET-VAR> [ARGS...]
#
# Reduce SET-VAR to those ARGS that are already in it.
#
function(set_retain SET_VAR)
  set(OUT "")
  foreach(ITEM ${ARGN})
    if(ITEM IN_LIST "${SET_VAR}")
      list(APPEND OUT "${ITEM}")
    endif()
  endforeach()

  set("${SET_VAR}" "${OUT}" PARENT_SCOPE)
endfunction()

#
# set_missing <OUTPUT-VAR> <SET-VAR> [ARGS...]
#
# Store the ARGS that are not in SET-VAR in OUTPUT-VAR.
#
function(set_missing OUTPUT_VAR SET_VAR)
  set(OUT "")
  foreach(ITEM ${ARGN})
    if(NOT ITEM IN_LIST "${SET_VAR}")
      list(APPEND OUT "${ITEM}")
    endif()
  endforeach()

  set("${OUTPUT_VAR}" "${OUT}" PARENT_SCOPE)
endfunction()

#
# set_extra <OUTPUT-VAR> <SET-VAR> [ARGS...]
#
# Store the items of SET-VAR that are not in ARGS in OUTPUT-VAR.
#
function(set_extra OUTPUT_VAR SET_VAR)
  set(OUT "")
  foreach(ITEM ${${SET_VAR}})
    if(NOT ITEM IN_LIST ARGN)
      list(APPEND OUT "${ITEM}")
    endif()
  endforeach()

  set("${OUTPUT_VAR}" "${OUT}" PARENT_SCOPE)
endfunction()

#
# set_remove <SET-VAR> [ARGS...]
#
# Remove the ARGS from SET-VAR.
#
function(set_remove SET_VAR)
  set(OUT "${${SET_VAR}}")
  list(REMOVE_ITEM OUT ${ARGN})
  set("${SET_VAR}" "${OUT}" PARENT_SCOPE)
endfunction()

#
# set_transform <SET-VAR> <FUNC> [FUNC-ARGS...]
#
# Call FUNC([FUNC-ARGS...] ITEM <item>) for every item of SET-VAR.
#
function(set_transform SET_VAR FUNC)
  set(OUT "")
  set(ARGS "ITEM \"\${ITEM}\"")
  if(ARGN)
    list(JOIN ARGN " " PREPEND)
    set(ARGS "${PREPEND} ${ARGS}")
  endif()

  foreach(ITEM ${${SET_VAR}})
    cmake_language(EVAL CODE "${FUNC}(${ARGS})")
    list(APPEND OUT "${ITEM}")
  endforeach()

  set("${SET_VAR}" "${OUT}" PARENT_SCOPE)
endfunction()

#
# escape_string <OUTPUT-VAR> <STR>
#
# Store STR in OUTPUT-VAR with backslash and control characters (newline, CR,
# tab, ESC) written as escape sequences.
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
# Store STR in OUTPUT-VAR with backslash escape sequences (\n, \r, \t, \e,
# \x1b, \033) turned back into characters.
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
# Assign the elements of the list ITEMS, in order, to the variables VAR0, ...
# (a macro).
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
# Like assign_items, but LIST is the name of a list variable.
#
macro(assign_list LIST)
  assign_items("${${LIST}}" ${ARGN})
endmacro()

#
# eat_line <BUFFER-VAR> <RESULT-VAR>
#
# Move the first line of BUFFER-VAR into RESULT-VAR and leave the rest in
# BUFFER-VAR.
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
# Store ARGS, each with PREFIX prepended, in OUTPUT-VAR.
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
# Store ARGS, each with SUFFIX appended, in OUTPUT-VAR.
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
# Treat ARGS as alternating NAME/VALUE lines and set the cache variable
# PREFIX<NAME> to VALUE for each pair.
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
# Treat ARGS as alternating NAME VALUE pairs and set each variable NAME to
# VALUE in the caller's scope.
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
# Apply assign_named_items to the pairs held in the list variable VAR_NAME.
#
macro(assign_named_var VAR_NAME)
  assign_named_items(${${VAR_NAME}})
endmacro()

#
# isin_var <OUTPUT-VAR> <ITEM> <VAR-NAME>
#
# Store TRUE in OUTPUT-VAR if ITEM is in the list variable VAR-NAME,
# otherwise FALSE.
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
# Store TRUE in OUTPUT-VAR if ITEM is one of the LIST elements, otherwise
# FALSE.
#
function(isin_list OUTPUT_VAR ITEM)
  set(LIST "${ARGN}")
  isin_var("${OUTPUT_VAR}" "${ITEM}" LIST)
endfunction()

#
# absolute_paths <OUTPUT-VAR> [PATHS...]
#
# Store PATHS, made absolute relative to the current directory, in OUTPUT-VAR.
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
# Store PATHS, made absolute relative to BASE-DIRECTORY, in OUTPUT-VAR.
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
# Store PATHS, made relative to BASE-DIRECTORY, in OUTPUT-VAR.
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
# Store the absolute path of the current directory in OUTPUT-VAR.
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
# Check whether PATH is a relative path.
#
function(is_relative PATH OUTPUT_VAR)
  if(OUTPUT_VAR)
    cmake_path(IS_RELATIVE PATH "${OUTPUT_VAR}")
  endif()
endfunction()

#
# is_absolute <PATH> <OUTPUT-VAR>
#
# Check whether PATH is an absolute path.
#
function(is_absolute PATH OUTPUT_VAR)
  if(OUTPUT_VAR)
    cmake_path(IS_ABSOLUTE PATH "${OUTPUT_VAR}")
  endif()
endfunction()

#
# concat <OUTPUT-VAR> <SEPARATOR> [ARGUMENTS...]
#
# Join ARGUMENTS with SEPARATOR between them and store the result in
# OUTPUT-VAR.
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
# Store STR without its leading directories, and without the extension
# EXT_NAME if given, in OUTPUT-VAR.
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
# Add the compile definition -DNAME: =1 or =0 by the truth of variable NAME,
# or =DEFINED_VALUE if VAR_NAME is true.
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
# Store the width of the terminal in OUTPUT-VAR, from $COLUMNS or tput, or
# DEFAULT (80) if unknown.
#
function(get_columns RESULT_VAR)
  if(${ARGC} GREATER_EQUAL 2)
    set(DEFAULT_VALUE ${ARGV1})
  else()
    set(DEFAULT_VALUE 80)
  endif()

  set(RESULT "$ENV{COLUMNS}")
  if(RESULT LESS 1)
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
# Print MSG followed by the name and value of each of VAR-NAMES, laid out as
# set by DUMP_FORMAT.
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
# Print the name and value of each of VAR-NAMES.
#
function(dump)
  named_dump("Variable dump" ${ARGN})
endfunction()

#
# dump_list <VAR-NAME>
#
# Print every element of the list variable VAR-NAME on its own numbered line.
#
function(dump_list)
  assign_named_var(DUMP_FORMAT)
  foreach(VAR_NAME ${ARGN})
    message("List dump of ${VAR_NAME}:")
    list(LENGTH "${VAR_NAME}" NUM_ITEMS)
    set(INDEX 0)
    while(${INDEX} LESS ${NUM_ITEMS})
      list(GET "${VAR_NAME}" "${INDEX}" ITEM)
      message("${INDENT}${INDEX}: ${ITEM}")
      math(EXPR INDEX "${INDEX} + 1")
    endwhile()
  endforeach()
endfunction()

#
# make_list <OUTPUT-VAR> <MAX_LINE_LEN>
#
# Store the words in the arguments in OUTPUT-VAR as lines no longer than
# MAX_LINE_LEN, laid out as set by LIST_FORMAT.
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
# make_columns <OUTPUT-VAR> <MAX_LINE_LEN> [ITEMS...]
#
# Store the ITEMS in OUTPUT-VAR as lines of aligned columns, filled column by
# column like ls(1), using as many columns as fit in MAX_LINE_LEN; the layout
# is set by COLUMN_FORMAT.
#
function(make_columns OUTPUT_VAR MAX_LINE_LEN)
  assign_named_var(COLUMN_FORMAT)
  set(ITEMS ${ARGN})
  list(LENGTH ITEMS COUNT)
  set(RESULT "")

  if(COUNT GREATER 0)
    string(LENGTH "${INDENT}" INDENT_LEN)
    string(LENGTH "${GAP}" GAP_LEN)

    set(LENGTHS "")
    foreach(ITEM ${ITEMS})
      string(LENGTH "${ITEM}" LEN)
      list(APPEND LENGTHS ${LEN})
    endforeach()

    # try the most columns first, until the widest layout that fits is found
    set(NCOLS ${COUNT})
    while(TRUE)
      math(EXPR ROWS "(${COUNT} + ${NCOLS} - 1) / ${NCOLS}")
      math(EXPR USED "(${COUNT} + ${ROWS} - 1) / ${ROWS}")

      set(WIDTHS "")
      set(TOTAL ${INDENT_LEN})
      foreach(COL RANGE 0 ${USED})
        if(COL EQUAL USED)
          break()
        endif()
        math(EXPR FIRST "${COL} * ${ROWS}")
        math(EXPR LAST "${FIRST} + ${ROWS} - 1")
        if(LAST GREATER_EQUAL COUNT)
          math(EXPR LAST "${COUNT} - 1")
        endif()
        set(WIDTH 0)
        foreach(I RANGE ${FIRST} ${LAST})
          list(GET LENGTHS ${I} LEN)
          if(LEN GREATER WIDTH)
            set(WIDTH ${LEN})
          endif()
        endforeach()
        list(APPEND WIDTHS ${WIDTH})
        math(EXPR TOTAL "${TOTAL} + ${WIDTH}")
        if(COL GREATER 0)
          math(EXPR TOTAL "${TOTAL} + ${GAP_LEN}")
        endif()
      endforeach()

      if(TOTAL LESS_EQUAL MAX_LINE_LEN OR NCOLS EQUAL 1)
        break()
      endif()
      math(EXPR NCOLS "${NCOLS} - 1")
    endwhile()

    foreach(ROW RANGE 0 ${ROWS})
      if(ROW EQUAL ROWS)
        break()
      endif()
      set(LINE "")
      foreach(COL RANGE 0 ${USED})
        if(COL EQUAL USED)
          break()
        endif()
        math(EXPR I "${COL} * ${ROWS} + ${ROW}")
        if(I LESS COUNT)
          list(GET ITEMS ${I} ITEM)
          math(EXPR NEXT "(${COL} + 1) * ${ROWS} + ${ROW}")
          if(NEXT LESS COUNT)
            list(GET WIDTHS ${COL} WIDTH)
            list(GET LENGTHS ${I} LEN)
            math(EXPR PAD "${WIDTH} - ${LEN}")
            string(REPEAT " " ${PAD} SPACES)
            set(LINE "${LINE}${ITEM}${SPACES}${GAP}")
          else()
            set(LINE "${LINE}${ITEM}")
          endif()
        endif()
      endforeach()
      set(RESULT "${RESULT}\n${INDENT}${LINE}")
    endforeach()
  endif()

  if(OUTPUT_VAR)
    set("${OUTPUT_VAR}" "${RESULT}" PARENT_SCOPE)
  endif()
endfunction()

set(COLUMN_FORMAT INDENT "  " GAP "  ")
  
#
# print_list <DESC> <VAR-NAME> [COLUMNS]
#
# Print DESC, the number of elements and the list variable VAR-NAME, wrapped
# to COLUMNS (or the terminal width).
#
function(print_list DESC VAR_NAME)
  list(LENGTH "${VAR_NAME}" COUNT)
  if(COUNT LESS_EQUAL 0)
    message(STATUS "${DESC} (0): empty")
  else()
    if(ARGC GREATER_EQUAL 3)
      set(COLUMNS ${ARGV2})
    elseif(NOT DEFINED MAX_COLUMNS)
      get_columns(COLUMNS)
      set(MAX_COLUMNS "${COLUMNS}" PARENT_SCOPE)
    endif()
    make_list(L ${COLUMNS} ${${VAR_NAME}})
    message("${DESC} (${COUNT}): ${L}\n")
  endif()
endfunction()

#
# print_columns <DESC> <VAR-NAME> [COLUMNS]
#
# Print DESC, the number of elements and the list variable VAR-NAME, wrapped
# to COLUMNS (or the terminal width).
#
function(print_columns DESC VAR_NAME)
  list(LENGTH "${VAR_NAME}" COUNT)
  if(COUNT LESS_EQUAL 0)
    message(STATUS "${DESC} (0): empty")
  else()
    if(ARGC GREATER_EQUAL 3)
      set(COLUMNS ${ARGV2})
    elseif(NOT DEFINED MAX_COLUMNS)
      get_columns(COLUMNS)
      set(MAX_COLUMNS "${COLUMNS}" PARENT_SCOPE)
    endif()
    make_columns(L ${COLUMNS} ${${VAR_NAME}})
    message("${DESC} (${COUNT})\n${L}\n")
  endif()
endfunction()

#
# make_filter_re <OUTPUT-VAR> [LIST...]
#
# Store a regular expression in OUTPUT-VAR that matches any LIST element,
# with '-' and '_' interchangeable.
#
function(make_filter_re OUTPUT_VAR)
  set(LIST "")
  list(APPEND LIST ${ARGN})
  list(JOIN LIST "|" RE)
  string(REGEX REPLACE "[-_]" "[-_]" RE "(${RE})")
  set("${OUTPUT_VAR}" "${RE}" PARENT_SCOPE)
endfunction()

#
# message_unescaped [STRINGS...]
#
# Print STRINGS joined together, after turning escape sequences such as \n
# and \e into characters.
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
# Print the colored function name FUNCTION-NAME followed by STRINGS (for
# tracing calls).
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

#
# init_colors
#
# Define the ANSI escape sequence variables COLOR_NONE, COLOR_RED, ... used
# for colored messages (a macro).
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
# Store the value of the environment variable VAR-NAME in OUTPUT-VAR, or
# unset OUTPUT-VAR if it is not set.
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
# Store the value of the environment variable VAR-NAME in OUTPUT-VAR, or
# DEFAULT-VALUE if it is not set.
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