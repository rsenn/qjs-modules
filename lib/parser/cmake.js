import { CMakeLexer } from '../lexer/cmake.js';
import { Lexer, Token } from 'lexer';

export class CMakeParser {
  constructor(input, filename) {
    this.lexer = new CMakeLexer(input, Lexer.LONGEST, filename);
  }

  /**
   * Parses the CMake script into an AST using O(1) integer-based FSM token dispatch.
   * @returns {Object} The root AST node of type 'File'
   */
  parse() {
    const ast = {
      type: 'File',
      body: [],
    };

    /* Destructure rule IDs once for the entire parse session */
    const { whitespace, newline, lineComment, bracketComment, identifier, unquotedArgument, quotedArgument, bracketArgument, variableRef, lparen, rparen } = this.lexer.rules;

    /* Defined inside parse() to share the destructured lexical rule IDs via closure */
    const parseCommand = () => {
      const cmdToken = this.lexer.nextToken();
      const name = cmdToken.lexeme;
      const args = [];

      /* Expect opening parenthesis '(' */
      const nextToken = this.lexer.peekToken();
      if(!nextToken || nextToken.id !== lparen) {
        /* If there's no opening parenthesis, it's not a standard command invocation */
        return null;
      }
      this.lexer.nextToken(); /* consume '(' */

      while(!this.lexer.eof) {
        const token = this.lexer.peekToken();
        if(!token) break;

        switch (token.id) {
          case rparen:
            this.lexer.nextToken(); /* consume ')' */
            return {
              type: 'CommandInvocation',
              name: name,
              arguments: args,
            };

          case whitespace:
          case newline:
          case lineComment:
          case bracketComment:
            this.lexer.nextToken();
            break;

          case identifier:
          case unquotedArgument:
          case quotedArgument:
          case bracketArgument:
          case variableRef:
            args.push({
              type: token.type,
              value: token.lexeme,
              loc: token.loc ? token.loc.toString() : null,
            });
            this.lexer.nextToken();
            break;

          default:
            args.push({
              type: token.type || 'unknown',
              value: token.lexeme,
            });
            this.lexer.nextToken();
            break;
        }
      }

      return {
        type: 'CommandInvocation',
        name: name,
        arguments: args,
      };
    };

    while(!this.lexer.eof) {
      const token = this.lexer.peekToken();
      if(!token) break;

      switch (token.id) {
        case whitespace:
        case newline:
        case lineComment:
        case bracketComment:
          this.lexer.nextToken();
          break;

        case identifier:
        case unquotedArgument: {
          const cmd = parseCommand();
          if(cmd) ast.body.push(cmd);
          break;
        }

        default:
          /* Skip unexpected tokens safely */
          this.lexer.nextToken();
          break;
      }
    }

    return ast;
  }
}

export default CMakeParser;
