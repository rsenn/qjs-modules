project(quickjs-modules C)

set(QUICKJS_MODULES deep mmap path pointer inspect tree-walker xml)

set(deep_SOURCES vector.c vector.h quickjs-pointer.h)
set(inspect_SOURCES vector.c vector.h)
set(tree_walker_SOURCES vector.c vector.h)
set(xml_SOURCES vector.c vector.h)
set(path_SOURCES path.c path.h)
set(pointer_SOURCES quickjs-pointer.h)

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

set(QUICKJS_INCLUDE_DIR "${QUICKJS_PREFIX}/include/quickjs")
set(CMAKE_REQUIRED_INCLUDES "${QUICKJS_INCLUDE_DIR}")

# message("CMAKE_REQUIRED_INCLUDES = ${CMAKE_REQUIRED_INCLUDES}")
check_include_file(quickjs.h HAVE_QUICKJS_H)
check_include_file(quickjs-config.h HAVE_QUICKJS_CONFIG_H)

if(NOT HAVE_QUICKJS_H)
  message(FATAL_ERROR "QuickJS headers not found")
endif(NOT HAVE_QUICKJS_H)

include_directories(${QUICKJS_INCLUDE_DIR})

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
  add_library(${NAME} SHARED quickjs-${NAME}.c ${${PFX}_SOURCES})
  set_target_properties(
    ${NAME} PROPERTIES PREFIX "" INCLUDE_DIRECTORIES
                                 "${QUICKJS_PREFIX}/include/quickjs")
  target_compile_definitions(${NAME} PRIVATE -DJS_SHARED_LIBRARY)
  install(TARGETS ${NAME} DESTINATION lib/quickjs
          PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ
                      GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
endforeach(NAME mmap inspect)

target_link_libraries(deep pointer)
