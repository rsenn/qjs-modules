import { SQLite3, SQLite3Result } from 'sqlite';

function splitQuery(q) {
  const re = /(\?|[^?]+)/g,
    fragments = [];
  let m;

  while((m = re.exec(q))) {
    fragments.push(m[0]);
  }

  return fragments;
}

function prepareQuery(sql, ...bindings) {
  let q = '';

  if(typeof sql != 'string') if (Array.isArray(sql)) sql = splitQuery(sql);

  for(let frag of sql) {
    if(frag == '?') q += SQLite3.valueString(bindings.shift());
    else q += frag;
  }

  return q;
}

export class Statement {
  #db;
  #sql;
  #bindings;
  #cache;
  #cached;

  constructor(db, query, { bindings = [], cache = false }) {
    this.#db = db;
    this.#sql = query;
    this.#bindings = bindings.length ? bindings : null;
    this.#cache = cache;
  }

  #queryString(params = []) {
    const q = this.#cached ?? prepareQuery(this.#sql, ...((params.length ? params : this.#bindings) ?? []));
    if(this.#cache) this.#cached = q;
    return q;
  }

  #query(params = []) {
    const q = this.#queryString(params);
    return this.#db.query(q);
  }

  all(...params) {
    const rows = [],
      result = this.#query(params);

    let row;
    while((row = result.fetchAssoc())) rows.push(row);
    return rows;
  }

  get(...params) {
    const result = this.#query(params);

    return result.fetchAssoc();
  }

  *iterate(...params) {
    const result = this.#query(params);
    let row;

    while((row = result.fetchAssoc())) yield row;
  }

  run(...params) {
    this.#query(params);

    const { changes, lastInsertRowid } = this.#db;
    return { changes, lastInsertRowid };
  }

  toString() {
    return this.#queryString();
  }
}

Statement.prototype[Symbol.toStringTag] = 'Statement';

export class Database {
  #db;

  constructor(filename, opts = { create: false }) {
    this.#db = new SQLite3(filename, opts);
  }

  run(sql, ...bindings) {
    this.#db.exec(prepareQuery(sql, ...bindings));

    const { changes, lastInsertRowid } = this.#db;

    return { changes, lastInsertRowid };
  }

  prepare(sql, ...bindings) {
    return new Statement(this.#db, sql, { bindings, cache: false });
  }

  query(sql, ...bindings) {
    return new Statement(this.#db, sql, { bindings, cache: true });
  }

  close(throwOnError) {
    this.#db.close();
  }
}

Database.prototype[Symbol.toStringTag] = 'Database';
