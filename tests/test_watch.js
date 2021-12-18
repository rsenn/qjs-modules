m = loadModule('fs');
globalThis.fs = getModuleExports(m);
const file = 'quickjs-misc.c';

console.log('file', file);
r = fs.watch(file, 0xfff, (eventType, filename) => console.log('watch event', { eventType, filename }));
