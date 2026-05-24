# pgsql

Source: `quickjs-pgsql.c` — module exports **`PGconn`**, **`PGerror`**, **`PGresult`**

Asynchronous PostgreSQL client (wraps `libpq`). Connection and queries run
non-blocking against the connection's socket fd.

## PGconn

```js
new PGconn()   // length 1
```

### Methods

| Method | Args | Description |
| --- | --- | --- |
| `connect(params)` | 1 | Connects using a conninfo string/object; resolves when ready. |
| `query(sql)` | 1 | Runs a query; resolves to a `PGresult` (alias `execute`). |
| `close()` | 0 | Closes the connection. |
| `escapeString(str)` | 1 | Escapes a string literal value. |
| `escapeLiteral(str)` | 1 | Quotes-and-escapes a string literal. |
| `escapeIdentifier(str)` | 1 | Quotes-and-escapes an identifier. |
| `escapeBytea(buf)` | 1 | Encodes binary data as `bytea`. |
| `unescapeBytea(str)` | 1 | Decodes a `bytea` representation. |
| `valueString(value)` | 0 | Renders a single value as SQL. |
| `valuesString(values)` | 1 | Renders a value list as SQL. |
| `insertQuery(table, row)` | 2 | Builds an `INSERT` statement. |

### Properties

| Property | Kind | Description |
| --- | --- | --- |
| `cmdTuples` / `affectedRows` | getter | Rows affected by the last command. |
| `nonblocking` | getter/setter | Non-blocking mode flag. |
| `fd` | getter | Connection socket descriptor. |
| `errorMessage` | getter | Last error text. |
| `options` | getter | Connection options. |
| `insertId` | getter | Last inserted id. |
| `charset` | getter/setter | Client encoding. |
| `protocolVersion` / `serverVersion` | getter | Protocol / server version. |
| `user`, `password`, `host`, `port`, `db`, `conninfo` | getter | Connection parameters. |

### Static functions

`escapeString(str)`, `escapeBytea(buf)`, `unescapeBytea(str)`.

## PGresult

A result set; iterable.

| Member | Args | Kind | Description |
| --- | --- | --- | --- |
| `fetchRow()` | 0 | method | Next row as an array. |
| `fetchAssoc()` | 0 | method | Next row as an object. |
| `fetchField(i)` | 1 | method | Metadata for one field. |
| `fetchFields()` | 0 | method | Metadata for all fields. |
| `eof` | — | getter | Whether rows are exhausted. |
| `numRows` | — | getter | Row count (enumerable). |
| `numFields` | — | getter | Field count (enumerable). |
| `[Symbol.iterator]()` | 0 | method | Row iteration. |

## PGerror

`Error` subclass (`name` = `"PGerror"`) carrying PostgreSQL error details.
