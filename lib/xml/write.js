/* Escapes '&', '<', '>' (always) and '"' (only when isAttr, matching
 * dbuf_put_escaped_xml() - attribute values here are always double-quoted). Not in
 * lib/lexer/xml.js alongside decodeEntities: escaping is the inverse,
 * serialization-side operation, not a lexical/tokenizing concern - only write.js
 * needs it. */
function escapeXml(str, isAttr) {
  let out = '';

  for(const c of str) {
    if(c == '&') out += '&amp;';
    else if(c == '<') out += '&lt;';
    else if(c == '>') out += '&gt;';
    else if(c == '"' && isAttr) out += '&quot;';
    else out += c;
  }

  return out;
}

/* JS port of quickjs-xml.c's js_xml_write()/xml.write(), mirroring its two
 * serializers exactly rather than re-deriving formatting rules: js_xml_write_tree
 * for a nested {tagName, attributes, children} tree (what lib/xml/read.js
 * produces), and js_xml_write_list for a flat array whose elements carry no
 * `children` property and instead use explicit {tagName: '/x'} end-marker entries.
 * js_xml_write() itself picks between them by checking whether the *last* array
 * entry has an array-valued `children` property. */

function makeBuf() {
  return {
    buf: '',
    put(s) {
      this.buf += s;
    },
    putc(c) {
      this.buf += c;
    },
    /* Mirrors is_whitespace_char()'s trailing-whitespace strip in xml_write_text(). */
    trimTrailingWs() {
      let i = this.buf.length;

      while(i > 0 && ' \t\r\n'.includes(this.buf[i - 1]))
        i--;

      this.buf = this.buf.slice(0, i);
    },
  };
}

function writeIndent(db, depth) {
  while(depth-- > 0)
    db.put('  ');
}

/* Ported from xml_write_attributes(): a value of exactly boolean `true` is written
 * as a bare boolean attribute; everything else (including boolean `false`) gets an
 * ="value" part, stringified and XML-escaped (with '"' escaped too, since attribute
 * values are always double-quoted here). */
function writeAttributes(attributes, db) {
  for(const key in attributes) {
    const value = attributes[key];

    db.putc(' ');
    db.put(key);

    if(value !== true) {
      db.put('="');
      db.put(escapeXml(String(value), true));
      db.putc('"');
    }
  }
}

/* Ported from xml_write_string(): each embedded newline in the text is replaced
 * with a fresh "\n" + indent(depth+1) when depth>0 - re-indenting multi-line text
 * to the current nesting level rather than preserving its original indentation.
 * When depth<=0 (single-child/"inline" text, matching xml_write_text's multiline=
 * false -> depth=0 call), embedded newlines are dropped entirely with no
 * replacement, silently splicing the surrounding lines together - a real quirk of
 * the C implementation, not a simplification, so it's kept here. */
function writeString(text, depth, escape, db) {
  let pos = 0;

  for(;;) {
    const nl = text.indexOf('\n', pos);
    const chunkEnd = nl == -1 ? text.length : nl;
    const chunk = text.slice(pos, chunkEnd);

    db.put(escape ? escapeXml(chunk, false) : chunk);

    pos = nl == -1 ? text.length : nl + 1;

    if(pos >= text.length)
      break;

    if(depth > 0) {
      db.putc('\n');
      writeIndent(db, depth + 1);
    }
  }
}

function writeText(text, db, depth, multiline) {
  if(multiline)
    writeIndent(db, depth);
  else
    db.trimTrailingWs();

  writeString(text, multiline ? depth : 0, true, db);

  if(multiline)
    db.putc('\n');
}

/* Ported from xml_write_element(). A `{tagName: '/x', ...}` end-marker object
 * (only ever produced/consumed by the flat-list format) writes a real closing tag
 * and returns, rather than going through the open-tag/self-closing logic below. */
function writeElement(element, db, depth, selfClosing) {
  const tagName = element.tagName;

  if(!tagName)
    return;

  if(tagName[0] == '/') {
    if(depth > 0)
      writeIndent(db, depth);

    db.put('</');
    db.put(tagName.slice(1));
    db.putc('>');
    db.putc('\n');
    return;
  }

  const attributes = element.attributes;
  const isComment = tagName.startsWith('!--');

  if(depth > 0)
    writeIndent(db, depth);

  db.putc('<');

  if(isComment) {
    db.put(tagName);
  } else if(tagName[0] == '!') {
    db.put(tagName);
  } else {
    db.put(tagName);

    if(attributes && typeof attributes == 'object')
      writeAttributes(attributes, db);
  }

  db.put(tagName[0] == '?' ? '?>' : selfClosing && !(tagName[0] == '!' || isComment) ? ' />' : '>');
  db.putc('\n');
}

