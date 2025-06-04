/*
 * Copyright 2016-2017 Nikolay Aleksiev. All rights reserved.
 * License: https://github.com/naleksiev/jread/blob/master/LICENSE
 */

#ifndef JREAD_H
#define JREAD_H

#include <stdint.h>

#ifndef JREAD_CONFIG_MAX_DEPTH
#   define JREAD_CONFIG_MAX_DEPTH 64
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum jr_type {
    jr_type_error,
    jr_type_null,
    jr_type_true,
    jr_type_false,
    jr_type_number,
    jr_type_string,
    jr_type_array_start,
    jr_type_array_end,
    jr_type_object_start,
    jr_type_object_end,
    jr_type_key,
} jr_type_t;

typedef struct jr_str {
    const char* cstr;
    int32_t     len;
} jr_str_t;

typedef void (*jr_callback)(jr_type_t type, const jr_str_t* data, void* user_data);

void jr_read(jr_callback cb, const char* doc, void* user_data);

#ifdef __cplusplus
}
#endif

#endif //#ifndef JREAD_H
