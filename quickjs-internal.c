#define SHORT_OPCODES 1
#include "quickjs-internal.h"

const JSOpCode js_opcodes[OP_COUNT + (OP_TEMP_END - OP_TEMP_START)] = {
#define FMT(f)
#define DEF(id, size, n_pop, n_push, f) {size, n_pop, n_push, OP_FMT_##f, #id},
#include "quickjs-opcode.h"
#undef DEF
#undef FMT
};