import * as os from 'os';
import * as std from 'std';
import inspect from 'inspect.so';
import * as deep from 'deep.so';
import * as xml from 'xml.so';
import Console from './console.js';

('use strict');
('use math');

globalThis.inspect = inspect;

function WriteFile(file, data) {
  let f = std.open(file, 'w+');
  f.puts(data);
  console.log(`Wrote '${file}': ${data.length} bytes`);
}

  const inspectOptions = {
    colors: true,
    showHidden: false,
    customInspect: true,
    showProxy: false,
    getters: false,
    depth: 3,
    maxArrayLength: 10,
    maxStringLength: 200,
    compact: 2,
    hideKeys: ['loc', 'range', 'inspect', Symbol.for('nodejs.util.inspect.custom')]
  };
function main(...args) {
    console = new Console(inspectOptions);

  console.log('args:', args);

  let data = std.loadFile(args[0] ?? 'FM-Radio-Receiver-1.5V.xml', 'utf-8');

  console.log('data:', data);

  let result = xml.read(data);
  console.log('result:', inspect(result, inspectOptions));

  let found = deep.find(result, n => typeof n == 'object' && n != null && n.tagName == 'elements');

  //deep.find(result, n => console.log(n));

  console.log('found:', inspect(found, inspectOptions));

  std.gc();
}

main(...scriptArgs.slice(1));
