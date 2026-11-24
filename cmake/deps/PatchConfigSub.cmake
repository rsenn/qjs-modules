# PatchConfigSub.cmake: make an autotools config.sub accept any triplet.
# usage (ExternalProject PATCH_COMMAND):
#   ${CMAKE_COMMAND} -DSOURCE_DIR=<SOURCE_DIR> -P PatchConfigSub.cmake
# the original stays as config.sub.orig and is tried first, so known
# triplets still get canonicalized, unknown ones (dietlibc) pass through.
# idempotent: a second run finds config.sub.orig and does nothing.

set(_sub "${SOURCE_DIR}/config.sub")

if(EXISTS "${_sub}" AND NOT EXISTS "${_sub}.orig")
  file(RENAME "${_sub}" "${_sub}.orig")
  file(WRITE "${_sub}" "#!/bin/sh\n\"$0.orig\" \"$@\" 2>/dev/null || echo \"$1\"\n")
  file(CHMOD "${_sub}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
endif()
