import * as fs from 'fs';
import { ReadableStream } from 'stream';
import { TransformStream } from 'stream';
import { WritableStream } from 'stream';
import { define } from 'util';
import { error } from 'util';
import { F_GETFL } from 'util';
import { F_SETFL } from 'util';
import { fcntl } from 'util';
import { O_NONBLOCK } from 'util';
import { quote } from 'util';
import { toString } from 'util';
import * as std from 'std';
import { TextDecoder } from 'textcode';
import { TextEncoder } from 'textcode';

export function FileSystemReadableStream(file, bufSize = 1024 * 64) {
  let err = {},
    buf = new ArrayBuffer(bufSize),
    fd = fs.fileno(file);

  return new ReadableStream({
    start(controller) {
      let flags = fcntl(fd, F_GETFL);
      flags |= O_NONBLOCK;
      if(fcntl(fd, F_SETFL, flags)) err = error();

      //console.log('FileSystemReadableStream.start', { controller });

      if(err.errno) throw new Error(`Error making fd ${fd} non-blocking`);
    },
    async pull(controller) {
      let ret;
      /* do {*/
      try {
        ret = await fs.read(file, buf, 0, bufSize);
      } catch(e) {
        err.exception = e;
        //          break;
      }

      //console.log('FileSystemReadableStream.pull', { controller, ret });
      if(ret > 0) {
        //console.log('FileSystemReadableStream.pull', { ret, buf, ret });
        controller.enqueue(ret == bufSize ? buf : buf.slice(0, ret));
        /* } while(ret != 0);*/
      } else if(err.exception) {
        // console.log('ReadableStream.pull', { err });
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

export const ByLineStream = (() => {
  class ByLineTransform {
    constructor() {
      this._buffer = [];
      this._lastChunkEndedWithCR = false;
    }

    transform(chunk, controller) {
      // see: http://www.unicode.org/reports/tr18/#Line_Boundaries
      const lines = chunk.split(/\r\n|[\n\v\f\r\x85\u2028\u2029]/g);
      const buffer = (controller._buffer ??= []);
      console.log('ByLineTransform', { lines, buffer });
      // don't split CRLF which spans chunks
      if(controller._lastChunkEndedWithCR && chunk[0] == '\n') {
        lines.shift();
      }

      if(buffer.length > 0) {
        buffer[buffer.length - 1] += lines[0];
        lines.shift();
      }

      controller._lastChunkEndedWithCR = chunk[chunk.length - 1] == '\r';
      buffer.push(...lines);
      let written = 0;
      console.log('controller.enqueue', controller.enqueue);
      // always buffer the last (possibly partial) line
      while(buffer.length > 1) {
        const line = buffer.shift();
        // skip empty lines
        if(line.length) {
          console.log('controller.enqueue()', controller.enqueue(line));
          written += line.length;
        }
      }
      return written;
    }

    flush(controller) {
      const buffer = controller._buffer;

      while(buffer.length) {
        const line = buffer.shift();
        // skip empty lines
        if(line.length) controller.enqueue(line);
      }
    }
  }

  class ByLineStream extends TransformStream {
    constructor() {
      super(new ByLineTransform());
    }
  }

  define(ByLineStream.prototype, { [Symbol.toStringTag]: 'ByLineStream' });

  return ByLineStream;
})();

export function FileSystemReadableFileStream(path, bufSize = 1024 * 64) {
  let file,
    err = {};

  return new ReadableStream({
    start(controller) {
      //console.log('ReadableStream.start', { controller });
      file = std.open(path, 'rb', err);
      if(err.errno) throw new Error("Error opening '" + path + "': " + std.strerror(err.errno));
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
    err = {};

  return new WritableStream({
    start(controller) {
      file = std.open(path, 'w+', err);
      //console.log('WritableStream.start', { file, error, controller });

      if(err.errno) throw new Error("Error opening '" + path + "': " + std.strerror(err.errno));
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
      },
      transform(chunk, ctl) {
        console.log('TextEncoderStream.transform', quote(chunk, "'"), ctl);
        enc ??= new TextEncoder(encoding ?? 'utf-8');
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

export class TextDecoderStream extends TransformStream {
  constructor(encoding) {
    let dec;
    super({
      start(ctl) {
        dec = new TextDecoder(encoding ?? 'utf-8');
      },
      transform(chunk, ctl) {
        let str;

        if(!(str = dec.decode(chunk))) return ctl.error();

        console.log('TextDecoderStream.transform', { chunk, str });

        return ctl.enqueue(str);
      },
      flush() {
        dec = null;
      }
    });
  }
}