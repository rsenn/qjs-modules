#include "lexer.h"
#include "location.h"
#include <libregexp.h>
#include <ctype.h>
#include "buffer-utils.h"

/**
 * \addtogroup lexer
 * @{
 */
int
lexer_state_findb(Lexer* lex, const char* state, size_t slen) {
  int ret = -1;
  char** statep;

  vector_foreach_t(&lex->states, statep) {
    ++ret;

    if(strlen((*statep)) == slen && !strncmp((*statep), state, slen))
      return ret;
  }

  return -1;
}

int
lexer_state_new(Lexer* lex, const char* name, size_t len) {
  char* state;
  int ret;

  if((ret = lexer_state_findb(lex, name, len)) != -1)
    return ret;

  state = str_ndup(name, len);
  ret = vector_size(&lex->states, sizeof(char*));
  vector_push(&lex->states, state);
  return ret;
}

int
lexer_state_push(Lexer* lex, const char* state) {
  int32_t id;
#ifdef DEBUG_OUTPUT_
  printf("lexer_state_push(%zu): %s\n", vector_size(&lex->state_stack, sizeof(int32_t)), state);
#endif
  if((id = lexer_state_findb(lex, state, strlen(state))) >= 0) {
    vector_push(&lex->state_stack, lex->state);
    lex->state = id;
  }
  assert(id >= 0);
  return id;
}

int
lexer_state_pop(Lexer* lex) {
  int32_t id;
  size_t n = vector_size(&lex->state_stack, sizeof(int32_t)) - 1;
  id = lex->state;
#ifdef DEBUG_OUTPUT_
  printf("lexer_state_pop(%zu): %s\n", n, lexer_state_name(lex, id));
#endif
  if(!vector_empty(&lex->state_stack)) {
    lex->state = *(int32_t*)vector_back(&lex->state_stack, sizeof(int32_t));
    vector_pop(&lex->state_stack, sizeof(int32_t));
  } else {
    lex->state = -1;
  }
  return id;
}

int
lexer_state_top(Lexer* lex, int i) {
  int sz;
  if(i == 0)
    return lex->state;
  sz = vector_size(&lex->state_stack, sizeof(int32_t));
  if(i - 1 >= sz)
    return -1;

  assert(sz >= i);
  return *(int32_t*)vector_at(&lex->state_stack, sizeof(int32_t), sz - i);
}

char*
lexer_states_skip(char* expr) {
  char* re = expr;

  if(*re == '<') {
    size_t offset = str_chr(re, '>');

    if(re[offset])
      re += offset + 1;
  }
  return re;
}

void
lexer_states_dump(Lexer* lex, uint64_t mask, DynBuf* dbuf) {
  int state = 0;
  char** statep;
  size_t n = dbuf->size;

  vector_foreach_t(&lex->states, statep) {
    if(mask & (1 << state)) {

      if(dbuf->size > n)
        dbuf_putc(dbuf, ',');
      dbuf_putstr(dbuf, *statep);
      state++;
    }
  }
}

char*
lexer_rule_regex(LexerRule* rule) {
  return lexer_states_skip(rule->expr);
}

BOOL
lexer_rule_expand(Lexer* lex, char* p, DynBuf* db) {
  size_t len;

  dbuf_zero(db);

  for(; *p; p++) {
    if(*p == '{') {
      if(p[len = str_chr(p, '}')]) {
        LexerRule* subst;
        if((subst = lexer_find_definition(lex, p + 1, len - 1))) {
          lexer_rule_expand(lex, subst->expr, db);
          p += len;
          continue;
        }
      }
    }

    if(*p == '\\')
      dbuf_putc(db, *p++);
    dbuf_putc(db, *p);
  }
  dbuf_0(db);

  // printf("expand %s %s\n", p, db->buf);

  return TRUE;
}

static BOOL
lexer_rule_compile(Lexer* lex, LexerRule* rule, JSContext* ctx) {
  DynBuf dbuf;
  BOOL ret;

  if(rule->bytecode)
    return TRUE;

  js_dbuf_init(ctx, &dbuf);

  if(lexer_rule_expand(lex, lexer_rule_regex(rule), &dbuf)) {
    rule->expansion = js_strndup(ctx, (const char*)dbuf.buf, dbuf.size);
    rule->bytecode = regexp_compile(regexp_from_dbuf(&dbuf, LRE_FLAG_GLOBAL | LRE_FLAG_MULTILINE | LRE_FLAG_STICKY), ctx);
    ret = rule->bytecode != 0;

  } else {
    JS_ThrowInternalError(ctx, "Error expanding rule '%s'", rule->name);
    ret = FALSE;
  }

  dbuf_free(&dbuf);
  return ret;
}

