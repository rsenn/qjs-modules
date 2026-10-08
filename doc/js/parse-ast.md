# parse-ast

Source: `utilities/parse-ast.js` (installed to `bin/`), `lib/parser/{earley,cfg,scan}.js`

Parses source files with a grammar and prints the syntax tree as JSON. The
grammar can be BNF, EBNF, ANTLR 4, yacc/bison/jison, with a lex/flex file for
the tokens where the grammar needs one.

Rationale: the grammar files already parse into an AST (see
[grammar-parsers](grammar-parsers.md), [lex](lex.md)); this runs them. One
Earley parser handles every dialect, so left recursion and ambiguity need no
grammar rewriting. Nothing here is a standard API.

## Usage

```sh
parse-ast [OPTIONS] <grammar-files...> [--] <sources...>

parse-ast JSON.g4 -- data.json
parse-ast JavaScriptLexer.g4 JavaScriptParser.g4 -- app.js
parse-ast ANSI-C-grammar-2011.l ANSI-C-grammar-2011.y -- main.c
parse-ast ecmascript.jison -- app.js            # %lex block inside
parse-ast -w calc.bnf -- expr.txt               # characters, whitespace skipped
```

Without `--`, the arguments with a grammar extension are the grammars.

| Option | Description |
| --- | --- |
| `-s, --start RULE` | start rule; default `%start` (yacc) or the first parser rule |
| `-c, --compact` | replace a node whose only child is a node by that child |
| `-L, --no-loc` | leave out `loc` |
| `-T, --no-text` | leave out `text` of nodes (tokens keep it) |
| `-w, --skip-ws` | `.bnf`/`.ebnf`: drop whitespace from the source |
| `-I, --ident-token N` | token for a lex rule that returns a function call (default `IDENTIFIER`) |
| `-f, --format FMT` | `json` (default) or `js`: `inspect(ast, {reparseable: true, colors: false, maxArrayLength: Infinity, maxStringLength: Infinity})` |
| `-i, --indent N` | JSON indent (default 2) |
| `-o, --output FILE` | write there instead of stdout |

Output: one source gives the tree; several give `[{file, ast}]`. A parse error
goes to stderr as `file:line:column: unexpected 'x', expected ...` and the
exit status is 1.

## Tree

```json
{ "type": "pair", "label": "Alt", "text": "\"a\": 1",
  "loc": {"start": {"offset": 1, "line": 1, "column": 2}, "end": {"offset": 7, "line": 1, "column": 8}},
  "children": [ {"type": "STRING", "text": "\"a\"", "loc": {}}, ... ] }
```

A node is a rule (`type`, `label` of a g4 `# Label`); a token has the type the
lexer gave it. Helper rules from `( )`, `[ ]`, `* + ?` are not in the tree: their
children belong to the enclosing rule.

## Where the tokens come from

| Grammar | Tokens |
| --- | --- |
| `.bnf`, `.ebnf` | characters (`type` is `char`); literals are matched character by character |
| `.y` / `.jison` | a given `.l`/`.jisonlex`, else the `%lex` block; `return NAME;`, `return 'x';` and `return "x";` give the token, `return yytext[0]` the text, any other call the `-I` token |
| `.g4` | its lexer rules (also from a separate `Lexer.g4`): longest match, then rule order; `-> skip`, `channel()`, `type()`, `pushMode()`, `popMode()`, `mode()`; quoted literals of the parser rules become tokens unless a rule is exactly that literal |

flex matches the longest rule, jison the first one (unless `%options flex`).

## Ambiguity

Earley finds every parse; the tree keeps one:

| Grammar | Rule |
| --- | --- |
| yacc | `%left %right %nonassoc` and `%prec` as bison; the lower level is nearer the root; otherwise the earlier rule alternative, and the longer inner match (`else` binds to the nearest `if`) |
| g4 | an earlier left-recursive alternative binds tighter; `<assoc=right>` is honored |
| bnf, ebnf | the earlier alternative |

## Limits

- Lexer actions are C or JS code and are not run: no typedef-name feedback
  (`IDENTIFIER` for every name unless the grammar copes), no `yymore`, no
  lexer predicates, no `more` command, no indentation tokens or semicolon insertion.
- g4 parser predicates and actions are ignored; `~`, `.` and sets work in lexer
  rules, `~` over tokens in parser rules.
- `a - b` of EBNF works between single-character items; `? ... ?` never matches.
- Earley is cubic in the worst case: a C file of 300 lines takes about 2 s.
