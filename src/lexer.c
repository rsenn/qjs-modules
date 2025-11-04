#include "lexer.h"
#include "debug.h"
#include "location.h"
#include <libregexp.h>
#include <ctype.h>
#include "buffer-utils.h"
#include "token.h"

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
  int32_t id = lex->state;

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

  if(i - 1 >= (sz = vector_size(&lex->state_stack, sizeof(int32_t))))
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
  return TRUE;
}

static BOOL
lexer_rule_compile(Lexer* lex, LexerRule* rule, JSContext* ctx) {
  DynBuf dbuf = DBUF_INIT_0();
  BOOL ret;

  if(rule->bytecode)
    return TRUE;

  js_dbuf_init(ctx, &dbuf);

  if(lexer_rule_expand(lex, lexer_rule_regex(rule), &dbuf)) {
    rule->expansion = js_strndup(ctx, (const char*)dbuf.buf, dbuf.size);
    rule->bytecode =
        regexp_compile(regexp_from_dbuf(&dbuf, LRE_FLAG_GLOBAL | LRE_FLAG_MULTILINE | LRE_FLAG_STICKY), ctx);
    ret = rule->bytecode != 0;

  } else {
    JS_ThrowInternalError(ctx, "Error expanding rule '%s'", rule->name);
    ret = FALSE;
  }

  dbuf_free(&dbuf);
  return ret;
}

static LexerResult
lexer_rule_match(Lexer* lex, LexerRule* rule, uint8_t** capture, JSContext* ctx) {

  if(rule->bytecode == 0) {
    if(!lexer_rule_compile(lex, rule, ctx))
      return LEXER_ERROR_COMPILE;
  }

  // fprintf(stderr, "lexer_rule_match %s %s %s\n", rule->name, rule->expr, rule->expansion);

  return lre_exec(capture, rule->bytecode, (uint8_t*)lex->data, lex->byte_offset, lex->byte_length, 0, ctx);
}

int
lexer_rule_add(Lexer* lex, char* name, char* expr) {
  LexerRule rule = {name, expr, MASK_ALL, 0, 0, 0}, *previous;
  int ret;

  if((previous = lexer_rule_find(lex, name))) {
    return -1;
  }

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

  ret = vector_size(&lex->rules, sizeof(LexerRule));
  vector_push(&lex->rules, rule);
  return ret;
}

LexerRule*
lexer_rule_find(Lexer* lex, const char* name) {
  LexerRule* rule;
  assert(name);

  vector_foreach_t(&lex->rules, rule) {
    assert(rule->name);

    if(!strcmp(rule->name, name))
      return rule;
  }

  return 0;
}

void
lexer_rule_release_rt(LexerRule* rule, JSRuntime* rt) {
  if(rule->name)
    js_free_rt(rt, rule->name);

  js_free_rt(rt, rule->expr);

  if(rule->bytecode)
    orig_js_free_rt(rt, rule->bytecode);
}

void
lexer_rule_dump(Lexer* lex, LexerRule* rule, DynBuf* dbuf) {
  lexer_rule_expand(lex, rule->expr, dbuf);
}

Lexer*
lexer_new(JSContext* ctx) {
  Lexer* lex;

  if((lex = js_malloc(ctx, sizeof(Lexer))))
    lexer_init(lex, -1, ctx);

  return lex;
}

