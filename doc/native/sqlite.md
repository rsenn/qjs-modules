# sqlite

Source: `quickjs-sqlite.c` — module exports **`SQLite3`**, **`SQLite3Error`**, **`SQLite3Result`**

SQLite3 client (wraps `libsqlite3`).

## SQLite3

```js
new SQLite3()   // length 1
```

### Methods

| Method | Args | Description |
| --- | --- | --- |
| `open(filename)` | 1 | Opens (or creates) a database file. |
| `query(sql)` | 1 | Runs a query; returns a `SQLite3Result`. |
| `exec(sql)` | 1 | Runs a statement without returning rows. |
| `close()` | 0 | Closes the connection. |
| `escapeString(str)` | 1 | Escapes a string for safe interpolation. |
| `quoteString(str)` | 1 | Quotes a string as an SQL literal. |
| `valueString(value)` | 0 | Renders one value as SQL. |
| `valuesString(values)` | 1 | Renders a value list as SQL. |
| `insertQuery(table, row)` | 2 | Builds an `INSERT` statement. |

### Properties (read-only)

`affectedRows`/`changes`, `insertId`/`lastInsertRowid`, `totalChanges`,
`errorMessage`, `errorCode`, `filename`.

### Static members

| Member | Args | Kind | Description |
| --- | --- | --- | --- |
| `escapeString(str)` | 1 | function | Static escaping helper. |
| `quoteString(str)` | 1 | function | Static quoting helper. |
| `valueString(value)` | 0 | function | Renders one value as SQL. |

## SQLite3Result

A result set; iterable.

| Member | Args | Kind | Description |
| --- | --- | --- | --- |
| `fetchRow()` | 0 | method | Fetches the next row as an array. |
| `fetchAssoc()` | 0 | method | Fetches the next row as an object. |
| `fetchField(i)` | 1 | method | Returns metadata for one field. |
| `fetchFields()` | 0 | method | Returns metadata for all fields. |
| `reset()` | 0 | method | Resets the statement for re-execution. |
| `eof` | — | getter | Whether all rows are consumed. |
| `[Symbol.iterator]` | 0 | method | Row iteration. |

## SQLite3Error

`Error` subclass (`name` = `"SQLite3Error"`) carrying SQLite error details.
