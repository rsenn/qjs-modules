/*
 * Copyright 2016-2017 Nikolay Aleksiev. All rights reserved.
 * License: https://github.com/naleksiev/xread/blob/master/LICENSE
 */

#include "xread.h"

#define XR_DISPATCH_NEXT()    goto *go[(uint8_t)*cstr++]
#define XR_DISPATCH_THIS()    goto *go[(uint8_t)cstr[-1]];

void xr_read(xr_callback cb, const char* cstr, void* user_data) {
    static void* go_root[] = {
        ['\0']        = &&l_done,
        [1 ... 8]     = &&l_error,
        ['\t']        = &&l_next,
        ['\n']        = &&l_next,
        [11 ... 12]   = &&l_error,
        ['\r']        = &&l_next,
        [14 ... 31]   = &&l_error,
        [' ']         = &&l_next,
        [33 ... 59]   = &&l_error,
        ['<']         = &&l_tag,
        [61 ... 255]  = &&l_error,
    };

    static void* go_name[] = {
        [0 ... 44]    = &&l_name_end,
        ['-']         = &&l_next,
        ['.']         = &&l_next,
        [47 ... 47]   = &&l_name_end,
        ['0' ... '9'] = &&l_next,
        [58 ... 64]   = &&l_name_end,
        ['A' ... 'Z'] = &&l_next,
        [91 ... 94]   = &&l_name_end,
        ['_']         = &&l_next,
        [96 ... 96]   = &&l_name_end,
        ['a' ... 'z'] = &&l_next,
        [123 ... 255] = &&l_name_end,
    };

    static void* go_stag[] = {
        [0 ... 46]    = &&l_error,
        ['/']         = &&l_etag,
        [48 ... 64]   = &&l_error,
        ['A' ... 'Z'] = &&l_name_begin,
        [91 ... 94]   = &&l_error,
        ['_']         = &&l_name_begin,
        [96 ... 96]   = &&l_error,
        ['a' ... 'z'] = &&l_name_begin,
        [123 ... 255] = &&l_error,
    };

    static void* go_etag[] = {
        [0 ... 64]    = &&l_error,
        ['A' ... 'Z'] = &&l_name_begin,
        [91 ... 94]   = &&l_error,
        ['_']         = &&l_name_begin,
        [96 ... 96]   = &&l_error,
        ['a' ... 'z'] = &&l_name_begin,
        [123 ... 255] = &&l_error,
    };

    static void* go_tag_close[] = {
        [0 ... 61]    = &&l_error,
        ['>']         = &&l_tag_end,
        [63 ... 255]  = &&l_error,
    };

    static void* go_attrib[] = {
        [0 ... 8]     = &&l_error,
        ['\t']        = &&l_next,
        ['\n']        = &&l_next,
        [11 ... 12]   = &&l_error,
        ['\r']        = &&l_next,
        [14 ... 31]   = &&l_error,
        [' ']         = &&l_next,
        [33 ... 46]   = &&l_error,
        ['/']         = &&l_empty_element_tag,
        [48 ... 61]   = &&l_error,
        ['>']         = &&l_tag_end,
        [63 ... 64]   = &&l_error,
        ['A' ... 'Z'] = &&l_attrib,
        [91 ... 94]   = &&l_error,
        ['_']         = &&l_attrib,
        [96 ... 96]   = &&l_error,
        ['a' ... 'z'] = &&l_attrib,
        [123 ... 255] = &&l_error,
    };

    static void* go_attrib_eq[] = {
        [0 ... 8]     = &&l_error,
        ['\t']        = &&l_next,
        ['\n']        = &&l_next,
        [11 ... 12]   = &&l_error,
        ['\r']        = &&l_next,
        [14 ... 31]   = &&l_error,
        [' ']         = &&l_next,
        [33 ... 60]   = &&l_error,
        ['=']         = &&l_attrib_eq,
        [62 ... 255]  = &&l_error,
    };

    static void* go_attrib_val_begin[] = {
        [0 ... 8]     = &&l_error,
        ['\t']        = &&l_next,
        ['\n']        = &&l_next,
        [11 ... 12]   = &&l_error,
        ['\r']        = &&l_next,
        [14 ... 31]   = &&l_error,
        [' ']         = &&l_next,
        [33]          = &&l_error,
        ['"']         = &&l_attrib_val_double,
        [35 ... 38]   = &&l_error,
        ['\'']        = &&l_attrib_val_single,
        [40 ... 255]  = &&l_error,
    };

    static void* go_attrib_val_single[] = {
        [0 ... 31]    = &&l_error,
        [32 ... 38]   = &&l_next,
        ['\'']        = &&l_attrib_val,
        [40 ... 255]  = &&l_next,
    };

    static void* go_attrib_val_double[] = {
        [0 ... 31]    = &&l_error,
        [32 ... 33]   = &&l_next,
        ['"']         = &&l_attrib_val,
        [35 ... 255]  = &&l_next,
    };

    xr_str_t tag  = { .cstr = 0, .len = 0 };
    xr_str_t name = { .cstr = 0, .len = 0 };
    xr_str_t val  = { .cstr = 0, .len = 0 };

    void**   go = go_root;
    void*    name_handle = 0;

l_next:
    XR_DISPATCH_NEXT();

l_error:
    name = (xr_str_t){ .cstr = "Error!", .len = 6 };
    val = (xr_str_t){ .cstr = cstr - 1, .len = 1 };
    cb(xr_type_error, &name, &val, user_data);
    return;

l_name_begin:
    tag.cstr = cstr - 1;
    go = go_name;
    XR_DISPATCH_NEXT();

l_name_end:
    goto *name_handle;

l_tag:
    name_handle = &&l_stag_name;
    go = go_stag;
    XR_DISPATCH_NEXT();

l_etag:
    name_handle = &&l_etag_name;
    go = go_etag;
    XR_DISPATCH_NEXT();

l_tag_end:
    go = go_root;
    XR_DISPATCH_NEXT();

l_stag_name:
    tag.len = (int32_t)(cstr - 1 - tag.cstr);
    cb(xr_type_element_start, &tag, 0, user_data);
    go = go_attrib;
    XR_DISPATCH_THIS();

l_etag_name:
    tag.len = (int32_t)(cstr - 1 - tag.cstr);
    cb(xr_type_element_end, &tag, 0, user_data);
    go = go_tag_close;
    XR_DISPATCH_THIS();

l_empty_element_tag:
    cb(xr_type_element_end, &tag, 0, user_data);
    go = go_tag_close;
    XR_DISPATCH_NEXT();

l_attrib:
    name.cstr = cstr - 1;
    name_handle = &&l_attrib_name;
    go = go_name;
    XR_DISPATCH_NEXT();

l_attrib_name:
    name.len = (int32_t)(cstr - 1 - name.cstr);
    go = go_attrib_eq;
    XR_DISPATCH_THIS();

l_attrib_eq:
    go = go_attrib_val_begin;
    XR_DISPATCH_NEXT();

l_attrib_val_single:
    val.cstr = cstr;
    go = go_attrib_val_single;
    XR_DISPATCH_NEXT();

l_attrib_val_double:
    val.cstr = cstr;
    go = go_attrib_val_double;
    XR_DISPATCH_NEXT();

l_attrib_val:
    val.len = (int32_t)(cstr - 1 - val.cstr);
    cb(xr_type_attribute, &name, &val, user_data);
    go = go_attrib;
    XR_DISPATCH_NEXT();

l_done:
    return;
}

