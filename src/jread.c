/*
 * Copyright 2016-2017 Nikolay Aleksiev. All rights reserved.
 * License: https://github.com/naleksiev/jread/blob/master/LICENSE
 */

#include "jread.h"

#define JR_DISPATCH_NEXT()      goto *go[(uint8_t)*cstr++]
#define JR_DISPATCH_THIS()      goto *go[(uint8_t)cstr[-1]];
#define JR_DISPATCH_NEXT_GO(x)  goto *x[(uint8_t)*cstr++]
#define JR_DISPATCH_THIS_GO(x)  goto *x[(uint8_t)cstr[-1]];
#define JR_DISPATCH_NEXT_MASK() goto *go_utf8[(uint8_t)*cstr++ & utf8_mask]
#define JR_PUSH(x)              go_stack[go_stack_idx++] = go
#define JR_PUSH_GO(x)           go_stack[go_stack_idx++] = go; go = x
#define JR_POP_GO()             go = go_stack[--go_stack_idx]

void jr_read(jr_callback cb, const char* cstr, void* user_data) {
    static void* go_doc[] = {
        ['\0']        = &&l_done,
        [1 ... 8]     = &&l_err,
        ['\t']        = &&l_next,
        ['\n']        = &&l_next,
        [11 ... 12]   = &&l_err,
        ['\r']        = &&l_next,
        [14 ... 31]   = &&l_err,
        [' ']         = &&l_next,
        [33 ... 33]   = &&l_err,
        ['"']         = &&l_str_s,
        [35 ... 44]   = &&l_err,
        ['-']         = &&l_num_s,
        [46 ... 47]   = &&l_err,
        ['0' ... '9'] = &&l_num_s,
        [58 ... 90]   = &&l_err,
        ['[']         = &&l_arr_s,
        [92 ... 101]  = &&l_err,
        ['f']         = &&l_false_f,
        [103 ... 109] = &&l_err,
        ['n']         = &&l_null_n,
        [111 ... 115] = &&l_err,
        ['t']         = &&l_true_t,
        [117 ... 122] = &&l_err,
        ['{']         = &&l_obj_s,
        [124 ... 255] = &&l_err,
    };

    static void* go_val[] = {
        [0 ... 8]     = &&l_err,
        ['\t']        = &&l_next,
        ['\n']        = &&l_next,
        [11 ... 12]   = &&l_err,
        ['\r']        = &&l_next,
        [14 ... 31]   = &&l_err,
        [' ']         = &&l_next,
        [33 ... 33]   = &&l_err,
        ['"']         = &&l_str_s,
        [35 ... 44]   = &&l_err,
        ['-']         = &&l_num_s,
        [46 ... 47]   = &&l_err,
        ['0' ... '9'] = &&l_num_s,
        [58 ... 90]   = &&l_err,
        ['[']         = &&l_arr_s,
        [92 ... 101]  = &&l_err,
        ['f']         = &&l_false_f,
        [103 ... 109] = &&l_err,
        ['n']         = &&l_null_n,
        [111 ... 115] = &&l_err,
        ['t']         = &&l_true_t,
        [117 ... 122] = &&l_err,
        ['{']         = &&l_obj_s,
        [124 ... 255] = &&l_err,
    };

    static void* go_num[] = {
        [0 ... 45]    = &&l_num_e,
        ['.']         = &&l_next,
        [47 ... 47]   = &&l_num_e,
        ['0' ... '9'] = &&l_next,
        [58 ... 255]  = &&l_num_e,
    };

    static void* go_str[] = {
        [0 ... 31]    = &&l_err,
        [32 ... 33]   = &&l_next,
        ['"']         = &&l_str_e,
        [35 ... 91]   = &&l_next,
        ['\\']        = &&l_esc,
        [93 ... 126]  = &&l_next,
        [127 ... 191] = &&l_err,
        [192 ... 223] = &&l_utf8_2,
        [224 ... 239] = &&l_utf8_3,
        [240 ... 247] = &&l_utf8_4,
        [248 ... 255] = &&l_err,
    };

    static void* go_esc[] = {
        [0 ... 33]    = &&l_err,
        ['"']         = &&l_next,
        [35 ... 46]   = &&l_err,
        ['/']         = &&l_next,
        [48 ... 91]   = &&l_err,
        ['\\']        = &&l_next,
        [93 ... 97]   = &&l_err,
        ['b']         = &&l_next,
        [99 ... 101]  = &&l_err,
        ['f']         = &&l_next,
        [103 ... 109] = &&l_err,
        ['n']         = &&l_next,
        [111 ... 113] = &&l_err,
        ['r']         = &&l_next,
        [115 ... 115] = &&l_err,
        ['t']         = &&l_next,
        [117 ... 255] = &&l_err,
    };

    static void* go_utf8[] = {
        ['\0']        = &&l_utf8_valid,
        [1 ... 127]   = &&l_err,
        [128 ... 191] = &&l_utf8,
        [192 ... 255] = &&l_err,
    };

    static void* go_null_n[] = {
        [0 ... 116]   = &&l_err,
        ['u']         = &&l_null_u,
        [118 ... 255] = &&l_err,
    };

    static void* go_null_u[] = {
        [0 ... 107]   = &&l_err,
        ['l']         = &&l_null_l,
        [109 ... 255] = &&l_err,
    };

    static void* go_null_l[] = {
        [0 ... 107]   = &&l_err,
        ['l']         = &&l_null_ll,
        [109 ... 255] = &&l_err,
    };

    static void* go_true_t[] = {
        [0 ... 113]   = &&l_err,
        ['r']         = &&l_true_r,
        [115 ... 255] = &&l_err,
    };

    static void* go_true_r[] = {
        [0 ... 116]   = &&l_err,
        ['u']         = &&l_true_u,
        [118 ... 255] = &&l_err,
    };

    static void* go_true_u[] = {
        [0 ... 100]   = &&l_err,
        ['e']         = &&l_true_e,
        [102 ... 255] = &&l_err,
    };

    static void* go_false_f[] = {
        [0 ... 96]    = &&l_err,
        ['a']         = &&l_false_a,
        [98 ... 255]  = &&l_err,
    };

    static void* go_false_a[] = {
        [0 ... 107]   = &&l_err,
        ['l']         = &&l_false_l,
        [109 ... 255] = &&l_err,
    };

    static void* go_false_l[] = {
        [0 ... 114]   = &&l_err,
        ['s']         = &&l_false_s,
        [116 ... 255] = &&l_err,
    };

    static void* go_false_s[] = {
        [0 ... 100]   = &&l_err,
        ['e']         = &&l_false_e,
        [102 ... 255] = &&l_err,
    };

    static void* go_arr[] = {
        [0 ... 8]     = &&l_err,
        ['\t']        = &&l_next,
        ['\n']        = &&l_next,
        [11 ... 12]   = &&l_err,
        ['\r']        = &&l_next,
        [14 ... 31]   = &&l_err,
        [' ']         = &&l_next,
        [33 ... 33]   = &&l_err,
        ['"']         = &&l_str_s,
        [35 ... 43]   = &&l_err,
        [',']         = &&l_next,
        ['-']         = &&l_num_s,
        [46 ... 47]   = &&l_err,
        ['0' ... '9'] = &&l_num_s,
        [58 ... 90]   = &&l_err,
        ['[']         = &&l_arr_s,
        [92 ... 92]   = &&l_err,
        [']']         = &&l_arr_e,
        [94 ... 101]  = &&l_err,
        ['f']         = &&l_false_f,
        [103 ... 109] = &&l_err,
        ['n']         = &&l_null_n,
        [111 ... 115] = &&l_err,
        ['t']         = &&l_true_t,
        [117 ... 122] = &&l_err,
        ['{']         = &&l_obj_s,
        [124 ... 255] = &&l_err,
    };

    static void* go_obj[] = {
        [0 ... 8]     = &&l_err,
        ['\t']        = &&l_next,
        ['\n']        = &&l_next,
        [11 ... 12]   = &&l_err,
        ['\r']        = &&l_next,
        [14 ... 31]   = &&l_err,
        [' ']         = &&l_next,
        [33 ... 33]   = &&l_err,
        ['"']         = &&l_kvp,
        [35 ... 43]   = &&l_err,
        [',']         = &&l_next,
        [45 ... 124]  = &&l_err,
        ['}']         = &&l_obj_e,
        [126 ... 255] = &&l_err,
    };

    static void* go_col[] = {
        [0 ... 8]     = &&l_err,
        ['\t']        = &&l_next,
        ['\n']        = &&l_next,
        [11 ... 12]   = &&l_err,
        ['\r']        = &&l_next,
        [14 ... 31]   = &&l_err,
        [' ']         = &&l_next,
        [33 ... 57]   = &&l_err,
        [':']         = &&l_col,
        [59 ... 255]  = &&l_err,
    };

    static void* go_obj_val[] = {
        [0 ... 8]     = &&l_err,
        ['\t']        = &&l_next,
        ['\n']        = &&l_next,
        [11 ... 12]   = &&l_err,
        ['\r']        = &&l_next,
        [14 ... 31]   = &&l_err,
        [' ']         = &&l_next,
        [33 ... 255]  = &&l_val,
    };

    jr_str_t  data = { .cstr = 0, .len = 0 };

    void**    go = go_doc;
    void**    go_stack[JREAD_CONFIG_MAX_DEPTH];
    int32_t   go_stack_idx = 0;
    int32_t   utf8_mask = 0;
    jr_type_t str_type = jr_type_string;

l_next:
    JR_DISPATCH_NEXT();

l_err:
    data.cstr = cstr - 1;
    data.len = 1;
    cb(jr_type_error, &data, user_data);
    return;

l_num_s:
    data.cstr = cstr - 1;
    JR_PUSH_GO(go_num);
    JR_DISPATCH_NEXT();

l_num_e:
    data.len = (int32_t)(cstr - 1 - data.cstr);
    cb(jr_type_number, &data, user_data);
    JR_POP_GO();
    JR_DISPATCH_THIS();

l_str_s:
    data.cstr = cstr;
    JR_PUSH_GO(go_str);
    str_type = jr_type_string;
    JR_DISPATCH_NEXT();

l_str_e:
    data.len = (int32_t)(cstr - 1 - data.cstr);
    cb(str_type, &data, user_data);
    JR_POP_GO();
    JR_DISPATCH_NEXT();

l_esc:
    JR_DISPATCH_NEXT_GO(go_esc);

l_utf8:
    utf8_mask >>= 8;
    JR_DISPATCH_NEXT_MASK();

l_utf8_2:
    utf8_mask = 0x000000FF;
    JR_DISPATCH_NEXT_GO(go_utf8);

l_utf8_3:
    utf8_mask = 0x0000FFFF;
    JR_DISPATCH_NEXT_GO(go_utf8);

l_utf8_4:
    utf8_mask = 0x00FFFFFF;
    JR_DISPATCH_NEXT_GO(go_utf8);

l_utf8_valid:
    JR_DISPATCH_THIS();

l_null_n:
    JR_PUSH();
    JR_DISPATCH_NEXT_GO(go_null_n);

l_null_u:
    JR_DISPATCH_NEXT_GO(go_null_u);

l_null_l:
    JR_DISPATCH_NEXT_GO(go_null_l);

l_null_ll:
    cb(jr_type_null, 0, user_data);
    JR_POP_GO();
    JR_DISPATCH_NEXT();

l_true_t:
    JR_PUSH();
    JR_DISPATCH_NEXT_GO(go_true_t);

l_true_r:
    JR_DISPATCH_NEXT_GO(go_true_r);

l_true_u:
    JR_DISPATCH_NEXT_GO(go_true_u);

l_true_e:
    cb(jr_type_true, 0, user_data);
    JR_POP_GO();
    JR_DISPATCH_NEXT();

l_false_f:
    JR_PUSH();
    JR_DISPATCH_NEXT_GO(go_false_f);

l_false_a:
    JR_DISPATCH_NEXT_GO(go_false_a);

l_false_l:
    JR_DISPATCH_NEXT_GO(go_false_l);

l_false_s:
    JR_DISPATCH_NEXT_GO(go_false_s);

l_false_e:
    cb(jr_type_false, 0, user_data);
    JR_POP_GO();
    JR_DISPATCH_NEXT();

l_arr_s:
    cb(jr_type_array_start, 0, user_data);
    JR_PUSH_GO(go_arr);
    JR_DISPATCH_NEXT();

l_arr_e:
    cb(jr_type_array_end, 0, user_data);
    JR_POP_GO();
    JR_DISPATCH_NEXT();

l_obj_s:
    cb(jr_type_object_start, 0, user_data);
    JR_PUSH_GO(go_obj);
    JR_DISPATCH_NEXT();

l_obj_e:
    cb(jr_type_object_end, 0, user_data);
    JR_POP_GO();
    JR_DISPATCH_NEXT();

l_kvp:
    data.cstr = cstr;
    JR_PUSH_GO(go_obj_val);
    JR_PUSH_GO(go_col);
    JR_PUSH_GO(go_str);
    str_type = jr_type_key;
    JR_DISPATCH_NEXT();

l_val:
    JR_POP_GO();
    JR_DISPATCH_THIS_GO(go_val);

l_col:
    JR_POP_GO();
    JR_DISPATCH_NEXT();

l_done:
    return;
}
