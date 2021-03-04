#ifndef QJS_MODULES_INTERNAL_H
#define QJS_MODULES_INTERNAL_H

#include <libbf.h>
#include <quickjs.h>

#ifdef HAVE_QUICKJS_CONFIG_H
#include <quickjs-config.h>
#endif

typedef enum JSErrorEnum {
  JS_EVAL_ERROR,
  JS_RANGE_ERROR,
  JS_REFERENCE_ERROR,
  JS_SYNTAX_ERROR,
  JS_TYPE_ERROR,
  JS_URI_ERROR,
  JS_INTERNAL_ERROR,
  JS_AGGREGATE_ERROR,
  JS_NATIVE_ERROR_COUNT
} JSErrorEnum;

struct JSString {
  JSRefCountHeader header;
  uint32_t len : 31;
  uint8_t is_wide_char : 1;
  uint32_t hash : 30;
  uint8_t atom_type : 2;
  uint32_t hash_next;
#ifdef DUMP_LEAKS
  struct list_head link;
#endif
  union {
    uint8_t str8[0];
    uint16_t str16[0];
  } u;
};

typedef struct JSString JSAtomStruct;
typedef struct JSShape JSShape;

typedef enum {
  JS_GC_PHASE_NONE,
  JS_GC_PHASE_DECREF,
  JS_GC_PHASE_REMOVE_CYCLES,
} JSGCPhaseEnum;

typedef enum {
  JS_GC_OBJ_TYPE_JS_OBJECT,
  JS_GC_OBJ_TYPE_FUNCTION_BYTECODE,
  JS_GC_OBJ_TYPE_SHAPE,
  JS_GC_OBJ_TYPE_VAR_REF,
  JS_GC_OBJ_TYPE_ASYNC_FUNCTION,
  JS_GC_OBJ_TYPE_JS_CONTEXT,
} JSGCObjectTypeEnum;

struct JSGCObjectHeader {
  int ref_count;
  JSGCObjectTypeEnum gc_obj_type : 4;
  uint8_t mark : 4;
  uint8_t dummy1;
  uint16_t dummy2;
  struct list_head link;
};

typedef enum OPCodeEnum { OPCODEENUM_DUMMY } OPCodeEnum;

typedef struct JSFloatEnv {
  limb_t prec;
  bf_flags_t flags;
  unsigned int status;
} JSFloatEnv;

typedef struct {
  JSValue (*to_string)(JSContext* ctx, JSValueConst val);
  JSValue (*from_string)(JSContext* ctx, const char* buf, int radix, int flags, slimb_t* pexponent);
  int (*unary_arith)(JSContext* ctx, JSValue* pres, OPCodeEnum op, JSValue op1);
  int (*binary_arith)(JSContext* ctx, OPCodeEnum op, JSValue* pres, JSValue op1, JSValue op2);
  int (*compare)(JSContext* ctx, OPCodeEnum op, JSValue op1, JSValue op2);
  JSValue (*mul_pow10_to_float64)(JSContext* ctx, const bf_t* a, int64_t exponent);
  int (*mul_pow10)(JSContext* ctx, JSValue* sp);
} JSNumericOperations;

typedef struct JSRuntime {
  JSMallocFunctions mf;
  JSMallocState malloc_state;
  const char* rt_info;
  int atom_hash_size;
  int atom_count;
  int atom_size;
  int atom_count_resize;
  uint32_t* atom_hash;
  JSAtomStruct** atom_array;
  int atom_free_index;
  int class_count;
  JSClass* class_array;
  struct list_head context_list;
  struct list_head gc_obj_list;
  struct list_head gc_zero_ref_count_list;
  struct list_head tmp_obj_list;
  JSGCPhaseEnum gc_phase : 8;
  size_t malloc_gc_threshold;
#ifdef DUMP_LEAKS
  struct list_head string_list;
#endif
  const uint8_t* stack_top;
  size_t stack_size;
  JSValue current_exception;
  BOOL in_out_of_memory : 8;
  struct JSStackFrame* current_stack_frame;
  JSInterruptHandler* interrupt_handler;
  void* interrupt_opaque;
  JSHostPromiseRejectionTracker* host_promise_rejection_tracker;
  void* host_promise_rejection_tracker_opaque;
  struct list_head job_list;
  JSModuleNormalizeFunc* module_normalize_func;
  JSModuleLoaderFunc* module_loader_func;
  void* module_loader_opaque;
  BOOL can_block : 8;
  JSSharedArrayBufferFunctions sab_funcs;
  int shape_hash_bits;
  int shape_hash_size;
  int shape_hash_count;
  JSShape** shape_hash;
#ifdef CONFIG_BIGNUM
  bf_context_t bf_ctx;
  JSNumericOperations bigint_ops;
  JSNumericOperations bigfloat_ops;
  JSNumericOperations bigdecimal_ops;
  uint32_t operator_count;
#endif
  void* user_opaque;
} JSRuntime;

struct JSContext {
  JSGCObjectHeader header;
  JSRuntime* rt;
  struct list_head link;
  uint16_t binary_object_count;
  int binary_object_size;
  JSShape* array_shape;
  JSValue* class_proto;
  JSValue function_proto;
  JSValue function_ctor;
  JSValue array_ctor;
  JSValue regexp_ctor;
  JSValue promise_ctor;
  JSValue native_error_proto[JS_NATIVE_ERROR_COUNT];
  JSValue iterator_proto;
  JSValue async_iterator_proto;
  JSValue array_proto_values;
  JSValue throw_type_error;
  JSValue eval_obj;
  JSValue global_obj;
  JSValue global_var_obj;
  uint64_t random_state;
#ifdef CONFIG_BIGNUM
  bf_context_t* bf_ctx;
  JSFloatEnv fp_env;
  BOOL bignum_ext : 8;
  BOOL allow_operator_overloading : 8;
#endif
  int interrupt_counter;
  BOOL is_error_property_enabled;
  struct list_head loaded_modules;
  JSValue (*compile_regexp)(JSContext* ctx, JSValueConst pattern, JSValueConst flags);
  JSValue (*eval_internal)(JSContext* ctx,
                           JSValueConst this_obj,
                           const char* input,
                           size_t input_len,
                           const char* filename,
                           int flags,
                           int scope_idx);
  void* user_opaque;
};

#endif /* defined(QJS_MODULES_INTERNAL_H) */