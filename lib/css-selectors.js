import { parse } from './parsel.js';
import { Predicate } from 'predicate';

export function emitPredicates(node) {
  let pred = null;

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
      pred = Predicate.property('attributes', Predicate.property(node.type, Predicate.string(node.name)));
      break;
    }

    case 'complex': {
      switch (node.combinator) {
        case '>': {
          break;
        }
      }

      break;
    }

    /* case 'pseudo-element': {
      break;
    }

    case 'pseudo-class': {
      break;
    }

    case 'universal': {
      pred = () => true;
      break;
    }*/

    case 'type': {
      pred = Predicate.property('tagName', Predicate.string(node.name));
      break;
    }

    default: {
      throw new Error(`ERROR parsing CSS selector: No such type '${node.type}'`);
      break;
    }
  }

  return pred;
}

export function parseSelectors(s) {
  const ast = parse(s, { recursive: false, list: false });

  return emitPredicates(ast);
}