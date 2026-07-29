import xml, { XMLParser, XMLPushParser, XMLSerializer } from 'xml';
import { assert, eq, tests } from './tinytest.js';

/* tinytest's eq() uses !=, which does reference comparison for arrays/objects -
 * deep-compare via JSON.stringify instead (same convention as test_stream.js). */
const eqArr = (actual, expected) => eq(JSON.stringify(actual), JSON.stringify(expected));

function assertThrows(fn, msg) {
  try {
    fn();
  } catch(e) {
    return e;
  }
  throw new Error('assertThrows(): did not throw' + (msg ? ' - ' + msg : ''));
}

/* Drives an XMLParser to completion, collecting [tokenId, eventName, eventValue-or-null]
 * triples (eventValue only recorded when .hasValue is set, matching the C-level
 * event_has_value/event_value contract for e.g. boolean attributes). */
function drain(input, filename) {
  let p = filename !== undefined ? new XMLParser(input, filename) : new XMLParser(input);
  let out = [];

  for(let i = 0; i < 10000; i++) {
    let tok = p.parse();

    out.push([tok, p.eventName, p.hasValue ? p.eventValue : null]);

    if(tok === XMLParser.PARSE_OK || tok === XMLParser.PARSE_ERROR) break;
  }

  return { out, parser: p };
}

/* A Reader-callback-compatible mock: (buf: ArrayBuffer, len: number) -> bytesWritten,
 * respecting len/buf.byteLength and tracking a cursor across calls - the actual
 * contract reader_from_jsfunction() (src/stream-utils.c) uses, which pulls one byte
 * at a time for XMLParser. Optionally stalls once (returns -1, the EAGAIN
 * convention) right before byte index `stallAt`. */
function makeReader(str, stallAt) {
  const bytes = [...str].map(ch => ch.charCodeAt(0));
  let pos = 0;
  let stalled = false;

  return (buf, len) => {
    if(stallAt !== undefined && pos === stallAt && !stalled) {
      stalled = true;
      return -1;
    }

    if(pos >= bytes.length) return 0;

    const view = new Uint8Array(buf);
    const n = Math.min(len, view.length, bytes.length - pos);

    for(let j = 0; j < n; j++) view[j] = bytes[pos + j];

    pos += n;
    return n;
  };
}

