// @portable: also run on node, bun and deno by ctest (tests/deno-import-map.json for deno)
/* the default export of Node's modules: the module object, EventEmitter for events */
import events from 'events';
import fsPromises from 'fs/promises';
import readline from 'readline';
import readlinePromises from 'readline/promises';
import url from 'url';
import util from 'util';
import { eq, tests } from '../../lib/tinytest.js';

tests({
  'events is EventEmitter'() {
    eq('EventEmitter', events.name);
    eq('function', typeof events.once);
  },

  'the others are the module object'() {
    eq('function', typeof fsPromises.readFile);
    eq('function', typeof readline.createInterface);
    eq('function', typeof readlinePromises.createInterface);
    eq('function', typeof url.fileURLToPath);
    eq('function', typeof util.promisify);
    eq('function', typeof util.inherits);
    eq('function', typeof util.format);
  },
});
