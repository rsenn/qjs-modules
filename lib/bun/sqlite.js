/* bun/sqlite.js: bun:sqlite, Database and Statement over the sqlite module.
 * depends on: sqlite (quickjs-sqlite.c).
 * rule: Bun's names, argument shapes and results, checked against bun 1.4.2.
 *
 * | bun:sqlite                  | here                                      |
 * | --------------------------- | ----------------------------------------- |
 * | Database, default export    | SQLite3, a prepared SQLite3Result per     |
 * |                             | Statement, cached by sql in query()       |
 * | SQLiteError                 | SQLite3Error converted, with code, errno  |
 * | constants                   | the SQLITE_* numbers Bun exports          |
 * | not here                    | serialize, deserialize, loadExtension,    |
 * |                             | fileControl, setCustomSQLite, columnTypes |
 */
import { SQLite3, SQLite3Error } from 'sqlite';

const { RESULT_OBJECT, RESULT_BIGINT, RESULT_BYTES, RESULT_PLAIN, OPEN_READONLY, OPEN_READWRITE, OPEN_CREATE } = SQLite3;

/* errno -> name, the extended codes a script is likely to test for */
const CODES = {
  1: 'SQLITE_ERROR', 2: 'SQLITE_INTERNAL', 3: 'SQLITE_PERM', 4: 'SQLITE_ABORT', 5: 'SQLITE_BUSY',
  6: 'SQLITE_LOCKED', 7: 'SQLITE_NOMEM', 8: 'SQLITE_READONLY', 9: 'SQLITE_INTERRUPT', 10: 'SQLITE_IOERR',
  11: 'SQLITE_CORRUPT', 12: 'SQLITE_NOTFOUND', 13: 'SQLITE_FULL', 14: 'SQLITE_CANTOPEN', 15: 'SQLITE_PROTOCOL',
  16: 'SQLITE_EMPTY', 17: 'SQLITE_SCHEMA', 18: 'SQLITE_TOOBIG', 19: 'SQLITE_CONSTRAINT', 20: 'SQLITE_MISMATCH',
  21: 'SQLITE_MISUSE', 22: 'SQLITE_NOLFS', 23: 'SQLITE_AUTH', 24: 'SQLITE_FORMAT', 25: 'SQLITE_RANGE',
  26: 'SQLITE_NOTADB', 27: 'SQLITE_NOTICE', 28: 'SQLITE_WARNING',
  261: 'SQLITE_BUSY_RECOVERY', 517: 'SQLITE_BUSY_SNAPSHOT', 773: 'SQLITE_BUSY_TIMEOUT',
  262: 'SQLITE_LOCKED_SHAREDCACHE', 516: 'SQLITE_ABORT_ROLLBACK',
  264: 'SQLITE_READONLY_RECOVERY', 520: 'SQLITE_READONLY_CANTLOCK', 776: 'SQLITE_READONLY_ROLLBACK',
  1032: 'SQLITE_READONLY_DBMOVED', 1288: 'SQLITE_READONLY_CANTINIT', 1544: 'SQLITE_READONLY_DIRECTORY',
  270: 'SQLITE_CANTOPEN_NOTEMPDIR', 526: 'SQLITE_CANTOPEN_ISDIR', 782: 'SQLITE_CANTOPEN_FULLPATH',
  1038: 'SQLITE_CANTOPEN_CONVPATH', 1294: 'SQLITE_CANTOPEN_DIRTYWAL', 1550: 'SQLITE_CANTOPEN_SYMLINK',
  275: 'SQLITE_CONSTRAINT_CHECK', 531: 'SQLITE_CONSTRAINT_COMMITHOOK', 787: 'SQLITE_CONSTRAINT_FOREIGNKEY',
  1043: 'SQLITE_CONSTRAINT_FUNCTION', 1299: 'SQLITE_CONSTRAINT_NOTNULL', 1555: 'SQLITE_CONSTRAINT_PRIMARYKEY',
  1811: 'SQLITE_CONSTRAINT_TRIGGER', 2067: 'SQLITE_CONSTRAINT_UNIQUE', 2323: 'SQLITE_CONSTRAINT_VTAB',
  2579: 'SQLITE_CONSTRAINT_ROWID', 2835: 'SQLITE_CONSTRAINT_PINNED', 3091: 'SQLITE_CONSTRAINT_DATATYPE',
};

