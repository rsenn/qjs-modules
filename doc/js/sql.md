# sql

Source: `lib/sql.js` (pure JS) — wraps `dbi.js`'s driver-agnostic `Database`

A [Bun `bun:sql`](https://bun.com/docs/runtime/sql)-flavored tagged-template
SQL client, unifying the `sqlite`/`pgsql`/`mysql` native drivers behind one
JS-facing API instead of three separate ones.

```js
import { SQL } from 'sql';

const sql = new SQL('sqlite://./app.db'); // or postgres://..., mysql://..., ':memory:'

const rows = await sql`SELECT * FROM users WHERE id = ${id}`;
// [{ id: 1, name: 'Ada', ... }, ...]
```

## Scope, vs. Bun's own `bun:sql`

- **Single connection, no pooling** — no `max`/`idleTimeout`/`reserve()`.
  Connects lazily on first query, then reuses one connection.
- **Escaping, not wire-protocol placeholders** — none of the three native
  drivers (see `doc/native/pgsql.md`, `doc/native/mysql.md`,
  `quickjs-sqlite.c`) expose real prepared-statement parameter binding,
  only a `query(sql)` taking a full string plus a `valueString()`/`quote()`
  escaping helper. Interpolated `${...}` values are escaped via the
  underlying driver's own escaping and inlined into the query text before
  it's sent — still injection-safe, just not sent as separate bind
  parameters over the wire the way Bun's own Zig-native drivers do.
- No `.raw()`, `.simple()`, `sql.listen()`/`.notify()` (Postgres pub/sub),
  `sql.reserve()`, or distributed-transaction (`beginDistributed`) support.

## `new SQL(url | options)`

```js
new SQL(':memory:')                                    // sqlite, in-memory
new SQL('./app.db')                                     // sqlite, file (no scheme)
new SQL('sqlite://./app.db')                             // sqlite, explicit scheme
new SQL('postgres://user:pass@host:5432/dbname')         // pgsql
new SQL('mysql://user:pass@host/dbname')                 // mysql
new SQL({ adapter: 'pgsql', host, user, password, database, port })
```

An options object must include an `adapter` field (`'sqlite'`, `'pgsql'`, or
`'mysql'`); the rest is passed straight through to that driver's
`dbi.js` adapter (see `doc/js/dbi.md`... — track via `doc/api-compatibility.md`
until a dedicated `dbi.md` exists).

The returned value is itself a callable tagged-template function.

## Methods

| Method | Description |
| --- | --- |
| `` sql`...` `` | Runs the interpolated query, resolving to an array of row objects (`{col: value}`). |
| `` sql`...`.values() `` | Same query, resolving to an array of positional-value arrays instead of row objects. |
| `sql.unsafe(text, params=[])` | Runs a raw SQL string with `?`-placeholder params, escaped the same way as the tagged-template form. |
| `sql.begin(async tx => {...})` | Runs `fn` inside a `BEGIN`/`COMMIT` transaction, `ROLLBACK`-ing and rethrowing on error. `tx` is the same `SQL` instance (single connection). |
| `sql.close()` | Closes the underlying connection. |
| `sql.driver` | The resolved adapter name (`'sqlite'`, `'pgsql'`, or `'mysql'`). |

## `SQLError`

Thrown for a bad connection string/options object (unsupported scheme, or an
options object missing `adapter`). Errors from the query itself propagate as
whatever the underlying driver throws (`SQLite3Error`, `PGerror`, `MySQLError`
— see their respective native docs), unchanged.
