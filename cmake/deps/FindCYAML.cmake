# find_cyaml()
#
# Looks for an installed CYAML (https://github.com/andrewmd5/cyaml): cyaml.h and libcyaml.
# Sets CYAML_LIBRARY, CYAML_INCLUDE_DIR and HAVE_CYAML_H; CYAML_LIBRARY stays empty when
# either is missing. CYAML has no pkg-config file; CYAML_PREFIX, CYAML_INCLUDE_DIR and
# CYAML_LIBRARY_DIR hint where to look.
macro(find_cyaml)
  find_path(CYAML_INCLUDE_DIR NAMES cyaml.h HINTS "${CYAML_PREFIX}/include" "${CYAML_INCLUDE_DIR}")
  find_library(CYAML_LIBRARY NAMES cyaml HINTS "${CYAML_PREFIX}/lib" "${CYAML_LIBRARY_DIR}")

  if(CYAML_INCLUDE_DIR AND EXISTS "${CYAML_INCLUDE_DIR}/cyaml.h")
    set(HAVE_CYAML_H TRUE)
  else()
    set(HAVE_CYAML_H FALSE)
  endif()

  if(CYAML_LIBRARY)
    message(STATUS "\tCYAML library: ${CYAML_LIBRARY}")
    message(STATUS "\tCYAML include dir: ${CYAML_INCLUDE_DIR}")
  endif(CYAML_LIBRARY)
endmacro()
