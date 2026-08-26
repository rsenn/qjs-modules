import CLexer from 'lexer/c.js';

const SKIP_TYPES = new Set(['whitespace', 'singleLineComment', 'multiLineComment', 'preprocessor']);

/**
 * Skips a balanced '(' ... ')' group starting at toks[i] (which must be an lparen).
 * Returns the index just past the matching rparen, or -1 if unbalanced.
 */
function skipParens(toks, i) {
  let depth = 0;

  do {
    if(toks[i].type == 'lparen') depth++;
    else if(toks[i].type == 'rparen') depth--;
    i++;
  } while(i < toks.length && depth > 0);

  return depth == 0 ? i : -1;
}

/**
 * Skips trailing attribute-like annotations between a function's ')' and its body/';',
 * e.g. `__attribute__((noreturn))`. Also tolerates 'const'/'noexcept'-style qualifier
 * keywords, in case this ever sees C++ input.
 */
function skipQualifiers(toks, i) {
  for(;;) {
    if(toks[i]?.type == 'const') {
      i++;
      continue;
    }

    if(toks[i]?.type == 'identifier' && toks[i + 1]?.type == 'lparen') {
      const j = skipParens(toks, i + 1);
      if(j == -1) break;
      i = j;
      continue;
    }

    break;
  }

  return i;
}

/**
 * Walks backward from the function-name token at toks[i] to the start of its full
 * declaration (return type + qualifiers), i.e. back to just after the previous
 * top-level ';' or '}', or the start of the token stream.
 */
function findDeclStart(toks, i) {
  let k = i - 1;
  while(k >= 0 && toks[k].type != 'rbrace' && toks[k].type != 'semi') k--;
  return k + 1;
}

function tokenize(source, filename) {
  const lexer = new CLexer(source, undefined, filename);
  const toks = [];

  let tok;
  while((tok = lexer.nextToken())) if(!SKIP_TYPES.has(tok.type)) toks.push(tok);

  return toks;
}

/**
 * Tokenizes C source and finds every top-level (brace-depth 0) construct of the shape
 * `name '(' ... ')' [qualifiers]` via paren/brace balancing - no preprocessing, so
 * macro-generated functions aren't recognized (they're simply absent from the result,
 * not misreported). Splits them into function *definitions* (followed by a '{' body)
 * and *prototypes* (followed by a ';').
 *
 * `declStartOffset`/`declStartLine` mark the start of the whole declaration (return
 * type onward), not just the name - that's the span a caller doing surgical removal
 * of the function/prototype should actually delete. `endOffset`/`endLine` mark the
 * end of the body ('}') or the terminating ';', respectively.
 *
 * @param {string} source
 * @returns {{definitions: Array<object>, prototypes: Array<object>}}
 */
function scanTopLevel(source, filename) {
  const toks = tokenize(source, filename);

  const definitions = [];
  const prototypes = [];
  let depth = 0;

  for(let i = 0; i < toks.length; i++) {
    const tok = toks[i];

    if(tok.type == 'lbrace') {
      depth++;
      continue;
    }

    if(tok.type == 'rbrace') {
      depth--;
      continue;
    }

    // A leading `__attribute__((...))` (or MSVC `__declspec(...)`) before the return
    // type/name would otherwise look just like a `name '(' ... ')'` definition itself,
    // with the *real* name+paramlist folded in as skipQualifiers()'s trailing-attribute
    // case - swallowing the actual function under the wrong name. Skip it as a bare
    // prefix instead, so the next iteration reaches the real name normally.
    if(depth == 0 && (tok.lexeme == '__attribute__' || tok.lexeme == '__declspec') && toks[i + 1]?.type == 'lparen') {
      const after = skipParens(toks, i + 1);
      if(after != -1) {
        i = after - 1;
        continue;
      }
    }

    if(depth == 0 && tok.type == 'identifier' && toks[i + 1]?.type == 'lparen') {
      const afterParams = skipParens(toks, i + 1);

      if(afterParams != -1) {
        const afterQual = skipQualifiers(toks, afterParams);
        const declStart = toks[findDeclStart(toks, i)];

        if(toks[afterQual]?.type == 'lbrace') {
          let d = 1,
            j = afterQual + 1;

          while(j < toks.length && d > 0) {
            if(toks[j].type == 'lbrace') d++;
            else if(toks[j].type == 'rbrace') d--;
            j++;
          }

          const end = toks[j - 1];

          definitions.push({
            name: tok.lexeme,
            startLine: tok.loc.line,
            endLine: end.loc.line,
            startOffset: tok.charPos,
            endOffset: end.charPos + end.charLength,
            declStartOffset: declStart.charPos,
            declStartLine: declStart.loc.line,
          });

          i = j - 1;
          continue;
        }

        if(toks[afterQual]?.type == 'semi') {
          prototypes.push({
            name: tok.lexeme,
            startLine: tok.loc.line,
            endLine: toks[afterQual].loc.line,
            startOffset: tok.charPos,
            endOffset: toks[afterQual].charPos + toks[afterQual].charLength,
            declStartOffset: declStart.charPos,
            declStartLine: declStart.loc.line,
          });

          i = afterQual;
          continue;
        }
      }
    }
  }

  return { definitions, prototypes };
}

/**
 * @param {string} source
 * @returns {Array<{name: string, startLine: number, endLine: number, startOffset: number, endOffset: number, declStartOffset: number, declStartLine: number}>}
 */
export function findFunctionDefinitions(source, filename) {
  return scanTopLevel(source, filename).definitions;
}

/**
 * @param {string} source
 * @returns {Array<{name: string, startLine: number, endLine: number, startOffset: number, endOffset: number, declStartOffset: number, declStartLine: number}>}
 */
export function findFunctionPrototypes(source, filename) {
  return scanTopLevel(source, filename).prototypes;
}

export default findFunctionDefinitions;
