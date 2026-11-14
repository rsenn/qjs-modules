# XML: our API against Browser, Bun, Node, Deno and JS packages

What `xml` (`quickjs-xml.c`, `lib/xml.js`, `lib/html.js`, `lib/dom.js`) offers, set next to what
the other runtimes and the widely used packages offer. Priority when they disagree:
WHATWG > Browser > Bun > Node > Deno. Companion documents:
[api-implement-xml.md](api-implement-xml.md), [api-interface-xml.md](api-interface-xml.md),
[native/xml.md](native/xml.md), and the plan in `TODO.md` Tier 20.

Method: probed on node 22.23.3, bun 1.4.2, deno 2.9.7 and the qjsm of this tree (2026-10-17).
Packages were installed and run on one document: `fast-xml-parser` 5.11.2, `sax` 1.6.1,
`saxes` 6.0.0, `@xmldom/xmldom` 0.9.12, `htmlparser2` 12.0.0 (`xmlMode`), `xml2js` 0.6.2,
`xmlbuilder2` 4.0.3, `txml` 6.0.3. The document is
`<a x="1"><b>t</b><b>u</b>tail<c/></a>`.

## 1. What each runtime has built in (probed)

| Capability | Browser | Bun | Node | Deno | qjsm (here) |
| --- | --- | --- | --- | --- | --- |
| `DOMParser.parseFromString(str, 'text/xml')` | yes | no | no | no | `lib/dom.js` `Parser.parseFromString`, not under the DOM name |
| `XMLSerializer.serializeToString(node)` | yes | no | no | no | `lib/dom.js` `Serializer.serializeToString`; **our former `XMLSerializer` (now `XMLTreeSerializer`) export is a different thing** (section 5) |
| `XMLHttpRequest.responseXML`, `XPathEvaluator`, `XSLTProcessor` | yes | no | no | no | XPath in `lib/xpath.js`, the rest no |
| `XML.parse(text)`, `XML.stringify(value)` | no | `Bun.XML` | no | no | planned (Tier 19, Tier 20 phase 9) |
| any other built-in XML | no | no | no | no | no |

Node and Deno have no XML support at all; everything there is a package. In the browser the
object model is the DOM, so the browser standard is the DOM subset, not a plain-object tree.

## 2. The same document in every tree shape (probed)

| API | Result |
| --- | --- |
| ours `xml.read(x)` | `[{"tagName":"a","attributes":{"x":"1"},"children":[{"tagName":"b","attributes":{},"children":["t"]},{"tagName":"b","attributes":{},"children":["u"]},"tail",{"tagName":"c","attributes":{}}]}]` |
| ours `xml.read(x, name, {flat: true})` | `[{tagName:'a',attributes:{x:'1'}}, {tagName:'b',...}, 't', {tagName:'/b'}, ..., {tagName:'/a'}]` |
| `txml` `parse` | the same as ours, except an empty element has `children: []` (ours omits `children`) |
| `Bun.XML.parse` | `{"a":{"@x":"1","b":["t","u"],"#text":"tail","c":""}}` |
| `fast-xml-parser` | `{"a":{"b":["t","u"],"c":"","#text":"tail","@_x":"1"}}` (attributes with `ignoreAttributes:false`, prefix `@_` configurable); `preserveOrder:true` gives `[{"a":[{"b":[{"#text":"t"}]},...],":@":{"@_x":"1"}}]` |
| `xml2js` | `{"a":{"_":"tail","$":{"x":"1"},"b":["t","u"],"c":[""]}}` (everything wrapped in arrays unless `explicitArray:false`) |
| `@xmldom/xmldom` | a DOM `Document`; `serializeToString` round-trips `<a x="1"><b>t</b><b>u</b>tail<c/></a>` |
| `sax`, `saxes`, `htmlparser2` | events, identical sequence: `open a{x:1}`, `open b`, `text t`, `close b`, `open b`, `text u`, `close b`, `text tail`, `open c`, `close c`, `close a` |
| ours `XMLParser` (pull) | `ELEMENT_START a`, `ATTRIBUTE x=1`, `ELEMENT_START b`, `TEXT t`, `ELEMENT_END b`, ..., `ELEMENT_END a`; attributes are separate events, a value-less attribute has `hasValue` false |

Observations:

- Our tree is `txml`'s tree (`tagName`, `attributes`, `children`, text as bare strings). It is also the only shape
  that keeps document order for mixed content, which is why `lib/dom.js` is built on it. Bun's object form loses
  the interleaving of text and elements (its own limitation: `"#text":"tail"` stands for text anywhere).
- The three object-form libraries (Bun, fast-xml-parser, xml2js) agree on repeated names becoming arrays and
  differ on how attributes and text are marked (`@x`/`#text`, `@_x`/`#text`, `$`/`_`). Bun's is the standard we follow.
