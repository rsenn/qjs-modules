import * as os from 'os';
import * as std from 'std';
import inspect from 'inspect.so';
import * as xml from 'xml.so';
import { TreeWalker } from 'tree-walker.so';

function main(...args) {
  let data = std.loadFile(args[0] ?? 'FM-Radio-Simple-Receiver-Dip1.sch', 'utf-8');

  //console.log('data:', Util.abbreviate(Util.escape(data)));
  console.log('data.length:', data.length);

  let result = xml.read(data);
  console.log('result:', result);

  console.log('xml:',
    inspect(result.slice(0, 2), { depth: Infinity, compact: Infinity, colors: true })
  );

  /*let walk = new TreeWalker(result[0]);
  console.log('walk:', walk.toString());
  let i = 0;
  console.log('~TreeWalker.MASK_PRIMITIVE:', TreeWalker.MASK_PRIMITIVE.toString(2));
  console.log(' TreeWalker.MASK_ALL:', TreeWalker.MASK_ALL);
  console.log(' TreeWalker.MASK_ALL:', TreeWalker.MASK_ALL.toString(2));
  walk.tagMask = TreeWalker.MASK_PRIMITIVE;
  walk.tagMask = TreeWalker.MASK_OBJECT;
  walk.flags = 0;
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
  }*/

  //  delete result[2];
  console.log('output:', xml.write(result));

  //  console.log('result[2]  :', inspect(result[2], { depth: 10, compact: Infinity, colors: true }));

  std.gc();
}

main(scriptArgs.slice(1));
