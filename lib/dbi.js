/**
 * dbi.js -- light database-independent abstraction over the
 * quickjs-{sqlite,mysql,pgsql} bindings. Inspired by Perl DBI and
 * libdbi: a unified Database/Result API regardless of the underlying
 * driver. Each driver adapter translates the common surface into the
 * native module's calls.
 */

const ADAPTERS = Object.create(null);

export class Database {
  static register(name, AdapterClass) {
    ADAPTERS[name] = AdapterClass;
  }

  static async connect(driver, options) {
    const Adapter = ADAPTERS[driver];

    if(!Adapter) throw new Error(`dbi: unknown driver '${driver}' (available: ${Object.keys(ADAPTERS).join(', ')})`);

    const adapter = new Adapter();
    await adapter.connect(options ?? {});

    return new Database(driver, adapter);
  }

  constructor(driver, adapter) {
    this.driver = driver;
    this._adapter = adapter;
  }

  /** Run a SQL statement and return a Result (rows + metadata). */
  async query(sql) {
    const raw = await this._adapter.query(sql);
    return new Result(raw, this._adapter);
  }

  /** Run a SQL statement and return the affected row count. */
  async exec(sql) {
    const raw = await this._adapter.query(sql);

    /* sqlite's sync query returns the number directly for non-SELECT */
    if(typeof raw === 'number') return raw;

    return this._adapter.affectedRows;
  }

  async close() {
    return this._adapter.close();
  }

  get insertId() {
    return this._adapter.insertId;
  }

  get affectedRows() {
    return this._adapter.affectedRows;
  }

  /** Quote a single JS value as a SQL literal (string / number / NULL / blob). */
  quote(value) {
    return this._adapter.valueString(value);
  }

  /** Build "INSERT INTO table (cols...) VALUES (...);". */
  insertQuery(table, fields, values) {
    return this._adapter.insertQuery(table, fields, values);
  }
}

/** Driver-agnostic wrapper around a single result set. */
export class Result {
  constructor(raw, adapter) {
    this._raw = raw;
    this._adapter = adapter;
  }

  get numFields() {
    const r = this._raw;
    if(!r || typeof r !== 'object') return 0;
    return r.numFields ?? 0;
  }

  fetchFields() {
    const r = this._raw;
    if(!r || typeof r !== 'object' || typeof r.fetchFields !== 'function') return [];
    return r.fetchFields();
  }

  /** Async iterator that works for sync (sqlite, pgsql) and async (mysql) raw results. */
  async *[Symbol.asyncIterator]() {
    const r = this._raw;
    if(!r || typeof r !== 'object') return;

    if(typeof r[Symbol.asyncIterator] === 'function') for await(const row of r) yield row;
    else if(typeof r[Symbol.iterator] === 'function') for(const row of r) yield row;
  }

  /** Collect all rows into an array. */
  async all() {
    const out = [];
    for await(const row of this) out.push(row);
    return out;
  }
}

/* ---------------- SQLite adapter (synchronous driver) ---------------- */
class SQLiteAdapter {
  async connect(options) {
    const { SQLite3 } = await import('sqlite');

    this._SQLite3 = SQLite3;

    const filename = typeof options === 'string' ? options : (options.filename ?? options.database ?? ':memory:');

    this.db = new SQLite3(filename);
  }

  async query(sql) {
    return this.db.query(sql);
  }

  async close() {
    this.db.close();
  }

  get insertId() {
    return this.db.insertId;
  }

  get affectedRows() {
    return this.db.affectedRows;
  }

  valueString(value) {
    return this.db.valueString(value);
  }

  insertQuery(table, fields, values) {
    return this.db.insertQuery(table, fields, values);
  }
}

/* ---------------- PostgreSQL adapter ---------------- */
class PGSQLAdapter {
  async connect(options) {
    /* PG's async connect path needs os.setReadHandler/setWriteHandler. */
    if(typeof globalThis.os === 'undefined') globalThis.os = await import('os');

    const { PGconn } = await import('pgsql');

    this._PGconn = PGconn;
    this.db = new PGconn();

    if(typeof options === 'string') await this.db.connect(options);
    else
      await this.db.connect(
        options.host ?? 'localhost',
        options.user ?? '',
        options.password ?? '',
        options.database ?? options.dbname ?? '',
        options.port ?? 5432,
        options.timeout ?? 10,
      );
  }

  async query(sql) {
    return this.db.query(sql);
  }

  async close() {
    this.db.close();
  }

  get insertId() {
    return this.db.insertId;
  }

  get affectedRows() {
    return this.db.affectedRows;
  }

  valueString(value) {
    return this.db.valueString(value);
  }

  insertQuery(table, fields, values) {
    return this.db.insertQuery(table, fields, values);
  }
}

/* ---------------- MySQL adapter ---------------- */
class MySQLAdapter {
  async connect(options) {
    /* MySQL's async (OPT_NONBLOCK) path uses asyncclosure which calls
     * js_iohandler_fn(ctx, ..., "io") — so we need globalThis.io,
     * not globalThis.os. */
    if(typeof globalThis.io === 'undefined') globalThis.io = await import('io');

    const { MySQL } = await import('mysql');

    this._MySQL = MySQL;
    this.db = new MySQL();

    this.db.setOption(MySQL.OPT_NONBLOCK, true);
    this.db.resultType |= MySQL.RESULT_OBJECT;

    await this.db.connect(
      options.host ?? 'localhost',
      options.user ?? '',
      options.password ?? '',
      options.database ?? options.dbname ?? '',
    );
  }

  async query(sql) {
    return this.db.query(sql);
  }

  async close() {
    this.db.close();
  }

  get insertId() {
    return this.db.insertId;
  }

  get affectedRows() {
    return this.db.affectedRows;
  }

  valueString(value) {
    return this._MySQL.valueString(value);
  }

  insertQuery(table, fields, values) {
    return this.db.insertQuery(table, fields, values);
  }
}

Database.register('sqlite', SQLiteAdapter);
Database.register('pgsql', PGSQLAdapter);
Database.register('mysql', MySQLAdapter);

export default Database;
