import { TypeSelector, ClassSelector, AttributeSelector, parseSelector } from '../lib/css3-selectors.js';
import { Predicate } from 'predicate';
import { Console } from 'console';
import { nodeTypes, Parser, Node, NodeList, NamedNodeMap, Element, Document, Attr, Text, TokenList, Factory } from '../lib/dom.js';
import { read as readXML, write as writeXML }  from 'xml';


function main(...args) {
  globalThis.console = new Console({
    stdout: process.stdout,
    stderr: process.stderr,
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

  console.log('attrSel =', attrSel.toSource());
  console.log('attrSel() =', attrSel({ attributes: { name: 'test' } }));

  let propSel = Predicate.property('name', Predicate.string('test'));
  console.log('propSel() =', propSel({ name: 'test' }));

  let selector = parseSelector('element.big[name="test"]');

  console.log('selector', selector);
}

try {
  main(...process.argv.slice(1));
} catch(error) {
  console.log(`FAIL: ${error.message}\n${error.stack}`);
} finally {
  console.log('SUCCESS');
}
