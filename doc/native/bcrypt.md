# bcrypt

Source: `quickjs-bcrypt.c` — module exports a flat list of functions.

Password hashing with the bcrypt algorithm (wraps `libbcrypt`).

## Functions

| Function | Args | Description |
| --- | --- | --- |
| `genSalt(rounds)` | 0 | Generates a new bcrypt salt; the optional `rounds` cost factor controls work. |
| `hash(password, salt)` | 1 | Hashes `password`; if a `salt` is omitted one is generated. Returns the encoded hash string. |
| `compare(password, hash)` | 2 | Verifies `password` against an existing bcrypt `hash`, returning a boolean. |

## Constants

| Constant | Description |
| --- | --- |
| `HASHSIZE` | Size in bytes of an encoded hash. |
| `SALTSIZE` | Size in bytes of a generated salt. |
