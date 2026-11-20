# yaml

Source: `quickjs-yaml.c` (module), `src/yaml/ywrite.c` + `include/yaml/ywrite.h` (writer) — flat function list.

Parses YAML into a plain JS value tree (`parse`, needs CYAML), and serializes one back to a small,
deterministic block-YAML subset (`write`, `stringify`): nested mappings/sequences via indentation,
plain or quoted scalars. The writer has no anchors/aliases, no multi-document streams, no flow style
(except the two exceptions noted below), no custom tags. Written for the eagle-agent library indexer
(`../eagle-agent.md`), which needs a compact, human- and LLM-readable source-of-truth text for the
EAGLE parts catalog — but it's general-purpose for any plain data tree.

## Build: CYAML is optional

`parse` is parsed by [CYAML](https://github.com/andrewmd5/cyaml) (YAML 1.2, MIT, no dependencies).
The module builds without it and then exports only `write` and `stringify`; a script that needs
`parse` can test `typeof yaml.parse`.

| `-DBUILD_LIBCYAML=` | CYAML | `parse` |
| --- | --- | --- |
| `AUTO` (default) | the installed one (`cyaml.h` + `libcyaml`), else none | only if found |
| `ON` | built from `third_party/cyaml` (cloned from git on first configure) | yes |
| `OFF` | not used | no |

`DEPS_BUILD=CMAKE` or `ALL` also builds a missing CYAML. `CYAML_PREFIX`, `CYAML_INCLUDE_DIR` and
`CYAML_LIBRARY_DIR` point at an installed copy that is not in the default paths.

## Functions

| Function | Args | Description |
| --- | --- | --- |
| `parse(text)` | 1 | YAML text to a JS value. Several documents (`---`) give an array of them, an empty text gives `null`. Only with CYAML. |
| `stringify(value, replacer?, space?)` | 1 | As `write()`, with `Bun.YAML.stringify`'s signature: `space` is a number of spaces per level or a string whose length is used (max 10), default 2. A `replacer` other than `null`/`undefined` throws `TypeError`. |
| `write(value, indent?)` | 1 | Serializes `value` to a YAML string. `indent` is the number of spaces per nesting level, default `2`. Non-positive values fall back to the default. |

`stringify` differs from Bun in the output: Bun writes flow style (`{a: 1}`) when `space` is omitted,
this module always writes block style.

### `parse()` rules

| YAML | JS |
| --- | --- |
| plain `null`, `~`, empty | `null` |
| plain `true`/`false` (any case) | boolean |
| plain `12`, `0x1F`, `0o17`, `1.5e3`, `.inf`, `.nan` | number (an integer past int64 is a double) |
| quoted, block (`\|`, `>`) scalar, or a plain one with a tag other than the types below (`!!str 12`) | string |
| plain `!!int 5`, `!!float 1`, `!!bool true` | number, number, boolean (a text that is not one stays a string); `!!null` keeps the text's own type |
| mapping | plain object, keys in document order, a key is its text (`1:` is `"1"`, `~:` is `"null"`) |
| sequence | array |
| alias `*a` | **the same JS value** as the anchored node (so `r.y === r.x`, and `&a {b: *a}` is a cycle) |

Merge keys (`<<`) are an ordinary key, as in YAML 1.2. A mapping key that is not a scalar throws
`TypeError`; a syntax error throws `SyntaxError` with `line N, column M`; a non-string argument
throws `TypeError`.

```js
import { parse } from 'yaml';

parse('a: 1\nb: [x, true, ~]\n'); // { a: 1, b: ['x', true, null] }
parse('a: 1\n---\nb: 2\n');      // [{ a: 1 }, { b: 2 }]
```

## Translation rules

| JS value | YAML output |
| --- | --- |
| `null`, `undefined` | `null` |
| `true` / `false` | `true` / `false` |
| number | its JS string form (`42`, `3.14`, …) — unquoted |
| string | plain if unambiguous, else double-quoted (see below) |
| `{}` (empty object) | `{}` |
| `[]` (empty array) | `[]` |
| non-empty object | block mapping, keys in **JS insertion order** |
| non-empty array | block sequence |
| symbol, BigInt | throws `TypeError` |
| function | `{}` (a known quirk, see `BUGS`) |

**Empty `{}`/`[]` are the one deliberate flow-style exception** — block
style has no way to represent zero entries, so an empty collection is
always written as the flow literal, at any depth.

### Nesting

Every level of nesting adds one `indent`-width block of leading spaces.
A key's value starts on the same line if it's a scalar or empty
collection; a non-empty nested object/array starts on the **next** line,
indented one level deeper than the key. Array items that are themselves
non-empty objects/arrays are written as a bare `-` on its own line,
followed by the nested block indented one level deeper than the `-`:

```yaml
key: 1
list:
  - a
  - b
nested:
  a: 1
  b:
    - 1
    - 2
listOfMaps:
  -
    x: 1
  -
    y: 2
```

from:

```js
write({
  key: 1,
  list: ['a', 'b'],
  nested: { a: 1, b: [1, 2] },
  listOfMaps: [{ x: 1 }, { y: 2 }],
});
```

### Scalar quoting

A string is written **plain** (bare, no quotes) unless any of these hold,
in which case it's written **double-quoted** with JSON-style escapes
(`\"`, `\\`, `\n`, `\t`, `\r`, `\xHH` for other control bytes):

- empty string
- leading or trailing space
- starts with a YAML indicator character: `- ? : , [ ] { } # & * ! | > ' " % @` \` `` ` or whitespace
- contains `: ` (colon-space) or a bare trailing `:`, or ` #` (space-hash) — these are structurally significant in YAML
- contains any control character (byte < 0x20)
- looks like a number (`123`, `-1.5e10`, `0x1F`, `0o17`, `.inf`, `.nan`, …) — quoted so it stays a string on read-back
- matches a YAML 1.1 boolean/null keyword, case-sensitively against `null`/`Null`/`NULL`/`~`, `true`/`True`/`TRUE`, `false`/`False`/`FALSE`, `yes`/`Yes`/`YES`, `no`/`No`/`NO`, `on`/`On`/`ON`, `off`/`Off`/`OFF`

Everything else — including a string that merely *contains* a `"` or `'`
mid-word (`a "quote" here`) — is left unquoted; those characters are only
special at the start of a scalar.

**Practical implication for pin names:** EAGLE pin names like `RA0/AN0` or
`RB7/KBI3/PGD` are plain (no leading indicator char, no `: `/`#`, not
numeric/keyword-shaped) and come out unquoted. A bare pad number used as a
pin name (e.g. `"1"`, `"2"` for a resistor) looks numeric and **will** be
quoted — keep that in mind if a consumer expects a raw string.

## Worked example: an EAGLE parts-catalog entry

Given a condensed deviceset record built as a plain JS object (matching
the fields `eagle-agent.md` §1 wants — name, prefix, description, gate →
pin list, package variants):

```js
import { write } from 'yaml';

const catalog = {
  'mcu.lbr': {
    'PIC18F2550-I/SP': {
      prefix: 'U',
      packages: ['SPDIP28'],
      pins: ['RA0/AN0', 'RA1/AN1', 'VDD', 'VSS'],
    },
  },
  'r.lbr': {
    R: { prefix: 'R', pins: ['1', '2'] },
  },
};

print(write(catalog));
```

produces:

```yaml
mcu.lbr:
  PIC18F2550-I/SP:
    prefix: U
    packages:
      - SPDIP28
    pins:
      - RA0/AN0
      - RA1/AN1
      - VDD
      - VSS
r.lbr:
  R:
    prefix: R
    pins:
      - "1"
      - "2"
```

Note the quoted `"1"`/`"2"`: those pin names are digit-only strings, so
the numeric-lookalike rule quotes them to keep them strings.

## Caveats for callers

- Build the plain object/array tree first (e.g. from `eagle.js`'s
  `EagleDocument` getters), then hand the finished tree to `write()` in
  one call — there's no streaming/incremental writer.
- Key order is whatever order you assigned object properties in; if
  ordering matters for the catalog's readability (e.g. `prefix` before
  `pins`), build the object literal in that order.
- `write()` throws on symbols and BigInts in the tree — strip or convert
  those before calling it.

## Export list

Generated by `utilities/doc-api.js` from the `yaml` dump of `utilities/api-surface.sh`: 3 members. Edit the text above this block, not the block.

**Functions:** `parse()`, `stringify()`, `write()`

