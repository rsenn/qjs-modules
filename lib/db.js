import { define } from 'util';
import { List } from 'list';

export class PoolClient {
  #client;
  #release;

  constructor(dbClient, releaseCallback) {
    this.#client = dbClient;
    this.#release = releaseCallback;
  }

  query(...args) {
    return this.#client.query(...args);
  }

  release() {
    this.#release();
  }
}

export class Pool {
  #available_connections;
  #construct;
  #ended = false;
  #lazy;
  #ready;
  #size;

  get available() {
    if(!this.#available_connections) {
      return 0;
    }
    return this.#available_connections.size;
  }

  get size() {
    return this.#size;
  }

  constructor(construct, size, lazy = false) {
    this.#construct = construct;
    this.#size = size;
    this.#lazy = lazy;
    this.#ready = this.#initialize();
  }

  async list() {
    await this.#ready;
    console.log('#available_connections', [...this.#available_connections]);
    return this.#available_connections;
  }

  async connect() {
    if(this.#ended) {
      this.#ready = this.#initialize();
    }
    await this.#ready;
    return this.#available_connections.pop();
  }

  async end() {
    if(this.#ended) {
      throw new Error('Pool connections have already been terminated');
    }
    await this.#ready;
    while(this.available > 0) {
      const client = await this.#available_connections.pop();
      await client.end();
    }
    this.#available_connections = undefined;
    this.#ended = true;
  }

  async #initialize() {
    const initialized = this.#lazy ? 0 : this.#size;
    const clients = Array.from(
      {
        length: this.#size,
      },
      async (_e, index) => {
        const client = this.#construct();
        if(index < initialized) {
          await client.connect();
        }
        return client;
      },
    );
    this.#available_connections = new List(await Promise.all(clients));
    this.#ended = false;
  }

  async connect() {}
}

define(Pool.prototype, {
  [Symbol.toStringTag]: 'Pool',
});
