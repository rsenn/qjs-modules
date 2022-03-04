#ifndef PREDICATE_H
#define PREDICATE_H

#include "vector.h"
#include "utils.h"
#include "buffer-utils.h"
#include <cutils.h>

/**
 * \defgroup predicate Predicate function object
 * @{
 */
enum predicate_id {
  // PREDICATE_NONE = -1,
  PREDICATE_TYPE = 0,
  PREDICATE_CHARSET,
  PREDICATE_STRING,
  PREDICATE_NOTNOT,
  PREDICATE_NOT,
  PREDICATE_BNOT,
  PREDICATE_SQRT,
  PREDICATE_ADD,
  PREDICATE_SUB,
  PREDICATE_MUL,
  PREDICATE_DIV,
  PREDICATE_MOD,
  PREDICATE_BOR,
  PREDICATE_BAND,
  PREDICATE_POW,
  PREDICATE_ATAN2,
  PREDICATE_OR,
  PREDICATE_AND,
  PREDICATE_XOR,
  PREDICATE_REGEXP,
  PREDICATE_INSTANCEOF,
  PREDICATE_PROTOTYPEIS,
  PREDICATE_EQUAL,
  PREDICATE_PROPERTY,
  PREDICATE_MEMBER,
  PREDICATE_SHIFT,
  PREDICATE_SLICE,
  PREDICATE_FUNCTION
};

typedef struct {
  int flags;
} TypePredicate;

typedef struct {
  char* set;
  size_t len;
  Vector chars;
} CharsetPredicate;

typedef struct {
  char* str;
  size_t len;
} StringPredicate;

typedef struct {
  JSValue predicate;
} UnaryPredicate;

typedef struct {
  JSValueConst left, right;
} BinaryPredicate;

typedef struct {
  size_t npredicates;
  JSValue* predicates;
} BooleanPredicate;

typedef struct {
  RegExp expr;
  uint8_t* bytecode;
} RegExpPredicate;

typedef struct {
  JSAtom atom;
  JSValue predicate;
} PropertyPredicate;

typedef struct {
  JSValue object;
} MemberPredicate;

typedef struct {
  int n;
  JSValue predicate;
} ShiftPredicate;

typedef struct {
  union {
    struct {
      int64_t start, end;
    };
    OffsetLength offset_length;
  };
} SlicePredicate;

typedef struct {
  JSValue func, this_val;
  int arity;
} FunctionPredicate;

typedef struct Predicate {
  enum predicate_id id;
  union {
    TypePredicate type;
    CharsetPredicate charset;
    StringPredicate string;
    UnaryPredicate unary;
    BinaryPredicate binary;
    BooleanPredicate boolean;
    RegExpPredicate regexp;
    PropertyPredicate property;
    MemberPredicate member;
    ShiftPredicate shift;
    SlicePredicate slice;
    FunctionPredicate function;
  };
} Predicate;

#define PREDICATE_INIT(id) \
  { \
    id, { \
      { 0 } \
    } \
  }
static const size_t CAPTURE_COUNT_MAX = 255;

BOOL predicate_is(JSValueConst);
BOOL predicate_callable(JSContext*, JSValueConst);
enum predicate_id predicate_id(JSValue);
JSValue predicate_eval(Predicate*, JSContext* ctx, JSArguments* args);
JSValue predicate_call(JSContext*, JSValue value, int argc, JSValue argv[]);
JSValue predicate_value(JSContext*, JSValue value, JSArguments* args);
const char* predicate_typename(const Predicate*);
void predicate_dump(const Predicate*, JSContext* ctx, DynBuf* dbuf);
char* predicate_tostring(const Predicate* pr, JSContext* ctx);
void predicate_tosource(const Predicate*, JSContext* ctx, DynBuf* dbuf, Arguments* args);
JSValue predicate_regexp_capture(uint8_t**, int capture_count, uint8_t* input, JSContext* ctx);
void predicate_free_rt(Predicate*, JSRuntime* rt);
JSValue predicate_values(const Predicate*, JSContext* ctx);
JSValue predicate_keys(const Predicate*, JSContext* ctx);
Predicate* predicate_clone(const Predicate*, JSContext* ctx);
int predicate_regexp_compile(Predicate*, JSContext* ctx);
int predicate_recursive_num_args(const Predicate*);
int predicate_direct_num_args(const Predicate*);
JSPrecedence predicate_precedence(const Predicate*);

