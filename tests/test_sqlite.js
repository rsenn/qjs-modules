import { abbreviate, randStr } from 'util';
import extendArray from '../lib/extendArray.js';
import { Console } from 'console';
import { SQLite3 } from 'sqlite';
import { SQLite3Result } from 'sqlite';
import { exit } from 'std';
extendArray();

async function main(...args) {
  globalThis.console = new Console({
    inspectOptions: {
      compact: 1,
      customInspect: true,
      showHidden: false,
      hideKeys: ['handle'],
      protoChain: true,
    },
  });

  Object.assign(globalThis, { SQLite3, SQLite3Result });

  let i, db, q, result, resultNum;

  const filename = args[0] ?? ':memory:';

  db = globalThis.db = new SQLite3(filename);

  console.log('db.filename =', db.filename);

  resultNum = 0;

  result = r => {
    let prop = 'result' + ++resultNum;
    globalThis[prop] = r;
    console.log(prop + ' =', console.config({ compact: true }), r);
    return r;
  };

  q = globalThis.q = s => (console.log(`q('\x1b[0;32m${abbreviate(s, 1000)}'\x1b[0m)`), result(db.query(s)));

  i = 0;

  q(`CREATE TABLE IF NOT EXISTS users (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT UNIQUE NOT NULL,
    password TEXT NOT NULL,
    email TEXT UNIQUE,
    properties TEXT DEFAULT '{}',
    bindata BLOB,
    last_seen TEXT DEFAULT CURRENT_TIMESTAMP
  );`);

  q(`CREATE TABLE IF NOT EXISTS sessions (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id INTEGER NOT NULL,
    uuid TEXT UNIQUE NOT NULL,
    variables TEXT DEFAULT '{}',
    created TEXT DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (user_id) REFERENCES users(id)
      ON UPDATE CASCADE
      ON DELETE RESTRICT
  );`);

  q(db.insertQuery('users', ['name', 'password', 'email'], [randStr(32), randStr(32), randStr(64)]));

  let userId = db.insertId;
  console.log('inserted user id =', userId);

  q(db.insertQuery('sessions', ['user_id', 'uuid', 'variables'], [userId, [8, 4, 4, 4, 12].map(n => randStr(n, '0123456789abcdef')).join('-'), { blah: 1234 }]));

  console.log('db.affectedRows =', db.affectedRows);
  console.log('db.insertId =', db.insertId);

  let res = q(`SELECT id, name, email FROM users;`);

  for(let row of res) result(row);

  res = q(`SELECT * FROM users LIMIT 0, 10;`);
  console.log('numFields =', res.numFields);
  console.log('fields =', res.fetchFields());

  for(let row of res) result(row);

  res = q(`SELECT id, name FROM users;`);
  res.resultType = SQLite3.RESULT_OBJECT;

  for(let row of res) result(row);

  console.log('totalChanges =', db.totalChanges);

  db.close();
}

try {
  main(...scriptArgs.slice(1)).catch(err => console.log(`FAIL: ${err.message}\n${err.stack}`));
} catch(error) {
  console.log(`FAIL: ${error.message}\n${error.stack}`);
  exit(1);
} finally {
  console.log('SUCCESS');
}
