import { readFileSync } from 'fs';
import { Rule, _capture, captureRoot } from '../lib/parser.js';
import { parseGrammar } from '../lib/parser/ebnf.js';
import { assert, eq, tests } from './tinytest.js';

/* Extra terminals used by Shell-Grammar.y but never declared via %token -
 * an incomplete transcription of the operator characters (PIPE '|', LPAREN
 * '(', RPAREN ')', SEMI ';', BACKGND '&', GREAT '>', LESS '<'). */
const EXTRA_TOKENS = ['PIPE', 'LPAREN', 'RPAREN', 'SEMI', 'BACKGND', 'GREAT', 'LESS'];

const RESERVED = { if: 'IF', then: 'THEN', else: 'ELSE', elif: 'ELIF', fi: 'FI', do: 'DO', done: 'DONE', case: 'CASE', esac: 'ESAC', while: 'WHILE', until: 'UNTIL', for: 'FOR', in: 'IN' };

/* Longest-match-first: 3-char operators before 2-char before 1-char. */
const OPERATORS = [
  ['<<-', 'DLESSDASH'],
  ['&&', 'AND_IF'],
  ['||', 'OR_IF'],
  [';;', 'DSEMI'],
  ['<<', 'DLESS'],
  ['>>', 'DGREAT'],
  ['<&', 'LESSAND'],
  ['>&', 'GREATAND'],
  ['<>', 'LESSGREAT'],
  ['>|', 'CLOBBER'],
  ['|', 'PIPE'],
  ['(', 'LPAREN'],
  [')', 'RPAREN'],
  [';', 'SEMI'],
  ['&', 'BACKGND'],
  ['>', 'GREAT'],
  ['<', 'LESS'],
  ['{', 'LBRACE'],
  ['}', 'RBRACE'],
  ['!', 'BANG'],
];

/* A minimal tokenizer for a *fantasy* shell script - deliberately not a
 * POSIX-correct one: no command substitution, no quoting/escaping, no
 * here-documents. Just enough to exercise the grammar compiled from
 * Shell-Grammar.y (reserved words, the handful of multi-char operators, and
 * plain WORDs). */
