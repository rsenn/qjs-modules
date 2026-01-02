import { clearTimeout } from 'os';
import { setTimeout } from 'os';

export { setTimeout, clearTimeout } from 'os';

const intervalMap = {},
  intervalId = (
    u32 => () =>
      u32[0]++ & 0x7fffffff
  )(new Uint32Array(1));

export function setInterval(fn, t) {
  ret = intervalId();

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