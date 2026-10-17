/* xml-write-golden.js: regenerates tests/fixtures/xml-write-golden.json.
 * runs on qjsm: `qjsm tests/tools/xml-write-golden.js` from the repo root.
 * rule: `expect` is whatever the current build writes; regenerate only to
 * accept a change on purpose, and review the diff of the json. */

import xml, { XMLWriter, XMLTreeSerializer } from 'xml';
import { toString } from 'util';
import * as fs from 'fs';

const SRC = {
  simple: '<a>x</a>',
  empty: '<a/>',
  nested: '<root><a x="1"><b>t</b><c/></a><d>u</d></root>',
  attrs: '<a href="/x?a=1&amp;b=2" title="q&quot;&lt;&gt;" data-x=\'s\'>t</a>',
  boolattr: '<input disabled checked value="v"/>',
  text: '<p>hello &amp; &lt;world&gt;</p>',
  multiline: '<pre>line1\nline2\n  line3</pre>',
  mixed: '<p>one <b>two</b> three</p>',
  comment: '<a><!-- c --><b/></a>',
  doctype: '<!DOCTYPE html><html><body><p>x</p></body></html>',
  pi: '<?xml version="1.0" encoding="UTF-8"?><a/>',
  deep: '<a><b><c><d><e>x</e></d></c></b></a>',
  siblings: '<ul><li>1</li><li>2</li><li>3</li></ul>',
  void: '<p>a<br>b<img src="i.png">c</p>',
};

const TREES = {
  attrvalues: [{ tagName: 'a', attributes: { n: 1, t: true, f: false, z: null, s: 'x' }, children: ['v'] }],
  emptychildren: [{ tagName: 'a', attributes: {}, children: [] }],
  nochildren: [{ tagName: 'a', attributes: {} }],
  noattributes: [{ tagName: 'a', children: ['t'] }],
  marker: [{ tagName: 'a' }, 't', { tagName: '/a' }],
  twoRoots: [{ tagName: 'a', children: ['1'] }, { tagName: 'b', children: ['2'] }],
  unicode: [{ tagName: 'a', attributes: { k: 'ü€😀' }, children: ['ü€😀 & <'] }],
};

const OPS = {
  simple: [['start', 'a'], ['text', 'x'], ['end', 'a']],
  selfclose: [['start', 'br'], ['end', 'br']],
  attrs: [['start', 'a'], ['attr', 'x', '1'], ['attr', 'y', 'two'], ['text', 't'], ['end', 'a']],
  attrselfclose: [['start', 'a'], ['attr', 'x', '1'], ['end', 'a']],
  boolattr: [['start', 'a'], ['attr', 'b', true], ['attr', 'c'], ['end', 'a']],
  nested: [['start', 'a'], ['start', 'b'], ['text', 't'], ['end', 'b'], ['start', 'c'], ['end', 'c'], ['end', 'a']],
  mixed: [['start', 'p'], ['text', 'one '], ['start', 'b'], ['text', 'two'], ['end', 'b'], ['text', ' three'], ['end', 'p']],
  special: [['start', 'a'], ['attr', 'q', 'a"b<c>&d'], ['text', '1 < 2 & 3 > "4"'], ['end', 'a']],
  textonly: [['text', 'plain']],
  twoRoots: [['start', 'a'], ['end', 'a'], ['start', 'b'], ['end', 'b']],
};

function run(kind, input, opts) {
  if(kind == 'write') {
    const tree = input.src !== undefined ? xml.read(input.src, 'x.xml', input.flat ? { flat: true } : {}) : input.tree;
    return input.depth !== undefined ? xml.write(tree, input.depth) : xml.write(tree);
  }

  if(kind == 'serializer') {
    // read(n) cuts at n bytes, so non-ASCII output differs by size (BUGS)
    const ascii = !/[^\x00-\x7f]/.test(JSON.stringify(input.tree));
    const outs = (ascii ? [1, 7, 1000] : [1000]).map(n => {
      const s = new XMLTreeSerializer(input.tree);
      let out = '', chunk;

      while((chunk = s.read(n)) !== '') out += chunk;

      return out;
    });

    if(outs.some(o => o != outs[0])) throw new Error('serializer differs by read size');
    return outs[0];
  }

  let out = '';
  const w = new XMLWriter(s => (out += toString(s)), opts.indent);

  for(const [op, a, b] of input.ops) {
    if(op == 'start') w.elementStart(a);
    else if(op == 'end') w.elementEnd(a);
    else if(op == 'text') w.text(a);
    else if(b === undefined) w.attribute(a);
    else w.attribute(a, b);
  }

  return out;
}

const cases = [];
const add = (name, kind, input, opts = {}) => cases.push({ name, kind, input, opts });

for(const [n, src] of Object.entries(SRC)) {
  add(`write tree ${n}`, 'write', { src });
  add(`write flat ${n}`, 'write', { src, flat: true });
  add(`serializer ${n}`, 'serializer', { tree: xml.read(src, 'x.xml', {}) });
}

add('write tree maxDepth 1', 'write', { src: SRC.deep, depth: 1 });
add('write tree maxDepth 2', 'write', { src: SRC.deep, depth: 2 });

for(const [n, tree] of Object.entries(TREES)) {
  add(`write ${n}`, 'write', { tree });
  add(`serializer ${n}`, 'serializer', { tree });
}

for(const [n, ops] of Object.entries(OPS))
  for(const indent of [-1, 0, 2, 4]) add(`writer ${n} indent ${indent}`, 'writer', { ops }, { indent });

for(const c of cases) {
  try {
    c.expect = run(c.kind, c.input, c.opts);
  } catch(e) {
    c.throws = String(e.message || e);
  }
}

fs.writeFileSync('tests/fixtures/xml-write-golden.json', JSON.stringify({ cases }, null, 1) + '\n');
console.log(cases.length, 'cases');
