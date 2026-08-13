# parsel

Source: `lib/parsel.js` (pure JS)

A port of the **parsel** CSS-selector tokenizer/parser: turns a selector string
into a token list / AST and back.

## Exports

| Export | Args | Kind | Description |
| --- | --- | --- | --- |
| `tokenize(selector, grammar=TOKENS)` | 1–2 | function | Splits a selector into tokens. |
| `nestTokens(tokens, {list})` | 1–2 | function | Builds a nested AST from a flat token list. |
| `parse(selector, {recursive, list})` | 1–2 | function | Tokenizes and nests into a selector AST. |
| `flatten(node, parent)` | 1–2 | generator | Yields `[node, parent]` pairs walking the AST. |
| `walk(node, visit, parent)` | 2–3 | function | Visits every AST node with `visit(node, parent)`. |
| `stringify(listOrNode)` | 1 | function | Serializes an AST back to a selector string. |
