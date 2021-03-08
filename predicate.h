#ifndef PREDICATE_H
#define PREDICATE_H

#include "vector.h"
#include "utils.h"

enum { PREDICATE_CHARSET, PREDICATE_NOT, PREDICATE_OR, PREDICATE_AND };

typedef struct {
  const char* set;
  size_t len;
} CharsetPredicate;

typedef struct {
  JSValueConst fn;
} NotPredicate;

typedef struct {
  JSValueConst a, b;
} OrPredicate;

typedef struct {
  JSValueConst a, b;
} AndPredicate;

typedef struct Predicate {
  int type;
  union {
    CharsetPredicate charset;
    NotPredicate not ;
    OrPredicate or ;
    AndPredicate and;
  };
} Predicate;

BOOL predicate_eval(const Predicate*, JSContext*, JSValue);

static inline Predicate
predicate_or(JSValueConst a, JSValueConst b) {
  Predicate ret = {PREDICATE_OR};
  ret.or.a = a;
  ret.or.b = b;
  return ret;
}

static inline Predicate
predicate_and(JSValueConst a, JSValueConst b) {
  Predicate ret = {PREDICATE_AND};
  ret.and.a = a;
  ret.and.b = b;
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
  ret.not .fn = fn;
  return ret;
}

#endif /* defined(PREDICATE_H) */