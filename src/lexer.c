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

  state = js_strndup(lex->states.opaque, name, len);
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

  dbuf_init_ctx(ctx, &dbuf);

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

static LexerResult
lexer_rule_match(Lexer* lex, LexerRule* rule, uint8_t** capture, JSContext* ctx) {

  if(rule->bytecode == 0) {
    if(!lexer_rule_compile(lex, rule, ctx))
      return LEXER_ERROR_COMPILE;
  }

  // fprintf(stderr, "lexer_rule_match %s %s %s\n", rule->name, rule->expr, rule->expansion);

  return lre_exec(capture, rule->bytecode, (uint8_t*)lex->data, LEXER_IDX(lex), lex->size, 0, ctx);
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
  char* initial = js_strdup(ctx, "INITIAL");

  memset(lex, 0, sizeof(Lexer));

  lex->ref_count = 1;
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

static int
lexer_peek_window(Lexer* lex, unsigned start_rule, uint8_t** reach, JSContext* ctx) {
  LexerRule *rule, *start = vector_begin(&lex->rules), *end = vector_end(&lex->rules);
  uint8_t* capture[512];
  int ret = LEXER_ERROR_NOMATCH;
  size_t len = 0;

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
      const char* t = CONST_STRARRAY("executing", "compiling", )[result - LEXER_ERROR_EXEC];

      JS_ThrowInternalError(ctx, "Error %s regex /%s/", t, rule->expr);
      fprintf(stderr, "Error %s regex /%s/\n", t, rule->expr);

      ret = result;
      break;
    } else if(result > 0 && (capture[1] - capture[0]) >= 0) {

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

      if(capture[1] > *reach)
        *reach = capture[1];

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

/* bytes the window should hold ahead of the offset before matching, so short lookaheads see real data */
#define LEXER_LOOKAHEAD 256
#define LEXER_BEHIND 256
#define LEXER_LINE_MAX 4096
#define LEXER_CHUNK_MAX (1 << 20)

/* read one more chunk into the window, dropping consumed bytes first.
 * Keeps LEXER_BEHIND bytes of history (for ^, \b, lookbehind) and the current line.
 * returns 0, or -1 with an exception pending. */
static int
lexer_fill(Lexer* lex, JSContext* ctx) {
  size_t idx = LEXER_IDX(lex), keep = idx > LEXER_BEHIND ? idx - LEXER_BEHIND : 0, ls = idx;
  ssize_t n;

  if(lex->at_eof)
    return 0;

  while(ls > 0 && idx - ls < LEXER_LINE_MAX && lex->data[ls - 1] != '\n')
    ls--;

  if(ls < keep)
    keep = ls;

  if(keep > 0 && keep >= lex->size / 2) {
    memmove(lex->data, lex->data + keep, lex->size - keep);
    lex->size -= keep;
    lex->base += keep;
  }

  if(lex->size + lex->chunk > lex->capacity) {
    size_t cap = lex->capacity ? lex->capacity : lex->chunk;
    uint8_t* p;

    while(cap < lex->size + lex->chunk)
      cap *= 2;

    if(!(p = js_realloc(ctx, lex->data, cap)))
      return -1;

    lex->data = p;
    lex->capacity = cap;
  }

  if((n = reader_read(&lex->reader, lex->data + lex->size, lex->chunk)) < 0) {
    if(!JS_HasException(ctx))
      JS_ThrowInternalError(ctx, "Lexer: error reading input");
    return -1;
  }

  if(n == 0)
    lex->at_eof = TRUE;
  else if((size_t)n == lex->chunk && lex->chunk < LEXER_CHUNK_MAX)
    lex->chunk *= 2; /* a read that filled the chunk: a long token is likely, read more per call */

  lex->size += n;
  return 0;
}

/* sets the window source: `rd` is read `chunk` bytes at a time */
void
lexer_input_reader(Lexer* lex, Reader rd, size_t chunk, JSContext* ctx) {
  lexer_input_free(lex, ctx);

  lex->reader = rd;
  lex->chunk = chunk ? chunk : 8192;
  lex->base = lex->size = lex->capacity = 0;
  lex->at_eof = FALSE;
  lex->data = 0;
}

/* drops the current input: an InputBuffer or a window and its reader */
void
lexer_input_free(Lexer* lex, JSContext* ctx) {
  if(lex->reader.read) {
    reader_free(&lex->reader);
    js_free(ctx, lex->data);
    lex->data = 0;
    lex->size = 0;
    memset(&lex->reader, 0, sizeof(lex->reader));
    lex->base = lex->capacity = 0;
    lex->at_eof = FALSE;
    lex->input = INPUTBUFFER();
  } else {
    inputbuffer_free(&lex->input, ctx);
  }
}

/* peeks the next token; with a reader, retries on a bigger window while the answer could still change
 *
 * returns rule index, or a LexerResult (EOF, NOMATCH, EXCEPTION with an exception pending, ...). */
int
lexer_peek(Lexer* lex, unsigned start_rule, JSContext* ctx) {
  if(lex->loc.byte_offset == -1)
    location_zero(&lex->loc);

  for(;;) {
    uint8_t* reach = 0;
    int ret;

    if(lex->reader.read) {
      if(!lex->at_eof && lex->size - LEXER_IDX(lex) < LEXER_LOOKAHEAD) {
        if(lexer_fill(lex, ctx) < 0)
          return LEXER_EXCEPTION;
        continue;
      }

      if(LEXER_IDX(lex) >= lex->size)
        return LEXER_EOF;
    } else {
      if(lex->byte_offset >= lex->size)
        return LEXER_EOF;

      if(inputbuffer_eof(&lex->input))
        return LEXER_EOF;
    }

    ret = lexer_peek_window(lex, start_rule, &reach, ctx);

    /* no match, or a match that touches the window's end: more input may change it */
    if(lex->reader.read && !lex->at_eof && (ret == LEXER_ERROR_NOMATCH || (ret >= 0 && reach >= lex->data + lex->size))) {
      if(lexer_fill(lex, ctx) < 0)
        return LEXER_EXCEPTION;

      continue;
    }

    return ret;
  }
}

size_t
lexer_skip_n(Lexer* lex, size_t bytes) {
  size_t len;

  assert(bytes <= lex->size - LEXER_IDX(lex));

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

  assert((lex->size - LEXER_IDX(lex)) >= lex->byte_length);

  return utf8_strlen(LEXER_PTR(lex), lex->byte_length);
}

char*
lexer_lexeme(Lexer* lex, size_t* lenp) {
  char* s = (char*)LEXER_PTR(lex);

  if(lenp)
    *lenp = lex->byte_length;

  return s;
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

  lexer_input_free(lex, lex->rules.opaque);

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

Token*
lexer_token(Lexer* lex, int32_t id, JSContext* ctx) {
  size_t len;
  char* lexeme;
  Token* tok;

  if(!(lexeme = lexer_lexeme(lex, &len)))
    return 0;

  if(!(tok = token_create(id, lexeme, len, ctx)))
    return 0;

  /* a window moves on, so the token keeps its own copy */
  if(lex->reader.read) {
    if(!(tok->lexeme = js_malloc(ctx, len + 1))) {
      token_free(tok, JS_GetRuntime(ctx));
      return 0;
    }

    memcpy(tok->lexeme, lexeme, len);
    tok->lexeme[len] = 0;
    tok->owned = TRUE;
  }

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
  size_t size, start = LEXER_IDX(lex);

  while(start > 0 && lex->data[start - 1] != '\n')
    start--;

  size = byte_chr((const char*)&lex->data[start], lex->size - start, '\n');

  return js_strndup(ctx, (const char*)&lex->data[start], size);
}

char*
lexer_lexeme_s(Lexer* lex, JSContext* ctx, int (*escape_fn)(int)) {
  size_t len;
  char* s;
  DynBuf output;

  dbuf_init_ctx(ctx, &output);

  if((s = lexer_lexeme(lex, &len)))
    dbuf_put_escaped_pred(&output, s, len, escape_fn);

  dbuf_0(&output);

  return (char*)output.buf;
}

/**
 * @}
 */
