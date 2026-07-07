/**
 * test_dbi.js -- driver-agnostic exercise of lib/dbi.js.
 *
 *   qjsm tests/test_dbi.js [driver] [args...]
 *
 *     qjsm tests/test_dbi.js sqlite [filename]
 *     qjsm tests/test_dbi.js mysql  [host] [user] [password] [database]
 *     qjsm tests/test_dbi.js pgsql  [host] [user] [password] [database] [port]
 *
 * Defaults to sqlite :memory: (works without external dependencies).
 */

import { Console } from 'console';
import { Database } from '../lib/dbi.js';
import { exit } from 'std';

/* Dialect-specific schema fragments (kept minimal — only what differs). */
const DIALECTS = {
  sqlite: {
    idColumn: 'INTEGER PRIMARY KEY AUTOINCREMENT',
    textColumn: 'TEXT',
    dropIfExists: 'DROP TABLE IF EXISTS',
  },
  mysql: {
    idColumn: 'INTEGER NOT NULL AUTO_INCREMENT PRIMARY KEY',
    textColumn: 'VARCHAR(255)',
    dropIfExists: 'DROP TABLE IF EXISTS',
  },
  pgsql: {
    idColumn: 'SERIAL PRIMARY KEY',
    textColumn: 'VARCHAR(255)',
    dropIfExists: 'DROP TABLE IF EXISTS',
  },
};

function optionsFor(driver, args) {
  switch(driver) {
    case 'sqlite': return { filename: args[0] ?? ':memory:' };
    case 'mysql': return { host: args[0] ?? 'localhost', user: args[1] ?? 'roman', password: args[2] ?? '', database: args[3] ?? 'test' };
    case 'pgsql': return { host: args[0] ?? 'localhost', user: args[1] ?? 'roman', password: args[2] ?? '', database: args[3] ?? 'roman', port: +(args[4] ?? 5432) };
    default: throw new Error(`unknown driver: ${driver}`);
  }
}

async function main(driver = 'sqlite', ...args) {
  globalThis.console = new Console({ inspectOptions: { compact: 1, customInspect: true, hideKeys: ['handle'] } });

  const dialect = DIALECTS[driver];
  if(!dialect) throw new Error(`no dialect defined for driver: ${driver}`);

  const options = optionsFor(driver, args);
  console.log(`\x1b[36m[dbi]\x1b[0m connecting to ${driver}`, console.config({ compact: true }), options);

  const db = await Database.connect(driver, options);
  console.log(`\x1b[36m[dbi]\x1b[0m connected (driver=${db.driver})`);

  const table = 'dbi_test_users';

  /* 1. clean slate */
  try {
    await db.exec(`${dialect.dropIfExists} ${table};`);
  } catch(e) {
    /* some drivers complain if table doesn't exist; ignore */
  }

  /* 2. create */
  await db.exec(`CREATE TABLE ${table} (
    id ${dialect.idColumn},
    name ${dialect.textColumn} NOT NULL,
    email ${dialect.textColumn},
    score INTEGER
  );`);
  console.log('[dbi] table created');

  /* 3. insert via insertQuery helper */
  const samples = [
    { name: 'alice', email: 'alice@example.com', score: 100 },
    { name: 'bob', email: 'bob@example.com', score: 85 },
    { name: 'carol', email: null, score: 72 },
  ];

  const insertedIds = [];
  for(const row of samples) {
    const sql = db.insertQuery(table, ['name', 'email', 'score'], [row.name, row.email, row.score]);
    console.log(`[dbi] \x1b[33m${sql}\x1b[0m`);
    await db.exec(sql);
    insertedIds.push(db.insertId);
  }

  console.log('[dbi] insertIds =', insertedIds, 'affectedRows =', db.affectedRows);

  /* 4. select all */
  console.log('[dbi] SELECT all rows:');
  const result = await db.query(`SELECT id, name, email, score FROM ${table} ORDER BY id;`);
  console.log('  numFields =', result.numFields, 'fields =', console.config({ compact: true }), result.fetchFields());

  let count = 0;
  for await(const row of result) {
    console.log(`  row[${count++}] =`, console.config({ compact: true }), row);
  }
  console.log('[dbi] row count =', count);

  /* 5. update */
  const updateSql = `UPDATE ${table} SET score = score + 10 WHERE name = ${db.quote('alice')};`;
  console.log(`[dbi] \x1b[33m${updateSql}\x1b[0m`);
  const changed = await db.exec(updateSql);
  console.log('[dbi] updated rows =', changed);

  /* 6. select via .all() */
  const updatedRows = await (await db.query(`SELECT name, score FROM ${table} ORDER BY score DESC;`)).all();
  console.log('[dbi] sorted by score:', updatedRows);

  /* 7. parameterized via db.quote */
  const search = `O'Brien`; /* tests escaping */
  const safe = db.quote(search);
  console.log(`[dbi] quote(${JSON.stringify(search)}) =`, safe);
  const safeQuery = `SELECT ${safe} AS literal;`;
  for await(const row of await db.query(safeQuery)) console.log('  literal row =', console.config({ compact: true }), row);

  /* 8. delete */
  const deleted = await db.exec(`DELETE FROM ${table} WHERE score < 80;`);
  console.log('[dbi] deleted rows =', deleted);

  /* 9. final count */
  const remaining = await (await db.query(`SELECT COUNT(*) AS n FROM ${table};`)).all();
  console.log('[dbi] remaining:', remaining);

  /* 10. drop & close */
  await db.exec(`DROP TABLE ${table};`);
  await db.close();
  console.log('[dbi] closed');
}

try {
  main(...scriptArgs.slice(1)).catch(err => {
    console.log(`FAIL: ${err.message}\n${err.stack}`);
    exit(1);
  });
} catch(error) {
  console.log(`FAIL: ${error.message}\n${error.stack}`);
  exit(1);
} finally {
  console.log('SUCCESS');
}
