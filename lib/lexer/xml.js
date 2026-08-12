import { define } from 'util';
import { Lexer } from 'lexer';

/* Tokenizes the structural grammar of quickjs-xml.c's js_xml_parse(): text runs,
 * comments, bang-tags (<!DOCTYPE ...>, captured raw - this parser has no real CDATA
 * support, matching the C implementation), and open/close tag boundaries. Tag names
 * and attribute names use two different stop-sets in the C code (a tag name only
 * stops at whitespace/'/'/'>' so it can start with '!' or '?'; an attribute name also
 * stops at '!'/'?'/'=' so a bare trailing '?' before a PI's '>' terminates the
 * attribute loop) - that's modeled here as two lexer states, TAGNAME (active for
 * exactly the one token right after '<' or '</') and ATTRS (active for everything
 * up to the tag's closing '>') rather than as regex lookahead, so lib/xml/read.js
 * can stay a thin driver over plain token types.
 *
 * '/' is excluded from attribute names here (unlike quickjs-xml.c's current attribute
 * scan, which omits SLASH from its stop-set and so absorbs a self-closing tag's
 * trailing '/' into the last boolean attribute's name - see BUGS:
 * quickjs-xml-attrname-swallows-slash) since that's a bug already fixed in this
 * session's other two XML tokenizers (src/xml.c, src/xread.c), not a deliberate,
 * cross-parser convention worth reproducing in a fresh implementation. */
export class XMLLexer extends Lexer {
  constructor(input, fileName) {
    super(input, Lexer.LONGEST, fileName);

    this.addRules();
  }

  addRules() {
    /* Order matters on a length tie: at a `<!--...-->` position with no other '>'
     * inside, 'bangTag' (<![^>]*>) matches the same span as 'comment' - registering
     * 'comment' first makes it win that tie. */
    this.addRule('comment', /<INITIAL><!--[\s\S]*?-->/);
    this.addRule('bangTag', /<INITIAL><![^>]*>/);
    this.addRule('closeTagStart', /<INITIAL><\//, lexer => (lexer.state = 'TAGNAME'));
    this.addRule('tagStart', /<INITIAL></, lexer => (lexer.state = 'TAGNAME'));
    this.addRule('text', /<INITIAL>[^<]+/);

    this.addRule('tagName', /<TAGNAME>[^\s\/>]+/, lexer => (lexer.state = 'ATTRS'));

    this.addRule('ws', /<ATTRS>[ \t\r\n]+/, (lexer, skip) => skip());
    this.addRule('quoted', /<ATTRS>"[^"]*"/);
    this.addRule('quotedSingle', /<ATTRS>'[^']*'/);
    this.addRule('eq', /<ATTRS>=/);
    this.addRule('slash', /<ATTRS>\//);
    this.addRule('question', /<ATTRS>\?/);
    this.addRule('gt', /<ATTRS>>/, lexer => (lexer.state = 'INITIAL'));
    this.addRule('attrName', /<ATTRS>[^\s=!?>\/]+/);

    /* Raw "inside script" text mode (quickjs-xml.c:561-587): while the innermost
     * open tag is named "script", content is scanned as opaque text up to the
     * literal `</script>`, not re-entered as markup - the driver switches into this
     * state itself right after a <script> open tag's '>', since "is the innermost
     * open tag named script" is stack state the lexer has no notion of. No
     * unterminated-input fallback rule here deliberately: in LONGEST mode an
     * unbounded `[\s\S]+` would always beat this lookahead-bounded match (it's
     * always the longer span), which would make it win even when a real closing tag
     * exists - the driver treats a null token here (no </script> anywhere in the
     * rest of the input) as reaching end of input instead, mirroring the C loop's
     * own behavior of just running to the end of the buffer. */
    this.addRule('scriptText', /<SCRIPT>[\s\S]*?(?=<\/script>)/);
  }
}

globalThis.XMLLexer = XMLLexer;

define(XMLLexer.prototype, { [Symbol.toStringTag]: 'XMLLexer' });

export default XMLLexer;
