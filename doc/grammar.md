# grammar

Source: `lib/parser/grammar.js` (pure JS) — default export: `Grammar`

A composable recursive-descent parsing library built on top of the
[`parser`](parser.md) driver and the [`lexer`](lexer.md) C module. Grammars
are constructed as trees of `GrammarRule` instances that are combined
through **methods** (`then`, `or`, `optional`, `many`, `some`, `not`, `as`,
`map`) — no QuickJS operator-overloading extension is required.

The companion [`ebnf`](../lib/parser/ebnf.js) module exports
`buildGrammar(source, filename)` which produces a `Grammar` from a
BNF / yacc-style source file.

## Exports

| Export | Args | Kind | Description |
| --- | --- | --- | --- |
| `Grammar` | `start?` | class | Named collection of rules with a start symbol. **(default export)** |
| `ParseNode` | `rule, children, tokens` | class | A node in a parse tree. |
| `ParseContext` | `parser, grammar` | class | Buffered, back-trackable view of a token stream. |
| `GrammarRule` | `name` | class | Abstract base for every grammar element. |
| `Terminal` | `matcher, name?` | class | Matches a token by type / lexeme / regex / predicate. |
| `NonTerminal` | `name` | class | Late-bound reference to another rule in the grammar. |
| `Sequence` | `children, name?` | class | Matches children in order. |
| `Alternatives` | `children, name?` | class | Tries children in order, first match wins. |
| `Optional` | `child, name?` | class | Zero or one occurrence. |
| `ZeroOrMore` | `child, name?` | class | Zero or more occurrences. |
| `OneOrMore` | `child, name?` | class | One or more occurrences. |
| `Not` | `child, name?` | class | Negative lookahead. |
| `Peek` | `child, name?` | class | Positive lookahead. |
| `Mapped` | `child, fn, name?` | class | Applies a transform to the parse result. |
| `Empty` | — | class | Always succeeds without consuming input. |
| `coerce(v)` | 1 | function | Turns a string / RegExp / function into a `Terminal`. |
| `t(matcher, name?)` | 1–2 | factory | Shortcut for `new Terminal(...)`. |
| `ref(name)` | 1 | factory | Shortcut for `new NonTerminal(name)`. |
| `seq(...items)` | * | factory | Shortcut for `new Sequence(...)`. |
| `alt(...items)` | * | factory | Shortcut for `new Alternatives(...)`. |
| `opt(item)` | 1 | factory | Shortcut for `new Optional(...)`. |
| `many(item)` | 1 | factory | Shortcut for `new ZeroOrMore(...)`. |
| `some(item)` | 1 | factory | Shortcut for `new OneOrMore(...)`. |
| `not(item)` | 1 | factory | Shortcut for `new Not(...)`. |
| `peek(item)` | 1 | factory | Shortcut for `new Peek(...)`. |
| `empty()` | 0 | factory | Shortcut for `new Empty()`. |

## GrammarRule

Every combinator derives from `GrammarRule`. Two methods drive the machinery:

| Method | Args | Description |
| --- | --- | --- |
| `parse(ctx)` | 1 | Entry point. Snapshots the context, calls `_parse`, rewinds on failure. Returns a `ParseNode` or `null`. |
| `_parse(ctx)` | 1 | Abstract; subclasses implement matching logic. |

### Composition methods (fluent)

| Method | Args | Description |
| --- | --- | --- |
| `then(...rest)` | * | Sequence. Named sequences are treated as one group, unnamed ones flatten. |
| `or(...rest)` | * | Alternatives. Named alternatives are one group, unnamed ones flatten. |
| `optional()` | 0 | Wraps in `Optional`. |
| `many()` | 0 | Wraps in `ZeroOrMore`. |
| `some()` | 0 | Wraps in `OneOrMore`. |
| `not()` | 0 | Wraps in `Not`. |
| `as(name)` | 1 | Returns a shallow copy with a new `.name` (used in the parse tree and in `toString`). |
| `map(fn)` | 1 | Wraps in `Mapped`; `fn(node, ctx)` is called on each successful match. |
| `toString()` | 0 | Returns a compact EBNF-like rendering. |

## Grammar

