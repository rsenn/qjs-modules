#ifndef BASE64_H
#define BASE64_H

#include <stddef.h>
#include <stdint.h>

size_t b64_get_encoded_buffer_size(const size_t decoded_size);
size_t b64_get_decoded_buffer_size(const size_t encoded_size);
size_t b64url_get_encoded_buffer_size(const size_t decoded_size);
size_t b64url_get_decoded_buffer_size(const size_t encoded_size);
void b64_encode(const uint8_t* raw, const size_t len, uint8_t* out);
void b64url_encode(const uint8_t* raw, const size_t len, uint8_t* out);
size_t b64_decode(const uint8_t* enc, const size_t len, uint8_t* out);
size_t b64url_decode(const uint8_t* enc, const size_t len, uint8_t* out);

#endif /* defined(BASE64_H) */
