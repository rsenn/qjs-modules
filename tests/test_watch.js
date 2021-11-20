m = loadModule('fs');
globalThis.fs = getModuleExports(m);
r = fs.watch(__dirname + '/../quickjs-misc.c', {}, (eventType, filename) =>
  console.log('watch event', { eventType, filename })
);