static inline void
predicate_free(Predicate* pred, JSContext* ctx) {
  predicate_free_rt(pred, JS_GetRuntime(ctx));
}

#define predicate_is_undefined() predicate_type(TYPE_UNDEFINED)
#define predicate_is_null() predicate_type(TYPE_NULL)
#define predicate_is_bool() predicate_type(TYPE_BOOL)
#define predicate_is_int() predicate_type(TYPE_INT)
#define predicate_is_object() predicate_type(TYPE_OBJECT)
#define predicate_is_string() predicate_type(TYPE_STRING)
#define predicate_is_symbol() predicate_type(TYPE_SYMBOL)
#define predicate_is_big_float() predicate_type(TYPE_BIG_FLOAT)
#define predicate_is_big_int() predicate_type(TYPE_BIG_INT)
#define predicate_is_big_decimal() predicate_type(TYPE_BIG_DECIMAL)
#define predicate_is_float64() predicate_type(TYPE_FLOAT64)
#define predicate_is_number() predicate_type(TYPE_NUMBER)
#define predicate_is_primitive() predicate_type(TYPE_PRIMITIVE)
#define predicate_is_all() predicate_type(TYPE_ALL)
#define predicate_is_function() predicate_type(TYPE_FUNCTION)
#define predicate_is_array() predicate_type(TYPE_ARRAY)

static inline Predicate
predicate_charset(const char* str, size_t len) {
  Predicate ret = PREDICATE_INIT(PREDICATE_CHARSET);
  ret.charset.set = (char*)str;
  ret.charset.len = len;
  memset(&ret.charset.chars, 0, sizeof(Vector));
  return ret;
}

static inline Predicate
predicate_string(const char* str, size_t len) {
  Predicate ret = PREDICATE_INIT(PREDICATE_STRING);
  ret.string.str = (char*)str;
  ret.string.len = len;
  return ret;
}

static inline Predicate
predicate_regexp(char* source, size_t len, int flags) {
  Predicate ret = PREDICATE_INIT(PREDICATE_REGEXP);
  ret.regexp.bytecode = 0;
  ret.regexp.expr.source = source;
  ret.regexp.expr.len = len;
  ret.regexp.expr.flags = flags;
  return ret;
}

static inline Predicate
predicate_type(int type) {
  Predicate ret = PREDICATE_INIT(PREDICATE_TYPE);
  ret.type.flags = type;
  return ret;
}

static inline Predicate
predicate_instanceof(JSValue ctor) {
  Predicate ret = PREDICATE_INIT(PREDICATE_INSTANCEOF);
  ret.unary.predicate = ctor;
  return ret;
}

static inline Predicate
predicate_prototype(JSValue proto) {
  Predicate ret = PREDICATE_INIT(PREDICATE_PROTOTYPEIS);
  ret.unary.predicate = proto;
  return ret;
}

static inline Predicate
predicate_add(JSValue left, JSValue right) {
  Predicate ret = PREDICATE_INIT(PREDICATE_ADD);
  ret.binary.left = js_is_null_or_undefined(left) ? JS_UNDEFINED : left;
  ret.binary.right = js_is_null_or_undefined(right) ? JS_UNDEFINED : right;
  return ret;
}

static inline Predicate
predicate_sub(JSValue left, JSValue right) {
  Predicate ret = PREDICATE_INIT(PREDICATE_SUB);
  ret.binary.left = js_is_null_or_undefined(left) ? JS_UNDEFINED : left;
  ret.binary.right = js_is_null_or_undefined(right) ? JS_UNDEFINED : right;
  return ret;
}

static inline Predicate
predicate_mul(JSValue left, JSValue right) {
  Predicate ret = PREDICATE_INIT(PREDICATE_MUL);
  ret.binary.left = js_is_null_or_undefined(left) ? JS_UNDEFINED : left;
  ret.binary.right = js_is_null_or_undefined(right) ? JS_UNDEFINED : right;
  return ret;
}

