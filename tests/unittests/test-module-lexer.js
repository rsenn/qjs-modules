/* lib/module-lexer.js: parse(), the ES module and the CommonJS part.
 * Records are compared as JSON, ranges through the text they cover. The
 * expected CommonJS exports are what Node 26's merve 1.2.2 returns. */
import { parse } from '../../lib/module-lexer.js';
import { toArrayBuffer } from 'misc';
import { assert, eq, tests } from '../../lib/tinytest.js';

const json = v => JSON.stringify(v);
const text = (src, [s, e]) => src.slice(s, e);

/* kind, path and bindings of every import, as one string per import */
const imports = src => parse(src).imports.map(({ kind, path, bindings }) => `${kind} ${path} ${bindings.map(({ imported, local }) => (imported == local ? local : `${imported}>${local}`)).join(',')}`.trim());

/* kind and name of every export, local after a colon when it differs */
const exports = src => parse(src).exports.map(({ kind, name, local }) => `${kind} ${name}${local != null && local != name ? ':' + local : ''}`);

/* the CommonJS exports in merve's shape: [names, re-exported paths] */
const merve = src => {
  const cjs = parse(src).exports.filter(e => e.kind == 'commonjs');

  return json([[...new Set(cjs.filter(e => e.name != '*').map(e => e.name))], cjs.filter(e => e.name == '*').map(e => e.path)]);
};

