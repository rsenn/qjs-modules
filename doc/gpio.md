# gpio

Source: `quickjs-gpio.c` — module export: **`GPIO`**

Reads and drives GPIO pins (Linux memory-mapped GPIO).

## Constructor

```js
new GPIO()   // length 1
```

## Methods

| Method | Args | Description |
| --- | --- | --- |
| `initPin(pin, mode)` | 2 | Configures `pin` as `GPIO.INPUT` or `GPIO.OUTPUT`. |
| `setPin(pin, value)` | 2 | Drives an output `pin` to `GPIO.LOW`/`GPIO.HIGH`. |
| `getPin(pin)` | 1 | Reads the current level of `pin`. |

## Properties (read-only)

| Property | Description |
| --- | --- |
| `buffer` | The underlying register buffer. |
| `[Symbol.toStringTag]` | `"GPIO"`. |

## Static constants

`INPUT` (0), `OUTPUT` (1), `LOW` (0), `HIGH` (1).
