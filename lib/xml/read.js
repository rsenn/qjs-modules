import { XMLLexer } from '../lexer/xml.js';
import { decodeEntities } from './entities.js';

/* JS port of quickjs-xml.c's js_xml_parse()/xml.read(), driven by XMLLexer
 * (lib/lexer/xml.js) instead of hand-rolled pointer arithmetic. Mirrors the C
 * function's tree shape ({tagName, attributes, children}), option surface
 * (flat/tolerant/location/selfClosingTags), and quirks (comments and generic
 * `<!...>` bang-tags captured as their own raw, childless elements; a `<?...?>`
 * processing instruction never self-closes and so becomes a normal open element,
 * same as the C parser - see the live repro this was verified against) - except
 * for BUGS: quickjs-xml-attrname-swallows-slash, which is a bug in the C
 * implementation's attribute-name scan, not a behavior worth reproducing here. */

const DEFAULT_SELF_CLOSING_TAGS = ['area', 'base', 'br', 'col', 'embed', 'hr', 'img', 'input', 'link', 'meta', 'param', 'source', 'track', 'wbr'];

function attrValue(tok) {
  if(tok.type == 'quoted' || tok.type == 'quotedSingle') return tok.lexeme.slice(1, -1);

  return tok.lexeme;
}

/* Ported from js_xml_parse()'s text-run handling (quickjs-xml.c:588-628): a run
 * that's nothing but whitespace is dropped entirely; a run with real content keeps
 * its boundary whitespace verbatim, except for the newline-anchored structural
 * whitespace xml.write()'s pretty-printer glues directly onto content with no
 * separator (see that function's own comment for the full rationale). */
function trimTextRun(raw) {
  const total = raw.length;

  if(/^[ \t\r\n]*$/.test(raw))
    return '';

  let leadStrip = 0;

  if(raw[0] == '\n') {
    const m = raw.match(/^[ \t\r\n]*/);
    leadStrip = m[0].length;
  }

  const lastNl = raw.lastIndexOf('\n');
  const trailStrip = lastNl >= 0 && lastNl >= leadStrip ? total - lastNl : 0;

  return raw.slice(leadStrip, total - trailStrip);
}

