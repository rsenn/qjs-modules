#ifndef QJS_MODULES_INTERNAL_H
#define QJS_MODULES_INTERNAL_H

#include "../list.h"
#include "../cutils.h"
#include "../libbf.h"
#include "../quickjs.h"

#ifdef HAVE_QUICKJS_CONFIG_H
#include <quickjs-config.h>
#endif

enum JSClassIds {
  /* classid tag        */ /* union usage   | properties */
  JS_CLASS_OBJECT = 1,     /* must be first */
  JS_CLASS_ARRAY,          /* u.array       | length */
  JS_CLASS_ERROR,
  JS_CLASS_NUMBER,           /* u.object_data */
  JS_CLASS_STRING,           /* u.object_data */
  JS_CLASS_BOOLEAN,          /* u.object_data */
  JS_CLASS_SYMBOL,           /* u.object_data */
  JS_CLASS_ARGUMENTS,        /* u.array       | length */
  JS_CLASS_MAPPED_ARGUMENTS, /*               | length */
  JS_CLASS_DATE,             /* u.object_data */
  JS_CLASS_MODULE_NS,
  JS_CLASS_C_FUNCTION,          /* u.cfunc */
  JS_CLASS_BYTECODE_FUNCTION,   /* u.func */
  JS_CLASS_BOUND_FUNCTION,      /* u.bound_function */
  JS_CLASS_C_FUNCTION_DATA,     /* u.c_function_data_record */
  JS_CLASS_GENERATOR_FUNCTION,  /* u.func */
  JS_CLASS_FOR_IN_ITERATOR,     /* u.for_in_iterator */
  JS_CLASS_REGEXP,              /* u.regexp */
  JS_CLASS_ARRAY_BUFFER,        /* u.array_buffer */
  JS_CLASS_SHARED_ARRAY_BUFFER, /* u.array_buffer */
  JS_CLASS_UINT8C_ARRAY,        /* u.array (typed_array) */
  JS_CLASS_INT8_ARRAY,          /* u.array (typed_array) */
  JS_CLASS_UINT8_ARRAY,         /* u.array (typed_array) */
  JS_CLASS_INT16_ARRAY,         /* u.array (typed_array) */
  JS_CLASS_UINT16_ARRAY,        /* u.array (typed_array) */
  JS_CLASS_INT32_ARRAY,         /* u.array (typed_array) */
  JS_CLASS_UINT32_ARRAY,        /* u.array (typed_array) */
#ifdef CONFIG_BIGNUM
  JS_CLASS_BIG_INT64_ARRAY,  /* u.array (typed_array) */
  JS_CLASS_BIG_UINT64_ARRAY, /* u.array (typed_array) */
#endif
  JS_CLASS_FLOAT32_ARRAY, /* u.array (typed_array) */
  JS_CLASS_FLOAT64_ARRAY, /* u.array (typed_array) */
  JS_CLASS_DATAVIEW,      /* u.typed_array */
#ifdef CONFIG_BIGNUM
  JS_CLASS_BIG_INT,      /* u.object_data */
  JS_CLASS_BIG_FLOAT,    /* u.object_data */
  JS_CLASS_FLOAT_ENV,    /* u.float_env */
  JS_CLASS_BIG_DECIMAL,  /* u.object_data */
  JS_CLASS_OPERATOR_SET, /* u.operator_set */
#endif
  JS_CLASS_MAP,                      /* u.map_state */
  JS_CLASS_SET,                      /* u.map_state */
  JS_CLASS_WEAKMAP,                  /* u.map_state */
  JS_CLASS_WEAKSET,                  /* u.map_state */
  JS_CLASS_MAP_ITERATOR,             /* u.map_iterator_data */
  JS_CLASS_SET_ITERATOR,             /* u.map_iterator_data */
  JS_CLASS_ARRAY_ITERATOR,           /* u.array_iterator_data */
  JS_CLASS_STRING_ITERATOR,          /* u.array_iterator_data */
  JS_CLASS_REGEXP_STRING_ITERATOR,   /* u.regexp_string_iterator_data */
  JS_CLASS_GENERATOR,                /* u.generator_data */
  JS_CLASS_PROXY,                    /* u.proxy_data */
  JS_CLASS_PROMISE,                  /* u.promise_data */
  JS_CLASS_PROMISE_RESOLVE_FUNCTION, /* u.promise_function_data */
  JS_CLASS_PROMISE_REJECT_FUNCTION,  /* u.promise_function_data */
  JS_CLASS_ASYNC_FUNCTION,           /* u.func */
  JS_CLASS_ASYNC_FUNCTION_RESOLVE,   /* u.async_function_data */
  JS_CLASS_ASYNC_FUNCTION_REJECT,    /* u.async_function_data */
  JS_CLASS_ASYNC_FROM_SYNC_ITERATOR, /* u.async_from_sync_iterator_data */
  JS_CLASS_ASYNC_GENERATOR_FUNCTION, /* u.func */
  JS_CLASS_ASYNC_GENERATOR,          /* u.async_generator_data */