- Event parsers agree on the same four events; ours delivers `ATTRIBUTE` as its own event where the
  others fold attributes into the open event. That is a deliberate fit for the resumable pull design.

## 3. Strictness (probed on bun 1.4.2, ours from `native/xml.md`)

| Input | Bun `XML.parse` | ours `read()` |
| --- | --- | --- |
| mismatched or missing closing tag | `SyntaxError: XML Parse error: Expected closing tag </a> but found </b>` | tolerant: recovers; strict only for mismatches when `tolerant` is off (`XE_ERROR`) |
| unquoted or duplicate attribute, `<` in an attribute | `SyntaxError` | accepted |
| undeclared entity, bare `&` | `SyntaxError` | accepted |
| more than one root, text after the root | `SyntaxError` | accepted (a list of roots) |
| HTML (`<br>`, unclosed `<p>`) | `SyntaxError` | accepted: implied end tags, void tags, value-less attributes |
| control character | `SyntaxError` | accepted |
| DOCTYPE internal subset (`<!DOCTYPE a [<!ENTITY foo "val">]>`) | entities declared and expanded | not parsed: the declaration ends at the first `>` inside the subset, giving `{tagName: '!DOCTYPE a [<!ENTITY foo "val"'}` plus the text `]>`; `&foo;` stays literal (`BUGS`) |

Our parser is lenient by design (it parses HTML; `lib/html.js`, `examples/html2md.js`). A strict
mode is the way to match Bun, not a second parser (Tier 20 phase 9).

## 4. Writers

| API | Shape | Escaping | Pretty printing |
| --- | --- | --- | --- |
| `Bun.XML.stringify(value)` | object form, or node form `{name, attributes, children}` | yes | no |
| `xmlbuilder2` `create(obj).end()` | `@x` attribute keys | yes | option |
| `fast-xml-parser` `XMLBuilder.build` | its own object form | yes | `format` option |
| `@xmldom/xmldom` `XMLSerializer` | DOM node | yes | no |
| ours `xml.write(tree, maxDepth)` | `read()` tree or flat list | yes | fixed layout (newline per tag, 2 spaces; quirks pinned by the golden corpus) |
| ours `XMLTreeSerializer` (was `XMLSerializer`) | tree, pull: `read(n)` | yes | none |
| ours `XMLWriter` | events: `elementStart`, `attribute`, `text`, `elementEnd` | **no** (planned fix, Tier 20 step 3c) | `indent` option |

## 5. Name clashes and gaps against the standards

- **`XMLSerializer`**: in every browser it is `new XMLSerializer().serializeToString(node)`. Ours is a pull serializer
  `new XMLTreeSerializer(tree).read(n)` (was `XMLSerializer`). Same name, different contract, so a browser script importing it from `xml` breaks silently.
  Options: rename ours (`XMLTreeSerializer`), or give it a `serializeToString(tree)` method so both uses work. Decided: renamed.
- `DOMParser` and `XMLSerializer` in the DOM sense exist in `lib/dom.js` as `Parser` and `Serializer`; exporting them under the
  DOM names (`DOMParser.parseFromString(str, type)` returning a `Document`) is the standards lift with the most users.
- `XML.parse` and `XML.stringify` (Bun) are missing; a converter exists in the attic (`attic/`), the plan is a mode of the
  new pieces rather than a second implementation.
- No XSLT, no `XMLHttpRequest`; out of scope.
- Package event APIs (`sax` family) have no equivalent need: `XMLParser` and `XMLPushParser` already cover them.

## 6. Findings and what they decide

1. Keep the `tagName`/`attributes`/`children` tree: it matches `txml`, is the lossless one and `lib/dom.js` depends on it.
2. Add Bun's `XML.parse`/`XML.stringify` as a conversion mode over the same parser and walker (priority Bun after Browser;
   there is no Browser plain-object form).
3. Export the DOM subset under its standard names from `lib/dom.js` (priority Browser); resolve the `XMLSerializer` clash first.
4. Make `XMLWriter` escape (the only one of our writers that does not), as every package above does.
5. A strict mode on `src/xml/xml.c` closes the Bun gap in section 3 without losing the HTML tolerance.

## 7. Parse errors and locations (probed 2026-10-17)

Input: `<a>` newline ` <b></c></a>` (the mismatched `</c>` starts at line 2, column 5).

