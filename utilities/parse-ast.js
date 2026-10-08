#!/usr/bin/env qjsm
import { readFileSync, writeFileSync } from 'fs';
import { getOpt } from 'util';
import * as std from 'std';
import { Earley } from 'parser/earley.js';
import { charItems, g4Lexer, lexItems } from 'parser/scan.js';
import { parseBNF } from 'parser/bnf.js';
import { parseEBNF } from 'parser/ebnf.js';
import { parseG4 } from 'parser/g4.js';
import { parseLex } from 'parser/lex.js';
import { parseYacc } from 'parser/yacc.js';
import { toCFG } from 'parser/cfg.js';

/* extension -> [kind, parser] */
const Grammars = {
  bnf: ['bnf', parseBNF],
  ebnf: ['ebnf', parseEBNF],
  g4: ['g4', parseG4],
  y: ['yacc', parseYacc],
  yy: ['yacc', parseYacc],
  jison: ['yacc', parseYacc],
  l: ['lex', parseLex],
  ll: ['lex', parseLex],
  flex: ['lex', parseLex],
  jisonlex: ['lex', parseLex],
};

const extOf = file => file.slice(file.lastIndexOf('.') + 1).toLowerCase();
const isGrammar = file => Object.prototype.hasOwnProperty.call(Grammars, extOf(file));

function usage(exitCode) {
  (exitCode ? std.err : std.out).puts(
    `Usage: ${scriptArgs[0]} [OPTIONS] <grammar-files...> [--] <sources...>\n\n` +
      `Parses each source with the grammar and prints its syntax tree as JSON.\n\n` +
      `Grammar files: .bnf .ebnf .g4 .y .yy .jison (rules), .l .ll .flex .jisonlex (tokens);\n` +
      `.bnf/.ebnf read characters, the others need a lexer: a .l file, a jison %lex block or\n` +
      `g4 lexer rules. A node is {type, text, loc, children}; a token {type, text, loc}.\n\n` +
      `  -s, --start RULE    start rule (default: the first)\n` +
      `  -c, --compact       replace a node with a single node child by that child\n` +
      `  -L, --no-loc        leave out loc\n` +
      `  -T, --no-text       leave out text of nodes (tokens keep it)\n` +
      `  -w, --skip-ws       .bnf/.ebnf: skip whitespace in the source\n` +
      `  -I, --ident-token N token returned for a lex rule whose action calls a function (default IDENTIFIER)\n` +
      `  -i, --indent N      JSON indent (default 2, 0 for one line)\n` +
      `  -o, --output FILE   write to FILE instead of stdout\n` +
      `  -h, --help          show this help\n`,
  );
  std.exit(exitCode);
}

/* offset -> {offset, line, column} */
function locator(text) {
  const starts = [0];

  for(let i = text.indexOf('\n'); i >= 0; i = text.indexOf('\n', i + 1)) starts.push(i + 1);

  return offset => {
    let lo = 0,
      hi = starts.length - 1;

    while(lo < hi) {
      const mid = (lo + hi + 1) >> 1;

      if(starts[mid] <= offset) lo = mid;
      else hi = mid - 1;
    }

    return { offset, line: lo + 1, column: offset - starts[lo] + 1 };
  };
}

function makeTree(text, items, opts) {
  const at = locator(text);
  const place = (from, to) => (opts.loc ? { loc: { start: at(from), end: at(to) } } : {});

  function convert(n) {
    if(n.token) return { type: n.token.type, text: n.token.text, ...place(n.token.pos, n.token.end) };

    const from = items[n.start]?.pos ?? text.length,
      to = n.end > n.start ? items[n.end - 1].end : from;
    const children = n.children.map(convert);

    if(opts.compact && children.length == 1 && children[0].children) return children[0];

    return { type: n.rule, ...(n.label ? { label: n.label } : {}), ...(opts.text ? { text: text.slice(from, to) } : {}), ...place(from, to), children };
  }

  return convert;
}