export const constants = {
  SQLITE_OPEN_READONLY: 1,
  SQLITE_OPEN_READWRITE: 2,
  SQLITE_OPEN_CREATE: 4,
  SQLITE_OPEN_DELETEONCLOSE: 8,
  SQLITE_OPEN_EXCLUSIVE: 16,
  SQLITE_OPEN_AUTOPROXY: 32,
  SQLITE_OPEN_URI: 64,
  SQLITE_OPEN_MEMORY: 128,
  SQLITE_OPEN_MAIN_DB: 256,
  SQLITE_OPEN_TEMP_DB: 512,
  SQLITE_OPEN_TRANSIENT_DB: 1024,
  SQLITE_OPEN_MAIN_JOURNAL: 2048,
  SQLITE_OPEN_TEMP_JOURNAL: 4096,
  SQLITE_OPEN_SUBJOURNAL: 8192,
  SQLITE_OPEN_SUPER_JOURNAL: 16384,
  SQLITE_OPEN_NOMUTEX: 32768,
  SQLITE_OPEN_FULLMUTEX: 65536,
  SQLITE_OPEN_SHAREDCACHE: 131072,
  SQLITE_OPEN_PRIVATECACHE: 262144,
  SQLITE_OPEN_WAL: 524288,
  SQLITE_OPEN_NOFOLLOW: 16777216,
  SQLITE_OPEN_EXRESCODE: 33554432,
  SQLITE_PREPARE_PERSISTENT: 1,
  SQLITE_PREPARE_NORMALIZE: 2,
  SQLITE_PREPARE_NO_VTAB: 4,
  SQLITE_DESERIALIZE_READONLY: 4,
  SQLITE_FCNTL_LOCKSTATE: 1,
  SQLITE_FCNTL_GET_LOCKPROXYFILE: 2,
  SQLITE_FCNTL_SET_LOCKPROXYFILE: 3,
  SQLITE_FCNTL_LAST_ERRNO: 4,
  SQLITE_FCNTL_SIZE_HINT: 5,
  SQLITE_FCNTL_CHUNK_SIZE: 6,
  SQLITE_FCNTL_FILE_POINTER: 7,
  SQLITE_FCNTL_SYNC_OMITTED: 8,
  SQLITE_FCNTL_WIN32_AV_RETRY: 9,
  SQLITE_FCNTL_PERSIST_WAL: 10,
  SQLITE_FCNTL_OVERWRITE: 11,
  SQLITE_FCNTL_VFSNAME: 12,
  SQLITE_FCNTL_POWERSAFE_OVERWRITE: 13,
  SQLITE_FCNTL_PRAGMA: 14,
  SQLITE_FCNTL_BUSYHANDLER: 15,
  SQLITE_FCNTL_TEMPFILENAME: 16,
  SQLITE_FCNTL_MMAP_SIZE: 18,
  SQLITE_FCNTL_TRACE: 19,
  SQLITE_FCNTL_HAS_MOVED: 20,
  SQLITE_FCNTL_SYNC: 21,
  SQLITE_FCNTL_COMMIT_PHASETWO: 22,
  SQLITE_FCNTL_WIN32_SET_HANDLE: 23,
  SQLITE_FCNTL_WAL_BLOCK: 24,
  SQLITE_FCNTL_ZIPVFS: 25,
  SQLITE_FCNTL_RBU: 26,
  SQLITE_FCNTL_VFS_POINTER: 27,
  SQLITE_FCNTL_JOURNAL_POINTER: 28,
  SQLITE_FCNTL_WIN32_GET_HANDLE: 29,
  SQLITE_FCNTL_PDB: 30,
  SQLITE_FCNTL_BEGIN_ATOMIC_WRITE: 31,
  SQLITE_FCNTL_COMMIT_ATOMIC_WRITE: 32,
  SQLITE_FCNTL_ROLLBACK_ATOMIC_WRITE: 33,
  SQLITE_FCNTL_LOCK_TIMEOUT: 34,
  SQLITE_FCNTL_DATA_VERSION: 35,
  SQLITE_FCNTL_SIZE_LIMIT: 36,
  SQLITE_FCNTL_CKPT_DONE: 37,
  SQLITE_FCNTL_RESERVE_BYTES: 38,
  SQLITE_FCNTL_CKPT_START: 39,
  SQLITE_FCNTL_EXTERNAL_READER: 40,
  SQLITE_FCNTL_CKSM_FILE: 41,
  SQLITE_FCNTL_RESET_CACHE: 42,
};

