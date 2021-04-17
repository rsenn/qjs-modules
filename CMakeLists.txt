project(quickjs-modules C)

cmake_minimum_required(VERSION 3.2)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/FindQuickJS.cmake)

set(QUICKJS_MODULES
    child-process
    deep
    lexer
    mmap
    path
    pointer
    predicate
    repeater
    inspect
    tree-walker
    xml)

if(EXISTS "${QUICKJS_H}")

  set(COMMON_HEADERS ${CUTILS_H} ${QUICKJS_H})
endif()
set(child_process_SOURCES child-process.c child-process.h utils.c utils.h
                          vector.c vector.h)
set(deep_SOURCES
    vector.c
    vector.h
    pointer.c
    virtual-properties.c
    property-enumeration.h
    utils.c
    utils.h
    pointer.h
    virtual-properties.h)
set(deep_LIBRARIES quickjs-pointer)
set(inspect_SOURCES
    vector.c
    vector.h
    iteration.h
    utils.c
    utils.h
    property-enumeration.h
    quickjs-internal.h)
set(tree_walker_SOURCES vector.c vector.h property-enumeration.h utils.c
                        utils.h)
set(xml_SOURCES vector.c vector.h property-enumeration.h utils.c utils.h)
set(path_SOURCES path.c path.h utils.c utils.h)
set(predicate_SOURCES predicate.c predicate.h vector.c vector.h utils.c utils.h)
set(pointer_SOURCES pointer.c pointer.h utils.c utils.h)
set(lexer_SOURCES utils.c utils.h vector.c vector.h lexer.c lexer.h)
set(lexer_LIBRARIES quickjs-predicate)
set(mmap_SOURCES utils.c utils.h)
set(repeater_SOURCES utils.c utils.h)
 
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release MinSizeRel
                                             RelWithDebInfo)

message("QuickJS install directory: ${QUICKJS_PREFIX}")

set(CMAKE_REQUIRED_QUIET TRUE)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/functions.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/check-flags.cmake)

check_flag("-fvisibility=hidden" VISIBILITY_HIDDEN MODULE_COMPILE_FLAGS)
dump(VISIBILITY_HIDDEN)
dump(MODULE_COMPILE_FLAGS)

check_flags(
  "-Wall;-Wno-unused-parameter;-Wno-unused-variable;-Wno-unused-but-set-variable;-Wno-unused-function;-Wno-cast-function-type"
  CMAKE_C_FLAGS)
# message("CMAKE_C_FLAGS: ${CMAKE_C_FLAGS}")

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  check_c_compiler_flag("-O0" O_OPT_NONE)
  if(O_OPT_NONE)
    if(NOT "${CMAKE_C_FLAGS_DEBUG}" MATCHES "-O0")
      set(CMAKE_C_FLAGS_DEBUG
          "${CMAKE_C_FLAGS_DEBUG} -O0"
          CACHE STRING "C compiler options" FORCE)
    endif(NOT "${CMAKE_C_FLAGS_DEBUG}" MATCHES "-O0")
  endif(O_OPT_NONE)
  check_c_compiler_flag("-ggdb" G_OPT_GDB)
  if(G_OPT_GDB)
    if(NOT "${CMAKE_C_FLAGS_DEBUG}" MATCHES "-ggdb")
      set(CMAKE_C_FLAGS_DEBUG
          "${CMAKE_C_FLAGS_DEBUG} -ggdb"
          CACHE STRING "C compiler options" FORCE)
    endif(NOT "${CMAKE_C_FLAGS_DEBUG}" MATCHES "-ggdb")
  endif(G_OPT_GDB)
  check_c_compiler_flag("-Wextra" G_WARN_EXTRA)
  if(G_WARN_EXTRA)
    if(NOT "${CMAKE_C_FLAGS_DEBUG}" MATCHES "-Wextra")
      set(CMAKE_C_FLAGS_DEBUG
          "${CMAKE_C_FLAGS_DEBUG} -Wextra"
          CACHE STRING "C compiler options" FORCE)
    endif(NOT "${CMAKE_C_FLAGS_DEBUG}" MATCHES "-Wextra")
  endif(G_WARN_EXTRA)
endif(CMAKE_BUILD_TYPE STREQUAL "Debug")

include(CheckFunctionExists)


include(CheckCSourceRuns)

set(CMAKE_REQUIRED_QUIET OFF)
check_include_file(spawn.h HAVE_SPAWN_H)

if(HAVE_SPAWN_H)
  list(APPEND CMAKE_REQUIRED_INCLUDES spawn.h)

  check_function_exists(posix_spawnp HAVE_POSIX_SPAWNP)
  message(STATUS "HAVE_POSIX_SPAWNP: ${HAVE_POSIX_SPAWNP}")

  if(HAVE_POSIX_SPAWNP)
    add_definitions(-DPOSIX_SPAWN=1)
  endif(HAVE_POSIX_SPAWNP)

