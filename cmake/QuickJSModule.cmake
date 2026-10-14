# quickjs_module_options([SHARED_DEFAULT <ON|OFF>] [STATIC_DEFAULT <ON|OFF>])
#
# Declares BUILD_SHARED/BUILD_STATIC the same way across
# every qjs-* project, so a single -DBUILD_SHARED_MODULES=.../
# -DBUILD_STATIC_MODULES=... passed to the top-level quickjs/ build reaches
# every add_subdirectory()'d qjs-* submodule unchanged (same cache-variable
# name everywhere). Guarded by NOT DEFINED so a value already set by the
# caller - this project's own earlier option() call, or the outer build -
# always wins; this macro only ever supplies the fallback default.
#
# WASI/Emscripten have no dlopen()-able shared-module story, so the shared
# default is forced off and the static default forced on there regardless
# of what the caller asked for.
macro(quickjs_module_options)
  cmake_parse_arguments(QMO "" "SHARED_DEFAULT;STATIC_DEFAULT" "" ${ARGN})
  if(NOT DEFINED QMO_SHARED_DEFAULT)
    set(QMO_SHARED_DEFAULT ON)
  endif()

  if(NOT DEFINED QMO_STATIC_DEFAULT)
    set(QMO_STATIC_DEFAULT OFF)
  endif(NOT DEFINED QMO_STATIC_DEFAULT)

  if(WASI OR EMSCRIPTEN OR "${CMAKE_SYSTEM_NAME}" STREQUAL "Emscripten")
    set(QMO_SHARED_DEFAULT OFF)
    set(QMO_STATIC_DEFAULT ON)
  endif(WASI OR EMSCRIPTEN OR "${CMAKE_SYSTEM_NAME}" STREQUAL "Emscripten")

  if(NOT DEFINED BUILD_SHARED)
    option(BUILD_SHARED "Build shared QuickJS module(s)" ${QMO_SHARED_DEFAULT})
  endif()

  if(NOT DEFINED BUILD_STATIC)
    option(BUILD_STATIC "Build static QuickJS module(s) (*.a)" ${QMO_STATIC_DEFAULT})
  endif(NOT DEFINED BUILD_STATIC)
endmacro()

#
# compile_code <RESULT-VARIABLE> <CODE>
#
# Try to compile the C source CODE against QuickJS and store whether it worked
# in RESULT-VARIABLE, unless already defined.
#
function(compile_code RESULT_VAR CODE)
  string(TOLOWER "${RESULT_VAR}" NAME)
  string(REGEX REPLACE "_" "-" FILE "try-${NAME}.c")

  if(NOT DEFINED "${RESULT_VAR}")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/${FILE}" "${CODE}")

    # must match the include search order the real qjsm.c/quickjs-*.c translation units get
    # (see include_directories(${QUICKJS_INCLUDE_DIRS}) / include_directories(${QUICKJS_INCLUDE_DIR})
    # in CMakeLists.txt) - otherwise this probe can silently detect a different header than the one
    # actually compiled against (js-module-loader-detection-vs-actual-headers)
    set(_compile_code_includes "${QUICKJS_INCLUDE_DIRS}" "${QUICKJS_SOURCES_ROOT}")
    set(_compile_code_iflags "")

    foreach(_dir ${_compile_code_includes})
      set(_compile_code_iflags "${_compile_code_iflags} -I${_dir}")
    endforeach(_dir ${_compile_code_includes})

    try_compile(
      RESULT "${CMAKE_CURRENT_BINARY_DIR}"
      "${CMAKE_CURRENT_BINARY_DIR}/${FILE}"
      LINK_OPTIONS "-L${QUICKJS_LIBRARY_DIR}"
      COMPILE_DEFINITIONS "${_compile_code_iflags}"
      CMAKE_FLAGS
        "-DINCLUDE_DIRECTORIES=${_compile_code_includes}" "-DLINK_DIRECTORIES=${QUICKJS_LIBRARY_DIR}" LINK_DIRECTORIES
        "${QUICKJS_LIBRARY_DIR}"
      LINK_LIBRARIES "${QUICKJS_LIBRARY}"
      OUTPUT_VARIABLE OUTPUT)

    set(${RESULT_VAR} "${RESULT}" PARENT_SCOPE)

    if(NOT RESULT)
      message(STATUS "Failed to compile '${FILE}'. Output:\n${OUTPUT}")
    endif(NOT RESULT)

  endif(NOT DEFINED "${RESULT_VAR}")
endfunction()

