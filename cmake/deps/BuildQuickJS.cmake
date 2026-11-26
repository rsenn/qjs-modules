# BuildQuickJS.cmake: build the quickjs fork when no installation is used.
#
#   -DBUILD_QUICKJS=ON     build quickjs (or reuse the parent project's build)
#   -DBUILD_QUICKJS=AUTO   build it when find_quickjs() finds no installation [default]
#   -DBUILD_QUICKJS=OFF    never; an installation is required
#
# build_quickjs() configures and builds quickjs at configure time (qjsc and
# qjs are needed while configuring) and sets, for find_quickjs() to read:
#
#   QUICKJS_SOURCES_ROOT         quickjs sources
#   QUICKJS_BUILD_ROOT           where libquickjs, qjs and qjsc were built
#   QUICKJS_PREFIX               CMAKE_INSTALL_PREFIX of this project (runtime paths)
#   QUICKJS_INSTALL_LIBRARY_DIR  <prefix>/<libdir>, for INSTALL_RPATH
#
# Compile and link use the headers and libraries in QUICKJS_BUILD_ROOT, run
# time (rpath, module directories) uses the install prefix, and an install rule
# installs quickjs along with the project.
#
# nested projects: the variables above are plain (non-cache) variables, so
# add_subdirectory() children inherit them and find_quickjs() builds nothing.
# A separate configure (ExternalProject, a script) passes them as
# -DQUICKJS_BUILD_ROOT=.. -DQUICKJS_SOURCES_ROOT=.. -DQUICKJS_PREFIX=..

if(NOT DEFINED BUILD_QUICKJS)
  set(BUILD_QUICKJS AUTO CACHE STRING
                               "Build quickjs from source: ON always, AUTO when no installation is found, OFF never")
  set_property(CACHE BUILD_QUICKJS PROPERTY STRINGS ON OFF AUTO)
endif()

set(QUICKJS_GIT_URL "git@github.com:rsenn/quickjs.git" CACHE STRING "quickjs repository cloned by BUILD_QUICKJS")
set(QUICKJS_GIT_URL_FALLBACK "https://github.com/rsenn/quickjs.git"
    CACHE STRING "quickjs repository tried when QUICKJS_GIT_URL fails")
set(QUICKJS_BUILD_OPTIONS "" CACHE STRING "extra -D options for the quickjs configure, ';' separated")
option(BUILD_QUICKJS_INSTALL "Install the quickjs built by BUILD_QUICKJS along with this project" ON)

