# xml

Source: `quickjs-xml.c` — module exports a function list and a `default` object.

A small XML/HTML reader and writer. Parses markup into a tree of plain JS
objects and serializes such a tree back to text.

## Functions

| Function | Args | Description |
| --- | --- | --- |
| `read(input)` | 1 | Parses XML/HTML text into an array/tree of element objects (`{tagName, attributes, children}`-shaped). |
| `write(value, options)` | 2 | Serializes a parsed tree back into XML/HTML text. |

Both are also reachable through the module's `default` export object.
