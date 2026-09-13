/**
 * sql.js -- a Bun bun:sql-flavored tagged-template SQL client, unifying
 * the sqlite/pgsql/mysql drivers behind one JS-facing API (see
 * https://bun.com/docs/runtime/sql). Built on top of dbi.js's
 * already driver-agnostic Database/Result.
 *
 * Scope, vs. Bun's own bun:sql: single connection (no pooling - no
 * `max`/`idleTimeout`/`reserve()`), and parameter interpolation is done
 * by escaping each value via the adapter's quote() and inlining it into
 * the query text (none of the three native drivers expose real
 * placeholder/prepared-statement binding - see doc/native/{pgsql,mysql}.md
 * and quickjs-sqlite.c), not sent as separate wire-protocol parameters.
 * `.unsafe()`/`.begin()` and the tagged-template call itself still protect
 * against SQL injection the same way lib/database.js's Statement already
 * does for sqlite alone; this module just extends that idea across all
 * three drivers with one shared API surface.
 */
import { Database } from 'dbi';
import { URL } from 'url';

export class SQLError extends Error {}

const SCHEME_ADAPTERS = {
  postgres: 'pgsql',
  postgresql: 'pgsql',
  pgsql: 'pgsql',
  mysql: 'mysql',
  sqlite: 'sqlite',
};

function parseConnectionOptions(urlOrOptions) {
  if(typeof urlOrOptions == 'object' && urlOrOptions != null) {
    const { adapter, ...options } = urlOrOptions;
    if(!adapter) throw new SQLError(`sql: options object must have an 'adapter' field ('sqlite', 'pgsql', or 'mysql')`);
    return { adapter, options };
  }

  const str = String(urlOrOptions);

  if(str == ':memory:' || !/:\/\//.test(str)) return { adapter: 'sqlite', options: { filename: str } };

  const u = new URL(str);
  const scheme = u.protocol.replace(/:$/, '');
  const adapter = SCHEME_ADAPTERS[scheme];

  if(!adapter) throw new SQLError(`sql: unsupported connection scheme '${scheme}:' (expected postgres:, mysql:, or sqlite:)`);

  if(adapter == 'sqlite') return { adapter, options: { filename: u.pathname || u.hostname || ':memory:' } };

  return {
    adapter,
    options: {
      host: u.hostname || 'localhost',
      port: u.port ? Number(u.port) : undefined,
      user: decodeURIComponent(u.username || ''),
      password: decodeURIComponent(u.password || ''),
      database: u.pathname.replace(/^\//, ''),
    },
  };
}

/** A tagged-template query, built lazily and only run once awaited/iterated. */
class PendingQuery {
  constructor(sql, strings, values) {
    this._sql = sql;
    this._strings = strings;
    this._values = values;
    this._mode = 'objects';
  }

  /** Switch the result shape to arrays of positional values instead of row objects. */
  values() {
    this._mode = 'values';
    return this;
  }

  async _run() {
    const db = await this._sql._connect();

    let text = this._strings[0];
    for(let i = 0; i < this._values.length; i++) text += db.quote(this._values[i]) + this._strings[i + 1];

    const result = await db.query(text);
    const rows = await result.all();

    if(this._mode == 'values') return rows.map(row => (Array.isArray(row) ? row : Object.values(row)));

    return rows;
  }

  then(onFulfilled, onRejected) {
    return this._run().then(onFulfilled, onRejected);
  }

  catch(onRejected) {
    return this._run().catch(onRejected);
  }

  finally(onFinally) {
    return this._run().finally(onFinally);
  }
}

/**
 * new SQL(url | options) - a callable tagged-template SQL client:
 *
 *   const sql = new SQL('sqlite://./app.db');
 *   const rows = await sql`SELECT * FROM users WHERE id = ${id}`;
 */
export class SQL {
  constructor(urlOrOptions) {
    const instance = (strings, ...values) => new PendingQuery(instance, strings, values);

    Object.setPrototypeOf(instance, SQL.prototype);

    const { adapter, options } = parseConnectionOptions(urlOrOptions);
    instance._adapter = adapter;
    instance._options = options;
    instance._db = null;
    instance._connecting = null;

    return instance;
  }

  async _connect() {
    if(this._db) return this._db;

    if(!this._connecting) this._connecting = Database.connect(this._adapter, this._options).then(db => (this._db = db));

    return this._connecting;
  }

  /** Run a raw SQL string with '?'-placeholder params, escaped the same way as the tagged-template form. */
  unsafe(text, params = []) {
    const parts = text.split('?');

    if(parts.length - 1 != params.length) throw new SQLError(`sql.unsafe(): ${parts.length - 1} '?' placeholders but ${params.length} params given`);

    return new PendingQuery(this, parts, params);
  }

  /**
   * Run `fn(tx)` inside a transaction (BEGIN/COMMIT, ROLLBACK on throw).
   * `tx` is this same SQL instance - single-connection, so queries made
   * with the outer `sql` during a `begin()` would interleave on the
   * same connection; only use `tx` inside the callback.
   */
  async begin(fn) {
    const db = await this._connect();

    await db.query('BEGIN');

    let result;
    try {
      result = await fn(this);
    } catch(e) {
      await db.query('ROLLBACK').catch(() => {});
      throw e;
    }

    await db.query('COMMIT');
    return result;
  }

  async close() {
    if(this._db) await this._db.close();
  }

  get driver() {
    return this._adapter;
  }
}

export default SQL;