  JS_CLASS_INIT_COUNT, /* last entry for predefined classes */
};

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

typedef enum OPCodeEnum OPCodeEnum;

typedef struct JSString JSString;
typedef struct JSString JSAtomStruct;
typedef struct JSShape JSShape;

typedef enum {
  JS_GC_PHASE_NONE,
  JS_GC_PHASE_DECREF,
  JS_GC_PHASE_REMOVE_CYCLES,
} JSGCPhaseEnum;

#ifdef CONFIG_BIGNUM
/* function pointers are used for numeric operations so that it is
   possible to remove some numeric types */
typedef struct {
  JSValue (*to_string)(JSContext* ctx, JSValueConst val);
  JSValue (*from_string)(JSContext* ctx, const char* buf, int radix, int flags, slimb_t* pexponent);
  int (*unary_arith)(JSContext* ctx, JSValue* pres, OPCodeEnum op, JSValue op1);
  int (*binary_arith)(JSContext* ctx, OPCodeEnum op, JSValue* pres, JSValue op1, JSValue op2);
  int (*compare)(JSContext* ctx, OPCodeEnum op, JSValue op1, JSValue op2);
  /* only for bigfloat: */
  JSValue (*mul_pow10_to_float64)(JSContext* ctx, const bf_t* a, int64_t exponent);
  int (*mul_pow10)(JSContext* ctx, JSValue* sp);
} JSNumericOperations;
#endif

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

struct JSClass {
  uint32_t class_id; /* 0 means free entry */
  JSAtom class_name;
  JSClassFinalizer* finalizer;
  JSClassGCMark* gc_mark;
  JSClassCall* call;
  /* pointers for exotic behavior, can be NULL if none are present */
  const JSClassExoticMethods* exotic;
};

#define JS_MODE_STRICT (1 << 0)
#define JS_MODE_STRIP (1 << 1)
#define JS_MODE_MATH (1 << 2)

typedef struct JSStackFrame {
  struct JSStackFrame* prev_frame; /* NULL if first stack frame */
  JSValue cur_func;                /* current function, JS_UNDEFINED if the frame is detached */
  JSValue* arg_buf;                /* arguments */
  JSValue* var_buf;                /* variables */
  struct list_head var_ref_list;   /* list of JSVarRef.link */
  const uint8_t* cur_pc;           /* only used in bytecode functions : PC of the
                                instruction after the call */
  int arg_count;
  int js_mode; /* 0 or JS_MODE_MATH for C functions */
  /* only used in generators. Current stack pointer value. NULL if
     the function is running. */
  JSValue* cur_sp;
} JSStackFrame;

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

typedef struct JSVarRef {
  union {
    JSGCObjectHeader header; /* must come first */
    struct {
      int __gc_ref_count; /* corresponds to header.ref_count */
      uint8_t __gc_mark;  /* corresponds to header.mark/gc_obj_type */

      /* 0 : the JSVarRef is on the stack. header.link is an element
         of JSStackFrame.var_ref_list.
         1 : the JSVarRef is detached. header.link has the normal meanning
      */
      uint8_t is_detached : 1;
      uint8_t is_arg : 1;
      uint16_t var_idx; /* index of the corresponding function variable on
                           the stack */
    };
  };
  JSValue* pvalue; /* pointer to the value, either on the stack or
                      to 'value' */
  JSValue value;   /* used when the variable is no longer on the stack */
} JSVarRef;

#ifdef CONFIG_BIGNUM
typedef struct JSFloatEnv {
  limb_t prec;
  bf_flags_t flags;
  unsigned int status;
} JSFloatEnv;

/* the same structure is used for big integers and big floats. Big
   integers are never infinite or NaNs */
typedef struct JSBigFloat {
  JSRefCountHeader header; /* must come first, 32-bit */
  bf_t num;
} JSBigFloat;

