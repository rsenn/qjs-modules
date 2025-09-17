import { define, properties, isNumber } from 'util';
import { Factory } from 'dom';

class EagleElement extends Element {
  constructor(...args) {
    super(...args);

    switch (this.tagName) {
      case 'instance': {
        Properties(this, {
          part: () => this.ownerDocument.querySelector(`part[name="${this.getAttribute('part')}"]`),
          gate: () => this.part.deviceset.querySelector(`gate[name="${this.getAttribute('gate')}"]`),
        });
        break;
      }
      case 'element': {
        Properties(this, {
          library: () => this.ownerDocument.querySelector(`library[name="${this.getAttribute('library')}"]`),
          package() {
            const { library } = this;
            return library.querySelector(`package[name="${this.getAttribute('package')}"]`);
          },
        });
        break;
      }
      case 'part': {
        Properties(this, {
          library: () => this.ownerDocument.querySelector(`library[name="${this.getAttribute('library')}"]`),
          deviceset() {
            const { library } = this;
            return library.querySelector(`deviceset[name="${this.getAttribute('deviceset')}"]`);
          },
          device() {
            const { deviceset } = this;
            return deviceset.querySelector(`device[name="${this.getAttribute('device')}"]`);
          },
        });

        break;
      }
    }

    switch (this.tagName) {
      case 'board':
      case 'schematic':
      case 'sheet':
      case 'library':
      case 'drawing': {
        for(const child of this.children) if(!(child.tagName in this)) Properties(this, { [child.tagName]: () => child });

        break;
      }
    }
  }
}

define(EagleElement.prototype, { [Symbol.toStringTag]: 'EagleElement' });

class EagleDocument extends Document {
  constructor(...args) {
    super(...args);

    Properties(this, {
      drawing: () => this.querySelector('drawing'),
      layers: () => {
        const { layers } = this.drawing;
        const FindLayer = prop => Node.raw(layers).children.findIndex(isNumber(prop) ? e => e.attributes.number == prop : e => e.attributes.name == prop);
        return new Proxy(
          {},
          {
            get: (target, prop) => {
              const index = FindLayer(prop);
              if(index != -1) return layers.children[index];
              return Reflect.get(target, prop, receiver);
            },
            getOwnPropertyDescriptor: (target, prop) => {
              const index = FindLayer(prop);
              if(index != -1) return { value: layers.children[index], enumerable: true, configurable: true };
              return Reflect.getOwnPropertyDescriptor(target, prop);
            },
            ownKeys: () => Node.raw(layers).children.map(e => e.attributes.name),
          },
        );
      },
    });

    //console.log('EagleDocument', this.tagName);
  }

  get type() {
    return FindChild(this.drawing, 'schematic') ? 'sch' : 'brd';
  }
}

define(EagleDocument.prototype, { [Symbol.toStringTag]: 'EagleDocument' });

function Properties(obj, props) {
  return define(obj, properties(props, { memoize: true }));
}

function FindChild(e, pred) {
  if(typeof pred == 'string') {
    const tag = pred;
    pred = e => e.tagName == tag;
  }

  return [...e.children].find(pred);
}

function createXPathFromElement(elm) {
  const document = elm.ownerDocument;
  var allNodes = document.getElementsByTagName('*');

  for(var segs = []; elm && elm.nodeType == 1; elm = elm.parentNode) {
    if(elm.hasAttribute('id')) {
      var uniqueIdCount = 0;
      for(var n = 0; n < allNodes.length; n++) {
        if(allNodes[n].hasAttribute('id') && allNodes[n].id == elm.id) uniqueIdCount++;
        if(uniqueIdCount > 1) break;
      }
      if(uniqueIdCount == 1) {
        segs.unshift('id("' + elm.getAttribute('id') + '")');
        return segs.join('/');
      } else {
        segs.unshift(elm.localName.toLowerCase() + '[@id="' + elm.getAttribute('id') + '"]');
      }
    } else if(elm.hasAttribute('class')) {
      segs.unshift(elm.localName.toLowerCase() + '[@class="' + elm.getAttribute('class') + '"]');
    } else {
      for(i = 1, sib = elm.previousSibling; sib; sib = sib.previousSibling) {
        if(sib.localName == elm.localName) i++;
      }
      segs.unshift(elm.localName.toLowerCase() + '[' + i + ']');
    }
  }
  return segs.length ? '/' + segs.join('/') : null;
}

export default function p(g = globalThis) {
  g.p = new Parser(new Factory({ Element: EagleElement.prototype, Document: EagleDocument.prototype }));

  g.document = g.p.parseFromFile('/home/roman/Projects/an-tronics/eagle/Pink-Noise-Filter.sch');

  g.e = document.querySelector('instance');
  g.createXPathFromElement = createXPathFromElement;
}
