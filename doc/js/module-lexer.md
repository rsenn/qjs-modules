# module-lexer (lib/module-lexer.js)

The imports and exports of a JS source, as records: ES module syntax and CommonJS `require`/`exports` in one
scan, with ranges for rewriting. qjsm only (it lexes with the native `lexer` module through
`lib/lexer/ecmascript.js`). `utilities/resolve-imports.js` is built on it.

| Function | Description |
| --- | --- |
| `parse(source)` | `source` is a string or an ArrayBuffer of UTF-8. Returns `{ format, facade, imports, exports, error? }`. Never throws: a lexing error is reported in `error`, the records before it are kept. |
| `print(record, format = 'module')` | The statement(s) an import record stands for: `'module'` gives `import`, `'commonjs'` gives `require`. Throws `TypeError` for a record without a literal path or with attributes. |
| `merge(imports)` | One `import-statement` record per path, with the bindings of all of them, in the order the paths first appear. Re-exports, records with attributes and other kinds pass through unchanged. |

```js
import { merge, parse, print } from 'module-lexer';

const { imports } = parse("import { a } from 'os';\nimport * as ns from './x.js';\nimport { b as c } from 'os';");

print(imports[1]);                         // "import * as ns from './x.js';"
print(imports[1], 'commonjs');             // "const ns = require('./x.js');"
merge(imports).map(r => print(r));         // ["import { a, b as c } from 'os';", "import * as ns from './x.js';"]
```

## Result of parse()

| Key | Holds |
| --- | --- |
| `format` | `'module'` when the source has import/export statements or `import.meta`, else `'commonjs'` |
| `facade` | `true` when the source is nothing but import, `export … from`, `export *` and `export { … }` statements |
| `imports` | import records, in source order |
| `exports` | export records, in source order, ES and CommonJS alike |
| `error` | the first lexing error: `{ code, line, column }`, codes named as merve's (`UNTERMINATED_STRING_LITERAL`, ...) plus `UNEXPECTED_CHARACTER` |

Offsets are UTF-16 indices into the source string, a range is `[start, end)` as `source.slice(start, end)` takes it.
A statement's `range` ends before its terminating `;`.

### Import record

| Key | Holds |
| --- | --- |
| `kind` | Bun's `import-statement`, `dynamic-import`, `require-call`, `require-resolve`, plus `import-meta` |
| `path` | the specifier, escapes decoded; `null` when it is not a literal (`import(p + '.js')`) |
| `glob` | for a template literal with substitutions, the pattern with `*` per substitution: `./a*.js` |
| `bindings` | `[{ imported, local }]`, `imported` is a name, `'default'` or `'*'`; empty for `import './x.js'`, dynamic imports and re-exports |
| `reexport` | `true` for `export … from`, whose names are in the export records |
| `range` | the statement, call or `import.meta` |
| `pathRange` | the specifier without quotes, or the argument expression when it is not a literal |
| `callStart` | the `(` of a dynamic import |
| `attributes` | the range of `with { … }`, or of the second `import()` argument |
| `line` | 1-based line of `range[0]` |

A `require` call gets `bindings` only from a top-level `var`/`let`/`const` whose initializer is nothing but the
call: `const m = require('x')` binds `default` to `m`, `const { a, b: c } = require('x')` binds `a` and `b as c`.

### Export record

| Key | Holds |
| --- | --- |
| `kind` | `declaration`, `list`, `default`, `reexport`, `commonjs` |
| `name` | exported name, `'default'`, or `'*'` for `export * from` and a CommonJS re-export |
| `local` | local binding, `null` for `export default <expr>` and re-exports |
| `imported` | for a re-export, the name in the other module, `null` for `export * as ns` |
| `path`, `importIndex` | for a re-export, the specifier and the index of its import record |
| `range`, `nameRange`, `localRange`, `importedRange` | the statement, the exported name, the local name, the imported name |
| `line` | 1-based line of `nameRange[0]` |

CommonJS exports follow the rules of merve 1.2.2, the lexer Node 26 uses: `exports.x =`, `exports['x'] =`,
`module.exports.x =`, `Object.defineProperty(exports, 'x', …)`, the keys of `module.exports = { a, b: c }`, and
the re-exports `module.exports = require('y')`, `{ ...require('y') }`, `__exportStar(require('y'))` and the
`Object.keys(_y).forEach(…)` loops Babel and TypeScript emit. Like merve, there is no scope analysis.

## print()

| Bindings | `'module'` | `'commonjs'` |
| --- | --- | --- |
| none | `import './x.js';` | `require('./x.js');` |
| default `a` | `import a from './x.js';` | `const a = require('./x.js');` |
| `*` as `ns` | `import * as ns from './x.js';` | `const ns = require('./x.js');` |
| `b as c`, `d` | `import { b as c, d } from './x.js';` | `const { b: c, d } = require('./x.js');` |
| default `a` and `b` | `import a, { b } from './x.js';` | two statements |
| `*` and `b` | two statements: `import * as ns …` then `import { b } …` | two statements |

A name that is not an identifier is quoted: `import { 'e-f' as g } from './x.js';`.
