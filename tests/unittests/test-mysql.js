/* native mysql module (libmysqlclient binding). Everything here runs
 * without a server; set MYSQL_TEST="host,user,password,database" to add
 * live-server checks. */
import { MySQL, MySQLError, MySQLResult } from 'mysql';
import { getenv } from 'std';
import { eq, tests } from '../../lib/tinytest.js';

const live = (getenv('MYSQL_TEST') ?? '').split(',');

/* the static escaping helpers crash until one instance exists (see BUGS) */
const instance = new MySQL();

const suite = {
  'exports the three classes'() {
    eq('function', typeof MySQL);
    eq('function', typeof MySQLResult);
    eq('function', typeof MySQLError);
  },

  'client library info is available'() {
    eq('number', typeof MySQL.clientVersion);
    eq('string', typeof MySQL.clientInfo);
    eq('boolean', typeof MySQL.threadSafe);
  },

  'escapeString escapes quotes with a backslash'() {
    eq("a\\'b", MySQL.escapeString("a'b"));
    eq('plain', MySQL.escapeString('plain'));
  },

  'valueString renders JS values as SQL'() {
    eq('5', MySQL.valueString(5));
    eq("'x'", MySQL.valueString('x'));
  },

  'valuesString renders a list'() {
    eq("(1, 'a', NULL)", MySQL.valuesString([1, 'a', null]));
  },

  'result type and status constants are distinct numbers'() {
    for(const name of ['RESULT_OBJECT', 'RESULT_STRING', 'RESULT_TBLNAM', 'STATUS_READY', 'STATUS_QUERY_SENT']) eq('number', typeof MySQL[name]);

    eq(5, new Set([MySQL.STATUS_READY, MySQL.STATUS_GET_RESULT, MySQL.STATUS_USE_RESULT, MySQL.STATUS_QUERY_SENT, MySQL.STATUS_NEXT_RESULT_PENDING]).size);
  },

  'a new connection has no error and no server'() {
    const db = new MySQL();

    eq(0, db.errno);
    eq(null, db.error);
    eq(0, db.affectedRows);
  },

  async 'connecting to a closed port sets errno 2002'() {
    const db = new MySQL();

    await db.connect({ host: '127.0.0.1', port: 1, user: 'nobody', password: '', db: 'none' });
    eq(2002, db.errno);
  },
};

if(live[0])
  Object.assign(suite, {
    async 'live: connect, query, iterate, close'() {
      const [host, user, password, db] = live;
      const conn = new MySQL();

      await conn.connect({ host, user, password, db });
      eq(0, conn.errno);

      const rows = [];

      for await(const row of await conn.query("SELECT 1 AS one, 'x' AS s")) rows.push(row);

      eq(1, rows.length);
      conn.close();
    },
  });
else console.log('SKIP: live server checks (MYSQL_TEST not set)');

tests(suite);
