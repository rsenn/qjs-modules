import * as os from 'os';
import * as std from 'std';
import inspect from 'inspect';
import * as xml from 'xml';
import { Pointer } from 'pointer';
import Console from '../lib/console.js';

('use strict');
('use math');

globalThis.inspect = inspect;

function WriteFile(file, data) {
  let f = std.open(file, 'w+');
  f.puts(data);
  console.log('Wrote "' + file + '": ' + data.length + ' bytes');
}

function main(...args) {
  globalThis.console = new Console({
    inspectOptions: {
      colors: true,
      depth: 1,
      maxArrayLength: 10,
      maxStringLength: 100,
      compact: 1
    }
  });

  console.log('args:', args);

  let data = std.loadFile(args[0] ?? '/etc/fonts/fonts.conf', 'utf-8');

  console.log('data:', data.substring(0, 100).replace(/\n/g, '\\n') + '...');

  let result = xml.read(data);
  console.log('result:', result);

  //console.log('xml:', xml.write(result));

  let pointer;
  pointer = new Pointer('[1].children[2].children[3]');
  console.log('[...pointer]:', [...pointer]);
  console.log('pointer:', pointer);
  console.log('pointer.toString()):', pointer.toString());
  console.log('pointer.toArray()):', pointer.toArray());
  console.log('pointer.atoms', pointer.atoms);
 
  for(let i = 0; i < pointer.length; i++)
  console.log(`pointer[${i}]`, pointer[i]);

  /*pointer = new Pointer([3, 'children', 0, 'children', 0]);
  try {
    console.log('deref pointer:', pointer.deref(result));
  } catch(e) {
    console.log('exception:', e);
  }
  console.log('keys:', [...pointer]);
  console.log('values:', [...pointer.values()]);
  console.log('pointer:', pointer.slice(0).inspect());

  WriteFile('output.json', JSON.stringify(result, null, 2));

  let ptr2 = new Pointer('3.children.0.children.0');

  console.log('deref ptr2:', ptr2.deref(result));
  console.log('dump ptr2:', ptr2);*/

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
