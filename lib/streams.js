import { ReadableStream, WritableStream, TransformStream } from 'stream';
import { TextDecoder, TextEncoder } from 'textcode';
import { quote, toString } from 'util';
import * as std from 'std';
import * as fs from 'fs';

export function FileSystemReadableStream(file, bufSize = 1024 * 64) {
  let error = {},
    buf = new ArrayBuffer(bufSize),
    fd = fs.fileno(file);

  return new ReadableStream({
    start(controller) {
      let flags = fs.fcntlSync(fd, fs.F_GETFL);
      flags |= fs.O_NONBLOCK;
      if(fs.fcntlSync(fd, fs.F_SETFL, flags)) error.errno = fs.errno;

      //console.log('FileSystemReadableStream.start', { controller });

      if(error.errno) throw new Error(`Error making fd ${fd} non-blocking`);
    },
    async pull(controller) {
      let ret;
      /* do {*/
      try {
        ret = await fs.read(file, buf, 0, bufSize);
      } catch(e) {
        error.exception = e;
        //          break;
      }

      //console.log('FileSystemReadableStream.pull', { controller, ret });
      if(ret > 0) {
        //console.log('FileSystemReadableStream.pull', { ret, buf, ret });
        controller.enqueue(ret == bufSize ? buf : buf.slice(0, ret));
        /* } while(ret != 0);*/
      } else if(error.exception) {
        // console.log('ReadableStream.pull', { error });
        controller.error(file);
      } else {
        controller.close();
      }
    },
    async cancel(reason) {
      //console.log('ReadableStream.cancel', { reason });
      fs.closeSync(file);
      return true;
    }
  });
}

export async function* StreamReadIterator(strm) {
  let reader = await strm.getReader();

  do {
    let { done, value } = await reader.read();
    if(done) break;
    //console.log('StreamReadIterator', { done, value });
    yield value;
  } while(true);
  //  for await(let chunk of await reader.read()) yield chunk;

  await reader.releaseLock();
}

export async function* LineStreamIterator(strm) {
  let buffer = '';
  if(strm instanceof ReadableStream) strm = StreamReadIterator(strm);

  for await(let chunk of await strm) {
    buffer += toString(chunk);
    if(chunk.indexOf('\n') < 0) continue;

    const lines = buffer.split('\n');
    const L = lines.length;
    for(let i = 0; i < L - 1; ++i) {
      const line = lines[i];
      yield line && line[line.length - 1] === '\r' ? line.substring(0, line.length - 1) : line;
    }
    buffer = lines[L - 1];
  }

  yield buffer;
}

export function FileSystemReadableFileStream(path, bufSize = 1024 * 64) {
  let file,
    error = {};

  return new ReadableStream({
    start(controller) {
      //console.log('ReadableStream.start', { controller });
      file = std.open(path, 'rb', error);
      if(error.errno) throw new Error("Error opening '" + path + "': " + std.strerror(error.errno));
    },
    pull(controller) {
      if(file.eof()) {
        //console.log('ReadableStream.pull', { eof: true });
        controller.close();
        return;
      }

      let buf = new ArrayBuffer(bufSize);
      let ret = file.read(buf, 0, bufSize);

      //console.log('ReadableStream.pull', { controller, ret });

      if(ret > 0) controller.enqueue(ret == bufSize ? buf : buf.slice(0, ret));

      if(file.error()) {
        //console.log('ReadableStream.pull', { error: true });
        controller.error(file);
      }
    },
    cancel(reason) {
      file.close();
      //console.log('ReadableStream.cancel', { reason });
    }
  });
}

export function FileSystemWritableFileStream(path) {
  let file,
    error = {};

  return new WritableStream({
    start(controller) {
      file = std.open(path, 'w+', error);
      //console.log('WritableStream.start', { file, error, controller });

      if(error.errno) throw new Error("Error opening '" + path + "': " + std.strerror(error.errno));
    },
    write(chunk, controller) {
      let ret = file.write(chunk, 0, chunk.byteLength);
      //console.log('WritableStream.write', { chunk, controller });

      if(file.error()) {
        //console.log('WritableStream.write', { error: true });
        controller.error(file);
      }
    },
    close(controller) {
      //console.log('WritableStream.close', { controller });

      file.close();
    },
    abort(reason) {
      //console.log('WritableStream.abort', { reason });

      file.close();
    }
  });
}

export class TextEncoderStream extends TransformStream {
  constructor(encoding) {
    let enc;
    super({
      start(ctl) {
        //console.log('TextEncoderStream.start', { ctl });
        enc = new TextEncoder(encoding);
      },
      transform(chunk, ctl) {
        //console.log('TextEncoderStream.transform', quote(chunk, "'"), ctl);
        let buf = enc.encode(chunk);
        //console.log('TextEncoderStream.transform', { buf, ctl });
        if(!buf) return ctl.error();

        return ctl.enqueue(buf.buffer);
      },
      flush() {
        //console.log('TextEncoderStream.flush', { enc });
        enc = null;
      }
    });
  }
}