| Method | Args | Description |
| --- | --- | --- |
| `define(name, rule)` | 2 | Registers a rule under `name`. First `define` sets the start rule if none. |
| `get(name)` | 1 | Fetches a registered rule. |
| `ref(name)` | 1 | Convenience: returns a `NonTerminal`. |
| `terminal(matcher, name?)` | 1–2 | Convenience: returns a `Terminal`. |
| `seq(...items)` | * | Convenience: returns a `Sequence`. |
| `alt(...items)` | * | Convenience: returns an `Alternatives`. |
| `parse(parser)` | 1 | Runs the grammar over a `Parser` (see [`parser`](parser.md)). Returns the root `ParseNode` or throws. |
| `toString()` | 0 | Prints all rules in EBNF-like form. |

| Property | Kind | Description |
| --- | --- | --- |
| `rules` | `Map<string, GrammarRule>` | Named rules. |
| `start` | string \| null | Name of the start rule. |

## ParseNode

| Property | Kind | Description |
| --- | --- | --- |
| `rule` | `GrammarRule` | The rule that produced this node. |
| `children` | `ParseNode[]` | Nested parse-tree children. |
| `tokens` | `Token[]` | Every leaf token spanned by this node. |
| `type` | getter | Rule name (or class name if unnamed). |
| `text` | getter | Concatenated lexemes of the spanned tokens. |
| `loc` | getter | `Location` of the first spanned token. |

## ParseContext

`Grammar.parse` wraps a `Parser` in a `ParseContext` internally; you rarely
construct one manually. It buffers tokens and offers cheap back-tracking:

| Method / Property | Description |
| --- | --- |
| `peek(offset = 0)` | Look ahead without consuming. |
| `consume()` | Consume and return the current token. |
| `mark()` / `restore(pos)` | Backtracking primitives. |
| `eof` | Whether the token stream is exhausted. |

## Terminal matchers

`new Terminal(matcher)` accepts:

| `matcher` | Meaning |
| --- | --- |
| `string` | Matches when `token.type === matcher` **or** `token.lexeme === matcher`. |
| `RegExp` | Matches when `matcher.test(token.lexeme)` is true. |
| `function` | Matches when `matcher(token)` returns truthy. |

## Example — fluent method-style CSV parser

```js
import CSVLexer from './lib/lexer/csv.js';
import Parser from './lib/parser.js';
import Grammar, { t } from './lib/parser/grammar.js';

// row = field (separator field)*
const field = t('field');
const sep   = t('separator');
const nl    = t('nl');

const row = field.then(sep.then(field).many()).as('row');
const csv = row.then(nl.then(row).many()).then(nl.optional()).as('csv');

const g = new Grammar();
g.define('csv', csv);

const parser = new Parser(new CSVLexer('a,b,c\n1,"hi, there",2\n', 'x.csv'));
const tree = g.parse(parser);
console.log(tree.type);           // "csv"
```

Renders as:

```
csv = "field" ("separator" "field")* ("nl" "field" ("separator" "field")*)* "nl"?
```

## Example — driven from a `.y` file via `buildGrammar`

```js
import { buildGrammar } from './lib/parser/ebnf.js';
import Parser from './lib/parser.js';
import BNFLexer from './lib/lexer/bnf.js';
import { readFileSync } from 'fs';

const src = readFileSync('tests/Shell-Grammar.y', 'utf-8');
const g   = buildGrammar(src, 'Shell-Grammar.y');
console.log(g.start, g.rules.size, [...g.declaredTokens].length);
```

`buildGrammar` understands `%token`, `%start`, alternatives (`|`), `?`, `*`,
`+`, single-quoted literals, and skips `{ … }` action blocks. Undeclared
identifiers referenced but never defined on the LHS are promoted to
`Terminal`s so the grammar remains parseable when the source omits
declarations.

## Notes and caveats

- The engine is recursive-descent / PEG-style. **Left recursion is not
  supported** — rewrite `list : list sep item | item` as
  `list : item (sep item)*` (or use right recursion) before feeding it
  through `buildGrammar`.
- Alternatives commit to the first successful branch; order matters.
- `ZeroOrMore` / `OneOrMore` break out of their loops if a child matches
  without consuming any tokens, so `Empty.many()` cannot run forever.
- `parse(parser)` throws on failure with the offending token's `Location`
  in the message.
