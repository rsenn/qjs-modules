#ifndef QUICKJS_LIST_H
#define QUICKJS_LIST_H

#include <list.h>
#include "utils.h"

/**
 * \defgroup quickjs-list QuickJS module: list - JS Object lists (deep keys)
 * @{
 */

typedef struct List {
  struct list_head head;
  size_t size;
  int ref_count;
} List;

extern VISIBLE JSClassID js_list_class_id, js_list_iterator_class_id;

static inline List*
js_list_data2(JSContext* ctx, JSValueConst value) {
  return JS_GetOpaque2(ctx, value, js_list_class_id);
}

static inline List*
js_list_data(JSValueConst value) {
  return JS_GetOpaque(value, js_list_class_id);
}

JSValue js_list_new(JSContext*, JSValueConst proto);
JSValue js_list_wrap(JSContext*, JSValueConst proto, List* list);
JSModuleDef* js_init_module_list(JSContext*, const char* module_name);

/**
 * @}
 */

#endif /* defined(QUICKJS_LIST_H) */