export class SQLiteError extends Error {
  constructor(message, options) {
    super(message);
    this.name = 'SQLiteError';
    this.code = options?.code ?? CODES[options?.errno] ?? CODES[(options?.errno ?? 1) & 255] ?? 'SQLITE_ERROR';
    this.errno = options?.errno ?? 1;
    this.byteOffset = -1;
  }
}

/* the SQLiteError for a SQLite3Error thrown by db, other errors as they are */
function convert(error, db) {
  if(!(error instanceof SQLite3Error)) return error;

  return new SQLiteError(error.message, { errno: error.errno ?? db.extendedErrorCode });
}

function guard(db, fn) {
  try {
    return fn();
  } catch(error) {
    throw convert(error, db);
  }
}

/* an object that names parameters, as opposed to a value to bind */
const isNamed = v => v !== null && typeof v == 'object' && !Array.isArray(v) && !ArrayBuffer.isView(v) && !(v instanceof ArrayBuffer) && !(v instanceof Date);

/* what a Database shares with its statements, never visible to a script:
   { db, closed, strict, flags, depth, cache } */
const check = state => {
  if(state.closed) throw new Error('Database has closed');
};

/* the { changes, lastInsertRowid } of a statement run since before, the
   total changes at its start */
function outcome({ db, flags }, before) {
  const id = db.lastInsertRowid;

  return { changes: db.totalChanges - before, lastInsertRowid: flags & RESULT_BIGINT ? BigInt(id) : id };
}

export class Statement {
  #state;
  #db;
  #result;
  #sql;
  #flags;
  #class = null;

  constructor(state, result, sql) {
    this.#state = state;
    this.#db = state.db;
    this.#result = result;
    this.#sql = sql;
    this.#flags = state.flags;
  }

  get isFinalized() {
    return this.#result === null;
  }

