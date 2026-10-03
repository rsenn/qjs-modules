/* lib/dbi.js: driver-agnostic Database, exercised on sqlite :memory:.
 * Set DBI_MYSQL / DBI_PGSQL to "host,user,password,database" to also run
 * the same suite against a live server; both are skipped otherwise. */
import { Database } from '../../lib/dbi.js';
import { getenv } from 'std';
import { assert, eq, tests } from '../../lib/tinytest.js';

const ID = { sqlite: 'INTEGER PRIMARY KEY AUTOINCREMENT', mysql: 'INTEGER NOT NULL AUTO_INCREMENT PRIMARY KEY', pgsql: 'SERIAL PRIMARY KEY' };
const TEXT = { sqlite: 'TEXT', mysql: 'VARCHAR(255)', pgsql: 'VARCHAR(255)' };

function target(driver) {
  if(driver == 'sqlite') return { filename: ':memory:' };

  const [host, user, password, database] = (getenv(driver == 'mysql' ? 'DBI_MYSQL' : 'DBI_PGSQL') ?? '').split(',');

  return host ? { host, user, password, database } : null;
}

const rows = result => result.all();

async function rejection(promise) {
  try {
    await promise;
  } catch(e) {
    return e;
  }
}

for(const driver of ['sqlite', 'mysql', 'pgsql']) {
  const options = target(driver);

  if(!options) {
    console.log(`SKIP: ${driver} (no DBI_${driver.toUpperCase()} server given)`);
    continue;
  }

  let db;

  await tests({
    async [`${driver}: connect`]() {
      db = await Database.connect(driver, options);
      eq(driver, db.driver);
      await db.exec('DROP TABLE IF EXISTS dbi_test');
      await db.exec(`CREATE TABLE dbi_test (id ${ID[driver]}, name ${TEXT[driver]} NOT NULL, score INTEGER)`);
    },

    async [`${driver}: insertQuery builds an INSERT that exec runs`]() {
      const sql = db.insertQuery('dbi_test', ['name', 'score'], ['alice', 100]);

      assert(/^INSERT INTO/.test(sql), sql);
      eq(1, await db.exec(sql));
    },

    async [`${driver}: insertId and affectedRows follow the last insert`]() {
      await db.exec(db.insertQuery('dbi_test', ['name', 'score'], ['bob', 85]));

      assert(db.insertId > 0, `insertId ${db.insertId}`);
      eq(1, db.affectedRows);
    },

    async [`${driver}: query returns rows through all()`]() {
      const result = await db.query('SELECT name, score FROM dbi_test ORDER BY score DESC');
      const all = await rows(result);

      eq(2, all.length);
      eq('alice', all[0].name);
      eq(85, all[1].score);
    },

    async [`${driver}: results report their fields`]() {
      const result = await db.query('SELECT name, score FROM dbi_test');

      eq(2, result.numFields);
      eq('name,score', result.fetchFields().map(f => f[0] ?? f.name).join());
    },

    async [`${driver}: a result is async-iterable`]() {
      const names = [];

      for await(const row of await db.query('SELECT name FROM dbi_test ORDER BY name')) names.push(row.name);

      eq('alice,bob', names.join());
    },

    async [`${driver}: exec returns the number of changed rows`]() {
      eq(1, await db.exec(`UPDATE dbi_test SET score = score + 10 WHERE name = ${db.quote('alice')}`));
      eq(110, (await rows(await db.query("SELECT score FROM dbi_test WHERE name = 'alice'")))[0].score);
    },

    async [`${driver}: quote escapes embedded quotes`]() {
      eq("'O''Brien'", db.quote("O'Brien").replace("\\'", "''"));

      const row = (await rows(await db.query(`SELECT ${db.quote("O'Brien")} AS literal`)))[0];

      eq("O'Brien", row.literal);
    },

    async [`${driver}: delete`]() {
      eq(1, await db.exec('DELETE FROM dbi_test WHERE score < 100'));
      eq(1, Number((await rows(await db.query('SELECT COUNT(*) AS n FROM dbi_test')))[0].n));
    },

    async [`${driver}: close`]() {
      await db.exec('DROP TABLE dbi_test');
      await db.close();
    },
  });
}

tests({
  async 'an unknown driver is rejected and lists the available ones'() {
    const error = await rejection(Database.connect('nonexistent', {}));

    assert(error && /unknown driver 'nonexistent'/.test(error.message), String(error));
    assert(/sqlite/.test(error.message), error.message);
  },
});
