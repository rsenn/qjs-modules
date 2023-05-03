import { Console } from 'console';
import { exit } from 'std';
import { abbreviate, startInteractive } from 'util';
import { PGconn, PGresult } from 'pgsql';
import extendArray from '../lib/extendArray.js';

extendArray();

('use strict');
('use math');

let resultNum = 0;

const result = r => {
  let prop = 'result' + ++resultNum;
  globalThis[prop] = r;
  console.log(/*'globalThis.' +*/ prop + ' =', r);
  return r;
};

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

  let pq = (globalThis.pq = new PGconn());

  console.log(
    'pq.connect() =',
    console.config({ compact: false }),
    await pq.connect('user=roman password=r4eHuJ host=localhost port=5432 dbname=roman connect_timeout=10')
  );

  let i;

  let q = (globalThis.q = async s => (
    console.log(`q('\x1b[0;32m${abbreviate(s, 1000)}'\x1b[0m)`), result(await pq.query(s))
  ));

  i = 0;

  await q(`CREATE DATABASE IF NOT EXISTS blah;`);
  await q(`USE blah;`);

  await q(
    `CREATE TABLE IF NOT EXISTS article ( id int unsigned NOT NULL auto_increment, title char(64) NOT NULL DEFAULT '', text TEXT NOT NULL DEFAULT '',  PRIMARY KEY (id)) CHARACTER SET utf8`
  );

  //for await(let table of await q(`SHOW TABLES;`)) console.log('table =', table);
  //
  //
  let res = (globalThis.res = await q(`SELECT id,title,category_id FROM article LIMIT 0,10;`));

  console.log(`res =`, res);
  /*console.log(`res[Symbol.iterator] =`, res[Symbol.iterator]);
  console.log(`res[Symbol.asyncIterator] =`, res[Symbol.asyncIterator]);*/

  //for await(let row of res) console.log(`row[${i++}] =`, row);

  /*  startInteractive();
  return;*/

  for await(let row of res) {
    console.log(`row[${i++}] =`, row);
  }
  let articles = [
    ['This is an article', 'lorem ipsum...'],
    ['This is another article', 'fliesstext...']
  ];

  await q(
    `INSERT INTO article (title,text) VALUES ${articles.map(cols => `(${pq.valueString(...cols)})`).join(', ')};`
  );

  let affected;
  console.log('affected =', (affected = pq.cmdTuples));

  let id = pq.insertId;
  console.log('id =', id);

  i = 0;

  res = await q(`SELECT * FROM article INNER JOIN categories ON article.category_id=categories.id LIMIT 0,10;`);

  for await(let row of res) console.log(`category[${i++}] =`, row);

  i = 0;
  let rows = (globalThis.rows = []);

  res = await q(`SELECT * FROM article ORDER BY id DESC LIMIT 0,10;`);
  for await(let row of res) {
    console.log(`row[${i++}] =`, console.config({ compact: 1 }), row);

    rows.unshift(row);
  }

  async function* showFields(table = 'article') {
    let res = await q(`SHOW FIELDS FROM article`);

    for await(let field of res) {
      let name = field['COLUMNS.Field'] ?? field['Field'] ?? field[0];
      let type = field[1];

      //console.log('field', { name, type });
      yield name;
    }
  }

  let fieldNames = (globalThis.fields = []);
  for await(let field of await showFields()) fieldNames.push(field);

  const rowValues = row => row.map(s => pq.valueString(s));
  const rowString = row => pq.valueString(...row);

  function makeInsertQuery(table = 'article', fields, data = {}) {
    return `INSERT INTO ${table} (${fields.map(f => '`' + f + '`').join(',')}) VALUES (${rowString(data)});`;
  }

  console.log('fieldNames', fieldNames);
  let myrow = Array.isArray(rows[0]) ? rows[0] : fieldNames.map(n => rows[0][n]);

  let insert = (globalThis.insert = makeInsertQuery('article', fieldNames.slice(1), myrow.slice(1)));

  console.log('insert', insert);

  for await(let row of await q(`SELECT id,title,category_id,visible FROM article ORDER BY id DESC LIMIT 0,10;`))
    console.log(`article[${i++}] =`, row);

  await q(insert);
  console.log('affected =', (affected = pq.cmdTuples));

  res = await q(`SELECT * FROM test;`);

  console.log('res =', res);
  let iter = (globalThis.iter = res[Symbol.iterator]());

  console.log('iter =', iter);
  let entry, row;
  console.log('await iter.next() =', (entry = await iter.next()));
  console.log('entry.value =', (row = entry.value));
  console.log('row =', row);
  if(row) {
    console.log('row[0] =', row[0]);
  }
  console.log('id =', (id = pq.insertId));

  startInteractive();
  // os.kill(process.pid, os.SIGUSR1);
}

try {
  main(...scriptArgs.slice(1)).catch(err => console.log(`FAIL: ${err.message}\n${err.stack}`));
} catch(error) {
  console.log(`FAIL: ${error.message}\n${error.stack}`);
  exit(1);
} finally {
  console.log('SUCCESS');
}