#
# config_module <TARGET_NAME>
#
# Apply the common link directory, dependencies and compile options
# (QUICKJS_LIBRARY_DIR, QUICKJS_MODULE_DEPENDENCIES, QUICKJS_MODULE_CFLAGS) to
# the target TARGET_NAME.
#
function(config_module TARGET_NAME)
  if(QUICKJS_LIBRARY_DIR)
    set_target_properties(${TARGET_NAME} PROPERTIES LINK_DIRECTORIES "${QUICKJS_LIBRARY_DIR}")
  endif()

  if(QUICKJS_MODULE_DEPENDENCIES)
    target_link_libraries(${TARGET_NAME} ${QUICKJS_MODULE_DEPENDENCIES})
  endif()

  if(QUICKJS_MODULE_CFLAGS)
    target_compile_options(${TARGET_NAME} PRIVATE "${QUICKJS_MODULE_CFLAGS}")
  endif(QUICKJS_MODULE_CFLAGS)
endfunction()

#
# make_module <FNAME>
#
# Define the QuickJS native module FNAME: a shared library target and/or a
# static one (per BUILD_SHARED/BUILD_STATIC) from its sources, libraries and
# deps, plus install rules.
#
function(make_module FNAME)
  string(REGEX REPLACE "_" "-" NAME "${FNAME}")
  string(REGEX REPLACE "-" "_" VNAME "${FNAME}")
  string(TOUPPER "${FNAME}" UUNAME)
  string(REGEX REPLACE "-" "_" UNAME "${UUNAME}")

  set(TARGET_NAME qjs-${NAME})
  set(DEPS ${${VNAME}_DEPS})
  set(LIBS ${${VNAME}_LIBRARIES})
  set(LINK_DIRECTORIES ${${VNAME}_LINK_DIRECTORIES})
  set(LINK_FLAGS ${${VNAME}_LINK_FLAGS})

  #dump(VNAME ${VNAME}_SOURCES)

  if(ARGN)
    set(SOURCES ${ARGN} ${${VNAME}_SOURCES} ${COMMON_SOURCES})
    set_add(DEPS ${${VNAME}_DEPS})
  else(ARGN)
    set(SOURCES quickjs-${NAME}.c ${${VNAME}_SOURCES} ${COMMON_SOURCES})
    set_add(LIBS ${${VNAME}_LIBRARIES})
  endif()

  set_add(LIBS ${COMMON_LIBRARIES})

  set(MSG "Building QuickJS module: ${FNAME}")

  if(DEPS)
    set(MSG "${MSG} (deps: ${DEPS})")
  endif()

  set(OUT "${LIBS}")
  list(REMOVE_ITEM OUT compiled)
  list(REMOVE_ITEM OUT modules)
  if(OUT)
    string(REPLACE ";" " " OUT "${OUT}")
    set(MSG "${MSG} (libs: ${OUT})")
  endif()

  #message(STATUS "${MSG}")

  if(WASI OR EMSCRIPTEN OR "${CMAKE_SYSTEM_NAME}" STREQUAL "Emscripten")
    set(BUILD_SHARED OFF)
  endif(WASI OR EMSCRIPTEN OR "${CMAKE_SYSTEM_NAME}" STREQUAL "Emscripten")

  if(NOT WASI AND "${CMAKE_SYSTEM_NAME}" STREQUAL "Emscripten")
    set(PREFIX "lib")
  else(NOT WASI AND "${CMAKE_SYSTEM_NAME}" STREQUAL "Emscripten")
    set(PREFIX "")
  endif(NOT WASI AND "${CMAKE_SYSTEM_NAME}" STREQUAL "Emscripten")

  if(BUILD_SHARED)
    #add_library(${TARGET_NAME} MODULE ${SOURCES})
    add_library(${TARGET_NAME} SHARED ${SOURCES})

    #dump(QUICKJS_C_MODULE_DIR QUICKJS_LIBRARY_DIR)
    #dump(MODULE_COMPILE_FLAGS)

    set_target_properties(
      ${TARGET_NAME}
      PROPERTIES RPATH "${MBEDTLS_LIBRARY_DIR}:${QUICKJS_C_MODULE_DIR}" INSTALL_RPATH "${QUICKJS_C_MODULE_DIR}"
                 LINK_FLAGS "${LINK_FLAGS}" PREFIX "${PREFIX}" OUTPUT_NAME "${VNAME}" COMPILE_FLAGS
                                                                                      "${MODULE_COMPILE_FLAGS}")

    target_compile_definitions(
      ${TARGET_NAME} PRIVATE _GNU_SOURCE=1 JS_SHARED_LIBRARY=1 JS_${UNAME}_MODULE=1
                             QUICKJS_PREFIX="${QUICKJS_INSTALL_PREFIX}" LIBMAGIC_DB="${LIBMAGIC_DB}")

    target_link_directories(${TARGET_NAME} PUBLIC ${LINK_DIRECTORIES} ${QUICKJS_LIBRARY_DIR}
                            ${CMAKE_CURRENT_BINARY_DIR})

    target_link_libraries(${TARGET_NAME} PUBLIC ${LIBS} ${QUICKJS_LIBRARY})

    install(TARGETS ${TARGET_NAME} DESTINATION "${QUICKJS_C_MODULE_DIR}"
            PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

    config_module(${TARGET_NAME})

    # ${VNAME}_LIBRARIES_shared overrides plain ${VNAME}_LIBRARIES when a module needs a
    # genuinely different (PIC-built) import target for its shared vs. static build --
    # see build_libarchive()/build_libserialport() in cmake/BuildLib*.cmake, which set
    # e.g. archive_LIBRARIES_shared/archive_LIBRARIES_static to two distinct IMPORTED
    # targets rather than one shared between both.
    if(DEFINED ${VNAME}_LIBRARIES_shared)
      set(LIBRARIES ${${VNAME}_LIBRARIES_shared})
    else()
      set(LIBRARIES ${${VNAME}_LIBRARIES})
    endif()
    if(LIBRARIES)
      target_link_libraries(${TARGET_NAME} PRIVATE ${LIBRARIES})
    endif(LIBRARIES)
    set(LINK_DIRECTORIES ${${VNAME}_LINK_DIRECTORIES})
    if(LINK_DIRECTORIES)
      target_link_directories(${TARGET_NAME} PRIVATE ${LINK_DIRECTORIES})
    endif(LINK_DIRECTORIES)
    if(DEPS)
      add_dependencies(${TARGET_NAME} ${DEPS})
    endif(DEPS)

  endif(BUILD_SHARED)

  list(APPEND MODULES_SOURCES quickjs-${NAME}.c)
  set(MODULES_SOURCES "${MODULES_SOURCES}" PARENT_SCOPE)

  if(BUILD_STATIC)
    set(STATIC_TARGET_NAME "${TARGET_NAME}-static")

    add_library(${STATIC_TARGET_NAME} STATIC ${SOURCES})

    # Deliberately no JS_SHARED_LIBRARY define here: every quickjs-*.c ends
    # with `#if defined(JS_SHARED_LIBRARY) && defined(JS_*_MODULE) /
    # define JS_INIT_MODULE js_init_module / #else / js_init_module_<name>`,
    # so leaving it undefined is what makes the entry point come out named
    # js_init_module_${VNAME} instead of the dlopen-convention js_init_module
    # (which would collide across every statically-linked module).
    set_target_properties(${STATIC_TARGET_NAME} PROPERTIES OUTPUT_NAME "${VNAME}" PREFIX "quickjs-"
                                                            COMPILE_FLAGS "${MODULE_COMPILE_FLAGS}")

    target_compile_definitions(
      ${STATIC_TARGET_NAME} PRIVATE _GNU_SOURCE=1 JS_${UNAME}_MODULE=1 QUICKJS_PREFIX="${QUICKJS_INSTALL_PREFIX}"
                                    LIBMAGIC_DB="${LIBMAGIC_DB}")

    # LIBRARIES/DEPS come from shared variables like ${VNAME}_LIBRARIES (e.g.
    # lexer_LIBRARIES=qjs-location) that name the *shared* module target;
    # under BUILD_STATIC only "<that>-static" actually exists, so rewrite
    # in-tree "qjs-*" references to their static counterpart. Anything else
    # (e.g. serial_DEPS=libserialport, an ExternalProject target) is left
    # alone. ${VNAME}_LIBRARIES_static, when defined, is used as-is instead (already the
    # correct non-PIC target -- see the ${VNAME}_LIBRARIES_shared comment above).
    set(STATIC_LIBRARIES "")
    if(DEFINED ${VNAME}_LIBRARIES_static)
      set(STATIC_LIBRARIES ${${VNAME}_LIBRARIES_static})
    else()
      foreach(LIB ${${VNAME}_LIBRARIES})
        if(LIB MATCHES "^qjs-")
          list(APPEND STATIC_LIBRARIES "${LIB}-static")
        else()
          list(APPEND STATIC_LIBRARIES "${LIB}")
        endif()
      endforeach()
    endif()
    if(STATIC_LIBRARIES)
      target_link_libraries(${STATIC_TARGET_NAME} PRIVATE ${STATIC_LIBRARIES})
    endif(STATIC_LIBRARIES)
    set(LINK_DIRECTORIES ${${VNAME}_LINK_DIRECTORIES})
    if(LINK_DIRECTORIES)
      target_link_directories(${STATIC_TARGET_NAME} PRIVATE ${LINK_DIRECTORIES})
    endif(LINK_DIRECTORIES)
    set(STATIC_DEPS "")
    foreach(DEP ${DEPS})
      if(DEP MATCHES "^qjs-")
        list(APPEND STATIC_DEPS "${DEP}-static")
      else()
        list(APPEND STATIC_DEPS "${DEP}")
      endif()
    endforeach()
    if(STATIC_DEPS)
      add_dependencies(${STATIC_TARGET_NAME} ${STATIC_DEPS})
    endif(STATIC_DEPS)

    list(APPEND STATIC_MODULE_TARGETS "${STATIC_TARGET_NAME}")
    list(APPEND STATIC_MODULE_NAMES "${NAME}")
    set(STATIC_MODULE_TARGETS "${STATIC_MODULE_TARGETS}" PARENT_SCOPE)
    set(STATIC_MODULE_NAMES "${STATIC_MODULE_NAMES}" PARENT_SCOPE)

    # A <name>.module.cmake next to the .a, for a *separate* CMake configure (a sibling
    # project, or this one's own EXTERNAL_MODULES/BUILTIN_MODULES machinery) to include()
    # and learn this module's transitive link requirements without re-deriving them --
    # "modules"/"compiled" (this project's own internal libraries, meaningless outside
    # it) are dropped, and any qjs-<dep> sibling module is exported by bare name ("dep",
    # not "qjs-dep-static", a target name that wouldn't exist in the other configure) so
    # the consumer can resolve it however it resolves module names (e.g. its own
    # BUILTIN_MODULES).
    # Recomputed from LIBS rather than reusing OUT: OUT gets mangled into a
    # space-joined display string above (for the status message), no longer a
    # proper list.
    set(_module_cmake_libs "${LIBS}")
    list(REMOVE_ITEM _module_cmake_libs compiled modules)
    list(TRANSFORM _module_cmake_libs REPLACE "^qjs-" "")
    get_target_property(_module_cmake_lib_path "${STATIC_TARGET_NAME}" ARCHIVE_OUTPUT_DIRECTORY)
    if(NOT _module_cmake_lib_path)
      set(_module_cmake_lib_path "${CMAKE_CURRENT_BINARY_DIR}")
    endif()
    # No CMAKE_STATIC_LIBRARY_PREFIX ("lib"): the target's own PREFIX property above
    # overrides it to "quickjs-" outright, not on top of it.
    set(_module_cmake_lib_file "${_module_cmake_lib_path}/quickjs-${VNAME}${CMAKE_STATIC_LIBRARY_SUFFIX}")

    file(
      WRITE "${CMAKE_CURRENT_BINARY_DIR}/quickjs-${VNAME}.module.cmake"
      "# Auto-generated by make_module(${NAME}) -- do not edit, will be overwritten.\n"
      "set(${VNAME}_MODULE_LIBRARY \"${_module_cmake_lib_file}\")\n"
      "set(${VNAME}_MODULE_LIBRARIES \"${_module_cmake_libs}\")\n"
      "set(${VNAME}_MODULE_LINK_FLAGS \"${LINK_FLAGS}\")\n"
      "set(${VNAME}_MODULE \"\${${VNAME}_MODULE_LIBRARY};\${${VNAME}_MODULE_LIBRARIES}\")\n")
  endif(BUILD_STATIC)

endfunction()

if(WASI)
  set(CMAKE_EXECUTABLE_SUFFIX ".wasm")
endif(WASI)

# BUILD_SHARED/BUILD_STATIC themselves are declared by this
# project's own CMakeLists.txt, right after it include()s this file (see
# quickjs_module_options() above) - not here, so each project can pick its
# own defaults through that one call site instead of this shared file
# hardcoding one default for everybody.

if(WIN32 OR MINGW)
  set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS TRUE)
endif(WIN32 OR MINGW)

if(WASI OR WASM OR EMSCRIPTEN OR "${CMAKE_SYSTEM_NAME}" STREQUAL "Emscripten")
  set(LIBRARY_PREFIX "lib")
  set(LIBRARY_SUFFIX ".a")
endif(WASI OR WASM OR EMSCRIPTEN OR "${CMAKE_SYSTEM_NAME}" STREQUAL "Emscripten")

if(NOT LIBRARY_PREFIX)
  set(LIBRARY_PREFIX "${CMAKE_STATIC_LIBRARY_PREFIX}")
endif(NOT LIBRARY_PREFIX)
if(NOT LIBRARY_SUFFIX)
  set(LIBRARY_SUFFIX "${CMAKE_STATIC_LIBRARY_SUFFIX}")
endif(NOT LIBRARY_SUFFIX)
