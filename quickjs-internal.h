#ifndef QJS_MODULES_INTERNAL_H
#define QJS_MODULES_INTERNAL_H

#include <list.h>
#include <cutils.h>
#include <quickjs.h>
#include <stddef.h>
#include <sys/types.h>

#include <quickjs-config.h>

#ifdef CONFIG_BIGNUM
#include <libbf.h>
#endif
#ifdef CONFIG_DEBUGGER
#include <quickjs-debugger.h>
#endif
#ifdef USE_WORKER
#include <pthread.h>
#endif

/**
 * \defgroup quickjs-internal QuickJS internal definitions
 * @{
 */
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
  uintptr_t stack_size;
  uintptr_t stack_top;
  uintptr_t stack_limit;
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
  struct JSStackFrame* prev_frame;
  JSValue cur_func;
  JSValue* arg_buf;              /* arguments */
  JSValue* var_buf;              /* variables */
  struct list_head var_ref_list; /* list of JSVarRef.link */
  const uint8_t* cur_pc;         /* bytecode functions : PC of the instruction after the call */
  int arg_count;
  int js_mode; /* 0 or JS_MODE_MATH for C functions */
  /* only used in generators. Current stack pointer value or NULL if the
   * function is running. */
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
    JSGCObjectHeader header;
    struct {
      int __gc_ref_count;
      uint8_t __gc_mark;
      /* 0 : the JSVarRef is on the stack. header.link is an element
         of JSStackFrame.var_ref_list.
         1 : the JSVarRef is detached. header.link has the normal meanning
      */
      uint8_t is_detached : 1;
      uint8_t is_arg : 1;
      uint16_t var_idx; /* index of the corresponding function variable on the
                           stack */
    };
  };
  JSValue* pvalue; /* pointer to the value, either on the stack or to 'value' */
  JSValue value;   /* used when the variable is no longer on the stack */
} JSVarRef;

typedef struct JSFloatEnv {
  uintptr_t prec;
  uint32_t flags;
  unsigned int status;
} JSFloatEnv;

#ifdef CONFIG_BIGNUM
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

/* must be large enough to have a negligible runtime cost and small enough to
 * call the interrupt callback often. */
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
  /*bf_context_t*/ void* bf_ctx; /* points to rt->bf_ctx, shared by all contexts */
#ifdef CONFIG_BIGNUM
  JSFloatEnv fp_env;   /* global FP environment */
  BOOL bignum_ext : 8; /* enable math mode */
  BOOL allow_operator_overloading : 8;
#endif
  /* when the counter reaches zero, JSRutime.interrupt_handler is called */
  int interrupt_counter;
  // BOOL is_error_property_enabled;

  struct list_head loaded_modules; /* list of JSModuleDef.link */

  /* if NULL, RegExp compilation is not supported */
  JSValue (*compile_regexp)(JSContext* ctx, JSValueConst pattern, JSValueConst flags);
  /* if NULL, eval is not supported */
  JSValue (*eval_internal)(JSContext* ctx, JSValueConst this_obj, const char* input, size_t input_len, const char* filename, int flags, int scope_idx);
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

typedef struct JSClosureVar {
  uint8_t is_local : 1;
  uint8_t is_arg : 1;
  uint8_t is_const : 1;
  uint8_t is_lexical : 1;
  uint8_t var_kind : 4; /* see JSVarKindEnum */
  /* 8 bits available */
  uint16_t var_idx; /* is_local = TRUE: index to a normal variable of the
                  parent function. otherwise: index to a closure
                  variable of the parent function */
  JSAtom var_name;
} JSClosureVar;

typedef struct JSVarScope {
  int parent; /* index into fd->scopes of the enclosing scope */
  int first;  /* index into fd->vars of the last variable in this scope */
} JSVarScope;

