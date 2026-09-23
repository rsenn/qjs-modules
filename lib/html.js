import { XMLParser } from 'xml';
import { isPrototypeOf } from 'util';

const { ELEMENT_START, ELEMENT_END, ATTRIBUTE, TEXT } = XMLParser;

export class HTMLParser {
  #xmlp;
  #stack = [];
  #top;

  constructor(source, filename_or_options) {
    // XMLParser's constructor reads options.* via JS_GetPropertyStr() without a
    // JS_IsUndefined() guard, so explicitly passing `undefined` through (as opposed
    // to omitting the argument) throws - always give it a real value.
    this.#xmlp = new XMLParser(source, typeof filename_or_options === 'string' ? { filename: filename_or_options } : filename_or_options ?? {});
    // HTML is case-insensitive; XMLParser isn't tolerant by default (XML is
    // case-sensitive and strict about closing-tag matches).
    this.#xmlp.tolerant = true;
  }

  parse() {
    const r = this.#xmlp.parse();
    let { eventName: name, eventValue: value } = this.#xmlp;

    if(name) name = name.toLowerCase();

    switch (r) {
      case ELEMENT_START: {
        this.#stack.push((this.#top = [name, {}]));
        break;
      }
      case ELEMENT_END: {
        let a;
        while(this.#stack.length) {
          const [tag, attrs] = this.#stack.pop();
          a = attrs;
          if(tag == name) break;
        }
        this.#top = this.#stack[this.#stack.length - 1];

        name = '/' + name;
        return { name, attributes: a };
        break;
      }
      case ATTRIBUTE: {
        this.#top[1][name] = value;

        return { name, value };
        break;
      }
      case TEXT: {
        return { value };
        break;
      }
      default: {
        return null;
      }
    }
    return { name };
  }

  get attributes() {
    return this.#top?.[1];
  }
  get tag() {
    return this.#top?.[0];
  }
}

export function* streamSrcHrefAndText(source, options) {
  const parser = isPrototypeOf(HTMLParser.prototype, source) ? source : new HTMLParser(source, options);
  const { attributes = ['src', 'href'] } = options;
  let event;

  while((event = parser.parse()) !== null) {
    const { tag } = parser;

    // 1. Text node event: contains `value` but no `name`
    if(!event.name && event.value !== undefined) {
      const text = event.value.trim();
      if(text.length > 0) {
        yield { type: 'text', value: event.value, tag };
      }
    }
    // 2. Attribute event: contains both `name` and `value`
    else if(event.name && event.value !== undefined) {
      const attrName = event.name.toLowerCase();
      if(attributes.includes(attrName)) {
        yield { type: attrName, value: event.value, tag };
      }
    }
  }
}

HTMLParser.prototype[Symbol.toStringTag] = 'HTMLParser';
