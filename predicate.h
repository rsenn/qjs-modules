#ifndef PREDICATE_H
#define PREDICATE_H

#include "vector.h"
#include "utils.h"

enum predicate_id {
  PREDICATE_NONE = -1,
  PREDICATE_TYPE,
  PREDICATE_CHARSET,
  PREDICATE_NOT,
  PREDICATE_OR,
  PREDICATE_AND,
  PREDICATE_XOR
};

typedef struct {
  int32_t flags;
} TypePredicate;

typedef struct {
  const char* set;
  size_t len;
} CharsetPredicate;

typedef struct {
  JSValueConst fn;
} UnaryPredicate;

typedef struct {
  JSValueConst a, b;
} BinaryPredicate;

typedef struct Predicate {
  enum predicate_id id;
  union {
    TypePredicate type;
    CharsetPredicate charset;
    UnaryPredicate unary;
    BinaryPredicate binary;
  };
} Predicate;

int32_t predicate_eval(const Predicate*, JSContext*, int argc, JSValueConst* argv);
void predicate_tostring(const Predicate*, JSContext*, DynBuf*);

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
predicate_type(int32_t type) {
  Predicate ret = {PREDICATE_TYPE};
  ret.type.flags = type;
  return ret;
}
static inline Predicate
predicate_or(JSValueConst a, JSValueConst b) {
  Predicate ret = {PREDICATE_OR};
  ret.binary.a = a;
  ret.binary.b = b;
  return ret;
}

static inline Predicate
predicate_and(JSValueConst a, JSValueConst b) {
  Predicate ret = {PREDICATE_AND};
  ret.binary.a = a;
  ret.binary.b = b;
  return ret;
}

static inline Predicate
predicate_charset(const char* str, size_t len) {
  Predicate ret = {PREDICATE_CHARSET};
  ret.charset.set = str;
  ret.charset.len = len;
  return ret;
}

static inline Predicate
predicate_not(JSValueConst fn) {
  Predicate ret = {PREDICATE_NOT};
  ret.unary.fn = fn;
  return ret;
}
static inline Predicate
predicate_xor(JSValueConst a, JSValueConst b) {
  Predicate ret = {PREDICATE_XOR};
  ret.binary.a = a;
  ret.binary.b = b;
  return ret;
}

#endif /* defined(PREDICATE_H) */
