import * as os from 'os';
import Console from '../lib/console.js';
import { MAP_PRIVATE, mmap, munmap, PROT_READ } from 'mmap';
import * as std from 'std';
async function main(...args) {
  globalThis.console = new Console({
    inspectOptions: {
      depth: 5,
      maxArrayLength: 10,
      compact: 1,
      maxStringLength: 120
    }
  });
  let filename = args[0] ?? scriptArgs[0];
  console.log('filename', filename);
  let file = std.open(filename, 'r+');
  let fd = file.fileno();

  let { size } = os.stat(filename)[0];
  console.log('fd', fd);
  console.log('size', size);
  console.log('typeof 1n', typeof 1n);

  let map = mmap(0, size, PROT_READ, MAP_PRIVATE, fd, 0);
  console.log('map', map);

  console.log('map =', ArrayBufToString(map));

  munmap(map);
}

function ArrayBufToString(buf, offset, length) {
  let arr = new Uint8Array(buf, offset, length);
  return arr.reduce((s, code) => s + String.fromCodePoint(code), '');
}

main(...scriptArgs.slice(1))
  .then(() => console.log('SUCCESS'))
  .catch(error => {
    console.log(`FAIL: ${error.message}\n${error.stack}`);
    std.exit(1);
  });
