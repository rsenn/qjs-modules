import { browserEvent, callTree, callees, callers, classifyStatement, findDefines, findFunctions, findIdentifiers, findTypes, format, formatRows, makeBrowser, makeInputFilter, parseInput, reachable, renderBrowser, rowAt, show, showable, srcColAt, locate, pointerString, codeFor, base64 } from '../../utilities/extract-c.js';
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

const DEFS = `#define CONST 213
#define MAX(a, b) \\
  ((a) > (b) ? (a) : (b))
#define LOG(fmt, ...) f(fmt, __VA_ARGS__)
`;

const SHOW_SRC = `struct S { int a; char *b; };
static int leaf(void) { return 1; }
int leaf(void);
int main(void) { return leaf(); }
`;
const read = () => SHOW_SRC.split('\n');

const CG_SRC = `static int leaf(int v) {
  return v + 1;
}

static int mid(int v) {
  int r = leaf(v);
  return leaf(r) + helper(r);
}

static int helper(int v) {
  return mid(v - 1) + printf("x");
}

int main(void) {
  return mid(1);
}
`;
const cgRead = () => CG_SRC.split('\n');
const cgIds = () => findIdentifiers(CG_SRC, 'cg.c');

tests({
  'makeInputFilter() takes mouse and cursor reports out of the byte stream'() {
    const seen = [];
    const f = makeInputFilter(ev => seen.push(`${ev.button}@${ev.x},${ev.y}${ev.press ? '' : '!'}`), (r, c) => seen.push(`cursor ${r};${c}`));
    const feed = s => [...s].flatMap(c => f(c.charCodeAt(0)));

    eq(feed('a\x1b[<0;12;5Mb\x1b[<2;3;4m\x1b[12;40Rc').map(b => String.fromCharCode(b)).join(''), 'abc');
    eq(seen.join(' '), 'left@12,5 right@3,4! cursor 12;40');
  },
  'makeInputFilter() passes other escape sequences on unchanged'() {
    const f = makeInputFilter(() => {}, () => {});
    const feed = s => [...s].flatMap(c => f(c.charCodeAt(0))).map(b => String.fromCharCode(b)).join('');
    eq(feed('\x1b[1;5C'), '\x1b[1;5C');
    eq(feed('\x1bb'), '\x1bb');
    eq(feed('\x1b[A'), '\x1b[A');
  },
  'base64() encodes UTF-8 with padding'() {
    eq(base64('a'), 'YQ==');
    eq(base64('ab'), 'YWI=');
    eq(base64('abc'), 'YWJj');
    eq(base64('/a/b.c:1:2'), 'L2EvYi5jOjE6Mg==');
  },
  'srcColAt() maps a display offset back to a source column'() {
    eq(srcColAt('int x;', 0, 0, 4), 5);
    eq(srcColAt('\tint x;', 0, 0, 0), 1);
    eq(srcColAt('\tint x;', 0, 0, 1), 1);
    eq(srcColAt('\tint x;', 0, 0, 2), 2);
    eq(srcColAt('abcdefgh', 4, 1, 1), 5);
    eq(srcColAt('abc', 0, 0, 99), 4);
  },
  'pointerString() writes .prop[0].value paths'() {
    eq(pointerString(['references', 3, 'in']), '.references[3].in');
    eq(pointerString([2, 'fields', 0]), '[2].fields[0]');
    eq(pointerString([]), '');
  },
  'rowAt() finds the show() row under a cell, counting wrapped rows'() {
    const rows = ['aaaa', 'b'.repeat(25), 'cc'];
    const hit = (x, y) => rowAt(rows, 10, 20, x, y);

    eq(hit(1, 15)?.row, rows[0]);
    eq(String(hit(5, 16)?.row), 'b'.repeat(25));
    eq(hit(5, 18)?.x, 25);
    eq(String(hit(1, 19)?.row), 'cc');
    assert(hit(1, 20) === null && hit(1, 14) === null);
  },
  'formatRows() rows know the pointer and source location of what they show'() {
    const ids = findIdentifiers(SHOW_SRC, 's.c');
    const { rows } = formatRows(ids.get('leaf'), { color: false, read });
    const findRow = text => rows.find(r => String(r).includes(text));

    const head = findRow('reference    s.c:4:25');
    const file = locate(head, head.cells.find(c => c.atoms.at(-1) == 'file').x0);
    eq(pointerString(file.atoms), '.references[0].file');

    const code = findRow('4 │ int main');
    const at = locate(code, code.code.x0 + 24);
    eq(pointerString(at.atoms), '.references[0]');
    eq(`${at.src.file}:${at.src.line}:${at.src.column}`, 's.c:4:25');

    const kind = locate(rows[0], 3);
    eq(pointerString(kind.atoms), '.declaration.kind');
  },
  'codeFor() reaches the record from globalThis'() {
    const ids = findIdentifiers(SHOW_SRC, 's.c');
    const leaf = ids.get('leaf');
    const hit = { atoms: ['references', 1], item: 0 };

    eq(codeFor(leaf, hit, { ids }), 'globalThis.ids.get("leaf").references[1]');
    eq(codeFor(leaf, hit, { records: [1, leaf] }), 'globalThis.records[1].references[1]');
    eq(codeFor([leaf], hit, { records: [leaf] }), 'globalThis.shown[0].references[1]');
    const list = [leaf];
    eq(codeFor(list, { atoms: ['name'], item: 0 }, { records: list }), 'globalThis.records[0].name');
    eq(codeFor(leaf, { atoms: [], root: 'ids.get("leaf")' }, {}), 'globalThis.ids.get("leaf")');
  },
  'callees() and callers() follow references made inside function bodies'() {
    const ids = cgIds();
    eq(callees(ids, 'mid').map(s => s.callee).join(), 'leaf,leaf,helper');
    eq(callers(ids, 'leaf').map(s => s.caller).join(), 'mid,mid');
    assert(!callees(ids, 'helper').some(s => s.callee == 'printf'), 'external functions are not callees');
  },
  'reachable() collects the call graph in both directions'() {
    const ids = cgIds();
    eq([...reachable(ids, 'main')].sort().join(), 'helper,leaf,main,mid');
    eq([...reachable(ids, 'leaf', true)].sort().join(), 'helper,leaf,main,mid');
  },
  'callTree() marks recursion and stops at depth'() {
    const t = callTree(cgIds(), 'main', { depth: 3, color: false });
    assert(t.includes('mid ↻'), t);
    assert(!callTree(cgIds(), 'main', { depth: 1, color: false }).includes('leaf'));
  },
  'parseInput() splits keys and SGR mouse reports'() {
    const ev = parseInput('\x1b[A\x1b[<0;12;5M\x1b[<65;3;4M\x1b[<0;1;1m\x1b[Zq\r\x7f\x1b');
    eq(ev.map(e => (e.type == 'key' ? e.key : `${e.button}@${e.x},${e.y}${e.press ? '' : '!'}`)).join(' '), 'up left@12,5 wheeldown@3,4 left@1,1! shifttab q enter backspace esc');
  },
  'makeBrowser() is null for a function without a body'() {
    assert(makeBrowser(cgIds(), 'printf', cgRead) === null);
    assert(makeBrowser(cgIds(), 'mid', cgRead) !== null);
  },
  'a click on a call site descends, a breadcrumb click and right-click go back'() {
    const b = makeBrowser(cgIds(), 'main', cgRead);
    const click = (x, y, button = 0) => browserEvent(b, ...[{ type: 'mouse', button: ['left', 'middle', 'right'][button], x, y, press: true }, renderBrowser(b, 60, 12, false)]);
    const site = renderBrowser(b, 60, 12, false).sites[0];

    click(site.x1, site.y);
    eq(b.stack.map(f => f.name).join(), 'main,mid');

    const inner = renderBrowser(b, 60, 12, false).sites[2];
    click(inner.x0, inner.y);
    eq(b.stack.map(f => f.name).join(), 'main,mid,helper');

    click(5, 5, 2);
    eq(b.stack.map(f => f.name).join(), 'main,mid');
    click(1, 1);
    eq(b.stack.map(f => f.name).join(), 'main');
  },
  'a click beside a call site does nothing; keys select, descend and quit'() {
    const b = makeBrowser(cgIds(), 'mid', cgRead);
    const lay = () => renderBrowser(b, 60, 12, false);
    const key = k => browserEvent(b, { type: 'key', key: k }, lay());
    const s = lay().sites[0];

    browserEvent(b, { type: 'mouse', button: 'left', x: s.x1 + 2, y: s.y, press: true }, lay());
    eq(b.stack.length, 1);

    key('tab');
    key('tab');
    eq(b.stack[0].sel, 2);
    key('enter');
    eq(b.stack.at(-1).name, 'helper');
    key('esc');
    key('q');
    assert(b.done);
  },
  'renderBrowser() highlights call sites at the clicked columns'() {
    const b = makeBrowser(cgIds(), 'mid', cgRead);
    const { lines, sites } = renderBrowser(b, 60, 12, false);
    for(const s of sites) {
      const row = lines[s.y - 1];
      assert(/^(leaf|helper)$/.test(row.slice(s.x0 - 1, s.x1)), `${row.slice(s.x0 - 1, s.x1)} in ${row}`);
    }
  },
  'format() lays out an identifier with its declaration, prototype and reference'() {
    const ids = findIdentifiers(SHOW_SRC, 's.c');
    const out = format(ids.get('leaf'), { color: false, read });
    assert(out.includes('function leaf  static · 1 prototype · 1 reference'), out);
    assert(out.includes('declaration  s.c:2:12'), out);
    assert(out.includes('2 │ static int leaf(void) { return 1; }'), out);
    assert(out.includes('prototype    s.c:3:5'), out);
    assert(out.includes('reference    s.c:4:25  in main'), out);
  },
  'format() flags an unreferenced identifier'() {
    const ids = findIdentifiers('static int dead(void) { return 0; }\n', 'd.c');
    assert(format(ids.get('dead'), { color: false, read: () => ['static int dead(void) { return 0; }'] }).includes('✗ unreferenced'));
  },
  'format() lists struct fields with offset and size'() {
    const [s] = findTypes(SHOW_SRC, 's.c');
    const out = format({ ...s, file: 's.c' }, { color: false, read });
    assert(out.includes('struct S  size 16 · align 8'), out);
    assert(/\+8\s+8\s+char \*\s+b/.test(out), out);
  },
  'format() with color uses xterm-256 escapes, without it none'() {
    const [s] = findTypes(SHOW_SRC, 's.c');
    const rec = { ...s, file: 's.c' };
    assert(format(rec, { color: true, read }).includes('\x1b[38;5;'));
    assert(!format(rec, { color: false, read }).includes('\x1b'));
  },
  'format() reports an unreadable source file instead of throwing'() {
    const ids = findIdentifiers(SHOW_SRC, 'gone.c');
    assert(format(ids.get('main'), { color: false, read: () => null }).includes('cannot read gone.c'));
  },
  'show.filter limits format() by file, kind and name'() {
    const ids = findIdentifiers(SHOW_SRC, 's.c');
    const list = [...ids.values()].filter(e => e.declaration);
    const fmt = () => format(list, { color: false, read });
    const { filter } = show;

    try {
      filter.name = /^lea/;
      assert(fmt().includes('(1 of ') && fmt().includes('function leaf') && !fmt().includes('function main'), fmt());

      filter.name = null;
      filter.kind = new Set(['function']);
      assert(fmt().includes('function main') && !fmt().includes('struct S'), fmt());

      filter.file = /nomatch/;
      assert(fmt().includes('(0 of ') && !fmt().includes('■'), fmt());

      filter.file = /s\.c$/;
      assert(fmt().includes('function leaf'), fmt());
    } finally {
      filter.file = filter.kind = filter.name = null;
    }

    assert(!fmt().includes('filter'));
  },
  'showable() accepts records and arrays of them, nothing else'() {
    const ids = findIdentifiers(SHOW_SRC, 's.c');
    assert(showable(ids.get('leaf')));
    assert(showable(findDefines('#define A 1\n', 'a.h').map(d => ({ ...d, file: 'a.h' }))));
    assert(!showable({}) && !showable([]) && !showable('x') && !showable(null) && !showable(ids));
    assert(!showable([ids.get('leaf'), 1]));
  },
  'findDefines() tells object-like defines from function-like macros'() {
    const d = findDefines(DEFS, 'd.h');
    eq(d.map(x => `${x.kind} ${x.name}`).join(','), 'define CONST,macro MAX,macro LOG');
    eq(byName(d, 'CONST').value, '213');
  },
  'findDefines() reports macro params, variadic as "..." and joins continuations'() {
    const d = findDefines(DEFS, 'd.h');
    eq(byName(d, 'MAX').params.join(','), 'a,b');
    eq(byName(d, 'MAX').value, '((a) > (b) ? (a) : (b))');
    eq(byName(d, 'LOG').params.join(','), 'fmt,...');
    eq(byName(d, 'MAX').endLine, 3);
  },
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
  'findIdentifiers() separates declaration, prototype and references'() {
    const ids = findIdentifiers(
      `typedef struct Foo { int a; Bar* b; } Foo;
enum E { E_A, E_B = E_A + 1 };
extern int counter;
int global = DEFAULT + 1;
int add(int a, int b);
static int helper(Foo* f) { return f->a; }
int add(int a, int b) {
  int tmp = helper(0);
  for(int i = 0; i < n; i++) { tmp += global; }
  counter++;
  return tmp + b;
}
`,
      't.c',
    );
    const kind = n => ids.get(n).declaration?.kind;

    eq(kind('Foo'), 'typedef');
    eq(kind('E_B'), 'enumerator');
    eq(kind('global'), 'data');
    eq(kind('add'), 'function');
    eq(kind('helper'), 'function');
    assert(ids.get('helper').declaration.static);
    eq(ids.get('add').prototype.length, 1);
    eq(ids.get('counter').declaration, null);
    eq(ids.get('counter').prototype.length, 1);
    eq(ids.get('helper').references.map(r => r.in).join(), 'add');
    eq(ids.get('E_A').references.length, 1);
    eq(ids.get('Bar').declaration, null);
    eq(ids.get('global').declaration.column, 5);
    eq(ids.get('helper').declaration.column, 12);
    eq(ids.get('DEFAULT').references[0].column, 14);
    eq(ids.get('DEFAULT').references.length, 1);
  },
  'findIdentifiers() lets locals and parameters shadow globals, and skips member names'() {
    const ids = findIdentifiers('int x;\nint f(int y) { int x = y; return p->y + x; }\n', 't.c');
    eq(ids.get('x').references.length, 0);
    assert(!ids.has('y'));
  },
  'findIdentifiers() tolerates malformed input'() {
    findIdentifiers('int f( { ((( int x = ;;; } } struct { enum', 't.c');
    findIdentifiers('', 't.c');
  },
  'findIdentifiers() accumulates across files'() {
    const ids = findIdentifiers('int g(void);\n', 'a.h');
    findIdentifiers('int g(void) { return 0; }\nint h(void) { return g(); }\n', 'b.c', ids);
    eq(ids.get('g').prototype[0].file, 'a.h');
    eq(ids.get('g').declaration.file, 'b.c');
    eq(ids.get('g').references[0].file, 'b.c');
  },
  'findIdentifiers() records macros, their parameters and body references'() {
    const ids = findIdentifiers(
      `#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define LONG(x) \\
  helper(x) + \\
  OTHER
#  define SPACED 1 /* note */
#ifdef FEATURE
#endif
#if defined(A) && B > 1
#endif
#include <stdio.h>
`,
      't.c',
    );
    eq(ids.get('MIN').declaration.kind, 'macro');
    eq(ids.get('LONG').declaration.line, 2);
    eq(ids.get('SPACED').declaration.kind, 'macro');
    assert(!ids.has('a'));
    eq(ids.get('helper').references[0].line, 3);
    eq(ids.get('OTHER').references[0].line, 4);
    eq(ids.get('helper').references[0].in, 'LONG');
    for(const n of ['FEATURE', 'A', 'B']) eq(ids.get(n).references.length, 1);
    assert(!ids.has('defined') && !ids.has('stdio'));
  },
  'findIdentifiers() recognises labels and goto targets'() {
    const ids = findIdentifiers('int f(void) {\n  if(x) goto l_error;\n  return 0;\nl_error:\n  return -1;\n}\n', 't.c');
    eq(ids.get('l_error').declaration.kind, 'label');
    eq(ids.get('l_error').declaration.in, 'f');
    eq(ids.get('l_error').references.length, 1);
  },
  'findIdentifiers() declares struct tags, forward declarations and members'() {
    const ids = findIdentifiers('struct fwd;\nstruct node { int value; struct node* next; };\ntypedef struct node Node;\nstruct fwd* p;\n', 't.c');
    eq(ids.get('fwd').prototype.length, 1);
    eq(ids.get('node').declaration.kind, 'struct');
    eq(ids.get('node.value').declaration.kind, 'field');
    assert(!ids.has('value'));
    eq(ids.get('Node').declaration.kind, 'typedef');
  },
  'findIdentifiers() names fields <parent>.<field>'() {
    const ids = findIdentifiers('struct node { int v; union { int i; float f; }; struct Inner { int x; } inner; };\ntypedef struct { int a; } Anon;\nstruct { int g; } glob;\n', 't.c');
    const fields = [...ids.keys()].filter(n => ids.get(n).declaration?.kind == 'field').sort();
    eq(fields.join(), 'Anon.a,Inner.x,glob.g,node.f,node.i,node.inner,node.v');
  },
  'findIdentifiers() and findTypes() record character offsets for ranges'() {
    const src = '#define M(a) a\nstruct s { int a; };\nint f(int x) {\n  return M(x);\n}\n';
    const ids = findIdentifiers(src, 't.c');
    const at = p => src.slice(p.offset, p.end);
    eq(at(ids.get('M').declaration), 'M');
    eq(at(ids.get('f').declaration), 'f');
    eq(at(ids.get('M').references[0]), 'M');

    const t = byName(findTypes(src, 't.c'), 's');
    eq(src.slice(t.offset, t.end), 'struct s { int a; }');
  },
  'findIdentifiers() and findTypes() record the end line and column'() {
    const src = 'struct s {\n  int a;\n};\nint f;\n';
    const t = byName(findTypes(src, 't.c'), 's');
    eq([t.line, t.column, t.endLine, t.endColumn].join(), '1,1,3,2');

    const f = findIdentifiers(src, 't.c').get('f').declaration;
    eq([f.line, f.column, f.endLine, f.endColumn].join(), '4,5,4,6');
  },
  'findIdentifiers() skips annotation macros after a prototype'() {
    const ids = findIdentifiers('int trace(int a, const char* fmt, ...) FORMAT_STRING(2, 3);\n', 't.c');
    eq(ids.get('trace').prototype.length, 1);
    assert(!ids.get('FORMAT_STRING')?.prototype.length);
  },
  'findIdentifiers() handles annotation macros, initializer braces and stray tokens'() {
    const ids = findIdentifiers('void FORMAT(2, 3) trace(int a, const char* fmt, ...) { Rule r = {a, 1}, *prev; prev = &r; }\n/ int after(void) { return 0; }\nint m = 0b11u;\n', 't.c');
    eq(ids.get('trace').declaration.kind, 'function');
    assert(!ids.has('prev') && !ids.has('fmt') && !ids.has('b11u'));
    eq(ids.get('after').declaration.kind, 'function');
  },
  'classifyStatement() tells declarations from expressions'() {
    eq(classifyStatement('int f(int a) { return a; }'), 'function');
    eq(classifyStatement('ChildProcess* f(JSValueConst v);'), 'prototype');
    eq(classifyStatement('extern int counter;'), 'prototype');
    eq(classifyStatement('static int table[4] = { 1, 2 };'), 'data');
    eq(classifyStatement('Foo* p = NULL;'), 'data');
    eq(classifyStatement('typedef unsigned long ulong_t;'), 'typedef');
    eq(classifyStatement('typedef int (*cb_t)(int);'), 'typedef');
    eq(classifyStatement('struct S { int a; };'), 'type');
    eq(classifyStatement('x = y + 1;'), 'other');
    eq(classifyStatement('foo(a, b);'), 'other');
    eq(classifyStatement('return a * b;'), 'other');
  },
});