typedef struct JSBigDecimal {
  JSRefCountHeader header; /* must come first, 32-bit */
  bfdec_t num;
} JSBigDecimal;
#endif

typedef enum {
  JS_AUTOINIT_ID_PROTOTYPE,
  JS_AUTOINIT_ID_MODULE_NS,
  JS_AUTOINIT_ID_PROP,
} JSAutoInitIDEnum;

/* must be large enough to have a negligible runtime cost and small
   enough to call the interrupt callback often. */
#define JS_INTERRUPT_COUNTER_INIT 10000

struct JSContext {
  JSGCObjectHeader header; /* must come first */
  JSRuntime* rt;
  struct list_head link;

  uint16_t binary_object_count;
  int binary_object_size;

  JSShape* array_shape; /* initial shape for Array objects */

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

  JSValue global_obj;     /* global object */
  JSValue global_var_obj; /* contains the global let/const definitions */

  uint64_t random_state;
#ifdef CONFIG_BIGNUM
  bf_context_t* bf_ctx; /* points to rt->bf_ctx, shared by all contexts */
  JSFloatEnv fp_env;    /* global FP environment */
  BOOL bignum_ext : 8;  /* enable math mode */
  BOOL allow_operator_overloading : 8;
#endif
  /* when the counter reaches zero, JSRutime.interrupt_handler is called */
  int interrupt_counter;
  BOOL is_error_property_enabled;

  struct list_head loaded_modules; /* list of JSModuleDef.link */

  /* if NULL, RegExp compilation is not supported */
  JSValue (*compile_regexp)(JSContext* ctx, JSValueConst pattern, JSValueConst flags);
  /* if NULL, eval is not supported */
  JSValue (*eval_internal)(JSContext* ctx,
                           JSValueConst this_obj,
                           const char* input,
                           size_t input_len,
                           const char* filename,
                           int flags,
                           int scope_idx);
  void* user_opaque;
};

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

typedef enum OPCodeEnum { OPCODEENUM_DUMMY } OPCodeEnum;

typedef struct JSProperty {
  union {
    JSValue value;      /* JS_PROP_NORMAL */
    struct {            /* JS_PROP_GETSET */
      JSObject* getter; /* NULL if undefined */
      JSObject* setter; /* NULL if undefined */
    } getset;
    JSVarRef* var_ref; /* JS_PROP_VARREF */
    struct {           /* JS_PROP_AUTOINIT */
      /* in order to use only 2 pointers, we compress the realm
         and the init function pointer */
      uintptr_t realm_and_id; /* realm and init_id (JS_AUTOINIT_ID_x)
                                 in the 2 low bits */
      void* opaque;
    } init;
  } u;
} JSProperty;

typedef struct JSRegExp {
  JSString* pattern;
  JSString* bytecode; /* also contains the flags */
} JSRegExp;

struct JSObject {
  union {
    JSGCObjectHeader header;
    struct {
      int __gc_ref_count; /* corresponds to header.ref_count */
      uint8_t __gc_mark;  /* corresponds to header.mark/gc_obj_type */

