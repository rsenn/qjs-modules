# repeater

Source: `quickjs-repeater.c` — module export: **`Repeater`**

An async-iterable push stream (similar to the `@repeaterjs/repeater` primitive):
a producer callback pushes values that consumers receive via async iteration.

## Constructor

```js
new Repeater(executor)   // length 1
```

`executor(push, stop)` receives functions to emit values and to terminate.

## Methods / properties

| Member | Args | Kind | Description |
| --- | --- | --- | --- |
| `next()` | 0 | method | Returns a promise for the next `{value, done}`. |
| `state` | — | getter | Current repeater state. |
| `[Symbol.asyncIterator]()` | 0 | method | Returns the async iterator (itself). |

## Static functions (combinators)

| Function | Args | Description |
| --- | --- | --- |
| `race(repeaters)` | 1 | Yields from whichever input settles first. |
| `merge(repeaters)` | 1 | Interleaves values from all inputs. |
| `zip(repeaters)` | 1 | Yields tuples combining one value from each input. |
