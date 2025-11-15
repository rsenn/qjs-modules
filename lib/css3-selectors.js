import { Predicate } from 'predicate';

export function LogicPredicate(pred = Predicate.or, ...args) {
  /*if(args.length == 1) return args[0];
  else*/ if(args.length > 0) return pred(...args);
}

export function TypeSelector(tagName) {
  return Predicate.property('tagName', Predicate.regexp('^' + tagName + '$', 'i'));
}

export function ClassSelector(className) {
  return Predicate.property('attributes', Predicate.property('class', Predicate.regexp('(^|\\b)' + className + '($|\\b)')));
}

export function PseudoClassSelector(pseudoClass) {
  let si = pseudoClass.indexOf('(');
  let se = pseudoClass.lastIndexOf(')');
  let name, arg;
  if(si != -1) {
    if(se == -1) se = pseudoClass.length;
    name = pseudoClass.substring(0, si);
    arg = pseudoClass.substring(si + 1, se);
  } else {
    name = pseudoClass;
    arg = undefined;
  }

  return (
    {
      ['nth-child'](n) {
        let y = Predicate.equal(n - 1);
        let x = Predicate.index(-1, y);
        return Predicate.shift(1, x);
        console.log('nth-child', { n });
      },
    }[name] ?? (() => {})
  )(arg);

  /*  return Predicate.property(
    'attributes',
    Predicate.property('class', Predicate.regexp('(^|\\b)' + className + '($|\\b)'))
  );*/
}

export function AttributeSelector(...args) {
  let pred;
  if(args.length > 1) {
    const [attrName, value] = args;
    pred = Predicate.property(attrName, Predicate.string(value));
  } else {
    pred = Predicate.has(...args);
  }
  return Predicate.property('attributes', pred);
}

export function IdSelector(id) {
  return AttributeSelector('id', id);
}

export function* parseSelectors(s) {
  let match,
    re = /(\[([-_\w]+)(\W+)?['"]?([-_\w/]+)?['"]?\]|(?:,\s*)?([.#]?[-_\w]+|:[-_\w]+(\([^\)]*\)|)|\s+))/g;
  let or = [],
    selectors = [],
    logic = Predicate.and;

  while((match = re.exec(s))) {
    let [str, ...capture] = match;
    let sel;

    if(str[0] == ',') {
      str = str.replace(/^,\s*/g, '');
      or.push(LogicPredicate(Predicate.and, ...selectors));
      selectors = [];
    }

    //console.log(`parseSelectors '${str}'`);

    /* if(str[0] == ' ') {
      if(selectors.length == 1) yield selectors[0];
      else if(selectors.length) yield Predicate.and(...selectors);
      selectors = [];
      continue;
    }*/

    if(/^\s*$/.test(str)) continue;

    if(str[0] == ':') {
      sel = PseudoClassSelector(str.substring(1));
    } else if(str[0] == '.') {
      sel = ClassSelector(str.substring(1));
    } else if(str[0] == '#') {
      sel = IdSelector(str.substring(1));
    } else if(str[0] == '[') {
      const [, name, , value] = capture;
      sel = value !== undefined ? AttributeSelector(name, value) : AttributeSelector(name);
    } else {
      sel = TypeSelector(str);
    }

    selectors.push(sel);
  }

  if(selectors) or.push(LogicPredicate(Predicate.and, ...selectors));

  yield* or;
  //return LogicPredicate(Predicate.or, ...or);

  /*  if(or.length > 1) yield Predicate.or(...or);
  else yield or[0];*/
}
