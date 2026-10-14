# qjsm-layout.cmake: the module layout of an installation, the build info baked into qjsm and
# the manifest installed next to the modules (doc/qjsm-layout.md 3 to 6).
#
#   qjsm_layout()    after qjsm_process_builtins(), before qjsm_add_executable():
#                    writes qjsm-buildinfo.c (added to QJSM_SOURCES) and manifest.json (installed
#                    to QJSM_DATA_DIR), sets QJSM_LAYOUT_REPORT for qjsm_report()
#
# cache variables:
#   QJSM_DATA_DIR                 manifest and other data, relative to the prefix (share/qjsm)
#   QJSM_INSTALL_BUILTIN_NATIVE   also install the .so of a native module that is builtin (OFF)
#
# The native and JS module directories stay the QUICKJS_C_MODULE_DIR / QUICKJS_JS_MODULE_DIR the
# quickjs fork installs by; QJSM_MODULE_PATH is QUICKJS_MODULE_PATH.

set(QJSM_DATA_DIR "share/qjsm" CACHE STRING "qjsm data directory (manifest), relative to the install prefix")
option(QJSM_INSTALL_BUILTIN_NATIVE "Also install the shared library of a native module that is built into qjsm" OFF)

# qjsm_json_escape(<OUT-VAR> <TEXT>): TEXT as the inside of a JSON string
function(qjsm_json_escape OUTPUT_VAR TEXT)
  string(REPLACE "\\" "\\\\" TEXT "${TEXT}")
  string(REPLACE "\"" "\\\"" TEXT "${TEXT}")
  string(REPLACE "\n" "\\n" TEXT "${TEXT}")
  string(REPLACE "\t" "\\t" TEXT "${TEXT}")
  set("${OUTPUT_VAR}" "${TEXT}" PARENT_SCOPE)
endfunction()

# qjsm_json_array(<OUT-VAR> <ITEM>...): ["a","b"]
function(qjsm_json_array OUTPUT_VAR)
  set(OUT "")
  foreach(ITEM ${ARGN})
    qjsm_json_escape(ITEM "${ITEM}")
    if(OUT)
      set(OUT "${OUT},")
    endif()
    set(OUT "${OUT}\"${ITEM}\"")
  endforeach()
  set("${OUTPUT_VAR}" "[${OUT}]" PARENT_SCOPE)
endfunction()

# qjsm_json_string(<OUT-VAR> <TEXT>): "text"
function(qjsm_json_string OUTPUT_VAR TEXT)
  qjsm_json_escape(TEXT "${TEXT}")
  set("${OUTPUT_VAR}" "\"${TEXT}\"" PARENT_SCOPE)
endfunction()

# qjsm_native_builtin <OUT-VAR> <NAME>: whether the native module NAME ('-' or '_') is built into qjsm
function(qjsm_native_builtin OUTPUT_VAR NAME)
  string(REPLACE "-" "_" NAME "${NAME}")
  if(NAME IN_LIST QJSM_BUILTIN_NATIVE)
    set("${OUTPUT_VAR}" TRUE PARENT_SCOPE)
  else()
    set("${OUTPUT_VAR}" FALSE PARENT_SCOPE)
  endif()
endfunction()

