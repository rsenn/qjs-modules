import Console from '../lib/console.js';

function main(...args) {
  globalThis.console = new Console({
    inspectOptions: { depth: 1, compact: Infinity },
  });

  console.log('console.options:', console.options);

  console.log('regexp:', /TEST/);
  
  console.log('process.hrtime():', process.hrtime());
  console.log('process.arch:', process.arch);
}

try {
  main(...process.argv.slice(2));
} catch(error) {
  console.log(`FAIL: ${error.message}\n${error.stack}`);
} finally {
  console.log('SUCCESS');
}
