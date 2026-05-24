# mysql

Source: `quickjs-mysql.c` — module exports **`MySQL`**, **`MySQLError`**, **`MySQLResult`**

Asynchronous MySQL/MariaDB client (wraps `libmysqlclient`). Connection and query
methods are non-blocking and return promises driven off the socket fd.

## MySQL

```js
new MySQL()   // length 1
```

### Methods

| Method | Args | Description |
| --- | --- | --- |
| `connect(params)` | 1 | Connects using `{host, user, password, db, port, socket, …}`; resolves when ready. |
| `query(sql)` | 1 | Runs a query; resolves to a `MySQLResult` (alias `execute`). |
| `close()` | 0 | Closes the connection. |
| `escapeString(str)` | 1 | Escapes a string for safe interpolation. |
| `getOption(opt)` | 1 | Reads a connection option. |
| `setOption(opt, value)` | 2 | Sets a connection option. |

### Properties (read-only)

`moreResults`, `affectedRows`, `warningCount`, `fieldCount`, `fd`/`socket`,
`errno`, `error`, `info`, `insertId`, `charset`, `timeout`, `timeoutMs`,
`serverName`, `serverInfo`, `serverVersion`, `user`, `password`, `host`, `port`,
`db`, `status`, `pending`.

### Static members

| Member | Args | Kind | Description |
| --- | --- | --- | --- |
| `clientInfo` | — | getter | Client library info string. |
| `clientVersion` | — | getter | Client library version. |
| `threadSafe` | — | getter | Whether the client is thread-safe. |
| `escapeString(str)` | 1 | function | Static escaping helper. |
| `valueString(value)` | 0 | function | Renders one value as SQL. |
| `valuesString(values)` | 1 | function | Renders a value list as SQL. |
| `insertQuery(table, row)` | 2 | function | Builds an `INSERT` statement. |

## MySQLResult

A result set; iterable (sync and async).

| Member | Args | Kind | Description |
| --- | --- | --- | --- |
| `next()` | 0 | method | Fetches the next row. |
| `fetchRow()` | 0 | method | Fetches the next row as an array. |
| `fetchAssoc()` | 0 | method | Fetches the next row as an object. |
| `fetchField(i)` | 1 | method | Returns metadata for one field. |
| `fetchFields()` | 0 | method | Returns metadata for all fields. |
| `eof` | — | getter | Whether all rows are consumed. |
| `numRows` | — | getter | Row count (enumerable). |
| `numFields` | — | getter | Field count (enumerable). |
| `fieldCount` | — | getter | Field count. |
| `currentField` | — | getter | Index of the current field. |
| `[Symbol.iterator]` / `[Symbol.asyncIterator]` | 0 | method | Row iteration. |

## MySQLError

`Error` subclass (`name` = `"MySQLError"`) carrying MySQL error details.
