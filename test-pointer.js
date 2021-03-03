"use strict";
"use math";
import * as os from 'os';
import * as std from 'std';
import inspect from 'inspect.so';
import * as xml from 'xml.so';
import { Pointer } from 'pointer.so';
import Console from './console.js';

globalThis.inspect = inspect;

function WriteFile(file, data) {
  let f = std.open(file, 'w+');
  f.puts(data);
  console.log(`Wrote '${file}': ${data.length} bytes`);
}

function main(...args) {
  console = new Console({
    colors: true,
    depth: 1,
    maxArrayLength: 10,
    maxStringLength: 100,
    compact: false
  });
  console.log('args:', args);

  let data = std.loadFile(args[0] ?? 'FM-Radio-Receiver-1.5V.xml', 'utf-8');

  console.log('data:', data);

  let result = xml.read(data);
  console.log('result:', result);

  console.log('xml:', xml.write(result));

  let pointer;

  pointer = new Pointer(2, 'children', 0, 'children', 2);
  try {
    console.log('deref:', pointer.deref(result));
  } catch(e) {
    console.log('exception:', e);
  }
  console.log('keys:', [...pointer]);
  console.log('values:', [...pointer.values()]);
  console.log('pointer:', pointer.slice(0).inspect());

  WriteFile('output.json', JSON.stringify(result, null, 2));

  std.gc();
}

main(...scriptArgs.slice(1));
