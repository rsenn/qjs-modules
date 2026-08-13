# lexer

Source: `quickjs-lexer.c` — module exports **`Lexer`**, **`Token`** (and `Location`, see [location](location.md))

A configurable, rule-based tokenizer. Rules are regex-driven and grouped into
named states; the lexer yields `Token` objects.

## Lexer

```js
new Lexer(input[, fileName, mode])   // length 1
```

### Scanning methods

| Method | Args | Description |
| --- | --- | --- |
| `peek()` | 0 | Returns the next token id without consuming. |
| `peekToken()` | 0 | Returns the next `Token` without consuming. |
| `next()` | 0 | Consumes and returns the next token id. |
| `nextToken()` | 0 | Consumes and returns the next `Token`. |
| `lex()` | 0 | Performs one lexing step. |
| `setInput(input, fileName)` | 1 | Replaces the input being scanned. |
| `skipBytes(n)` / `skipChars(n)` | 0 | Advance the cursor by bytes / chars. |
| `skipToken()` | 0 | Discards the next token. |
| `skipUntil(predicate)` | 1 | Skips until a condition holds. |
| `back()` | 0 | Pushes the last token back. |
| `currentLine()` | 0 | Returns the text of the current line. |
| `tokenClass(id)` | 1 | Returns the class/name for a token id. |

### Rule & state management

| Method | Args | Description |
| --- | --- | --- |
| `define(name, pattern)` | 2 | Defines a named sub-pattern. |
| `addRule(name, pattern)` | 2 | Adds a token rule. |
| `getRule(id)` | 1 | Looks up a rule. |
| `pushState(state)` | 1 | Enters a lexer state (alias `begin`). |
| `popState()` | 0 | Leaves the current state (alias `end`). |
| `topState()` | 0 | Returns the active state. |

### Properties

| Property | Kind | Description |
| --- | --- | --- |
| `size` | getter | Input size in bytes. |
| `charPos` | getter/setter | Cursor position in characters. |
| `bytePos` | getter | Cursor position in bytes. |
| `loc` | getter | Current `Location`. |
| `eof` | getter | Whether the end of input is reached. |
| `mode` | getter/setter | Lexer mode flags. |
| `seq` | getter | Token sequence counter. |
| `byteLength` / `charLength` | getter | Remaining input length. |
| `state` | getter/setter | Active state. |
| `states` | getter | All defined states. |
| `stateDepth` / `stateStack` | getter | State-stack inspection. |
| `input` | getter | The input text. |
| `lexeme` | getter | The most recent lexeme. |
| `fileName` | getter/setter | Source file name. |
| `ruleNames` / `rules` | getter | Defined rule names / rules. |
| `tokens` | getter | Collected tokens. |

`[Symbol.iterator]` yields tokens, so a `Lexer` can drive `for…of`.

### Static functions

| Function | Args | Description |
| --- | --- | --- |
| `escape(str)` | 1 | Escapes regex/lexer metacharacters. |
| `unescape(str)` | 1 | Reverses `escape`. |
| `toString(value)` | 1 | String form helper. |
| `fromFile(path)` | 1 | Creates a lexer reading from a file. |

## Token

```js
new Token(...)   // length 1
```

| Property | Kind | Description |
| --- | --- | --- |
| `charLength` / `byteLength` | getter | Lexeme length in chars / bytes. |
| `charRange` / `byteRange` | getter | `[start, end]` ranges. |
| `charPos` / `bytePos` | getter | Start offsets. |
| `loc` | getter/setter | `Location` of the token. |
| `id` | getter | Numeric token id. |
| `seq` | getter | Sequence number. |
| `type` | getter | Token type/rule name (enumerable). |
| `lexer` | getter | Owning lexer. |
| `rule` | getter | The matched rule. |
| `lexeme` | getter | The matched text (enumerable). |

Methods: `toString()`, `[Symbol.toPrimitive]()`.
