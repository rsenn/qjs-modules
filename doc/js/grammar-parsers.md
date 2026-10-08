# grammar-parsers

Source: `lib/parser/bnf.js`, `ebnf.js`, `g4.js`, `yacc.js` (pure JS, no native imports)

Parsers that turn grammar *files* into an AST: BNF, EBNF (ISO, W3C and
ANTLR-style), ANTLR 4 `.g4`, and yacc/bison/jison `.y`/`.jison`. They are small
on purpose: one sticky regexp per token kind and a recursive-descent parser;
`ebnf.js` extends `bnf.js`, `g4.js` and `yacc.js` extend `ebnf.js`.

Rationale: the existing `parser/grammar-compile.js` turns a grammar straight
into running combinators and keeps no tree of the file. These give the tree
itself, for tools (conversion, listing, linting); [parse-ast](parse-ast.md) runs them. Nothing here is a standard API.

Together with `parser/lex.js` (see [lex](lex.md)) they read every file in
`~/Sources/plot-cv/lib/grammars/` (62 `.g4`, 21 `.y`, 3 `.jison`, 1 `.ebnf`,
22 `.l`/`.jisonlex`); `tests/unittests/test-grammar-parsers.js` checks that.

## Usage

```js
import { parseBNF } from 'parser/bnf.js';
import { parseEBNF } from 'parser/ebnf.js';
import { parseG4 } from 'parser/g4.js';
import { parseYacc } from 'parser/yacc.js';

const grammar = parseG4(text, 'JSON.g4'); // throws SyntaxError 'JSON.g4:3:7: ...'
```

Each module also exports its class (`BNFParser`, `EBNFParser`, ...) and a
default export, the `parse*` function.

## AST

Every node is a plain object with a `type`. A grammar is
`Grammar > Rule > Choice > Sequence > item`:

| Node | Fields | Where |
| --- | --- | --- |
| `Grammar` | `rules`; g4: `kind`, `name`, `decls`; yacc: `decls`, `epilogue` | all |
| `Rule` | `name`, `body` (a `Choice`); g4: `fragment`, `args`, `returns`, `locals`, `throws`, `options`, `actions`, `handlers`; yacc: `alias` | all |
| `Choice` | `alternatives`: `Sequence[]` | all |
| `Sequence` | `items`; g4: `label` (`# Name`), `commands` (`-> skip`) | all |
| `Terminal` | `value` (text between the quotes, escapes kept), `quote` | all |
| `Nonterminal` | `name`; g4: `args`; yacc: `alias` | all |
| `Group` / `Optional` | `body` (a `Choice`) | ebnf, g4, yacc |
| `Repeat` | `op` (`*` `+` `?`), `item`; ISO: `count`; g4: `greedy` | ebnf, g4, yacc |
| `Except` | `item`, `except` (`a - b`) | ebnf |
| `CharClass` | `text` (`[a-z]`) | ebnf, g4 |
| `Char` | `code` (`#x41`) | ebnf |
| `Special` | `text` (`? any text ?`) | ebnf (ISO) |
| `Range`, `Wildcard`, `Not`, `Label`, `Action`, `Predicate`, `Option`, `Command` | see `g4.js` header | g4 |
| `Mode`, `Options`, `Tokens`, `Channels`, `Import`, `ActionDecl`, `Handler` | | g4 |
| `Prologue`, `Lex`, `Directive`, `Tag`, `Annotation`, `Action` | see `yacc.js` header | yacc |

## What each dialect accepts

| Module | Accepts |
| --- | --- |
| `bnf.js` | `<name>` or `name`, `::=` `:=` `:` `=`, `"x"` and `'x'`, `\|`, optional `;` or `.`; a rule also ends where the next `name ::=` starts |
| `ebnf.js` | adds `( )`, `* + ?`, `[a-z]`, `#x41`, `a - b`; ISO form (`=`, `,`, `{ }`, `[ ]`, `n * x`, `? text ?`, names with spaces, `(* *)` comments) when rules use `=` and never `::=` |
| `g4.js` | `lexer`/`parser grammar`, `options`, `tokens`, `channels`, `import`, `@members`, `mode`, `fragment`, rule arguments, labels `a=b` `a+=b`, `# Label`, `-> commands`, `{actions}` and `{pred}?`, `~`, `.`, `'a'..'z'`, non-greedy `*?`, `catch`/`finally`; a missing `;` before the next rule is tolerated |
| `yacc.js` | `%{ %}`, `%token`/`%type`/`%left`/... (name plus raw arguments), `%%`, rules with `{actions}`, `%prec`/`%empty`/`%dprec`, `[alias]`, `<tag>`, the epilogue, jison `%lex ... /lex` (kept as text: `parseLex(lex.text)`) and its EBNF postfix operators |

Notes:
- Code (`{...}`, `%{...%}`, the epilogue) is never parsed, only found: braces
  inside strings, char literals and comments do not end a block.
- Strings keep their escapes as written; nothing is unescaped.
- `ebnf.js` picks ISO by looking at the text (`=` rules, no `::=`); set
  `static iso = true/false` on a subclass to force it.
