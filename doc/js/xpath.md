# xpath

Source: `lib/xpath.js` (pure JS) — default export: `XPath`

XPath-style addressing over a DOM-like tree, plus the standard
`XPathEvaluator`/`XPathResult` surface.

## Exports

| Export | Args | Kind | Description |
| --- | --- | --- | --- |
| `XPath` | — | class | An XPath expression / path object. **(default export)** |
| `ImmutableXPath`, `MutableXPath`, `XPathExpression` | — | const | Aliases of `XPath`. |
| `XPathEvaluator` | — | class | Evaluates expressions against a context node. |
| `XPathResult` | — | class | Result wrapper (node set / value). |
| `XPathException` | — | class | Error type for evaluation failures. |
| `DereferenceError` | — | class | Thrown when a path can't be resolved (`extends Error`). |
| `parseXPath(str)` | 1 | function | Parses an XPath string. |
| `buildXPath(ptr, root)` | 2 | function | Builds an XPath addressing `ptr` within `root`. |
| `getSiblings(ptr, root)` | 2 | function | Returns sibling nodes of the addressed node. |