function main(...args) {
  const cut = args.indexOf('--');
  const params = getOpt(
    {
      help: [false, () => usage(0), 'h'],
      start: [true, null, 's'],
      compact: [false, null, 'c'],
      'no-loc': [false, null, 'L'],
      'no-text': [false, null, 'T'],
      'skip-ws': [false, null, 'w'],
      'ident-token': [true, null, 'I'],
      indent: [true, null, 'i'],
      output: [true, null, 'o'],
      '@': 'files',
    },
    cut < 0 ? args : args.slice(0, cut),
  );

  let files = params['@'],
    sources = cut < 0 ? [] : args.slice(cut + 1);

  /* without `--`, the files with a grammar extension are the grammars */
  if(cut < 0) [files, sources] = [files.filter(isGrammar), files.filter(f => !isGrammar(f))];

  if(!files.length || !sources.length) usage(1);

  const fail = msg => {
    std.err.puts(`${scriptArgs[0]}: ${msg}\n`);
    std.exit(1);
  };

  const loaded = files.map(file => {
    if(!isGrammar(file)) fail(`${file}: not a grammar file (.bnf .ebnf .g4 .y .jison .l ...)`);

    const [kind, parse] = Grammars[extOf(file)];

    try {
      return { file, kind, ast: parse(readFileSync(file, 'utf-8'), file) };
    } catch(e) {
      return fail(e.message);
    }
  });

  /* the grammar of rules, and where its tokens come from */
  const rules = loaded.find(g => g.kind == 'bnf' || g.kind == 'ebnf' || g.kind == 'yacc' || (g.kind == 'g4' && g.ast.kind != 'lexer' && g.ast.rules.some(r => /^[a-z]/.test(r.name))));

  if(!rules) fail('no grammar with rules among ' + files.join(' '));

  const scannerless = rules.kind == 'bnf' || rules.kind == 'ebnf';
  const cfg = toCFG(rules.ast, rules.kind, { scannerless, start: params.start });

  for(const w of cfg.warnings) std.err.puts(`${scriptArgs[0]}: warning: ${w}\n`);

  const lex = loaded.find(g => g.kind == 'lex')?.ast ?? (rules.kind == 'yacc' && rules.ast.decls.find(d => d.type == 'Lex') ? parseLex(rules.ast.decls.find(d => d.type == 'Lex').text) : null);
  const g4s = loaded.filter(g => g.kind == 'g4').map(g => g.ast);
  const identToken = params['ident-token'] ?? 'IDENTIFIER';
  let tokenize;

  if(scannerless) tokenize = text => charItems(text, !!params['skip-ws']);
  else if(lex) tokenize = (text, file) => lexItems(lex, text, { fileName: file, identToken });
  else if(g4s.length) {
    try {
      tokenize = g4Lexer(g4s, cfg.literals);
    } catch(e) {
      fail(e.message);
    }
  } else fail('no lexer: add a .l file, a jison %lex block or a g4 grammar with lexer rules');

  let earley;

  try {
    earley = new Earley(cfg);
  } catch(e) {
    fail(e.message);
  }

  const opts = { compact: !!params.compact, loc: !params['no-loc'], text: !params['no-text'] };
  const results = [];
  let failed = false;

  for(const file of sources) {
    try {
      const text = readFileSync(file, 'utf-8');
      const items = tokenize(text, file);

      if(cfg.usesEOF) items.push({ type: 'EOF', text: '', pos: text.length, end: text.length });

      try {
        results.push({ file, ast: makeTree(text, items, opts)(earley.parse(items)) });
      } catch(e) {
        if(e.index === undefined) throw e;

        const { line, column } = locator(text)(items[e.index]?.pos ?? text.length);

        throw new SyntaxError(`${line}:${column}: ${e.message.replace(/item \d+/, `'${items[e.index]?.text ?? ''}'`)}`);
      }
    } catch(e) {
      std.err.puts(`${file}:${/^\d+:\d+:/.test(e.message) ? '' : ' '}${e.message}\n`);
      failed = true;
    }
  }

  if(results.length) {
    const json = JSON.stringify(sources.length == 1 ? results[0].ast : results, null, +(params.indent ?? 2)) + '\n';

    if(params.output) writeFileSync(params.output, json);
    else std.out.puts(json);
  }

  std.out.flush();
  if(failed) std.exit(1);
}

main(...scriptArgs.slice(1));
