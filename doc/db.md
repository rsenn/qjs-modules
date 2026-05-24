# db

Source: `lib/db.js` (pure JS)

A small connection-pool abstraction over a database client (e.g. the
[`mysql`](mysql.md) / [`pgsql`](pgsql.md) bindings).

## Exports

| Export | Kind | Description |
| --- | --- | --- |
| `Pool` | class | Manages a pool of connections; hands out `PoolClient`s and runs queries. |
| `PoolClient` | class | A single checked-out connection: `query()`, `release()`. |
