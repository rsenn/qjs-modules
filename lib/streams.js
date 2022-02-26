import { ReadableStream, WritableStream } from 'stream';
import * as std from 'std';

export function FileSystemReadableFileStream(path, bufSize = 1024 * 64) {
  let file,
    error = {};

  return new ReadableStream({
    start(controller) {
      console.log('ReadableStream.start', { controller });
      file = std.open(path, 'rb', error);
      if(error.errno) throw new Error("Error opening '" + path + "': " + std.strerror(error.errno));
    },
    pull(controller) {
      if(file.eof()) {
        console.log('ReadableStream.pull', { eof: true });
        controller.close();
        return;
      }

      let buf = new ArrayBuffer(bufSize);
      let ret = file.read(buf, 0, bufSize);

      console.log('ReadableStream.pull', { controller, ret });

      if(ret > 0) controller.enqueue(ret == bufSize ? buf : buf.slice(0, ret));

      if(file.error()) {
        console.log('ReadableStream.pull', { error: true });
        controller.error(file);
      }
    },
    cancel(reason) {
      file.close();
      console.log('ReadableStream.cancel', { reason });
    }
  });
}

export function FileSystemWritableFileStream(path) {
  let file,
    error = {};

  return new WritableStream({
    start(controller) {
      file = std.open(path, 'w+', error);
      console.log('WritableStream.start', { file, error, controller });

      if(error.errno) throw new Error("Error opening '" + path + "': " + std.strerror(error.errno));
    },
    write(chunk, controller) {
      let ret = file.write(chunk, 0, chunk.byteLength);
      console.log('WritableStream.write', { chunk, controller });

      if(file.error()) {
        console.log('WritableStream.write', { error: true });
        controller.error(file);
      }
    },
    close(controller) {
      console.log('WritableStream.close', { controller });

      file.close();
    },
    abort(reason) {
      console.log('WritableStream.abort', { reason });

      file.close();
    }
  });
}
