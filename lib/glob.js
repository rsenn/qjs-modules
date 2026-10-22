/* glob.js: glob patterns as Bun.Glob has them, match() and a directory scan.
 * depends on: fs (scan only), path.
 * rule: match() compares strings only, dot files and the file system
 * matter to scanSync() and scan() alone.
 *
 *   new Glob('src/**').match('src/a/b.ts')               // true
 *   [...new Glob('*.js').scanSync({ cwd: 'lib' })]       // ['a.js', ...]
 *
 * | pattern  | matches                                         |
 * | -------- | ----------------------------------------------- |
 * | *        | any characters except /                         |
 * | ?        | one character except /                          |
 * | [ab] [!a] [^a] [a-c] | one character of the set, or not in it |
 * | {a,b}    | either alternative, nests                       |
 * | **       | any number of path segments, also none          |
 * | !p       | leading: everything p does not match            |
 * | \x       | x literally                                     |
 */
import { readdirSync, statSync } from 'fs';
import { join, resolve } from 'path';

const escapeRe = s => s.replace(/[.+^${}()|[\]\\*?/]/g, '\\$&');

/* the RegExp source of pattern p from index i up to an unnested stop character */
function convert(p, i, stops) {
  let re = '';

  while(i < p.length && !stops.includes(p[i])) {
    const c = p[i];

    if(c == '\\' && i + 1 < p.length) {
      re += escapeRe(p[i + 1]);
      i += 2;
    } else if(c == '*' && p[i + 1] == '*' && (i == 0 || '/{,'.includes(p[i - 1])) && (i + 2 == p.length || '/},'.includes(p[i + 2]))) {
      /* a whole double-star segment: followed by a slash it also matches no
       * segment at all, at the end of the pattern it needs one more segment */
      if(p[i + 2] == '/') {
        re += '(?:.*/)?';
        i += 3;
      } else {
        re += '.*';
        i += 2;
      }
    } else if(c == '*') {
      re += '[^/]*';
      i++;
    } else if(c == '?') {
      re += '[^/]';
      i++;
    } else if(c == '[' && p.indexOf(']', i + 2) > i) {
      const end = p.indexOf(']', i + 2);
      let set = p.slice(i + 1, end);
      const negated = set[0] == '!' || set[0] == '^';

      if(negated) set = set.slice(1);

      re += '[' + (negated ? '^/' : '') + set.replace(/[\\\]^]/g, '\\$&') + ']';
      i = end + 1;
    } else if(c == '{') {
      const alts = [];

      i++;

      for(;;) {
        const [alt, j] = convert(p, i, ',}');

        alts.push(alt);
        i = j + 1;

        if(p[j] != ',') break;
      }

      re += '(?:' + alts.join('|') + ')';
    } else {
      re += escapeRe(c);
      i++;
    }
  }

  return [re, i];
}

/* globToRegExp(pattern): { re, negated }, re anchored at both ends */
export function globToRegExp(pattern) {
  const negated = pattern[0] == '!';
  const [src] = convert(negated ? pattern.slice(1) : pattern, 0, '');

  return { re: new RegExp('^' + src + '$', 's'), negated };
}

export class Glob {
  #pattern;
  #re;
  #negated;

  constructor(pattern) {
    if(typeof pattern != 'string') throw new TypeError('Glob expected a string pattern');

    this.#pattern = pattern;
    ({ re: this.#re, negated: this.#negated } = globToRegExp(pattern));
  }

  /* true when path, a string with / separators, fits the pattern */
  match(path) {
    return this.#re.test(String(path)) != this.#negated;
  }

  /* scanSync(options | cwd): the paths under cwd that match, relative unless absolute
   *
   *   cwd         where to start, default the current directory
   *   dot         also files and directories whose name starts with a dot
   *   absolute    absolute paths instead of relative ones
   *   onlyFiles   leave directories out, default true
   *   followSymlinks  a symlink to a directory is walked into
   */
  *scanSync(options = {}) {
    if(typeof options == 'string') options = { cwd: options };

    const { cwd = '.', dot = false, absolute = false, onlyFiles = true, followSymlinks = false } = options;
    const base = resolve(cwd);
    const dotAllowed = dot || /(^|\/)\./.test(this.#pattern);
    const maxDepth = this.#pattern.includes('**') || this.#negated ? Infinity : this.#pattern.split('/').length;
    const self = this;

    function* walk(dir, rel, depth) {
      let entries;

      try {
        entries = readdirSync(dir, { withFileTypes: true });
      } catch(e) {
        return;
      }

      for(const ent of entries) {
        if(!dotAllowed && ent.name[0] == '.') continue;

        const path = rel ? rel + '/' + ent.name : ent.name;
        let isDir = ent.isDirectory();

        if(!isDir && followSymlinks && ent.isSymbolicLink())
          try {
            isDir = statSync(join(dir, ent.name)).isDirectory();
          } catch(e) {}

        if((!onlyFiles || !isDir) && self.match(path)) yield absolute ? join(base, path) : path;

        if(isDir && depth < maxDepth) yield* walk(join(dir, ent.name), path, depth + 1);
      }
    }

    yield* walk(base, '', 1);
  }

  /* scan(options | cwd): scanSync() as an async iterator */
  async *scan(options) {
    yield* this.scanSync(options);
  }
}

export default Glob;
