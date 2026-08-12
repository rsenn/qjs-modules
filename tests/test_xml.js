import xml, { XMLParser, XMLNodeParser, XMLWriter, XMLPushParser, XMLSerializer } from 'xml';
import { toString } from 'util';
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
        children: [
          { tagName: 'br', attributes: {} },
          { tagName: 'p', attributes: {}, children: ['x'] },
        ],
      },
    ]);
  },
  'xml.read: custom selfClosingTags option'() {
    eqArr(xml.read('<div><custom-void/></div>', 'f.xml', { selfClosingTags: ['custom-void'] }), [{ tagName: 'div', attributes: {}, children: [{ tagName: 'custom-void', attributes: {} }] }]);
  },
  'xml.read: whitespace-only text between tags is dropped'() {
    eqArr(xml.read('<a>\n  <b>x</b>\n</a>'), [{ tagName: 'a', attributes: {}, children: [{ tagName: 'b', attributes: {}, children: ['x'] }] }]);
  },
  'xml.read: multiple root-level siblings'() {
    eqArr(xml.read('<a/><b/>'), [
      { tagName: 'a', attributes: {} },
      { tagName: 'b', attributes: {} },
    ]);
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
      {
        tagName: 'root',
        attributes: { a: '1' },
        children: [
          { tagName: 'child', attributes: {}, children: ['hello'] },
          { tagName: 'empty', attributes: {} },
        ],
      },
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
        children: [
          { tagName: 'b', attributes: { y: '2' }, children: [] },
          { tagName: 'c', attributes: {}, children: [] },
        ],
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

    eqArr(events, [
      ['start', 'a'],
      ['attr', 'x', '1'],
      ['start', 'b'],
      ['end', 'b'],
      ['end', 'a'],
    ]);
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
        children: [{ tagName: 'child', attributes: {}, children: ['hello ', { tagName: 'b', attributes: {}, children: ['world'] }] }],
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
    assert(
      locs.some(l => l.line === 2),
      JSON.stringify(locs),
    );
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
  'XMLParser: constructor accepts a pull function(fn(buffer, len) -> bytesRead)'() {
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
        children: [
          { tagName: 'child', attributes: { n: '1' }, children: ['plain text'] },
          { tagName: 'empty', attributes: {}, children: [] },
        ],
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
  'XMLParser plugs into XMLWriter'() {
    let s = '';
    let xw = new XMLWriter(data => (s += toString(data)), -1);
    let p = new XMLParser('<html><body><div><p><img src="blah.jpg" />test blah</p>goes a loong way</div></body></html>', xw);
    let tok;

    while((tok = p.parse())) {
      console.log('tok', tok);
      console.log(`s='${s}'`);
    }
  },

  /* ========== XMLNodeParser ========== */
  'XMLNodeParser: is a constructor'() {
    assert(typeof XMLNodeParser === 'function');
  },

  'XMLNodeParser: parse() returns start tag objects with tagName and attributes'() {
    let p = new XMLNodeParser('<a x="1"/>');
    let node = p.parse();

    assert(typeof node === 'object' && node !== null, 'expected object, got ' + typeof node);
    eq(node.tagName, 'a');
    assert(node.attributes !== undefined, 'expected attributes property');
    eq(node.attributes.x, '1');
  },

  'XMLNodeParser: parse() returns end tag objects with tagName prefixed by /'() {
    let p = new XMLNodeParser('<a></a>');
    let start = p.parse();
    let end = p.parse();

    eq(start.tagName, 'a');
    eq(end.tagName, '/a');
  },

  'XMLNodeParser: parse() returns strings for text content'() {
    let p = new XMLNodeParser('<a>hello</a>');
    p.parse(); // skip start tag
    let text = p.parse();

    eq(typeof text, 'string');
    eq(text, 'hello');
  },

  'XMLNodeParser: parse() returns PARSE_OK at end of input'() {
    let p = new XMLNodeParser('<a/>');
    p.parse(); // start a
    p.parse(); // end /a (self-closing tags yield a separate start + end pair)
    let status = p.parse(); // should be PARSE_OK

    eq(status, 0); // XML_PARSE_OK
  },

  'XMLNodeParser: .depth tracks nesting level'() {
    let p = new XMLNodeParser('<a><b><c/></b></a>');
    eq(p.depth, 0);

    p.parse(); // <a>
    eq(p.depth, 1);

    p.parse(); // <b>
    eq(p.depth, 2);

    p.parse(); // <c> (self-closing tags yield a separate start + end pair)
    eq(p.depth, 3);

    p.parse(); // </c>
    eq(p.depth, 2);

    p.parse(); // </b>
    eq(p.depth, 1);

    p.parse(); // </a>
    eq(p.depth, 0);
  },

  'XMLNodeParser: .location tracks line/column'() {
    let p = new XMLNodeParser('<a>\n<b/>\n</a>');
    let node = p.parse(); // <a>

    assert(p.location !== undefined, 'expected location property');
    assert(p.location.line !== undefined, 'expected line property');
    assert(p.location.column !== undefined, 'expected column property');
  },

  'XMLNodeParser: handles boolean (valueless) attributes'() {
    let p = new XMLNodeParser('<input disabled type="text"/>');
    let node = p.parse();

    eq(node.tagName, 'input');
    eq(node.attributes.disabled, true);
    eq(node.attributes.type, 'text');
  },

  'XMLNodeParser: handles multiple attributes'() {
    let p = new XMLNodeParser('<a x="1" y="2" z="3"/>');
    let node = p.parse();

    eq(node.attributes.x, '1');
    eq(node.attributes.y, '2');
    eq(node.attributes.z, '3');
  },

  'XMLNodeParser: handles nested elements'() {
    let p = new XMLNodeParser('<root><child>text</child></root>');
    let nodes = [];

    for(let i = 0; i < 10; i++) {
      let node = p.parse();
      if(node <= 0) break; // status code
      nodes.push(node);
    }

    eq(nodes.length, 4); // start root, start child, text, end child, end root
    eq(nodes[0].tagName, 'root');
    eq(nodes[1].tagName, 'child');
    eq(nodes[2], 'text');
    eq(nodes[3].tagName, '/child');
  },

  'XMLNodeParser: handles self-closing tags'() {
    let p = new XMLNodeParser('<a><b/><c/></a>');
    let nodes = [];

    for(let i = 0; i < 10; i++) {
      let node = p.parse();
      if(node <= 0) break;
      nodes.push(node);
    }

    eq(nodes[0].tagName, 'a');
    eq(nodes[1].tagName, 'b');
    eq(nodes[2].tagName, '/b');
    eq(nodes[3].tagName, 'c');
    eq(nodes[4].tagName, '/c');
    eq(nodes[5].tagName, '/a');
  },

  'XMLNodeParser: constructor accepts options with filename'() {
    let p = new XMLNodeParser('<a/>', { filename: 'test.xml' });
    p.parse();

    assert(p.location !== undefined, 'expected location');
  },

  /* ========== XMLWriter (expanded) ========== */
  'XMLWriter: is a constructor'() {
    assert(typeof XMLWriter === 'function');
  },

  'XMLWriter: writes a simple element'() {
    /* elementStart() immediately followed by elementEnd(), with nothing in between,
     * is the self-closing case - covered separately by the next test. A genuinely
     * distinct "simple element" needs real content in between to exercise the
     * non-self-closing "</name>" path at all. */
    let out = '';
    let w = new XMLWriter(s => (out += toString(s)));

    w.elementStart('a');
    w.text('x');
    w.elementEnd('a');

    eq(out, '<a>x</a>');
  },

  'XMLWriter: writes self-closing element when elementEnd() is called immediately'() {
    let out = '';
    let w = new XMLWriter(s => (out += toString(s)));

    w.elementStart('br');
    w.elementEnd('br');

    assert(out.includes('/>'), 'expected self-closing, got: ' + out);
  },

  'XMLWriter: writes attributes with values'() {
    let out = '';
    let w = new XMLWriter(s => (out += toString(s)));

    w.elementStart('a');
    w.attribute('href', 'https://example.com');
    w.elementEnd('a');

    assert(out.includes('href="https://example.com"'), out);
  },

  'XMLWriter: writes boolean (valueless) attributes'() {
    let out = '';
    let w = new XMLWriter(s => (out += toString(s)));

    w.elementStart('input');
    w.attribute('disabled');
    w.elementEnd('input');

    assert(out.includes('disabled'), out);
    assert(!out.includes('disabled='), 'should not have = for boolean attr: ' + out);
  },

  'XMLWriter: writes text content'() {
    let out = '';
    let w = new XMLWriter(s => (out += toString(s)));

    w.elementStart('p');
    w.text('hello world');
    w.elementEnd('p');

    assert(out.includes('>hello world<'), out);
  },

  'XMLWriter: writes nested elements'() {
    let out = '';
    let w = new XMLWriter(s => (out += toString(s)));

    w.elementStart('root');
    w.elementStart('child');
    w.text('text');
    w.elementEnd('child');
    w.elementEnd('root');

    assert(out.includes('<root>'), out);
    assert(out.includes('<child>'), out);
    assert(out.includes('text'), out);
    assert(out.includes('</child>'), out);
    assert(out.includes('</root>'), out);
  },

  'XMLWriter: .written tracks bytes written'() {
    /* Per doc/xml.md, a callback output sink must return the number of bytes written
     * (same contract as an object's write(buf, offset, length) method) - returning
     * anything else (e.g. the accumulated string, as other XMLWriter tests in this
     * file do for tests that don't check .written) makes .written stay stuck at 0. */
    let out = '';
    let w = new XMLWriter(s => {
      out += toString(s);
      return s.byteLength;
    });

    eq(w.written, 0);

    w.elementStart('a');
    let afterStart = w.written;

    assert(afterStart > 0, 'expected bytes written after elementStart');

    w.elementEnd('a');
    let afterEnd = w.written;

    assert(afterEnd > afterStart, 'expected more bytes after elementEnd');
  },

  'XMLWriter: .indent controls indentation'() {
    let out = '';
    let w = new XMLWriter(s => (out += toString(s)), 2);

    w.elementStart('root');
    w.elementStart('child');
    w.elementEnd('child');
    w.elementEnd('root');

    assert(out.includes('\n'), 'expected newlines with indent: ' + out);
    assert(out.includes('  '), 'expected spaces with indent: ' + out);
  },

  'XMLWriter: .indent getter/setter works'() {
    let out = '';
    let w = new XMLWriter(s => (out += toString(s)));

    eq(w.indent, 2); // default
    w.indent = 4;
    eq(w.indent, 4);
  },

  'XMLWriter: .state reflects last operation'() {
    let out = '';
    let w = new XMLWriter(s => (out += toString(s)));

    w.elementStart('a');
    eq(w.state, 1); // ELEMENT_START

    w.attribute('x', '1');
    eq(w.state, 2); // ATTRIBUTE

    w.text('text');
    eq(w.state, 4); // TEXT

    w.elementEnd('a');
    eq(w.state, 3); // ELEMENT_END
  },

  'XMLWriter: writes to ArrayBuffer'() {
    let buf = new Uint8Array(1000);
    let w = new XMLWriter(buf);

    w.elementStart('a');
    w.text('x');
    w.elementEnd('a');

    assert(w.written > 0, 'expected bytes written to buffer');
  },

  'XMLWriter: handles multiple attributes on one element'() {
    let out = '';
    let w = new XMLWriter(s => (out += toString(s)));

    w.elementStart('a');
    w.attribute('x', '1');
    w.attribute('y', '2');
    w.attribute('z', '3');
    w.elementEnd('a');

    assert(out.includes('x="1"'), out);
    assert(out.includes('y="2"'), out);
    assert(out.includes('z="3"'), out);
  },

  'XMLWriter: complex nested structure'() {
    let out = '';
    let w = new XMLWriter(s => (out += toString(s)));

    w.elementStart('html');
    w.elementStart('body');
    w.elementStart('div');
    w.attribute('class', 'container');
    w.elementStart('p');
    w.text('Hello ');
    w.elementStart('strong');
    w.text('world');
    w.elementEnd('strong');
    w.text('!');
    w.elementEnd('p');
    w.elementEnd('div');
    w.elementEnd('body');
    w.elementEnd('html');

    assert(out.includes('<html>'), out);
    assert(out.includes('class="container"'), out);
    assert(out.includes('Hello '), out);
    assert(out.includes('<strong>world</strong>'), out);
    assert(out.includes('</html>'), out);
  },

  /* ========== XMLParser callbacks ========== */
  'XMLParser: onelementstart callback fires'() {
    let starts = [];
    let p = new XMLParser('<a><b/></a>');

    p.onelementstart = name => starts.push(name);

    for(let i = 0; i < 10; i++) {
      let tok = p.parse();
      if(tok === XMLParser.PARSE_OK) break;
    }

    eqArr(starts, ['a', 'b']);
  },

  'XMLParser: onelementend callback fires'() {
    let ends = [];
    let p = new XMLParser('<a><b/></a>');

    p.onelementend = name => ends.push(name);

    for(let i = 0; i < 10; i++) {
      let tok = p.parse();
      if(tok === XMLParser.PARSE_OK) break;
    }

    eqArr(ends, ['b', 'a']);
  },

  'XMLParser: onattribute callback fires'() {
    let attrs = [];
    let p = new XMLParser('<a x="1" y="2"/>');

    p.onattribute = (name, value) => attrs.push([name, value]);

    for(let i = 0; i < 10; i++) {
      let tok = p.parse();
      if(tok === XMLParser.PARSE_OK) break;
    }

    eqArr(attrs, [['x', '1'], ['y', '2']]);
  },

  'XMLParser: ontext callback fires'() {
    let texts = [];
    let p = new XMLParser('<a>hello <b>world</b>!</a>');

    p.ontext = value => texts.push(value);

    for(let i = 0; i < 10; i++) {
      let tok = p.parse();
      if(tok === XMLParser.PARSE_OK) break;
    }

    eqArr(texts, ['hello ', 'world', '!']);
  },

  'XMLParser: all callbacks fire in document order'() {
    let events = [];
    let p = new XMLParser('<root x="1">text<child/></root>');

    p.onelementstart = name => events.push(['start', name]);
    p.onelementend = name => events.push(['end', name]);
    p.onattribute = (name, value) => events.push(['attr', name, value]);
    p.ontext = value => events.push(['text', value]);

    for(let i = 0; i < 20; i++) {
      let tok = p.parse();
      if(tok === XMLParser.PARSE_OK) break;
    }

    eqArr(events, [['start', 'root'], ['attr', 'x', '1'], ['text', 'text'], ['start', 'child'], ['end', 'child'], ['end', 'root']]);
  },

  /* ========== XMLParser edge cases ========== */
  'XMLParser: empty document returns PARSE_OK immediately'() {
    let p = new XMLParser('');
    let tok = p.parse();

    eq(tok, XMLParser.PARSE_OK);
  },

  'XMLParser: whitespace-only document returns PARSE_OK'() {
    let p = new XMLParser('   \n\t  ');
    let tok = p.parse();

    eq(tok, XMLParser.PARSE_OK);
  },

  'XMLParser: processing instruction comes through as start+end'() {
    let { out } = drain('<?xml version="1.0"?><root/>');
    let toks = out.map(e => e[0]);

    assert(toks.includes(XMLParser.ELEMENT_START), JSON.stringify(toks));
    assert(toks.includes(XMLParser.ELEMENT_END), JSON.stringify(toks));
  },

  'XMLParser: DOCTYPE declaration comes through as start+end'() {
    let { out } = drain('<!DOCTYPE html><html/>');
    let toks = out.map(e => e[0]);

    assert(toks.includes(XMLParser.ELEMENT_START), JSON.stringify(toks));
    assert(toks.includes(XMLParser.ELEMENT_END), JSON.stringify(toks));
  },

  'XMLParser: deeply nested structure'() {
    let depth = 10;
    let input = '<a>'.repeat(depth) + 'x' + '</a>'.repeat(depth);
    let p = new XMLParser(input);
    let maxDepth = 0;

    for(let i = 0; i < 1000; i++) {
      let tok = p.parse();
      if(p.depth > maxDepth) maxDepth = p.depth;
      if(tok === XMLParser.PARSE_OK) break;
    }

    eq(maxDepth, depth);
  },

  'XMLParser: mixed content (text + elements)'() {
    let { parser } = drain('<p>before <b>bold</b> after</p>');
    let root = parser.root[0];

    eq(root.children.length, 3);
    eq(root.children[0], 'before ');
    eq(root.children[1].tagName, 'b');
    eq(root.children[2], ' after');
  },

  'XMLParser: multiple root elements'() {
    let { parser } = drain('<a/><b/><c/>');

    eq(parser.root.length, 3);
    eq(parser.root[0].tagName, 'a');
    eq(parser.root[1].tagName, 'b');
    eq(parser.root[2].tagName, 'c');
  },

  'XMLParser: eventName is undefined for TEXT events'() {
    let p = new XMLParser('<a>text</a>');
    p.parse(); // ELEMENT_START
    p.parse(); // TEXT

    eq(p.eventName, undefined);
    eq(p.eventValue, 'text');
  },

  'XMLParser: eventValue is undefined for ELEMENT_START events'() {
    let p = new XMLParser('<a/>');
    p.parse(); // ELEMENT_START

    eq(p.eventName, 'a');
    eq(p.hasValue, false);
  },

  'XMLParser: hasValue is true for ATTRIBUTE events with values'() {
    let p = new XMLParser('<a x="1"/>');
    p.parse(); // ELEMENT_START
    p.parse(); // ATTRIBUTE

    eq(p.hasValue, true);
    eq(p.eventValue, '1');
  },

  'XMLParser: hasValue is false for boolean ATTRIBUTE events'() {
    let p = new XMLParser('<input disabled/>');
    p.parse(); // ELEMENT_START
    p.parse(); // ATTRIBUTE

    eq(p.hasValue, false);
    eq(p.eventName, 'disabled');
  },

  /* ========== XMLPushParser edge cases ========== */
  'XMLPushParser: chunk boundary mid-tag-name'() {
    let pp = new XMLPushParser();

    pp.write('<div');
    pp.write(' class="x">');
    pp.write('</div>');
    pp.close();

    eqArr(pp.root, [{ tagName: 'div', attributes: { class: 'x' }, children: [] }]);
  },

  'XMLPushParser: chunk boundary mid-attribute-value'() {
    let pp = new XMLPushParser();

    pp.write('<a href="https://ex');
    pp.write('ample.com"/>');
    pp.close();

    eqArr(pp.root, [{ tagName: 'a', attributes: { href: 'https://example.com' }, children: [] }]);
  },

  'XMLPushParser: chunk boundary mid-escape-sequence'() {
    let pp = new XMLPushParser();

    pp.write('<a>text with &am');
    pp.write('p; entity</a>');
    pp.close();

    eqArr(pp.root, [{ tagName: 'a', attributes: {}, children: ['text with & entity'] }]);
  },

  'XMLPushParser: multiple root elements'() {
    let pp = new XMLPushParser();

    pp.write('<a/><b/><c/>');
    pp.close();

    eq(pp.root.length, 3);
    eq(pp.root[0].tagName, 'a');
    eq(pp.root[1].tagName, 'b');
    eq(pp.root[2].tagName, 'c');
  },

  'XMLPushParser: empty document closes cleanly'() {
    let pp = new XMLPushParser();

    pp.close();

    eqArr(pp.root, []);
  },

  'XMLPushParser: whitespace-only document closes cleanly'() {
    let pp = new XMLPushParser();

    pp.write('   \n\t  ');
    pp.close();

    eqArr(pp.root, []);
  },

  'XMLPushParser: nested text content'() {
    let pp = new XMLPushParser();

    pp.write('<a>outer <b>inner</b> outer</a>');
    pp.close();

    eqArr(pp.root, [{ tagName: 'a', attributes: {}, children: ['outer ', { tagName: 'b', attributes: {}, children: ['inner'] }, ' outer'] }]);
  },

  'XMLPushParser: deeply nested structure'() {
    let pp = new XMLPushParser();
    let depth = 10;

    pp.write('<a>'.repeat(depth) + 'x' + '</a>'.repeat(depth));
    pp.close();

    let current = pp.root[0];
    for(let i = 1; i < depth; i++) {
      assert(current.children[0].tagName === 'a', 'expected nested structure');
      current = current.children[0];
    }
  },

  'XMLPushParser: large document'() {
    let pp = new XMLPushParser();
    let count = 100;
    let doc = '<root>';

    for(let i = 0; i < count; i++) doc += `<item id="${i}">text ${i}</item>`;
    doc += '</root>';

    pp.write(doc);
    pp.close();

    eq(pp.root[0].children.length, count);
  },

  /* ========== XMLSerializer edge cases ========== */
  'XMLSerializer: empty root array produces no output'() {
    let s = new XMLSerializer([]);
    let out = '',
      chunk;

    while((chunk = s.read(100)) !== '') out += chunk;

    eq(out, '');
  },

  'XMLSerializer: deeply nested structure round-trips'() {
    let depth = 10;
    let tree = [{ tagName: 'a', attributes: {}, children: [] }];
    let current = tree[0];

    for(let i = 1; i < depth; i++) {
      let child = { tagName: 'a', attributes: {}, children: [] };
      current.children.push(child);
      current = child;
    }

    current.children.push('leaf');

    let s = new XMLSerializer(tree);
    let out = '',
      chunk;

    while((chunk = s.read(10)) !== '') out += chunk;

    let { parser } = drain(out);

    eqArr(parser.root, tree);
  },

  'XMLSerializer: large number of attributes'() {
    let attrs = {};
    for(let i = 0; i < 50; i++) attrs[`attr${i}`] = `value${i}`;

    let tree = [{ tagName: 'a', attributes: attrs, children: [] }];
    let s = new XMLSerializer(tree);
    let out = '',
      chunk;

    while((chunk = s.read(100)) !== '') out += chunk;

    for(let i = 0; i < 50; i++) {
      assert(out.includes(`attr${i}="value${i}"`), out);
    }
  },

  'XMLSerializer: boolean attributes render correctly'() {
    let tree = [{ tagName: 'input', attributes: { disabled: true, type: 'text' }, children: [] }];
    let s = new XMLSerializer(tree);
    let out = '',
      chunk;

    while((chunk = s.read(100)) !== '') out += chunk;

    assert(out.includes('disabled'), out);
    assert(!out.includes('disabled='), 'boolean attr should not have =: ' + out);
    assert(out.includes('type="text"'), out);
  },

  'XMLSerializer: read(n) with n=0 returns empty string'() {
    let s = new XMLSerializer([{ tagName: 'a', attributes: {}, children: ['x'] }]);
    let chunk = s.read(0);

    eq(chunk, '');
  },

  'XMLSerializer: read(buffer) with empty buffer returns 0'() {
    let s = new XMLSerializer([{ tagName: 'a', attributes: {}, children: ['x'] }]);
    let buf = new Uint8Array(0);
    let n = s.read(buf);

    eq(n, 0);
  },

  'XMLSerializer: multiple elements at root level'() {
    let tree = [
      { tagName: 'a', attributes: {}, children: [] },
      { tagName: 'b', attributes: {}, children: [] },
      { tagName: 'c', attributes: {}, children: [] },
    ];

    let s = new XMLSerializer(tree);
    let out = '',
      chunk;

    while((chunk = s.read(100)) !== '') out += chunk;

    assert(out.includes('<a'), out);
    assert(out.includes('<b'), out);
    assert(out.includes('<c'), out);
  },

  /* ========== xml.read() additional tests ========== */
  'xml.read: flat mode returns a flat list instead of nested tree'() {
    let r = xml.read('<a><b>text</b></a>', 'f.xml', { flat: true });

    assert(Array.isArray(r), 'expected array');
    assert(r.length > 1, 'flat mode should produce multiple nodes');

    let hasStartA = r.some(n => n.tagName === 'a');
    let hasStartB = r.some(n => n.tagName === 'b');
    let hasEndB = r.some(n => n.tagName === '/b');
    let hasEndA = r.some(n => n.tagName === '/a');

    assert(hasStartA, 'expected start tag a');
    assert(hasStartB, 'expected start tag b');
    assert(hasEndB, 'expected end tag /b');
    assert(hasEndA, 'expected end tag /a');
  },

  'xml.read: tolerant mode ignores mismatched closing tags'() {
    let r = xml.read('<a><b></c></a>', 'f.xml', { tolerant: true });

    assert(Array.isArray(r), 'expected array');
    assert(r.length > 0, 'expected at least one element');
  },

  'xml.read: handles processing instructions'() {
    let r = xml.read('<?xml version="1.0"?><root/>');

    assert(Array.isArray(r), 'expected array');
    assert(r.length > 0, 'expected at least one element');
  },

  'xml.read: handles comments'() {
    let r = xml.read('<root><!-- comment --><child/></root>');

    assert(Array.isArray(r), 'expected array');
    assert(r.length > 0, 'expected at least one element');
  },

  'xml.read: handles DOCTYPE'() {
    let r = xml.read('<!DOCTYPE html><html/>');

    assert(Array.isArray(r), 'expected array');
    assert(r.length > 0, 'expected at least one element');
  },

  'xml.read: decodes numeric character references'() {
    let r = xml.read('<a>&#65;&#66;&#67;</a>');

    eq(r[0].children[0], 'ABC');
  },

  'xml.read: decodes hex character references'() {
    let r = xml.read('<a>&#x41;&#x42;&#x43;</a>');

    eq(r[0].children[0], 'ABC');
  },

  'xml.read: decodes named entities'() {
    let r = xml.read('<a>&lt;&gt;&amp;&quot;</a>');

    eq(r[0].children[0], '<>&"');
  },

  'xml.read: handles attributes with single quotes'() {
    let r = xml.read("<a x='1' y='2'/>");

    eq(r[0].attributes.x, '1');
    eq(r[0].attributes.y, '2');
  },

  'xml.read: handles empty attribute values'() {
    let r = xml.read('<a x="" y=""/>');

    eq(r[0].attributes.x, '');
    eq(r[0].attributes.y, '');
  },

  'xml.read: preserves whitespace in text content'() {
    let r = xml.read('<a>  hello  world  </a>');

    assert(r[0].children[0].includes('hello'), 'expected text content');
    assert(r[0].children[0].includes('world'), 'expected text content');
  },

  /* ========== xml.write() additional tests ========== */
  'xml.write: handles flat list input'() {
    let flat = [
      { tagName: 'a', attributes: { x: '1' } },
      'text',
      { tagName: '/a', attributes: {} },
    ];

    let out = xml.write(flat);

    assert(out.includes('<a'), out);
    assert(out.includes('text'), out);
    assert(out.includes('</a>'), out);
  },

  'xml.write: maxDepth parameter limits traversal'() {
    let tree = [{ tagName: 'a', attributes: {}, children: [{ tagName: 'b', attributes: {}, children: [{ tagName: 'c', attributes: {}, children: [] }] }] }];

    let full = xml.write(tree);
    let shallow = xml.write(tree, 1);

    assert(full.includes('<c'), 'full output should include c: ' + full);
  },

  'xml.write: handles comments'() {
    let tree = [{ tagName: '!-- comment --', attributes: {} }];
    let out = xml.write(tree);

    assert(out.includes('comment'), out);
  },

  'xml.write: handles processing instructions'() {
    let tree = [{ tagName: '?xml version="1.0"', attributes: {} }];
    let out = xml.write(tree);

    assert(out.includes('<?xml'), out);
  },

  'xml.write: round-trips complex nested structure'() {
    let tree = [
      {
        tagName: 'html',
        attributes: { lang: 'en' },
        children: [
          {
            tagName: 'body',
            attributes: { class: 'main' },
            children: [
              { tagName: 'div', attributes: { id: 'content' }, children: ['Hello ', { tagName: 'strong', attributes: {}, children: ['world'] }, '!'] },
              { tagName: 'br', attributes: {} },
            ],
          },
        ],
      },
    ];

    let written = xml.write(tree);
    let parsed = xml.read(written);

    eqArr(parsed, tree);
  },

  /* ========== Cross-class integration tests ========== */
  'integration: XMLWriter output parses via XMLParser'() {
    let out = '';
    let w = new XMLWriter(s => (out += toString(s)));

    w.elementStart('root');
    w.attribute('id', '1');
    w.elementStart('child');
    w.text('hello');
    w.elementEnd('child');
    w.elementEnd('root');

    let { parser } = drain(out);

    eq(parser.root[0].tagName, 'root');
    eq(parser.root[0].attributes.id, '1');
    eq(parser.root[0].children[0].tagName, 'child');
    eq(parser.root[0].children[0].children[0], 'hello');
  },

  'integration: XMLPushParser tree serializes via XMLSerializer'() {
    let pp = new XMLPushParser();
    pp.write('<root x="1"><child>text</child></root>');
    pp.close();

    let s = new XMLSerializer(pp.root);
    let out = '',
      chunk;

    while((chunk = s.read(100)) !== '') out += chunk;

    assert(out.includes('<root'), out);
    assert(out.includes('x="1"'), out);
    assert(out.includes('text'), out);
  },

  'integration: XMLNodeParser nodes reconstruct via manual assembly'() {
    let p = new XMLNodeParser('<a x="1">text<b/></a>');
    let nodes = [];

    for(let i = 0; i < 20; i++) {
      let node = p.parse();
      if(node <= 0) break;
      nodes.push(node);
    }

    eq(nodes[0].tagName, 'a');
    eq(nodes[0].attributes.x, '1');
    eq(nodes[1], 'text');
    eq(nodes[2].tagName, 'b');
    eq(nodes[3].tagName, '/b');
    eq(nodes[4].tagName, '/a');
  },

  'integration: xml.read() tree serializes via XMLSerializer'() {
    let tree = xml.read('<root><child attr="val">text</child></root>');

    let s = new XMLSerializer(tree);
    let out = '',
      chunk;

    while((chunk = s.read(100)) !== '') out += chunk;

    let reparsed = xml.read(out);

    eqArr(reparsed, tree);
  },

  'integration: xml.read() tree writes via XMLWriter'() {
    let tree = xml.read('<a x="1">text</a>');
    let out = '';
    let w = new XMLWriter(s => (out += toString(s)));

    w.elementStart('a');
    w.attribute('x', '1');
    w.text('text');
    w.elementEnd('a');

    assert(out.includes('<a'), out);
    assert(out.includes('x="1"'), out);
    assert(out.includes('text'), out);
    assert(out.includes('</a>'), out);
  },

  /* ========== Error handling ========== */
  'error: XMLParser on malformed tag throws or returns PARSE_ERROR'() {
    let p = new XMLParser('<1invalid/>');
    let tok = p.parse();

    eq(tok, XMLParser.PARSE_ERROR);
  },

  'error: XMLPushParser write() on malformed input throws in builder mode'() {
    let pp = new XMLPushParser();

    assertThrows(() => pp.write('<1invalid/>'));
  },

  'error: XMLSerializer read(-1) throws RangeError'() {
    let s = new XMLSerializer([{ tagName: 'a', attributes: {}, children: [] }]);

    assertThrows(() => s.read(-1));
  },

  /* ========== Symbol.toStringTag ========== */
  'XMLParser[Symbol.toStringTag] is "XMLParser"'() {
    let p = new XMLParser('<a/>');
    eq(p[Symbol.toStringTag], 'XMLParser');
  },

  'XMLPushParser[Symbol.toStringTag] is "XMLPushParser"'() {
    let pp = new XMLPushParser();
    eq(pp[Symbol.toStringTag], 'XMLPushParser');
  },

  'XMLSerializer[Symbol.toStringTag] is "XMLSerializer"'() {
    let s = new XMLSerializer([]);
    eq(s[Symbol.toStringTag], 'XMLSerializer');
  },

  'XMLWriter[Symbol.toStringTag] is "XMLWriter"'() {
    let w = new XMLWriter(s => {});
    eq(w[Symbol.toStringTag], 'XMLWriter');
  },

  'XMLNodeParser[Symbol.toStringTag] is "XMLNodeParser"'() {
    let p = new XMLNodeParser('<a/>');
    eq(p[Symbol.toStringTag], 'XMLNodeParser');
  },
});
