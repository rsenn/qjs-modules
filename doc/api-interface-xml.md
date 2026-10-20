# Plan: the `xml` interface against Browser, Bun, Node and Deno (2026-10-17)

> **Status 2026-10-18 (implemented).** The event model is in: every class is a source or sink over `JsonEvent` / `XmlEvent`, `pump()`/`connect()`/`tee`/`JsonBuilder`/`XmlBuilder` exist, `parse()` and `XMLNodeParser` are removed (`next()` and the iterator replace them), `xml.write()` is `xwalk` pumped into an `XmlWriter` (`XW_PRETTY`, indent 2), `quickjs-json.c` is 933 and `quickjs-xml.c` 967 lines.
> Running state and what is left: [event-model-state.md](event-model-state.md). Text below that describes `parse()`, `XMLNodeParser`,
> `eventName`/`eventValue`, `PARSE_*`, `xtreewrite` or `xlayout` is the plan or the history, those are gone.

Goal: the JS surface of `import ... from 'xml'` follows the standards in this order: WHATWG > Browser > Bun > Node >
Deno (`CLAUDE.md`), keeps the `tagName`/`attributes`/`children` tree that `lib/dom.js` is built on, and adds no custom
public API. Findings are in [api-compare-xml.md](api-compare-xml.md), the code split in
[api-implement-xml.md](api-implement-xml.md); the step list that orders all three is `TODO.md` Tier 20. Sections 4 to 10 below
are the reference: the C contracts between the pieces and the JS contract of each class.

**Layering (plan 2026-10-18, [api-event-model.md](api-event-model.md)).** The `xml` native module exports **engines only**: pull and
push parsers, builder (tree and flat), walker, writer, serializer, sink adapters, all sources or sinks of `XmlEvent`. `read`, `write`,
`DOMParser`, `XMLSerializer`, `XML.parse`, `XML.stringify` and the `XMLNodeParser` shape are thin compositions of those engines in JS
(`lib/xml.js`, `lib/dom.js`, `lib/bun.js`): "Lives in" below. A flat parse is `XMLParser({flat: true})` pumped into a flat `XmlBuilder`
(a list of element objects, an end is a `{tagName: '/x'}` entry), entirely in C. Sections 4 to 8 describe the contracts as they are; the
event model marks what changes (pull parser writes an `XmlEvent`, push parser calls an `XmlEvent` handler, walker is a pull source that
speaks `XmlEvent`, serializer is demand-driven).

## 1. Target interface

| Export | Target | Standard it follows | Action | Lives in |
| --- | --- | --- | --- | --- |
| `read(input, name?, options?)`, `write(tree, maxDepth?)` | as now; same tree as `txml` | none (Browser has the DOM, not a plain tree) | keep the API, move the code | `lib/xml.js`: `read` = `XMLParser` pumped into an `XmlBuilder`; `write` = `xwalk` pumped into an `XMLWriter` |
| `XMLParser`, `XMLPushParser` | as now; `XMLParser` gains `flat`, `strict`, `connect`/`pump` | event parsers of the `sax` family | keep as engines | C |
| `XMLNodeParser` | `new XMLParser(input, {flat: true})` into a flat `XmlBuilder`, same list as now | | keep the name as an alias | glue |
| `XMLWriter` | escapes; `attribute(n, true)` writes `n`; compact mode really compact | every package escapes | fix (Tier 20 step 3c) | C |
| `XMLTreeSerializer` (ours, was `XMLSerializer`) | one meaning only; over a tree **or** a pull source | Browser: `new XMLSerializer().serializeToString(node)` | renamed (D3 decided) | C |
| `DOMParser`, `XMLSerializer` (DOM) | `parseFromString(str, type)` returning a `Document`; `serializeToString(node)` | Browser | add by exporting `lib/dom.js` `Parser` / `Serializer` | `lib/dom.js` on `XMLParser` + `XmlBuilder`, `xwalk` + `XMLWriter` |
| `XML.parse`, `XML.stringify` | Bun's object form (`'@name'`, `'#text'`, repeated names as arrays) and node form | `Bun.XML` | add | `lib/bun.js`: a JS sink on `XMLParser(strict)` (M8, `api-event-model.md`); a C builder mode only if measured slow |
| strict option on the parser | Bun's `XML Parse error` rules; DOCTYPE entities | `Bun.XML.parse` | add (Tier 20 phase 9) | C (`xml.c`) |
| XSLT, `XMLHttpRequest`, namespaces-aware DOM | none | Browser | out of scope | |

