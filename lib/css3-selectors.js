import { Predicate } from 'predicate';

function escapeRegExp(s) {
  return s.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
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

/*
 * Tokenizer: splits a selector string into simple-selector, combinator and
 * comma tokens, handling quoted strings, backslash escapes, namespaces and
 * balanced parentheses.
 */
const IDENT_CHAR = /[-\w-￿]/;

function tokenize(s) {
  const tokens = [];
  const n = s.length;
  let i = 0;

  const readIdent = () => {
    let r = '';

    while(i < n) {
      if(s[i] == '\\' && i + 1 < n) {
        r += s[i + 1];
        i += 2;
        continue;
      }

      if(!IDENT_CHAR.test(s[i])) break;

      r += s[i++];
    }

    if(r == '') throw new Error(`ERROR parsing CSS selector: identifier expected at position ${i} in '${s}'`);

    return r;
  };

  const readString = () => {
    const quote = s[i++];
    let r = '';

    while(i < n && s[i] != quote) {
      if(s[i] == '\\' && i + 1 < n) {
        r += s[i + 1];
        i += 2;
        continue;
      }

      r += s[i++];
    }

    if(s[i] != quote) throw new Error(`ERROR parsing CSS selector: unterminated string in '${s}'`);

    i++;
    return r;
  };

  const skipWs = () => {
    while(i < n && /\s/.test(s[i])) i++;
  };

  const last = () => tokens[tokens.length - 1];

  while(i < n) {
    const c = s[i];

    if(/\s/.test(c)) {
      skipWs();

      if(tokens.length && last().type != 'combinator' && last().type != 'comma') tokens.push({ type: 'combinator', combinator: ' ' });

      continue;
    }

    if(c == ',') {
      if(last()?.type == 'combinator' && last().combinator == ' ') tokens.pop();

      tokens.push({ type: 'comma' });
      i++;
      continue;
    }

    if(c == '>' || c == '+' || c == '~') {
      if(last()?.type == 'combinator' && last().combinator == ' ') tokens.pop();

      tokens.push({ type: 'combinator', combinator: c });
      i++;
      continue;
    }

    if(c == '#') {
      i++;
      tokens.push({ type: 'id', name: readIdent() });
      continue;
    }

    if(c == '.') {
      i++;
      tokens.push({ type: 'class', name: readIdent() });
      continue;
    }

    if(c == '[') {
      i++;
      skipWs();

      let namespace, name;

      if(s[i] == '|' && s[i + 1] != '=') {
        namespace = '';
        i++;
        name = readIdent();
      } else if(s[i] == '*' && s[i + 1] == '|') {
        namespace = '*';
        i += 2;
        name = readIdent();
      } else {
        name = readIdent();

        if(s[i] == '|' && s[i + 1] != '=') {
          namespace = name;
          i++;
          name = readIdent();
        }
      }

      skipWs();

      let operator, value, caseSensitive;

      if(s[i] != ']') {
        if(s[i] == '=') operator = '=';
        else if('~|^$*'.includes(s[i]) && s[i + 1] == '=') operator = s[i] + '=';

        if(operator == undefined) throw new Error(`ERROR parsing CSS selector: invalid attribute operator at position ${i} in '${s}'`);

        i += operator.length;
        skipWs();

        value = s[i] == '"' || s[i] == "'" ? readString() : readIdent();
        skipWs();

        if(i < n && /[iIsS]/.test(s[i]) && (i + 1 >= n || /[\s\]]/.test(s[i + 1]))) {
          caseSensitive = s[i++];
          skipWs();
        }
      }

      if(s[i] != ']') throw new Error(`ERROR parsing CSS selector: expected ']' at position ${i} in '${s}'`);

      i++;
      tokens.push({ type: 'attribute', name, namespace, operator, value, caseSensitive });
      continue;
    }

    if(c == ':') {
      i++;
      const element = s[i] == ':';

      if(element) i++;

      const name = readIdent();
      let argument;

      if(s[i] == '(') {
        i++;
        const start = i;
        let depth = 0;

        while(i < n) {
          if(s[i] == '"' || s[i] == "'") {
            readString();
            continue;
          }

          if(s[i] == '(') depth++;
          else if(s[i] == ')') {
            if(depth == 0) break;
            depth--;
          }

          i++;
        }

        if(s[i] != ')') throw new Error(`ERROR parsing CSS selector: expected ')' at position ${i} in '${s}'`);

        argument = s.slice(start, i).trim();
        i++;
      }

      tokens.push({ type: element ? 'pseudo-element' : 'pseudo-class', name, argument });
      continue;
    }

    if(c == '*' || c == '|' || c == '\\' || IDENT_CHAR.test(c)) {
      let namespace, name;

      if(c == '*') {
        name = '*';
        i++;
      } else if(c != '|') {
        name = readIdent();
      }

      if(s[i] == '|' && s[i + 1] != '=') {
        namespace = name ?? '';
        i++;

        if(s[i] == '*') {
          name = '*';
          i++;
        } else {
          name = readIdent();
        }
      }

      tokens.push({ type: name == '*' ? 'universal' : 'type', name, namespace });
      continue;
    }

    throw new Error(`ERROR parsing CSS selector: unexpected '${c}' at position ${i} in '${s}'`);
  }

  if(last()?.type == 'combinator' && last().combinator == ' ') tokens.pop();

  return tokens;
}

