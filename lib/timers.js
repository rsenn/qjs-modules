import { clearTimeout, setTimeout } from 'os';

export { setTimeout, clearTimeout } from 'os';

const intervalMap = {},
  intervalId = (
    u32 => () =>
      u32[0]++ & 0x7fffffff
  )(new Uint32Array(1));

export function setInterval(fn, t) {
  const ret = intervalId();

  function start() {
    intervalMap[ret] = setTimeout(() => {
      start();
      fn();
    }, t);
  }

  start();

  return ret;
}

export function clearInterval(id) {
  clearTimeout(intervalMap[id]);
  delete intervalMap[id];
}

/* Importing this module is what makes the timer functions global in QuickJS, which has none by default. */
for(const [name, fn] of Object.entries({ setTimeout, clearTimeout, setInterval, clearInterval })) globalThis[name] ??= fn;