typedef enum {
  /* XXX: add more variable kinds here instead of using bit fields */
  JS_VAR_NORMAL,
  JS_VAR_FUNCTION_DECL,     /* lexical var with function declaration */
  JS_VAR_NEW_FUNCTION_DECL, /* lexical var with async/generator
                               function declaration */
  JS_VAR_CATCH,
  JS_VAR_FUNCTION_NAME, /* function expression name */
  JS_VAR_PRIVATE_FIELD,
  JS_VAR_PRIVATE_METHOD,
  JS_VAR_PRIVATE_GETTER,
  JS_VAR_PRIVATE_SETTER,        /* must come after JS_VAR_PRIVATE_GETTER */
  JS_VAR_PRIVATE_GETTER_SETTER, /* must come after JS_VAR_PRIVATE_SETTER */
} JSVarKindEnum;

/* XXX: could use a different structure in bytecode functions to save
   memory */
typedef struct JSVarDef {
  JSAtom var_name;
  /* index into fd->scopes of this variable lexical scope */
  int scope_level;
  /* during compilation:
      - if scope_level = 0: scope in which the variable is defined
      - if scope_level != 0: index into fd->vars of the next
        variable in the same or enclosing lexical scope
     in a bytecode function:
     index into fd->vars of the next
     variable in the same or enclosing lexical scope
  */
  int scope_next;
  uint8_t is_const : 1;
  uint8_t is_lexical : 1;
  uint8_t is_captured : 1;
  uint8_t var_kind : 4; /* see JSVarKindEnum */
  /* only used during compilation: function pool index for lexical
     variables with var_kind =
     JS_VAR_FUNCTION_DECL/JS_VAR_NEW_FUNCTION_DECL or scope level of
     the definition of the 'var' variables (they have scope_level =
     0) */
  int func_pool_idx : 24; /* only used during compilation : index in
                             the constant pool for hoisted function
                             definition */
} JSVarDef;

typedef enum JSFunctionKindEnum {
  JS_FUNC_NORMAL = 0,
  JS_FUNC_GENERATOR = (1 << 0),
  JS_FUNC_ASYNC = (1 << 1),
  JS_FUNC_ASYNC_GENERATOR = (JS_FUNC_GENERATOR | JS_FUNC_ASYNC),
} JSFunctionKindEnum;

typedef struct JSFunctionBytecode {
  JSGCObjectHeader header; /* must come first */
  uint8_t js_mode;
  uint8_t has_prototype : 1; /* true if a prototype field is necessary */
  uint8_t has_simple_parameter_list : 1;
  uint8_t is_derived_class_constructor : 1;
  /* true if home_object needs to be initialized */
  uint8_t need_home_object : 1;
  uint8_t func_kind : 2;
  uint8_t new_target_allowed : 1;
  uint8_t super_call_allowed : 1;
  uint8_t super_allowed : 1;
  uint8_t arguments_allowed : 1;
  uint8_t has_debug : 1;
  uint8_t backtrace_barrier : 1; /* stop backtrace on this function */
  uint8_t read_only_bytecode : 1;
  /* XXX: 4 bits available */
  uint8_t* byte_code_buf; /* (self pointer) */
  int byte_code_len;
  JSAtom func_name;
  JSVarDef* vardefs;         /* arguments + local variables (arg_count + var_count)
                                (self pointer) */
  JSClosureVar* closure_var; /* list of variables in the closure (self pointer) */
  uint16_t arg_count;
  uint16_t var_count;
  uint16_t defined_arg_count; /* for length function property */
  uint16_t stack_size;        /* maximum stack size */
  JSContext* realm;           /* function realm */
  JSValue* cpool;             /* constant pool (self pointer) */
  int cpool_count;
  int closure_var_count;
  struct {
    /* debug info, move to separate structure to save memory? */
    JSAtom filename;
    int line_num;
    int source_len;
    int pc2line_len;
    uint8_t* pc2line_buf;
    char* source;
  } debug;
#ifdef CONFIG_DEBUGGER
  struct JSDebuggerFunctionInfo debugger;
#endif
} JSFunctionBytecode;

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

#define JS_PROP_INITIAL_SIZE 2
#define JS_PROP_INITIAL_HASH_SIZE 4 /* must be a power of two */
#define JS_ARRAY_INITIAL_SIZE 2