      uint8_t extensible : 1;
      uint8_t free_mark : 1;  /* only used when freeing objects with cycles */
      uint8_t is_exotic : 1;  /* TRUE if object has exotic property handlers */
      uint8_t fast_array : 1; /* TRUE if u.array is used for get/put (for JS_CLASS_ARRAY, JS_CLASS_ARGUMENTS and typed
                                 arrays) */
      uint8_t is_constructor : 1;       /* TRUE if object is a constructor function */
      uint8_t is_uncatchable_error : 1; /* if TRUE, error is not catchable */
      uint8_t tmp_mark : 1;             /* used in JS_WriteObjectRec() */
      uint8_t is_HTMLDDA : 1;           /* specific annex B IsHtmlDDA behavior */
      uint16_t class_id;                /* see JS_CLASS_x */
    };
  };
  /* byte offsets: 16/24 */
  JSShape* shape;   /* prototype and property names + flag */
  JSProperty* prop; /* array of properties */
  /* byte offsets: 24/40 */
  struct JSMapRecord* first_weak_ref; /* XXX: use a bit and an external hash table? */
  /* byte offsets: 28/48 */
  union {
    void* opaque;
    struct JSBoundFunction* bound_function;               /* JS_CLASS_BOUND_FUNCTION */
    struct JSCFunctionDataRecord* c_function_data_record; /* JS_CLASS_C_FUNCTION_DATA */
    struct JSForInIterator* for_in_iterator;              /* JS_CLASS_FOR_IN_ITERATOR */
    struct JSArrayBuffer* array_buffer;                   /* JS_CLASS_ARRAY_BUFFER, JS_CLASS_SHARED_ARRAY_BUFFER */
    struct JSTypedArray* typed_array;                     /* JS_CLASS_UINT8C_ARRAY..JS_CLASS_DATAVIEW */
#ifdef CONFIG_BIGNUM
    struct JSFloatEnv* float_env;           /* JS_CLASS_FLOAT_ENV */
    struct JSOperatorSetData* operator_set; /* JS_CLASS_OPERATOR_SET */
#endif
    struct JSMapState* map_state;                    /* JS_CLASS_MAP..JS_CLASS_WEAKSET */
    struct JSMapIteratorData* map_iterator_data;     /* JS_CLASS_MAP_ITERATOR, JS_CLASS_SET_ITERATOR */
    struct JSArrayIteratorData* array_iterator_data; /* JS_CLASS_ARRAY_ITERATOR, JS_CLASS_STRING_ITERATOR */
    struct JSRegExpStringIteratorData* regexp_string_iterator_data; /* JS_CLASS_REGEXP_STRING_ITERATOR */
    struct JSGeneratorData* generator_data;                         /* JS_CLASS_GENERATOR */
    struct JSProxyData* proxy_data;                                 /* JS_CLASS_PROXY */
    struct JSPromiseData* promise_data;                             /* JS_CLASS_PROMISE */
    struct JSPromiseFunctionData*
        promise_function_data; /* JS_CLASS_PROMISE_RESOLVE_FUNCTION, JS_CLASS_PROMISE_REJECT_FUNCTION */
    struct JSAsyncFunctionData*
        async_function_data; /* JS_CLASS_ASYNC_FUNCTION_RESOLVE, JS_CLASS_ASYNC_FUNCTION_REJECT */
    struct JSAsyncFromSyncIteratorData* async_from_sync_iterator_data; /* JS_CLASS_ASYNC_FROM_SYNC_ITERATOR */
    struct JSAsyncGeneratorData* async_generator_data;                 /* JS_CLASS_ASYNC_GENERATOR */
    struct {                                                           /* JS_CLASS_BYTECODE_FUNCTION: 12/24 bytes */
      /* also used by JS_CLASS_GENERATOR_FUNCTION, JS_CLASS_ASYNC_FUNCTION and JS_CLASS_ASYNC_GENERATOR_FUNCTION */
      struct JSFunctionBytecode* function_bytecode;
      JSVarRef** var_refs;
      JSObject* home_object; /* for 'super' access */
    } func;
    struct { /* JS_CLASS_C_FUNCTION: 12/20 bytes */
      JSContext* realm;
      JSCFunctionType c_function;
      uint8_t length;
      uint8_t cproto;
      int16_t magic;
    } cfunc;
    /* array part for fast arrays and typed arrays */
    struct { /* JS_CLASS_ARRAY, JS_CLASS_ARGUMENTS, JS_CLASS_UINT8C_ARRAY..JS_CLASS_FLOAT64_ARRAY */
      union {
        uint32_t size;                    /* JS_CLASS_ARRAY, JS_CLASS_ARGUMENTS */
        struct JSTypedArray* typed_array; /* JS_CLASS_UINT8C_ARRAY..JS_CLASS_FLOAT64_ARRAY */
      } u1;
      union {
        JSValue* values;      /* JS_CLASS_ARRAY, JS_CLASS_ARGUMENTS */
        void* ptr;            /* JS_CLASS_UINT8C_ARRAY..JS_CLASS_FLOAT64_ARRAY */
        int8_t* int8_ptr;     /* JS_CLASS_INT8_ARRAY */
        uint8_t* uint8_ptr;   /* JS_CLASS_UINT8_ARRAY, JS_CLASS_UINT8C_ARRAY */
        int16_t* int16_ptr;   /* JS_CLASS_INT16_ARRAY */
        uint16_t* uint16_ptr; /* JS_CLASS_UINT16_ARRAY */
        int32_t* int32_ptr;   /* JS_CLASS_INT32_ARRAY */
        uint32_t* uint32_ptr; /* JS_CLASS_UINT32_ARRAY */
        int64_t* int64_ptr;   /* JS_CLASS_INT64_ARRAY */
        uint64_t* uint64_ptr; /* JS_CLASS_UINT64_ARRAY */
        float* float_ptr;     /* JS_CLASS_FLOAT32_ARRAY */
        double* double_ptr;   /* JS_CLASS_FLOAT64_ARRAY */
      } u;
      uint32_t count;    /* <= 2^31-1. 0 for a detached typed array */
    } array;             /* 12/20 bytes */
    JSRegExp regexp;     /* JS_CLASS_REGEXP: 8/16 bytes */
    JSValue object_data; /* for JS_SetObjectData(): 8/16/16 bytes */
  } u;
  /* byte sizes: 40/48/72 */
};