| API | Exception | Message | Position for the client |
| --- | --- | --- | --- |
| browser `DOMParser.parseFromString(s, 'text/xml')` (spec and docs, not probed) | **none**: returns a document whose root is a `<parsererror>` element | Chrome: `error on line 2 at column 10: Opening and ending tag mismatch: b line 2 and c`; Firefox: `XML Parsing Error: mismatched tag. Expected: </b>.\nLocation: ...\nLine Number 2, Column 10:` plus the source line | only inside that element's text; `XMLHttpRequest.responseXML` is `null` on failure |
| browser HTML parsing | **never fails**; the HTML spec names dozens of parse errors (`unexpected-null-character`, `end-tag-with-attributes`, ...) that script cannot observe | | none; tools such as `parse5` surface them |
| bun `Bun.XML.parse` | `SyntaxError` | `XML Parse error: Expected closing tag </b> but found </c>` | own `line`/`column` are the throw site in the caller, as for JSON (document 1, section 7) |
| node, deno | no XML support | | |
| `parse5` (HTML) | **no exception**; `onParseError(err)` callback | | plain object `{code, startLine, startCol, startOffset, endLine, endCol, endOffset}` (spec code names) |
| `@xmldom/xmldom` 0.9.12 | custom **`ParseError`** (extends `Error`) | `Opening and ending tag mismatch: "b" != "c"` | own **`locator: {lineNumber, columnNumber}`**; `onError(level, msg, context)` callback; helper `onErrorStopParsing`; the old `errorHandler` object throws `TypeError` |
| `saxes` 6.0.0 | `Error` through `on('error')` | `2:8: unexpected close tag.` | line and column as the message prefix, and live `parser.line`, `.column`, `.position` |
| `sax` 1.6.1, `xml2js` 0.6.2, `txml` 6.0.3 | plain `Error` (callback or throw) | `Unexpected close tag\nLine: 1\nColumn: 8\nChar: >` | in the message only, 0-based line; the numbers differ between the three for the same input, so not a contract |
| `fast-xml-parser` 5.11.2 | `XMLValidator.validate` returns instead of throwing; `parse(xml, true)` throws plain `Error` | `Expected closing tag 'b' (opened in line 2, col 2) instead of closing tag 'c'.:2:5` | **`{err: {code, msg, line, col}}`** from `validate`; stable `code` (`InvalidTag`, ...); default `parse` does not validate |
| `htmlparser2` 12 | none (lenient) | | |
| **ours now** | see section 8 | | |

### 7.1 What this shows

- Two models: the browser's (never throw, put the error in the result) and the library models (throw, callback, or return a
  `{code, line, col}` record). Bun, the only runtime with XML parsing, throws `SyntaxError` without a position.
- Structured positions exist only in libraries: `xmldom` (`ParseError.locator`), `fast-xml-parser` (`line`, `col`, `code`),
  `parse5` (`start/end` line, column, offset, `code`). Their numbering is 1-based, and the three use different names for the same thing.
- Stable error `code`s are what clients branch on (`fast-xml-parser`, `parse5`, `jsonc-parser`); message texts are not stable anywhere.
- A custom exception class is used by a parsing library only to carry the position (`ParseError`); the platform class for the
  same job is `SyntaxError`.

## 8. Our parse errors today (probed)

| API | Exception | Message | Position |
| --- | --- | --- | --- |
| `xml.read()` | `SyntaxError` | `mismatch </c> at f.xml:2:5` | in the message only (correct start of the closing tag); truncated input (`<a><b`) is accepted silently by design |
| `XMLParser.next()` | `SyntaxError` | `mismatch </c>` | `loc`, `length` and `code` on the error, at the start of the closing tag (`parse()` and `XMLNodeParser` are removed) |
| `XMLPushParser.write()` | **silent** on a mismatch | | `.path` only |
| `XMLPushParser.close()` | `SyntaxError` | `malformed or truncated input`, `unexpected end of input (unclosed element)` | none |
| bad argument (`read(5)`) | **`ReferenceError`** | `xml.read(): expecting buffer or string` | should be `TypeError` |

## 9. Target: exceptions and locations

Same contract as JSON (the text is in [api-interface-xml.md](api-interface-xml.md) section 11), with these XML specifics:

- strict failures are `SyntaxError` with own non-enumerable `loc` (start of the offending token or tag), `length` and `code`
  (`mismatched-end-tag`, `unexpected-eof-in-tag`, `duplicate-attribute`, `unquoted-attribute-value`, `undefined-entity`, `multiple-roots`,
  `text-outside-root`, `invalid-character`, `unterminated-comment`, `bad-xml-declaration`, ...);
- the lenient (HTML) mode throws nothing and reports what it repaired through an optional `onParseError(err)` with the same
  `SyntaxError`-shaped object (`code`, `loc`, `length`), using the HTML spec's code names where one exists (`parse5`'s callback name and codes);
- event parsers keep their status protocol (`PARSE_ERROR`) and gain an `error` getter holding that same `SyntaxError`;
- the DOM subset follows the browser: `DOMParser.parseFromString(s, 'text/xml')` returns a document with a `<parsererror>` element and
  does not throw (`lib/dom.js`), while `xml.read()` and `XML.parse()` throw.

