project(quickjs-modules C)

set(QUICKJS_MODULES deep lexer mmap path pointer predicate repeater inspect tree-walker
                    xml)

set(CUTILS_H ${CMAKE_CURRENT_SOURCE_DIR}/../cutils.h)
set(QUICKJS_H ${CMAKE_CURRENT_SOURCE_DIR}/../quickjs.h)

if(EXISTS "${QUICKJS_H}")

  set(COMMON_HEADERS ${CUTILS_H} ${QUICKJS_H})
endif()

set(deep_SOURCES vector.c pointer.c virtual-properties.c property-enumeration.h vector.h utils.h pointer.h virtual-properties.h)
set(deep_LIBRARIES quickjs-pointer)
set(inspect_SOURCES vector.c iteration.h utils.h property-enumeration.h vector.h quickjs-internal.h)
set(tree_walker_SOURCES vector.c property-enumeration.h vector.h utils.h)
set(xml_SOURCES vector.c property-enumeration.h vector.h utils.h)
set(path_SOURCES path.c path.h utils.h)
set(predicate_SOURCES predicate.c predicate.h vector.h utils.h)
set(pointer_SOURCES pointer.c pointer.h utils.h)
set(lexer_SOURCES utils.h quickjs-predicate.h predicate.h vector.h)
set(lexer_LIBRARIES quickjs-predicate)

set(repeater_SOURCES utils.h)
 
if(NOT QUICKJS_PREFIX)
  set(QUICKJS_PREFIX "${CMAKE_INSTALL_PREFIX}"
      CACHE PATH "QuickJS install directory")
else(NOT QUICKJS_PREFIX)
  set(QUICKJS_PREFIX "${QUICKJS_PREFIX}" CACHE PATH "QuickJS install directory")
endif(NOT QUICKJS_PREFIX)

set(CMAKE_INSTALL_PREFIX "${QUICKJS_PREFIX}" CACHE PATH "Install directory"
                                                   FORCE)
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release MinSizeRel RelWithDebInfo)

message("QuickJS install directory: ${QUICKJS_PREFIX}")

set(CMAKE_REQUIRED_QUIET TRUE CACHE BOOLEAN "Quiet" FORCE)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/functions.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/check-flags.cmake)

check_flag("-fvisibility=hidden" VISIBILITY_HIDDEN MODULE_COMPILE_FLAGS)
dump(VISIBILITY_HIDDEN)
dump(MODULE_COMPILE_FLAGS)

check_flags("-Wall;-Wno-unused-parameter;-Wno-unused-variable;-Wno-unused-but-set-variable;-Wno-unused-function;-Wno-cast-function-type" CMAKE_C_FLAGS)
#message("CMAKE_C_FLAGS: ${CMAKE_C_FLAGS}")

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
  check_c_compiler_flag("-Wextra" G_WARN_EXTRA)
  if(G_WARN_EXTRA)
    if(NOT "${CMAKE_C_FLAGS_DEBUG}" MATCHES "-Wextra")
      set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -Wextra"
          CACHE STRING "C compiler options" FORCE)
    endif(NOT "${CMAKE_C_FLAGS_DEBUG}" MATCHES "-Wextra")
  endif(G_WARN_EXTRA)
endif(CMAKE_BUILD_TYPE STREQUAL "Debug")

include(CheckIncludeFile)

set(QUICKJS_INCLUDE_DIRS "")

if(EXISTS "${CMAKE_CURRENT_BINARY_DIR}/../quickjs-config.h")
  list(APPEND QUICKJS_INCLUDE_DIRS ..)
  list(APPEND QUICKJS_INCLUDE_DIRS "${CMAKE_CURRENT_BINARY_DIR}/..")
endif(EXISTS "${CMAKE_CURRENT_BINARY_DIR}/../quickjs-config.h")

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../quickjs.h")
  file(RELATIVE_PATH QUICKJS_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}"
       "${CMAKE_CURRENT_SOURCE_DIR}/..")
  list(APPEND QUICKJS_INCLUDE_DIRS "${QUICKJS_INCLUDE_DIR}")
  list(APPEND QUICKJS_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/..")
endif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../quickjs.h")

if(EXISTS "${QUICKJS_PREFIX}/include/quickjs")
  list(APPEND QUICKJS_INCLUDE_DIRS "${QUICKJS_PREFIX}/include/quickjs")
endif(EXISTS "${QUICKJS_PREFIX}/include/quickjs")

set(QUICKJS_INCLUDE_DIRS "${QUICKJS_INCLUDE_DIRS}"
    CACHE STRING "QuickJS include dirs" FORCE)

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
  set(SOURCES quickjs-${NAME}.c)
  if(EXISTS quickjs-${NAME}.h)
    list(APPEND SOURCES quickjs-${NAME}.h)
  endif(EXISTS quickjs-${NAME}.h)
  list(APPEND SOURCES ${${PFX}_SOURCES})
  add_library(quickjs-${NAME} SHARED ${SOURCES})
  set_target_properties(
    quickjs-${NAME}
    PROPERTIES PREFIX "" INCLUDE_DIRECTORIES "${QUICKJS_PREFIX}/include/quickjs"
               OUTPUT_NAME "${NAME}"
               COMPILE_FLAGS "${MODULE_COMPILE_FLAGS}")
  if(${${PFX}_LIBRARIES})
    target_link_libraries(quickjs-${NAME} PUBLIC ${${PFX}_LIBRARIES})
  endif(${${PFX}_LIBRARIES})

  target_compile_definitions(quickjs-${NAME} PRIVATE -DJS_SHARED_LIBRARY)
  target_include_directories(quickjs-${NAME} BEFORE
                             PUBLIC ${QUICKJS_INCLUDE_DIRS})
  install(TARGETS quickjs-${NAME} DESTINATION lib/quickjs
          PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ
                      GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
endforeach(NAME mmap inspect)
