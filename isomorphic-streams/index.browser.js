export const {
  ReadableStream,
  ReadableStreamDefaultReader,
  ReadableStreamBYOBReader,
  ReadableStreamBYOBRequest,
  ReadableByteStreamController,
  ReadableStreamDefaultController,
  TransformStream,
  TransformStreamDefaultController,
  WritableStream,
  WritableStreamDefaultWriter,
  WritableStreamDefaultController,
  ByteLengthQueuingStrategy,
  CountQueuingStrategy,
  TextEncoderStream,
  TextDecoderStream
} = window;

// Polyfill to make ReadableStream async-iterable with for-await
// https://jakearchibald.com/2017/async-iterators-and-generators/#making-streams-iterate
if(!ReadableStream.prototype[Symbol.asyncIterator]) {
  async function* streamAsyncIterator() {
    // Get a lock on the stream
    const reader = this.getReader();

    try {
      while(true) {
        // Read from the stream
        const { done, value } = await reader.read();
        // Exit if we're done
        if(done) return;
        // Else yield the chunk
        yield value;
      }
    } finally {
      reader.releaseLock();
    }
  }

  ReadableStream.prototype[Symbol.asyncIterator] = streamAsyncIterator;
}
