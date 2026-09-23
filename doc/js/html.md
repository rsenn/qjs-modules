# html

Source: `lib/html.js` — exports: `HTMLParser`, `streamSrcHrefAndText`

A SAX-style pull parser for HTML, built on the native `xml` module's
`XMLParser`. Not a full WHATWG HTML5 tree-construction parser (no
insertion-mode state machine), but `src/xml.c`'s tokenizer handles the
HTML5-relevant raw-text elements (`<script>`, `<style>`, `<textarea>`,
`<title>` — content never tokenized as markup), and `HTMLParser` itself
enables tolerant closing-tag-mismatch recovery and lowercases tag/attribute
names, since HTML (unlike XML) is case-insensitive.

## `HTMLParser`

```js
new HTMLParser(source, filenameOrOptions?)
```

- `source` — HTML string (or anything `XMLParser`'s reader accepts).
- `filenameOrOptions` — optional filename string, or an options object
  forwarded to `XMLParser` (`{ filename, attribute, elementStart, elementEnd, error, text }`).

### `parser.parse()`

Advances the parser one event at a time; returns `null` at end of input.
Event shapes:

| Event | Shape |
| --- | --- |
| Element start | `{ name }` |
| Attribute | `{ name, value }` |
| Text | `{ value }` |
| Element end | `{ name: '/' + name, attributes }` |

### `parser.tag` / `parser.attributes`

The currently-open element's tag name and its attributes object.

## `streamSrcHrefAndText(source, options)`

Generator yielding `{ type, value, tag }` for text nodes and for attributes
named in `options.attributes` (default `['src', 'href']`).

```js
import { streamSrcHrefAndText } from 'html';

for(const { type, value, tag } of streamSrcHrefAndText(html, {}))
  console.log(type, tag, value);
```
