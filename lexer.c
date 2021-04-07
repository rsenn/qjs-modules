#include "lexer.h"
#include "libregexp.h"

static BOOL
lexer_expand_rule(Lexer* lex, LexerRule* rule, DynBuf* db) {
  char* p;
  size_t len;

  for(p = rule->expr; *p; p += len) {
    if(*p == '{') {
      LexerRule* subst;
      len = str_chr(p + 1, '}');
      subst = lexer_find_rule(lex, p + 1, len);
      len += 2;
      if(subst) {
        lexer_expand_rule(lex, subst, db);
        continue;
      }
    } else {
      len = str_chr(p, '{');
    }
    dbuf_append(db, p, len);
  }

  dbuf_0(db);

  return TRUE;
}

static BOOL
lexer_compile_rule(Lexer* lex, LexerRule* rule, JSContext* ctx) {
  DynBuf dbuf;
  BOOL ret;

  if(rule->bytecode)
    return TRUE;

  js_dbuf_init(ctx, &dbuf);

  if(lexer_expand_rule(lex, rule, &dbuf)) {
    RegExp re = regexp_from_dbuf(&dbuf, LRE_FLAG_GLOBAL | LRE_FLAG_STICKY);

    rule->bytecode = regexp_compile(re, ctx);
    ret = rule->bytecode != 0;
  } else {
    JS_ThrowInternalError(ctx, "Error expanding rule '%s'", rule->name);
    ret = FALSE;
  }

  dbuf_free(&dbuf);
  return ret;
}

static int
lexer_match_rule(Lexer* lex, LexerRule* rule, uint8_t** capture, JSContext* ctx) {

  if(rule->bytecode == 0) {
    if(!lexer_compile_rule(lex, rule, ctx))
      return -2;
  }

  return lre_exec(capture, rule->bytecode, (uint8_t*)lex->input.data, lex->input.pos, lex->input.size, 0, ctx);
}

void
lexer_init(Lexer* lex, int mode, JSContext* ctx) {
  memset(lex, 0, sizeof(Lexer));
  lex->mode = mode;
  vector_init(&lex->rules, ctx);
}

void
lexer_set_input(Lexer* lex, InputBuffer input, char* filename) {
  lex->input = input;
  lex->loc.file = filename;
}

int
lexer_add_rule(Lexer* lex, char* name, char* expr) {
  LexerRule rule = {name, expr, 0};
  int ret = vector_size(&lex->rules, sizeof(LexerRule));
  vector_push(&lex->rules, rule);
  return ret;
}

LexerRule*
lexer_find_rule(Lexer* lex, const char* name, size_t namelen) {
  LexerRule* rule;
  vector_foreach_t(&lex->rules, rule) {
    if(!strncmp(rule->name, name, namelen) && rule->name[namelen] == '\0')
      return rule;
  }
  return 0;
}

BOOL
lexer_compile_rules(Lexer* lex, JSContext* ctx) {
  LexerRule* rule;

  vector_foreach_t(&lex->rules, rule) {
    if(!lexer_compile_rule(lex, rule, ctx))
      return FALSE;
  }
  return TRUE;
}

int
lexer_next(Lexer* lex, JSContext* ctx) {

  LexerRule* rule;
  uint8_t* capture[512];
  int i = 0, ret = -2;
  size_t len;

  if(input_buffer_eof(&lex->input))
    return -1;

  lex->start = lex->input.pos;

  vector_foreach_t(&lex->rules, rule) {
    size_t start, end;
    int result;

    result = lexer_match_rule(lex, rule, capture, ctx);

    if(result < 0) {

      JSValue exception = JS_GetException(ctx);
      const char* error = JS_ToCString(ctx, exception);

      printf(
          "%s:%" PRIu32 ":%" PRIu32 " #%i %-20s - result: %i error: %s\n", lex->loc.file, lex->loc.line + 1, lex->loc.column + 1, i, rule->name, result, error);
      JS_FreeCString(ctx, error);

    } else if(result > 0) {

      if((lex->mode & LEXER_LONGEST) == 0 || ret < 0 || (capture[1] - capture[0]) > len) {
        ret = i;
        len = capture[1] - capture[0];

        start = capture[0] - lex->input.data;
        end = capture[1] - lex->input.data;

        printf("%s:%" PRIu32 ":%" PRIu32 " #%i %-20s - [%zu] %.*s\n",
               lex->loc.file,
               lex->loc.line + 1,
               lex->loc.column + 1,
               i,
               rule->name,
               len,
               (int)len,
               capture[0]);
      }
    }
    i++;
  }

  if(ret >= 0) {
    while(len-- > 0) {
      if(input_buffer_getc(&lex->input) == '\n') {
        lex->loc.line++;
        lex->loc.column = 0;
      } else {
        lex->loc.column++;
      }
    }
  }

  // assert(ret >= 0);
  return ret;
}

static int
lexer_free_rule(LexerRule* rule, JSContext* ctx) {
  js_free(ctx, rule->name);
  js_free(ctx, rule->expr);

  if(rule->bytecode)
    js_free(ctx, rule->bytecode);
}

void
lexer_free(Lexer* lex, JSContext* ctx) {
  LexerRule* rule;

  if(!ctx)
    ctx = lex->rules.opaque;

  input_buffer_free(&lex->input, ctx);

  vector_foreach_t(&lex->rules, rule) { lexer_free_rule(rule, ctx); }

  vector_free(&lex->rules);
}