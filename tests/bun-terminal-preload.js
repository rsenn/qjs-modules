/* bun --preload tests/bun-terminal-preload.js: makes the bare names of lib/terminal*.js resolve */
const lib = new URL('../lib/', import.meta.url).pathname;

Bun.plugin({
  name: 'terminal',
  setup(build) {
    build.onResolve({ filter: /^terminal(\/\w+)?$/ }, ({ path }) => ({ path: lib + (path == 'terminal' ? 'terminal.js' : path + '.js') }));
  },
});