endif(HAVE_SPAWN_H)

file(GLOB TESTS tests/test_*.js)

function(BASENAME OUT_VAR FILEPATH)
  set(OUTPUT "${FILEPATH}")
  string(REGEX REPLACE ".*/" "" OUTPUT "${OUTPUT}")
  string(REGEX REPLACE "\\.[^/.]*$" "" OUTPUT "${OUTPUT}")
  set("${OUT_VAR}"
      "${OUTPUT}"
      PARENT_SCOPE)
endfunction(
  BASENAME
  OUT_VAR
  FILEPATH)

function(RELATIVE_PATH OUT_VAR RELATIVE_TO)
  set(LIST "")

  foreach(ARG ${ARGN})
    file(RELATIVE_PATH ARG "${RELATIVE_TO}" "${ARG}")
    list(APPEND LIST "${ARG}")
  endforeach(ARG ${ARGN})

  set("${OUT_VAR}"
      "${LIST}"
      PARENT_SCOPE)
endfunction(
  RELATIVE_PATH
  RELATIVE_TO
  OUT_VAR)

relative_path(TESTS "${CMAKE_CURRENT_SOURCE_DIR}" ${TESTS})

if(DO_TESTS)
  include(CTest)

  foreach(TEST ${TESTS})
    basename(BASE "${TEST}")
    string(REPLACE "test_" "" NAME "${BASE}")
    add_test(
      NAME "${BASE}"
      COMMAND
        env
        QUICKJS_MODULE_PATH=${CMAKE_CURRENT_SOURCE_DIR}:${CMAKE_CURRENT_BINARY_DIR}
        "${QJS}" --bignum "${TEST}"
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    add_custom_target(
      "${BASE}" ALL
      COMMAND
        env
        QUICKJS_MODULE_PATH=${CMAKE_CURRENT_SOURCE_DIR}:${CMAKE_CURRENT_BINARY_DIR}
        "${QJSC}" -fbignum -M std -M os -M ${NAME} -o "${BASE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/${TEST}"
      WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
      SOURCES "${TEST}")
  endforeach(TEST ${TESTS})
endif(DO_TESTS)

check_c_source_runs(
  "#include <stdbool.h>\nbool foo(int a, int b, int *c) {\n   return __builtin_mul_overflow(a, b, c);\n}\nint main() {\n   int out;\n   if (foo(1, 2, &out)) {\n       return 0;\n   }\n   return 0;\n}"
  HAVE__BUILTIN_MUL_OVERFLOW)
if(HAVE__BUILTIN_MUL_OVERFLOW)
  add_definitions(-DHAVE__BUILTIN_MUL_OVERFLOW)
endif(HAVE__BUILTIN_MUL_OVERFLOW)


#[[foreach(NAME ${QUICKJS_MODULES})
  string(REGEX REPLACE "[-/]" "_" PFX "${NAME}")
  set(SOURCES quickjs-${NAME}.c)
  if(EXISTS quickjs-${NAME}.h)
    list(APPEND SOURCES quickjs-${NAME}.h)
  endif(EXISTS quickjs-${NAME}.h)
  list(APPEND SOURCES ${${PFX}_SOURCES})
  message("quickjs-${NAME} SOURCES ${SOURCES}")
  add_library(quickjs-${NAME} SHARED ${SOURCES})
  set_target_properties(quickjs-${NAME} PROPERTIES PREFIX ""
  INCLUDE_DIRECTORIES "${QUICKJS_PREFIX}/include/quickjs" OUTPUT_NAME "${NAME}"
)

  target_compile_definitions(quickjs-${NAME} PRIVATE -DJS_SHARED_LIBRARY)
  target_include_directories(quickjs-${NAME} BEFORE PUBLIC ${QUICKJS_INCLUDE_DIRS})
  install(TARGETS quickjs-${NAME} DESTINATION lib/quickjs PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
endforeach(NAME mmap inspect)]]

foreach(JS_MODULE ${QUICKJS_MODULES})
  make_shared_module(${JS_MODULE})
endforeach(JS_MODULE ${QUICKJS_MODULES})

target_link_libraries(quickjs-lexer quickjs-predicate)
add_dependencies(quickjs-lexer quickjs-predicate)


file(GLOB TESTS_SOURCES tests/test_*.js)
source_group(TESTS_GROUP FILES ${TESTS_SOURCES})

 include(CTest)

foreach(TEST_SOURCE ${TESTS_SOURCES})
    file(RELATIVE_PATH TEST_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}" "${TEST_SOURCE}")
  basename(TEST_NAME ${TEST_SOURCE} .js)
  add_test(NAME "${TEST_NAME}" COMMAND qjs --bignum "${TEST_SOURCE}" WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

endforeach(TEST_SOURCE ${TESTS_SOURCES})

install(FILES tests/console.js DESTINATION "${QUICKJS_LIBRARY_DIR}")