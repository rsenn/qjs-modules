import * as os from 'os';
import * as std from 'std';
import { Console } from 'console';
import { MySQL, MySQLResult } from 'mysql';

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
      customInspect: true,
      showHidden: false,
      hideKeys: ['query']
    }
  });

  let my = (globalThis.my = new MySQL());

  console.log(
    'my.connect() =',
    await my.connect('localhost', 'root', 'tD51o7xf', 'mysql', 0, '/var/run/mysqld/mysqld.sock')
  );

  let res;

  for await(let row of (res = await my.query(`SELECT user,password,host FROM user WHERE host!='';`))) {
    console.log('row =', row);
  }
  result(res);

  result((res = await my.query(`CREATE DATABASE blah;`)));
  result((res = await my.query(`USE blah;`)));

  result(
    (res = await my.query(
      `CREATE TABLE IF NOT EXISTS article ( id int unsigned NOT NULL auto_increment, title char(64) NOT NULL DEFAULT '', text TEXT NOT NULL DEFAULT '',  PRIMARY KEY (id)) CHARACTER SET utf8`
    ))
  );

  for await(let table of (res = await my.query(`SHOW TABLES;`))) {
    console.log('table =', table);
  }
  result(res);

  for await(let field of (res = await my.query(`SHOW FIELDS FROM article;`))) {
    console.log('field =', field);
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

  for await(let row of (res = await my.query(`SELECT * FROM article;`))) {
    console.log('row =', row);
  }
  result(res);
  
  for await(let row of (res = await my.query(`SELECT * FROM article INNER JOIN categories ON article.category_id=categories.id;`))) {
    console.log('row =', row);
  }
  result(res);


  os.kill(process.pid, os.SIGUSR1);
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error.message}\n${error.stack}`);
  std.exit(1);
} finally {
  console.log('SUCCESS');
}
