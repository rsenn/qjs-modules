import { abbreviate, randStr, startInteractive } from 'util';
import extendArray from '../lib/extendArray.js';
import { Console } from 'console';
import { PGconn, PGresult } from 'pgsql';
import { exit } from 'std';

extendArray();

async function main(...args) {
  globalThis.console = new Console({
    inspectOptions: {
      compact: 1,
      customInspect: true,
      showHidden: false,
      hideKeys: ['query'],
      protoChain: true
    }
  });

  Object.assign(globalThis, { PGconn, PGresult });

  let i, pq, q, result, resultNum;

  pq = globalThis.pq = new PGconn();

  console.log('pq.connect() =', console.config({ compact: false }), await pq.connect('localhost', 'roman', 'r4eHuJ', 'roman', 5432, 10));

  resultNum = 0;

  result = r => {
    let prop = 'result' + ++resultNum;
    globalThis[prop] = r;
    console.log(prop + ' =', console.config({ compact: true }), r);
    return r;
  };

  q = globalThis.q = async s => (console.log(`q('\x1b[0;32m${abbreviate(s, 1000)}'\x1b[0m)`), result(await pq.query(s)));
  i = 0;

  await q(`CREATE TABLE IF NOT EXISTS users (
  id SERIAL PRIMARY KEY,
  name character varying(1024) UNIQUE NOT NULL,
  password character varying(1024) NOT NULL,
  email character varying(512) UNIQUE,       
  properties JSON DEFAULT '{}',
  bindata BYTEA,
  last_seen timestamptz DEFAULT now()
);`);

  await q(`CREATE TABLE IF NOT EXISTS sessions (
  id SERIAL PRIMARY KEY,
  user_id integer NOT NULL,    
  uuid character varying(1024) UNIQUE NOT NULL,
  variables JSON DEFAULT '{}',
  created timestamptz DEFAULT now(),
  FOREIGN KEY (user_id) REFERENCES users(id)
  ON UPDATE CASCADE
  ON DELETE RESTRICT
);`);

  let id,
    res = await q(`SELECT * FROM test;`);

  for(let row of res) result(row);

  await q(pq.insertQuery('users', ['name', 'password', 'email'], [randStr(32), randStr(32), randStr(64)]));

  await q(pq.insertQuery('sessions', ['user_id', 'uuid', 'variables'], [pq.insertId, [8, 4, 4, 4, 12].map(n => randStr(n, '0123456789abcdef')).join('-'), { blah: 1234 }]));

  console.log('pq.affectedRows =', pq.affectedRows);
  console.log('id =', (id = pq.insertId));
}

try {
  main(...scriptArgs.slice(1)).catch(err => console.log(`FAIL: ${err.message}\n${err.stack}`));
} catch(error) {
  console.log(`FAIL: ${error.message}\n${error.stack}`);
  exit(1);
} finally {
  console.log('SUCCESS');
}
