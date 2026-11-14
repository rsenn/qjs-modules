# find_openssl()
#
# Looks for a system OpenSSL (libssl + libcrypto, headers included). Sets OPENSSL_FOUND, and when
# found OPENSSL_INCLUDE_DIR and OPENSSL_LIBRARIES (the ssl and crypto libraries, libssl first).
#
# Named after CMake's own FindOpenSSL module, which it wraps: include this file by path, it is
# not on CMAKE_MODULE_PATH, so find_package(OpenSSL) still reaches CMake's module.
macro(find_openssl)
  find_package(OpenSSL QUIET)

  if(OPENSSL_FOUND AND OPENSSL_SSL_LIBRARY AND OPENSSL_CRYPTO_LIBRARY)
    set(OPENSSL_LIBRARIES ${OPENSSL_SSL_LIBRARY} ${OPENSSL_CRYPTO_LIBRARY})
    message(STATUS "\tOpenSSL library: ${OPENSSL_SSL_LIBRARY} ${OPENSSL_CRYPTO_LIBRARY}")
    message(STATUS "\tOpenSSL include dir: ${OPENSSL_INCLUDE_DIR}")
  else()
    set(OPENSSL_FOUND FALSE)
  endif()
endmacro()
