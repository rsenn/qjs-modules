# readlinePromises

Source: `lib/readlinePromises.js` — compiled into `qjsm`; imported as `readline/promises`

Node's `readline/promises` (also Bun's and Deno's): the `readline` interface with a
promise-returning `question()`. See [readline](readline.md) for inputs and events.

```js
import { createInterface } from 'readline/promises';

const rl = createInterface({ input: process.stdin, output: process.stdout });
const name = await rl.question('name? ');
rl.close();
```

| Export | Description |
| --- | --- |
| `createInterface(...)` | as in `readline`, returning an `Interface` of this module |
| `Interface` | `question(query, { signal })` resolves with the answer |

An aborted `signal` rejects with an `AbortError` (`code: 'ABORT_ERR'`). Closing the interface
leaves a pending `question()` unsettled, as in Node. `Readline` (batched cursor commands) is
not implemented.
