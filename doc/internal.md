# internal

Source: `quickjs-internal.c`

> **Note:** Unlike the other `quickjs-*.c` files, this is a support module. It
> registers **no JS module of its own** (no `js_init_module`, no
> `JSCFunctionListEntry` export list). Instead it provides C helpers that build
> JS introspection objects, which are surfaced to JS through
> [`misc`](misc.md) (e.g. `getModule*`, `getOpCodes`, `getByteCode`).

## C helpers (not directly callable from JS)

### Module introspection
`module_object`, `module_make_object`, `module_exports_get`, `module_imports`,
`module_reqmodules`, `module_default_export`, `module_ns`, `module_exception`,
`module_meta_obj`, `module_func`, `module_name`/`module_nameval`/
`module_namecstr`, `module_next`/`module_prev`/`module_last`,
`module_exports_find`, `module_rename`, `module_indexof`.

### Module lists / lookup (back the `misc` module functions)
`js_modules_list`, `js_modules_vector`, `js_modules_entries`,
`js_modules_object`, `js_module_find_fwd`, `js_module_find_rev`,
`js_module_index`, `js_module_at`.

### Bytecode / opcodes
`js_opcode_array`, `js_opcode_object`, `js_opcode_list`, `js_get_bytecode` —
produce the data returned by `misc.getOpCodes()` and `misc.getByteCode()`.

### Misc
`js_std_file` (wrap a `FILE*` as a JS object), `js_cstring_dup`,
`js_stack_get`.
