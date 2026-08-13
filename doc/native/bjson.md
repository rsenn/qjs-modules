# bjson

Source: `quickjs-bjson.c` — module exports a flat list of functions.

Reads and writes values using QuickJS's native binary object serialization (the
same format used by `JS_ReadObject` / `JS_WriteObject`).

## Functions

| Function | Args | Description |
| --- | --- | --- |
| `read(buffer, offset, length, flags)` | 4 | Deserializes a JS value from the bytes of an ArrayBuffer at `offset`/`length`, using the `JS_READ_OBJ_*` `flags`. |
| `write(value, flags)` | 2 | Serializes `value` to an `ArrayBuffer`, using the `JS_WRITE_OBJ_*` `flags`. |

## Constants

Read flags: `JS_READ_OBJ_BYTECODE`, `JS_READ_OBJ_ROM_DATA`, `JS_READ_OBJ_SAB`,
`JS_READ_OBJ_REFERENCE`.

Write flags: `JS_WRITE_OBJ_BYTECODE`, `JS_WRITE_OBJ_BSWAP`, `JS_WRITE_OBJ_SAB`,
`JS_WRITE_OBJ_REFERENCE`.
