# find_libressl()
#
# Like find_openssl() (FindOpenSSL.cmake), but only accepts an installed LibreSSL: the found headers
# must define LIBRESSL_VERSION_NUMBER. Sets OPENSSL_FOUND, OPENSSL_INCLUDE_DIR and OPENSSL_LIBRARIES.
include(${CMAKE_CURRENT_LIST_DIR}/FindOpenSSL.cmake)

macro(find_libressl)
  find_openssl()

  if(OPENSSL_FOUND)
    file(STRINGS "${OPENSSL_INCLUDE_DIR}/openssl/opensslv.h" _FLS_LINE REGEX "LIBRESSL_VERSION_NUMBER")
    if(NOT _FLS_LINE)
      set(OPENSSL_FOUND FALSE)
    endif()
  endif()
endmacro()
