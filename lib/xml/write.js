export function toString(o, depth, newline = '\n') {
  if(typeof o == 'string') return o;
  else if(typeof o != 'object') return o + '';
  else if(o.tagName === undefined) return o + '';
  let { tagName, attributes, children, ...obj } = o;
  let s = `<${tagName}`;
  let attrs = attributes || obj;
  for(let k in attrs) {
    let v = attrs[k];
    s += ' ' + k;
    if(v !== true) s += '=' + quote + v + quote;
  }
  const a = children && children.length !== undefined ? children : [];
  if(tagName == '!--') {
    //console.log('o:', o);
    s += children.join('\n');
    s += '-->';
  } else if(a && a.length > 0) {
    s += tagName[0] == '?' ? '?>' : '>';
    const textChildren = typeof a[0] == 'string';
    let nl =
      /*textChildren
        ? '\n' :*/
      /* : tagName == 'text' && a.length == 1
        ? ''*/
      tagName[0] != '?' ? newline + indent : newline;
    if(textChildren) {
      let t = a.join('\n').replace(/\n[ \t]*$/, '');
      s += t + (/\n/.test(t) ? newline : '') + '</' + tagName + `>`;
    } else if(depth > 0) {
      for(let child of a) s += nl + toString(child, depth > 0 ? depth - 1 : depth, nl) /*.replace(/>\n/g, '>' + nl)*/;
      if(tagName[0] != '?') s += `${newline}</${tagName}>`;
    }
  } else {
    if(tagName[0] == '?') s += '?';
    else if(tagName[0] != '!') s += ' /';
    s += '>';
  }
  return s;
}

export const write = process.release.name == 'quickjs' ? require('xml.so').write : toString;

export default write;
