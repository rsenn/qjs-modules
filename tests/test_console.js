import Console from 'console';
import * as std from 'std';

function main(...args) {
  globalThis.console = new Console({
    inspectOptions: {
      showHidden: false,
      showProxy: false,
      stringBreakNewline: true,
      maxStringLength: Infinity,
      compact: 1,
      numberBase: 16,
    },
  });

  const testString =
    '/*\n * [The "BSD license"]\n *  Copyright (c) 2012-2015 Terence Parr\n *  Copyright (c) 2012-2015 Sam Harwell\n *  Copyright (c) 2015 Gerald Rosenberg\n *  All rights reserved.\n *\n *  Redistribution and use in source and binary forms, with or without\n *  modification, are permitted provided that the following conditions\n *  are met:\n *\n *  1. Redistributions of source code must retain the above copyright\n *     notice, this list of conditions and the following disclaimer.\n *  2. Redistributions in binary form must reproduce the above copyright\n *     notice, this list of conditions and the following disclaimer in the\n *     documentation and/or other materials provided with the distribution.\n *  3. The name of the author may not be used to endorse or promote products\n *     derived from this software without specific prior written permission.\n *\n *  THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS\'\' AND ANY EXPRESS OR\n *  IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES\n *  OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.\n *  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,\n *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT\n *  NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,\n *  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY\n *  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT\n *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF\n *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.\n */\n/**\n *\tA grammar for ANTLR v4 implemented using v4 syntax\n *\n *\tModified 2015.06.16 gbr\n *\t-- update for compatibility with Antlr v4.5\n */\n\n// ======================================================\n// Lexer specification\n// ======================================================\n\nlexer grammar ANTLRv4Lexer;\n\noptions { superClass = LexerAdaptor; }\nimport LexBasic;\n\n// Standard set of fragments\ntokens { TOKEN_REF , RULE_REF , LEXER_CHAR_SET }\nchannels { OFF_CHANNEL , COMMENT }\n\n// -------------------------\n// Comments\nDOC_COMMENT\n   : DocComment -> channel (COMMENT)\n   ;\n\nBLOCK_COMMENT\n   : BlockComment -> channel (COMMENT)\n   ;\n\nLINE_COMMENT\n   : LineComment -> channel (COMMENT)\n   ;\n\n// -------------------------\n// Integer\n\nINT\n   : DecimalNumeral\n   ;\n\n// -------------------------\n// Literal string\n//\n// ANTLR makes no distinction between a single character literal and a\n// multi-character string. All literals are single quote delimited and\n';
  console.log('console.options:', console.options);
  console.log('test:', { testString });

  const testObject = {
    numb: 1234,
    str: 'blah',
  };

  testObject.circular = { testObject };

  console.log('testObject:', testObject);

  std.gc();
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  std.err.puts(`FAIL: ${error.message}\n${error.stack}`);
  std.exit(1);
} finally {
  console.log('SUCCESS');
}
