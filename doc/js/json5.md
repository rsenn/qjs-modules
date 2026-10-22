# json5

Source: `lib/json5.js` (over the native [json](../native/json.md) engines, qjsm only) — exports `JSON5` (also the
default), `parse`, `stringify` and `parseDialect`

`Bun.JSON5`, which is this object; the same shape as the npm `json5` package. Results were compared with bun 1.4.2
(`tests/unittests/test-json5.js`: 35 parse and 17 stringify cases).

```js
import { JSON5 } from 'json5';

JSON5.parse("{a: 1, 'b': [0x1F, .5,], // c\n}");   // { a: 1, b: [31, 0.5] }
JSON5.stringify({ a: [1, 'x'] });                    // "{a:[1,'x']}"
JSON5.stringify({ a: 1 }, null, 2);                  // "{\n  a: 1,\n}"
```

| Function | Notes |
| --- | --- |
| `parse(text)` | one argument like Bun, no reviver; objects are plain, a `__proto__` key stays an own key; nesting depth is not limited |
| `stringify(value, replacer, space)` | `replacer` must be `null` or `undefined` (an `Error` otherwise, like Bun); `space` is a count up to 10 or a string of up to 10 characters |
| `parseDialect(text, dialect, prefix)` | the one value of `text` in a `JsonParser` dialect (`'json'`, `'jsonc'`, `'json5'`), errors prefixed with `prefix` |

`stringify` follows Bun, not `JSON.stringify`:

- keys are bare where they are identifiers, otherwise single-quoted
- strings are single-quoted, control characters as `\0`, `\b`, `\t`, `\n`, `\f`, `\r` or `\xHH`
- `NaN`, `Infinity`, `-Infinity` are written as such
- `toJSON()` is not called; `Number`, `String`, `Boolean` objects are written as their primitive
- indented output ends every member with a comma
- `BigInt` and a circular reference throw `Error`

A parse error is a `SyntaxError` with the engine's `code` and `loc`; its message starts with `JSON5 Parse error: `.

See also: [jsonc](jsonc.md), [jsonl](jsonl.md), [bun](bun.md).
