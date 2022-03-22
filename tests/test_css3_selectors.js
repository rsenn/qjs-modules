import { TypeSelector, ClassSelector } from '../lib/css3-selectors.js';
import { Console } from 'console';
import * as Predicate from 'predicate';

function main(...args) {
  globalThis.console = new Console(process.stderr, {
    inspectOptions: {
      colors: true
    }
  });

  let typeSel = new TypeSelector('html');
  //typeSel=Predicate.string('HTML');

  console.log('', typeSel({ tagName: 'HTML' }));

  let classSel = new ClassSelector('common');

  console.log('', classSel({ attributes: { class: 'common big item' } }));
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error.message}\n${error.stack}`);
} finally {
  console.log('SUCCESS');
}
