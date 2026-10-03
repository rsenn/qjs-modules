/* native pgsql module (libpq binding). Everything here runs without a
 * server; set PGSQL_TEST to a libpq conninfo string to add live checks. */
import { PGconn, PGerror, PGresult } from 'pgsql';
import { getenv } from 'std';
import { assert, eq, tests } from '../../lib/tinytest.js';

const live = getenv('PGSQL_TEST');

const suite = {
  'exports the classes'() {
    eq('function', typeof PGconn);
    eq('function', typeof PGresult);
    eq('function', typeof PGerror);
  },

  'escapeString doubles single quotes'() {
    eq("a''b", PGconn.escapeString("a'b"));
  },

  'bytea escaping round-trips'() {
    const bytes = new Uint8Array([1, 2, 255]);
    const escaped = PGconn.escapeBytea(bytes.buffer);

    eq('string', typeof escaped);

    const back = new Uint8Array(PGconn.unescapeBytea('\\x0102ff'));

    eq('1,2,255', back.join());
  },

  'valueString and valuesString render SQL literals'() {
    const db = new PGconn();

    eq('5', db.valueString(5));
    eq("'x''y'", db.valueString("x'y"));
    eq("(1, 'a', NULL)", db.valuesString([1, 'a', null]));
  },

  'result type constants'() {
    eq(1, PGconn.RESULT_OBJECT);
    eq(2, PGconn.RESULT_STRING);
    eq('number', typeof PGconn.RESULT_TBLNAM);
  },

  'an unconnected PGconn reports a null connection'() {
    const db = new PGconn();

    assert(/connection pointer is NULL/.test(db.errorMessage), db.errorMessage);
    eq(-1, db.fd);
    eq(null, db.host);
  },

  'connecting to a closed port records the failure'() {
    const db = new PGconn();

    db.connect('host=127.0.0.1 port=1 connect_timeout=2');
    assert(/Connection refused/.test(db.errorMessage), db.errorMessage);
  },
};

if(live)
  Object.assign(suite, {
    async 'live: connect and query'() {
      const db = new PGconn();

      await db.connect(live);

      const result = await db.query("SELECT 1 AS one, 'x' AS s");

      eq(1, result.numRows ?? [...result].length);
      db.close();
    },
  });
else console.log('SKIP: live server checks (PGSQL_TEST not set)');

tests(suite);
