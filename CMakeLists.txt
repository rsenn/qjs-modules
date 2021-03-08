project(quickjs-modules C)

set(QUICKJS_MODULES deep lexer mmap path pointer predicate inspect tree-walker xml)

set(deep_SOURCES vector.c vector.h pointer.c pointer.h virtual-properties.c virtual-properties.h)
set(deep_LIBRARIES quickjs-pointer)
set(lexer_LIBRARIES quickjs-predicate)
set(inspect_SOURCES vector.c vector.h)
set(tree_walker_SOURCES vector.c vector.h)
set(xml_SOURCES vector.c vector.h)
set(path_SOURCES path.c path.h)
set(pointer_SOURCES quickjs-pointer.h pointer.c pointer.h)
set(lexer_SOURCES utils.h predicate.c predicate.h ${CMAKE_CURRENT_SOURCE_DIR}/../quickjs.h   ${CMAKE_CURRENT_SOURCE_DIR}/../cutils.h)

if(NOT QUICKJS_PREFIX)
  set(QUICKJS_PREFIX "${CMAKE_INSTALL_PREFIX}"
      CACHE PATH "QuickJS install directory")
else(NOT QUICKJS_PREFIX)
  set(QUICKJS_PREFIX "${QUICKJS_PREFIX}" CACHE PATH "QuickJS install directory")
endif(NOT QUICKJS_PREFIX)

set(CMAKE_INSTALL_PREFIX "${QUICKJS_PREFIX}" CACHE PATH "Install directory"
                                                   FORCE)

message("QuickJS install directory: ${QUICKJS_PREFIX}")

set(CMAKE_REQUIRED_QUIET TRUE CACHE BOOLEAN "Quiet" FORCE)

include(CheckCCompilerFlag)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  check_c_compiler_flag("-O0" O_OPT_NONE)
  if(O_OPT_NONE)
    if(NOT "${CMAKE_C_FLAGS_DEBUG}" MATCHES "-O0")
      set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0"
          CACHE STRING "C compiler options" FORCE)
    endif(NOT "${CMAKE_C_FLAGS_DEBUG}" MATCHES "-O0")
  endif(O_OPT_NONE)
  check_c_compiler_flag("-ggdb" G_OPT_GDB)
  if(G_OPT_GDB)
    if(NOT "${CMAKE_C_FLAGS_DEBUG}" MATCHES "-ggdb")
      set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -ggdb"
          CACHE STRING "C compiler options" FORCE)
    endif(NOT "${CMAKE_C_FLAGS_DEBUG}" MATCHES "-ggdb")
  endif(G_OPT_GDB)
endif(CMAKE_BUILD_TYPE STREQUAL "Debug")

include(CheckIncludeFile)

set(QUICKJS_INCLUDE_DIRS "")

if(EXISTS "${CMAKE_CURRENT_BINARY_DIR}/../quickjs-config.h")
  list(APPEND QUICKJS_INCLUDE_DIRS ..)
  list(APPEND QUICKJS_INCLUDE_DIRS "${CMAKE_CURRENT_BINARY_DIR}/..")
endif(EXISTS "${CMAKE_CURRENT_BINARY_DIR}/../quickjs-config.h")

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../quickjs.h")
  file(RELATIVE_PATH QUICKJS_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/..")
  list(APPEND QUICKJS_INCLUDE_DIRS "${QUICKJS_INCLUDE_DIR}")
  list(APPEND QUICKJS_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/..")
endif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../quickjs.h")

if(EXISTS "${QUICKJS_PREFIX}/include/quickjs")
  list(APPEND QUICKJS_INCLUDE_DIRS "${QUICKJS_PREFIX}/include/quickjs")
endif(EXISTS "${QUICKJS_PREFIX}/include/quickjs")

set(QUICKJS_INCLUDE_DIRS "${QUICKJS_INCLUDE_DIRS}" CACHE STRING "QuickJS include dirs" FORCE)

set(CMAKE_REQUIRED_INCLUDES "${QUICKJS_INCLUDE_DIRS}")

# message("CMAKE_REQUIRED_INCLUDES = ${CMAKE_REQUIRED_INCLUDES}")
check_include_file(quickjs.h HAVE_QUICKJS_H)
check_include_file(quickjs-config.h HAVE_QUICKJS_CONFIG_H)

if(NOT HAVE_QUICKJS_H)
  message(FATAL_ERROR "QuickJS headers not found")
endif(NOT HAVE_QUICKJS_H)

include_directories(${QUICKJS_INCLUDE_DIRS})

if(HAVE_QUICKJS_CONFIG_H)
  add_definitions(-DHAVE_QUICKJS_CONFIG_H=1)
endif(HAVE_QUICKJS_CONFIG_H)

include(CheckCSourceRuns)

check_c_source_runs(
  "#include <stdbool.h>\nbool foo(int a, int b, int *c) {\n   return __builtin_mul_overflow(a, b, c);\n}\nint main() {\n   int out;\n   if (foo(1, 2, &out)) {\n       return 0;\n   }\n   return 0;\n}"
  HAVE__BUILTIN_MUL_OVERFLOW)
if(HAVE__BUILTIN_MUL_OVERFLOW)
  add_definitions(-DHAVE__BUILTIN_MUL_OVERFLOW)
endif(HAVE__BUILTIN_MUL_OVERFLOW)

foreach(NAME ${QUICKJS_MODULES})
  string(REGEX REPLACE "[-/]" "_" PFX "${NAME}")
  add_library(quickjs-${NAME} SHARED quickjs-${NAME}.c ${${PFX}_SOURCES})
  set_target_properties(
    quickjs-${NAME} PROPERTIES
    PREFIX ""
    INCLUDE_DIRECTORIES "${QUICKJS_PREFIX}/include/quickjs"
    OUTPUT_NAME "${NAME}"
  )
  if(${${PFX}_LIBRARIES})
    target_link_libraries(quickjs-${NAME} PUBLIC ${${PFX}_LIBRARIES})
  endif(${${PFX}_LIBRARIES})

  target_compile_definitions(quickjs-${NAME} PRIVATE -DJS_SHARED_LIBRARY)
  target_include_directories(quickjs-${NAME} BEFORE PUBLIC ${QUICKJS_INCLUDE_DIRS})
  install(TARGETS quickjs-${NAME} DESTINATION lib/quickjs
          PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ
                      GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
endforeach(NAME mmap inspect)