  get safeIntegers() {
    return !!(this.#flags & RESULT_BIGINT);
  }

  set safeIntegers(value) {
    this.#flags = value ? this.#flags | RESULT_BIGINT : this.#flags & ~RESULT_BIGINT;
  }

  get columnNames() {
    return this.#live().fetchFields().map(f => f[0]);
  }

  get declaredTypes() {
    return this.#live().fetchFields().map(f => f[1]);
  }

  get paramsCount() {
    return this.#live().paramCount;
  }

  #live() {
    if(this.#result === null) throw new Error('Statement has finalized');
    check(this.#state);

    return this.#result;
  }

  /* reset, then bind the arguments: a list of values, one array of them or
     one object of named values */
  #bind(args, type) {
    const result = this.#live();
    const strict = this.#state.strict;
    const values = args.length == 1 && (Array.isArray(args[0]) || isNamed(args[0])) ? args[0] : args;

    if(Array.isArray(values) && values.length != 0 && values.length != result.paramCount)
      throw new Error(`SQLite query expected ${result.paramCount} values, received ${values.length}`);

    result.resultType = this.#flags | type;
    guard(this.#db, () => result.bind(values, strict));

    return result;
  }

  #row(row) {
    if(row !== null && this.#class !== null) Object.setPrototypeOf(row, this.#class.prototype);

    return row;
  }

  /* reset the statement when fn is done or throws */
  #run(args, type, fn) {
    const result = this.#bind(args, type);

    try {
      return guard(this.#db, () => fn(result));
    } finally {
      result.reset();
    }
  }

  get(...args) {
    return this.#run(args, RESULT_OBJECT | RESULT_PLAIN, r => this.#row(r.fetchRow()));
  }

  all(...args) {
    return this.#run(args, RESULT_OBJECT | RESULT_PLAIN, r => {
      const rows = [];

      for(let row; (row = r.fetchRow()) !== null; ) rows.push(this.#row(row));

      return rows;
    });
  }

  values(...args) {
    return this.#run(args, 0, r => {
      const rows = [];

      for(let row; (row = r.fetchRow()) !== null; ) rows.push(row);

      return rows;
    });
  }

  run(...args) {
    const before = this.#db.totalChanges;

    this.#run(args, 0, r => {
      while(r.fetchRow() !== null);
    });

    return outcome(this.#state, before);
  }

  *iterate(...args) {
    const result = this.#bind(args, RESULT_OBJECT | RESULT_PLAIN);

    try {
      for(let row; (row = guard(this.#db, () => result.fetchRow())) !== null; ) yield this.#row(row);
    } finally {
      result.reset();
    }
  }

  [Symbol.iterator]() {
    return this.iterate();
  }

  as(Class) {
    this.#class = Class ?? null;

    return this;
  }

  finalize() {
    if(this.#result === null) return;

    this.#result.finalize();
    this.#result = null;
  }

  toJSON() {
    return { sql: this.#sql, isFinalized: this.isFinalized, paramsCount: this.isFinalized ? 0 : this.paramsCount, columnNames: this.isFinalized ? [] : this.columnNames };
  }

  toString() {
    return this.#sql;
  }
}

const STATES = new WeakMap();

export class Database {
  static MAX_QUERY_CACHE_SIZE = 20;

  static open(filename, options) {
    return new Database(filename, options);
  }

  constructor(filename = ':memory:', options) {
    let flags = OPEN_READWRITE | OPEN_CREATE;

    if(typeof options == 'number') flags = options;
    else if(options) flags = (options.readonly ? OPEN_READONLY : OPEN_READWRITE) | (options.create === false ? 0 : OPEN_CREATE);

    STATES.set(this, {
      db: guard(null, () => new SQLite3(String(filename), flags)),
      closed: false,
      strict: !!options?.strict,
      flags: RESULT_BYTES | (options?.safeIntegers ? RESULT_BIGINT : 0),
      cache: new Map(),
      depth: 0,
    });

    this.filename = String(filename);
  }

  get inTransaction() {
    const state = STATES.get(this);

    check(state);
    return state.db.inTransaction;
  }

  #statement(sql) {
    const state = STATES.get(this);

    check(state);
    sql = String(sql);

    if(sql.trim() === '') throw new Error('SQL query cannot be empty.');

    const result = guard(state.db, () => state.db.prepare(sql));

    if(result === null) throw new RangeError('Invalid SQL statement');

    return new Statement(state, result, sql);
  }

  query(sql) {
    const { cache, closed } = STATES.get(this);

    if(closed) throw new RangeError('Cannot use a closed database');

    sql = String(sql);

    let statement = cache.get(sql);

    if(statement === undefined || statement.isFinalized) {
      statement = this.#statement(sql);

      if(cache.size >= Database.MAX_QUERY_CACHE_SIZE) this.clearQueryCache();

      cache.set(sql, statement);
    }

    return statement;
  }

  prepare(sql) {
    return this.#statement(sql);
  }

  run(sql, ...args) {
    const state = STATES.get(this);

    check(state);
    sql = String(sql);

    if(sql.trim() === '') throw new Error("SQL string mustn't be blank");

    const before = state.db.totalChanges;

    /* no values: every statement of sql runs, otherwise just the first */
    if(args.length == 0) {
      guard(state.db, () => state.db.exec(sql));
    } else {
      const statement = this.#statement(sql);

      try {
        statement.run(...args);
      } finally {
        statement.finalize();
      }
    }

    return outcome(state, before);
  }

  exec(sql, ...args) {
    return this.run(sql, ...args);
  }

  transaction(fn) {
    const state = STATES.get(this);

    const make = mode => (...args) => {
      const outer = !this.inTransaction;
      const savepoint = `bun_tx_${state.depth + 1}`;

      this.run(outer ? `BEGIN ${mode}` : `SAVEPOINT ${savepoint}`);
      state.depth++;

      try {
        const result = fn.apply(this, args);

        this.run(outer ? 'COMMIT' : `RELEASE ${savepoint}`);
        return result;
      } catch(error) {
        if(outer) {
          this.run('ROLLBACK');
        } else {
          this.run(`ROLLBACK TO ${savepoint}`);
          this.run(`RELEASE ${savepoint}`);
        }

        throw error;
      } finally {
        state.depth--;
      }
    };

    const tx = make('DEFERRED');

    tx.deferred = tx;
    tx.immediate = make('IMMEDIATE');
    tx.exclusive = make('EXCLUSIVE');

    return tx;
  }

  clearQueryCache() {
    const { cache } = STATES.get(this);

    for(const statement of cache.values()) statement.finalize();

    cache.clear();
  }

  close() {
    const state = STATES.get(this);

    if(state.closed) return;

    this.clearQueryCache();
    state.db.close();
    state.closed = true;
  }
}

export default Database;
