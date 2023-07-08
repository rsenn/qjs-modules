import { readFileSync } from 'fs';
import { AttributeSelector } from '../lib/css3-selectors.js';
import { ClassSelector } from '../lib/css3-selectors.js';
import { parseSelectors } from '../lib/css3-selectors.js';
import { TypeSelector } from '../lib/css3-selectors.js';
import { Attr } from '../lib/dom.js';
import { Parser } from '../lib/dom.js';
import { Console } from 'console';
import { Predicate } from 'predicate';
import { REPL } from 'repl';
function main(...args) {
  globalThis.console = new Console({
    stdout: process.stdout,
    stderr: process.stderr,
    inspectOptions: {
      depth: Infinity,
      compact: false
    }
  });

  let typeSel = new TypeSelector('html');
  //typeSel=Predicate.string('HTML');

  console.log('typeSel() =', typeSel({ tagName: 'HTML' }));

  let classSel = new ClassSelector('common');

  console.log('classSel() =', classSel({ attributes: { class: 'common big item' } }));

  let attrSel = new AttributeSelector('name', 'test');

  console.log('attrSel =', attrSel.toSource());
  console.log('attrSel() =', attrSel({ attributes: { name: 'test' } }));

  let propSel = Predicate.property('name', Predicate.string('test'));
  console.log('propSel() =', propSel({ name: 'test' }));

  let xmlDoc = new Parser().parseFromString(readFileSync('tests/test1.xml', 'utf-8'));
  console.log('xmlDoc', xmlDoc);
  console.log('xmlDoc2');
  console.log('xmlDoc', xmlDoc.querySelector('.icon span'));

  for(let selector of ['element.big[name="test"]', '[name="C1"]']) {
    console.log('selector', selector);
    let pred = [...parseSelectors(selector)];

    console.log('pred', pred);
    console.log('pred', pred + '');
  }

  //new REPL().run();
}

try {
  main(...process.argv.slice(1));
} catch(error) {
  console.log(`FAIL: ${error.message}\n${error.stack}`);
} finally {
  console.log('SUCCESS');
}