#
# quickjs_source_dir <OUTPUT-VARIABLE>
#
# The quickjs source directory, in this order:
#
#   - QUICKJS_SOURCES_ROOT, when it holds quickjs.c
#   - the parent of this project, when that is a quickjs tree (qjs-* checked out
#     inside quickjs/): no clone
#   - third_party/quickjs when third_party/ exists, else <build>/quickjs-src:
#     cloned from QUICKJS_GIT_URL, with its qjs-modules submodule
#
function(quickjs_source_dir OUTVAR)
  get_filename_component(_parent "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

  foreach(_dir "${QUICKJS_SOURCES_ROOT}" "${_parent}")
    if(_dir AND EXISTS "${_dir}/quickjs.c" AND EXISTS "${_dir}/CMakeLists.txt")
      set(${OUTVAR} "${_dir}" PARENT_SCOPE)
      return()
    endif()
  endforeach()

  if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
    set(_dir "${CMAKE_CURRENT_SOURCE_DIR}/third_party/quickjs")
  else()
    set(_dir "${CMAKE_CURRENT_BINARY_DIR}/quickjs-src")
  endif()

  if(NOT EXISTS "${_dir}/quickjs.c")
    find_package(Git REQUIRED)

    foreach(_url "${QUICKJS_GIT_URL}" "${QUICKJS_GIT_URL_FALLBACK}")
      if(_url)
        message(STATUS "Cloning quickjs from ${_url} into ${_dir}")
        execute_process(COMMAND "${GIT_EXECUTABLE}" clone --depth 1 "${_url}" "${_dir}" RESULT_VARIABLE _result
                        OUTPUT_QUIET ERROR_QUIET)
        if(_result EQUAL 0)
          break()
        endif()
        file(REMOVE_RECURSE "${_dir}")
      endif()
    endforeach()

    if(NOT EXISTS "${_dir}/quickjs.c")
      message(FATAL_ERROR "Could not clone quickjs from ${QUICKJS_GIT_URL} or ${QUICKJS_GIT_URL_FALLBACK}")
    endif()
  endif()

  # the quickjs build includes cmake/Functions.cmake, Checks.cmake and Compat.cmake
  # of its qjs-modules submodule
  if(EXISTS "${_dir}/.gitmodules" AND NOT EXISTS "${_dir}/qjs-modules/cmake/Functions.cmake")
    find_package(Git REQUIRED)
    message(STATUS "Fetching the qjs-modules submodule of quickjs")

    foreach(_rewrite "" "url.https://github.com/.insteadOf=git@github.com:")
      if(_rewrite)
        set(_config -c "${_rewrite}")
      else()
        set(_config)
      endif()

      execute_process(COMMAND "${GIT_EXECUTABLE}" ${_config} submodule update --init --depth 1 qjs-modules
                      WORKING_DIRECTORY "${_dir}" RESULT_VARIABLE _result OUTPUT_QUIET ERROR_QUIET)
      if(_result EQUAL 0)
        break()
      endif()
    endforeach()

    if(NOT EXISTS "${_dir}/qjs-modules/cmake/Functions.cmake")
      message(FATAL_ERROR "Could not fetch the qjs-modules submodule of ${_dir}")
    endif()
  endif()

  set(${OUTVAR} "${_dir}" PARENT_SCOPE)
endfunction()

#
# quickjs_configure_args <OUTPUT-VARIABLE> <SOURCE-DIR>
#
# The -D options for configuring quickjs: install prefix and toolchain of this
# project, shared + static libraries, and every sibling qjs-* module project
# switched off (the quickjs build would add_subdirectory() them, this project
# among them).
#
function(quickjs_configure_args OUTVAR SRC)
  if(CMAKE_BUILD_TYPE)
    set(_type "${CMAKE_BUILD_TYPE}")
  else()
    set(_type Release)
  endif()

  if(WASI OR EMSCRIPTEN OR LINK_STATIC OR "${CMAKE_SYSTEM_NAME}" STREQUAL "Emscripten")
    set(_shared OFF)
  else()
    set(_shared ON)
  endif()

  set(_args
      "-DCMAKE_BUILD_TYPE=${_type}" "-DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}"
      "-DQUICKJS_PREFIX=${CMAKE_INSTALL_PREFIX}" "-DBUILD_SHARED_LIBS=${_shared}" -DBUILD_STATIC_LIBS=ON
      -DCMAKE_POSITION_INDEPENDENT_CODE=ON)

  # CMAKE_SYSTEM_NAME and CMAKE_SYSROOT given on the command line make the
  # nested configure a cross build (CMAKE_CROSSCOMPILING=TRUE, quickjs then
  # expects a host qjsc on PATH) even when host and target are the same
  # machine, as on Termux (system name Android), so they are forwarded only
  # when this project itself is cross compiling. The toolchain's own inputs
  # (ANDROID_ABI, ANDROID_PLATFORM, ...) go with them, else the toolchain file
  # falls back to its defaults and builds another architecture (32 bit ARM).
  set(_vars CMAKE_TOOLCHAIN_FILE CMAKE_C_COMPILER)
  if(CMAKE_CROSSCOMPILING)
    list(APPEND _vars CMAKE_SYSROOT CMAKE_SYSTEM_NAME CMAKE_SYSTEM_PROCESSOR CMAKE_C_COMPILER_TARGET)
    get_cmake_property(_cache CACHE_VARIABLES)
    foreach(_var ${_cache})
      if(_var MATCHES "^(CMAKE_)?ANDROID_")
        list(APPEND _vars ${_var})
      endif()
    endforeach()
  endif()

  foreach(_var ${_vars})
    if(${_var})
      list(APPEND _args "-D${_var}=${${_var}}")
    endif()
  endforeach()

  file(GLOB _siblings RELATIVE "${SRC}" "${SRC}/qjs-*")
  foreach(_sibling ${_siblings})
    string(REGEX REPLACE "^qjs-" "" _name "${_sibling}")
    string(TOUPPER "${_name}" _name)
    list(APPEND _args "-DMODULE_${_name}=OFF")
  endforeach()

  list(APPEND _args ${QUICKJS_BUILD_OPTIONS})
  set(${OUTVAR} "${_args}" PARENT_SCOPE)
endfunction()

#
# quickjs_host_qjsc <OUTPUT-VARIABLE> <SOURCE-DIR>
#
# The qjsc to run while building, for a cross build: QJSC when given, else one
# built for the host in <build>/quickjs-host-build (host compiler, no toolchain
# file, no module projects). Its directory is put on PATH for the nested quickjs
# build, which looks for qjsc there, and QJSC is set to it so the modules'
# generated sources use it too, not the target qjsc found in QUICKJS_BUILD_ROOT.
#
function(quickjs_host_qjsc OUTVAR SRC)
  if(QJSC AND EXISTS "${QJSC}")
    set(${OUTVAR} "${QJSC}" PARENT_SCOPE)
    return()
  endif()

  set(_hdir "${CMAKE_CURRENT_BINARY_DIR}/quickjs-host-build")

  if(NOT EXISTS "${_hdir}/qjsc")
    find_program(_host_cc NAMES cc gcc clang NO_CMAKE_FIND_ROOT_PATH)
    if(NOT _host_cc)
      message(FATAL_ERROR "Cross build needs a host qjsc: no host C compiler found, pass -DQJSC=<host qjsc>")
    endif()

    set(_hargs -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON "-DCMAKE_C_COMPILER=${_host_cc}"
               -DCMAKE_INSTALL_PREFIX=${_hdir}/inst)
    file(GLOB _siblings RELATIVE "${SRC}" "${SRC}/qjs-*")
    foreach(_sibling ${_siblings})
      string(REGEX REPLACE "^qjs-" "" _name "${_sibling}")
      string(TOUPPER "${_name}" _name)
      list(APPEND _hargs "-DMODULE_${_name}=OFF")
    endforeach()

    message(STATUS "Configuring host qjsc: ${SRC} -> ${_hdir}")
    file(MAKE_DIRECTORY "${_hdir}")
    # a toolchain or compiler chosen through the environment must not reach the host build
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env --unset=CC --unset=CXX --unset=CFLAGS --unset=CXXFLAGS
                            --unset=LDFLAGS "${CMAKE_COMMAND}" -G "${CMAKE_GENERATOR}" ${_hargs} "${SRC}"
                    WORKING_DIRECTORY "${_hdir}" RESULT_VARIABLE _result OUTPUT_VARIABLE _out ERROR_VARIABLE _out)
    if(NOT _result EQUAL 0)
      message(FATAL_ERROR "Configuring the host qjsc failed:\n${_out}")
    endif()

    cmake_host_system_information(RESULT _cores QUERY NUMBER_OF_LOGICAL_CORES)
    message(STATUS "Building host qjsc in ${_hdir}")
    execute_process(COMMAND "${CMAKE_COMMAND}" --build "${_hdir}" --target qjsc --parallel ${_cores}
                    RESULT_VARIABLE _result OUTPUT_VARIABLE _out ERROR_VARIABLE _out)
    if(NOT _result EQUAL 0 OR NOT EXISTS "${_hdir}/qjsc")
      message(FATAL_ERROR "Building the host qjsc failed:\n${_out}")
    endif()
  endif()

  set(${OUTVAR} "${_hdir}/qjsc" PARENT_SCOPE)
