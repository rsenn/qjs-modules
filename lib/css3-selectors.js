import * as Predicate from 'predicate';

export function TypeSelector(tagName) {
  return Predicate.property('tagName', Predicate.string(tagName.toUpperCase()));
}

export function ClassSelector(className) {
  return Predicate.property('attributes', Predicate.property('class', Predicate.regexp(`\b${className}\b`)));
}
