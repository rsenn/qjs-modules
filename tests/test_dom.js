import * as os from 'os';
import * as std from 'std';
import { escape, quote, isObject } from 'util';
import inspect from 'inspect';
import * as xml from 'xml';
import * as path from 'path';
import * as deep from 'deep';
import Console from '../lib/console.js';
import { Document, Element, Node } from '../lib/dom.js';

function main(...args) {
  globalThis.console = new Console(process.stdout, {
    inspectOptions: {
      colors: true,
      depth: Infinity,
      //stringBreakNewline: false,
      maxArrayLength: Infinity,
      compact: false,
      maxStringLength: 60,
      customInspect: true
    }
  });

  let file = args[0] ?? '/etc/fonts/fonts.conf';

  let base = path.basename(file, path.extname(file));

  let data = std.loadFile(file, 'utf-8');

  let start = Date.now();

  let result = xml.read(data, file, false);

  let doc = new Document(result[0]);

  console.log('doc', inspect(doc, { depth: 20, compact: false }));

  for(let value of deep.iterate(doc, deep.RETURN_VALUE)) {
    console.log('value', value);
  }

  std.gc();
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error.message}\n${error.stack}`);
  std.exit(1);
}
