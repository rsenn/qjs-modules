/* test-xml-write-golden.js: the byte-exact output of write(), XMLTreeSerializer
 * and XMLWriter, from tests/fixtures/xml-write-golden.json.
 * rule: a case fails only when the output changed; accept a change on
 * purpose with `qjsm tests/tools/xml-write-golden.js`. */

import xml, { XMLWriter, XMLTreeSerializer } from 'xml';
import { toString } from 'util';
import * as fs from 'fs';
import { assert, eq, tests } from '../../lib/tinytest.js';

const { cases } = JSON.parse(fs.readFileSync('tests/fixtures/xml-write-golden.json', 'utf8'));

function run(kind, input, opts) {
  if(kind == 'write') {
    const tree = input.src !== undefined ? xml.read(input.src, 'x.xml', input.flat ? { flat: true } : {}) : input.tree;
    return input.depth !== undefined ? xml.write(tree, input.depth) : xml.write(tree);
  }

  if(kind == 'serializer') {
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

tests(Object.fromEntries(cases.map(c => [`golden: ${c.name}`, () => {
  let got, threw;

  try {
    got = run(c.kind, c.input, c.opts);
  } catch(e) {
    threw = String(e.message || e);
  }

  eq(threw, c.throws);
  eq(got, c.expect);
}])));
