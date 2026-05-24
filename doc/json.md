# json

Source: `quickjs-json.c` — module exports **`JsonParser`** and a function list.

A streaming/extended JSON reader plus simple read/write helpers.

## Module functions

| Function | Args | Description |
| --- | --- | --- |
| `read(input)` | 1 | Parses JSON text into a JS value. |
| `write(value, options)` | 2 | Serializes a JS value to JSON text. |

## JsonParser

An incremental JSON parser.

```js
new JsonParser(input)   // length 1
```

| Member | Args | Kind | Description |
| --- | --- | --- | --- |
| `parse()` | 0 | method | Parses the next token/value from the input. |
| `pos` | — | getter | Current parse position (enumerable). |
| `token` | — | getter | The current token (enumerable). |
| `state` | — | getter | Parser state (enumerable). |
| `depth` | — | getter | Current nesting depth (enumerable). |
| `callback` | — | getter/setter | Per-value callback invoked while parsing. |
