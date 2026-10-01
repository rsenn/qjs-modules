# parser

Source: `lib/parser.js` (pure JS) — default export: `Parser`

A small grammar/parser-combinator toolkit (rules, terminals, sequences,
repetition). `Rule` instances compose through methods: `.then(...rules)`
(sequence), `.or(...rules)` (alternative), `.expect(rule)` (expectation, throws
`ExpectationError` if the right side fails), `.some()` (one or more),
`.optional()` (zero or one) and `.many()` (zero or more). A plain function
operand is a semantic action; a bare token id is wrapped in a `Rule`.

## Exports

| Export | Args | Kind | Description |
| --- | --- | --- | --- |
| `Parser` | — | class | Parser driver over a grammar of `Rule`s. **(default export)** |
| `Rule` | — | class | Base grammar rule. |
| `Terminal` | — | class | A terminal rule matching a literal/token (`extends Rule`). |
| `OneOrMore` | — | class | Repetition rule, one or more (`extends Rule`). |
| `Sequence` | — | class | Sequence of sub-rules (`extends Rule`). |
| `DumpToken(...args)` | * | function | Debug-prints a token. |
