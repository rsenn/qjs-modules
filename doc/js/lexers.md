# lexers

Source: `lib/lexers.js` (wraps the native `lexer` module and `lib/lexer/*.js`)

Picks the lexer that fits a file name or a language name, so a tool does not
carry its own extension table. Used by `utilities/highlight.js` and
`utilities/extract-comments.js`.

Rationale: `highlight.js` and `extract-comments.js` both need "which lexer for
this file"; keeping one table avoids two copies drifting apart. Nothing here is
a standard API.

## Exports

| Export | Kind | Description |
| --- | --- | --- |
| `Lexers` | object | Language name → factory `(source, fileName) => lexer`. Keys: `js`, `c`, `bnf`, `csv`, `xml`, `sh`, `cmake`, `make`, `ini`. |
| `languageFor(file)` | function | Language name of a file, or `undefined`. Looks at the base name first (`Makefile`, `GNUmakefile`, `CMakeLists.txt`, `*.ini`, `*.mcw`, `*.mcp`), then at the extension. |
| `lexerFor(file, language = languageFor(file))` | function | The factory for a file, or for `language` when given; `undefined` if there is none. |

Extensions that map to another language: `h hpp hh hxx cc cpp cxx` → `c`,
`mjs cjs json ts` → `js`, `g4 ebnf l y` → `bnf`, `html htm svg` → `xml`,
`bash` → `sh`, `mk mak` → `make`.

## Example

```js
import { languageFor, lexerFor } from 'lexers';

languageFor('src/a.cpp'); // 'c'
languageFor('Makefile'); // 'make'

const make = lexerFor('a.js');
const lexer = make('let a = 1; // one', 'a.js');

for(let tok; (tok = lexer.nextToken()); ) console.log(tok.type, tok.lexeme);
```

## Notes

- Build one lexer per input and let it go out of scope before the next;
  `lexer.so` is not designed for many lexers sharing one input.
- The ECMAScript lexer cannot tell a division from a regexp literal on its
  own: a consumer sets `lexer.state = 'NOREGEX'` after a token that ends an
  expression (see `utilities/extract-comments.js`).
- `lexer/bnf.js` scans BNF (`<rule> ::= ...`), ISO EBNF (`{ }` repeat, `[ ]`
  option, `? ? ` special sequence, `(* *)` comments), W3C EBNF (`#x20`,
  `[a-z]`, `/* */`) and yacc/ANTLR files. Comments are tokens (`/* */`, `//`,
  `#`, `(* *)`). The file name picks the dialect: `.y .yy .l .ll .g4` keep
  `{ ... }` as embedded code (and the text after the second `%%` of a `.y`
  file is C); anything else is plain EBNF, where `{ }` and `[ ]` are
  punctuation. flex `.l` files are not supported.
