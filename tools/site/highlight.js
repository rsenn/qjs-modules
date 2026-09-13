/**
 * Tiny syntax highlighter for the three languages the docs use in fenced
 * blocks (js, sh, c). Not a parser: a longest-first scanner over a small
 * ordered rule list per language. Anything unmatched is emitted as escaped
 * text, so the worst failure mode is "less colour", never broken HTML.
 *
 * Runs under qjsm; no dependencies.
 */

const esc = s => s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');

const kw = words => new RegExp('\\b(?:' + words.join('|') + ')\\b');

const JS_KEYWORDS = [
  'async', 'await', 'break', 'case', 'catch', 'class', 'const', 'continue', 'default',
  'delete', 'do', 'else', 'export', 'extends', 'finally', 'for', 'from', 'function',
  'get', 'if', 'import', 'in', 'instanceof', 'let', 'new', 'of', 'return', 'set',
  'static', 'super', 'switch', 'this', 'throw', 'try', 'typeof', 'var', 'void',
  'while', 'yield',
];

const JS_ATOMS = ['true', 'false', 'null', 'undefined', 'NaN', 'Infinity'];

const C_KEYWORDS = [
  'break', 'case', 'const', 'continue', 'default', 'do', 'else', 'enum', 'extern',
  'for', 'goto', 'if', 'inline', 'register', 'return', 'sizeof', 'static', 'struct',
  'switch', 'typedef', 'union', 'volatile', 'while',
];

const C_TYPES = [
  'char', 'double', 'float', 'int', 'long', 'short', 'signed', 'unsigned', 'void',
  'size_t', 'ssize_t', 'uint8_t', 'uint16_t', 'uint32_t', 'uint64_t', 'int64_t', 'bool',
];

const SH_KEYWORDS = [
  'case', 'do', 'done', 'elif', 'else', 'esac', 'fi', 'for', 'function', 'if', 'in',
  'then', 'until', 'while', 'export', 'local', 'return', 'source',
];

/* Rules are tried in order at each position; first match wins. */
const LANGS = {
  js: [
    ['cm', /\/\/[^\n]*/],
    ['cm', /\/\*[\s\S]*?\*\//],
    ['str', /`(?:\\[\s\S]|[^`\\])*`/],
    ['str', /'(?:\\[\s\S]|[^'\\\n])*'/],
    ['str', /"(?:\\[\s\S]|[^"\\\n])*"/],
    ['num', /\b0[xX][0-9a-fA-F_]+n?\b|\b\d[\d_]*(?:\.\d[\d_]*)?(?:[eE][+-]?\d+)?n?\b/],
    ['atom', kw(JS_ATOMS)],
    ['kw', kw(JS_KEYWORDS)],
    ['fn', /\b[A-Za-z_$][\w$]*(?=\s*\()/],
    ['type', /\b[A-Z][\w$]*\b/],
    ['prop', /(?<=\.)[A-Za-z_$][\w$]*/],
    ['id', /\b[A-Za-z_$][\w$]*\b/],
    ['op', /[=!<>+\-*/%&|^~?:]+|=>/],
  ],
  c: [
    ['cm', /\/\/[^\n]*/],
    ['cm', /\/\*[\s\S]*?\*\//],
    ['pre', /^[ \t]*#[ \t]*\w+/],
    ['str', /"(?:\\[\s\S]|[^"\\\n])*"/],
    ['str', /'(?:\\[\s\S]|[^'\\\n])*'/],
    ['num', /\b0[xX][0-9a-fA-F]+[uUlL]*\b|\b\d+(?:\.\d+)?[uUlLfF]*\b/],
    ['kw', kw(C_KEYWORDS)],
    ['type', kw(C_TYPES)],
    ['type', /\b[A-Za-z_]\w*_t\b|\bLWS[A-Z_]*\b/],
    ['fn', /\b[A-Za-z_]\w*(?=\s*\()/],
    ['id', /\b[A-Za-z_]\w*\b/],
    ['op', /[=!<>+\-*/%&|^~?:]+/],
  ],
  sh: [
    ['cm', /#[^\n]*/],
    ['str', /"(?:\\[\s\S]|[^"\\])*"/],
    ['str', /'[^']*'/],
    ['var', /\$\{[^}]*\}|\$\w+/],
    ['op', /--?[A-Za-z][\w-]*/],
    ['kw', kw(SH_KEYWORDS)],
    ['num', /\b\d+\b/],
    ['id', /\b[A-Za-z_][\w.-]*\b/],
  ],
};

const ALIASES = { javascript: 'js', jsx: 'js', mjs: 'js', shell: 'sh', bash: 'sh', console: 'sh', h: 'c', cpp: 'c' };

/** Anchor a rule at the scan position without mutating the shared literal. */
function sticky(re) {
  return new RegExp(re.source, re.flags.replace(/[gy]/g, '') + 'y');
}

const compiled = new Map();

function rulesFor(lang) {
  if (!compiled.has(lang))
    compiled.set(lang, LANGS[lang].map(([cls, re]) => [cls, sticky(re)]));
  return compiled.get(lang);
}

/**
 * @param {string} code raw source text
 * @param {string} lang fence info string ('js', 'sh', 'c', aliases, or '')
 * @returns {string} HTML with token spans; escaped text if the language is unknown
 */
export function highlight(code, lang) {
  const key = ALIASES[lang] || lang;
  if (!LANGS[key]) return esc(code);

  const rules = rulesFor(key);
  let out = '', plain = '', i = 0;

  while (i < code.length) {
    let hit = null;
    for (const [cls, re] of rules) {
      re.lastIndex = i;
      const m = re.exec(code);
      if (m && m[0]) { hit = [cls, m[0]]; break; }
    }
    if (!hit) { plain += code[i++]; continue; }
    if (plain) { out += esc(plain); plain = ''; }
    out += '<span class="t-' + hit[0] + '">' + esc(hit[1]) + '</span>';
    i += hit[1].length;
  }

  return out + esc(plain);
}
