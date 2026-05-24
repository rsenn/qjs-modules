# sockets

Source: `quickjs-sockets.c` — module exports **`Socket`**, **`AsyncSocket`**, **`SockAddr`**, re-exports **`SyscallError`**, plus a function list and `SOCK_*`/`AF_*`/… constants.

BSD-socket bindings. `Socket` is the blocking API; `AsyncSocket` exposes the same
surface but its I/O methods return promises.

## Module functions

| Function | Args | Description |
| --- | --- | --- |
| `socketpair(domain, type, protocol, out)` | 4 | Creates a connected pair of sockets. |
| `select(fds[, timeout])` | 1 | `select(2)` over a set of descriptors. |
| `poll(fds[, timeout])` | 1 | `poll(2)` over a set of descriptors. |

## SockAddr

```js
new SockAddr([family, addr, port])   // length 1
```

| Member | Args | Kind | Description |
| --- | --- | --- | --- |
| `family` | — | getter | Address family (enumerable). |
| `addr` | — | getter/setter | IP address (writable). |
| `port` | — | getter/setter | Port number (writable). |
| `path` | — | getter/setter | Path for `AF_UNIX` sockets. |
| `buffer` | — | getter | Raw `sockaddr` bytes. |
| `byteLength` | — | getter | Size of the address structure. |
| `clone()` | 0 | method | Copies the address. |
| `toString()` | 0 | method | Renders as `addr:port` / path. |

## Socket / AsyncSocket

```js
new Socket(domain, type, protocol)        // length 1
new AsyncSocket(domain, type, protocol)   // length 1
```

### Methods

| Method | Args | Description |
| --- | --- | --- |
| `ndelay([on])` | 0 | Toggles non-blocking mode. |
| `bind(addr)` | 1 | Binds to a local `SockAddr`. |
| `connect(addr)` | 1 | Connects to a remote `SockAddr`. |
| `listen([backlog])` | 0 | Marks the socket as listening. |
| `accept()` | 0 | Accepts an incoming connection. |
| `send(data)` | 1 | Sends data. |
| `sendto(data, addr)` | 2 | Sends a datagram to `addr`. |
| `recv(buffer)` | 1 | Receives into a buffer. |
| `recvfrom(buffer, addr)` | 2 | Receives a datagram and the sender address. |
| `sendmsg` / `recvmsg` | 1 | Scatter/gather message I/O. |
| `sendmmsg` / `recvmmsg` | 1 | Multiple-message I/O. |
| `shutdown(how)` | 1 | Shuts down one or both directions. |
| `close()` | 0 | Closes the socket. |
| `getsockopt(level, name, out)` | 3 | Reads a socket option. |
| `setsockopt(level, name, value)` | 3 | Sets a socket option. |
| `valueOf()` | 0 | Returns the fd (also `[Symbol.toPrimitive]`). |

On `AsyncSocket`, `accept`, `send`, `sendto`, `recv`, `recvfrom`, `sendmsg`,
`recvmsg`, `sendmmsg`, `recvmmsg` are promise-returning.

### Properties

| Property | Kind | Description |
| --- | --- | --- |
| `fd` | getter | File descriptor. |
| `local` / `remote` | getter | Local / peer `SockAddr`. |
| `open` | getter | Whether the socket is open. |
| `eof` | getter | Whether the peer closed the connection. |
| `mode` | getter/setter | I/O mode. |
| `nonblock` | getter/setter | Non-blocking flag. |

### Static functions

| Function | Args | Description |
| --- | --- | --- |
| `adopt(fd)` | 1 | Wraps an existing descriptor as a `Socket`. |

## Constants

Exported `SOCK_*`, `AF_*`, `SOL_*`, `SO_*`, `IPPROTO_*`, `MSG_*`, `SHUT_*`,
`POLL*` and related defines (see `js_sockets_defines`).
