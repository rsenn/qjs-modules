import { performance } from 'perf_hooks';

function waitFor(msecs) {
  return new Promise((resolve, reject) => {
    setTimeout(() => {
      resolve();
    }, msecs);
  });
}

async function main(...args) {
  console.log('now()', performance.now());

  await waitFor(1000);
  console.log('now()', performance.now());

  try {
    const obs = new (await import('perf_hooks')).PerformanceObserver(list => {
      console.log('function duration', list.getEntries()[0].duration);
      obs.disconnect();
    });
    obs.observe({ entryTypes: ['function'] });
    const wrapped = performance.timerify(async () => waitFor(1000));
    console.log(await wrapped());
  } catch(e) {}
}

try {
  main(...process.argv.slice(2));
} catch(error) {
  console.log(`FAIL: ${error.message}\n${error.stack}`);
} finally {
  console.log('SUCCESS');
}
