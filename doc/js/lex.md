# lex

Source: `lib/lexer/lex.js` (tokens), `lib/parser/lex.js` (AST and what it becomes)

Reads lex/flex `.l` files. `LexLexer` tokenizes the source (it is what
`lexers.js` uses for `.l` files, so `utilities/highlight.js` colors them);
`parseLex()` builds an AST that prints back the exact text it was parsed from
and turns into a working `lexer` rule set.

Rationale: flex files describe a scanner declaratively, so the same file can
drive a `Lexer` directly instead of being translated by hand. Nothing here is
a standard API; flex itself is the format being read.

## LexLexer

```js
import LexLexer from 'lexer/lex.js';

for(const tok of new LexLexer(src, LexLexer.FIRST, 'a.l')) console.log(tok.type, tok.lexeme);
```

Token types, by section:

| Section | Types |
| --- | --- |
| any | `codeblock` (`%{ ... %}`), `comment`, `indented` (an indented line, copied code), `section` / `user_section` (`%%`) |
| definitions | `directive` (`%option`, `%x`, `%s`, ...) followed by `d_word`, `d_string`, `d_number`, `d_equals`; `def_name` + `def_regex` for `NAME  pattern`; `top` (`%top{ }`) |
| rules | `scope` / `scope_end` (`<A>{` ... `}`), `rule_comment`, `condition` (`<A,B>`), `regex` (the pattern), `eof` (`<<EOF>>`), `action` (balanced `{ }`), `action_line` (rest of line), `action_fallthrough` (`|`) |
| user code | `user` (everything after the second `%%`) |

Whitespace and newlines are skipped; `highlight.js` fills them back in.

## parseLex

```js
import { parseLex } from 'parser/lex.js';

const file = parseLex(text, 'scan.l'); // LexFile
String(file) === text; // true, for any file that parses
```

| `LexFile` member | Description |
| --- | --- |
| `definitions` | `Code`, `Directive` and `Definition` items of section 1 |
| `rules` | `Code` and `Rule` items of section 2 |
| `user` | text after the second `%%`, or `null` |
| `states` | `[[name, exclusive]]` from the `%x` / `%s` lines |
| `toString()` | the `.l` text again; each item keeps the whitespace before it (`lead`) |
| `compile()` | `{defines, states, rules}` as plain data |
| `createLexer(input, fileName, mode)` | a `Lexer` with the rules; `Lexer.LONGEST` by default |
| `scan(input, fileName)` | generator of tokens: `for(const tok of file.scan(src))` |
| `toLexerSource(className)` | JS module source: a `Lexer` subclass like those in `lib/lexer/` |

`Rule` has `conditions` (`null` or names), `pattern` (a regex node) and
`action` (the text). A pattern is a tree of `Alt`, `Seq`, `Literal`, `Char`,
`CharClass`, `Any`, `Ref`, `Group`, `Repeat`, `Bol`, `Eol`, `Slash` (trailing
context) and `Eof`; `String(node)` is flex source, `node.toJS()` the same
pattern for the `lexer` module. `parseRegex(src)` parses one pattern.

## What the generated lexer does

Actions are C code and cannot run, so only these are understood:

| In the action | Effect |
| --- | --- |
| `return NAME;`, `return(NAME);` | the rule is named `NAME`, its match is a token |
| no `return` | the match is skipped |
| `BEGIN(S)` | `lexer.state = 'S'` |
| `yy_push_state(S)`, `yy_pop_state()` | `pushState` / `popState` |
| `|` instead of an action | shares the next rule's action |

Other behavior:
- Rule names must be unique: a repeated token is `NAME_2`, a rule that returns
  something else is `rule_<n>` (or its quoted word, e.g. `auto`).
- A rule without `<STATE>` is active in `INITIAL` and the `%s` states; with
  `<*>` in all of them; `%x` states only where named.
- Rules compete longest match first, then the earlier rule (flex), which is
  `Lexer.LONGEST`. Inside one pattern JS alternation is leftmost-first, so
  `a|ab` differs from flex; put the longer alternative first.
- `<STATE>{ ... }` blocks give their rules that start condition.
- A file that does not parse as flex is read again with jison's single-quoted
  patterns (`'"'[^"]+'"'`).
- `<<EOF>>` rules are dropped; `^` is "after a newline", `$` "before a
  newline", `r/s` is `r(?=s)`.
- `%option case-insensitive`, `yymore`, `yyless` and similar are not applied.
