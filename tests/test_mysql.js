import { Console } from 'console';
import { exit } from 'std';
import { abbreviate, startInteractive } from 'util';
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

  Object.assign(globalThis, { MySQL, MySQLResult });

  let my = (globalThis.my = new MySQL());

  my.resultType |= MySQL.RESULT_OBJECT;

  my.setOption(MySQL.OPT_NONBLOCK, true);

  console.log('2: my.getOption(OPT_NONBLOCK) =', my.getOption(MySQL.OPT_NONBLOCK));

  console.log(
    'my.connect() =',
    console.config({ compact: false }),
    await my.connect('localhost', 'roman', 'r4eHuJ', 'blah', undefined, '/var/run/mysqld/mysqld.sock')
  );

  let i;

  let q = (globalThis.q = async s => (
    console.log(`q('\x1b[0;32m${abbreviate(s, 1000)}'\x1b[0m)`), result(await my.query(s))
  ));

  i = 0;
  let res = await q(`SELECT id,title,category_id FROM article LIMIT 0,10;`);

  console.log(`res =`, res);

  /*  await q(`CREATE DATABASE IF NOT EXISTS blah;`);
  await q(`USE blah;`);

  await q(
    `CREATE TABLE IF NOT EXISTS article ( id int unsigned NOT NULL auto_increment, title char(64) NOT NULL DEFAULT '', text TEXT NOT NULL DEFAULT '',  PRIMARY KEY (id)) CHARACTER SET utf8`
  );
*/

  for await(let row of res) {
    console.log(`row[${i++}] =`, row);
  }
  let articles = [
    ['This is an article', 'lorem ipsum...'],
    ['This is another article', 'fliesstext...']
  ];

  await q(
    `INSERT INTO article (title,text) VALUES ${articles.map(cols => `(${MySQL.valueString(...cols)})`).join(', ')};`
  );

  let affected;
  console.log('affected =', (affected = my.affectedRows));

  let id = my.insertId;
  console.log('id =', id);

  i = 0;
  my.resultType &= ~(MySQL.RESULT_TABLENAME | MySQL.RESULT_OBJECT);

  res = await q(`SELECT * FROM article INNER JOIN categories ON article.category_id=categories.id LIMIT 0,10;`);

  for await(let row of res) console.log(`category[${i++}] =`, row);

  i = 0;
  let rows = (globalThis.rows = []);

  my.resultType &= ~MySQL.RESULT_OBJECT;
  res = await q(`SELECT * FROM article ORDER BY id DESC LIMIT 0,10;`);
  for await(let row of res) {
    console.log(`row[${i++}] =`, console.config({ compact: 1 }), row);

    rows.unshift(row);
  }

  async function* showFields(table = 'article') {
    my.resultType &= ~MySQL.RESULT_OBJECT;
    let res = await q(`SHOW FIELDS FROM article`);

    for await(let field of res) {
      let name = field['COLUMNS.Field'] ?? field['Field'] ?? field[0];
      let type = field[1];

      //console.log('field', { name, type });
      yield name;
    }
    // my.resultType |= MySQL.RESULT_OBJECT;
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

  console.log('insert', insert);

  for await(let row of await q(`SELECT id,title,category_id,visible FROM article ORDER BY id DESC LIMIT 0,10;`))
    console.log(`article[${i++}] =`, row);

  await q(insert);
  console.log('affected =', (affected = my.affectedRows));

  res = await q(`SELECT last_insert_id();`);

  console.log('res =', res);
  let iter = (globalThis.iter = res[Symbol.asyncIterator]());

  console.log('iter =', iter);
  let row;
  console.log('(await iter.next()).value =', (row = globalThis.row = (await iter.next()).value));
  console.log('row[0] =', row[0]);

  console.log('id =', (id = my.insertId));

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
