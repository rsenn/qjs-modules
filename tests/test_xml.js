import * as os from 'os';
import * as std from 'std';
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

async function main(...args) {
  console = new Console({
    colors: true,
    depth: 5,
    _stringBreakNewline: false,
    maxArrayLength: 10,
    compact: 1,
    maxStringLength: 60
  });

  let winsz;

  try {
    winsz = await os.ttyGetWinSize(1);
  } catch(err) {}
  console.log('winsz:', winsz);

  let file = args[0] ?? '/etc/fonts/fonts.conf';
  console.log('file:', file);

  let base = path.basename(file, path.extname(file));
  console.log('base:', base);

  let data = std.loadFile(file, 'utf-8');
  console.log('data:', data.substring(0, 100));

  let result = xml.read(data);

  console.log('result:', result);

  let str = xml.write(result);
  console.log('write:', str);

  WriteFile(base + '.json', JSON.stringify(result, null, 2));

  WriteFile(base + '.xml', str);

  await import('std').then(std => std.gc());
}

main(...scriptArgs.slice(1))
  .then(() => console.log('SUCCESS'))
  .catch(error => {
    console.log(`FAIL: ${error.message}\n${error.stack}`);
    std.exit(1);
  });