static inline Predicate
predicate_div(JSValue left, JSValue right) {
  Predicate ret = PREDICATE_INIT(PREDICATE_DIV);
  ret.binary.left = js_is_null_or_undefined(left) ? JS_UNDEFINED : left;
  ret.binary.right = js_is_null_or_undefined(right) ? JS_UNDEFINED : right;
  return ret;
}

static inline Predicate
predicate_mod(JSValue left, JSValue right) {
  Predicate ret = PREDICATE_INIT(PREDICATE_MOD);
  ret.binary.left = js_is_null_or_undefined(left) ? JS_UNDEFINED : left;
  ret.binary.right = js_is_null_or_undefined(right) ? JS_UNDEFINED : right;
  return ret;
}

#define PREDICATE_BINARY(op, id) \
  static inline Predicate predicate_##op(JSValue left, JSValue right) { \
    Predicate ret = PREDICATE_INIT(PREDICATE_##id); \
    ret.binary.left = js_is_null_or_undefined(left) ? JS_UNDEFINED : left; \
    ret.binary.right = js_is_null_or_undefined(right) ? JS_UNDEFINED : right; \
    return ret; \
  }
#define PREDICATE_UNARY(op, id) \
  static inline Predicate predicate_##op(JSValue value) { \
    Predicate ret = PREDICATE_INIT(PREDICATE_##id); \
    ret.unary.predicate = value; \
    return ret; \
  }

PREDICATE_BINARY(bor, BOR)
PREDICATE_BINARY(band, BAND)
PREDICATE_BINARY(pow, POW)

static inline Predicate
predicate_or(size_t npredicates, JSValue* predicates) {
  Predicate ret = PREDICATE_INIT(PREDICATE_OR);
  ret.boolean.npredicates = npredicates;
  ret.boolean.predicates = predicates;
  return ret;
}

static inline Predicate
predicate_and(size_t npredicates, JSValue* predicates) {
  Predicate ret = PREDICATE_INIT(PREDICATE_AND);
  ret.boolean.npredicates = npredicates;
  ret.boolean.predicates = predicates;
  return ret;
}

static inline Predicate
predicate_xor(size_t npredicates, JSValue* predicates) {
  Predicate ret = PREDICATE_INIT(PREDICATE_XOR);
  ret.boolean.npredicates = npredicates;
  ret.boolean.predicates = predicates;
  return ret;
}

PREDICATE_UNARY(equal, EQUAL)
PREDICATE_UNARY(notnot, NOTNOT)
PREDICATE_UNARY(not, NOT)
PREDICATE_UNARY(bnot, BNOT)
PREDICATE_UNARY(sqrt, SQRT)

static inline Predicate
predicate_property(JSAtom prop, JSValue pred) {
  Predicate ret = PREDICATE_INIT(PREDICATE_PROPERTY);
  ret.property.atom = prop;
  ret.property.predicate = pred;
  return ret;
}

static inline Predicate
predicate_member(JSValue obj) {
  Predicate ret = PREDICATE_INIT(PREDICATE_MEMBER);
  ret.member.object = obj;
  return ret;
}

static inline Predicate
predicate_shift(int n, JSValue pred) {
  Predicate ret = PREDICATE_INIT(PREDICATE_SHIFT);
  ret.shift.n = n;
  ret.shift.predicate = pred;
  return ret;
}

static inline Predicate
predicate_slice(int64_t start, int64_t end) {
  Predicate ret = PREDICATE_INIT(PREDICATE_SLICE);
  ret.slice.start = start;
  ret.slice.end = end;
  return ret;
}

static inline Predicate
predicate_function(JSValue func, JSValue this_val, int arity) {
  Predicate ret = PREDICATE_INIT(PREDICATE_FUNCTION);
  ret.function.func = func;
  ret.function.this_val = this_val;
  ret.function.arity = arity;
  return ret;
}

/**
 * @}
 */
#endif /* defined(PREDICATE_H) */
