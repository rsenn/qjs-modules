# xml

Source: `quickjs-xml.c` — module exports **`XMLParser`**, **`XMLPushParser`**, **`XMLTreeSerializer`**, **`XMLWriter`** and a function list.

An XML/HTML reader and writer. Parses markup into a tree (or flat list) of plain JS
objects and serializes such a tree back to text. Element objects are `{tagName, attributes, children}`-shaped.

## Module functions

| Function | Args | Description |
| --- | --- | --- |
| `read(input, inputName?, options?)` | 1–3 | Parses XML/HTML text into an array/tree of element objects. `input` is a string or buffer. `inputName` is an optional filename for error messages. `options` is either a boolean (`flat` mode) or an object with `flat`, `tolerant`, `location`, and `selfClosingTags` (array of void-element tag names). When `location` is true, returns `[tree, locationMap]` instead of just the tree. |
| `write(value, maxDepth?)` | 1–2 | Serializes a parsed tree (or flat list) back into XML/HTML text. `maxDepth` limits traversal depth (default: unlimited). See "Layout of `write()`" below. |

Both are also reachable through the module's `default` export object.

`read()` is built on the event parser of `src/xml.c` (the one behind `XMLParser`), so the three agree:

| Behaviour | Rule |
| --- | --- |
| tolerant mode | a stray closing tag is ignored (the `>` after it stays text); an open `<li>`, `<dt>`, `<dd>`, `<p>`, `<option>`, `<tr>`, `<td>`, `<th>`, `<tbody>`... is closed when a sibling starts, and `<p>`, `<li>`, `<dt>`, `<dd>` also when a block element starts |
| `flat: true` | no element stack and no implied ends: every closing tag, matched or not, is an entry `{tagName: '/name'}` |
| text | a run of only whitespace is dropped; otherwise just the writer's structural newlines around the content are stripped |
| `<style>`, `<script>`, `<textarea>`, `<title>` | content is raw text, one string per line |
| `</>` | closes nothing |
| input ending inside a tag | the tag is dropped (an unterminated comment or declaration keeps the rest as its `tagName`) |


### Layout of `write()`

`write()` pumps the element-tree walker (`include/xml/xwalk.h`) into an `XmlWriter` with `XW_PRETTY` (indent 2, no trailing newline,
text-only and mixed content stay on one line). An element cut by `maxDepth` is written empty (`<b />`). Both shapes of `read()` are accepted; a list whose last entry has no `children`
array is a flat list.

| rule | tree | flat list |
| --- | --- | --- |
| indent | 2 spaces per level; the children of a root element are at level 0 | the level moves after a `dt` start tag and back at a `/x` marker |
| self-closing `<a />` | `children` missing or empty | the next entry is its own `/a` marker (dropped), or `a` is the last entry |
| text | multiline (own line) when its list has more than one entry; otherwise it follows the tag directly and trailing whitespace before it is dropped | multiline when it contains a newline |
| `maxDepth` | an element cut off is written with `>` and no close tag | not used |
| comment `<!-- -->`, `<!x>`, `<?x?>` | raw name, no attributes; `?` names never get a close tag | same |
| final newline | trimmed | kept |

Text and attribute values are escaped (`& < >`, and `"` in attributes); an attribute value `true` is written bare.

## XMLParser.next() and for..of

`next()` returns the next event as `{ type, name?, value?, loc: { line, column } }` and the parser is iterable. Markup arrives typed:
`COMMENT` (5), `DECLARATION` (6), `INSTRUCTION` (7) and `CDATA` (8) carry their text in `value`. A mismatched closing tag throws the
`SyntaxError`.

## Parser options

`xml.read(text, name, options)` and `new XMLParser(input, options)` share one option reader: `{ flat, tolerant, selfClosingTags, filename }`,
and `location` for `read()`. `XMLParser` has no default self-closing tags (`read()` uses the HTML ones). `strict` and `entities` are not
implemented, they wait for the engine.

## Connecting engines: pump(), connect(), tee, XmlBuilder

Every source runs into every sink in C. A sink is an `XMLWriter`, an `XmlBuilder`, a `tee(...)` or any object with the sink methods
(`elementStart`, `attribute`, `elementEnd`, `text`, `comment`, `declaration`, `instruction`, `cdata`, `error`).

