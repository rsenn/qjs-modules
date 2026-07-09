import { parse } from './parsel.js';
import { Predicate } from 'predicate';

function escapeRegExp(s) {
  return s.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}

function unquote(s) {
  if(s.length >= 2 && (s[0] == '"' || s[0] == "'") && s[s.length - 1] == s[0]) return s.slice(1, -1);
  return s;
}

/* On wrapped dom.js elements attributes[name] is an Attr object, on raw nodes
   and plain objects it is the value string; getAttribute() covers both. */
function getAttribute(e, name) {
  if(typeof e.getAttribute == 'function') return e.getAttribute(name) ?? undefined;

  const v = e.attributes?.[name];
  return v == null ? undefined : String(v);
}

function hasAttribute(e, name) {
  if(typeof e.hasAttribute == 'function') return e.hasAttribute(name);

  return e.attributes != null && name in e.attributes;
}

function isElement(node) {
  return node != null && typeof node == 'object' && typeof node.tagName == 'string' && !/^[!?]/.test(node.tagName);
}

function* childElements(node) {
  for(const child of node.children ?? []) if(isElement(child)) yield child;
}

function* descendantElements(node) {
  for(const child of childElements(node)) {
    yield child;
    yield* descendantElements(child);
  }
}

/* 1-based position among the (filtered) siblings, walking dir =
   'previousElementSibling' (:nth-child) or 'nextElementSibling' (:nth-last-child) */
function siblingIndex(e, dir, filter) {
  let i = 1;

  for(let s = e[dir]; s; s = s[dir]) if(!filter || filter(s)) i++;

  return i;
}

/* Parse an An+B expression ('odd', 'even', '5', '2n+1', '-n+3', ...) into [a, b] */
function parseNth(s) {
  let m;
  s = (s ?? '').trim().toLowerCase();

  if(s == 'odd') return [2, 1];
  if(s == 'even') return [2, 0];

  if((m = /^([+-]?\d+)$/.exec(s))) return [0, +m[1]];

  if((m = /^([+-]?\d*)n(?:\s*([+-])\s*(\d+))?$/.exec(s))) return [m[1] == '' || m[1] == '+' ? 1 : m[1] == '-' ? -1 : +m[1], m[2] ? +(m[2] + m[3]) : 0];

  throw new Error(`ERROR parsing CSS selector: invalid An+B expression '${s}'`);
}

/* Does the 1-based index i satisfy An+B, i.e. exists n >= 0 with i == a*n + b */
function matchNth([a, b], i) {
  if(a == 0) return i == b;

  return (i - b) / a >= 0 && (i - b) % a == 0;
}

function TypeSelector(name, namespace) {
  const part = name == '*' ? '.*' : escapeRegExp(name);
  let source;

  if(namespace == null || namespace == '') source = `^${part}$`;
  else if(namespace == '*') source = `^(?:[^:]+:)?${part}$`;
  else source = `^${escapeRegExp(namespace)}:${part}$`;

  return Predicate.property('tagName', Predicate.regexp(source, 'i'));
}

function AttributeSelector(node) {
  const name = node.namespace && node.namespace != '*' ? `${node.namespace}:${node.name}` : node.name;

  if(!node.operator) return e => hasAttribute(e, name);

  const value = escapeRegExp(unquote(node.value ?? ''));
  const flags = /[iI]/.test(node.caseSensitive ?? '') ? 'i' : '';
  const source = {
    '=': `^${value}$`,
    '~': `(^|\\s)${value}(\\s|$)`,
    '|': `^${value}(-|$)`,
    '^': `^${value}`,
    $: `${value}$`,
    '*': value,
  }[node.operator[0]];

  if(source == undefined) throw new Error(`ERROR parsing CSS selector: No such attribute operator '${node.operator}'`);

  const re = new RegExp(source, flags);

  return e => {
    const v = getAttribute(e, name);

    return v != null && re.test(v);
  };
}

function CombinatorSelector(combinator, left, right) {
  switch (combinator) {
    case '>':
      return e => Boolean(right(e)) && e.parentElement != null && Boolean(left(e.parentElement));

    case '+':
      return e => Boolean(right(e)) && e.previousElementSibling != null && Boolean(left(e.previousElementSibling));

    case '~':
      return e => {
        if(!right(e)) return false;

        for(let s = e.previousElementSibling; s; s = s.previousElementSibling) if(left(s)) return true;

        return false;
      };

    case ' ':
    default:
      return e => {
        if(!right(e)) return false;

        for(let p = e.parentElement; p; p = p.parentElement) if(left(p)) return true;

        return false;
      };
  }
}

