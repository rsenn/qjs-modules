import { performance } from 'perf_hooks';

function main(...args) {
  console.log('now()', performance.now());
}

try {
  main(...process.argv.slice(2));
} catch(error) {
  console.log(`FAIL: ${error.message}\n${error.stack}`);
} finally {
  console.log('SUCCESS');
}