typedef struct JSShapeProperty {
  uint32_t hash_next : 26; /* 0 if last in list */
  uint32_t flags : 6;      /* JS_PROP_XXX */
  JSAtom atom;             /* JS_ATOM_NULL = free property entry */
} JSShapeProperty;

struct JSShape {
  /* hash table of size hash_mask + 1 before the start of the
     structure (see prop_hash_end()). */
  JSGCObjectHeader header;
  /* true if the shape is inserted in the shape hash table. If not,
     JSShape.hash is not valid */
  uint8_t is_hashed;
  /* If true, the shape may have small array index properties 'n' with 0
     <= n <= 2^31-1. If false, the shape is guaranteed not to have
     small array index properties */
  uint8_t has_small_array_index;
  uint32_t hash; /* current hash value */
  uint32_t prop_hash_mask;
  int prop_size;  /* allocated properties */
  int prop_count; /* include deleted properties */
  int deleted_prop_count;
  JSShape* shape_hash_next; /* in JSRuntime.shape_hash[h] list */
  JSObject* proto;
  JSShapeProperty prop[0]; /* prop_size elements */
};

typedef struct JSRegExp {
  JSString* pattern;
  JSString* bytecode; /* also contains the flags */
} JSRegExp;

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

struct JSObject {
  union {
    JSGCObjectHeader header;
    struct {
      int __gc_ref_count; /* corresponds to header.ref_count */
      uint8_t __gc_mark;  /* corresponds to header.mark/gc_obj_type */