static int
lexer_rule_match(Lexer* lex, LexerRule* rule, uint8_t** capture, JSContext* ctx) {

  if(rule->bytecode == 0) {
    if(!lexer_rule_compile(lex, rule, ctx))
      return LEXER_ERROR_COMPILE;
  }

  // fprintf(stderr, "lexer_rule_match %s %s %s\n", rule->name, rule->expr, rule->expansion);

  return lre_exec(capture, rule->bytecode, (uint8_t*)lex->input.data, lex->input.pos, lex->input.size, 0, ctx);
}

int
lexer_rule_add(Lexer* lex, char* name, char* expr) {
  LexerRule rule = {name, expr, 1, 0, 0, 0}, *previous;
  int ret = vector_size(&lex->rules, sizeof(LexerRule));

  if(rule.expr[0] == '<') {
    char* s;
    int32_t flags = 0;

    for(s = &rule.expr[1]; *s && *s != '>';) {
      size_t len = str_chrs(s, ",>", 2);
      int index;

      if(s[len] == '\0')
        break;

      if((index = lexer_state_findb(lex, s, len)) == -1)
        index = lexer_state_new(lex, s, len);

      assert(index != -1);
      flags |= 1 << index;

      if(*(s += len) == ',')
        s++;
    }

    if(*s == '>')
      rule.mask = flags;
  }

  // fprintf(stderr, "lexer_rule_add %s %s %08x\n", rule.name, rule.expr, rule.mask);

  vector_push(&lex->rules, rule);
  return ret;
}

LexerRule*
lexer_rule_find(Lexer* lex, const char* name) {
  LexerRule* rule;
  vector_foreach_t(&lex->rules, rule) {
    if(name && rule->name) {
      if(!strcmp(rule->name, name))
        return rule;
    } else {
      if(name == rule->name)
        return rule;
    }
  }
  return 0;
}

void
lexer_rule_free(LexerRule* rule, JSContext* ctx) {
  if(rule->name)
    js_free(ctx, rule->name);
  js_free(ctx, rule->expr);

  if(rule->bytecode)
    js_free(ctx, rule->bytecode);
}

void
lexer_rule_free_rt(LexerRule* rule, JSRuntime* rt) {
  if(rule->name)
    js_free_rt(rt, rule->name);
  js_free_rt(rt, rule->expr);

  if(rule->bytecode)
    js_free_rt(rt, rule->bytecode);
}

void
lexer_rule_dump(Lexer* lex, LexerRule* rule, DynBuf* dbuf) {
  /*  if(rule->mask != 0) {
      dbuf_putc(dbuf, '<');
      lexer_states_dump(lex, rule->mask, dbuf);
      dbuf_putc(dbuf, '>');
    }*/
  lexer_rule_expand(lex, rule->expr, dbuf);
}

void
lexer_init(Lexer* lex, enum lexer_mode mode, JSContext* ctx) {
  char* initial = js_strdup(ctx, "INITIAL");

  memset(lex, 0, sizeof(Lexer));

  lex->mode = mode;
  lex->state = 0;
  lex->seq = 0;

  location_init(&lex->loc);

  vector_init(&lex->defines, ctx);
  vector_init(&lex->rules, ctx);
  vector_init(&lex->states, ctx);
  vector_push(&lex->states, initial);
  vector_init(&lex->state_stack, ctx);
}

void
lexer_set_input(Lexer* lex, InputBuffer input, char* filename) {
  lex->input = input;
  lex->loc.file = filename;
}

void
lexer_define(Lexer* lex, char* name, char* expr) {
  LexerRule definition = {name, expr, MASK_ALL, 0, 0, 0};
  vector_size(&lex->defines, sizeof(LexerRule));
  vector_push(&lex->defines, definition);
}

LexerRule*
lexer_find_definition(Lexer* lex, const char* name, size_t namelen) {
  LexerRule* definition;
  vector_foreach_t(&lex->defines, definition) {
    if(!strncmp(definition->name, name, namelen) && definition->name[namelen] == '\0')
      return definition;
  }
  return 0;
}

BOOL
lexer_compile_rules(Lexer* lex, JSContext* ctx) {
  LexerRule* rule;

  vector_foreach_t(&lex->rules, rule) {
    if(!lexer_rule_compile(lex, rule, ctx))
      return FALSE;
  }
  return TRUE;
}