/*
 * Parser: groups tokens into a list of alternatives (comma-separated), each an
 * array of steps { combinator, parts } where parts is a compound selector and
 * combinator relates it to the preceding step (null on the first step).
 * With relative = true a leading '>', '+' or '~' is allowed (for :has()).
 */
function parseSelectorList(s, { relative = false } = {}) {
  const alternatives = [];
  let steps = [],
    parts = [],
    combinator = null;

  const endCompound = () => {
    if(parts.length) {
      steps.push({ combinator, parts });
      parts = [];
      combinator = null;
    }
  };

  const endAlternative = () => {
    endCompound();

    if(combinator != null || steps.length == 0) throw new Error(`ERROR parsing CSS selector: incomplete selector '${s}'`);

    alternatives.push(steps);
    steps = [];
  };

  for(const token of tokenize(s)) {
    switch (token.type) {
      case 'comma':
        endAlternative();
        break;

      case 'combinator':
        if(parts.length == 0) {
          if(steps.length == 0 && relative && combinator == null) {
            combinator = token.combinator;
            break;
          }

          throw new Error(`ERROR parsing CSS selector: unexpected combinator '${token.combinator}' in '${s}'`);
        }

        endCompound();
        combinator = token.combinator;
        break;

      default:
        parts.push(token);
        break;
    }
  }

  endAlternative();

  return alternatives;
}

export function LogicPredicate(pred = Predicate.or, ...args) {
  if(args.length > 0) return pred(...args);
}

export function TypeSelector(tagName, namespace) {
  const part = tagName == '*' ? '.*' : escapeRegExp(tagName);
  let source;

  if(namespace == null || namespace == '') source = `^${part}$`;
  else if(namespace == '*') source = `^(?:[^:]+:)?${part}$`;
  else source = `^${escapeRegExp(namespace)}:${part}$`;

  return Predicate.property('tagName', Predicate.regexp(source, 'i'));
}

export function ClassSelector(className) {
  return Predicate.property('attributes', Predicate.property('class', Predicate.regexp(`(^|\\s)${escapeRegExp(className)}(\\s|$)`)));
}

export function AttributeSelector(name, value, operator = '=', caseSensitive) {
  if(value === undefined) return Predicate.property('attributes', Predicate.has(name));

  const flags = /[iI]/.test(caseSensitive ?? '') ? 'i' : '';

  if(operator == '=' && !flags) return Predicate.property('attributes', Predicate.property(name, Predicate.string(value)));

  const v = escapeRegExp(value);
  const source = {
    '=': `^${v}$`,
    '~=': `(^|\\s)${v}(\\s|$)`,
    '|=': `^${v}(-|$)`,
    '^=': `^${v}`,
    '$=': `${v}$`,
    '*=': v,
  }[operator];

  if(source == undefined) throw new Error(`ERROR parsing CSS selector: No such attribute operator '${operator}'`);

  return Predicate.property('attributes', Predicate.property(name, Predicate.regexp(source, flags)));
}

export function IdSelector(id) {
  return AttributeSelector('id', id);
}