void
lexer_init(Lexer* lex, enum lexer_mode mode, JSContext* ctx) {
  // char* initial = js_strdup(ctx, "INITIAL");

  memset(lex, 0, sizeof(Lexer));

  lex->ref_count = 1;
  lex->mode = mode;
  lex->state = 0;
  lex->seq = 0;

  location_init(&lex->loc);

  vector_init(&lex->defines, ctx);
  vector_init(&lex->rules, ctx);
  vector_init(&lex->states, ctx);
  // vector_push(&lex->states, initial);
  vector_init(&lex->state_stack, ctx);
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
lexer_peek(Lexer* lex, unsigned start_rule, JSContext* ctx) {
  LexerRule *rule, *start = vector_begin(&lex->rules), *end = vector_end(&lex->rules);
  uint8_t* capture[512];
  int ret = LEXER_ERROR_NOMATCH;
  size_t len = 0;

  if(inputbuffer_eof(&lex->input))
    return LEXER_EOF;

  if(lex->loc.byte_offset == -1)
    location_zero(&lex->loc);

  assert(start_rule < vector_size(&lex->rules, sizeof(LexerRule)));

  for(rule = start + start_rule; rule < end; ++rule) {
    LexerResult result;

    if((rule->mask & (1 << lex->state)) == 0)
      continue;

    result = lexer_rule_match(lex, rule, capture, ctx);

    /*size_t elen = strlen(rule->expansion);
    printf("%s result %i state %i rule#%ld %s (start=%d) /%.*s%s\n",
           __func__,
           result,
           lex->state,
           rule - start,
           rule->name,
           start_rule,
           (int)(elen > 30 ? 30 : elen),
           rule->expansion,
           elen > 30 ? "...." : "/");*/

    /*if(result == LEXER_ERROR_COMPILE) {
      ret = result;
      break;
    } else */
    if(result < LEXER_ERROR_NOMATCH) {
      const char* t = ((const char*[]){
          "executing",
          "compiling",
      })[result - LEXER_ERROR_EXEC];

      JS_ThrowInternalError(ctx, "Error %s regex /%s/", t, rule->expr);
      fprintf(stderr, "Error %s regex /%s/\n", t, rule->expr);

      ret = result;
      break;
    } else if(result > 0 && (capture[1] - capture[0]) > 0) {

#ifdef DEBUG_OUTPUT
      const char* filename = lex->loc.file == -1 ? 0 : JS_AtomToCString(ctx, lex->loc.file);

      printf("%s%s%" PRIu32 ":%-4" PRIu32 " #%i %-20s - /%s/ [%zu] %.*s\n",
             filename ? filename : "",
             filename ? ":" : "",
             lex->loc.line + 1,
             lex->loc.column + 1,
             (int)(rule - start),
             rule->name,
             rule->expr,
             capture[1] - capture[0],
             (int)(capture[1] - capture[0]),
             capture[0]);
      JS_FreeCString(ctx, filename);
#endif

      if((lex->mode & LEXER_LONGEST) == 0 || ret < 0 || (size_t)(capture[1] - capture[0]) > len) {
        ret = rule - start;
        len = capture[1] - capture[0];

        if(lex->mode == LEXER_FIRST)
          break;
      }
    }
  }

  if(ret >= 0) {
    lex->byte_length = len;
    lex->token_id = ret;
  } else {
    lex->byte_length = 0;
    lex->token_id = -1;
  }

  return ret;
}

size_t
lexer_skip_n(Lexer* lex, size_t bytes) {
  size_t len;

  assert(bytes <= lex->byte_length - lex->byte_offset);

  lex->loc.byte_offset = LEXER_POS(lex);

  return location_count(&lex->loc, LEXER_PTR(lex), bytes);
}

size_t
lexer_skip(Lexer* lex) {
  size_t len;

  len = lexer_skip_n(lex, lex->byte_length);
  lex->seq++;
  lexer_clear_token(lex);

  return len;
}

void
lexer_clear_token(Lexer* lex) {
  assert(lex->byte_length);
  assert(lex->token_id != -1);

  lex->byte_length = 0;
  lex->token_id = -1;
}

size_t
lexer_charlen(Lexer* lex) {
  if(lex->byte_length == 0)
    return 0;

  assert((lex->byte_length - LEXER_POS(lex)) >= lex->byte_length);

  return utf8_strlen(LEXER_PTR(lex), lex->byte_length);
}

char*
lexer_lexeme(Lexer* lex, size_t* lenp) {
  char* s = (char*)LEXER_PTR(lex);

  if(lenp)
    *lenp = lex->byte_length;

  return s;
}

int
lexer_next(Lexer* lex, JSContext* ctx) {
  int ret;

  if((ret = lexer_peek(lex, 0, ctx)) >= 0)
    lexer_skip(lex);

  return ret;
}

void
lexer_set_input(Lexer* lex, InputBuffer input, int32_t file_atom) {
  lex->input = input;
  lex->loc.file = file_atom;
}

void
lexer_set_location(Lexer* lex, const Location* loc, JSContext* ctx) {
  lex->byte_length = 0;
  lex->byte_offset = loc->char_offset;
  location_release(&lex->loc, JS_GetRuntime(ctx));
  location_copy(&lex->loc, loc, ctx);
}

void
lexer_release(Lexer* lex, JSRuntime* rt) {
  char** statep;
  LexerRule* rule;

  inputbuffer_free(&lex->input, lex->rules.opaque);

  vector_foreach_t(&lex->defines, rule) {
    lexer_rule_release_rt(rule, rt);
  }
  vector_foreach_t(&lex->rules, rule) {
    lexer_rule_release_rt(rule, rt);
  }
  vector_foreach_t(&lex->states, statep) {
    js_free_rt(rt, *statep);
  }

  vector_free(&lex->defines);
  vector_free(&lex->rules);
  vector_free(&lex->states);
  vector_free(&lex->state_stack);

  location_release(&lex->loc, rt);
}

void
lexer_free(Lexer* lex, JSRuntime* rt) {
  if(--lex->ref_count == 0) {
    lexer_release(lex, rt);

    js_free_rt(rt, lex);
  }
}

void
lexer_dump(Lexer* lex, DynBuf* dbuf) {
  dbuf_printf(dbuf, "Lexer {\n  mode: %x,\n  state: %s", lex->mode, lexer_state_name(lex, lex->state));
  dbuf_putstr(dbuf, ",\n  input: ");
  inputbuffer_dump(&lex->input, dbuf);
  dbuf_putstr(dbuf, ",\n  location: ");
  location_print(&lex->loc, dbuf, 0);
  dbuf_putstr(dbuf, "\n}");
}

Location
lexer_get_location(Lexer* lex, JSContext* ctx) {
  Location loc = {1};
  location_copy(&loc, &lex->loc, ctx);
  return loc;
}

Token*
lexer_token(Lexer* lex, int32_t id, JSContext* ctx) {
  size_t len;
  char* lexeme;
  Token* tok;

  if(!(lexeme = lexer_lexeme(lex, &len)))
    return 0;

  if(!(tok = token_create(id, lexeme, len, ctx)))
    return 0;

  /*tok->lexer = lexer_dup(lex);*/
  tok->seq = lex->seq;

  if(!tok->loc)
    tok->loc = location_new(ctx);

  if(tok->loc)
    location_copy(tok->loc, &lex->loc, ctx);

  return tok;
}

char*
lexer_current_line(Lexer* lex, JSContext* ctx) {
  size_t size, start = lex->byte_offset;

  while(start > 0 && lex->data[start - 1] != '\n')
    start--;

  size = byte_chr((const char*)&lex->data[start], lex->byte_length - start, '\n');

  return js_strndup(ctx, (const char*)&lex->data[start], size);
}

char*
lexer_lexeme_s(Lexer* lex, JSContext* ctx, int (*escape_fn)(int)) {
  size_t len;
  char* s;
  DynBuf output;

  js_dbuf_init(ctx, &output);

  if((s = lexer_lexeme(lex, &len)))
    dbuf_put_escaped_pred(&output, s, len, escape_fn);

  dbuf_0(&output);

  return (char*)output.buf;
}

/**
 * @}
 */
