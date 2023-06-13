export class Pool {
  constructor(connection_params, size, lazy = false) {
    this.#connection_params = connection_params;
    this.#size = size;
    this.#lazy = lazy;
  }
}
