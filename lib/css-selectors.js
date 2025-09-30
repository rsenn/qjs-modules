import { tokenize, nestTokens, flatten, walk, parse, stringify } from './lib/parsel.js';
import { Predicate } from 'predicate';

export function emitPredicates(node) {
  let pred;
  switch (node.type) {
    case 'attribute': {
      let m =
        {
          '=': v => `${v}`,
          '~': v => `\\b${v}\\b`,
          '|': v => `^(${v}|^${v}-)`,
          '^': v => `^${v}`,
          $: v => `${v}$`,
          '*': v => `${v}`,
        }[node.operator[0]] ?? (() => true);

      pred = Predicate.property('attributes', Predicate.property(node.name, Predicate.regexp(m)));
      break;
    }

    case 'id':
    case 'class': {
      pred = Predicate.property(
        'attributes',
        Predicate.property(node.type, Predicate.string(node.name)),
      );
      break;
    }

    case 'complex': {
      break;
    }
 
    case 'pseudo-element': {
      break;
    }
 
    case 'pseudo-class': {
      break;
    }
 
    case 'universal': {
      pred = () => true;
      break;
    }

    case 'type': {
      pred = Predicate.property('tagName', Predicate.string(node.name));
      break;
    }
  }

  return pred;
}

export function parseSelectors(s) {
  const ast = parse(s);

  return emitPredicates(node);
}
