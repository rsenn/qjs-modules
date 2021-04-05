#ifndef PREDICATE_H
#define PREDICATE_H

#include "vector.h"
#include "utils.h"

enum predicate_id {
  PREDICATE_NONE = -1,
  PREDICATE_TYPE,
  PREDICATE_CHARSET,
  PREDICATE_NOTNOT,
  PREDICATE_NOT,
  PREDICATE_OR,
  PREDICATE_AND,
  PREDICATE_XOR,
  PREDICATE_REGEXP,
  PREDICATE_INSTANCEOF,
  PREDICATE_PROTOTYPEIS,
  PREDICATE_EQUAL
};

typedef struct {
  int flags;
} TypePredicate;

typedef struct {
  const char* set;
  vector chars;
  size_t len;
} CharsetPredicate;

typedef struct {
  JSValue predicate;
} UnaryPredicate;

typedef struct {
  JSValueConst a, b;
} BinaryPredicate;

typedef struct {
  size_t npredicates;
  JSValue* predicates;
} BooleanPredicate;

typedef struct {
  char* expr;
  size_t exprlen;
  int flags;
  uint8_t* bytecode;
} RegExpPredicate;

typedef struct Predicate {
  enum predicate_id id;
  union {
    TypePredicate type;
    CharsetPredicate charset;
    UnaryPredicate unary;
    BinaryPredicate binary;
    BooleanPredicate boolean;
    RegExpPredicate regexp;
  };
} Predicate;

#define PREDICATE_INIT(id)                                                                                             \
  {                                                                                                                    \
    id, {                                                                                                              \
      { 0 }                                                                                                            \
    }                                                                                                                  \
  }
static const size_t CAPTURE_COUNT_MAX = 255;

int predicate_call(JSContext*, JSValue, int argc, JSValue* argv);
int predicate_eval(Predicate*, JSContext*, int argc, JSValue* argv);
void predicate_free_rt(Predicate*, JSRuntime*);
JSValue predicate_regexp_capture(uint8_t**, int, uint8_t* input, JSContext* ctx);
int predicate_regexp_flags2str(int, char*);
int predicate_regexp_str2flags(const char*);
int predicate_regexp_compile(Predicate* pred, JSContext* ctx);
void predicate_tostring(const Predicate*, JSContext*, DynBuf* dbuf);
JSValue predicate_values(const Predicate*, JSContext*);
Predicate* predicate_dup(const Predicate* pred, JSContext* ctx);

static inline void
predicate_free(Predicate* pred, JSContext* ctx) {
  predicate_free_rt(pred, JS_GetRuntime(ctx));
}

#define predicate_undefined() predicate_type(TYPE_UNDEFINED)
#define predicate_null() predicate_type(TYPE_NULL)
#define predicate_bool() predicate_type(TYPE_BOOL)
#define predicate_int() predicate_type(TYPE_INT)
#define predicate_object() predicate_type(TYPE_OBJECT)
#define predicate_string() predicate_type(TYPE_STRING)
#define predicate_symbol() predicate_type(TYPE_SYMBOL)
#define predicate_big_float() predicate_type(TYPE_BIG_FLOAT)
#define predicate_big_int() predicate_type(TYPE_BIG_INT)
#define predicate_big_decimal() predicate_type(TYPE_BIG_DECIMAL)
#define predicate_float64() predicate_type(TYPE_FLOAT64)
#define predicate_number() predicate_type(TYPE_NUMBER)
#define predicate_primitive() predicate_type(TYPE_PRIMITIVE)
#define predicate_all() predicate_type(TYPE_ALL)
#define predicate_function() predicate_type(TYPE_FUNCTION)
#define predicate_array() predicate_type(TYPE_ARRAY)

static inline Predicate
predicate_charset(const char* str, size_t len) {
  Predicate ret = PREDICATE_INIT(PREDICATE_CHARSET);
  ret.charset.set = str;
  ret.charset.len = len;
  memset(&ret.charset.chars, 0, sizeof(vector));
  return ret;
}

static inline Predicate
predicate_regexp(const char* regexp, size_t rlen, int flags) {
  Predicate ret = PREDICATE_INIT(PREDICATE_REGEXP);
  ret.regexp.bytecode = 0;
  ret.regexp.expr = regexp;
  ret.regexp.exprlen = rlen;
  ret.regexp.flags = flags;
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
predicate_or(size_t npredicates, JSValue* predicates) {
  size_t i;
  Predicate ret = PREDICATE_INIT(PREDICATE_OR);
  ret.boolean.npredicates = npredicates;
  ret.boolean.predicates = predicates;
  return ret;
}

static inline Predicate
predicate_and(size_t npredicates, JSValue* predicates) {
  size_t i;
  Predicate ret = PREDICATE_INIT(PREDICATE_AND);
  ret.boolean.npredicates = npredicates;
  ret.boolean.predicates = predicates;
  return ret;
}

static inline Predicate
predicate_xor(size_t npredicates, JSValue* predicates) {
  size_t i;
  Predicate ret = PREDICATE_INIT(PREDICATE_XOR);
  ret.boolean.npredicates = npredicates;
  ret.boolean.predicates = predicates;
  return ret;
}

static inline Predicate
predicate_equal(JSValue value) {
  Predicate ret = PREDICATE_INIT(PREDICATE_EQUAL);
  ret.unary.predicate = value;
  return ret;
}

static inline Predicate
predicate_notnot(JSValue value) {
  Predicate ret = PREDICATE_INIT(PREDICATE_NOTNOT);
  ret.unary.predicate = value;
  return ret;
}

static inline Predicate
predicate_not(JSValue value) {
  Predicate ret = PREDICATE_INIT(PREDICATE_NOT);
  ret.unary.predicate = value;
  return ret;
}

#endif /* defined(PREDICATE_H) */
