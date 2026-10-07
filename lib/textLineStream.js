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

        let start = 0;

        for(let i = 0; i < buf.length; i++) {
          const c = buf[i];

          if(c == '\n') {
            controller.enqueue(buf.slice(start, buf[i - 1] == '\r' && i > start ? i - 1 : i));
            start = i + 1;
          } else if(c == '\r' && allowCR) {
            if(i == buf.length - 1) break;

            controller.enqueue(buf.slice(start, i));
            if(buf[i + 1] == '\n') i++;
            start = i + 1;
          }
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