/* Ported from xml_close_element(): only elements with a non-empty `children`
 * array get a closing tag written here (self-closing/childless elements already
 * emitted their own end via writeElement's " />"). */
function closeElement(element, db, depth) {
  const numChildren = Array.isArray(element.children) ? element.children.length : -1;

  if(numChildren > 0) {
    const tagName = element.tagName;

    if(tagName && tagName[0] != '?') {
      if(db.buf.length > 0 && db.buf[db.buf.length - 1] == '\n')
        writeIndent(db, depth);

      db.put('</');
      db.put(tagName);
      db.put('>');
      db.putc('\n');
    }
  }
}

/* Ported from js_xml_write_tree(). depth for a value at a given array-nesting
 * level (1 = the top-level root array, 2 = a top-level element's children, ...)
 * is MAX(0, level-2) - i.e. top-level siblings AND their direct children both
 * render unindented, and indentation starts one level deeper than that. Verified
 * against xml.write()'s own output for a 3-deep nested tree rather than derived
 * from reading js_xml_write_tree alone, since that formula isn't obvious from the
 * property-enumeration-stack code it's ported from. */
function writeTree(rootArray, maxDepth, db) {
  const walk = (arr, level) => {
    const depth = Math.max(0, level - 1);
    const multiline = arr.length > 1;

    for(const value of arr) {
      if(typeof value == 'string') {
        writeText(value, db, depth, multiline);
      } else if(value && typeof value == 'object' && !Array.isArray(value)) {
        const numChildren = Array.isArray(value.children) ? value.children.length : -1;

        writeElement(value, db, depth, numChildren <= 0);

        if(numChildren > 0 && (maxDepth == undefined || level + 1 < maxDepth)) {
          walk(value.children, level + 1);
          closeElement(value, db, depth);
        }
      }
    }
  };

  walk(rootArray, 0);

  while(db.buf.length > 0 && (db.buf[db.buf.length - 1] == '\0' || ' \t\r\n'.includes(db.buf[db.buf.length - 1])))
    db.buf = db.buf.slice(0, -1);

  return db.buf;
}

/* Ported from js_xml_write_list(). Self-closes either when the very next list
 * entry is this element's own end marker, or when this is the last entry in the
 * whole list with nothing after it at all. depth only increments after an
 * opening tag literally named "dt" (case-insensitively) - verified directly
 * against xml.write()'s own flat-mode output (other nested tags like <li>/<span>
 * render with no indentation at all), so this is real observed behavior of the C
 * implementation being mirrored here, not a misreading of it. */
function writeList(arr, db) {
  let depth = 0;
  let singleLine = false;
  let value;
  let next = arr[0];
  let tagName;
  let nextTag = next && typeof next == 'object' ? next.tagName : undefined;

  for(let i = 0; i < arr.length; i++) {
    value = next;
    next = arr[i + 1];
    tagName = nextTag;
    nextTag = next && typeof next == 'object' ? next.tagName : undefined;

    if(typeof value == 'string') {
      const singleLineNow = !value.includes('\n');

      singleLine = singleLineNow;
      writeText(value, db, depth, !singleLine);
    } else if(value && typeof value == 'object' && !Array.isArray(value)) {
      const tag = value.tagName;

      if(tag !== undefined) {
        const selfClosing = (nextTag && nextTag[0] == '/' && nextTag.slice(1) == tag) || (tag[0] != '/' && i + 1 >= arr.length);

        if(tag[0] == '/')
          depth--;

        writeElement(value, db, singleLine ? 0 : depth, selfClosing);

        if(selfClosing) {
          i++;
          next = arr[i + 1];
          nextTag = next && typeof next == 'object' ? next.tagName : undefined;
        } else if(tag[0] != '/' && tag[0] != '?' && tag[0] != '!' && tag.toLowerCase() == 'dt') {
          depth++;
        }
      }

      singleLine = false;
    }
  }

  return db.buf;
}

export function write(obj, maxDepth) {
  let arr = obj;

  if(!Array.isArray(arr))
    arr = [obj];

  const last = arr[arr.length - 1];
  const flat = !(last && typeof last == 'object' && Array.isArray(last.children));

  const db = makeBuf();

  return flat ? writeList(arr, db) : writeTree(arr, maxDepth, db);
}

export default write;
