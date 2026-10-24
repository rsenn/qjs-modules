function loadColorScheme(path) {
  const content = fs.readFileSync(path, 'utf-8');
  return requireModule('json5').parse(content);
}

function resolveColor(val, vars) {
  if(!val) return;
  const m = val.match(/^var\(([^)]+)\)$/);
  return m ? (vars[m[1].trim()] ? resolveColor(vars[m[1].trim()], vars) : val) : val;
}

function createColorMatcher(scheme) {
  const vars = scheme.variables || {};
  const rules = (scheme.rules || []).map(r => ({
    name: r.name || 'Unnamed',
    scopes: r.scope ? r.scope.split(',').map(s => s.trim().split(/\s+/)) : [],
    fg: resolveColor(r.foreground, vars),
    bg: resolveColor(r.background, vars),
    style: r.font_style,
  }));

  function matchToken(target, selector) {
    if(target === selector) return true;
    const t = target.split('.');
    const s = selector.split('.');
    if(s.length > t.length) return false;
    for(let i = 0; i < s.length; i++) {
      if(t[i] !== s[i]) return false;
    }
    return true;
  }

  function matchSelector(targets, chain) {
    const tList = Array.isArray(targets) ? targets : targets.split(/\s*,?\s+/);
    let i = tList.length - 1;
    let j = chain.length - 1;

    while(i >= 0 && j >= 0) {
      if(matchToken(tList[i], chain[j])) j--;
      i--;
    }
    return j < 0;
  }

  return {
    lookup(scope) {
      const a = [];
      for(const r of rules) {
        const m = { name: r.name };
        if(r.fg) m.fg = r.fg;
        if(r.bg) m.bg = r.bg;
        if(r.style) m.style = r.style;

        //console.log(`scopes \x1b[1;33m${r.name}\x1b[0m`,console.config({compact:-2}), r.scopes);

        for(const chain of r.scopes) {
          if(matchSelector(scope, chain)) {
            a.push({ ...m, selector: chain.join(' ') });
            break;
          }
        }
      }
      return a;
    },

    resolve(scope) {
      const a = this.lookup(scope);
      return a.length ? a[a.length - 1] : null;
    },
  };
}
