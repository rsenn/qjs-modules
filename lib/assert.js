export function assert(cond, msg) {
  if(!cond) throw new Error('assertion failed' + (msg ? ': ' : '') + (msg ?? ''));
}

export default assert;
