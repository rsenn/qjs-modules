import * as os from 'os';
import * as std from 'std';
import { Console } from 'console';
import { Blob } from 'blob';
import * as misc from 'misc';

('use strict');
('use math');

function main(...args) {
  globalThis.console = new Console({
    inspectOptions: {
      colors: true,
      depth: 8,
      maxStringLength: Infinity,
      maxArrayLength: 256,
      compact: 2,
      showHidden: false
    }
  });
  console.log('Blob', Blob);

  let childBlob = new Blob(['\nx\ny\nz\n']);

  let blob = new Blob(
    [
      '<html></html>',
      new Uint8Array(misc.toArrayBuffer('BLAH blah BLAH'), 5, 4),
      childBlob,
      new DataView(misc.toArrayBuffer('TEST test TEST'), 5, 4)
    ],
    { type: 'text/html', endings: 'transparent' }
  );
  console.log('blob', Object.getPrototypeOf(blob));
  console.log('blob', Object.getOwnPropertyNames(Object.getPrototypeOf(blob)));
  console.log('blob', blob);
  console.log('blob.size', blob.size);
  console.log('blob.type', blob.type);
  console.log('blob.text()', blob.text());
  console.log('blob.arrayBuffer()', blob.arrayBuffer());
  let sl = blob.slice(1, -1);
  console.log('blob.slice(1)', sl);
  console.log('sl.arrayBuffer()', sl.arrayBuffer());
  console.log('sl.text()', sl.text());
  for(let i = 0; i < 4; i++) {}
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error.message}\n${error.stack}`);
  std.exit(1);
} finally {
  console.log('SUCCESS');
}
