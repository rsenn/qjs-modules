project(quickjs-modules C)

set(QUICKJS_MODULES mmap inspect tree-walker xml)

set(tree_walker_SOURCES vector.c vector.h)
set(xml_SOURCES vector.c vector.h)

if(NOT QUICKJS_PREFIX)
  set(QUICKJS_PREFIX "${CMAKE_INSTALL_PREFIX}"
      CACHE PATH "QuickJS install directory")
endif(NOT QUICKJS_PREFIX)

include(CheckCCompilerFlag)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  check_c_compiler_flag("-O0" O_OPT_NONE)
  if(O_OPT_NONE)
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0")
  endif(O_OPT_NONE)
  check_c_compiler_flag("-ggdb" G_OPT_GDB)
  if(G_OPT_GDB)
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -ggdb")
  endif(G_OPT_GDB)
endif(CMAKE_BUILD_TYPE STREQUAL "Debug")

include(CheckIncludeFile)

set(CMAKE_REQUIRED_INCLUDES "${QUICKJS_PREFIX}/include/quickjs")
message("CMAKE_REQUIRED_INCLUDES = ${CMAKE_REQUIRED_INCLUDES}")
check_include_file(quickjs.h HAVE_QUICKJS_H)

include(CheckCSourceRuns)

check_c_source_runs(
    "#include <stdbool.h>\nbool foo(int a, int b, int *c) {\n   return __builtin_mul_overflow(a, b, c);\n}\nint main() {\n   int out;\n   if (foo(1, 2, &out)) {\n       return 0;\n   }\n   return 0;\n}"

    HAVE__BUILTIN_MUL_OVERFLOW)
if(HAVE__BUILTIN_MUL_OVERFLOW)
    add_definitions(-DHAVE__BUILTIN_MUL_OVERFLOW)
endif(HAVE__BUILTIN_MUL_OVERFLOW)

if(NOT HAVE_QUICKJS_H)
  message(FATAL_ERROR "QuickJS headers not found")
endif(NOT HAVE_QUICKJS_H)

foreach(NAME ${QUICKJS_MODULES})
  string(REGEX REPLACE "[-/]" "_" PFX "${NAME}")
  add_library(${NAME} MODULE ${NAME}.c ${${PFX}_SOURCES} )
  set_target_properties(${NAME} PROPERTIES
    PREFIX ""
    INCLUDE_DIRECTORIES "${QUICKJS_PREFIX}/include/quickjs"
  )
  target_compile_definitions(${NAME} PRIVATE -DJS_SHARED_LIBRARY)
  install(TARGETS ${NAME} DESTINATION lib/quickjs PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
endforeach(NAME mmap inspect)

