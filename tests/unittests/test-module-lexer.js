/* lib/module-lexer.js: parse(), the ES module part.
 * Records are compared as JSON, ranges through the text they cover. */
import { parse } from '../../lib/module-lexer.js';
import { toArrayBuffer } from 'misc';
import { assert, eq, tests } from '../../lib/tinytest.js';

const json = v => JSON.stringify(v);
const text = (src, [s, e]) => src.slice(s, e);

/* kind, path and bindings of every import, as one string per import */
const imports = src => parse(src).imports.map(({ kind, path, bindings }) => `${kind} ${path} ${bindings.map(({ imported, local }) => (imported == local ? local : `${imported}>${local}`)).join(',')}`.trim());

/* kind and name of every export, local after a colon when it differs */
const exports = src => parse(src).exports.map(({ kind, name, local }) => `${kind} ${name}${local != null && local != name ? ':' + local : ''}`);

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
});