typedef struct StringBuffer {
  JSContext* ctx;
  JSString* str;
  int len;
  int size;
  int is_wide_char;
  int error_status;
} StringBuffer;

typedef enum JSPromiseStateEnum {
  JS_PROMISE_PENDING,
  JS_PROMISE_FULFILLED,
  JS_PROMISE_REJECTED,
} JSPromiseStateEnum;

typedef struct JSPromiseData {
  JSPromiseStateEnum promise_state;
  /* 0=fulfill, 1=reject, list of JSPromiseReactionData.link */
  struct list_head promise_reactions[2];
  BOOL is_handled; /* Note: only useful to debug */
  JSValue promise_result;
} JSPromiseData;

typedef struct JSPromiseFunctionDataResolved {
  int ref_count;
  BOOL already_resolved;
} JSPromiseFunctionDataResolved;

typedef struct JSPromiseFunctionData {
  JSValue promise;
  JSPromiseFunctionDataResolved* presolved;
} JSPromiseFunctionData;

typedef struct JSPromiseReactionData {
  struct list_head link; /* not used in promise_reaction_job */
  JSValue resolving_funcs[2];
  JSValue handler;
} JSPromiseReactionData;

typedef struct JSBoundFunction {
  JSValue func_obj;
  JSValue this_val;
  int argc;
  JSValue argv[0];
} JSBoundFunction;

typedef enum JSIteratorKindEnum {
  JS_ITERATOR_KIND_KEY,
  JS_ITERATOR_KIND_VALUE,
  JS_ITERATOR_KIND_KEY_AND_VALUE,
} JSIteratorKindEnum;

typedef struct JSForInIterator {
  JSValue obj;
  BOOL is_array;
  uint32_t array_length;
  uint32_t idx;
} JSForInIterator;

typedef struct JSProxyData {
  JSValue target;
  JSValue handler;
  uint8_t is_func;
  uint8_t is_revoked;
} JSProxyData;

typedef struct JSArrayBuffer {
  int byte_length; /* 0 if detached */
  uint8_t detached;
  uint8_t shared; /* if shared, the array buffer cannot be detached */
  uint8_t* data;  /* NULL if detached */
  struct list_head array_list;
  void* opaque;
  JSFreeArrayBufferDataFunc* free_func;
} JSArrayBuffer;

typedef struct JSTypedArray {
  struct list_head link; /* link to arraybuffer */
  JSObject* obj;         /* back pointer to the TypedArray/DataView object */
  JSObject* buffer;      /* based array buffer */
  uint32_t offset;       /* offset in the array buffer */
  uint32_t length;       /* length in the array buffer */
} JSTypedArray;

typedef struct JSAsyncFunctionState {
  JSValue this_val; /* 'this' generator argument */
  int argc;         /* number of function arguments */
  BOOL throw_flag;  /* used to throw an exception in JS_CallInternal() */
  JSStackFrame frame;
} JSAsyncFunctionState;

/* XXX: could use an object instead to avoid the
   JS_TAG_ASYNC_FUNCTION tag for the GC */
typedef struct JSAsyncFunctionData {
  JSGCObjectHeader header; /* must come first */
  JSValue resolving_funcs[2];
  BOOL is_active; /* true if the async function state is valid */
  JSAsyncFunctionState func_state;
} JSAsyncFunctionData;

typedef enum {
  /* binary operators */
  JS_OVOP_ADD,
  JS_OVOP_SUB,
  JS_OVOP_MUL,
  JS_OVOP_DIV,
  JS_OVOP_MOD,
  JS_OVOP_POW,
  JS_OVOP_OR,
  JS_OVOP_AND,
  JS_OVOP_XOR,
  JS_OVOP_SHL,
  JS_OVOP_SAR,
  JS_OVOP_SHR,
  JS_OVOP_EQ,
  JS_OVOP_LESS,

  JS_OVOP_BINARY_COUNT,
  /* unary operators */
  JS_OVOP_POS = JS_OVOP_BINARY_COUNT,
  JS_OVOP_NEG,
  JS_OVOP_INC,
  JS_OVOP_DEC,
  JS_OVOP_NOT,

  JS_OVOP_COUNT,
} JSOverloadableOperatorEnum;

