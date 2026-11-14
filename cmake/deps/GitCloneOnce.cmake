# cmake -DDIR=<dir> -DMARKER=<file> -DREPO=<url> -DTAG=<tag> -P GitCloneOnce.cmake
#
# Shallow-clones REPO at TAG into DIR unless DIR/MARKER exists. Runs at build time, so a
# checkout deleted after configuring is cloned again. Submodules are skipped.
if(EXISTS "${DIR}/${MARKER}")
  return()
endif()

file(REMOVE_RECURSE "${DIR}")
execute_process(COMMAND git clone --depth 1 --branch ${TAG} ${REPO} "${DIR}" RESULT_VARIABLE rc)

if(NOT rc EQUAL 0)
  message(FATAL_ERROR "git clone ${REPO} (${TAG}) failed: ${rc}")
endif()
