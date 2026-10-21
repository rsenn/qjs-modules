/* lib/util.js, the qjsm-only helpers */
import { isMainModule } from 'util';
import { eq, tests } from '../../lib/tinytest.js';

tests({
  'isMainModule is true for the script qjsm runs'() {
    eq(true, isMainModule(import.meta.url));
  },

  'isMainModule is false for another module'() {
    eq(false, isMainModule(import.meta.url.replace(/[^/]*$/, 'other.js')));
  },

  /* qjsm -e and the REPL define no scriptArgs */
  'isMainModule is false without scriptArgs'() {
    const args = globalThis.scriptArgs;

    delete globalThis.scriptArgs;

    try {
      eq(false, isMainModule(import.meta.url));
    } finally {
      globalThis.scriptArgs = args;
    }
  },
});
