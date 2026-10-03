/* native sqlite module (libsqlite3 binding), in-memory databases only */
import { SQLite3, SQLite3Error, SQLite3Result } from 'sqlite';
import { assert, eq, tests } from '../../lib/tinytest.js';

function open() {
  const db = new SQLite3(':memory:');

  db.exec('CREATE TABLE t (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT UNIQUE NOT NULL, n REAL)');
  return db;
}

function throws(fn) {
  try {
    fn();
  } catch(e) {
    return e;
  }
}

tests({
  'exports the three classes'() {
    eq('function', typeof SQLite3);
    eq('function', typeof SQLite3Result);
    eq('function', typeof SQLite3Error);
  },

  'a database opens in memory, which has no file name'() {
    const db = new SQLite3(':memory:');

    eq(null, db.filename);
    db.close();
  },

  'open() after construction without a file'() {
    const db = new SQLite3();

    eq(true, db.open(':memory:'));
    db.close();
  },

  'exec reports affected rows, insert id and total changes'() {
    const db = open();

    eq(2, db.exec("INSERT INTO t (name, n) VALUES ('a', 1.5), ('b', 2)"));
    eq(2, db.affectedRows);
    eq(2, db.changes);
    eq(2, db.insertId);
    eq(2, db.lastInsertRowid);
    eq(2, db.totalChanges);
    db.close();
  },

  'query returns a SQLite3Result with typed columns'() {
    const db = open();

    db.exec("INSERT INTO t (name, n) VALUES ('a', 1.5)");

    const result = db.query('SELECT id, name, n FROM t');

    assert(result instanceof SQLite3Result, 'instance');
    eq(3, result.numFields);
    eq('id,INTEGER;name,TEXT;n,REAL', result.fetchFields().map(f => f.join()).join(';'));
    db.close();
  },

  'fetchRow returns arrays then null at the end'() {
    const db = open();

    db.exec("INSERT INTO t (name, n) VALUES ('a', 1.5), ('b', 2)");

    const result = db.query('SELECT id, name, n FROM t ORDER BY id');

    eq(false, result.eof);
    eq('1,a,1.5', result.fetchRow().join());
    eq('2,b,2', result.fetchRow().join());
    eq(null, result.fetchRow());
    eq(true, result.eof);
    db.close();
  },

  'fetchAssoc returns objects keyed by column'() {
    const db = open();

    db.exec("INSERT INTO t (name, n) VALUES ('a', 1.5)");

    const row = db.query('SELECT id, name, n FROM t').fetchAssoc();

    eq(1, row.id);
    eq('a', row.name);
    eq(1.5, row.n);
    db.close();
  },

  'a result is iterable'() {
    const db = open();

    db.exec("INSERT INTO t (name) VALUES ('b'), ('a')");
    eq('a,b', [...db.query('SELECT name FROM t ORDER BY name')].map(r => r[0]).join());
    db.close();
  },

  'reset() rewinds the result'() {
    const db = open();

    db.exec("INSERT INTO t (name) VALUES ('a')");

    const result = db.query('SELECT name FROM t');

    result.fetchRow();
    result.reset();
    eq('a', result.fetchRow()[0]);
    db.close();
  },

  'NULL, integers, reals and text map to JS values'() {
    const db = open();
    const row = db.query("SELECT 1 AS one, NULL AS nul, 2.5 AS f, 't' AS s").fetchAssoc();

    eq(1, row.one);
    eq(null, row.nul);
    eq(2.5, row.f);
    eq('t', row.s);
    db.close();
  },

  'escapeString doubles single quotes'() {
    eq("a''b", SQLite3.escapeString("a'b"));
    eq("'a''b'", SQLite3.quoteString("a'b"));
  },

  'valueString renders JS values as SQL'() {
    eq('5', SQLite3.valueString(5));
    eq("'x'", SQLite3.valueString('x'));
    eq('NULL', SQLite3.valueString(null));
  },

  'valuesString renders a list'() {
    eq("(1, 'a', NULL)", open().valuesString([1, 'a', null]));
  },

  'quoted values survive a round trip'() {
    const db = open();
    const name = "O'Brien \"quoted\"";

    db.exec(`INSERT INTO t (name) VALUES (${SQLite3.quoteString(name)})`);
    eq(name, db.query('SELECT name FROM t').fetchRow()[0]);
    db.close();
  },

  'a constraint violation throws SQLite3Error with the code'() {
    const db = open();

    db.exec("INSERT INTO t (name) VALUES ('a')");

    const error = throws(() => db.exec("INSERT INTO t (name) VALUES ('a')"));

    assert(error instanceof SQLite3Error, String(error));
    eq('SQLite3Error', error.name);
    assert(/UNIQUE constraint failed: t\.name/.test(error.message), error.message);
    eq(19, db.errorCode);
    db.close();
  },

  'a syntax error throws'() {
    const db = open();
    const error = throws(() => db.query('SELEC'));

    assert(error instanceof SQLite3Error && /syntax error/.test(error.message), String(error));
    db.close();
  },

  'a query on a closed database throws'() {
    const db = open();

    db.close();
    assert(throws(() => db.query('SELECT 1')) instanceof SQLite3Error, 'closed');
  },

  'column type and open-flag constants'() {
    eq(1, SQLite3.INTEGER);
    eq(2, SQLite3.FLOAT);
    eq(3, SQLite3.TEXT);
    eq(4, SQLite3.BLOB);
    eq(5, SQLite3.NULL);
    eq(1, SQLite3.OPEN_READONLY);
    eq(128, SQLite3.OPEN_MEMORY);
  },
});