macro(qjsm_layout)
  set(QJSM_BUILTIN_NATIVE ${BUILTINS_NATIVE} ${BUILTINS_NEEDED_NATIVE})
  set(QJSM_BUILTIN_JS ${BUILTINS_COMPILED} ${BUILTINS_NEEDED_JS})
  list(TRANSFORM QJSM_BUILTIN_NATIVE REPLACE "-" "_")
  list(REMOVE_DUPLICATES QJSM_BUILTIN_NATIVE)
  list(REMOVE_DUPLICATES QJSM_BUILTIN_JS)
  list(SORT QJSM_BUILTIN_NATIVE)
  list(SORT QJSM_BUILTIN_JS)

  # native modules: installed unless builtin (and QJSM_INSTALL_BUILTIN_NATIVE is off)
  set(QJSM_INSTALLED_NATIVE "")
  set(QJSM_SHADOWED "")
  foreach(_ln ${QUICKJS_MODULES})
    string(REPLACE "-" "_" _ln "${_ln}")
    if(_ln IN_LIST QJSM_BUILTIN_NATIVE)
      if(QJSM_INSTALL_BUILTIN_NATIVE)
        list(APPEND QJSM_INSTALLED_NATIVE "${_ln}")
        list(APPEND QJSM_SHADOWED "${_ln}")
      endif()
    else()
      list(APPEND QJSM_INSTALLED_NATIVE "${_ln}")
    endif()
  endforeach()
  list(SORT QJSM_INSTALLED_NATIVE)

  set(QJSM_INSTALLED_JS ${INSTALLED_JS})
  list(SORT QJSM_INSTALLED_JS)

  file(RELATIVE_PATH _native_rel "${CMAKE_INSTALL_PREFIX}" "${QUICKJS_C_MODULE_DIR}")
  file(RELATIVE_PATH _js_rel "${CMAKE_INSTALL_PREFIX}" "${QUICKJS_JS_MODULE_DIR}")

  # build info: the commit, the date and the tools
  find_package(Git QUIET)
  set(_commit "")
  set(_dirty false)
  if(GIT_FOUND)
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 HEAD WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                    OUTPUT_VARIABLE _commit OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    execute_process(COMMAND "${GIT_EXECUTABLE}" diff --quiet HEAD WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                    RESULT_VARIABLE _dirty_result OUTPUT_QUIET ERROR_QUIET)
    if(_dirty_result)
      set(_dirty true)
    endif()
  endif()

  if(DEFINED ENV{SOURCE_DATE_EPOCH})
    string(TIMESTAMP _date "%Y-%m-%dT%H:%M:%SZ" UTC)
    execute_process(COMMAND date -u -d "@$ENV{SOURCE_DATE_EPOCH}" +%Y-%m-%dT%H:%M:%SZ OUTPUT_VARIABLE _date_epoch
                    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(_date_epoch)
      set(_date "${_date_epoch}")
    endif()
  else()
    string(TIMESTAMP _date "%Y-%m-%dT%H:%M:%SZ" UTC)
  endif()

  set(_qjsc_version "")
  if(QJSC)
    execute_process(COMMAND "${QJSC}" -h OUTPUT_VARIABLE _qjsc_out ERROR_VARIABLE _qjsc_err OUTPUT_STRIP_TRAILING_WHITESPACE)
    string(REGEX MATCH "QuickJS Compiler version [^\n]*" _qjsc_line "${_qjsc_out}${_qjsc_err}")
    string(REGEX REPLACE "^QuickJS Compiler version " "" _qjsc_version "${_qjsc_line}")
  endif()

  execute_process(COMMAND "${CMAKE_C_COMPILER}" -dumpmachine OUTPUT_VARIABLE _target OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  if(NOT _target)
    set(_target "${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
  endif()

  qjsm_json_string(_j_version "${PROJECT_VERSION}")
  qjsm_json_string(_j_commit "${_commit}")
  qjsm_json_string(_j_date "${_date}")
  set(_qjs_version "${QUICKJS_VERSION}")
  if(NOT _qjs_version)
    set(_qjs_version "${_qjsc_version}")
  endif()
  qjsm_json_string(_j_qjs_version "${_qjs_version}")
  qjsm_json_string(_j_qjsc_version "${_qjsc_version}")
  qjsm_json_string(_j_qjsc_path "${QJSC}")
  qjsm_json_string(_j_compiler "${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}")
  qjsm_json_string(_j_target "${_target}")
  qjsm_json_string(_j_build_type "${CMAKE_BUILD_TYPE}")
  qjsm_json_string(_j_prefix "${CMAKE_INSTALL_PREFIX}")
  qjsm_json_string(_j_native_dir "${_native_rel}")
  qjsm_json_string(_j_js_dir "${_js_rel}")
  qjsm_json_string(_j_data_dir "${QJSM_DATA_DIR}")
  qjsm_json_array(_j_module_path ${QUICKJS_MODULE_PATH})
  qjsm_json_array(_j_b_native ${QJSM_BUILTIN_NATIVE})
  qjsm_json_array(_j_b_js ${QJSM_BUILTIN_JS})
  qjsm_json_array(_j_i_native ${QJSM_INSTALLED_NATIVE})
  qjsm_json_array(_j_i_js ${QJSM_INSTALLED_JS})
  qjsm_json_array(_j_shadowed ${QJSM_SHADOWED})

  set(_info
      "{\"qjsm\":{\"version\":${_j_version},\"commit\":${_j_commit},\"dirty\":${_dirty},\"date\":${_j_date}},\"quickjs\":{\"version\":${_j_qjs_version}},\"qjsc\":{\"version\":${_j_qjsc_version},\"path\":${_j_qjsc_path}},\"compiler\":${_j_compiler},\"target\":${_j_target},\"buildType\":${_j_build_type},\"layout\":{\"prefix\":${_j_prefix},\"native\":${_j_native_dir},\"js\":${_j_js_dir},\"data\":${_j_data_dir},\"modulePath\":${_j_module_path}},\"builtin\":{\"native\":${_j_b_native},\"js\":${_j_b_js}}}")

  set(_manifest
      "{\n  \"qjsm\": {\"version\": ${_j_version}, \"commit\": ${_j_commit}, \"dirty\": ${_dirty}, \"date\": ${_j_date}},\n  \"quickjs\": {\"version\": ${_j_qjs_version}},\n  \"qjsc\": {\"version\": ${_j_qjsc_version}},\n  \"target\": ${_j_target},\n  \"paths\": {\"native\": ${_j_native_dir}, \"js\": ${_j_js_dir}, \"data\": ${_j_data_dir}},\n  \"builtin\": {\"native\": ${_j_b_native}, \"js\": ${_j_b_js}},\n  \"installed\": {\"native\": ${_j_i_native}, \"js\": ${_j_i_js}},\n  \"shadowed\": ${_j_shadowed}\n}\n")
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/manifest.json" "${_manifest}")
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/manifest.json" DESTINATION "${QJSM_DATA_DIR}")

  set(_version_line "qjsm ${PROJECT_VERSION}")
  if(_commit)
    set(_version_line "${_version_line} (${_commit}")
    if(_dirty)
      set(_version_line "${_version_line}-dirty")
    endif()
    set(_version_line "${_version_line})")
  endif()
  set(_version_line "${_version_line} quickjs ${_qjs_version} ${_target} ${_date}")

  # qjsm-buildinfo.c: the info as one string behind a magic, in its own ELF section, so it can
  # be found without running qjsm: objcopy -O binary --only-section=.qjsm.info qjsm info.bin
  string(REPLACE "\\" "\\\\" _c_info "${_info}")
  string(REPLACE "\"" "\\\"" _c_info "${_c_info}")
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/qjsm-buildinfo.c.tmp"
       "/* generated by cmake/qjsm-layout.cmake */\n#if defined(__ELF__)\n__attribute__((section(\".qjsm.info\"), used))\n#endif\nconst char qjsm_buildinfo[] = \"QJSM-BUILDINFO\\0\" \"${_c_info}\";\nconst char qjsm_version_line[] = \"${_version_line}\";\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_CURRENT_BINARY_DIR}/qjsm-buildinfo.c.tmp"
                          "${CMAKE_CURRENT_BINARY_DIR}/qjsm-buildinfo.c")
  list(APPEND QJSM_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/qjsm-buildinfo.c")

  # the configure report
  list(LENGTH QJSM_BUILTIN_NATIVE _n_b_native)
  list(LENGTH QJSM_INSTALLED_NATIVE _n_i_native)
  list(LENGTH QJSM_INSTALLED_JS _n_i_js)
  set(QJSM_INSTALL_BUILTIN_NATIVE_NOTE "")
  if(QJSM_INSTALL_BUILTIN_NATIVE)
    set(QJSM_INSTALL_BUILTIN_NATIVE_NOTE ", builtin ones installed too")
  endif()
  set(_shadow_text "none")
  if(QJSM_SHADOWED)
    string(REPLACE ";" " " _shadow_text "${QJSM_SHADOWED}")
  endif()
  string(REPLACE ";" " : " _path_text "${QUICKJS_MODULE_PATH}")
  set(QJSM_LAYOUT_REPORT
      "=> Layout\n\n  prefix            ${CMAKE_INSTALL_PREFIX}\n  native modules    ${QUICKJS_C_MODULE_DIR}   (${_n_i_native} installed, ${_n_b_native} builtin${QJSM_INSTALL_BUILTIN_NATIVE_NOTE})\n  JS modules        ${QUICKJS_JS_MODULE_DIR}   (${_n_i_js} installed)\n  data              ${CMAKE_INSTALL_PREFIX}/${QJSM_DATA_DIR}   (manifest.json)\n  module path       ${_path_text}\n  builtin and installed at the same time: ${_shadow_text}\n")
endmacro(qjsm_layout)
