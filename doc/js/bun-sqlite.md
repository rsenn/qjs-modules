# bun-sqlite

Source: `lib/bun/sqlite.js`, imported as `bun:sqlite`

`Database`, `Statement`, `SQLiteError` and `constants` of Bun's `bun:sqlite`, on top of the native [sqlite](../native/sqlite.md) module (needs `libsqlite3`). Names, argument shapes and results follow Bun (checked against bun 1.4.2). The `bun:<name>` specifier is resolved by [bun](bun.md).

```js
import { Database } from 'bun:sqlite';

const db = new Database(':memory:');

db.run('create table t(id integer primary key, name text)');
db.query('insert into t(name) values ($name)').run({ $name: 'a' });
db.query('select * from t where id = ?').get(1); // { id: 1, name: 'a' }
```

| `bun:sqlite` | Implemented with |
| --- | --- |
| `new Database(file, options)`, `Database.open()` | `SQLite3`, options `readonly`, `create`, `strict`, `safeIntegers` or a flags number |
| `db.query(sql)` | `prepare()`, cached by sql (`Database.MAX_QUERY_CACHE_SIZE` = 20) |
| `db.prepare(sql)` | `SQLite3.prototype.prepare()`, not cached |
| `db.run(sql, ...values)`, `db.exec()` | `exec()` without values, a prepared statement with them |
| `db.transaction(fn)` | `BEGIN`, a `SAVEPOINT` when nested, `.deferred`, `.immediate`, `.exclusive` |
| `db.inTransaction`, `db.close()`, `db.clearQueryCache()` | `inTransaction`, `close()`, `finalize()` of each cached statement |
| `stmt.get()`, `all()`, `values()`, `run()`, `iterate()` | `bind()`, `fetchRow()`, `reset()` |
| `stmt.as(Class)` | the rows get `Class.prototype`, the constructor is not run |
| `stmt.columnNames`, `declaredTypes`, `paramsCount` | `fetchFields()`, `paramCount` |
| `stmt.finalize()`, `isFinalized`, `safeIntegers` | `finalize()`, the `RESULT_BIGINT` flag |
| `SQLiteError` | `SQLite3Error` with `code`, `errno` (the extended code, `SQLITE_CONSTRAINT_UNIQUE` is 2067) |

Binding: a blob goes in from an `ArrayBuffer` or any view and comes out as a `Uint8Array`. `NaN`, `Infinity`, `Date` and plain objects bind `NULL`, a number with an integer value binds `INTEGER`. A list of values must match the parameter count. A named parameter needs its prefix (`{ $id: 1 }`) unless the database was opened with `strict: true`.

## Differences from Bun

Not implemented: `serialize()`, `Database.deserialize()`, `loadExtension()`, `fileControl()`, `Database.setCustomSQLite()`, `Statement.columnTypes`, `Statement.native`. `Statement.toString()` is the sql text, Bun's expands the bound values. `SQLiteError.byteOffset` is always -1, and `SQLiteError.constructor` is `SQLiteError`, Bun's is `Error`.