export function read(input, inputName, opts, tolerantArg) {
  if(typeof input != 'string') {
    if(input instanceof ArrayBuffer) input = new Uint8Array(input);
    input = new TextDecoder().decode(input);
  }

  let flat = false;
  let tolerant = false;
  let location = false;
  let selfClosingTags = DEFAULT_SELF_CLOSING_TAGS;

  if(opts !== undefined && opts !== null) {
    if(typeof opts == 'object') {
      if('flat' in opts) flat = !!opts.flat;
      if('tolerant' in opts) tolerant = !!opts.tolerant;
      if('location' in opts) location = !!opts.location;
      if(Array.isArray(opts.selfClosingTags)) selfClosingTags = opts.selfClosingTags;
    } else {
      flat = !!opts;
      if(tolerantArg !== undefined) tolerant = !!tolerantArg;
    }
  }

  const isSelfClosingTag = name => selfClosingTags.some(t => t.toLowerCase() == name.toLowerCase());

  const root = [];
  const stack = [{ name: '', arr: root }];
  const locations = location ? new Map() : null;
  const lexer = new XMLLexer(input, inputName);

  const top = () => stack[stack.length - 1];

  const findTag = name => {
    for(let i = stack.length - 1; i >= 0; i--)
      if(stack[i].name == name)
        return i;

    return -1;
  };

  const addChild = (value, tok) => {
    top().arr.push(value);

    if(location && tok)
      locations.set(value, [tok.loc.line, tok.loc.column]);
  };

  const pushText = raw => {
    const text = trimTextRun(raw);

    if(text.length > 0)
      addChild(decodeEntities(text));
  };

  /* "inside script" raw-text mode (quickjs-xml.c:563-587): while the innermost open
   * tag is named "script", content up to the literal `</script>` is scanned as
   * opaque text (XMLLexer's SCRIPT state, entered below) rather than re-tokenized as
   * markup, and yielded one line at a time, each line de-dented by up to the first
   * line's own leading-whitespace amount. */
  const pushScriptRun = raw => {
    const leadingWs = (raw.match(/^[ \t\r\n]*/) || [''])[0].length;
    let pos = 0;

    while(pos < raw.length) {
      let stripped = 0;

      while(stripped < leadingWs && pos < raw.length && / |\t|\r|\n/.test(raw[pos])) {
        pos++;
        stripped++;
      }

      const nl = raw.indexOf('\n', pos);
      const end = nl == -1 ? raw.length : nl + 1;
      const chunk = raw.slice(pos, end);

      if(chunk.length > 0)
        addChild(decodeEntities(chunk));

      pos = end;
    }
  };

  while(!lexer.eof) {
    const tok = lexer.nextToken();

    if(!tok)
      break;

    if(tok.type == 'text') {
      pushText(tok.lexeme);
      continue;
    }

    if(tok.type == 'comment') {
      addChild({ tagName: tok.lexeme.slice(1, -1) }, tok);
      continue;
    }

    if(tok.type == 'bangTag') {
      addChild({ tagName: tok.lexeme.slice(1, -1) }, tok);
      continue;
    }

    if(tok.type == 'tagStart' || tok.type == 'closeTagStart') {
      const closing = tok.type == 'closeTagStart';
      const nameTok = lexer.nextToken();
      const name = nameTok ? nameTok.lexeme : '';

      if(closing) {
        let t;

        while((t = lexer.nextToken()) && t.type != 'gt') {}

        if(flat) {
          addChild({ tagName: '/' + name });
        } else {
          const idx = findTag(name);

          if(idx == -1) {
            if(!tolerant)
              throw new SyntaxError(`mismatch </${name}>` + (inputName ? ` at ${inputName}` : ''));
          } else {
            stack.length = idx;
          }
        }

        continue;
      }

      let selfClosing = isSelfClosingTag(name);
      const el = { tagName: name };

      addChild(el, tok);

      const attributes = {};

      el.attributes = attributes;

      let sawSlash = false;
      let t;

      /* peekToken()/back() don't compose reliably with rule actions and state
       * transitions in practice (verified against a standalone repro) - a
       * single-token buffer filled purely from sequential nextToken() calls avoids
       * that entirely and is all the lookahead this grammar needs (attrName,
       * optionally followed by '='). */
      let pending = null;
      const readTok = () => {
        if(pending) {
          const p = pending;

          pending = null;
          return p;
        }

        return lexer.nextToken();
      };

      for(;;) {
        t = readTok();

        if(!t || t.type == 'gt')
          break;

        if(t.type == 'slash') {
          sawSlash = true;
          continue;
        }

        if(t.type == 'question')
          continue;

        if(t.type == 'attrName' || t.type == 'quoted' || t.type == 'quotedSingle') {
          const attrName = t.lexeme;
          const t2 = readTok();

          if(t2 && t2.type == 'eq') {
            const valTok = readTok();

            attributes[attrName] = valTok ? decodeEntities(attrValue(valTok)) : '';
          } else {
            attributes[attrName] = true;

            if(t2)
              pending = t2;
          }
        }
      }

      if(sawSlash)
        selfClosing = true;

      if(!flat && !selfClosing) {
        const children = [];

        el.children = children;
        stack.push({ name, arr: children });
      }

      if(selfClosing && flat)
        addChild({ tagName: '/' + name });

      /* "inside script" raw-text mode (quickjs-xml.c:563-587): read exactly once,
       * right after the open tag, rather than re-checked every loop iteration - the
       * lookahead-based scriptText rule matches zero characters once already
       * sitting at `</script>`, which would otherwise never make progress. */
      if(name == 'script' && !selfClosing) {
        lexer.state = 'SCRIPT';

        const scriptTok = lexer.nextToken();

        lexer.state = 'INITIAL';

        /* No </script> anywhere in the rest of the input - matches the C loop's
         * own behavior of running to the end of the buffer. */
        if(!scriptTok)
          break;

        pushScriptRun(scriptTok.lexeme);
      }

      continue;
    }
  }

  if(location)
    return [root, locations];

  return root;
}

export default read;
