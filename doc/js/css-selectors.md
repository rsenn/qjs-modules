# css-selectors

Source: `lib/css-selectors.js` (pure JS)

Compiles CSS selectors into predicate functions (for matching against a DOM-like
tree).

## Exports

| Export | Args | Kind | Description |
| --- | --- | --- | --- |
| `parseSelectors(s)` | 1 | function | Parses a selector string `s` into a matcher. |
| `emitPredicates(node)` | 1 | function | Emits the predicate(s) for a parsed selector node. |