tests({
  /* ---------- module shape ---------- */
  'module: default export has read/write, named exports exist'() {
    assert(typeof xml.read === 'function');
    assert(typeof xml.write === 'function');
    assert(typeof XMLParser === 'function');
    assert(typeof XMLPushParser === 'function');
    assert(typeof XMLSerializer === 'function');
  },

  /* ---------- xml.read() (legacy tree parser) ---------- */
  'xml.read: simple element with attribute and text'() {
    eqArr(xml.read('<a x="1">hello</a>'), [{ tagName: 'a', attributes: { x: '1' }, children: ['hello'] }]);
  },
  'xml.read: nested elements'() {
    eqArr(xml.read('<root a="1"><child>hello</child><child2/></root>'), [
      {
        tagName: 'root',
        attributes: { a: '1' },
        children: [
          { tagName: 'child', attributes: {}, children: ['hello'] },
          { tagName: 'child2', attributes: {} },
        ],
      },
    ]);
  },
  'xml.read: boolean (valueless) attribute'() {
    let r = xml.read('<input disabled type="text">');

    eq(r[0].attributes.disabled, true);
    eq(r[0].attributes.type, 'text');
  },
  'xml.read: default self-closing tags (e.g. br) need no explicit slash'() {
    eqArr(xml.read('<div><br><p>x</p></div>'), [
      {
        tagName: 'div',
        attributes: {},
        children: [{ tagName: 'br', attributes: {} }, { tagName: 'p', attributes: {}, children: ['x'] }],
      },
    ]);
  },
  'xml.read: custom selfClosingTags option'() {
    eqArr(xml.read('<div><custom-void/></div>', 'f.xml', { selfClosingTags: ['custom-void'] }), [
      { tagName: 'div', attributes: {}, children: [{ tagName: 'custom-void', attributes: {} }] },
    ]);
  },
  'xml.read: whitespace-only text between tags is dropped'() {
    eqArr(xml.read('<a>\n  <b>x</b>\n</a>'), [{ tagName: 'a', attributes: {}, children: [{ tagName: 'b', attributes: {}, children: ['x'] }] }]);
  },
  'xml.read: multiple root-level siblings'() {
    eqArr(xml.read('<a/><b/>'), [{ tagName: 'a', attributes: {} }, { tagName: 'b', attributes: {} }]);
  },
  'xml.read: mismatched closing tag with a matching ancestor auto-closes intervening elements'() {
    /* </a> doesn't match the innermost open element (b), but does match an
     * ancestor (a) - both non-tolerant and tolerant modes recover here by
     * implicitly closing everything down to that ancestor; this isn't the
     * "no matching ancestor at all" error case (see next test). */
    let r = xml.read('<a><b></a>');

    eqArr(r, [{ tagName: 'a', attributes: {}, children: [{ tagName: 'b', attributes: {}, children: [] }] }]);
  },
  'xml.read: closing tag with no matching ancestor throws unless tolerant'() {
    assertThrows(() => xml.read('<a></c>'));

    let r = xml.read('<a></c>', 'f.xml', { tolerant: true });
    eqArr(r, [{ tagName: 'a', attributes: {}, children: [] }]);
  },
  'xml.read: location option returns a [tree, locations] tuple'() {
    let result = xml.read('<a>x</a>', 'f.xml', { location: true });

    assert(Array.isArray(result) && result.length === 2);
    eqArr(result[0], [{ tagName: 'a', attributes: {}, children: ['x'] }]);
  },
  'xml.read: throws a ReferenceError on empty/non-buffer input'() {
    assertThrows(() => xml.read(''));
  },

  /* ---------- xml.write() (legacy tree serializer) ---------- */
  'xml.write: round-trips a tree read back through xml.read()'() {
    let tree = xml.read('<root a="1"><child>hello</child><child2/></root>');
    let written = xml.write(tree);

    eqArr(xml.read(written), tree);
  },
  'xml.write: escapes special characters in attributes and text'() {
    let tree = [{ tagName: 'a', attributes: { x: '1 & "2" <3>' }, children: ['<tag> & amp'] }];
    let out = xml.write(tree);

    assert(out.includes('&amp;'), out);
    assert(out.includes('&lt;'), out);
    assert(out.includes('&gt;'), out);
    assert(out.includes('&quot;'), out);
    assert(!/[^&]<3>/.test(out), out);
  },
  'xml.write: self-closing element with no children renders with a slash'() {
    let out = xml.write([{ tagName: 'br', attributes: {} }]);

    assert(/<br\s*\/>/.test(out), out);
  },

  /* ---------- XMLSerializer (pull, .read()) ---------- */
  'XMLSerializer: read(n) round-trips a tree, any chunk size'() {
    let tree = [
      { tagName: 'root', attributes: { a: '1' }, children: [{ tagName: 'child', attributes: {}, children: ['hello'] }, { tagName: 'empty', attributes: {} }] },
    ];

    for(let bufSize of [1, 2, 3, 7, 1000]) {
      let s = new XMLSerializer(tree);
      let out = '',
        chunk;

      while((chunk = s.read(bufSize)) !== '') out += chunk;

      eqArr(xml.read(out), tree);
    }
  },
  'XMLSerializer: read(n) escapes special characters, resumable across tiny buffers'() {
    let tree = [{ tagName: 'a', attributes: { x: '1 & "2" <3>' }, children: ['text with <tag> & amp'] }];
    let outs = [];

    for(let bufSize of [1, 2, 3, 1000]) {
      let s = new XMLSerializer(tree);
      let out = '',
        chunk;

      while((chunk = s.read(bufSize)) !== '') out += chunk;

      outs.push(out);
    }

    /* every chunk size must produce byte-for-byte the same output - a bug in the
     * blocked/replay bookkeeping around a multi-character entity would show up as
     * a small-bufSize-only difference. */
    for(let out of outs) eq(out, outs[0]);

    assert(outs[0].includes('&amp;') && outs[0].includes('&quot;') && outs[0].includes('&lt;') && outs[0].includes('&gt;'), outs[0]);
  },
  'XMLSerializer: read(buffer) writes directly into an ArrayBuffer/TypedArray'() {
    let tree = [{ tagName: 'a', attributes: { x: '1' }, children: ['hello'] }];
    let s = new XMLSerializer(tree);
    let buf = new Uint8Array(1000);
    let n = s.read(buf);
    let str = String.fromCharCode(...buf.slice(0, n));

    let s2 = new XMLSerializer(tree);
    let out = '',
      chunk;

    while((chunk = s2.read(3)) !== '') out += chunk;

    eq(str, out);
  },
  'XMLSerializer: read(buffer) returns byte count, then 0 at EOF'() {
    let tree = [{ tagName: 'a', attributes: {}, children: ['x'] }];
    let s = new XMLSerializer(tree);
    let buf = new Uint8Array(1000);
    let n = s.read(buf);

    assert(n > 0);
    eq(s.read(buf), 0);
  },
  'XMLSerializer: .finished reflects completion'() {
    let tree = [{ tagName: 'a', attributes: {}, children: ['x'] }];
    let s = new XMLSerializer(tree);

    eq(s.finished, false);

    while(s.read(1000) !== '') {}

    eq(s.finished, true);
  },
  'XMLSerializer: .root returns the constructor argument'() {
    let tree = [{ tagName: 'a', attributes: {}, children: [] }];
    let s = new XMLSerializer(tree);

    eqArr(s.root, tree);
  },
  'XMLSerializer: a single (non-array) element root is wrapped'() {
    let s = new XMLSerializer({ tagName: 'a', attributes: {}, children: ['x'] });
    let out = '',
      chunk;

    while((chunk = s.read(1000)) !== '') out += chunk;

    eq(out, '<a>x</a>');
  },

  /* ---------- XMLPushParser (push, .write()/.close()) - builder fallback ---------- */
  'XMLPushParser: builder fallback (no callbacks) builds a tree via .root'() {
    let pp = new XMLPushParser();
    pp.write('<a x="1"><b/></a>');
    pp.close();

    eqArr(pp.root, [{ tagName: 'a', attributes: { x: '1' }, children: [{ tagName: 'b', attributes: {}, children: [] }] }]);
  },
  'XMLPushParser: fed one byte at a time'() {
    let pp = new XMLPushParser();
    let doc = '<a x="1"><b y="2"/><c/></a>';

    for(let i = 0; i < doc.length; i++) pp.write(doc[i]);
    pp.close();

    eqArr(pp.root, [
      {
        tagName: 'a',
        attributes: { x: '1' },
        children: [{ tagName: 'b', attributes: { y: '2' }, children: [] }, { tagName: 'c', attributes: {}, children: [] }],
      },
    ]);
  },
  'XMLPushParser: .path tracks the open-element stack, empty once closed'() {
    let pp = new XMLPushParser();
    pp.write('<a><b>');

    eqArr(pp.path, ['a', 'b']);

    pp.write('</b></a>');

    eqArr(pp.path, []);
  },
  'XMLPushParser: boolean (valueless) attributes'() {
    /* xread.c's go_attrib_eq[] dispatch table now recognizes '/', '>', or the
     * start of another attribute name as a valid terminator for an attribute
     * with no '=value' at all, matching XMLParser/xml.read() - see BUGS
     * (fixed): xread-no-boolean-attributes. */
    let pp = new XMLPushParser();
    pp.write('<input disabled type="text"/>');
    pp.close();

    eqArr(pp.root, [{ tagName: 'input', attributes: { disabled: true, type: 'text' }, children: [] }]);
  },
  'XMLPushParser: text content between tags'() {
    /* xread.c's go_root[]/go_text[] dispatch tables now accumulate non-'<' bytes
     * between tags as text content instead of erroring - see BUGS (fixed):
     * xread-no-text-content. */
    let pp = new XMLPushParser();
    pp.write('<a><b>text</b></a>');
    pp.close();

    eqArr(pp.root, [{ tagName: 'a', attributes: {}, children: [{ tagName: 'b', attributes: {}, children: ['text'] }] }]);
  },
  'XMLPushParser: close() throws on an element left open'() {
    let pp = new XMLPushParser();
    pp.write('<a><b/>');

    assertThrows(() => pp.close());
  },
  'XMLPushParser: close() throws mid-tag truncation'() {
    let pp = new XMLPushParser();
    pp.write('<a><b');

    assertThrows(() => pp.close());
  },
  'XMLPushParser: well-formed document with no trailing whitespace closes cleanly'() {
    let pp = new XMLPushParser();
    pp.write('<a><b/></a>');

    pp.close(); // must not throw

    eqArr(pp.path, []);
  },

  /* ---------- XMLPushParser - custom callbacks ---------- */
  'XMLPushParser: elementStart/attribute/elementEnd callbacks fire in document order'() {
    let events = [];
    let pp = new XMLPushParser({
      elementStart(name) {
        events.push(['start', name]);
      },
      attribute(name, value) {
        events.push(['attr', name, value]);
      },
      elementEnd(name) {
        events.push(['end', name]);
      },
    });

    pp.write('<a x="1"><b/></a>');
    pp.close();

    eqArr(events, [['start', 'a'], ['attr', 'x', '1'], ['start', 'b'], ['end', 'b'], ['end', 'a']]);
  },
  'XMLPushParser: custom callbacks mean .root stays empty (builder not fed)'() {
    let pp = new XMLPushParser({ elementStart() {} });
    pp.write('<a/>');
    pp.close();

    eqArr(pp.root, []);
  },
  'XMLPushParser: custom error callback receives the error, write() does not also throw'() {
    let errors = [];
    let pp = new XMLPushParser({
      elementStart() {},
      error(name, value) {
        errors.push([name, value]);
      },
    });

    pp.write('<1a/>'); // a tag name may not start with a digit - must not throw, error() owns this
    assert(errors.length > 0, JSON.stringify(errors));
  },
  'XMLPushParser: custom text callback fires for text content between tags'() {
    let texts = [];
    let pp = new XMLPushParser({
      elementStart() {},
      text(name, value) {
        texts.push(value);
      },
    });

    pp.write('<a>hi<b/>there</a>');
    pp.close();

    eqArr(texts, ['hi', 'there']);
  },
  'XMLPushParser: options object is also `this` inside callbacks'() {
    let seenThis;
    let options = {
      elementStart() {
        seenThis = this;
      },
    };
    let pp = new XMLPushParser(options);

    pp.write('<a/>');

    eq(seenThis, options);
  },

  /* ---------- XMLParser (pull, .parse()) ---------- */
  'XMLParser: is a constructor with named token-id constants'() {
    assert(typeof XMLParser === 'function');
    eq(XMLParser.PARSE_AGAIN, -2);
    eq(XMLParser.PARSE_ERROR, -1);
    eq(XMLParser.PARSE_OK, 0);
    eq(XMLParser.ELEMENT_START, 1);
    eq(XMLParser.ATTRIBUTE, 2);
    eq(XMLParser.ELEMENT_END, 3);
    eq(XMLParser.TEXT, 4);
  },
  'XMLParser: basic token sequence for a well-formed document'() {
    let { out } = drain('<a><b>hi</b></a>');
    let toks = out.map(e => e[0]);

    eqArr(toks, [XMLParser.ELEMENT_START, XMLParser.ELEMENT_START, XMLParser.TEXT, XMLParser.ELEMENT_END, XMLParser.ELEMENT_END, XMLParser.PARSE_OK]);
  },
  'XMLParser: regression - final closing tag at end of input (no trailing whitespace) is not dropped'() {
    /* Was a real bug: src/xml.c's closing-tag handling checked p->done right
     * after consuming the tag's '>' and bailed out before yielding
     * XML_ELEMENT_END whenever that '>' happened to be the last byte of the
     * whole document - so the outermost element's end event (and only that
     * one) silently vanished unless there was at least one more byte (e.g. a
     * trailing newline) after it. */
    let { out } = drain('<a><b></b></a>');
    let toks = out.map(e => e[0]);

    eqArr(toks, [XMLParser.ELEMENT_START, XMLParser.ELEMENT_START, XMLParser.ELEMENT_END, XMLParser.ELEMENT_END, XMLParser.PARSE_OK]);

    let withTrailingWs = drain('<a><b></b></a>\n').out.map(e => e[0]);
    eqArr(withTrailingWs, toks);
  },
  'XMLParser: attribute events distinguish valued vs. boolean attributes'() {
    let { out } = drain('<input disabled type="text">');

    eqArr(out.map(e => [e[0], e[1], e[2]]).slice(1, 3), [
      [XMLParser.ATTRIBUTE, 'disabled', null],
      [XMLParser.ATTRIBUTE, 'type', 'text'],
    ]);
  },
  'XMLParser: default self-closing tags (e.g. input) need no explicit slash'() {
    let { out } = drain('<div><input type="text"><p>x</p></div>');
    let toks = out.map(e => e[0]);

    eqArr(toks, [
      XMLParser.ELEMENT_START, // div
      XMLParser.ELEMENT_START, // input
      XMLParser.ATTRIBUTE, // type
      XMLParser.ELEMENT_END, // input (self-closing)
      XMLParser.ELEMENT_START, // p
      XMLParser.TEXT,
      XMLParser.ELEMENT_END, // p
      XMLParser.ELEMENT_END, // div
      XMLParser.PARSE_OK,
    ]);
  },
  'XMLParser: comments come through as a single start+end pair'() {
    let { out } = drain('<a><!-- hi --><b/></a>');
    let toks = out.map(e => e[0]);

    eqArr(toks, [
      XMLParser.ELEMENT_START, // a
      XMLParser.ELEMENT_START, // comment
      XMLParser.ELEMENT_END, // comment
      XMLParser.ELEMENT_START, // b
      XMLParser.ELEMENT_END, // b
      XMLParser.ELEMENT_END, // a
      XMLParser.PARSE_OK,
    ]);
  },
  'XMLParser: .depth tracks the open-element stack'() {
    let p = new XMLParser('<a><b><c/></b></a>');

    eq(p.parse(), XMLParser.ELEMENT_START); // a
    eq(p.depth, 0);

    eq(p.parse(), XMLParser.ELEMENT_START); // b
    eq(p.depth, 1);

    eq(p.parse(), XMLParser.ELEMENT_START); // c
    eq(p.depth, 2);
  },
  'XMLParser: .root optionally builds a tree (via the same XmlBuilder as XMLPushParser)'() {
    let { parser } = drain('<root a="1"><child>hello <b>world</b></child></root>');

    eqArr(parser.root, [
      {
        tagName: 'root',
        attributes: { a: '1' },
        children: [{ tagName: 'child', attributes: {}, children: ['hello', { tagName: 'b', attributes: {}, children: ['world'] }] }],
      },
    ]);
  },
  'XMLParser: .location tracks line/column/byteOffset and advances'() {
    let p = new XMLParser('<a>\n<b/>\n</a>');
    let locs = [];

    for(let i = 0; i < 10; i++) {
      let tok = p.parse();

      locs.push({ tok, line: p.location.line, column: p.location.column });

      if(tok === XMLParser.PARSE_OK) break;
    }

    assert(locs[0].line === 1, JSON.stringify(locs));
    assert(locs.some(l => l.line === 2), JSON.stringify(locs));
  },
  'XMLParser: .location.file reflects the constructor filename argument'() {
    let { parser } = drain('<a/>', 'my-input.xml');

    eq(parser.location.file, 'my-input.xml');
  },
  'XMLParser: mismatched closing tag is an error unless .tolerant'() {
    let { out } = drain('<a></c>');
    let last = out[out.length - 1];

    eq(last[0], XMLParser.PARSE_ERROR);
    eq(last[1], 'c');
  },
  'XMLParser: .tolerant suppresses the mismatched-closing-tag error'() {
    let p = new XMLParser('<a></c>');
    p.tolerant = true;

    let toks = [];
    for(let i = 0; i < 10; i++) {
      let tok = p.parse();
      toks.push(tok);
      if(tok === XMLParser.PARSE_OK || tok === XMLParser.PARSE_ERROR) break;
    }

    eqArr(toks, [XMLParser.ELEMENT_START, XMLParser.PARSE_OK]);
  },
  'XMLParser: .tolerant getter reflects what was set'() {
    let p = new XMLParser('<a/>');

    eq(p.tolerant, false);
    p.tolerant = true;
    eq(p.tolerant, true);
  },
  'XMLParser: constructor accepts a pull function (fn(buffer, len) -> bytesRead)'() {
    let p = new XMLParser(makeReader('<a>x</a>'));
    let toks = [];

    for(let i = 0; i < 10; i++) {
      let tok = p.parse();
      toks.push(tok);
      if(tok === XMLParser.PARSE_OK) break;
    }

    eqArr(toks, [XMLParser.ELEMENT_START, XMLParser.TEXT, XMLParser.ELEMENT_END, XMLParser.PARSE_OK]);
    eqArr(p.root, [{ tagName: 'a', attributes: {}, children: ['x'] }]);
  },
  'XMLParser: constructor accepts an object exposing a read(buffer, len) method'() {
    let p = new XMLParser({ read: makeReader('<a>y</a>') });
    let toks = [];

    for(let i = 0; i < 10; i++) {
      let tok = p.parse();
      toks.push(tok);
      if(tok === XMLParser.PARSE_OK) break;
    }

    eqArr(p.root, [{ tagName: 'a', attributes: {}, children: ['y'] }]);
  },
  'XMLParser: PARSE_AGAIN suspends and later resumes correctly'() {
    let p = new XMLParser(makeReader('<a>x</a>', 3));
    let toks = [];

    for(let i = 0; i < 10; i++) {
      let tok = p.parse();
      toks.push(tok);
      if(tok === XMLParser.PARSE_OK) break;
    }

    assert(toks.includes(XMLParser.PARSE_AGAIN), JSON.stringify(toks));
    eqArr(p.root, [{ tagName: 'a', attributes: {}, children: ['x'] }]);
  },
  'XMLParser: entities are decoded on read (see BUGS: fixed - xml-no-entity-decoding)'() {
    let { parser } = drain('<a x="1 &amp; 2">&lt;text&gt; &#65; &#x42;</a>');

    eq(parser.root[0].attributes.x, '1 & 2');
    eq(parser.root[0].children[0], '<text> A B');
  },

  /* ---------- write -> read round-trip across the whole module ---------- */
  'round-trip: XMLSerializer output re-parses via XMLParser to the same shape (no entities involved)'() {
    let tree = [
      {
        tagName: 'root',
        attributes: { id: 'x1', class: 'main' },
        children: [{ tagName: 'child', attributes: { n: '1' }, children: ['plain text'] }, { tagName: 'empty', attributes: {}, children: [] }],
      },
    ];

    let s = new XMLSerializer(tree);
    let out = '',
      chunk;

    while((chunk = s.read(5)) !== '') out += chunk;

    let { parser } = drain(out);

    eqArr(parser.root, tree);
  },
  'round-trip: XMLSerializer output re-parses via XMLParser to the same shape (entities involved)'() {
    let tree = [{ tagName: 'root', attributes: { x: '<>&"' }, children: ['<>&'] }];

    let s = new XMLSerializer(tree);
    let out = '',
      chunk;

    while((chunk = s.read(5)) !== '') out += chunk;

    let { parser } = drain(out);

    eqArr(parser.root, tree);
  },
});
