import * as os from 'os';

export function setTimeout(fn, t) {
  return os.setTimeout(fn, t);
}

export function clearTimeout(id) {
  return os.clearTimeout(id);
}

let intervalId,
  intervalMap = {};

export function setInterval(fn, t) {
  let id, fn2, ret;
  ret = intervalId = (intervalId | 0) + 1;
  const sto = () => (intervalMap[ret] = setTimeout(fn2, t));
  fn2 = () => {
    fn();
    sto();
  };
  sto();
  return ret;
}

export function clearInterval(id) {
  clearTimeout(intervalMap[id]);
  delete intervalMap[id];
}