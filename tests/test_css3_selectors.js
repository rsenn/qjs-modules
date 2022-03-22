import { TypeSelector, ClassSelector, AttributeSelector, parseSelector } from '../lib/css3-selectors.js';
import { Console } from 'console';
import * as Predicate from 'predicate';

function main(...args) {
  globalThis.console = new Console(process.stderr, {
    inspectOptions: {
      colors: true,
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

  console.log('attrSel() =', attrSel({ attributes: { name: 'test' } }));

  let selector = parseSelector('element.big[name="test"]');

  console.log('selector', selector);
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error.message}\n${error.stack}`);
} finally {
  console.log('SUCCESS');
}
