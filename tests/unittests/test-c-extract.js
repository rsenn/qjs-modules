import { findFunctions, findTypes } from '../../utilities/c-extract.js';
import { assert, eq, tests } from '../../lib/tinytest.js';

const byName = (list, name) => list.find(d => d.name == name);
const layoutOf = d => d.fields.map(f => [f.name, f.offset, f.size]);

const FUNCS = `#include <stdio.h>

/* adds */
static int
add(int a, int b) {
  if(a) {
    return a + b;
  }
  return b;
}

void noop(void) {
}

int main(int argc, char* argv[]) {
  return 0;
}
`;

tests({
  'findFunctions() finds every function by name'() {
    const funcs = findFunctions(FUNCS, 'f.c');
    eq(funcs.map(f => f.name).join(','), 'add,noop,main');
  },
  'findFunctions() ends a body at a column-1 brace, not a nested one'() {
    const [add] = findFunctions(FUNCS, 'f.c');
    const text = FUNCS.slice(add.start, add.end);
    assert(text.endsWith('return b;\n}'), text);
    assert(text.includes('return a + b;'));
  },
  'findFunctions() includes the leading comment and return type'() {
    const [add] = findFunctions(FUNCS, 'f.c');
    assert(FUNCS.slice(add.start, add.end).startsWith('/* adds */\nstatic int\nadd('));
  },
  'findFunctions() reports the line of the name'() {
    eq(findFunctions(FUNCS, 'f.c')[1].line, 12);
  },
  'findFunctions() ignores prototypes and struct initializers'() {
    eq(findFunctions('int f(int);\nstruct s v = { 1 };\n', 'f.c').length, 0);
  },
  'findTypes() lays out a plain struct'() {
    const t = byName(findTypes('struct p { char c; int i; double d; };', 't.c'), 'p');
    eq(t.type, 'struct');
    eq(t.size, 16);
    eq(t.align, 8);
    eq(JSON.stringify(layoutOf(t)), JSON.stringify([['c', 0, 1], ['i', 4, 4], ['d', 8, 8]]));
  },
  'findTypes() splits comma-separated declarators'() {
    const t = byName(findTypes('struct p { int x, y; };', 't.c'), 'p');
    eq(JSON.stringify(layoutOf(t)), JSON.stringify([['x', 0, 4], ['y', 4, 4]]));
  },
  'findTypes() sizes pointers, arrays and function pointers'() {
    const t = byName(findTypes('struct s { char n[10]; char* p; int (*cb)(void*); };', 't.c'), 's');
    eq(JSON.stringify(layoutOf(t)), JSON.stringify([['n', 0, 10], ['p', 16, 8], ['cb', 24, 8]]));
    eq(t.fields[0].type, 'char[10]');
    eq(t.fields[1].type, 'char *');
  },
  'findTypes() lays out unions at offset 0'() {
    const u = byName(findTypes('union u { char c; double d; };', 't.c'), 'u');
    eq(u.size, 8);
    eq(JSON.stringify(layoutOf(u)), JSON.stringify([['c', 0, 1], ['d', 0, 8]]));
  },
  'findTypes() lays out anonymous nested unions'() {
    const t = byName(findTypes('struct s { char a; union { int i; float f; } u; };', 't.c'), 's');
    eq(t.size, 8);
    eq(t.fields[1].offset, 4);
    eq(t.fields[1].fields.length, 2);
  },
  'findTypes() resolves typedefs and earlier structs'() {
    const list = findTypes('typedef unsigned int u32;\nstruct p { u32 a; };\nstruct q { char c; struct p p; };', 't.c');
    eq(byName(list, 'u32').type, 'typedef');
    eq(byName(list, 'u32').size, 4);
    eq(byName(list, 'q').size, 8);
    eq(byName(list, 'q').fields[1].offset, 4);
  },
  'findTypes() names anonymous typedef structs and emits pointer typedefs'() {
    const list = findTypes('typedef struct { int a; } rec_t, *rec_p;', 't.c');
    eq(byName(list, 'rec_t').type, 'struct');
    eq(byName(list, 'rec_p').type, 'typedef');
    eq(byName(list, 'rec_p').size, 8);
    eq(byName(list, 'rec_p').target, 'struct rec_t *');
  },
  'findTypes() handles enums'() {
    const e = byName(findTypes('enum color { RED, GREEN };', 't.c'), 'color');
    eq(e.type, 'enum');
    eq(e.size, 4);
    eq(e.fields.map(f => f.name).join(','), 'RED,GREEN');
  },
  'findTypes() handles using aliases'() {
    const a = byName(findTypes('struct p { int x; };\nusing P = struct p*;', 't.cpp'), 'P');
    eq(a.type, 'typedef');
    eq(a.size, 8);
  },
  'findTypes() handles classes: access specifiers, methods, static members'() {
    const c = byName(findTypes('class C : public B {\npublic:\n  int a;\n  void m(int x) { return; }\n  static int s;\n  long l;\n};', 't.cpp'), 'C');
    eq(c.type, 'class');
    eq(c.methods.map(m => m.name).join(','), 'm');
    eq(JSON.stringify(layoutOf(c)), JSON.stringify([['a', 0, 4], ['l', 8, 8]]));
  },
  'findTypes() packs bitfields like GCC'() {
    const t = byName(findTypes('struct bf { char a; unsigned x : 3; unsigned y : 6; int : 0; char z; unsigned long w : 40; int last; };', 't.c'), 'bf');
    eq(t.size, 24);
    const f = n => t.fields.find(x => x.name == n);
    eq(f('x').offset, 1);
    eq(f('x').bitOffset, 0);
    eq(f('y').bitOffset, 3);
    eq(f('y').bits, 6);
    eq(f('z').offset, 4);
    eq(f('w').offset, 8);
    eq(f('last').offset, 16);
  },
  'findTypes() leaves sizes null for unknown types instead of guessing'() {
    const t = byName(findTypes('struct s { char a; mystery_t m; int b; };', 't.c'), 's');
    eq(t.size, null);
    eq(t.fields[0].offset, 0);
    eq(t.fields[2].offset, null);
  },
  'findTypes() ignores forward declarations, variables and function definitions'() {
    eq(findTypes('struct fwd;\nstruct s v;\nstruct s* f(void) { return 0; }\n', 't.c').length, 0);
  },
});
