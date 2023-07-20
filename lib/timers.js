import { setTimeout, clearTimeout } from 'os';
export { setTimeout, clearTimeout } from 'os';

const intervalId = (() => {
    const u32 = new Uint32Array(1);
    return () => u32[0]++;
  })(),
  intervalMap = {};

export function setInterval(fn, t) {
  let ret = intervalId();

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
