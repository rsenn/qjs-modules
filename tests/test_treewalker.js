import Console from '../lib/console.js';
import inspect from 'inspect';
import * as std from 'std';
import { TreeIterator, TreeWalker } from 'tree_walker';
import * as xml from 'xml';

function WriteFile(file, data) {
  let f = std.open(file, 'w+');
  f.puts(data);
  console.log('Wrote "' + file + '": ' + data.length + ' bytes');
}

function main(...args) {
  globalThis.console = new Console({
    inspectOptions: {
      depth: 2,
      maxArrayLength: 4,
      maxStringLength: 60,
      compact: 1,
    },
  });
  console.log('args:', args);

  if(args.length == 0) args = ['tests/test3.xml'];

  let data = std.loadFile(args[0], 'utf-8');
  console.log('data:', data.substring(0, 100).replace(/\n/g, '\\n') + '...');

  let result;
  try {
    result = JSON.parse(data);
  } catch(err) {
    try {
      result = xml.read(data);
    } catch(err) {}
  }
  //console.log('result:', result);
  TestIterator();

  function TestWalker() {
    let walk = new TreeWalker(result);
    console.log('walk:', walk.toString());
    let i = 0;
    console.log('~TreeWalker.MASK_PRIMITIVE:', TreeWalker.MASK_PRIMITIVE.toString(2));
    console.log(' TreeWalker.MASK_ALL:', TreeWalker.MASK_ALL);
    console.log(' TreeWalker.MASK_ALL:', TreeWalker.MASK_ALL.toString(2));
    walk.tagMask = TreeWalker.MASK_ALL;

    const { flags, tagMask } = walk;
    console.log(' walk', { flags, tagMask });
    while(walk.nextNode((v, k, w) => typeof v != 'object')) {
      console.log('type:', typeof walk.currentNode, 'path:', walk.currentPath.join('.'), typeof walk.currentNode != 'object' ? walk.currentNode : '');
      let node = walk.currentNode;
      if(typeof node == 'object') {
        console.log(
          'object:',
          inspect(node, { depth: 0 }) ||
            Object.getOwnPropertyNames(node)
              .filter(n => typeof node[n] != 'object')
              .reduce((acc, name) => ({ ...acc, [name]: node[name] }), {}),
        );
      }
      i++;
    }
    WriteFile('output.json', JSON.stringify(result, null, 2));
  }
  function TestIterator() {
    for(let c of ['TYPE_OBJECT', 'RETURN_VALUE_PATH']) {
      console.log(`${c} = `, TreeIterator[c]);
    }
    let it = new TreeIterator(result, TreeIterator.TYPE_OBJECT | TreeIterator.RETURN_VALUE_PATH);
    for(let [entry, pointer] of it) {
      console.log(`pointer: ${pointer}, entry:`, entry);
    }
  }
  console.log('result', result);
  std.gc();
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error.message}\n${error.stack}`);
  std.exit(1);
} finally {
  console.log('SUCCESS');
}
