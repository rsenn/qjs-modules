//import process from 'process';

export const Predicate =
  process.release.name == 'quickjs'
    ? require('predicate')
    : (() => {
        function returnPredicate(fn, tostr, tosrc) {
          if(typeof fn == 'string') fn = new Function('arg', 'return ' + fn);
          return Object.defineProperties(Object.setPrototypeOf(fn, Predicate.prototype), {
            toString: {
              value: () => tostr
            },
            toSource: { value: arg => tosrc(arg) }
          });
        }

        class Predicate extends Function {
          static property(name, pred) {
            return returnPredicate(
              arg => {
                if(typeof arg == 'object' && arg != null && name in arg) return pred(arg[name]);
              },
              `.${name}`,
              (a = 'arg') => pred.toSource(`${a}.${name}`)
            );
          }
          static regexp(re, flags = '') {
            if(typeof re != 'object' || re == null || !(re instanceof RegExp)) re = new RegExp(re, flags);
            return returnPredicate(re.toString() + '.test(arg)', `${re}`, (a = 'arg') => `${re}.test(${a})`);
          }
          static string(str) {
            if(typeof str != 'string') str = str + '';
            return returnPredicate(
              arg => {
                let result = arg == str || 0 == str.localeCompare(arg);
                return result;
              },
              `'${str}'`,
              (a = 'arg') => `arg => '${str}' == ${a}`
            );
          }
        }
        return Predicate;
      })();

export function TypeSelector(tagName) {
  return Predicate.property('tagName', Predicate.regexp('^' + tagName + '$', 'i'));
}

export function ClassSelector(className) {
  return Predicate.property(
    'attributes',
    Predicate.property('class', Predicate.regexp('(^|\\b)' + className + '($|\\b)'))
  );
}

export function AttributeSelector(attrName, value) {
  return Predicate.property('attributes', Predicate.property(attrName, Predicate.string(value)));
}

export function IdSelector(id) {
  return AttributeSelector('id', id);
}

export function parseSelector(s) {
  let match,
    re = /([.#]?[-_\w]+|\[([-_\w]+)(\W+)['"]?([-_\w]+)['"]?\])/g;
  let selectors = [];

  while((match = re.exec(s))) {
    let [str, ...capture] = match;
    let sel;
    if(str[0] == '.') {
      sel = ClassSelector(str.substring(1));
    } else if(str[0] == '#') {
      sel = IdSelector(str.substring(1));
    } else if(str[0] == '[') {
      sel = AttributeSelector(capture[0], capture[1]);
    } else {
      sel = TypeSelector(str);
    }
    selectors.push(sel);
  }
  return element => selectors.every(sel => sel(element));
}
