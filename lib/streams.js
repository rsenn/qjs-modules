import { ReadableStream, WritableStream } from 'stream';
import * as std from 'std';

export function FileSystemReadableFileStream(path, bufSize = 1024) {
  let file,
    error = {};

  return new ReadableStream({
    start(controller) {
      console.log('ReadableStream.start', { controller });
      file = std.open(path, 'rb', error);
      if(error.errno) throw new Error("Error opening '" + path + "': " + std.strerror(error.errno));
    },
    pull(controller) {
      if(file.eof()) controller.close();

      let ret,
        buf = new ArrayBuffer(bufSize);

      if((ret = file.read(buf, 0, bufSize)) > 0) controller.enqueue(ret == bufSize ? buf : buf.slice(0, ret));

      if(file.error()) controller.error(file);

      console.log('ReadableStream.pull', { controller, ret });
    },
    cancel(reason) {
      console.log('ReadableStream.cancel', { reason });
    }
  });
}

export function FileSystemWritableFileStream(path) {
  let file,
    error = {};

  return new WritableStream({
    start(controller) {
      console.log('WritableStream.start', { controller });
      file = std.open(path, 'w+', error);
      if(error.errno) throw new Error("Error opening '" + path + "': " + std.strerror(error.errno));
    },
    write(chunk, controller) {
      console.log('WritableStream.write', { chunk, controller });
    },
    close(controller) {
      console.log('WritableStream.close', { controller });
    },
    abort(reason) {
      console.log('WritableStream.abort', { reason });
    }
  });
}
