typedef enum JSErrorEnum {
  JS_EVAL_ERROR,
  JS_RANGE_ERROR,
  JS_REFERENCE_ERROR,
  JS_SYNTAX_ERROR,
  JS_TYPE_ERROR,
  JS_URI_ERROR,
  JS_INTERNAL_ERROR,
  JS_AGGREGATE_ERROR,

  JS_NATIVE_ERROR_COUNT,
} JSErrorEnum;

typedef struct JSShape JSShape;

typedef struct JSString JSAtomStruct;

typedef enum {
  JS_GC_PHASE_NONE,
  JS_GC_PHASE_DECREF,
  JS_GC_PHASE_REMOVE_CYCLES,
} JSGCPhaseEnum;

typedef enum OPCodeEnum OPCodeEnum;

typedef struct {
  JSValue (*to_string)(JSContext* ctx, JSValueConst val);
  JSValue (*from_string)(JSContext* ctx, const char* buf, int radix, int flags, slimb_t* pexponent);
  int (*unary_arith)(JSContext* ctx, JSValue* pres, OPCodeEnum op, JSValue op1);
  int (*binary_arith)(JSContext* ctx, OPCodeEnum op, JSValue* pres, JSValue op1, JSValue op2);
  int (*compare)(JSContext* ctx, OPCodeEnum op, JSValue op1, JSValue op2);

  JSValue (*mul_pow10_to_float64)(JSContext* ctx, const bf_t* a, int64_t exponent);
  int (*mul_pow10)(JSContext* ctx, JSValue* sp);
} JSNumericOperations;

struct JSRuntime {
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

  JSDebuggerInfo debugger_info;
};

struct JSClass {
  uint32_t class_id;
  JSAtom class_name;
  JSClassFinalizer* finalizer;
  JSClassGCMark* gc_mark;
  JSClassCall* call;

  const JSClassExoticMethods* exotic;
};

typedef struct JSStackFrame {
  struct JSStackFrame* prev_frame;
  JSValue cur_func;
  JSValue* arg_buf;
  JSValue* var_buf;
  struct list_head var_ref_list;
  const uint8_t* cur_pc;
  int arg_count;
  int js_mode;

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

      uint8_t is_detached : 1;
      uint8_t is_arg : 1;
      uint16_t var_idx;
    };
  };
  JSValue* pvalue;
  JSValue value;
} JSVarRef;

typedef struct JSFloatEnv {
  limb_t prec;
  bf_flags_t flags;
  unsigned int status;
} JSFloatEnv;

typedef struct JSBigFloat {
  JSRefCountHeader header;
  bf_t num;
} JSBigFloat;

typedef struct JSBigDecimal {
  JSRefCountHeader header;
  bfdec_t num;
} JSBigDecimal;

typedef enum {
  JS_AUTOINIT_ID_PROTOTYPE,
  JS_AUTOINIT_ID_MODULE_NS,
  JS_AUTOINIT_ID_PROP,
} JSAutoInitIDEnum;

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

  JSValue (*eval_internal)(JSContext* ctx, JSValueConst this_obj, const char* input, size_t input_len, const char* filename, int flags, int scope_idx);
  void* user_opaque;
};

typedef union JSFloat64Union {
  double d;
  uint64_t u64;
  uint32_t u32[2];
} JSFloat64Union;

typedef enum {
  JS_ATOM_KIND_STRING,
  JS_ATOM_KIND_SYMBOL,
  JS_ATOM_KIND_PRIVATE,
} JSAtomKindEnum;

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
  uint8_t var_kind : 4;

  uint16_t var_idx;
  JSAtom var_name;
} JSClosureVar;

typedef struct JSVarScope {
  int parent;
  int first;
} JSVarScope;

typedef enum {

  JS_VAR_NORMAL,
  JS_VAR_FUNCTION_DECL,
  JS_VAR_NEW_FUNCTION_DECL,
  JS_VAR_CATCH,
  JS_VAR_FUNCTION_NAME,
  JS_VAR_PRIVATE_FIELD,
  JS_VAR_PRIVATE_METHOD,
  JS_VAR_PRIVATE_GETTER,
  JS_VAR_PRIVATE_SETTER,
  JS_VAR_PRIVATE_GETTER_SETTER,
} JSVarKindEnum;

typedef struct JSVarDef {
  JSAtom var_name;

  int scope_level;

  int scope_next;
  uint8_t is_const : 1;
  uint8_t is_lexical : 1;
  uint8_t is_captured : 1;
  uint8_t var_kind : 4;

  int func_pool_idx : 24;
} JSVarDef;

