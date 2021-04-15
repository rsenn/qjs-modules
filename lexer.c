#include "lexer.h"
#include "libregexp.h"

void
location_print(const Location* loc, DynBuf* dbuf) {
  if(loc->file) {
    dbuf_putstr(dbuf, loc->file);
    dbuf_putc(dbuf, ':');
  }
  dbuf_printf(dbuf, "%" PRId32 ":%" PRId32, loc->line + 1, loc->column + 1);
}

Location
location_dup(const Location* loc, JSContext* ctx) {
  Location ret = {0, 0, 0, 0};
  if(loc->file)
    ret.file = js_strdup(ctx, loc->file);
  ret.line = loc->line;
  ret.column = loc->column;
  ret.pos = loc->pos;
  return ret;
}

void
location_free(Location* loc, JSRuntime* rt) {
  if(loc->file)
    js_free_rt(rt, (char*)loc->file);
  memset(loc, 0, sizeof(Location));
}

BOOL
lexer_rule_expand(Lexer* lex, LexerRule* rule, DynBuf* db) {
  char* p;
  size_t len;

  for(p = rule->expr; *p; p++) {
    if(*p == '{') {
      if(p[len = str_chr(p, '}')]) {
        LexerRule* subst;
        if((subst = lexer_find_definition(lex, p + 1, len - 1))) {
          lexer_rule_expand(lex, subst, db);
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
  DynBuf dbuf;
  BOOL ret;

  if(rule->bytecode)
    return TRUE;

  js_dbuf_init(ctx, &dbuf);

  if(lexer_rule_expand(lex, rule, &dbuf)) {
    RegExp re = regexp_from_dbuf(&dbuf, LRE_FLAG_GLOBAL | LRE_FLAG_STICKY);

    // printf("rule %s /%s/\n", rule->name, re.source);

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
lexer_rule_match(Lexer* lex, LexerRule* rule, uint8_t** capture, JSContext* ctx) {

  if(rule->bytecode == 0) {
    if(!lexer_rule_compile(lex, rule, ctx))
      return LEXER_ERROR_COMPILE;
  }

  return lre_exec(capture, rule->bytecode, (uint8_t*)lex->input.data, lex->input.pos, lex->input.size, 0, ctx);
}

void
lexer_init(Lexer* lex, enum lexer_mode mode, JSContext* ctx) {
  memset(lex, 0, sizeof(Lexer));
  lex->mode = mode;
  vector_init(&lex->defines, ctx);
  vector_init(&lex->rules, ctx);
}

void
lexer_set_input(Lexer* lex, InputBuffer input, char* filename) {
  lex->input = input;
  lex->loc.file = filename;
}

void
lexer_define(Lexer* lex, char* name, char* expr) {
  LexerRule definition = {name, expr, MASK_ALL, 0};
  vector_size(&lex->defines, sizeof(LexerRule));
  vector_push(&lex->defines, definition);
}

int
lexer_rule_add(Lexer* lex, char* name, char* expr) {
  LexerRule rule = {name, expr, MASK_ALL, 0}, *previous;
  int ret = vector_size(&lex->rules, sizeof(LexerRule));
  if(ret) {
    previous = vector_back(&lex->rules, sizeof(LexerRule));
    rule.mask = previous->mask;
  }
  vector_push(&lex->rules, rule);
  return ret;
}

LexerRule*
lexer_rule_find(Lexer* lex, const char* name) {
  LexerRule* rule;
  vector_foreach_t(&lex->rules, rule) {
    if(!strcmp(rule->name, name))
      return rule;
  }
  return 0;
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
  size_t len;

  if(input_buffer_eof(&lex->input))
    return LEXER_EOF;

  lex->start = lex->input.pos;

  vector_foreach_t(&lex->rules, rule) {
    int result;

    if((state & rule->mask) == 0)
      continue;

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
    lex->bytelen = len;
    lex->tokid = i;
  }

  return ret;
}

size_t
lexer_skip(Lexer* lex) {
  size_t end = lex->input.pos + lex->bytelen;
  size_t n = 0;

  while(lex->input.pos < end) {
    size_t prev = lex->input.pos;

    if(input_buffer_getc(&lex->input) == '\n') {
      lex->loc.line++;
      lex->loc.column = 0;
    } else {
      lex->loc.column++;
    }
    lex->loc.pos++;
    n++;
  }

  // printf("skipped %lu chars (%lu bytes)\n", n, lex->bytelen);
  lex->bytelen = 0;

  return n;
}

int
lexer_next(Lexer* lex, uint64_t state, JSContext* ctx) {
  int ret;

  ret = lexer_peek(lex, state, ctx);

  if(ret >= 0)
    lexer_skip(lex);

  return ret;
}

static void
lexer_rule_free(LexerRule* rule, JSContext* ctx) {
  js_free(ctx, rule->name);
  js_free(ctx, rule->expr);

  if(rule->bytecode)
    js_free(ctx, rule->bytecode);
}

void
lexer_dump(Lexer* lex, DynBuf* dbuf) {
  dbuf_printf(dbuf, "Lexer {\n  mode: %x,\n  start: %zu", lex->mode, lex->start);
  dbuf_putstr(dbuf, ",\n  input: ");
  input_buffer_dump(&lex->input, dbuf);
  dbuf_putstr(dbuf, ",\n  location: ");
  location_print(&lex->loc, dbuf);
  dbuf_putstr(dbuf, "\n}");
}

void
lexer_free(Lexer* lex, JSContext* ctx) {
  LexerRule* rule;

  if(!ctx)
    ctx = lex->rules.opaque;

  input_buffer_free(&lex->input, ctx);

  vector_foreach_t(&lex->rules, rule) { lexer_rule_free(rule, ctx); }

  vector_free(&lex->rules);
}
