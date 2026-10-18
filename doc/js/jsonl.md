# JSONL (lib/jsonl.js)

Bun's `JSONL` namespace: values separated by newlines (`\n` or `\r\n`, blank lines
allowed, a value may span lines). Same results as `Bun.JSONL` for every case in
`tests/unittests/test-json.js`.

| Function | Description |
| --- | --- |
| `JSONL.parse(input)` | `input` is a string or `Uint8Array`. Returns the array of values read; an incomplete trailing value is ignored; a bad value ends the list. Throws `SyntaxError` only when the first value is bad. |
| `JSONL.parseChunk(input, start?, end?)` | Never throws on bad data. Returns `{ values, read, done, error }`. `start`/`end` are chars for a string, bytes for a `Uint8Array`. |

`parseChunk` result:

| Field | Meaning |
| --- | --- |
| `values` | the values parsed |
| `read` | unit just after the last value (absolute, in the input's units); carry `input.slice(read)` into the next chunk |
| `done` | `true` when the rest of the range is only whitespace |
| `error` | `SyntaxError` for a bad value or for content after a value on the same line, else `null` |

```js
import { JSONL } from './jsonl.js';

JSONL.parse('{"a":1}\n[2]\n3');        // [{a: 1}, [2], 3]
JSONL.parseChunk('1\n2\n[3');          // { values: [1, 2], read: 3, done: false, error: null }
```

A value must end its line: `1 2` is an error, as is `{}{}`. A last token without a newline
(`12`) is a value; one that does not parse (`xx`, `1e`) is incomplete, not an error.

