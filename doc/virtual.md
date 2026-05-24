# virtual

Source: `quickjs-virtual.c` — module export: **`VirtualProperties`**

Wraps an arbitrary backing store (array, map, plain object, …) behind a uniform
get/set/has/delete property interface — a lightweight virtual proxy.

## Constructor

```js
new VirtualProperties(target)   // length 1
```

## Methods

| Method | Args | Description |
| --- | --- | --- |
| `has(key)` | 1 | Returns whether `key` exists in the backing store. |
| `get(key)` | 1 | Returns the value for `key`. |
| `set(key, value)` | 2 | Stores `value` under `key`. |
| `delete(key)` | 1 | Removes `key`. |
| `keys()` | 0 | Returns the set of keys. |

## Static functions

| Function | Args | Description |
| --- | --- | --- |
| `array(target)` | 1 | Creates a `VirtualProperties` backed by an array. |
| `map(target)` | 1 | Creates one backed by a `Map`. |
| `object(target)` | 1 | Creates one backed by a plain object. |
| `from(target)` | 1 | Creates one by auto-detecting the backing-store type. |