tests({
  'default, named, renamed and string-named imports'() {
    eq(json(['import-statement ./x.js default>a,b>c,d,e-f>g']), json(imports("import a, { b as c, d, 'e-f' as g } from './x.js';")));
  },

  'namespace and side-effect imports'() {
    eq(json(['import-statement ./y.js *>ns', 'import-statement ./side.js']), json(imports("import * as ns from './y.js';\nimport './side.js'")));
  },

  'an import after other statements is found'() {
    eq(json(['import-statement ./x.js default>a', 'import-statement ./y.js default>b']), json(imports("import a from './x.js';\nconst z = 1;\nimport b from './y.js';")));
  },

  'the ranges of a static import'() {
    const src = "import a from './x.js' with { type: 'json' };";
    const [imp] = parse(src).imports;

    eq("import a from './x.js' with { type: 'json' }", text(src, imp.range));
    eq('./x.js', text(src, imp.pathRange));
    eq("{ type: 'json' }", text(src, imp.attributes));
    eq(1, imp.line);
  },

  'dynamic import with a literal, an expression, a template and options'() {
    const src = "import('./a.js');\nimport(p + '.js');\nimport(`./t${x}.js`);\nimport(`./plain.js`, { with: { type: 'json' } })";
    const r = parse(src).imports;

    eq(json(['./a.js', null, null, './plain.js']), json(r.map(i => i.path)));
    eq('./a.js', text(src, r[0].pathRange));
    eq("p + '.js'", text(src, r[1].pathRange));
    eq('./t*.js', r[2].glob);
    eq("import('./a.js')", text(src, r[0].range));
    eq('(', src[r[0].callStart]);
    eq("{ with: { type: 'json' } }", text(src, r[3].attributes));
    eq(json([1, 2, 3, 4]), json(r.map(i => i.line)));
    eq('commonjs', parse(src).format);
  },

  'import.meta'() {
    const src = 'const u = import.meta.url;';
    const r = parse(src);

    eq('import-meta', r.imports[0].kind);
    eq('import.meta', text(src, r.imports[0].range));
    eq('module', r.format);
  },

  'import as a property or method name is no import'() {
    eq(0, parse("x.import('a'); y?.import('b'); class C { import() { return 1; } }; o = { import: 1 };").imports.length);
  },

  'an import inside a string, a comment, a regex or a template text is no import'() {
    eq(0, parse("s = \"import('a')\"; /* import('b') */ r = /import\\('c'\\)/; t = `import('d')`;").imports.length);
  },

  'a dynamic import inside a template substitution with braces'() {
    eq(json(['dynamic-import ./x.js']), json(imports("t = `${ {a: 1}.a } ${ await import('./x.js') }`;")));
  },

  'division is not mistaken for a regex'() {
    eq(json(['import-statement ./y.js default>y']), json(imports("const q = a / 2; import y from './y.js'; const r = b / 3;")));
  },

  'export declarations, destructuring included'() {
    eq(
      json(['declaration a', 'declaration b', 'declaration c', 'declaration d', 'declaration e', 'declaration f', 'declaration g', 'declaration h', 'declaration K']),
      json(exports('export const a = 1, { b, x: c, [k]: d, ...e } = o, [f, , g = 2] = p;\nexport function h() {}\nexport class K extends B {}')),
    );
  },

  'export default forms'() {
    eq(json(['default default:h']), json(exports('export default function h() {}')));
    eq(json(['default default:K']), json(exports('export default class K {}')));
    eq(json(['default default']), json(exports('export default async function () {}')));
    eq(json(['default default']), json(exports('export default a + b;')));
  },

  'the ranges of export default'() {
    const src = 'export default function h() { return 1; }\nfoo();';
    const [exp] = parse(src).exports;

    eq('default', text(src, exp.nameRange));
    eq('h', text(src, exp.localRange));
    eq('export default function h() { return 1; }', text(src, exp.range));
  },

  'an export list'() {
    const src = "export { a, b as c, d as 'e-f', g as default };";
    const r = parse(src);

    eq(json(['list a', 'list c:b', 'list e-f:d', 'list default:g']), json(exports(src)));
    eq('c', text(src, r.exports[1].nameRange));
    eq('b', text(src, r.exports[1].localRange));
    eq(0, r.imports.length);
  },

  're-exports'() {
    const src = "export * from './z.js';\nexport * as w from './w.js';\nexport { e as f, default } from './y.js';";
    const r = parse(src);

    eq(json(['reexport *', 'reexport w', 'reexport f', 'reexport default']), json(exports(src)));
    eq(json([null, null, 'e', 'default']), json(r.exports.map(e => e.imported)));
    eq(json([0, 1, 2, 2]), json(r.exports.map(e => e.importIndex)));
    eq(json([true, true, true]), json(r.imports.map(i => i.reexport)));
    eq('e', text(src, r.exports[2].importedRange));
    eq("export { e as f, default } from './y.js'", text(src, r.imports[2].range));
  },

  'format and facade'() {
    eq('module', parse("import a from './a.js';").format);
    eq('commonjs', parse("const a = require('./a.js');").format);
    eq(true, parse("import a from './a.js';\nexport * from './b.js';\nexport { a };").facade);
    eq(false, parse("import a from './a.js'; a();").facade);
    eq(false, parse('export const x = 1;').facade);
  },

  'offsets are UTF-16 indices'() {
    const src = "const s = '😀😀';\nimport('./x.js');";
    const [imp] = parse(src).imports;

    eq('./x.js', text(src, imp.pathRange));
    eq(2, imp.line);
  },

  'an ArrayBuffer source gives the same records'() {
    const src = "const s = 'é😀';\nimport a from './x.js';";

    eq(json(parse(src)), json(parse(toArrayBuffer(src))));
  },

  'escapes in specifiers and names are decoded'() {
    const r = parse("import { '\\u0061b' as c } from './\\x78.js';");

    eq('./x.js', r.imports[0].path);
    eq('ab', r.imports[0].bindings[0].imported);
  },

  'a shebang and comments around the imports'() {
    eq(json(['import-statement ./x.js default>a']), json(imports("#!/usr/bin/env qjsm\n// c\nimport /* d */ a from './x.js';")));
  },

  'errors keep the records before them'() {
    const r = parse("import a from './x.js';\nconst s = 'abc");

    eq(json({ code: 'UNTERMINATED_STRING_LITERAL', line: 2, column: 11 }), json(r.error));
    eq(1, r.imports.length);
  },

  'error codes'() {
    const code = src => parse(src).error?.code;

    eq('UNTERMINATED_TEMPLATE_STRING', code('x = `abc'));
    eq('UNTERMINATED_PAREN', code('f(a'));
    eq('UNTERMINATED_BRACE', code('{ a'));
    eq('UNEXPECTED_PAREN', code('a)'));
    eq('UNEXPECTED_BRACE', code('a }'));
    eq('UNTERMINATED_REGEX', code('x = /abc\n'));
    eq(undefined, code("import a from './x.js'"));
  },

  'require and require.resolve calls'() {
    eq(
      json(['require-call ./a.js', 'require-call b', 'require-call ./t.js', 'require-resolve ./d.js']),
      json(imports("require('./a.js'); function f() { require('b') } x.require('c'); require(`./t.js`); require.resolve('./d.js'); require('e', 1); require()")),
    );
  },

  'a require with an expression or a template'() {
    const src = 'require(p + ".js"); require(`./u${x}.js`)';
    const r = parse(src).imports;

    eq(json([null, null]), json(r.map(i => i.path)));
    eq('p + ".js"', text(src, r[0].pathRange));
    eq('./u*.js', r[1].glob);
  },

  'the ranges of a require call'() {
    const src = "x = 1;\nconst m = require('./m.js');";
    const [imp] = parse(src).imports;

    eq("require('./m.js')", text(src, imp.range));
    eq('./m.js', text(src, imp.pathRange));
    eq(2, imp.line);
  },

  'bindings of a declared require'() {
    eq(json(['require-call path p,q>r,s', 'require-call ./m.js default>m']), json(imports("const { p, q: r, s = 1 } = require('path');\nvar m = require('./m.js')")));
    eq(json(['require-call ./a.js default>a', 'require-call ./b.js default>b']), json(imports("let a = require('./a.js'), b = require('./b.js');")));
  },

  'no bindings unless the declarator is only the call'() {
    const none = src => json(parse(src).imports.map(i => i.bindings.length));

    eq('[0]', none("const x = require('./x.js').y;"));
    eq('[0]', none("const { a: { b } } = require('./x.js');"));
    eq('[0]', none("x = require('./x.js');"));
    eq('[0]', none("function f() { const x = require('./x.js'); }"));
  },

  'a require or import() inside an export statement'() {
    eq(json(['dynamic-import ./x.js', 'require-call acorn']), json(imports("export const m = await import('./x.js');\nexport default function f() { return require('acorn'); }")));
  },

  'exports.x and module.exports.x'() {
    eq('[["a","b","c","ab","x-y"],[]]', merve("exports.a = 1; module.exports.b = 2; function f() { exports.c = 3 } exports['\\u0061b'] = 4; module.exports['x-y'] = 5"));
    eq('[["a","b"],[]]', merve('exports.a = 1;\nexports\n.b\n= 2'));
  },

  'what is not an export'() {
    eq('[[],[]]', merve('x.exports.a = 1; x.module.exports.b = 2; exports.c.d = 3; exports = 4; e(exports.f)'));
  },

  'merve counts == and === after exports.x'() {
    eq('[["a","b","c"],[]]', merve('exports.a == 1; exports.b === 2; exports.c = 3'));
  },

  'module.exports = an object literal'() {
    eq('[["a","b-c"],[]]', merve("module.exports = { a, 'b-c': d, e: 1, f }"));
    eq('[["a","b"],[]]', merve('module.exports = { a, b: x.y, c }'));
    eq('[["a","b","c"],[]]', merve('module.exports = { a: true, b: null, c() {}, d }'));
    eq('[["b"],[]]', merve('module.exports = { b, get a() { return 1 } }'));
    eq('[[],[]]', merve('module.exports = { get a() { return 1 }, b }'));
  },

  'the export records of an object literal'() {
    const src = 'module.exports = { a, b: c };';
    const r = parse(src).exports;

    eq(json(['commonjs a', 'commonjs b:c']), json(exports(src)));
    eq('c', text(src, r[1].localRange));
    eq('b', text(src, r[1].nameRange));
    eq('module.exports = { a, b: c }', text(src, r[0].range));
  },

  'Object.defineProperty'() {
    eq('[["__esModule","g","h"],[]]', merve("Object.defineProperty(exports, '__esModule', { value: true }); Object.defineProperty(module.exports, 'g', { enumerable: true, get: function () { return m.g; } }); Object.defineProperty(exports, 'h', { get() { return q['h'] } })"));
    eq('[[],[]]', merve("Object.defineProperty(exports, 'i', { get: () => 1 })"));
  },

  're-exports of require'() {
    eq('[["a","b"],["./r","./s"]]', merve("module.exports = { a, ...require('./r'), ...rest, b: require('./s') }"));
    eq('[[],["./r"]]', merve("module.exports = require('./r').foo"));
    eq('[[],["./e","./s"]]', merve("__export(require('./e')); tslib.__exportStar(require('./s'), exports); function g() { __export(require('./n')) }"));
  },

  'module.exports = drops the re-exports before it'() {
    eq('[[],["./b"]]', merve("module.exports = require('./a'); module.exports = require('./b')"));
    eq('[["x"],[]]', merve("module.exports = require('./a'); module.exports.x = 1; module.exports = y"));
  },

  'the re-export record points at its import'() {
    const src = "module.exports = require('./r');";
    const r = parse(src);
    const [exp] = r.exports;

    eq('./r', exp.path);
    eq(0, exp.importIndex);
    eq("require('./r')", text(src, exp.nameRange));
    eq('require-call', r.imports[exp.importIndex].kind);
  },

  'the Object.keys re-export loops of Babel and TypeScript'() {
    eq('[[],["./x"]]', merve("var _x = require('./x'); Object.keys(_x).forEach(function (k) { if (k === 'default' || k === '__esModule') return; exports[k] = _x[k]; });"));
    eq(
      '[[],["./y"]]',
      merve("var _y = require('./y'); Object.keys(_y).forEach(function (key) { if (key === 'default' || key === '__esModule') return; if (key in exports && exports[key] === _y[key]) return; Object.defineProperty(exports, key, { enumerable: true, get: function () { return _y[key]; } }); });"),
    );
    eq('[[],["./z"]]', merve("var _z = require('./z'); Object.keys(_z).forEach(function (k) { if (k !== 'default' && !Object.prototype.hasOwnProperty.call(exports, k)) exports[k] = _z[k]; });"));
    eq('[[],["./w"]]', merve("var _w = _interopRequireWildcard(require('./w')); Object.keys(_w).forEach(function (k) { if (k === 'default' || k === '__esModule') return; exports[k] = _w[k]; });"));
  },

  'an Object.keys loop inside a function or over an unknown name'() {
    eq('[[],[]]', merve("function f() { var _t = require('./t'); Object.keys(_t).forEach(function (k) { if (k === 'default' || k === '__esModule') return; exports[k] = _t[k]; }); }"));
    eq('[[],[]]', merve("Object.keys(o).forEach(function (k) { if (k === 'default' || k === '__esModule') return; exports[k] = o[k]; });"));
  },

  'CommonJS exports leave the format alone'() {
    eq('commonjs', parse('exports.a = 1;').format);
    eq('module', parse("import a from './a.js'; exports.b = a;").format);
  },
});
