import * as os from 'os';
import * as std from 'std';
import { Console } from 'console';
import { abbreviate } from 'util';
import { MySQL, MySQLResult } from 'mysql';
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
      hideKeys: ['query']
    }
  });

  let my = (globalThis.my = new MySQL());

  my.resultType |= MySQL.RESULT_OBJECT;

  /*my.setOption(MySQL.OPT_NONBLOCK, true);
  my.setOption(MySQL.OPT_NONBLOCK, false);

  console.log('2: my.getOption(OPT_NONBLOCK) =', my.getOption(MySQL.OPT_NONBLOCK));*/

  console.log(
    'my.connect() =',
    await my.connect('localhost', 'root', 'tD51o7xf', 'mysql', undefined, '/var/run/mysqld/mysqld.sock')
  );

  let i;

  let q = /*async*/ s => (console.log(`q('\x1b[0;32m${abbreviate(s, 1000)}'\x1b[0m)`), result(my.query(s)));

  i = 0;

  await q(`CREATE DATABASE blah;`);
  await q(`USE blah;`);

  await q(
    `CREATE TABLE IF NOT EXISTS article ( id int unsigned NOT NULL auto_increment, title char(64) NOT NULL DEFAULT '', text TEXT NOT NULL DEFAULT '',  PRIMARY KEY (id)) CHARACTER SET utf8`
  );

  //for await(let table of await q(`SHOW TABLES;`)) console.log('table =', table);
let res=await q(`SELECT id,title,category_id FROM article LIMIT 0,10;`);
   console.log(`res =`, res);
  console.log(`res[Symbol.asyncIterator] =`, res[Symbol.asyncIterator]);
  //for await(let row of await res)
  for (let row of  res)
    console.log(`row[${i++}] =`, row);

  let articles = [
    ['This is an article', 'lorem ipsum...'],
    ['This is another article', 'fliesstext...']
  ];

  await q(
    `INSERT INTO article (title,text) VALUES ${articles.map(cols => `(${MySQL.valueString(...cols)})`).join(', ')};`
  );

  let affected = my.affectedRows;
  console.log('affected =', affected);

  let id = my.insertId;
  console.log('id =', id);

  i = 0;
  for await(let row of await q(
    `SELECT * FROM article INNER JOIN categories ON article.category_id=categories.id LIMIT 0,10;`
  ))
    console.log(`row[${i++}] =`, row);

  i = 0;
  let rows = (globalThis.rows = []);

  // my.resultType &= ~MySQL.RESULT_OBJECT;

  for await(let row of await q(`SELECT * FROM article ORDER BY id DESC LIMIT 0,10;`)) {
    console.log(`row[${i++}] =`, console.config({ compact: 1 }), row);

    rows.unshift(row);
  }

  async function* showFields(table = 'article') {
    my.resultType &= ~MySQL.RESULT_OBJECT;
    for await(let field of await q(`SHOW FIELDS FROM article`)) {
      let name = field['COLUMNS.Field'] ?? field['Field'] ?? field[0];
      let type = field[1];

      //console.log('field', { name, type });
      yield name;
    }
    my.resultType |= MySQL.RESULT_OBJECT;
  }

  let fieldNames = (globalThis.fields = []);
  for await(let field of await showFields()) fieldNames.push(field);

  const rowValues = row => row.map(s => MySQL.valueString(s));
  const rowString = row => MySQL.valueString(...row);

  function makeInsertQuery(table = 'article', fields, data = {}) {
    return `INSERT INTO ${table} (${fields.map(f => '`' + f + '`').join(',')}) VALUES (${rowString(data)});`;
  }

  console.log('fieldNames', fieldNames);
  let myrow = Array.isArray(rows[0]) ? rows[0] : fieldNames.map(n => rows[0][n]);

  let insert = (globalThis.insert = makeInsertQuery('article', fieldNames.slice(1), myrow.slice(1)));

  //console.log('insert', insert);

  await q(insert);

  console.log('affected =', (affected = my.affectedRows));
  console.log('id =', (id = my.insertId));

  os.kill(process.pid, os.SIGUSR1);
}

try {
  main(...scriptArgs.slice(1)).catch(err => console.log(`FAIL: ${err.message}\n${err.stack}`));
} catch(error) {
  console.log(`FAIL: ${error.message}\n${error.stack}`);
  std.exit(1);
} finally {
  console.log('SUCCESS');
}