int
lexer_peek(Lexer* lex, uint64_t state, JSContext* ctx) {
  LexerRule* rule;
  uint8_t* capture[512];
  int i = 0, ret = LEXER_ERROR_NOMATCH;
  size_t len = 0;

  if(input_buffer_eof(&lex->input))
    return LEXER_EOF;

  lex->start = lex->input.pos;

  vector_foreach_t(&lex->rules, rule) {
    int result;
    if((rule->mask & (1 << lex->state)) == 0) {
      i++;
      continue;
    }
    result = lexer_rule_match(lex, rule, capture, ctx);
    if(result == LEXER_ERROR_COMPILE) {
      ret = result;
      break;
    } else if(result < 0) {
      JS_ThrowInternalError(ctx, "Error matching regex /%s/", rule->expr);
      ret = LEXER_ERROR_EXEC;
      break;
    } else if(result > 0 && (capture[1] - capture[0]) > 0) {
      /*printf("%s:%" PRIu32 ":%" PRIu32 " #%i %-20s - /%s/ [%zu] %.*s\n",
             lex->loc.file,
             lex->loc.line + 1,
             lex->loc.column + 1,
             i,
             rule->name,
             rule->expr,
             capture[1] - capture[0],
             capture[1] - capture[0],
             capture[0]); */
      if((lex->mode & LEXER_LONGEST) == 0 || ret < 0 || (size_t)(capture[1] - capture[0]) >= len) {
        ret = i;
        len = capture[1] - capture[0];
        if(lex->mode == LEXER_FIRST)
          break;
      }
    }
    i++;
  }
  if(ret >= 0) {
    lex->byte_length = len;
    lex->token_id = i;
  }

  return ret;
}

static inline size_t
input_skip(InputBuffer* input, size_t end, Location* loc) {
  size_t n = 0;
  while(input->pos < end) {
    size_t prev = input->pos;
    if(input_buffer_getc(input) == '\n') {
      loc->line++;
      loc->column = 0;
    } else {
      loc->column++;
    }
    loc->pos++;
    n++;
  }
  return n;
}

size_t
lexer_skip(Lexer* lex) {
  size_t len = input_skip(&lex->input, lex->start + lex->byte_length, &lex->loc);
  lex->seq++;
  return len;
}

char*
lexer_lexeme(Lexer* lex, size_t* lenp) {
  size_t len = lex->input.pos - lex->start;
  char* s = (char*)lex->input.data + lex->start;
  if(lenp)
    *lenp = len;
  return s;
}

int
lexer_next(Lexer* lex, uint64_t state, JSContext* ctx) {
  int ret;

  if((ret = lexer_peek(lex, state, ctx)) >= 0)
    lexer_skip(lex);

  return ret;
}

void
lexer_set_location(Lexer* lex, const Location* loc, JSContext* ctx) {
  lex->start = loc->pos;
  lex->byte_length = 0;
  lex->input.pos = loc->pos;
  location_free(&lex->loc, ctx);
  lex->loc = location_clone(loc, ctx);
}

void
lexer_free(Lexer* lex, JSContext* ctx) {
  LexerRule* rule;
  char** state;

  if(!ctx)
    ctx = lex->rules.opaque;

  input_buffer_free(&lex->input, ctx);

  vector_foreach_t(&lex->defines, rule) { lexer_rule_free(rule, ctx); }
  vector_foreach_t(&lex->rules, rule) { lexer_rule_free(rule, ctx); }
  vector_foreach_t(&lex->states, state) { free(*state); }

  vector_free(&lex->defines);
  vector_free(&lex->rules);
  vector_free(&lex->states);
  vector_free(&lex->state_stack);
}

void
lexer_free_rt(Lexer* lex, JSRuntime* rt) {
  char** statep;
  LexerRule* rule;
  vector_foreach_t(&lex->states, statep) { js_free_rt(rt, *statep); }
  vector_free(&lex->states);
  vector_foreach_t(&lex->rules, rule) { lexer_rule_free_rt(rule, rt); }
  vector_free(&lex->rules);
  vector_foreach_t(&lex->defines, rule) { lexer_rule_free_rt(rule, rt); }
  vector_free(&lex->defines);
  vector_free(&lex->state_stack);
}

void
lexer_dump(Lexer* lex, DynBuf* dbuf) {
  dbuf_printf(dbuf, "Lexer {\n  mode: %x,\n  start: %zu, state: %s", lex->mode, lex->start, lexer_state_name(lex, lex->state));
  dbuf_putstr(dbuf, ",\n  input: ");
  input_buffer_dump(&lex->input, dbuf);
  dbuf_putstr(dbuf, ",\n  location: ");
  location_print(&lex->loc, dbuf, 0);
  dbuf_putstr(dbuf, "\n}");
}

/**
 * @}
 */
