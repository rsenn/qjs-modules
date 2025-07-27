function isIterator(v) {
  return typeof v == 'object' && typeof v.next == 'function';
}

const WS = 0x01;
const START = 0x02;
const END = 0x04;
const QUOTE = 0x08;
const CLOSE = 0x10;
const EQUAL = 0x20;
const SPECIAL = 0x40;
const SLASH = 0x80;
const BACKSLASH = 0x100;
const QUESTION = 0x200;
const EXCLAM = 0x400;
const HYPHEN = 0x800;

const CharacterClasses = {
  ' ': WS,
  '\t': WS,
  '\r': WS,
  '\n': WS,
  '!': SPECIAL | EXCLAM,
  '"': QUOTE,
  '/': END | SLASH,
  '<': START,
  '=': EQUAL,
  '>': END | CLOSE,
  '?': SPECIAL | QUESTION,
  '\\': BACKSLASH,
  '-': HYPHEN,
};
const CharCodeClasses = {
  0x20: WS,
  0x09: WS,
  0x0d: WS,
  0x0a: WS,
  0x21: SPECIAL | EXCLAM,
  0x22: QUOTE,
  0x2f: END | SLASH,
  0x3c: START,
  0x3d: EQUAL,
  0x3e: END | CLOSE,
  0x3f: SPECIAL | QUESTION,
  0x5c: BACKSLASH,
  0x2d: HYPHEN,
};

export function parse(s) {
  if(typeof s != 'string') {
    if(s instanceof ArrayBuffer) s = new Uint8Array(s);
  }

  let i = 0,
    n = s.length,
    r = [];
  let st = [r],
    e;
  const m = typeof s == 'string' ? CharacterClasses : CharCodeClasses;
  const codeAt = typeof s == 'string' ? i => s.codePointAt(i) : i => s[i];
  const range = typeof s == 'string' ? (i, j) => s.substring(i, j) : (i, j) => [...s.slice(i, j)].reduce((a, c) => a + String.fromCharCode(c), '');
  const start = tagName => {
    e = { tagName, attributes: {}, children: [] };
    st[0].push(e);
    st.unshift(e.children);
  };
  const end = tagName => {
    st.shift();
    e = null;
  };
  const skip = pred => {
    let k;
    for(k = i; pred(s[k]); ) k++;
    return k;
  };
  const skipws = () => {
    while(m[s[i]] & WS) i++;
  };

  while(i < n) {
    let j;

    for(j = i; (m[s[j]] & START) == 0; ) j++;

    if(j > i) {
      const data = range(i, j);
      if(data.trim() != '') st[0].push(data);
    }

    i = j;

    if(m[s[i]] & START) {
      let closing = false;
      i++;

      if(m[s[i]] & SLASH) {
        closing = true;
        i++;
      }

      //console.log('#1', { i, n,closing }, `'${range(i,i+1)}'`, `c=${codeAt(i)}`);
      j = skip(c => (m[c] & (WS | END)) == 0);
      let name = range(i, j);
      i = j;

      if(!closing) {
        start(name);

        for(;;) {
          skipws();

          if(m[s[i]] & END) break;

          j = skip(c => (m[c] & (EQUAL | WS | SPECIAL | CLOSE)) == 0);

          if(j == i) break;

          let attr = range(i, j);
          let value = true;

          i = j;

          if(m[s[i]] & EQUAL && m[s[i + 1]] & QUOTE) {
            //console.log('#2', { i, name }, `'${range(i,i+1)}'`, `"${range(i, i + 20)}..."`);

            i += 2;

            for(j = i; (m[s[j]] & QUOTE) == 0; j++) if(m[s[j]] & BACKSLASH) j++;

            value = range(i, j);

            if(m[s[j]] & QUOTE) j++;

            i = j;
          }

          e.attributes[attr] = value;
          //console.log('#3', { attr, value });
        }
      } else end(name);

      //console.log('#4', { i, j }, `'${range(i,i+1)}'`, `"${range(i, i + 10)}..."`);

      if(name[0] == '!') end(name);

      if(name[0] == '?' && m[s[i]] & QUESTION) {
        i++;
      } else if(m[s[i]] & SLASH) {
        i++;
        end(name);
      }

      skipws();

      if(m[s[i]] & CLOSE) i++;

      skipws();
      //console.log('#5', { i,  n }, `'${range(i,i+1)}'`, `"${range(i, i + 10)}..."`);
    }
  }

  //console.log('#6', { i,  n } , r);
  return r;
}

