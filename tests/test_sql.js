import { SQL, SQLError } from '../lib/sql.js';
import { assert, eq, tests } from './tinytest.js';

async function exerciseDriver(sql, label) {
  await sql`DROP TABLE IF EXISTS sql_test_users`;
  await sql`CREATE TABLE sql_test_users (id INTEGER PRIMARY KEY, name TEXT, age INTEGER)`;
  await sql`INSERT INTO sql_test_users (id, name, age) VALUES (1, ${"O'Brien"}, ${30})`;
  await sql`INSERT INTO sql_test_users (id, name, age) VALUES (2, ${'Bob'}, ${25})`;

  const rows = await sql`SELECT * FROM sql_test_users ORDER BY id`;
  eq(2, rows.length);
  eq("O'Brien", rows[0].name);
  eq(30, rows[0].age);
  eq('Bob', rows[1].name);

  const vals = await sql`SELECT id, name FROM sql_test_users ORDER BY id`.values();
  assert(Array.isArray(vals[0]), `${label}: .values() should give arrays, got ${JSON.stringify(vals)}`);
  eq('Bob', vals[1][1]);

  /* A value containing SQL-meaningful characters must be escaped, not
   * concatenated raw - this is the actual injection-safety guarantee. */
  const evil = "x'; DROP TABLE sql_test_users; --";
  await sql`INSERT INTO sql_test_users (id, name, age) VALUES (3, ${evil}, 1)`;
  const afterEvil = await sql`SELECT COUNT(*) as n FROM sql_test_users`;
  eq(3, afterEvil[0].n);

  const oneRow = await sql.unsafe('SELECT * FROM sql_test_users WHERE id = ?', [1]);
  eq(1, oneRow.length);
  eq("O'Brien", oneRow[0].name);

  let rollbackError;
  try {
    await sql.begin(async tx => {
      await tx`INSERT INTO sql_test_users (id, name, age) VALUES (4, 'Carl', 40)`;
      throw new Error('force rollback');
    });
  } catch(e) {
    rollbackError = e;
  }
  assert(rollbackError && rollbackError.message === 'force rollback', `${label}: begin() should propagate the callback's error`);
  const afterRollback = await sql`SELECT COUNT(*) as n FROM sql_test_users`;
  eq(3, afterRollback[0].n);

  await sql.begin(async tx => {
    await tx`INSERT INTO sql_test_users (id, name, age) VALUES (5, 'Dora', 22)`;
  });
  const afterCommit = await sql`SELECT COUNT(*) as n FROM sql_test_users`;
  eq(4, afterCommit[0].n);

  await sql`DROP TABLE sql_test_users`;
  await sql.close();
}

tests({
  'new SQL() parses connection strings/options and rejects bad ones'() {
    eq('sqlite', new SQL(':memory:').driver);
    eq('sqlite', new SQL('./somefile.db').driver);
    eq('sqlite', new SQL('sqlite://./somefile.db').driver);
    eq('pgsql', new SQL('postgres://user:pw@myhost:5433/mydb').driver);
    eq('pgsql', new SQL('postgresql://user:pw@myhost/mydb').driver);
    eq('mysql', new SQL('mysql://user:pw@myhost/mydb').driver);
    eq('sqlite', new SQL({ adapter: 'sqlite', filename: ':memory:' }).driver);

    let threw = null;
    try {
      new SQL('redis://localhost/0');
    } catch(e) {
      threw = e;
    }
    assert(threw instanceof SQLError, 'unsupported scheme should throw SQLError');

    threw = null;
    try {
      new SQL({ filename: ':memory:' });
    } catch(e) {
      threw = e;
    }
    assert(threw instanceof SQLError, "an options object without 'adapter' should throw SQLError");
  },

  async 'sqlite: tagged-template queries, .values(), .unsafe(), begin()/rollback/commit'() {
    const sql = new SQL(':memory:');
    await exerciseDriver(sql, 'sqlite');
  },

  async 'pgsql: same behavior as sqlite, against a live Postgres if reachable'() {
    const sql = new SQL({ adapter: 'pgsql', host: 'localhost', user: 'roman', password: 'r4eHuJ', database: 'roman', port: 5432, timeout: 10 });

    try {
      await sql._connect();
    } catch(e) {
      console.log('  (skipped: no local PostgreSQL reachable -', e.message, ')');
      return;
    }

    await exerciseDriver(sql, 'pgsql');
  },

  async 'mysql: same behavior as sqlite, against a live MySQL if reachable'() {
    const sql = new SQL({ adapter: 'mysql', host: 'localhost', user: 'roman', password: 'r4eHuJ', database: 'roman' });

    try {
      await sql._connect();
    } catch(e) {
      console.log('  (skipped: no local MySQL reachable -', e.message, ')');
      return;
    }

    await exerciseDriver(sql, 'mysql');
  },
});