```js
import { XMLParser, XMLPushParser, XmlBuilder, XMLWriter, tee } from 'xml';

const b = new XmlBuilder({ flat: false, plain: true });
new XMLParser('<a><b/></a>').pump(b);                 // 0 = ended, 1 = the reader has no data now
b.finish().root;                                      // [{tagName: 'a', attributes: {}, children: [...]}]

new XMLPushParser().connect(tee(b, new XMLWriter(sink))).write('<a/>');
```

| Member | Does |
| --- | --- |
| `parser.pump(sink)` | runs the pull parser into the sink; markup arrives typed. Throws the SyntaxError of a mismatched tag |
| `pushParser.connect(sink)` / `.sink` | `write()` and `close()` drive the sink; returns the parser |
| `tee(a, b, ...)` | a sink that sends every event to each, in order |
| `new XmlBuilder({flat, plain})` | `root`, `path`, `finish()`, the sink methods |
| `[Symbol.for('qjs.xmlsink')]()` | handshake of the native sinks, as in `json` |

The sink interface has no self-closing flag, so a tree built through a sink gives `<b/>` an empty `children` like `<b></b>`;
`read()` keeps the difference.

## XMLParser

An incremental pull parser. `next()` and the iterator give the events above, `pump(sink)` runs it into a sink in C.

```js
new XMLParser(input, options?)   // input is a buffer/string or a reader; options: see Parser options
```

`input` may be:
- a buffer (string, `ArrayBuffer`, or typed array) holding the whole document, or
- a pull function `(buf, len) => bytesRead`, called as needed to fill `buf`, or
- an object exposing such a function as its `read` method.

The iteration ends when the document is complete or the reader has no data now (call `next()` again once it might). The `type` of an
event is one of these constants, static properties of the constructor:

| Constant | Value | `name` | `value` |
| --- | --- | --- | --- |
| `XMLParser.ELEMENT_START` | 1 | tag name | |
| `XMLParser.ATTRIBUTE` | 2 | attribute name | the value, undefined for a boolean attribute |
| `XMLParser.ELEMENT_END` | 3 | tag name | |
| `XMLParser.TEXT` | 4 | | the text run |
| `XMLParser.COMMENT` | 5 | | the text |
| `XMLParser.DECLARATION` | 6 | | the text |
| `XMLParser.INSTRUCTION` | 7 | | the text |
| `XMLParser.CDATA` | 8 | | the text |

| Member | Args | Kind | Description |
| --- | --- | --- | --- |
| `next()` | 0 | method | The next event `{ type, name?, value?, loc }`, as an iterator result. |
| `pump(sink)` | 1 | method | Runs the parser into a sink, returns 0 when the input ended, 1 when the reader has no data now. |
| `depth` | — | getter | Current element nesting depth (enumerable). |
| `location` | — | getter | A `Location` reflecting the current input position (line/column/byte offset/filename), live (enumerable). |
| `tolerant` | — | getter/setter | When true, mismatched closing tags are silently skipped instead of throwing. |

```js
import { XMLParser } from 'xml';

for(const { type, name, value } of new XMLParser('<root attr="val"><child>text</child></root>')) console.log(type, name, value);
// 1 root  2 attr val  1 child  4 undefined text  3 child  3 root
```

## XMLPushParser

A "push" XML parser: instead of pulling from an input, data is fed to it via `.write()` at any
byte boundary. It can operate in two modes:

- **Callback mode**: when the constructor options define any of `attribute`, `elementStart`,
  `elementEnd`, `text`, or `error` as functions, those callbacks are invoked for each event.
- **Builder mode** (default): when no callbacks are given, it builds a `{tagName, attributes,
  children}` tree internally, retrievable via `.root`.

```js
new XMLPushParser(options?)   // options may define callbacks; also serves as `this` for callback invocations
```

| Member | Args | Kind | Description |
| --- | --- | --- | --- |
| `write(chunk)` | 1 | method | Feeds a chunk of XML input (string or buffer). In builder mode, throws a `SyntaxError` on malformed input. In callback mode, the `error` callback is invoked instead. |
| `close()` | 0 | method | Signals end of input. In builder mode, throws if the document is incomplete (unclosed elements). |
| `root` | — | getter | The `{tagName, attributes, children}` tree built so far (builder mode only). |
| `path` | — | getter | Array of `tagName` strings from the document root down to the currently-open element, root-first (builder mode only). |