function HasSelector(subtree) {
  const alternatives = (subtree.type == 'list' ? subtree.list : [subtree]).map(alt => {
    const combinator = alt.type == 'relative' ? alt.combinator : ' ';
    const pred = emitPredicates(alt.type == 'relative' ? alt.right : alt);

    switch (combinator) {
      case '>':
        return e => {
          for(const child of childElements(e)) if(pred(child)) return true;

          return false;
        };

      case '+':
        return e => e.nextElementSibling != null && Boolean(pred(e.nextElementSibling));

      case '~':
        return e => {
          for(let s = e.nextElementSibling; s; s = s.nextElementSibling) if(pred(s)) return true;

          return false;
        };

      case ' ':
      default:
        return e => {
          for(const descendant of descendantElements(e)) if(pred(descendant)) return true;

          return false;
        };
    }
  });

  return e => alternatives.some(f => f(e));
}

function PseudoClassSelector(node) {
  const subtree = () => node.subtree ?? parse(node.argument ?? '');
  const sameType = e => s => s.tagName === e.tagName;

  switch (node.name) {
    case 'not':
      return Predicate.not(emitPredicates(subtree()));

    case 'is':
    case 'where':
    case 'matches':
    case '-moz-any':
    case '-webkit-any':
      return emitPredicates(subtree());

    case 'has':
      return HasSelector(subtree());

    case 'nth-child':
    case 'nth-last-child': {
      const nth = parseNth(node.index ?? node.argument);
      const filter = node.subtree ? emitPredicates(node.subtree) : null;
      const dir = node.name == 'nth-child' ? 'previousElementSibling' : 'nextElementSibling';

      return e => e.parentNode != null && (!filter || Boolean(filter(e))) && matchNth(nth, siblingIndex(e, dir, filter && (s => Boolean(filter(s)))));
    }

    case 'nth-of-type':
    case 'nth-last-of-type': {
      const nth = parseNth(node.argument);
      const dir = node.name == 'nth-of-type' ? 'previousElementSibling' : 'nextElementSibling';

      return e => e.parentNode != null && matchNth(nth, siblingIndex(e, dir, sameType(e)));
    }

    case 'first-child':
      return e => e.parentNode != null && e.previousElementSibling == null;

    case 'last-child':
      return e => e.parentNode != null && e.nextElementSibling == null;

    case 'only-child':
      return e => e.parentNode != null && e.previousElementSibling == null && e.nextElementSibling == null;

    case 'first-of-type':
      return e => e.parentNode != null && siblingIndex(e, 'previousElementSibling', sameType(e)) == 1;

    case 'last-of-type':
      return e => e.parentNode != null && siblingIndex(e, 'nextElementSibling', sameType(e)) == 1;

    case 'only-of-type':
      return e => e.parentNode != null && siblingIndex(e, 'previousElementSibling', sameType(e)) == 1 && siblingIndex(e, 'nextElementSibling', sameType(e)) == 1;

    case 'root':
      return e => e.parentElement == null;

    case 'empty':
      return e => (e.children?.length ?? 0) == 0;

    default:
      throw new Error(`ERROR parsing CSS selector: No such pseudo-class ':${node.name}'`);
  }
}

export function emitPredicates(node) {
  let pred = null;

  switch (node.type) {
    case 'attribute': {
      pred = AttributeSelector(node);
      break;
    }

    case 'id': {
      pred = Predicate.property('attributes', Predicate.property('id', Predicate.string(node.name)));
      break;
    }

    case 'class': {
      pred = Predicate.property('attributes', Predicate.property('class', Predicate.regexp(`(^|\\s)${escapeRegExp(node.name)}(\\s|$)`)));
      break;
    }

    case 'type': {
      pred = TypeSelector(node.name, node.namespace);
      break;
    }

    case 'universal': {
      pred = node.namespace ? TypeSelector('*', node.namespace) : Predicate.property('tagName');
      break;
    }

    case 'list': {
      pred = Predicate.or(...node.list.map(emitPredicates));
      break;
    }

    case 'compound': {
      pred = Predicate.and(...node.list.map(emitPredicates));
      break;
    }

    case 'complex': {
      pred = CombinatorSelector(node.combinator, emitPredicates(node.left), emitPredicates(node.right));
      break;
    }

    case 'relative': {
      throw new Error(`ERROR parsing CSS selector: relative selector '${node.combinator}' only supported inside :has()`);
    }

    case 'pseudo-class': {
      pred = PseudoClassSelector(node);
      break;
    }

    case 'pseudo-element': {
      throw new Error(`ERROR parsing CSS selector: pseudo-element '::${node.name}' cannot match a node`);
    }

    default: {
      throw new Error(`ERROR parsing CSS selector: No such type '${node.type}'`);
    }
  }

  return pred;
}

export function parseSelectors(s) {
  const ast = parse(s);

  return emitPredicates(ast);
}