## 2. Decisions

| # | Decision | Reason |
| --- | --- | --- |
| D1 | the `read()` tree stays as is, `children` omitted on self-closing elements | `lib/dom.js` and `lib/html.js` consume it; only `txml` differs (`children: []`) |
| D2 | `XML.parse` / `XML.stringify` are conversions over the same parser and walker, not a second implementation | the 2000-line standalone version was rejected and shelved in `attic/` |
| D3 | decided 2026-10-17: ours is `XMLTreeSerializer`; `XMLSerializer` stays free for the DOM class | a browser script must not break silently |
| D4 | `XMLWriter` escapes by default, no opt-out until a user asks | only `test-xml.js` uses it; `{raw: true}` would be a custom API |
| D5 | the parser stays lenient by default (HTML); strictness is an option | `lib/html.js`, `examples/html2md.js` parse HTML with it |
| D6 | no Node streams; `XMLTreeSerializer` pull model stays | `CLAUDE.md` |
| D7 | every strict failure is a `SyntaxError` with an own `loc` (a `Location`), `length` and `code`; section 11 | no runtime or library offers one correct structured position (`api-compare-xml.md` 7); one property name, `loc`, on the standard class |
| D8 | `TypeError` for bad argument types (not `ReferenceError`); the lenient mode reports repairs through `onParseError`, it does not throw | `xml.read(5)` throws `ReferenceError` today; HTML parsing never fails |
| D9 | the DOM `DOMParser` follows the browser: no exception for malformed XML, a `<parsererror>` document instead | Browser priority |
| D10 | decided 2026-10-18: no runtime API is C. `read`, `write`, `DOMParser`, `XMLSerializer`, `XML.parse`, `XML.stringify`, `XMLNodeParser` are compositions of engine classes in `lib/` | one set of engines serves every runtime shape; `D2` is the same rule for `XML.*` |
| D11 | decided 2026-10-18: every engine class is a source or a sink, pull or push, over `XmlEvent`; a source reaches a sink in C through `pump(sink)` / `connect(sink)` | `api-event-model.md` 1 and 2 |
| D12 | the typed markup events (`XE_COMMENT`, `XE_DECLARATION`, `XE_INSTRUCTION`, `XE_CDATA`) are produced by the parsers; elements with magic names (`!--`, `!`, `?`) exist only as the builder's `read()` output | sinks (`XMLWriter`, DOM builder) must not parse names to find out what an element is |
| D13 | `XmlEvent` text of error, text, comment, declaration, instruction, cdata is `str` / `len` (aliases of `name` / `nlen`); `value` is attribute-only | same shape as `JsonEvent`, one rule for glue code |

## 3. Steps

Numbering is that of `TODO.md` Tier 20 (phases) plus the interface-only additions marked new.

| Step | Interface change | Needs |
| --- | --- | --- |
| 3c | `XMLWriter` escapes, `true` attribute bare, true compact mode | `xwrite` (done) |
| E1 | strict errors carry `loc`, `length`, `code`; `TypeError` for bad arguments; `XMLPushParser.write()` reports a mismatch | `js_throw_syntax_error()` (`quickjs-location.c`), position in `xread` (section 11) |
| E2 | `XMLParser.error`, `XMLNodeParser.error`; `onParseError` for the lenient mode; `DOMParser` `<parsererror>` | E1, `lib/dom.js` |
| 3d, 4d | none visible | `XW_TREE` layout, `xwalk` adapter |
| D3 | `XMLTreeSerializer` contract settled (new) | decision |
| 5 | `XMLTreeSerializer` on `xwalk` + `xwrite`; may cut `read(n)` on character boundaries | steps 3d, 4d |
| new | `DOMParser`, `XMLTreeSerializer` (DOM) exported from `lib/dom.js` | D3 |
| 2, 1 | none visible: `XMLParser.root` / `XMLPushParser.root` take the `read()` shape | `xbuilder` |
| 9 | `XML.parse`, `XML.stringify`, strict option, DOCTYPE subset fixed | strict mode in `xml.c`; the object shape in the glue (D10), `xbuilder` / `xwalk` object mode only if measured |

