/* timersPromises.js: Node's `timers/promises` (also Bun's, Deno's).
 *
 * ```js
 * import { setTimeout as sleep } from 'timers/promises';
 * await sleep(100, 'value');   // resolves 'value' after 100 ms
 * ```
 *
 * `signal` (AbortSignal) rejects with an AbortError; the iterator form of
 * setInterval() yields `value` every `delay` ms until it is returned.
 */
import { clearTimeout, setTimeout as setTimer } from 'timers';

const abortError = signal => Object.assign(new Error('The operation was aborted', { cause: signal.reason }), { name: 'AbortError', code: 'ABORT_ERR' });

export function setTimeout(delay, value, { signal } = {}) {
  if(signal?.aborted) return Promise.reject(abortError(signal));

  return new Promise((resolve, reject) => {
    const id = setTimer(() => resolve(value), delay);

    signal?.addEventListener(
      'abort',
      () => {
        clearTimeout(id);
        reject(abortError(signal));
      },
      { once: true },
    );
  });
}

export function setImmediate(value, options) {
  return setTimeout(0, value, options);
}

export async function* setInterval(delay, value, { signal } = {}) {
  for(;;) {
    await setTimeout(delay, undefined, { signal });
    yield value;
  }
}

export const scheduler = { wait: (delay, options) => setTimeout(delay, undefined, options), yield: () => setImmediate() };

export default { setTimeout, setImmediate, setInterval, scheduler };
