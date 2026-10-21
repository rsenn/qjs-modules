/* textLineStream.js: Deno's TextLineStream (@std/streams); a WHATWG TransformStream.
 * depends on: the TransformStream global (qjsm: `import 'globals'` first).
 * rule: no native code; the same results as `jsr:@std/streams` on deno.
 *
 * ```js
 * import { TextLineStream } from './textLineStream.js';
 *
 * bytes.pipeThrough(new TextDecoderStream()).pipeThrough(new TextLineStream())
 * // 'a\r\nb\nc' in any chunking -> 'a', 'b', 'c'
 * ```
 *
 *   boolean  allowCR  a lone `\r` also ends a line, default false
 *
 * lines end at `\n` or `\r\n`, without the terminator; a final line without one is
 * emitted at the end of the stream; no empty line follows a trailing newline.
 */
const { TransformStream } = globalThis;

if(typeof TransformStream != 'function') throw new TypeError('textLineStream: TransformStream is not defined (import globals first)');

export class TextLineStream extends TransformStream {
  constructor({ allowCR = false } = {}) {
    let buf = '';

    super({
      transform(chunk, controller) {
        buf += chunk;

        /* with allowCR a \r at the end waits, the next chunk may start with \n */
        const re = allowCR ? /\r\n|\n|\r(?=[^])/g : /\r?\n/g;
        let start = 0,
          m;

        while((m = re.exec(buf))) {
          controller.enqueue(buf.slice(start, m.index));
          start = re.lastIndex;
        }

        buf = buf.slice(start);
      },
      flush(controller) {
        if(allowCR && buf.endsWith('\r')) buf = buf.slice(0, -1);
        if(buf) controller.enqueue(buf);
      },
    });
  }
}
