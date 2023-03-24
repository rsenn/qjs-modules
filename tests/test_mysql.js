import * as os from 'os';
import * as std from 'std';
import { Console } from 'console';
import { MySQL, MySQLResult } from 'mysql';
import extendArray from '../lib/extendArray.js';

extendArray();

('use strict');
('use math');

let resultNum = 0;

const result = r => {
  let prop = 'res' + ++resultNum;
  globalThis[prop] = r;
  console.log(prop + ':', r);
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

  //my.resultType |= MySQL.RESULT_STRING;
  my.resultType |= MySQL.RESULT_OBJECT;
  //my.resultType |= MySQL.RESULT_TABLENAME;

  console.log(
    'my.connect() =',
    await my.connect('localhost', 'root', 'tD51o7xf', 'mysql', undefined, '/var/run/mysqld/mysqld.sock')
  );

  let res, i;

  let q = async s => (console.log(`q('\x1b[0;32m${s}'\x1b[0m)`), (res = await my.query(s)));

  i = 0;
  for await(let row of await q(`SELECT user,password,host FROM user WHERE host!='';`)) {
    console.log(`row[${i++}] =`, row);
  }
  result(res);

  result(await q(`CREATE DATABASE blah;`));
  result(await q(`USE blah;`));

  result(
    (res = await my.query(
      `CREATE TABLE IF NOT EXISTS article ( id int unsigned NOT NULL auto_increment, title char(64) NOT NULL DEFAULT '', text TEXT NOT NULL DEFAULT '',  PRIMARY KEY (id)) CHARACTER SET utf8`
    ))
  );

  for await(let table of await q(`SHOW TABLES;`)) {
    console.log('table =', table);
  }
  result(res);

  let title = 'This is an article';
  let text = 'lorem ipsum...';

  result(
    (res = await my.query(
      `INSERT INTO article (title,text) VALUES ('${my.escapeString(title)}', '${my.escapeString(text)}');`
    ))
  );

  let id = my.insertId;
  console.log('id =', id);

  i = 0;
  for await(let row of (res = await my.query(
    `SELECT * FROM article INNER JOIN categories ON article.category_id=categories.id;`
  ))) {
    console.log(`row[${i++}] =`, row);
  }
  result(res);

  i = 0;
  let rows = (globalThis.rows = []);

  // my.resultType &= ~MySQL.RESULT_OBJECT;

  for await(let row of await q(`SELECT * FROM article ORDER BY id DESC;`)) {
    console.log(`row[${i++}] =`, row);

    rows.unshift(row);
  }
  result(res);

  async function* showFields(table = 'article') {
    my.resultType &= ~MySQL.RESULT_OBJECT;
    for await(let field of await q(`SHOW FIELDS FROM article`)) {
      let name=field['COLUMNS.Field'] ?? field['Field'] ?? field[0];
let type= field[1];

      console.log('field',{name,type});
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

  console.log('insert', insert);

  result((res = await q(insert)));

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
