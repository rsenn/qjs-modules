import { ReadableStream } from 'stream.so';

ReadableStream.prototype.values = ReadableStream.prototype[Symbol.asyncIterator] = function() {
  const reader = this.getReader();
  const result = (value, done = true) => (done && (reader.releaseLock(), (reader = null)), { value, done });

  return {
    async next() {
      const value = reader ? await reader.read() : null;

      return result(value, !value);
    },
    return: result,
    throw: result,
  };
};

export * from 'stream.so';
