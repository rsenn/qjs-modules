import { Lexer } from 'lexer';

export class CSVLexer extends Lexer {
  constructor(input, filename, separator = ',') {
    super(input, Lexer.FIRST, filename);

    this.mask = -1;
    this.skip = 0;

    this.handler = (arg, tok) => console.log(`Unmatched token at ${arg.loc}\narg.currentLine()\n${' '.repeat(arg.loc.column - 1)}^`);

    this.addRules(separator);
  }

  addRules(separator = ',') {
    this.define('Text', /[^,\n\r"]+/g);
    this.define('String', /"(""|[^"])*"/g);
    this.define('Separator', separator);

    this.addRule('field', /({Text}|{String})/);
    this.addRule('separator', /{Separator}/);
    this.addRule('nl', /\r?\n/);
  }

  /* prettier-ignore */ get [Symbol.toStringTag]() {
    return "CSVLexer";
  }
}

globalThis.CSVLexer = CSVLexer;

export default CSVLexer;