      uint8_t extensible : 1;
      uint8_t free_mark : 1;            /* only used when freeing objects with cycles */
      uint8_t is_exotic : 1;            /* TRUE if object has exotic property handlers */
      uint8_t fast_array : 1;           /* TRUE if u.array is used for get/put (for
                                           JS_CLASS_ARRAY, JS_CLASS_ARGUMENTS and typed
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
    struct JSMapState* map_state;                                      /* JS_CLASS_MAP..JS_CLASS_WEAKSET */
    struct JSMapIteratorData* map_iterator_data;                       /* JS_CLASS_MAP_ITERATOR, JS_CLASS_SET_ITERATOR */
    struct JSArrayIteratorData* array_iterator_data;                   /* JS_CLASS_ARRAY_ITERATOR,
                                                                          JS_CLASS_STRING_ITERATOR */
    struct JSRegExpStringIteratorData* regexp_string_iterator_data;    /* JS_CLASS_REGEXP_STRING_ITERATOR */
    struct JSGeneratorData* generator_data;                            /* JS_CLASS_GENERATOR */
    struct JSProxyData* proxy_data;                                    /* JS_CLASS_PROXY */
    struct JSPromiseData* promise_data;                                /* JS_CLASS_PROMISE */
    struct JSPromiseFunctionData* promise_function_data;               /* JS_CLASS_PROMISE_RESOLVE_FUNCTION,
                                                                          JS_CLASS_PROMISE_REJECT_FUNCTION */
    struct JSAsyncFunctionData* async_function_data;                   /* JS_CLASS_ASYNC_FUNCTION_RESOLVE,
                                                                          JS_CLASS_ASYNC_FUNCTION_REJECT */
    struct JSAsyncFromSyncIteratorData* async_from_sync_iterator_data; /* JS_CLASS_ASYNC_FROM_SYNC_ITERATOR */
    struct JSAsyncGeneratorData* async_generator_data;                 /* JS_CLASS_ASYNC_GENERATOR */
    struct {                                                           /* JS_CLASS_BYTECODE_FUNCTION: 12/24 bytes */
      /* also used by JS_CLASS_GENERATOR_FUNCTION, JS_CLASS_ASYNC_FUNCTION and
       * JS_CLASS_ASYNC_GENERATOR_FUNCTION */
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
    struct { /* JS_CLASS_ARRAY, JS_CLASS_ARGUMENTS,
                JS_CLASS_UINT8C_ARRAY..JS_CLASS_FLOAT64_ARRAY */
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

#ifndef SHORT_OPCODES
#define SHORT_OPCODES 1
#endif

typedef enum OPCodeFormat {
#define FMT(f) OP_FMT_##f,
#define DEF(id, size, n_pop, n_push, f)
#include <quickjs-opcode.h>
#undef DEF
#undef FMT
} OPCodeFormat;

enum OPCodeEnum {
#define FMT(f)
#define DEF(id, size, n_pop, n_push, f) OP_##id,
#define def(id, size, n_pop, n_push, f)
#include <quickjs-opcode.h>
#undef def
#undef DEF
#undef FMT
  OP_COUNT, /* excluding temporary opcodes */
  /* temporary opcodes : overlap with the short opcodes */
  OP_TEMP_START = OP_nop + 1,
  OP___dummy = OP_TEMP_START - 1,
#define FMT(f)
#define DEF(id, size, n_pop, n_push, f)
#define def(id, size, n_pop, n_push, f) OP_##id,
#include <quickjs-opcode.h>
#undef def
#undef DEF
#undef FMT
  OP_TEMP_END,
};
/* JSAtom support */

#define JS_ATOM_TAG_INT (1U << 31)
#define JS_ATOM_MAX_INT (JS_ATOM_TAG_INT - 1)
#define JS_ATOM_MAX ((1U << 30) - 1)

typedef struct StringBuffer {
  JSContext* ctx;
  JSString* str;
  int len;
  int size;
  int is_wide_char;
  int error_status;
} StringBuffer;

typedef struct JSCFunctionDataRecord {
  JSCFunctionData* func;
  uint8_t length;
  uint8_t data_len;
  uint16_t magic;
  JSValue data[0];
} JSCFunctionDataRecord;

/*typedef struct JSCClosureRecord {
  JSCClosure* func;
  uint16_t length;
  uint16_t magic;
  void* opaque;
  void (*opaque_finalize)(void*);
} JSCClosureRecord;*/

typedef struct JSMemoryUsage_helper {
  double memory_used_count;
  double str_count;
  double str_size;
  int64_t js_func_count;
  double js_func_size;
  int64_t js_func_code_size;
  int64_t js_func_pc2line_count;
  int64_t js_func_pc2line_size;
} JSMemoryUsage_helper;

typedef enum JSGeneratorStateEnum {
  JS_GENERATOR_STATE_SUSPENDED_START,
  JS_GENERATOR_STATE_SUSPENDED_YIELD,
  JS_GENERATOR_STATE_SUSPENDED_YIELD_STAR,
  JS_GENERATOR_STATE_EXECUTING,
  JS_GENERATOR_STATE_COMPLETED,
} JSGeneratorStateEnum;

typedef struct JSGeneratorData {
  JSGeneratorStateEnum state;
  JSAsyncFunctionState func_state;
} JSGeneratorData;

typedef enum JSAsyncGeneratorStateEnum {
  JS_ASYNC_GENERATOR_STATE_SUSPENDED_START,
  JS_ASYNC_GENERATOR_STATE_SUSPENDED_YIELD,
  JS_ASYNC_GENERATOR_STATE_SUSPENDED_YIELD_STAR,
  JS_ASYNC_GENERATOR_STATE_EXECUTING,
  JS_ASYNC_GENERATOR_STATE_AWAITING_RETURN,
  JS_ASYNC_GENERATOR_STATE_COMPLETED,
} JSAsyncGeneratorStateEnum;

typedef struct JSAsyncGeneratorRequest {
  struct list_head link;
  /* completion */
  int completion_type; /* GEN_MAGIC_x */
  JSValue result;
  /* promise capability */
  JSValue promise;
  JSValue resolving_funcs[2];
} JSAsyncGeneratorRequest;

typedef struct JSAsyncGeneratorData {
  JSObject* generator; /* back pointer to the object (const) */
  JSAsyncGeneratorStateEnum state;
  JSAsyncFunctionState func_state;
  struct list_head queue; /* list of JSAsyncGeneratorRequest.link */
} JSAsyncGeneratorData;

typedef struct JSPromiseData {
  /*JSPromiseStateEnum*/ int promise_state;
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

  JSValue promise; /* for top-level-await */
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

typedef struct JSOpCode {
  uint8_t size; /* in bytes */
  /* the opcodes remove n_pop items from the top of the stack, then
     pushes n_push items */
  uint8_t n_pop;
  uint8_t n_push;
  uint8_t fmt;
  const char* name;
} JSOpCode;

#if SHORT_OPCODES
/* After the final compilation pass, short opcodes are used. Their
   opcodes overlap with the temporary opcodes which cannot appear in
   the final bytecode. Their description is after the temporary
   opcodes in opcode_info[]. */
#define short_opcode_info(op) opcode_info[(op) >= OP_TEMP_START ? (op) + (OP_TEMP_END - OP_TEMP_START) : (op)]
#else
#define short_opcode_info(op) opcode_info[op]
#endif

extern const JSOpCode js_opcodes[OP_COUNT + (OP_TEMP_END - OP_TEMP_START)];

typedef struct JSParsePos {
  int last_line_num;
  int line_num;
  BOOL got_lf;
  const uint8_t* ptr;
} JSParsePos;

typedef struct JSResolveEntry {
  JSModuleDef* module;
  JSAtom name;
} JSResolveEntry;

typedef struct JSResolveState {
  JSResolveEntry* array;
  int size;
  int count;
} JSResolveState;

typedef enum {
  EXPORTED_NAME_AMBIGUOUS,
  EXPORTED_NAME_NORMAL,
  EXPORTED_NAME_NS,
} ExportedNameEntryEnum;

typedef struct ExportedNameEntry {
  JSAtom export_name;
  ExportedNameEntryEnum export_type;
  union {
    JSExportEntry* me;   /* using when the list is built */
    JSVarRef* var_ref;   /* EXPORTED_NAME_NORMAL */
    JSModuleDef* module; /* for EXPORTED_NAME_NS */
  } u;
} ExportedNameEntry;

typedef struct GetExportNamesState {
  JSModuleDef** modules;
  int modules_size;
  int modules_count;

  ExportedNameEntry* exported_names;
  int exported_names_size;
  int exported_names_count;
} GetExportNamesState;

typedef struct CodeContext {
  const uint8_t* bc_buf; /* code buffer */
  int bc_len;            /* length of the code buffer */
  int pos;               /* position past the matched code pattern */
  int line_num;          /* last visited OP_line_num parameter or -1 */
  int op;
  int idx;
  int label;
  int val;
  JSAtom atom;
} CodeContext;

typedef struct StackSizeState {
  int bc_len;
  int stack_len_max;
  uint16_t* stack_level_tab;
  int* pc_stack;
  int pc_stack_len;
  int pc_stack_size;
} StackSizeState;

typedef struct {
  JSObject* obj;
  uint32_t hash_next; /* -1 if no next entry */
} JSObjectListEntry;

/* XXX: reuse it to optimize weak references */
typedef struct {
  JSObjectListEntry* object_tab;
  int object_count;
  int object_size;
  uint32_t* hash_table;
  uint32_t hash_size;
} JSObjectList;

typedef enum BCTagEnum {
  BC_TAG_NULL = 1,
  BC_TAG_UNDEFINED,
  BC_TAG_BOOL_FALSE,
  BC_TAG_BOOL_TRUE,
  BC_TAG_INT32,
  BC_TAG_FLOAT64,
  BC_TAG_STRING,
  BC_TAG_OBJECT,
  BC_TAG_ARRAY,
  BC_TAG_BIG_INT,
  BC_TAG_BIG_FLOAT,
  BC_TAG_BIG_DECIMAL,
  BC_TAG_TEMPLATE_OBJECT,
  BC_TAG_FUNCTION_BYTECODE,
  BC_TAG_MODULE,
  BC_TAG_TYPED_ARRAY,
  BC_TAG_ARRAY_BUFFER,
  BC_TAG_SHARED_ARRAY_BUFFER,
  BC_TAG_DATE,
  BC_TAG_OBJECT_VALUE,
  BC_TAG_OBJECT_REFERENCE,
} BCTagEnum;

#ifdef CONFIG_BIGNUM
#define BC_BASE_VERSION 2
#else
#define BC_BASE_VERSION 1
#endif
#define BC_BE_VERSION 0x40
#ifdef WORDS_BIGENDIAN
#define BC_VERSION (BC_BASE_VERSION | BC_BE_VERSION)
#else
#define BC_VERSION BC_BASE_VERSION
#endif

typedef struct BCWriterState {
  JSContext* ctx;
  DynBuf dbuf;
  BOOL byte_swap : 8;
  BOOL allow_bytecode : 8;
  BOOL allow_sab : 8;
  BOOL allow_reference : 8;
  uint32_t first_atom;
  uint32_t* atom_to_idx;
  int atom_to_idx_size;
  JSAtom* idx_to_atom;
  int idx_to_atom_count;
  int idx_to_atom_size;
  uint8_t** sab_tab;
  int sab_tab_len;
  int sab_tab_size;
  /* list of referenced objects (used if allow_reference = TRUE) */
  JSObjectList object_list;
} BCWriterState;

typedef struct BCReaderState {
  JSContext* ctx;
  const uint8_t *buf_start, *ptr, *buf_end;
  uint32_t first_atom;
  uint32_t idx_to_atom_count;
  JSAtom* idx_to_atom;
  int error_state;
  BOOL allow_sab : 8;
  BOOL allow_bytecode : 8;
  BOOL is_rom_data : 8;
  BOOL allow_reference : 8;
  /* object references */
  JSObject** objects;
  int objects_count;
  int objects_size;

#ifdef DUMP_READ_OBJECT
  const uint8_t* ptr_last;
  int level;
#endif
} BCReaderState;

typedef struct {
  struct list_head link;
  int fd;
  JSValue rw_func[2];
  ssize_t ipfd;
} JSOSRWHandler;

typedef struct {
  struct list_head link;
  int sig_num;
  JSValue func;
} JSOSSignalHandler;

typedef struct {
  struct list_head link;
  BOOL has_object;
  int64_t timeout;
  JSValue func;
} JSOSTimer;

typedef struct {
  struct list_head link;
  uint8_t* data;
  size_t data_len;
  /* list of SharedArrayBuffers, necessary to free the message */
  uint8_t** sab_tab;
  size_t sab_tab_len;
} JSWorkerMessage;

typedef struct {
  int ref_count;
#ifdef USE_WORKER
  pthread_mutex_t mutex;
#endif
  struct list_head msg_queue; /* list of JSWorkerMessage.link */
  int read_fd;
  int write_fd;
  ssize_t ipfd;
} JSWorkerMessagePipe;

typedef struct {
  struct list_head link;
  JSWorkerMessagePipe* recv_pipe;
  JSValue on_message_func;
} JSWorkerMessageHandler;

typedef struct JSThreadState {
  struct list_head os_rw_handlers;     /* list of JSOSRWHandler.link */
  struct list_head os_signal_handlers; /* list JSOSSignalHandler.link */
  struct list_head os_timers;          /* list of JSOSTimer.link */
  struct list_head port_list;          /* list of JSWorkerMessageHandler.link */
  int eval_script_recurse;             /* only used in the main thread */
  /* not used in the main thread */
  JSWorkerMessagePipe *recv_pipe, *send_pipe;
} JSThreadState;

typedef struct {
  int ref_count;
  uint64_t buf[0];
} JSSABHeader;

typedef struct {
  FILE* f;
  BOOL close_in_finalizer;
  BOOL is_popen;
} JSSTDFile;

BOOL JS_IsUncatchableError(JSContext*, JSValueConst);

/**
 * @}
 */

#endif /* defined(QJS_MODULES_INTERNAL_H) */
