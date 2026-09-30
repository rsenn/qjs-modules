# location

Source: `quickjs-location.c` — module export: **`Location`**

Represents a position in a source text: line, column, character/byte offset and
file name. Used by the lexer and other parsers.

## Constructor

```js
new Location([line, column, charOffset, file])   // length 1
```

## Properties (enumerable)

| Property | Read/write | Description |
| --- | --- | --- |
| `line` | read-only | 1-based line number. |
| `column` | read-only | 1-based column number. |
| `charOffset` | read-only | Offset in characters from the start. |
| `byteOffset` | read-only | Offset in bytes from the start. |
| `file` | read/write | Source file name. |

`line`, `column`, `charOffset` and `byteOffset` have no setter at all (assigning
to them throws, same as any accessor property without one) - use `copy()` or
`nextChar()` below to advance/update them. `file` is the only property that can
be assigned directly.

## Methods

| Method | Args | Description |
| --- | --- | --- |
| `equal(other)` | 1 | Compares this location with another for equality. |
| `clone()` | 0 | Returns a copy of the location. |
| `copy(other)` | 1 | Overwrites this location in place with `other`'s line/column/offsets/file (`other` may be a `Location` or a location-like object with `line`/`column`/`file`/`charOffset`/`byteOffset`, or `lineNumber`/`columnNumber`/`fileName` aliases). |
| `nextChar(input)` | 1 | Advances the location by one character - `input` may be a numeric code point, or an `ArrayBuffer`/typed array/string to decode the next UTF-8 codepoint from. Returns the number of bytes consumed. |
| `toString()` | 0 | Renders as `file:line:column`. |
| `[Symbol.toPrimitive]()` | 0 | Primitive coercion (string form; the `"number"` hint instead returns `charOffset`). |

## Static functions

| Function | Args | Description |
| --- | --- | --- |
| `count(input)` | 1 | Counts lines/columns by scanning a string, producing a `Location`. |