function tokenizeShell(src) {
  const tokens = [];
  const n = src.length;
  let i = 0;

  while(i < n) {
    const c = src[i];

    if(c === '\n') {
      tokens.push({ type: 'NEWLINE', lexeme: '\n' });
      i++;
      continue;
    }

    if(c === ' ' || c === '\t') {
      i++;
      continue;
    }

    if(c === '#') {
      while(i < n && src[i] !== '\n') i++;
      continue;
    }

    const op = OPERATORS.find(([text]) => src.startsWith(text, i));

    if(op) {
      tokens.push({ type: op[1], lexeme: op[0] });
      i += op[0].length;
      continue;
    }

    const start = i;

    while(i < n && !/[\s|&;<>(){}!#]/.test(src[i])) i++;

    const word = src.slice(start, i);

    if(word in RESERVED) tokens.push({ type: RESERVED[word], lexeme: word });
    else if(/^[A-Za-z_][A-Za-z0-9_]*=/.test(word)) tokens.push({ type: 'ASSIGNMENT_WORD', lexeme: word });
    else tokens.push({ type: 'WORD', lexeme: word });
  }

  return tokens;
}

/* A token-level counterpart to lib/parser.js's stringInput(): same
 * duck-typed interface (.next() / .loc.clone() / .charPos), but iterating
 * over pre-lexed {type, lexeme} tokens instead of characters - what
 * Shell-Grammar.y's terminals (WORD, NEWLINE, IF, LPAREN, ...) actually
 * match against. */
function tokenInput(tokens) {
  let i = 0;

  return {
    next() {
      return i < tokens.length ? tokens[i++] : undefined;
    },
    get loc() {
      return { clone: () => i };
    },
    get charPos() {
      return i;
    },
    set charPos(value) {
      i = value;
    },
    get eof() {
      return i >= tokens.length;
    },
    tokens,
  };
}

/* Matches one token whose `.type` equals `type` - the terminal-matching
 * counterpart to lib/parser.js's own id-comparing Rule, for a lexer that
 * yields token objects rather than characters. */
class TokenTerminal extends Rule {
  constructor(type) {
    super();
    this.type = type;
  }

  match(lexer) {
    return Rule.match(lexer, lex => {
      const tok = lex.next();
      return tok !== undefined && tok.type === this.type;
    });
  }

  toSource() {
    return this.type;
  }
}

function compileShellGrammar() {
  const grammar = parseGrammar(readFileSync('./tests/Shell-Grammar.y', 'utf-8'));

  for(const name of [...grammar.tokens, ...EXTRA_TOKENS]) if(!(name in grammar.rules)) grammar.rules[name] = new TokenTerminal(name);

  /* Wrap every user-defined (non-terminal) rule in a Capture, so matching
   * the compiled grammar against real input builds a labeled syntax tree -
   * RuleRef resolves by name at match time, so this rewiring is picked up
   * by every recursive/forward reference too. */
  for(const name of grammar.order) grammar.rules[name] = _capture(name, grammar.rules[name]);

  return grammar;
}

function findNode(node, name) {
  if(node.name === name) return node;

  for(const child of node.children) {
    const found = findNode(child, name);
    if(found) return found;
  }

  return null;
}

tests({
  'Shell-Grammar.y compiles into a Grammar of Rule objects'() {
    const grammar = parseGrammar(readFileSync('./tests/Shell-Grammar.y', 'utf-8'));

    eq(grammar.start, 'complete_command');
    assert(grammar.order.length > 0);
    assert(grammar.rules['complete_command'] instanceof Rule);
    assert(grammar.tokens.has('WORD'));
    assert(grammar.tokens.has('IF'));
  },

  'tokenizeShell() splits a fantasy script into WORD/reserved-word/operator tokens'() {
    const tokens = tokenizeShell('if true\nthen\n    echo yes\nfi');

    eq(
      tokens.map(t => t.type).join(','),
      ['IF', 'WORD', 'NEWLINE', 'THEN', 'NEWLINE', 'WORD', 'WORD', 'NEWLINE', 'FI'].join(','),
    );
    eq(tokens[1].lexeme, 'true');
  },

  /* Note: deliberately a pipeline + a `;`-separated command list, not an
   * if/then/else script - see BUGS: quickjs-stack-size-flag-ineffective.
   * This vendored QuickJS build hits an uncatchably-low ~164-frame
   * recursion ceiling (unaffected by --stack-size) well before a script
   * with several more levels of nested compound_command/compound_list
   * would finish parsing; this script still exercises Shell-Grammar.y's
   * left-recursive rules (list, pipe_sequence) via compileRule()'s
   * left-recursion elimination, while staying inside that budget. */
  'the compiled Shell-Grammar.y grammar parses a fantasy pipeline + command list'() {
    const tokens = tokenizeShell('echo hello world | cat; echo bye');

    const grammar = compileShellGrammar();
    const lexer = tokenInput(tokens);

    const ok = grammar.startRule.match(lexer);

    assert(ok, 'expected the fantasy script to match complete_command');
    eq(lexer.charPos, tokens.length, 'expected the whole token stream to be consumed');
  },

  'the resulting syntax tree reflects the shell script structure (list / pipe_sequence / simple_command)'() {
    const tokens = tokenizeShell('echo hello world | cat; echo bye');

    const grammar = compileShellGrammar();
    const lexer = tokenInput(tokens);

    assert(grammar.startRule.match(lexer));

    const [root] = captureRoot(lexer);

    eq(root.name, 'complete_command');
    eq(root.start, 0);
    eq(root.end, tokens.length);

    const list = findNode(root, 'list');
    assert(list !== null, 'expected a list node in the syntax tree');
    eq(list.children.filter(n => n.name === 'and_or').length, 2, 'expected two and_or branches joined by ";"');
    eq(list.children.filter(n => n.name === 'separator_op').length, 1, 'expected one separator_op (";") between them');

    const pipeSequence = findNode(root, 'pipe_sequence');
    assert(pipeSequence !== null, 'expected a pipe_sequence node (the "echo ... | cat" pipeline)');
    eq(pipeSequence.children.filter(n => n.name === 'command').length, 2, 'expected two commands joined by "|"');

    /* three cmd_name nodes: "echo" (piped), "cat", "echo" (after ";") -
     * also exercises that a failed Alternative branch (Capture nodes from
     * an abandoned "cmd_name cmd_suffix" attempt that then falls back to
     * bare "cmd_name") doesn't leak duplicate nodes into the tree. */
    const cmdNames = [];
    (function walk(node) {
      if(node.name === 'cmd_name') cmdNames.push(node);
      for(const child of node.children) walk(child);
    })(root);

    eq(cmdNames.length, 3);
  },

  'a syntactically invalid fantasy script fails to parse'() {
    const grammar = compileShellGrammar();
    const lexer = tokenInput(tokenizeShell('| echo hi'));

    const ok = grammar.startRule.match(lexer);

    assert(!ok, 'expected a leading "|" with no command before it not to match');
  },
});
