/* jsonStreams.js: Deno's @std/json streams: JsonParseStream, ConcatenatedJsonParseStream,
 * JsonStringifyStream; WHATWG TransformStreams.
 * depends on: the TransformStream global (qjsm: `import 'globals'` first).
 * rule: no native code; the same results as `jsr:@std/json` on deno.
 *
 * ```js
 * import { JsonParseStream, JsonStringifyStream } from './jsonStreams.js';
 *
 * lines.pipeThrough(new JsonParseStream())                  // '{"a":1}' -> {a: 1}
 * values.pipeThrough(new JsonStringifyStream())             // {a: 1} -> '{"a":1}\n'
 * ```
 *
 * | class                         | writable | readable | chunk handling                      |
 * | ----------------------------- | -------- | -------- | ----------------------------------- |
 * | JsonParseStream               | string   | any      | one JSON value per chunk (a line)   |
 * | ConcatenatedJsonParseStream   | string   | any      | values back to back, any chunking   |
 * | JsonStringifyStream           | any      | string   | `prefix + JSON.stringify + suffix`  |
 *
 * every constructor takes `{ writableStrategy, readableStrategy }`; the parse streams error
 * the readable with a SyntaxError on bad or truncated JSON.
 */
const { TransformStream } = globalThis;

if(typeof TransformStream != 'function') throw new TypeError('jsonStreams: TransformStream is not defined (import globals first)');

/* JsonParseStream: each non-empty chunk is parsed as one JSON value.
 *
 * ```js
 * ['{"a":1}', '[2]', '']  ->  {a: 1}, [2]
 * ```
 */
export class JsonParseStream extends TransformStream {
  constructor({ writableStrategy, readableStrategy } = {}) {
    super(
      {
        transform(chunk, controller) {
          if(chunk) controller.enqueue(JSON.parse(chunk));
        },
      },
      writableStrategy,
      readableStrategy,
    );
  }
}

const isSpace = c => c == ' ' || c == '\t' || c == '\n' || c == '\r';

/* ConcatenatedJsonParseStream: values back to back, split by structure, not by lines.
 *
 * ```js
 * ['{"a":1}{"b"', ':2}[3] 4 5 ']  ->  {a: 1}, {b: 2}, [3], 4, 5
 * ```
 *
 * a number or literal ends at whitespace, `{`, `[`, `"` or the end of the stream.
 */
export class ConcatenatedJsonParseStream extends TransformStream {
  constructor({ writableStrategy, readableStrategy } = {}) {
    let cur = '', mode = 'idle', depth = 0, inStr = false, esc = false;

    const emit = controller => {
      const text = cur;

      cur = '';
      mode = 'idle';
      controller.enqueue(JSON.parse(text));
    };

    super(
      {
        transform(chunk, controller) {
          for(let i = 0; i < chunk.length; i++) {
            const c = chunk[i];

            if(mode == 'bare') {
              if(isSpace(c) || c == '{' || c == '[' || c == '"') emit(controller);
              else {
                cur += c;
                continue;
              }
            }

            if(mode == 'idle') {
              if(isSpace(c)) continue;

              cur = c;
              inStr = esc = false;
              depth = c == '{' || c == '[' ? 1 : 0;
              mode = depth ? 'compound' : c == '"' ? 'string' : 'bare';
              continue;
            }

            cur += c;

            if(esc) esc = false;
            else if(mode == 'string') {
              if(c == '\\') esc = true;
              else if(c == '"') emit(controller);
            } else if(inStr) {
              if(c == '\\') esc = true;
              else if(c == '"') inStr = false;
            } else if(c == '"') inStr = true;
            else if(c == '{' || c == '[') depth++;
            else if((c == '}' || c == ']') && --depth == 0) emit(controller);
          }
        },
        flush(controller) {
          if(mode == 'bare') emit(controller);
          else if(mode != 'idle') JSON.parse(cur);
        },
      },
      writableStrategy,
      readableStrategy,
    );
  }
}

/* JsonStringifyStream: each value becomes `prefix + JSON.stringify(value) + suffix`.
 *
 * ```js
 * new JsonStringifyStream({ prefix: '\x1e', suffix: '\n' })   // RFC 7464 JSON text sequences
 * ```
 *
 *   string  prefix  put before every value, default ''
 *   string  suffix  put after every value, default '\n'
 */
export class JsonStringifyStream extends TransformStream {
  constructor({ prefix = '', suffix = '\n', writableStrategy, readableStrategy } = {}) {
    super(
      {
        transform(chunk, controller) {
          controller.enqueue(`${prefix}${JSON.stringify(chunk)}${suffix}`);
        },
      },
      writableStrategy,
      readableStrategy,
    );
  }
}