typedef enum JSFunctionKindEnum {
  JS_FUNC_NORMAL = 0,
  JS_FUNC_GENERATOR = (1 << 0),
  JS_FUNC_ASYNC = (1 << 1),
  JS_FUNC_ASYNC_GENERATOR = (JS_FUNC_GENERATOR | JS_FUNC_ASYNC),
} JSFunctionKindEnum;

typedef struct JSFunctionBytecode {
  JSGCObjectHeader header;
  uint8_t js_mode;
  uint8_t has_prototype : 1;
  uint8_t has_simple_parameter_list : 1;
  uint8_t is_derived_class_constructor : 1;

  uint8_t need_home_object : 1;
  uint8_t func_kind : 2;
  uint8_t new_target_allowed : 1;
  uint8_t super_call_allowed : 1;
  uint8_t super_allowed : 1;
  uint8_t arguments_allowed : 1;
  uint8_t has_debug : 1;
  uint8_t backtrace_barrier : 1;
  uint8_t read_only_bytecode : 1;

  uint8_t* byte_code_buf;
  int byte_code_len;
  JSAtom func_name;
  JSVarDef* vardefs;
  JSClosureVar* closure_var;
  uint16_t arg_count;
  uint16_t var_count;
  uint16_t defined_arg_count;
  uint16_t stack_size;
  JSContext* realm;
  JSValue* cpool;
  int cpool_count;
  int closure_var_count;
  struct {

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

typedef struct JSRegExp {
  JSString* pattern;
  JSString* bytecode;
} JSRegExp;

typedef struct JSProxyData {
  JSValue target;
  JSValue handler;
  uint8_t is_func;
  uint8_t is_revoked;
} JSProxyData;

typedef struct JSArrayBuffer {
  int byte_length;
  uint8_t detached;
  uint8_t shared;
  uint8_t* data;
  struct list_head array_list;
  void* opaque;
  JSFreeArrayBufferDataFunc* free_func;
} JSArrayBuffer;

typedef struct JSTypedArray {
  struct list_head link;
  JSObject* obj;
  JSObject* buffer;
  uint32_t offset;
  uint32_t length;
} JSTypedArray;

typedef struct JSAsyncFunctionState {
  JSValue this_val;
  int argc;
  BOOL throw_flag;
  JSStackFrame frame;
} JSAsyncFunctionState;

typedef struct JSAsyncFunctionData {
  JSGCObjectHeader header;
  JSValue resolving_funcs[2];
  BOOL is_active;
  JSAsyncFunctionState func_state;
} JSAsyncFunctionData;

typedef enum {

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

  JS_OVOP_POS = JS_OVOP_BINARY_COUNT,
  JS_OVOP_NEG,
  JS_OVOP_INC,
  JS_OVOP_DEC,
  JS_OVOP_NOT,

  JS_OVOP_COUNT,
} JSOverloadableOperatorEnum;

typedef struct {
  uint32_t operator_index;
  JSObject* ops[JS_OVOP_BINARY_COUNT];
} JSBinaryOperatorDefEntry;

typedef struct {
  int count;
  JSBinaryOperatorDefEntry* tab;
} JSBinaryOperatorDef;

typedef struct {
  uint32_t operator_counter;
  BOOL is_primitive;

  JSObject* self_ops[JS_OVOP_COUNT];
  JSBinaryOperatorDef left;
  JSBinaryOperatorDef right;
} JSOperatorSetData;

typedef struct JSReqModuleEntry {
  JSAtom module_name;
  JSModuleDef* module;
} JSReqModuleEntry;

typedef enum JSExportTypeEnum {
  JS_EXPORT_TYPE_LOCAL,
  JS_EXPORT_TYPE_INDIRECT,
} JSExportTypeEnum;

typedef struct JSExportEntry {
  union {
    struct {
      int var_idx;
      JSVarRef* var_ref;
    } local;
    int req_module_idx;
  } u;
  JSExportTypeEnum export_type;
  JSAtom local_name;
  JSAtom export_name;
} JSExportEntry;

typedef struct JSStarExportEntry {
  int req_module_idx;
} JSStarExportEntry;

typedef struct JSImportEntry {
  int var_idx;
  JSAtom import_name;
  int req_module_idx;
} JSImportEntry;

struct JSModuleDef {
  JSRefCountHeader header;
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
  JSValue func_obj;
  JSModuleInitFunc* init_func;
  BOOL resolved : 8;
  BOOL func_created : 8;
  BOOL instantiated : 8;
  BOOL evaluated : 8;
  BOOL eval_mark : 8;

  BOOL eval_has_exception : 8;
  JSValue eval_exception;
  JSValue meta_obj;
};

typedef struct JSJobEntry {
  struct list_head link;
  JSContext* ctx;
  JSJobFunc* job_func;
  int argc;
  JSValue argv[0];
} JSJobEntry;

typedef struct JSProperty {
  union {
    JSValue value;
    struct {
      JSObject* getter;
      JSObject* setter;
    } getset;
    JSVarRef* var_ref;
    struct {

      uintptr_t realm_and_id;
      void* opaque;
    } init;
  } u;
} JSProperty;

typedef struct JSShapeProperty {
  uint32_t hash_next : 26;
  uint32_t flags : 6;
  JSAtom atom;
} JSShapeProperty;

struct JSShape {

  JSGCObjectHeader header;

  uint8_t is_hashed;

  uint8_t has_small_array_index;
  uint32_t hash;
  uint32_t prop_hash_mask;
  int prop_size;
  int prop_count;
  int deleted_prop_count;
  JSShape* shape_hash_next;
  JSObject* proto;
  JSShapeProperty prop[0];
};

struct JSObject {
  union {
    JSGCObjectHeader header;
    struct {
      int __gc_ref_count;
      uint8_t __gc_mark;

      uint8_t extensible : 1;
      uint8_t free_mark : 1;
      uint8_t is_exotic : 1;
      uint8_t fast_array : 1;
      uint8_t is_constructor : 1;
      uint8_t is_uncatchable_error : 1;
      uint8_t tmp_mark : 1;
      uint8_t is_HTMLDDA : 1;
      uint16_t class_id;
    };
  };

  JSShape* shape;
  JSProperty* prop;

  struct JSMapRecord* first_weak_ref;

  union {
    void* opaque;
    struct JSBoundFunction* bound_function;
    struct JSCFunctionDataRecord* c_function_data_record;
    struct JSCClosureRecord* c_closure_record;
    struct JSForInIterator* for_in_iterator;
    struct JSArrayBuffer* array_buffer;
    struct JSTypedArray* typed_array;
#ifdef CONFIG_BIGNUM
    struct JSFloatEnv* float_env;
    struct JSOperatorSetData* operator_set;
#endif
    struct JSMapState* map_state;
    struct JSMapIteratorData* map_iterator_data;
    struct JSArrayIteratorData* array_iterator_data;
    struct JSRegExpStringIteratorData* regexp_string_iterator_data;
    struct JSGeneratorData* generator_data;
    struct JSProxyData* proxy_data;
    struct JSPromiseData* promise_data;
    struct JSPromiseFunctionData* promise_function_data;
    struct JSAsyncFunctionData* async_function_data;
    struct JSAsyncFromSyncIteratorData* async_from_sync_iterator_data;
    struct JSAsyncGeneratorData* async_generator_data;
    struct {

      struct JSFunctionBytecode* function_bytecode;
      JSVarRef** var_refs;
      JSObject* home_object;
    } func;
    struct {
      JSContext* realm;
      JSCFunctionType c_function;
      uint8_t length;
      uint8_t cproto;
      int16_t magic;
    } cfunc;

    struct {
      union {
        uint32_t size;
        struct JSTypedArray* typed_array;
      } u1;
      union {
        JSValue* values;
        void* ptr;
        int8_t* int8_ptr;
        uint8_t* uint8_ptr;
        int16_t* int16_ptr;
        uint16_t* uint16_ptr;
        int32_t* int32_ptr;
        uint32_t* uint32_ptr;
        int64_t* int64_ptr;
        uint64_t* uint64_ptr;
        float* float_ptr;
        double* double_ptr;
      } u;
      uint32_t count;
    } array;
    JSRegExp regexp;
    JSValue object_data;
  } u;
};

typedef enum OPCodeFormat {

#define DEF(id, size, n_pop, n_push, f)
#include "quickjs-opcode.h"
#undef DEF
#undef FMT
} OPCodeFormat;

typedef enum JSStrictEqModeEnum {
  JS_EQ_STRICT,
  JS_EQ_SAME_VALUE,
  JS_EQ_SAME_VALUE_ZERO,
} JSStrictEqModeEnum;

typedef struct JSClassShortDef {
  JSAtom class_name;
  JSClassFinalizer* finalizer;
  JSClassGCMark* gc_mark;
} JSClassShortDef;

typedef enum JSFreeModuleEnum {
  JS_FREE_MODULE_ALL,
  JS_FREE_MODULE_NOT_RESOLVED,
  JS_FREE_MODULE_NOT_EVALUATED,
} JSFreeModuleEnum;

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

typedef struct JSCClosureRecord {
  JSCClosure* func;
  uint16_t length;
  uint16_t magic;
  void* opaque;
  void (*opaque_finalize)(void*);
} JSCClosureRecord;

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

typedef JSValue JSAutoInitFunc(JSContext* ctx, JSObject* p, JSAtom atom, void* opaque);

typedef enum JSToNumberHintEnum {
  TON_FLAG_NUMBER,
  TON_FLAG_NUMERIC,
} JSToNumberHintEnum;

typedef enum {
  OP_SPECIAL_OBJECT_ARGUMENTS,
  OP_SPECIAL_OBJECT_MAPPED_ARGUMENTS,
  OP_SPECIAL_OBJECT_THIS_FUNC,
  OP_SPECIAL_OBJECT_NEW_TARGET,
  OP_SPECIAL_OBJECT_HOME_OBJECT,
  OP_SPECIAL_OBJECT_VAR_OBJECT,
  OP_SPECIAL_OBJECT_IMPORT_META,
} OPSpecialObjectEnum;

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

  int completion_type;
  JSValue result;

  JSValue promise;
  JSValue resolving_funcs[2];
} JSAsyncGeneratorRequest;

typedef struct JSAsyncGeneratorData {
  JSObject* generator;
  JSAsyncGeneratorStateEnum state;
  JSAsyncFunctionState func_state;
  struct list_head queue;
} JSAsyncGeneratorData;

typedef struct BlockEnv {
  struct BlockEnv* prev;
  JSAtom label_name;
  int label_break;
  int label_cont;
  int drop_count;
  int label_finally;
  int scope_level;
  int has_iterator;
} BlockEnv;

typedef struct JSGlobalVar {
  int cpool_idx;
  uint8_t force_init : 1;
  uint8_t is_lexical : 1;
  uint8_t is_const : 1;
  int scope_level;
  JSAtom var_name;
} JSGlobalVar;

typedef struct RelocEntry {
  struct RelocEntry* next;
  uint32_t addr;
  int size;
} RelocEntry;

typedef struct JumpSlot {
  int op;
  int size;
  int pos;
  int label;
} JumpSlot;

typedef struct LabelSlot {
  int ref_count;
  int pos;
  int pos2;
  int addr;
  RelocEntry* first_reloc;
} LabelSlot;

typedef struct LineNumberSlot {
  uint32_t pc;
  int line_num;
} LineNumberSlot;

typedef enum JSParseFunctionEnum {
  JS_PARSE_FUNC_STATEMENT,
  JS_PARSE_FUNC_VAR,
  JS_PARSE_FUNC_EXPR,
  JS_PARSE_FUNC_ARROW,
  JS_PARSE_FUNC_GETTER,
  JS_PARSE_FUNC_SETTER,
  JS_PARSE_FUNC_METHOD,
  JS_PARSE_FUNC_CLASS_CONSTRUCTOR,
  JS_PARSE_FUNC_DERIVED_CLASS_CONSTRUCTOR,
} JSParseFunctionEnum;

typedef enum JSParseExportEnum {
  JS_PARSE_EXPORT_NONE,
  JS_PARSE_EXPORT_NAMED,
  JS_PARSE_EXPORT_DEFAULT,
} JSParseExportEnum;

typedef struct JSFunctionDef {
  JSContext* ctx;
  struct JSFunctionDef* parent;
  int parent_cpool_idx;
  int parent_scope_level;
  struct list_head child_list;
  struct list_head link;

  BOOL is_eval;
  int eval_type;
  BOOL is_global_var;
  BOOL is_func_expr;
  BOOL has_home_object;
  BOOL has_prototype;
  BOOL has_simple_parameter_list;
  BOOL has_parameter_expressions;
  BOOL has_use_strict;
  BOOL has_eval_call;
  BOOL has_arguments_binding;
  BOOL has_this_binding;
  BOOL new_target_allowed;
  BOOL super_call_allowed;
  BOOL super_allowed;
  BOOL arguments_allowed;
  BOOL is_derived_class_constructor;
  BOOL in_function_body;
  BOOL backtrace_barrier;
  JSFunctionKindEnum func_kind : 8;
  JSParseFunctionEnum func_type : 8;
  uint8_t js_mode;
  JSAtom func_name;

  JSVarDef* vars;
  int var_size;
  int var_count;
  JSVarDef* args;
  int arg_size;
  int arg_count;
  int defined_arg_count;
  int var_object_idx;
  int arg_var_object_idx;
  int arguments_var_idx;
  int arguments_arg_idx;
  int func_var_idx;
  int eval_ret_idx;
  int this_var_idx;
  int new_target_var_idx;
  int this_active_func_var_idx;
  int home_object_var_idx;
  BOOL need_home_object;

  int scope_level;
  int scope_first;
  int scope_size;
  int scope_count;
  JSVarScope* scopes;
  JSVarScope def_scope_array[4];
  int body_scope;

  int global_var_count;
  int global_var_size;
  JSGlobalVar* global_vars;

  DynBuf byte_code;
  int last_opcode_pos;
  int last_opcode_line_num;
  BOOL use_short_opcodes;

  LabelSlot* label_slots;
  int label_size;
  int label_count;
  BlockEnv* top_break;

  JSValue* cpool;
  int cpool_count;
  int cpool_size;

  int closure_var_count;
  int closure_var_size;
  JSClosureVar* closure_var;

  JumpSlot* jump_slots;
  int jump_size;
  int jump_count;

  LineNumberSlot* line_number_slots;
  int line_number_size;
  int line_number_count;
  int line_number_last;
  int line_number_last_pc;

  JSAtom filename;
  int line_num;
  DynBuf pc2line;

  char* source;
  int source_len;

  JSModuleDef* module;
} JSFunctionDef;

typedef struct JSToken {
  int val;
  int line_num;
  const uint8_t* ptr;
  union {
    struct {
      JSValue str;
      int sep;
    } str;
    struct {
      JSValue val;
#ifdef CONFIG_BIGNUM
      slimb_t exponent;
#endif
    } num;
    struct {
      JSAtom atom;
      BOOL has_escape;
      BOOL is_reserved;
    } ident;
    struct {
      JSValue body;
      JSValue flags;
    } regexp;
  } u;
} JSToken;

typedef struct JSParseState {
  JSContext* ctx;
  int last_line_num;
  int line_num;
  const char* filename;
  JSToken token;
  BOOL got_lf;
  const uint8_t* last_ptr;
  const uint8_t* buf_ptr;
  const uint8_t* buf_end;

  JSFunctionDef* cur_func;
  BOOL is_module;
  BOOL allow_html_comments;
  BOOL ext_json;
} JSParseState;

typedef struct JSOpCode {

  const char* name;
#endif
  uint8_t size;

  uint8_t n_pop;
  uint8_t n_push;
  uint8_t fmt;
} JSOpCode;

typedef enum {
  JS_VAR_DEF_WITH,
  JS_VAR_DEF_LET,
  JS_VAR_DEF_CONST,
  JS_VAR_DEF_FUNCTION_DECL,
  JS_VAR_DEF_NEW_FUNCTION_DECL,
  JS_VAR_DEF_CATCH,
  JS_VAR_DEF_VAR,
} JSVarDefEnum;

typedef struct JSParsePos {
  int last_line_num;
  int line_num;
  BOOL got_lf;
  const uint8_t* ptr;
} JSParsePos;

typedef struct {
  JSFunctionDef* fields_init_fd;
  int computed_fields_count;
  BOOL has_brand;
  int brand_push_pos;
} ClassFieldsDef;

typedef enum {
  PUT_LVALUE_NOKEEP,
  PUT_LVALUE_NOKEEP_DEPTH,
  PUT_LVALUE_KEEP_TOP,
  PUT_LVALUE_KEEP_SECOND,
  PUT_LVALUE_NOKEEP_BOTTOM,
} PutLValueEnum;

typedef enum FuncCallType {
  FUNC_CALL_NORMAL,
  FUNC_CALL_NEW,
  FUNC_CALL_SUPER_CTOR,
  FUNC_CALL_TEMPLATE,
} FuncCallType;

typedef struct JSResolveEntry {
  JSModuleDef* module;
  JSAtom name;
} JSResolveEntry;

typedef struct JSResolveState {
  JSResolveEntry* array;
  int size;
  int count;
} JSResolveState;

typedef enum JSResolveResultEnum {
  JS_RESOLVE_RES_EXCEPTION = -1,
  JS_RESOLVE_RES_FOUND = 0,
  JS_RESOLVE_RES_NOT_FOUND,
  JS_RESOLVE_RES_CIRCULAR,
  JS_RESOLVE_RES_AMBIGUOUS,
} JSResolveResultEnum;

typedef enum {
  EXPORTED_NAME_AMBIGUOUS,
  EXPORTED_NAME_NORMAL,
  EXPORTED_NAME_NS,
} ExportedNameEntryEnum;

typedef struct ExportedNameEntry {
  JSAtom export_name;
  ExportedNameEntryEnum export_type;
  union {
    JSExportEntry* me;
    JSVarRef* var_ref;
    JSModuleDef* module;
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
  const uint8_t* bc_buf;
  int bc_len;
  int pos;
  int line_num;
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
  uint32_t hash_next;
} JSObjectListEntry;

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

  JSObject** objects;
  int objects_count;
  int objects_size;

#ifdef DUMP_READ_OBJECT
  const uint8_t* ptr_last;
  int level;
#endif
} BCReaderState;

typedef struct ValueSlot {
  JSValue val;
  JSString* str;
  int64_t pos;
} ValueSlot;

struct array_sort_context {
  JSContext* ctx;
  int exception;
  int has_method;
  JSValueConst method;
};

typedef struct JSArrayIteratorData {
  JSValue obj;
  JSIteratorKindEnum kind;
  uint32_t idx;
} JSArrayIteratorData;

typedef struct JSRegExpStringIteratorData {
  JSValue iterating_regexp;
  JSValue iterated_string;
  BOOL global;
  BOOL unicode;
  BOOL done;
} JSRegExpStringIteratorData;

typedef struct ValueBuffer {
  JSContext* ctx;
  JSValue* arr;
  JSValue def[4];
  int len;
  int size;
  int error_status;
} ValueBuffer;

typedef struct JSONStringifyContext {
  JSValueConst replacer_func;
  JSValue stack;
  JSValue property_list;
  JSValue gap;
  JSValue empty;
  StringBuffer* b;
} JSONStringifyContext;

typedef struct JSMapRecord {
  int ref_count;
  BOOL empty;
  struct JSMapState* map;
  struct JSMapRecord* next_weak_ref;
  struct list_head link;
  struct list_head hash_link;
  JSValue key;
  JSValue value;
} JSMapRecord;

typedef struct JSMapState {
  BOOL is_weak;
  struct list_head records;
  uint32_t record_count;
  struct list_head* hash_table;
  uint32_t hash_size;
  uint32_t record_count_threshold;
} JSMapState;

typedef struct JSMapIteratorData {
  JSValue obj;
  JSIteratorKindEnum kind;
  JSMapRecord* cur_record;
} JSMapIteratorData;

typedef enum JSPromiseStateEnum {
  JS_PROMISE_PENDING,
  JS_PROMISE_FULFILLED,
  JS_PROMISE_REJECTED,
} JSPromiseStateEnum;

typedef struct JSPromiseData {
  JSPromiseStateEnum promise_state;

  struct list_head promise_reactions[2];
  BOOL is_handled;
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
  struct list_head link;
  JSValue resolving_funcs[2];
  JSValue handler;
} JSPromiseReactionData;

typedef struct JSAsyncFromSyncIteratorData {
  JSValue sync_iter;
  JSValue next_method;
} JSAsyncFromSyncIteratorData;

typedef struct {
  int64_t prec;
  bf_flags_t flags;
} BigDecimalEnv;

struct TA_sort_context {
  JSContext* ctx;
  int exception;
  JSValueConst arr;
  JSValueConst cmp;
  JSValue (*getfun)(JSContext* ctx, const void* a);
  uint8_t* array_ptr;
  int elt_size;
};

typedef enum AtomicsOpEnum {
  ATOMICS_OP_ADD,
  ATOMICS_OP_AND,
  ATOMICS_OP_OR,
  ATOMICS_OP_SUB,
  ATOMICS_OP_XOR,
  ATOMICS_OP_EXCHANGE,
  ATOMICS_OP_COMPARE_EXCHANGE,
  ATOMICS_OP_LOAD,
} AtomicsOpEnum;

typedef struct JSAtomicsWaiter {
  struct list_head link;
  BOOL linked;
  pthread_cond_t cond;
  int32_t* ptr;
} JSAtomicsWaiter;