The event-model steps P1 to P10 (`api-event-model.md` 10, `TODO.md` "Event model v2") run alongside; the XML ones are P2 (`xml_next(p, &ev)`,
typed markup events), P3 (push handler), P4 (`xwalk_event`: the element interpretation of `xtreewrite.c` moves into the walker), P5 (demand-driven
`XMLTreeSerializer`), P7 (flat `XmlBuilder` for `XMLNodeParser`), P8 (one `xml_options_from_js()`, strict), P9 (`read`, `write` into `lib/xml.js`).

## 4. Reference: the event protocol: what exists and what is missing

JSON and XML each have one vocabulary (`JsonEventType`, `XmlEventType`) shared by reader, builder,
walker and writer. For XML it is `include/xml/xevent.h`; the parsers differ only in how they hand it out:

| `XmlEventType` | `xml_parser_run()` result (`xml.h`, pull) | `xr_callback` argument (`xread.h`, push) | `xwrite` calls (new) |
| --- | --- | --- | --- |
| start tag | `XE_ELEMENT_START` | `XE_ELEMENT_START` | `xwrite_element_start(name)` |
| attribute | `XE_ATTRIBUTE` | `XE_ATTRIBUTE` | `xwrite_attribute(name, value \| NULL)` |
| text | `XE_TEXT` | `XE_TEXT` | `xwrite_text(str)` |
| end tag | `XE_ELEMENT_END` | `XE_ELEMENT_END` | `xwrite_element_end(name \| NULL)` |
| error | return `XE_ERROR` (-1) | `XE_ERROR` | `-XWRITE_E_*` |
| comment, doctype, PI, CDATA | a start event with a magic name (`!-- c --`, `!DOCTYPE html`, `?xml`) followed by an end | same | `xwrite_markup(name)` verbatim, or typed `xwrite_comment/declaration/instruction/cdata` |
| status | `XML_PARSE_OK` (0), `XML_PARSE_AGAIN` (-2) | none | |
| position | `tag_line`, `tag_column`, `loc` | none | |

Planned (Tier 20 phase 1): one `xe_type_t` order shared by both parsers, `xwrite` and `xbuilder`; one string type; one callback
(`xe_callback(type, name, value, user_data)`); an `XMLWriteInterface` of function pointers like `JsonWriteInterface`, returned by
`xwrite_interface(wr)`; the typed markup events, so `xbuilder` can map them back to the magic names for `read()` compatibility.
Until then the two enums stay and each adapter translates.

Done since: `XmlEventType`, `XmlEvent`, `XmlEventSink` (`xevent.h`), `xwrite_sink`, `xbuild_sink`, the pumps. Plan 2026-10-18
(`api-event-model.md` 3.2, 4): the pull parser returns `XmlPull` (`XML_PULL_END | AGAIN | EVENT | FAIL`) and fills an `XmlEvent`
(`xml_next(p, &ev)`), which replaces the `event_name` / `event_value` / `event_has_value` fields and the `XML_PARSE_*` statuses (an
`XE_ERROR` is an event, then `XML_PULL_END`); the push parser calls an `XmlEventHandler` with the same `XmlEvent`, which replaces
`xr_callback` / `xr_str_t`; `xread_pump` / `xml_pump` are then the 3-line loop.

## 5. Reading

### `xml_parser_run` (pull, `xml.h`)

```c
void xml_parser_init(XMLParser*, Reader* reader);   void xml_parser_free(XMLParser*);
void xml_parser_set_tolerant(XMLParser*, int);      void xml_parser_set_implied_end(XMLParser*, int);
void xml_parser_set_flat(XMLParser*, int);          void xml_parser_set_self_closing_tags(XMLParser*, const char* const*);
int xml_parser_run(XMLParser*);
```