export function read(g) {
  let s = g;

  if(!g[Symbol.iterator]) g = new Uint8Array(g);
  if(!isIterator(g)) g = g[Symbol.iterator]();

  let r = [];
  let st = [r],
    e;
  const m = typeof s == 'string' ? CharacterClasses : CharCodeClasses;
  const codeAt = typeof s == 'string' ? i => s.codePointAt(i) : i => g[i];
  let c,
    done,
    data = typeof s == 'string' ? '' : [];
  const start = tagName => {
    e = { tagName, attributes: {}, children: [] };
    st[0].push(e);
    st.unshift(e.children);
  };
  const end = tagName => {
    st.shift();
    e = null;
  };
  const add = typeof s == 'string' ? c => (data += c) : c => data.push(c);
  const next = () => {
    let it = g.next();
    if(!(done = it.done)) c = it.value;
    return !done;
  };
  const is = (ch, cl) => m[ch] & cl;
  const str = typeof s == 'string' ? s => s : toString;
  const skip = pred => {
    data = [];

    while(!done) {
      if(m[c] & BACKSLASH) {
        add(c);
        next();
      } else if(!pred(c)) {
        break;
      }

      add(c);
      next();
    }

    return data;
  };
  const skipws = () => {
    while(m[c] & WS) next();
  };

  next();

  while(!done) {
    let text = skip(c => (m[c] & START) == 0);

    if(text.length) {
      let s = str(text);
      if(s.trim() != '') st[0].push(s);
    }

    //console.log("ST", {c}, String.fromCodePoint(c));

    if(m[c] & START) {
      let closing = false;
      next();

      if(m[c] & SLASH) {
        closing = true;
        next();
      }

      //console.log("BN", {c}, String.fromCodePoint(c), m[c]);
      let name = skip(c => (m[c] & (WS | END)) == 0);
      add(c);
      //console.log("AN", {name,closing});

      if(m[name[0]] & EXCLAM && m[name[1]] & HYPHEN && m[name[2]] & HYPHEN) {
        while(next()) {
          add(c);

          if(is(data[data.length - 3], HYPHEN) && is(data[data.length - 2], HYPHEN) && is(data[data.length - 1], CLOSE)) break;
        }

        //console.log("data:", data);
        start(str(data.slice(0, 3)));
        e.children = [data.slice(3, -3)];
        end();
        next();
      } else {
        if(!closing) {
          start(str(name));

          while(!done) {
            skipws();
            //console.log("NC",  {c}, String.fromCodePoint(c), m[c] & END);

            if(m[c] & END) break;

            let attr = skip(c => (m[c] & (EQUAL | WS | SPECIAL | CLOSE)) == 0);
            //console.log("AT",  {c,attr: str(attr)}, String.fromCodePoint(c), m[c] & END);

            if(attr.length == 0) break;

            let value = true;

            if(m[c] & EQUAL && next() && m[c] & QUOTE) {
              next();
              //
              value = skip(c => (m[c] & QUOTE) == 0);
              if(m[c] & QUOTE) next();
            }

            e.attributes[str(attr)] = str(value);
          }
        } else end();

        if(!closing) {
          if(m[name[0]] & EXCLAM) {
            end(str(name));
          } else if(m[name[0]] & QUESTION && m[c] & QUESTION) {
            next();
          } else if(m[c] & SLASH) {
            next();
            end();
          }
        }

        skipws();
        //console.log("CL", {c}, String.fromCodePoint(c), m[c]& CLOSE, done);

        if(m[c] & CLOSE) next();
      }
      //console.log("END", {c}, escape(String.fromCodePoint(c)),  done);
    }
  }

  return r;
}
