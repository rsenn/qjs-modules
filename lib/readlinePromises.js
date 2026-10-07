/* readlinePromises.js: Node's `readline/promises` (also Bun's, Deno's).
 *
 * ```js
 * import { createInterface } from 'readline/promises';
 *
 * const rl = createInterface({ input: process.stdin, output: process.stdout });
 * const name = await rl.question('name? ');
 * ```
 *
 * `question(query, { signal })` resolves with the answer; an aborted `signal` rejects with an
 * AbortError; closing the interface leaves a pending question unsettled, as in Node.
 */
import { promises } from 'readline';

export const { Interface, createInterface } = promises;