| Contract | |
| --- | --- |
| result | `XE_ELEMENT_START/ATTRIBUTE/ELEMENT_END/TEXT` with the data in `event_name`, `event_value`, `event_has_value`; or `XML_PARSE_OK` (clean end), `XML_PARSE_AGAIN` (reader dry, call again), `XE_ERROR` (terminal: mismatched closing tag with `tolerant` off, `event_name` is the tag) |
| data lifetime | `event_name` / `event_value` point into dedicated buffers, valid until the next `xml_parser_run()` |
| value-less attribute | `event_has_value == 0` (`<input disabled>`) |
| modes | `tolerant` (recover from mismatches), `flat` (no open-element stack: every closing tag is an `ELEMENT_END`), `no_implied_end` (do not close `<li>`, `<p>` ... on a sibling), `self_closing_tags` (void elements) |
| position | `tag_line`, `tag_column`: the `<` of the tag the event belongs to; a closing tag's own `<` for the ends it causes |
| truncated input | real EOF with elements open is not an error; no synthetic ends |
| engine | `Location` only |

### `xr_read` / `xr_finish` (push, `xread.h`)

```c
typedef void (*xr_callback)(XmlEventType type, const xr_str_t* name, const xr_str_t* value, void* user_data);
void xr_read(xr_callback cb, const char* chunk, size_t len, void* user_data, xr_state_t*);
void xr_finish(xr_callback cb, void* user_data, xr_state_t*);
```

Chunks may end anywhere; `name` and `value` are valid inside the callback; a value-less attribute has `value == NULL`; `xr_finish`
reports `XE_ERROR` when elements are still open. No tolerant mode, no implied ends, no flat mode, no locations.

## 6. Writing: `XmlWriter` (`xwrite.h`)

Every call returns the bytes written (>= 0) or `-XWRITE_E_*`; `xwrite_error_message(code)` gives the text. No call touches a `JSContext`.

| Call | Writes | Notes |
| --- | --- | --- |
| `xwrite_element_start(name, len)` | `<name`, tag stays open | pushes the name for `element_end` |
| `xwrite_attribute(name, nlen, value, vlen)` | ` name="v"` or ` name` | value escaped (`& < > "`); `XWRITE_E_NOT_IN_TAG` outside a start tag |
| `xwrite_text(str, len)` | escaped text, closing an open tag with `>` | |
| `xwrite_element_end(name, len)` | ` />` if nothing was written since the start, else `</name>` | `name == NULL` uses the open element; another name is `XWRITE_E_MISMATCH`; nothing open is `XWRITE_E_UNMATCHED` |
| `xwrite_markup(name, len)` | `<name>` verbatim, `<?...?>` when the name starts with `?` | the form legacy trees use |
| `xwrite_comment`, `_declaration`, `_instruction`, `_cdata` | `<!--s-->`, `<!s>`, `<?s?>`, `<![CDATA[s]]>` | typed forms of the above |
| `xwrite_finish()` | nothing | `XWRITE_E_UNCLOSED` while elements are open |
| `xwrite_put_escaped(Writer*, str, len, is_attr)` | entities for `& < >` (and `"` if `is_attr`) | the one escaping rule; -1 when the `Writer` refuses |

Options (`XmlWriteOptions`): `layout` and `indent`.

| Layout | Rule | Used by |
| --- | --- | --- |
| `XW_COMPACT` | nothing added | `XMLTreeSerializer`, `XMLWriter` with a negative indent |
| `XW_PRETTY` | newline plus `indent * level` spaces before a tag, never next to text | `XMLWriter` |
| `XW_TREE` (step 3d) | `write()`'s rules: newline after every tag, text inline only for an only child, multi-line text re-indented; takes a children-count hint (`xwrite_children(wr, n)`) | `write()` |

Resumability is not a property of `xwrite`: a refused write is `XWRITE_E_WRITE`. `XMLTreeSerializer` gets its pull behaviour by stepping one event per
`read()` into an unbounded `DynBuf` (the design `JsonSerializer` already uses).

## 7. Walking: `WalkInterface` and `XwalkIterator` (`walk.h`, `xwalk.h`)

`xwalk` reports a tree of elements through the same `WalkInterface` as `jwalk`, configured by two `WalkHooks`:

| Tree part | Walked as |
| --- | --- |
| an element object | exactly the keys `tagName`, `attributes`, `children`, in that order, each read with a plain property get (prototype getters work), skipped when undefined |
| `attributes` | an object, by its own enumerable keys; its values are values, never containers |
| `children` | an array, by index; an object item is an element, anything else a value; a nested array is a value |
| a root array | a list of elements and texts |
| every other property | never read (`parentNode`, `ownerDocument`, `line` cannot cause a cycle) |

