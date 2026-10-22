# jsonc

Source: `lib/jsonc.js` (over the native [json](../native/json.md) engines, qjsm only) — exports `JSONC` (also the
default) and `parse`

`Bun.JSONC`, which is this object: JSON with `//` and `/* */` comments and trailing commas, as VS Code reads its
settings files.

```js
import { JSONC } from 'jsonc';

JSONC.parse('// settings\n{"a": [1, 2,],}');   // { a: [1, 2] }
```

`parse(text)` takes one argument like Bun. Objects are plain, nesting depth is not limited. A parse error is a
`SyntaxError` with `code` and `loc`; its message starts with `JSONC Parse error: `.

Bun also accepts three inputs that are a `SyntaxError` here, so a truncated or corrupt file is not read as valid:

| Input | Bun 1.4.2 | here |
| --- | --- | --- |
| `{'a': 1}` | `{ a: 1 }` | `SyntaxError` |
| `0x1` | `1` | `SyntaxError` |
| `1 2` | `1` | `SyntaxError` |

See also: [json5](json5.md), [jsonl](jsonl.md), [bun](bun.md).
