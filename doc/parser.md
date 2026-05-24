# parser

Source: `lib/parser.js` (pure JS) — default export: `Parser`

A small grammar/parser-combinator toolkit (rules, terminals, sequences,
repetition) plus operator-set construction.

## Exports

| Export | Args | Kind | Description |
| --- | --- | --- | --- |
| `Parser` | — | class | Parser driver over a grammar of `Rule`s. **(default export)** |
| `Rule` | — | class | Base grammar rule. |
| `Terminal` | — | class | A terminal rule matching a literal/token (`extends Rule`). |
| `OneOrMore` | — | class | Repetition rule, one or more (`extends Rule`). |
| `Sequence` | — | class | Sequence of sub-rules (`extends Rule`). |
| `make_operators_set(...op_list)` | * | function | Builds an operator-precedence set from operator definitions. |
| `DumpToken(...args)` | * | function | Debug-prints a token. |
