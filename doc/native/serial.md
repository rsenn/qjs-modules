# serial

Source: `quickjs-serial.c` — module exports **`Serial`**, **`SerialPort`**, **`SerialError`**

Serial-port access (Web Serial-flavored API over POSIX termios).

## SerialPort

```js
new SerialPort([path, options])   // length 1
```

| Member | Args | Kind | Description |
| --- | --- | --- | --- |
| `open()` | 0 | method | Opens the port. |
| `close()` | 0 | method | Closes the port. |
| `getInfo()` | 0 | method | Returns port info (vendor/product, etc.). |
| `getSignals()` | 0 | method | Reads control signals (CTS/DSR/DCD/RI). |
| `setSignals(signals)` | 1 | method | Sets control signals (DTR/RTS/BRK). |
| `read(dest)` | 1 | method | Reads bytes into a buffer. |
| `write(data)` | 1 | method | Writes bytes. |
| `drain()` | 0 | method | Waits until pending output is transmitted. |
| `flush()` | 0 | method | Discards buffered input/output. |
| `fd` | — | getter | Underlying file descriptor. |
| `name` | — | getter | Device name. |
| `transport` | — | getter | Transport identifier. |
| `description` | — | getter | Human-readable description. |
| `inputWaiting` | — | getter | Bytes available to read. |
| `outputWaiting` | — | getter | Bytes pending to write. |

## Serial

Static-only namespace object.

| Function | Args | Description |
| --- | --- | --- |
| `getPorts()` | 0 | Enumerates available serial ports. |
| `requestPort(filter)` | 1 | Selects a port matching `filter`. |

## SerialError

`Error` subclass (`name` = `"SerialError"`) raised on serial I/O failures.
