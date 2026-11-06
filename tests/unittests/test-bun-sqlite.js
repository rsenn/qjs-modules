/* bun:sqlite of lib/bun/sqlite.js; expected values come from bun 1.4.2.
 * runs under bun as well: bun tests/unittests/test-bun-sqlite.js */
import Database, { Database as Named, SQLiteError, Statement, constants } from 'bun:sqlite';
import { assert, eq, tests } from '../../lib/tinytest.js';

const json = v => JSON.stringify(v, (k, x) => (typeof x == 'bigint' ? x + 'n' : x));

function fails(fn) {
  try {
    fn();
  } catch(e) {
    return e;
  }

  return null;
}

function open(options) {
  const db = new Database(':memory:', options);

  db.run('create table t(id integer primary key, name text, b blob, x real)');
  return db;
}

tests({
  'exports Database as the default, Statement, SQLiteError, constants'() {
    assert(Database === Named, 'default is Database');
    eq('function', typeof Statement);
    eq('function', typeof SQLiteError);
    eq('1,2,4', [constants.SQLITE_OPEN_READONLY, constants.SQLITE_OPEN_READWRITE, constants.SQLITE_OPEN_CREATE].join());
  },

  'run returns changes and lastInsertRowid'() {
    const db = open();

    eq('{"changes":1,"lastInsertRowid":1}', json(db.run('insert into t(name, x) values (?, ?)', ['a', 1.5])));
    eq('{"changes":2,"lastInsertRowid":3}', json(db.run('insert into t(name) values (?), (?)', 'b', 'c')));
    eq('{"changes":3,"lastInsertRowid":3}', json(db.run('update t set x = 2')));
    eq('{"changes":0,"lastInsertRowid":3}', json(db.run('select 1')));
    db.close();
  },

  'run without values executes every statement'() {
    const db = open();

    eq('{"changes":1,"lastInsertRowid":1}', json(db.run('create table u(a); insert into u values (1)')));
    eq('[{"a":1}]', json(db.query('select * from u').all()));
    db.close();
  },

  'get, all and values return objects, arrays, null and empty lists'() {
    const db = open();

    db.run('insert into t(name, x) values (?, ?)', ['a', 1.5]);

    const q = db.query('select id, name, b, x from t where id = ?');

    eq('{"id":1,"name":"a","b":null,"x":1.5}', json(q.get(1)));
    eq('[{"id":1,"name":"a","b":null,"x":1.5}]', json(q.all(1)));
    eq('[[1,"a",null,1.5]]', json(q.values(1)));
    eq('null', json(q.get(2)));
    eq('[]', json(q.all(2)));
    assert(Object.getPrototypeOf(q.get(1)) === Object.prototype, 'plain object');
    db.close();
  },

  'a blob goes in from a view and comes out as a Uint8Array'() {
    const db = open();

    db.run('insert into t(b) values (?)', [new Uint8Array([1, 2])]);

    const { b } = db.query('select b from t').get();

    assert(b instanceof Uint8Array, 'Uint8Array');
    eq('1,2', b.join());
    eq(0, db.query("select x'' b").get().b.length);
    db.close();
  },

  'values bind as Bun binds them'() {
    const db = open();
    const one = v => db.query('select ? v').get(v).v;

    eq('[[1,null,null,10]]', json(db.query('select ?, ?, ?, ?').values(true, null, undefined, 10n)));
    eq('null', json(one(NaN)));
    eq('null', json(one(Infinity)));
    eq(0, one(-0));
    eq(2 ** 53, one(2 ** 53));
    eq(1.5, one(1.5));
    eq('é€😀', one('é€😀'));
    eq('null', json(one(new Date(0))));
    eq('{"?1":7,"?2":8}', json(db.query('select ?1, ?2').get(7, 8)));
    eq('integer', db.query('select typeof(?) t').get(2.0).t);
    db.close();
  },

  'named parameters need their prefix unless strict'() {
    const db = open();
    const q = db.query('select $a, :b, @c');

    eq('{"$a":1,":b":2,"@c":3}', json(q.get({ $a: 1, ':b': 2, '@c': 3 })));
    eq('{"$a":null,":b":null,"@c":null}', json(q.get({ a: 1, b: 2, c: 3 })));
    eq('{"$a":5}', json(db.query('select $a').get([5])));
    db.close();

    const strict = open({ strict: true });

    eq('{"$a":1}', json(strict.query('select $a').get({ a: 1 })));
    eq('Missing parameter "a"', fails(() => strict.query('select $a').get({ b: 1 })).message);
    strict.close();
  },

  'a wrong number of values throws'() {
    const db = open();

    eq('SQLite query expected 2 values, received 1', fails(() => db.query('select ?, ?').get(1)).message);
    eq('SQLite query expected 2 values, received 1', fails(() => db.query('select ?, ?').get([1])).message);
    eq('SQLite query expected 1 values, received 2', fails(() => db.query('select ?').get(1, 2)).message);
    eq('{"?":null}', json(db.query('select ?').get()));
    db.close();
  },

  'safeIntegers returns bigint for integers'() {
    const db = open({ safeIntegers: true });

    eq('{"a":"1n"}', json(db.query('select 1 a').get()));
    eq('{"?":"5n"}', json(db.query('select ?').get(5)));
    eq('{"x":1.5}', json(db.query('select 1.5 x').get()));
    db.close();
  },

  'as() makes the rows instances of a class'() {
    const db = open();
    class C {
      get double() {
        return this.a * 2;
      }
    }
    const q = db.query('select 1 a, 2 b').as(C);

    assert(q.get() instanceof C, 'get');
    assert(q.all()[0] instanceof C, 'all');
    eq(2, q.get().double);
    eq('a,b', Object.keys(q.get()).join());
    db.close();
  },

  'iterate yields the rows and a statement is iterable'() {
    const db = open();

    db.run('insert into t(name) values (?), (?)', 'a', 'b');

    const q = db.query('select name from t order by id');
    const it = q.iterate();

    eq('{"value":{"name":"a"},"done":false}', json(it.next()));
    eq('b', [...q].map(r => r.name).pop());
    eq('a,b', [...q.iterate()].map(r => r.name).join());
    db.close();
  },

  'column names, declared types and the parameter count'() {
    const db = open();
    const q = db.query('select id, name, b, x from t where id = ? and name = ?');

    q.all(1, 'a');
    eq('id,name,b,x', q.columnNames.join());
    eq('INTEGER,TEXT,BLOB,REAL', q.declaredTypes.join());
    eq(2, q.paramsCount);
    eq('{"1":1,"2":2}', json(db.query('select 1, 2').get()));
    eq('{"":1,"x y":2}', json(db.query('select 1 "", 2 "x y"').get()));
    eq('[{"a":2}]', json(db.query('select 1 a, 2 a').all()));
    db.close();
  },

  'a finalized statement throws'() {
    const db = open();
    const q = db.query('select 1');

    assert(!q.isFinalized, 'live');
    q.finalize();
    assert(q.isFinalized, 'finalized');
    eq('Statement has finalized', fails(() => q.get()).message);
    eq('Statement has finalized', fails(() => q.columnNames).message);
    q.finalize();
    db.close();
  },

  'query caches by sql and prepare does not'() {
    const db = open();

    assert(db.query('select 1') === db.query('select 1'), 'cached');
    assert(db.prepare('select 1') !== db.prepare('select 1'), 'not cached');
    assert(db.query('select 1') !== db.query('select 2'), 'by sql');
    db.clearQueryCache();
    db.close();
  },

  'transaction commits, rolls back and nests'() {
    const db = open();
    const count = name => db.query('select count(*) c from t where name = ?').get(name).c;
    const add = name => db.run('insert into t(name) values (?)', [name]);
    const tx = db.transaction((a, b) => {
      add(a);
      add(b);
      assert(db.inTransaction, 'inside');
      return a + b;
    });

    eq('xy', tx('x', 'y'));
    eq(1, count('x'));
    assert(!db.inTransaction, 'outside');
    eq('xy', tx.immediate('x', 'y'));
    eq('xy', tx.exclusive('x', 'y'));
    eq('xy', tx.deferred('x', 'y'));
    eq(4, count('x'));
    eq('boom', fails(() => db.transaction(() => (add('r'), (() => { throw new Error('boom'); })()))()).message);
    eq(0, count('r'));
    assert(!db.inTransaction, 'rolled back');
    eq('inner', fails(() => db.transaction(() => db.transaction(() => (add('n'), (() => { throw new Error('inner'); })()))())()).message);
    eq(0, count('n'));
    db.transaction(() => {
      add('k');
      fails(() => db.transaction(() => (add('l'), (() => { throw new Error('x'); })()))());
    })();
    eq('1,0', [count('k'), count('l')].join());
    db.close();
  },

  'errors are SQLiteError with code and errno'() {
    const db = open();

    db.run('insert into t(id) values (1)');

    for(const [fn, code, errno, message] of [
      [() => db.query('select * from nope'), 'SQLITE_ERROR', 1, 'no such table: nope'],
      [() => db.run('insert into t(id) values (1)'), 'SQLITE_CONSTRAINT_PRIMARYKEY', 1555, 'UNIQUE constraint failed: t.id'],
      [() => db.query('selec 1'), 'SQLITE_ERROR', 1, 'near "selec": syntax error'],
      [() => db.run('create table t(a)'), 'SQLITE_ERROR', 1, 'table t already exists'],
      [() => new Database('/nonexistent/x.db'), 'SQLITE_CANTOPEN', 14, 'unable to open database file'],
    ]) {
      const e = fails(fn);

      assert(e instanceof SQLiteError && e instanceof Error, message);
      eq(`SQLiteError ${code} ${errno} ${message}`, [e.name, e.code, e.errno, e.message].join(' '));
    }

    db.close();
  },

  'empty sql throws'() {
    const db = open();

    eq("SQL string mustn't be blank", fails(() => db.run('')).message);
    eq('SQL query cannot be empty.', fails(() => db.query('')).message);
    eq('Invalid SQL statement', fails(() => db.query('-- c')).message);
    db.close();
  },

  'a closed database throws, closing twice does not'() {
    const db = open();

    db.close();
    db.close();
    eq('RangeError Cannot use a closed database', fails(() => db.query('select 1')).name + ' ' + fails(() => db.query('select 1')).message);
    eq('Database has closed', fails(() => db.run('select 1')).message);
    eq('Database has closed', fails(() => db.inTransaction).message);
  },

  'filename, open() and the read-only flag'() {
    eq(':memory:', new Database().filename);
    eq(':memory:', Database.open(':memory:').filename);
    eq('Database', Database.open(':memory:').constructor.name);
    eq('SQLiteError', fails(() => new Database('/nonexistent/x.db', { readonly: true })).name);
  },
});