endfunction()

#
# build_quickjs
#
# Configure and build quickjs in <build>/quickjs-build (the configure is redone
# only when sources or options change, the build is incremental), then export
# the variables described at the top of this file and add the install rule.
#
function(build_quickjs)
  quickjs_source_dir(_src)
  quickjs_configure_args(_args "${_src}")

  if(CMAKE_CROSSCOMPILING)
    quickjs_host_qjsc(_host_qjsc "${_src}")
    get_filename_component(_host_dir "${_host_qjsc}" DIRECTORY)
    set(ENV{PATH} "${_host_dir}:$ENV{PATH}")
    set(QJSC "${_host_qjsc}" CACHE FILEPATH "QuickJS compiler (host)" FORCE)
  endif()

  set(_bdir "${CMAKE_CURRENT_BINARY_DIR}/quickjs-build")
  set(_stamp "${_bdir}/configure.stamp")
  string(MD5 _hash "${_src};${_args};${CMAKE_GENERATOR}")

  set(_old "")
  if(EXISTS "${_stamp}")
    file(READ "${_stamp}" _old)
  endif()

  if(NOT EXISTS "${_bdir}/CMakeCache.txt" OR NOT "${_old}" STREQUAL "${_hash}")
    message(STATUS "Configuring quickjs: ${_src} -> ${_bdir}")
    file(MAKE_DIRECTORY "${_bdir}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -G "${CMAKE_GENERATOR}" ${_args} "${_src}" WORKING_DIRECTORY "${_bdir}"
                    RESULT_VARIABLE _result OUTPUT_VARIABLE _out ERROR_VARIABLE _out)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/quickjs-configure.log" "${_out}")
    if(NOT _result EQUAL 0)
      message(FATAL_ERROR "Configuring quickjs failed, see ${CMAKE_CURRENT_BINARY_DIR}/quickjs-configure.log\n${_out}")
    endif()
    file(WRITE "${_stamp}" "${_hash}")
  endif()

  cmake_host_system_information(RESULT _cores QUERY NUMBER_OF_LOGICAL_CORES)
  message(STATUS "Building quickjs in ${_bdir}")
  execute_process(COMMAND "${CMAKE_COMMAND}" --build "${_bdir}" --parallel ${_cores} RESULT_VARIABLE _result
                  OUTPUT_VARIABLE _out ERROR_VARIABLE _out)
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/quickjs-build.log" "${_out}")
  if(NOT _result EQUAL 0)
    message(FATAL_ERROR "Building quickjs failed, see ${CMAKE_CURRENT_BINARY_DIR}/quickjs-build.log\n${_out}")
  endif()

  # the library directory quickjs installs to, relative to the prefix (lib/<multiarch>)
  set(_libdir "")
  foreach(_var CMAKE_ARCH_LIBDIR QUICKJS_LIBRARY_DIR)
    if(NOT _libdir)
      file(STRINGS "${_bdir}/CMakeCache.txt" _line REGEX "^${_var}:" LIMIT_COUNT 1)
      string(REGEX REPLACE "^[^=]*=" "" _libdir "${_line}")
    endif()
  endforeach()
  if(NOT _libdir)
    set(_libdir lib)
  endif()
  if(NOT IS_ABSOLUTE "${_libdir}")
    set(_libdir "${CMAKE_INSTALL_PREFIX}/${_libdir}")
  endif()

  set(QUICKJS_SOURCES_ROOT "${_src}" CACHE PATH "QuickJS source directory" FORCE)
  set(QUICKJS_SOURCES_ROOT "${_src}" PARENT_SCOPE)
  set(QUICKJS_BUILD_ROOT "${_bdir}" PARENT_SCOPE)
  set(QUICKJS_PREFIX "${CMAKE_INSTALL_PREFIX}" PARENT_SCOPE)
  set(QUICKJS_INSTALL_LIBRARY_DIR "${_libdir}" PARENT_SCOPE)

  if(BUILD_QUICKJS_INSTALL)
    install(
      CODE "
      message(STATUS \"Installing quickjs from ${_bdir}\")
      execute_process(COMMAND \"${CMAKE_COMMAND}\" --install \"${_bdir}\" --prefix \"\${CMAKE_INSTALL_PREFIX}\"
                      RESULT_VARIABLE _quickjs_install_result)
      if(NOT _quickjs_install_result EQUAL 0)
        message(FATAL_ERROR \"Installing quickjs failed\")
      endif()
    ")
  endif()
endfunction()
