import { Blob } from 'blob';
import { Console } from 'console';
import { escape } from 'misc';
import { toArrayBuffer } from 'misc';
import * as std from 'std';
('use strict');
('use math');

function main(...args) {
  globalThis.console = new Console({
    inspectOptions: {
      depth: 8,
      maxStringLength: Infinity,
      maxArrayLength: 256,
      compact: 2,
      showHidden: false,
      customInspect: false
    }
  });
  console.log('Blob', Blob);

  //  let childBlob = new Blob(['\nx\ny\nz\n']);
  let blob = new Blob(
    [
      '<html></html>',
      new Uint8Array([0xa]),
      new Uint8Array(toArrayBuffer('BLAH blah BLAH'), 5, 4),
      //childBlob,
      new DataView(toArrayBuffer('TEST test TEST'), 5, 4).buffer
    ],
    { type: 'text/html', endings: 'transparent' }
  );

  console.log('blob', blob);

  console.log('blob', Object.getPrototypeOf(blob));
  console.log('blob', Object.getOwnPropertyNames(Object.getPrototypeOf(blob)));
  console.log('blob', blob);
  console.log('blob.size', blob.size);
  console.log('blob.type', blob.type);
  console.log('blob.text()', blob.text());
  console.log('blob.arrayBuffer()', blob.arrayBuffer());
  let sl = blob.slice(1, 14, 'text/plain');
  console.log('blob.slice(1)', sl);
  console.log('sl.arrayBuffer()', sl.arrayBuffer());
  console.log('sl.text()', escape(sl.text()));
  let blobs = [];
  for(let i = 0; i < 4; i++) {
    sl = blobs[i] = new Blob([sl, toArrayBuffer(`line #${i}\n`)]);

    console.log(`sl[${i}]`, escape(sl.text()));
  }
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error.message}\n${error.stack}`);
  std.exit(1);
} finally {
  console.log('SUCCESS');
}
  
