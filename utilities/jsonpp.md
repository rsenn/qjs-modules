# jsonpp

JSON pretty-printer / minifier for `qjsm` (`utilities/jsonpp.js`).

```sh
jsonpp.js [options] [file...]     # no file or '-': stdin
```

| Option | Meaning |
| --- | --- |
| `-i, --indent=NUM` | spaces per nesting level (default 2) |
| `-c, --compact` | single-line output (same as `--indent=0`) |
| `-o, --output=FILE` | write to FILE instead of stdout |
| `-w, --write` | rewrite each input file in place, atomically (temp file + rename); a file that fails to parse is left alone |
| `-b, --bare-keys` / `-s, --single-quotes` / `-x, --hex-numbers` / `-m, --minify` | JSON5 output options of `JsonWriter` |

## How it works

`JsonParser` pulls bytes with `os.read()` and emits tokens that go straight to a
`JsonWriter`; the document is never held in memory, and comments are kept.

- **Streaming:** stdout is flushed just before each blocking read, so output for the
  front of a document appears while the rest is still arriving (e.g. from a slow pipe).
  `--write` buffers the whole output instead.
- **Numbers:** plain integers pass through exactly (arbitrary size); floats and
  exponents are normalized through `parseFloat` (`1e3` becomes `1000`).
- **Output lag:** a value is written together with its following comma, i.e. when
  the next element starts.
