import { Directory } from 'directory';
import Console from 'console';

function main(...args) {
  globalThis.console = new Console({
    inspectOptions: {
      colors: true,
      showHidden: false,
      customInspect: true,
      showProxy: false,
      getters: false,
      depth: 4,
      maxArrayLength: 10,
      maxStringLength: 200,
      compact: false,
      hideKeys: ['loc', 'range', 'inspect', Symbol.for('nodejs.util.inspect.custom')]
    }
  });
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error.message}\n${error.stack}`);
}
