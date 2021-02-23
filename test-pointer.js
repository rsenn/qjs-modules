import * as os from 'os';
import * as std from 'std';
import inspect from 'inspect.so';
import * as xml from 'xml.so';
import { Pointer } from 'pointer.so';
import { TreeWalker } from 'tree-walker.so';
import Console from './console.js';

function WriteFile(file, data) {
  let f = std.open(file, 'w+');
  f.puts(data);
  console.log(`Wrote '${file}': ${data.length} bytes`);
}

function main(...args) {
  new Console({});

  let data = std.loadFile(args[0] ?? std.getenv('HOME') + '/Sources/an-tronics/eagle/FM-Radio-Simple-Receiver-Dip1.sch',
    'utf-8'
  );

  console.log('data:', data.length);

  let result = xml.read(data);
  console.log('result:', result);

  console.log('xml:',
    inspect(result.slice(0, 2), { depth: Infinity, compact: Infinity, colors: true })
  );

  let pointer;
  let walker = new TreeWalker(result);
    walker.tagMask = TreeWalker.MASK_OBJECT;

  let node;
  while((node = walker.nextNode())) {

    if(typeof walker.currentNode != 'object')continue;
    console.log('node:', node);
    console.log('path:', walker.currentPath);

    pointer =      new Pointer(...(walker.currentPath ?? []));
    try {
      console.log('deref:', pointer.deref(result[2]));
     }catch(e) {
console.log("exception:",e.constructor);
break;
     }
      console.log('keys:', [...pointer]);
      console.log('values:', [...pointer.values()]);
      console.log('pointer:', pointer.slice(0).inspect());
}

  WriteFile('output.json', JSON.stringify(result, null, 2));

  std.gc();
}

main(scriptArgs.slice(1));
