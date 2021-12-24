import * as os from 'os';
import * as std from 'std';
import { escape, quote } from 'misc';
import inspect from 'inspect';
import * as xml from 'xml';
import * as path from 'path';
import Console from '../lib/console.js';

('use strict');
('use math');

function WriteFile(file, data) {
  let f = std.open(file, 'w+');
  f.puts(data);
  console.log('Wrote "' + file + '": ' + data.length + ' bytes');
}

function main(...args) {
  globalThis.console = new Console(process.stdout, {
    inspectOptions: {
      colors: true,
      depth: 10,
      stringBreakNewline: false,
      maxArrayLength: 10,
      compact: 2,
      maxStringLength: 60
    }
  });

  let file = args[0] ?? '/etc/fonts/fonts.conf';

  let base = path.basename(file, path.extname(file));

  let data = std.loadFile(file, 'utf-8');

  let start = Date.now();

  let result = xml.read(data, file, true);
  let end = Date.now();

  console.log(`Parsing took ${end - start}ms`);

  // console.log('result:', inspect(result, { depth: Infinity, compact: 1, maxArrayLength: Infinity }));
  WriteFile(base + '.json', JSON.stringify(result, null, 2));

  start = Date.now();
  let str = xml.write(result);
  end = Date.now();

  console.log(`Generating took ${end - start}ms`);

  WriteFile(base + '.xml', str);

  std.gc();
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error.message}\n${error.stack}`);
  std.exit(1);
}