```js
import { XMLPushParser } from 'xml';

let p = new XMLPushParser();

p.write('<root><child attr="v');
p.write('alue">text</child></root>');
p.close();

console.log(p.root);
// [{ tagName: "root", attributes: {}, children: [{ tagName: "child", attributes: { attr: "value" }, children: ["text"] }] }]
```

## XMLTreeSerializer

A "pull" serializer: it traverses a parsed tree lazily, producing only as much text as requested
per `.read()` call, rather than building the whole string up front. Modeled on `JsonSerializer`
(`quickjs-json.c`).

```js
new XMLTreeSerializer(root)   // root is a tree array or a single element object
```

| Member | Args | Kind | Description |
| --- | --- | --- | --- |
| `read(n)` | 1 | method | Returns a string of up to `n` characters of serialized XML, `''` once exhausted. |
| `read(buffer, offset?, length?)` | 1–3 | method | Writes serialized bytes directly into the given `ArrayBuffer`/`TypedArray` (zero-copy), starting at `offset` for up to `length` bytes — or the whole buffer if omitted. Returns the number of bytes written, `0` once exhausted. |
| `root` | — | getter | The root value being serialized. |
| `finished` | — | getter | `true` once all output has been produced. |

```js
import { XMLTreeSerializer } from 'xml';

let tree = xml.read('<root><child attr="val">text</child></root>');
let s = new XMLTreeSerializer(tree);
let out = '';
let chunk;

while((chunk = s.read(32)) !== '') out += chunk;

console.log(out); // <root><child attr="val">text</child></root>
```

```js
// Zero-copy: write straight into a fixed-size buffer.
let s = new XMLTreeSerializer(bigTree);
let buf = new Uint8Array(4096);
let n;

while((n = s.read(buf)) > 0) socket.send(buf.subarray(0, n));
```

## XMLWriter

An event-driven XML writer: instead of serializing a tree, the caller drives output by calling
`elementStart()`, `attribute()`, `text()`, and `elementEnd()` in document order. Output goes to a
`Writer` (from `stream-utils.h`) — a buffer, fd, or any writable sink. Handles indentation
automatically.

The methods are the `XmlEventSink` interface (`include/xml/xsink.h`, the twin of the walk interface), so the same
object can be handed to anything that drives an `XmlEventSink`; failures carry a stable `err.code`
(`mismatched-end-tag`, `unmatched-end-tag`, `attribute-outside-tag`, `write-failed`).

```js
new XMLWriter(output, options?)   // output is a buffer or writer; options may be a number (indent) or {indent}
```

`output` may be:
- a writable buffer (`ArrayBuffer`, typed array), or
- an object exposing a `write(buf, offset, length)` method, or
- omitted, in which case an internal buffer is used.

`options` is either a number (the indent width, default 2) or an object with an `indent` property.

| Member | Args | Kind | Description |
| --- | --- | --- | --- |
| `elementStart(name)` | 1 | method | Writes `<name`. Increments the nesting level. Returns bytes written. |
| `attribute(name, value?)` | 1–2 | method | Writes ` name="value"`. If `value` is omitted or `true`, writes a boolean/valueless attribute (`name` only). Returns bytes written. |
| `elementEnd(name?)` | 0–1 | method | If called right after `elementStart()`/`attribute()` (no text or child elements in between), writes ` />` (self-closing). Otherwise writes `</name>`. Decrements the nesting level. Returns bytes written. |
| `text(content)` | 1 | method | Writes text content. Returns bytes written. |
| `comment(str)` | 1 | method | Writes `<!--str-->`. |
| `cdata(str)` | 1 | method | Writes `<![CDATA[str]]>`. |
| `instruction(str)` | 1 | method | Writes `<?str?>`. |
| `declaration(str)` | 1 | method | Writes `<!str>`. |
| `error(str)` | 1 | method | Writes nothing (the output has no form for an error); returns 0. |
| `state` | — | getter | The event type of the last method call (an `XmlEventType` integer). |
| `written` | — | getter | Total number of bytes written so far. |
| `indent` | — | getter/setter | The indentation width (number of spaces per level). |

```js
import { XMLWriter } from 'xml';

let buf = new Uint8Array(1024);
let w = new XMLWriter(buf);

w.elementStart('root');
  w.elementStart('child');
    w.attribute('attr', 'val');
    w.text('hello');
  w.elementEnd('child');
w.elementEnd('root');

let text = new TextDecoder().decode(buf.subarray(0, w.written));
console.log(text);
// <root>
//   <child attr="val">hello</child>
// </root>
```
