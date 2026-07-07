
import { ParseNode, GrammarRule, Terminal, NonTerminal, Sequence, Alternatives, Optional, ZeroOrMore, OneOrMore, Not, Peek, Mapped, Empty, ParseContext, Grammar, coerce, t, ref, seq, alt, opt, many, some, not, peek, empty, default as grammar } from '../lib/parser/grammar.js'
  import { TryCatch, EBNFParser, buildGrammar, default as ebnf } from '../lib/parser/ebnf.js'
  import { readFileSync } from 'fs';  
  import { Console } from 'console';  


function main(...args) {
  globalThis.console = new Console({
    inspectOptions: {
      depth: 8,
      maxStringLength: Infinity,
      maxArrayLength: 256,
      compact: 2,
      showHidden: false,
      customInspect: false,
    },
  }); 
const grammar=buildGrammar(readFileSync('./tests/Shell-Grammar.y','utf-8'),'./tests/Shell-Grammar.y');


}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error.message}\n${error.stack}`);
  std.exit(1);
} finally {
  console.log('SUCCESS');
}
