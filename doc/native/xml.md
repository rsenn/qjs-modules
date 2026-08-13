# xml

Source: `quickjs-xml.c` — module exports **`XMLParser`**, **`XMLNodeParser`**, **`XMLPushParser`**, **`XMLSerializer`**, **`XMLWriter`** and a function list.

An XML/HTML reader and writer. Parses markup into a tree (or flat list) of plain JS
objects and serializes such a tree back to text. Element objects are `{tagName, attributes, children}`-shaped.

## Module functions

| Function | Args | Description |
| --- | --- | --- |
| `read(input, inputName?, options?)` | 1–3 | Parses XML/HTML text into an array/tree of element objects. `input` is a string or buffer. `inputName` is an optional filename for error messages. `options` is either a boolean (`flat` mode) or an object with `flat`, `tolerant`, `location`, and `selfClosingTags` (array of void-element tag names). When `location` is true, returns `[tree, locationMap]` instead of just the tree. |
| `write(value, maxDepth?)` | 1–2 | Serializes a parsed tree (or flat list) back into XML/HTML text. `maxDepth` limits traversal depth (default: unlimited). |

Both are also reachable through the module's `default` export object.

## XMLParser

An incremental pull parser built on `Reader`/`Location` from `stream-utils.h` and `include/xml.h`'s
`xml_parser_run()`. Each call to `.parse()` advances just far enough to produce one event (or a
status code) and also feeds every element/attribute/text event into an internal `XMLBuilder`, so
`.root` gives the tree built so far without the caller having to do that bookkeeping.

```js
new XMLParser(input, options?)   // input is a buffer/string or a reader; options may carry callbacks and filename
```

`input` may be:
- a buffer (string, `ArrayBuffer`, or typed array) holding the whole document, or
- a pull function `(buf, len) => bytesRead`, called as needed to fill `buf`, or
- an object exposing such a function as its `read` method.

`options` is an object. If it defines any of the callback properties (`elementStart`, `elementEnd`, `attribute`, `text`, `error`), events are forwarded to those callbacks instead of being fed into the builder. It may also carry a `filename` string for `.location`.

`.parse()` returns one of the following integer constants (exposed as static properties on the constructor):

| Constant | Value | Meaning |
| --- | --- | --- |
| `XMLParser.PARSE_AGAIN` | −2 | Reader has no data right now; call `.parse()` again once it might. |
| `XMLParser.PARSE_ERROR` | −1 | Non-tolerant mismatched closing tag (terminal). |
| `XMLParser.PARSE_OK` | 0 | Clean end of stream (terminal). |
| `XMLParser.ELEMENT_START` | 1 | Opening tag — `eventName` is the tag name. |
| `XMLParser.ATTRIBUTE` | 2 | Attribute — `eventName` is the name, `eventValue` the value (`hasValue` is false for boolean attributes). |
| `XMLParser.ELEMENT_END` | 3 | Closing tag — `eventName` is the tag name. |
| `XMLParser.TEXT` | 4 | Text content — `eventValue` is the text run. |

| Member | Args | Kind | Description |
| --- | --- | --- | --- |
| `parse()` | 0 | method | Advances one event. Returns an `xml_event_t` integer (see table above). |
| `eventName` | — | getter | The current event's tag/attribute name (enumerable). |
| `eventValue` | — | getter | The current event's value — attribute value or text content (enumerable). |
| `hasValue` | — | getter | Whether the current event carries a value (enumerable). |
| `depth` | — | getter | Current element nesting depth (enumerable). |
| `location` | — | getter | A `Location` reflecting the current input position (line/column/byte offset/filename); live (enumerable). |
| `tolerant` | — | getter/setter | When true, mismatched closing tags are silently skipped instead of producing `PARSE_ERROR`. |
| `root` | — | getter | The `{tagName, attributes, children}` tree built so far (enumerable). |
| `onelementstart` | — | setter | Callback invoked on element start events (configurable, writable). |
| `onelementend` | — | setter | Callback invoked on element end events (configurable, writable). |
| `onattribute` | — | setter | Callback invoked on attribute events (configurable, writable). |
| `ontext` | — | setter | Callback invoked on text events (configurable, writable). |

