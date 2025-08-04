import fs from 'fs';
import { toString } from 'util';
import { extendAsyncGenerator } from '../lib/extendAsyncGenerator.js';
import { ByLineStream, FileSystemReadableFileStream, FileSystemReadableStream, StreamReadIterator } from '../lib/streams.js';
import { Console } from 'console';
import { exit } from 'std';

extendAsyncGenerator();

async function main(...args) {
  globalThis.console = new Console({
    inspectOptions: {
      maxStringLength: 50,
      maxArrayLength: 10,
      compact: false,
    },
  });
  let fd = fs.openSync('quickjs-misc.c', fs.O_RDONLY);

  let st = new FileSystemReadableStream(fd, 1024);

  let read = new FileSystemReadableFileStream('tests/test1.xml');

  let iter = await StreamReadIterator(st);
  let { readable, writable } = new ByLineStream();

  let rd = readable.getReader();
  let br = rd.read();

  console.log('rd.read()', br);

  let wr = writable.getWriter();

  console.log('transform', { readable, writable, wr });
  let bw = await wr.write('blah\nhaha\n');

  console.log('wr.write()', bw);
  //await wr.close();

  for(let i = 0; i < 1; i++) {
    br = await br; //(await br??br.read());
    console.log(`rd.read(${i})`, br);
    br = rd.read();
  }

  /*  let tfrm=  LineStreamIterator(iter);*/
  let result = await iter.reduce(
    (buf => (acc, n) => {
      buf += toString(n);
      if(buf.indexOf('\n') != -1) {
        let lines = buf.split('\n');
        buf = lines.pop();
        (acc ??= []).push(...lines);
      }
      return acc;
    })(''),
  );

  /*let linestream=await LineStreamIterator(iter);
console.log('linestream',  linestream );
let result = await  linestream.reduce((acc, n) => (acc.push(n), acc), [])*/

  console.log('result', result);
  /*
  let write = new FileSystemWritableFileStream('/tmp/out.txt');
  */
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error.message}\n${error.stack}`);
  exit(1);
}
