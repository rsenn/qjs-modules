import { Blob } from 'blob';

export class File extends Blob {
  #name;
  #lastModified;

  constructor(chunks, name, options = {}) {
    super(chunks, options);
    this.#name = String(name);
    this.#lastModified = options.lastModified !== undefined ? Number(options.lastModified) : Date.now();
  }

  get name() {
    return this.#name;
  }

  get lastModified() {
    return this.#lastModified;
  }

  get webkitRelativePath() {
    return '';
  }
}

Object.defineProperty(File.prototype, Symbol.toStringTag, { value: 'File', configurable: true });