```c
XwalkIterator it;  xwalk_init(&it, ctx, tree, iface, NULL /* XwalkShape: tag, attributes, children */);
while((r = xwalk_next(&it, ctx)) > 0) {}   xwalk_close(&it, ctx);
```

Planned (step 4d): an adapter that turns these walk events into `xwrite` calls (the table is in `TODO.md` Tier 20 phase 4), reading the
children count at `array_start` and using one event of lookahead for the flat-list rule (self-closing iff the next entry is the element's own `/x` marker).

Plan 2026-10-18 (`api-event-model.md` 5): that adapter is `xwalk_event(&src, &ev)`, a **pull source of `XmlEvent`**, the same translation
(`xtreewrite.c` today) living in the walker. `xtreewrite` shrinks to a pump into `xwrite`, `XMLTreeSerializer` is the walker behind the
demand-driven serializer, and `js_walk_*` for XML is replaced by the sink methods of `js_xmlsink_define`.

## 8. Building: planned `xbuilder` on `jbuilder`

| XML event | `jbuild_*` calls |
| --- | --- |
| element start | `push(object)`, `key("tagName")`, `value(string)`; position as `line`, `column` when asked |
| first attribute | `key("attributes")`, `push(object)` |
| attribute | `key(name)`, `value(string)`; value-less: `value(true)` |
| first child or text | close `attributes`; `key("children")`, `push(array)` |
| text | `value(string)` |
| element end | pop open `children` / `attributes`, then the element; a self-closing element has no `children` |

Needs from `jbuilder`: a `proto` option (plain objects: `lib/dom.js` calls `hasOwnProperty`) and `jbuild_value_js()`.

Done: `xbuilder` exists (`XmlBuilder` on `JsonBuilder`, `XB_PLAIN`, `XB_FLAT`). Plan 2026-10-18: `XB_FLAT` is what `XMLNodeParser` uses (P7), and the
typed markup events replace the `push(object)` + magic `tagName` encoding of comments and declarations inside the builder only.

## 9. The JS-facing contract

| Class / function | Constructor | Methods and properties | Errors |
| --- | --- | --- | --- |
| `read(input, inputName?, options?)` | n/a | options: a boolean (`flat`) or `{flat, tolerant, location, selfClosingTags}`; with `location` the result is `[tree, locationMap]` | `ReferenceError` for a missing or empty input; `SyntaxError` for a mismatched closing tag unless `tolerant` |
| `write(tree \| list, maxDepth?)` | n/a | | |
| `XMLParser(input, options?)` | buffer, string or reader; options carry callbacks and filename | `parse()` returns an event id; `eventName`, `eventValue`, `hasValue`, `depth`, `location`, `tolerant`, `root`; `onelementstart`, `onelementend`, `onattribute`, `ontext`; static `PARSE_AGAIN`, `PARSE_ERROR`, `PARSE_OK`, `ELEMENT_START`, `ATTRIBUTE`, `ELEMENT_END`, `TEXT` | |
| `XMLNodeParser(input, options?)` | buffer, string or reader; options may carry `filename` | `parse()` returns a string (text), `{tagName, attributes}` (start tag), `{tagName: '/x'}` (end tag) or a status integer; `depth`, `location` | |
| `XMLPushParser(options?)` | options may define callbacks | `write(chunk)`, `close()`; `root`, `path` | `SyntaxError` on `close()` of a truncated stream |
| `XMLTreeSerializer(root)` | a tree array or one element; plan 2026-10-18: or any pull source (`XMLParser`) | `read(n)`, `read(buffer, offset, length)`; `root`, `finished` | |
| `XmlBuilder(options?)` (plan) | `{flat, plain}` | sink methods (`elementStart`, `attribute`, `text`, `elementEnd`, ...), `root`, `path`, `[Symbol.for('qjs.xmlsink')]` | |
| `source.pump(sink)`, `pushParser.connect(sink)`, `tee(...sinks)` (plan) | sink: a handshake object or any object with the sink methods | | the first error event as `SyntaxError`, or `onParseError` |
| `XMLWriter(output?, options?)` | buffer, writer or function; options a number (indent) or `{indent}` | `elementStart`, `attribute`, `text`, `elementEnd`; `state`, `written`, `indent` | planned: `TypeError` for a mismatched `elementEnd` |

Name clash: the DOM's `XMLSerializer` has `serializeToString(node)`; ours is a different contract under the same name until the rename to `XMLTreeSerializer`
(`api-compare-xml.md` section 5). Whichever way it is resolved is an interface change and goes in the ChangeLog.

## 10. Planned interface changes (Tier 20)

| Interface | Change | Step |
| --- | --- | --- |
| `XMLWriter` | escapes text and attribute values; `attribute(n, true)` writes `n`; `indent < 0` is truly compact | 3c |
| `write()` | unchanged output; emits through `xwrite` with `XW_TREE` | 3d |
| `walk`/`xwalk` | adapter into `xwrite` | 4d |
| `XMLTreeSerializer` | stepping model; may cut `read(n)` on character boundaries (fixes `BUGS`) | 5 |
| `jbuilder` | `proto`, `jbuild_value_js`; `xbuilder` | 2 |
| event protocol | `xe_type_t`, `xe_callback`, `XMLWriteInterface`, typed markup events | 1 |
| `xml.c` | `strict` option (Bun's `XML.parse` rules) | 9 |

## 11. Errors and locations: the contract

The exception classes, the `loc` object, and the position rules are those of [api-interface-json.md](api-interface-json.md) section 11
(`SyntaxError` with own non-enumerable `loc`, `length`, `code`; `TypeError` for bad arguments; callbacks' exceptions unchanged; `AggregateError` for
collected errors). XML specifics:

### 11.1 Where each failure appears

| Mode / API | Failure | Result |
| --- | --- | --- |
| `xml.read()`, `XML.parse()` strict | malformed document | throws `SyntaxError` (`loc`, `length`, `code`) |
| `xml.read()` lenient (default today) | a repair (stray end tag, implied end, void element, unterminated tag at EOF) | no exception; optional `onParseError(err)` with the same shape |
| `XMLParser`, `XMLNodeParser` | mismatched end tag with `tolerant` off | `parse()` returns `PARSE_ERROR`; `parser.error` holds the `SyntaxError`; `.location` is the position of the next token as now |
| `XMLPushParser` | mismatch while writing; truncated input on `close()` | `SyntaxError` with `loc` at the point; `onParseError` in lenient use |
| `XMLWriter` / `xwrite` | `elementEnd` mismatch, `attribute` outside a tag | `TypeError` with `code` (`mismatched-end-tag`, `attribute-outside-tag`) |
| `DOMParser.parseFromString(s, 'text/xml')` | malformed document | no exception; the result's document element is `<parsererror>` carrying the message and `line`/`column` text (Browser) |
| HTML mode of the DOM | any input | never an error; repairs go to `onParseError` only |

### 11.2 Codes (XML)

Strict: `mismatched-end-tag`, `unexpected-eof-in-tag`, `unexpected-eof-in-element`, `duplicate-attribute`, `unquoted-attribute-value`,
`missing-attribute-value`, `undefined-entity`, `invalid-character-reference`, `multiple-roots`, `text-outside-root`, `missing-root`,
`invalid-character`, `unterminated-comment`, `unterminated-cdata`, `bad-xml-declaration`, `invalid-name`. Lenient repairs reuse the HTML
spec's names where one exists (`end-tag-without-matching-open-element`, `eof-in-tag`, `duplicate-attribute`, `unexpected-null-character`).
`loc` is the `<` of the tag or the first character of the offending token; for a mismatched end tag it is the start of the end tag (what
`xml.read()` already prints), with `length` covering it.

### 11.3 Location tracking in the parsers

| Parser | Position today | Target |
| --- | --- | --- |
| `xml.c` (pull) | `Location loc` plus `tag_line`/`tag_column` (start of the tag) | the error event carries `tag_line`/`tag_column` and `length`; `Location` counts `\r\n` as one line break |
| `xread.c` (push) | none | counts offset, line and column (`SourcePos`), or is deleted in favour of `xml.c` over a push `Reader` (Tier 20 phase 6) |
| `read(..., {location: true})` | `[tree, WeakMap]` of per-element `Location` | unchanged; `xbuilder` takes over the side table |
| `XMLParser.location`, `XMLNodeParser.location` | live | unchanged |
| `XMLPushParser` | none | `.location` live, as `JsonPushParser` has |

Per-node locations are the client-facing form of location tracking (editors, linters, `lib/dom.js` source maps); the error `loc` is the
same `Location` class.