typedef struct {
  uint32_t operator_index;
  JSObject* ops[JS_OVOP_BINARY_COUNT]; /* self operators */
} JSBinaryOperatorDefEntry;

typedef struct {
  int count;
  JSBinaryOperatorDefEntry* tab;
} JSBinaryOperatorDef;

typedef struct {
  uint32_t operator_counter;
  BOOL is_primitive; /* OperatorSet for a primitive type */
  /* NULL if no operator is defined */
  JSObject* self_ops[JS_OVOP_COUNT]; /* self operators */
  JSBinaryOperatorDef left;
  JSBinaryOperatorDef right;
} JSOperatorSetData;

typedef struct JSReqModuleEntry {
  JSAtom module_name;
  JSModuleDef* module; /* used using resolution */
} JSReqModuleEntry;

typedef enum JSExportTypeEnum {
  JS_EXPORT_TYPE_LOCAL,
  JS_EXPORT_TYPE_INDIRECT,
} JSExportTypeEnum;

typedef struct JSExportEntry {
  union {
    struct {
      int var_idx;       /* closure variable index */
      JSVarRef* var_ref; /* if != NULL, reference to the variable */
    } local;             /* for local export */
    int req_module_idx;  /* module for indirect export */
  } u;
  JSExportTypeEnum export_type;
  JSAtom local_name;  /* '*' if export ns from. not used for local
                         export after compilation */
  JSAtom export_name; /* exported variable name */
} JSExportEntry;

typedef struct JSStarExportEntry {
  int req_module_idx; /* in req_module_entries */
} JSStarExportEntry;

typedef struct JSImportEntry {
  int var_idx; /* closure variable index */
  JSAtom import_name;
  int req_module_idx; /* in req_module_entries */
} JSImportEntry;

struct JSModuleDef {
  JSRefCountHeader header; /* must come first, 32-bit */
  JSAtom module_name;
  struct list_head link;

  JSReqModuleEntry* req_module_entries;
  int req_module_entries_count;
  int req_module_entries_size;

  JSExportEntry* export_entries;
  int export_entries_count;
  int export_entries_size;

  JSStarExportEntry* star_export_entries;
  int star_export_entries_count;
  int star_export_entries_size;

  JSImportEntry* import_entries;
  int import_entries_count;
  int import_entries_size;

  JSValue module_ns;
  JSValue func_obj;            /* only used for JS modules */
  JSModuleInitFunc* init_func; /* only used for C modules */
  BOOL resolved : 8;
  BOOL func_created : 8;
  BOOL instantiated : 8;
  BOOL evaluated : 8;
  BOOL eval_mark : 8; /* temporary use during js_evaluate_module() */
  /* true if evaluation yielded an exception. It is saved in
     eval_exception */
  BOOL eval_has_exception : 8;
  JSValue eval_exception;
  JSValue meta_obj; /* for import.meta */
};

typedef struct JSJobEntry {
  struct list_head link;
  JSContext* ctx;
  JSJobFunc* job_func;
  int argc;
  JSValue argv[0];
} JSJobEntry;

/* Set/Map/WeakSet/WeakMap */

typedef struct JSMapRecord {
  int ref_count; /* used during enumeration to avoid freeing the record */
  BOOL empty;    /* TRUE if the record is deleted */
  struct JSMapState* map;
  struct JSMapRecord* next_weak_ref;
  struct list_head link;
  struct list_head hash_link;
  JSValue key;
  JSValue value;
} JSMapRecord;

typedef struct JSMapState {
  BOOL is_weak;             /* TRUE if WeakSet/WeakMap */
  struct list_head records; /* list of JSMapRecord.link */
  uint32_t record_count;
  struct list_head* hash_table;
  uint32_t hash_size;              /* must be a power of two */
  uint32_t record_count_threshold; /* count at which a hash table resize is needed */
} JSMapState;

#define MAGIC_SET (1 << 0)
#define MAGIC_WEAK (1 << 1)

typedef struct JSMapIteratorData {
  JSValue obj;
  JSIteratorKindEnum kind;
  JSMapRecord* cur_record;
} JSMapIteratorData;

#endif /* defined(QJS_MODULES_INTERNAL_H) */
