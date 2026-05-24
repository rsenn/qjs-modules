# location

Source: `quickjs-location.c` — module export: **`Location`**

Represents a position in a source text: line, column, character/byte offset and
file name. Used by the lexer and other parsers.

## Constructor

```js
new Location([line, column, charOffset, file])   // length 1
```

## Properties (read/write, enumerable)

| Property | Description |
| --- | --- |
| `line` | 1-based line number. |
| `column` | 1-based column number. |
| `charOffset` | Offset in characters from the start. |
| `byteOffset` | Offset in bytes from the start. |
| `file` | Source file name. |

## Methods

| Method | Args | Description |
| --- | --- | --- |
| `equal(other)` | 1 | Compares this location with another for equality. |
| `clone()` | 0 | Returns a copy of the location. |
| `toString()` | 0 | Renders as `file:line:column`. |
| `[Symbol.toPrimitive]()` | 0 | Primitive coercion (string form). |

## Static functions

| Function | Args | Description |
| --- | --- | --- |
| `count(input)` | 1 | Counts lines/columns by scanning a string, producing a `Location`. |