/* One predicate that requires every part of a compound selector on the same node */
function CompoundPredicate(parts) {
  const preds = parts.map(SimpleSelector);

  return preds.length == 1 ? preds[0] : Predicate.and(...preds);
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

/* Precise predicate for one alternative: rightmost compound plus combinator
   checks walking left (needs parentElement/…ElementSibling navigation) */
function ComplexPredicate(steps) {
  let pred = CompoundPredicate(steps[0].parts);

  for(let k = 1; k < steps.length; k++) pred = CombinatorSelector(steps[k].combinator, pred, CompoundPredicate(steps[k].parts));

  return pred;
}

/* Predicate for a full selector list (used for pseudo-class arguments) */
function SelectorListPredicate(s, options) {
  const preds = parseSelectorList(s, options).map(steps => (steps.length == 1 && steps[0].combinator == null ? CompoundPredicate(steps[0].parts) : ComplexPredicate(steps)));

  return preds.length == 1 ? preds[0] : Predicate.or(...preds);
}

function HasSelector(argument) {
  const alternatives = parseSelectorList(argument, { relative: true }).map(steps => {
    const entry = steps[0].combinator ?? ' ';
    const pred = ComplexPredicate([{ ...steps[0], combinator: null }, ...steps.slice(1)]);

    switch (entry) {
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

function PseudoClass(name, argument) {
  const sameType = e => s => s.tagName === e.tagName;

  switch (name) {
    case 'not':
      return Predicate.not(SelectorListPredicate(argument ?? ''));

    case 'is':
    case 'where':
    case 'matches':
    case 'any':
    case '-moz-any':
    case '-webkit-any':
      return SelectorListPredicate(argument ?? '');

    case 'has':
      return HasSelector(argument ?? '');

    case 'nth-child':
    case 'nth-last-child': {
      const m = /^(.+?)\s+of\s+(.+)$/.exec(argument ?? '');
      const nth = parseNth(m ? m[1] : argument);
      const filter = m ? SelectorListPredicate(m[2]) : null;
      const dir = name == 'nth-child' ? 'previousElementSibling' : 'nextElementSibling';

      return e => e.parentNode != null && (!filter || Boolean(filter(e))) && matchNth(nth, siblingIndex(e, dir, filter && (s => Boolean(filter(s)))));
    }

    case 'nth-of-type':
    case 'nth-last-of-type': {
      const nth = parseNth(argument);
      const dir = name == 'nth-of-type' ? 'previousElementSibling' : 'nextElementSibling';

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

    case 'lang': {
      const lang = (argument ?? '').toLowerCase();

      return e => {
        for(let p = e; p; p = p.parentElement) {
          const v = getAttribute(p, 'lang') ?? getAttribute(p, 'xml:lang');

          if(v != null) {
            const lv = v.toLowerCase();
            return lv == lang || lv.startsWith(lang + '-');
          }
        }

        return false;
      };
    }

    case 'link':
      return e => /^(a|area)$/i.test(e.tagName) && hasAttribute(e, 'href');

    case 'enabled':
      return e => !hasAttribute(e, 'disabled');

    case 'disabled':
      return e => hasAttribute(e, 'disabled');

    case 'checked':
      return e => hasAttribute(e, 'checked') || hasAttribute(e, 'selected');

    /* dynamic user-interaction states never match in a static document */
    case 'visited':
    case 'hover':
    case 'active':
    case 'focus':
    case 'target':
      return () => false;

    default:
      throw new Error(`ERROR parsing CSS selector: No such pseudo-class ':${name}'`);
  }
}

export function PseudoClassSelector(pseudoClass) {
  const m = /^([-\w]+)(?:\((.*)\))?$/.exec(pseudoClass);

  if(!m) throw new Error(`ERROR parsing CSS selector: invalid pseudo-class '${pseudoClass}'`);

  return PseudoClass(m[1], m[2]?.trim());
}

function SimpleSelector(token) {
  switch (token.type) {
    case 'type':
      return TypeSelector(token.name, token.namespace);

    case 'universal':
      return token.namespace ? TypeSelector('*', token.namespace) : Predicate.property('tagName');

    case 'id':
      return IdSelector(token.name);

    case 'class':
      return ClassSelector(token.name);

    case 'attribute': {
      const name = token.namespace && token.namespace != '*' ? `${token.namespace}:${token.name}` : token.name;

      return token.operator == undefined ? AttributeSelector(name) : AttributeSelector(name, token.value, token.operator, token.caseSensitive);
    }

    case 'pseudo-class':
      return PseudoClass(token.name, token.argument);

    case 'pseudo-element':
      throw new Error(`ERROR parsing CSS selector: pseudo-element '::${token.name}' cannot match a node`);

    default:
      throw new Error(`ERROR parsing CSS selector: No such type '${token.type}'`);
  }
}

/*
 * Yields one Predicate.and(...) per comma-separated alternative.
 *
 * The parts of descendant ('a b') and child ('a > b') steps are flattened into
 * the and(), whose .values() the dom.js querySelector machinery matches along
 * the ancestor hierarchy. Sibling steps ('a + b', 'a ~ b') contribute a single
 * navigation-based value predicate.
 */
export function* parseSelectors(s) {
  for(const steps of parseSelectorList(s)) {
    const values = [];
    let prev = null;

    for(const step of steps) {
      const compound = CompoundPredicate(step.parts);

      if(step.combinator == '+' || step.combinator == '~') values.push(CombinatorSelector(step.combinator, prev, compound));
      else for(const part of step.parts) values.push(SimpleSelector(part));

      prev = compound;
    }

    yield LogicPredicate(Predicate.and, ...values);
  }
}