```js
import { XMLParser } from 'xml';

let p = new XMLParser('<root attr="val"><child>text</child></root>');
let ev;

while((ev = p.parse()) > 0) {
  if(ev === XMLParser.ELEMENT_START) console.log('start', p.eventName);
  if(ev === XMLParser.ATTRIBUTE)     console.log('attr', p.eventName, '=', p.eventValue);
  if(ev === XMLParser.TEXT)          console.log('text', p.eventValue);
  if(ev === XMLParser.ELEMENT_END)   console.log('end', p.eventName);
}
// start root  attr attr = val  start child  text text  end child  end root
```

## XMLNodeParser

A pull parser that yields one "flat list" node per `.parse()` call. Returns JS strings for text
content, and JS objects `{tagName, attributes}` for start tags (end tags have a `tagName` prefixed
with `/`). Unlike `XMLParser`, each call returns a complete node rather than individual events —
attributes are collected internally and attached to the start-tag object before it is returned.

```js
new XMLNodeParser(input, options?)   // input is a buffer/string or a reader; options may carry filename
```

`input` and the reader conventions are the same as `XMLParser`. `options` is an object that may
carry a `filename` string for `.location`.

`.parse()` returns:
- A JS **string** for text content.
- A JS **object** `{tagName, attributes}` for a start tag (attributes are collected before returning).
- A JS **object** `{tagName}` with `tagName` prefixed by `/` for an end tag.
- An **integer** status code (`PARSE_AGAIN`, `PARSE_ERROR`, `PARSE_OK`) — same constants as `XMLParser`.

| Member | Args | Kind | Description |
| --- | --- | --- | --- |
| `parse()` | 0 | method | Advances to the next node. Returns a string, object, or integer status (see above). |
| `depth` | — | getter | Current element nesting depth (enumerable). |
| `location` | — | getter | A `Location` reflecting the current input position (line/column/byte offset/filename); live (enumerable). |

```js
import { XMLNodeParser } from 'xml';

let p = new XMLNodeParser('<ul><li>hello</li></ul>');
let node;

while((node = p.parse()) > 0) {
  if(typeof node === 'string')       console.log('text:', node);
  else if(node.tagName[0] === '/')   console.log('close:', node.tagName);
  else                               console.log('open:', node.tagName, node.attributes);
}
// open: ul {}  open: li {}  text: hello  close: /li  close: /ul
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

## XMLSerializer

A "pull" serializer: it traverses a parsed tree lazily, producing only as much text as requested
per `.read()` call, rather than building the whole string up front. Modeled on `JsonSerializer`
(`quickjs-json.c`).

```js
new XMLSerializer(root)   // root is a tree array or a single element object
```

| Member | Args | Kind | Description |
| --- | --- | --- | --- |
| `read(n)` | 1 | method | Returns a string of up to `n` characters of serialized XML, `''` once exhausted. |
| `read(buffer, offset?, length?)` | 1–3 | method | Writes serialized bytes directly into the given `ArrayBuffer`/`TypedArray` (zero-copy), starting at `offset` for up to `length` bytes — or the whole buffer if omitted. Returns the number of bytes written, `0` once exhausted. |
| `root` | — | getter | The root value being serialized. |
| `finished` | — | getter | `true` once all output has been produced. |

```js
import { XMLSerializer } from 'xml';

let tree = xml.read('<root><child attr="val">text</child></root>');
let s = new XMLSerializer(tree);
let out = '';
let chunk;

while((chunk = s.read(32)) !== '') out += chunk;

console.log(out); // <root><child attr="val">text</child></root>
```

```js
// Zero-copy: write straight into a fixed-size buffer.
let s = new XMLSerializer(bigTree);
let buf = new Uint8Array(4096);
let n;

while((n = s.read(buf)) > 0) socket.send(buf.subarray(0, n));
```

## XMLWriter

An event-driven XML writer: instead of serializing a tree, the caller drives output by calling
`elementStart()`, `attribute()`, `text()`, and `elementEnd()` in document order. Output goes to a
`Writer` (from `stream-utils.h`) — a buffer, fd, or any writable sink. Handles indentation
automatically.

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
| `state` | — | getter | The event type of the last method call (an `xml_event_t` integer). |
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
