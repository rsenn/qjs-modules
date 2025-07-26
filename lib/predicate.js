function returnPredicate(fn, tostr, tosrc) {
  if(typeof fn == 'string') fn = new Function('arg', 'return ' + fn);
  return Object.defineProperties(Object.setPrototypeOf(fn, Predicate.prototype), {
    toString: {
      value: () => tostr,
    },
    toSource: { value: arg => tosrc(arg) },
  });
}

export class Predicate extends Function {
  static property(name, pred) {
    return returnPredicate(
      arg => {
        if(typeof arg == 'object' && arg != null && name in arg) return pred(arg[name]);
      },
      `.${name}`,
      (a = 'arg') => pred.toSource(`${a}.${name}`),
    );
  }

  static regexp(re, flags = '') {
    if(typeof re != 'object' || re == null || !(re instanceof RegExp)) re = new RegExp(re, flags);
    return returnPredicate(
      arg => re.test(arg),
      `${re}`,
      (a = 'arg') => `${re}.test(${a})`,
    );
  }

  static string(str) {
    if(typeof str != 'string') str = str + '';
    return returnPredicate(
      arg => arg == str || 0 == str.localeCompare(arg),
      `'${str}'`,
      (a = 'arg') => `arg => '${str}' == ${a}`,
    );
  }

  static and(...predicates) {
    return returnPredicate(
      (...args) => predicates.every(pred => pred(...args)),
      predicates.map(pred => pred + '').join(' && '),
      (a = 'args...') => `(args...) => ` + predicates.map(pred => pred + '(...args)').join(' && '),
    );
  }
}
