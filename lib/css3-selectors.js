import * as Predicate from 'predicate';

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
    console.log('str', str);
    console.log('capture', capture);
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
