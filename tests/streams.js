import { ReadableStream, WritableStream } from 'stream';
import * as std from 'std';

export function FileSystemReadableFileStream(path) {
  let file,
    error = {};

  return new ReadableStream({
    start(controller) {
      file = std.open(path, 'rb', error);
      if(error.errno) throw new Error("Error opening '" + path + "': " + std.strerror(error.errno));
    },
    pull(controller) {},
    cancel(reason) {}
  });
}

export function FileSystemWritableFileStream(path) {
  let file,
    error = {};

  return new WritableStream({
    start(controller) {
      file = std.open(path, 'w+', error);
      if(error.errno) throw new Error("Error opening '" + path + "': " + std.strerror(error.errno));
    },
    write(chunk, controller) {},
    close(controller) {},
    abort(reason) {}
  });
}
