import * as os from 'os';
import * as std from 'std';
import inspect from 'inspect.so';
import * as xml from 'xml.so';
import { TreeWalker } from 'tree-walker.so';
import Console from './console.js';

function WriteFile(file, data) {
  let f = std.open(file, 'w+');
  f.puts(data);
  console.log(`Wrote '${file}': ${data.length} bytes`);
}

function main(...args) {
  console = new Console({ colors: true, depth: 1 });

  console.log('args:', args);
  let data = std.loadFile(args[0] ?? 'Simple-NPN-Regen-Receiver.xml', 'utf-8');

  //console.log('data:', data);

  let result = xml.read(data);
  console.log('result:', result);

  //console.log('xml:\n' + xml.write(result));

  let walk = new TreeWalker(result);
  console.log('walk:', walk.toString());
  let i = 0;
  console.log('~TreeWalker.MASK_PRIMITIVE:', TreeWalker.MASK_PRIMITIVE.toString(2));
  console.log(' TreeWalker.MASK_ALL:', TreeWalker.MASK_ALL);
  console.log(' TreeWalker.MASK_ALL:', TreeWalker.MASK_ALL.toString(2));
  //walk.tagMask = TreeWalker.MASK_PRIMITIVE;
  walk.tagMask = TreeWalker.MASK_ALL;
  //walk.flags = 0;
  const { flags, tagMask } = walk;
  console.log(' walk', { flags, tagMask });
  while(walk.nextNode((v, k, w) => typeof v != 'object')) {
    console.log('type:',
      typeof walk.currentNode,
      'path:',
      walk.currentPath.join('.'),
      typeof walk.currentNode != 'object' ? walk.currentNode : ''
    );
    let node = walk.currentNode;

    if(typeof node == 'object') {
      console.log('object:',
        inspect(node, { depth: 0, colors: true }) ||
          Object.getOwnPropertyNames(node)
            .filter(n => typeof node[n] != 'object')
            .reduce((acc, name) => ({ ...acc, [name]: node[name] }), {})
      );
    }
    i++;
  }

  //  delete result[2];
  WriteFile('output.json', JSON.stringify(result, null, 2));

  //  console.log('result[2]  :', inspect(result[2], { depth: 10, compact: Infinity, colors: true }));

  std.gc();
}

main(...scriptArgs.slice(2));
