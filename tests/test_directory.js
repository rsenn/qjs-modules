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

  if(args.length == 0) args = ['.'];

  for(let arg of args) {
    let dir = new Directory(arg);

    for(let [name, type] of dir) {
      if(type == Directory.TYPE_DIR) name += '/';

      console.log('entry', console.config({ compact: 0 }), { name, type });
    }
  }
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error.message}\n${error.stack}`);
}
