#include <nexus.h>

#include "../logos.h"
#include "./l_internal.h"

/* ---------------------------------------------------------------------------- */
/* TOKENS                                                                        */
/* ---------------------------------------------------------------------------- */

typedef enum LGTokenKind {
  LGT_EOF,
  LGT_IDENT,
  LGT_DOLLAR, /* $name or $_name */
  LGT_PARAM,  /* $$name */
  LGT_HASH,   /* #name */
  LGT_STRING, /* 'literal' */
  LGT_TEXT,   /* "text" */
  LGT_INT,
  LGT_DOTDOT,
  LGT_DOT,
  LGT_UNION, /* || */
  LGT_BAR,   /* | */
  LGT_BANG,  /* ! */
  LGT_ARROW, /* -> */
  LGT_AT,    /* @name */
  LGT_LBRACE,
  LGT_RBRACE,
  LGT_LPAREN,
  LGT_RPAREN,
  LGT_LANGLE,
  LGT_RANGLE,
  LGT_LBRACKET,
  LGT_RBRACKET,
  LGT_COLON,
  LGT_COMMA,
  LGT_EQ,
  LGT_EQEQ,
  LGT_NEQ,
  LGT_QUESTION,
  LGT_STAR,
  LGT_PLUS,
  LGT_MINUS,
  LGT_ERROR
} LGTokenKind;

typedef struct LGLexed {
  LGTokenKind kind;
  uint32      offset;
  uint32      width;
  uint32      line_break_before; /* Nonzero when a newline separates this token from the previous one. */
} LGLexed;

typedef struct LGParser {
  LGenesisCompiler   *compiler;
  NexusDataArena     *arena;
  const PharosSource *source;
  uint32              source_id;
  LGLexed            *tokens;
  uint_large          count;
  uint_large          position;
  boolean             ok;
  boolean             panic;
  uint32              decl_column; /* The column of the definition being parsed: a token starting a line at or left of it starts a new declaration. */
  uint32              nesting;     /* Open parentheses and braces inside the definition. */
} LGParser;

/* ---------------------------------------------------------------------------- */
/* LEXING                                                                        */
/* ---------------------------------------------------------------------------- */

static boolean l_genesis_ident_start(byte character) {
  return (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z') || character == '_';
}

static boolean l_genesis_ident_part(byte character) {
  return l_genesis_ident_start(character) || (character >= '0' && character <= '9');
}

static void l_genesis_lex_error(LGParser *parser, uint32 offset, uint32 width, const char *what) {
  PharosDiagnostic *diagnostic;
  PharosSpan        span;

  span.source = parser->source_id;
  span.offset = offset;
  span.width  = width;
  diagnostic  = pharos_diagnostics_report((parser->compiler)->reporting, PDS_ERROR, "G0002", span, "%s", what);
  pharos_diagnostics_label_add(diagnostic, span, TRUE, NULL);
  parser->ok = FALSE;
}

/* Summary: tokenizes the whole source; comments and whitespace are dropped and newlines are remembered. */
static void l_genesis_lex(LGParser *parser) {
  const byte    *text;
  uint_large     size;
  uint_large     i;
  uint_large     start;
  NexusDataList *list;
  LGLexed        token;
  uint32         breaks;

  text   = parser->source->text;
  size   = parser->source->size;
  list   = nexus_data_list_create(NEXUS_SIZEOF(LGLexed), 256);
  i      = 0;
  breaks = 0;

  while (i < size) {
    if (text[i] == '\n') {
      breaks++;
      i++;
      continue;
    }

    if (text[i] == ' ' || text[i] == '\t' || text[i] == '\r') {
      i++;
      continue;
    }

    if (text[i] == '/' && i + 1 < size && text[i + 1] == '/') {
      while (i < size && text[i] != '\n') {
        i++;
      }

      continue;
    }

    if (text[i] == '/' && i + 1 < size && text[i + 1] == '*') {
      start = i;
      i += 2;

      while (i + 1 < size && !(text[i] == '*' && text[i + 1] == '/')) {
        if (text[i] == '\n') {
          breaks++;
        }

        i++;
      }

      if (i + 1 >= size) {
        l_genesis_lex_error(parser, (uint32)start, 2, "unterminated block comment");
        i = size;
      } else {
        i += 2;
      }

      continue;
    }

    start                   = i;
    token.line_break_before = breaks;
    breaks                  = 0;
    token.kind              = LGT_ERROR;

    if (text[i] == '$') {
      i++;

      if (i < size && text[i] == '$') {
        i++;
        token.kind = LGT_PARAM;
      } else {
        token.kind = LGT_DOLLAR;
      }

      if (i >= size || !l_genesis_ident_start(text[i])) {
        l_genesis_lex_error(parser, (uint32)start, (uint32)(i - start), "a `$` must be followed by a name");
        token.kind = LGT_ERROR;
      } else {
        while (i < size && l_genesis_ident_part(text[i])) {
          i++;
        }
      }
    } else if (text[i] == '#') {
      i++;

      if (i >= size || !l_genesis_ident_start(text[i])) {
        l_genesis_lex_error(parser, (uint32)start, 1, "a `#` must be followed by a rule name");
      } else {
        token.kind = LGT_HASH;

        while (i < size && l_genesis_ident_part(text[i])) {
          i++;
        }
      }
    } else if (text[i] == '@') {
      i++;

      if (i < size && l_genesis_ident_start(text[i])) {
        token.kind = LGT_AT;

        while (i < size && l_genesis_ident_part(text[i])) {
          i++;
        }
      } else {
        l_genesis_lex_error(parser, (uint32)start, 1, "an `@` must be followed by an annotation name");
      }
    } else if (l_genesis_ident_start(text[i])) {
      token.kind = LGT_IDENT;

      while (i < size && l_genesis_ident_part(text[i])) {
        i++;
      }
    } else if (text[i] >= '0' && text[i] <= '9') {
      token.kind = LGT_INT;

      while (i < size && text[i] >= '0' && text[i] <= '9') {
        i++;
      }
    } else if (text[i] == '\'' || text[i] == '"') {
      byte quote;

      quote      = text[i];
      token.kind = quote == '\'' ? LGT_STRING : LGT_TEXT;
      i++;

      while (i < size && text[i] != quote && text[i] != '\n') {
        if (text[i] == '\\' && i + 1 < size) {
          i++;
        }

        i++;
      }

      if (i >= size || text[i] != quote) {
        l_genesis_lex_error(parser, (uint32)start, (uint32)(i - start), "unterminated string literal");
        token.kind = LGT_ERROR;
      } else {
        i++;
      }
    } else {
      LGSpecPunctuationId punctuation;
      uint32              punctuation_length;

      punctuation_length = logos_languages_genesis_spec_punctuation_match(text + start, size - start, &punctuation);

      if (punctuation_length == 0) {
        i++;

        if (text[start] == '!') {
          l_genesis_lex_error(parser, (uint32)start, 1, "unexpected `!`; the comparison operators are `==` and `!=`");
        } else {
          l_genesis_lex_error(parser, (uint32)start, 1, "unexpected character");
        }
      } else {
        i += punctuation_length;

        switch (punctuation) {
        case LGSP_LBRACE:
          token.kind = LGT_LBRACE;
          break;
        case LGSP_RBRACE:
          token.kind = LGT_RBRACE;
          break;
        case LGSP_LPAREN:
          token.kind = LGT_LPAREN;
          break;
        case LGSP_RPAREN:
          token.kind = LGT_RPAREN;
          break;
        case LGSP_LANGLE:
          token.kind = LGT_LANGLE;
          break;
        case LGSP_RANGLE:
          token.kind = LGT_RANGLE;
          break;
        case LGSP_LBRACKET:
          token.kind = LGT_LBRACKET;
          break;
        case LGSP_RBRACKET:
          token.kind = LGT_RBRACKET;
          break;
        case LGSP_COLON:
          token.kind = LGT_COLON;
          break;
        case LGSP_COMMA:
          token.kind = LGT_COMMA;
          break;
        case LGSP_ARROW:
          token.kind = LGT_ARROW;
          break;
        case LGSP_EQUALS_EQUALS:
          token.kind = LGT_EQEQ;
          break;
        case LGSP_NOT_EQUALS:
          token.kind = LGT_NEQ;
          break;
        case LGSP_EQUALS:
          token.kind = LGT_EQ;
          break;
        case LGSP_QUESTION:
          token.kind = LGT_QUESTION;
          break;
        case LGSP_STAR:
          token.kind = LGT_STAR;
          break;
        case LGSP_PLUS:
          token.kind = LGT_PLUS;
          break;
        case LGSP_MINUS:
          token.kind = LGT_MINUS;
          break;
        case LGSP_DOT_DOT:
          token.kind = LGT_DOTDOT;
          break;
        case LGSP_DOT:
          token.kind = LGT_DOT;
          break;
        case LGSP_UNION:
          token.kind = LGT_UNION;
          break;
        case LGSP_BAR:
          token.kind = LGT_BAR;
          break;
        case LGSP_BANG:
          token.kind = LGT_BANG;
          break;
        case LGSP_COUNT:
        default:
          token.kind = LGT_ERROR;
          break;
        }
      }
    }

    if (token.kind == LGT_ERROR) {
      continue;
    }

    token.offset = (uint32)start;
    token.width  = (uint32)(i - start);
    (void)nexus_data_list_append(list, &token);
  }

  token.kind              = LGT_EOF;
  token.offset            = (uint32)size;
  token.width             = 0;
  token.line_break_before = breaks;
  (void)nexus_data_list_append(list, &token);

  parser->count  = nexus_data_list_length_get(list);
  parser->tokens = (LGLexed *)nexus_data_arena_allocate(parser->arena, parser->count * NEXUS_SIZEOF(LGLexed));
  nexus_memory_bytes_copy(nexus_data_list_data_get_const(list), parser->tokens, parser->count * NEXUS_SIZEOF(LGLexed));
  nexus_data_list_destroy(list);
}

/* ---------------------------------------------------------------------------- */
/* PARSER SUPPORT                                                                */
/* ---------------------------------------------------------------------------- */

static const LGLexed *l_genesis_peek(const LGParser *parser, uint_large ahead) {
  uint_large index;

  index = parser->position + ahead;

  if (index >= parser->count) {
    index = parser->count - 1;
  }

  return &parser->tokens[index];
}

static const char *l_genesis_kind_name(LGTokenKind kind) {
  switch (kind) {
  case LGT_EOF:
    return "the end of the file";
  case LGT_IDENT:
    return "a name";
  case LGT_DOLLAR:
    return "a `$` token";
  case LGT_PARAM:
    return "a `$$` parameter";
  case LGT_HASH:
    return "a `#` rule";
  case LGT_STRING:
    return "a quoted literal";
  case LGT_TEXT:
    return "a text value";
  case LGT_INT:
    return "a number";
  case LGT_DOTDOT:
    return "`..`";
  case LGT_DOT:
    return "`.`";
  case LGT_UNION:
    return "`||`";
  case LGT_BAR:
    return "`|`";
  case LGT_BANG:
    return "`!`";
  case LGT_ARROW:
    return "`->`";
  case LGT_AT:
    return "an `@` annotation";
  case LGT_LBRACE:
    return "`{`";
  case LGT_RBRACE:
    return "`}`";
  case LGT_LPAREN:
    return "`(`";
  case LGT_RPAREN:
    return "`)`";
  case LGT_LANGLE:
    return "`<`";
  case LGT_RANGLE:
    return "`>`";
  case LGT_LBRACKET:
    return "`[`";
  case LGT_RBRACKET:
    return "`]`";
  case LGT_COLON:
    return "`:`";
  case LGT_COMMA:
    return "`,`";
  case LGT_EQ:
    return "`=`";
  case LGT_EQEQ:
    return "`==`";
  case LGT_NEQ:
    return "`!=`";
  case LGT_QUESTION:
    return "`?`";
  case LGT_STAR:
    return "`*`";
  case LGT_PLUS:
    return "`+`";
  case LGT_MINUS:
    return "`-`";
  case LGT_ERROR:
  default:
    return "an unexpected character";
  }
}

static PharosSpan l_genesis_span_of(const LGParser *parser, const LGLexed *token) {
  PharosSpan span;

  span.source = parser->source_id;
  span.offset = token->offset;
  span.width  = token->width;

  return span;
}

static PharosSpan l_genesis_span_join(PharosSpan first, PharosSpan last) {
  PharosSpan span;

  span       = first;
  span.width = (last.offset + last.width) - first.offset;

  return span;
}

static PharosSpan l_genesis_previous_span(const LGParser *parser) {
  return l_genesis_span_of(parser, &parser->tokens[parser->position > 0 ? parser->position - 1 : 0]);
}

static void l_genesis_unexpected(LGParser *parser, const char *expected) {
  const LGLexed    *token;
  PharosDiagnostic *diagnostic;
  PharosSpan        span;

  if (parser->panic) {
    return;
  }

  token      = l_genesis_peek(parser, 0);
  span       = l_genesis_span_of(parser, token);
  diagnostic = pharos_diagnostics_report((parser->compiler)->reporting, PDS_ERROR, "G0001", span, "expected %s, found %s", expected,
                                         l_genesis_kind_name(token->kind));
  pharos_diagnostics_label_add(diagnostic, span, TRUE, "expected %s here", expected);
  parser->ok    = FALSE;
  parser->panic = TRUE;
}

static boolean l_genesis_accept(LGParser *parser, LGTokenKind kind) {
  if (l_genesis_peek(parser, 0)->kind == kind) {
    parser->position++;

    return TRUE;
  }

  return FALSE;
}

static boolean l_genesis_expect(LGParser *parser, LGTokenKind kind) {
  if (l_genesis_accept(parser, kind)) {
    return TRUE;
  }

  l_genesis_unexpected(parser, l_genesis_kind_name(kind));

  return FALSE;
}

/* Summary: the text of a token, copied into the arena without its sigil: skip leading $, $$ or #. */
static const char *l_genesis_token_name(const LGParser *parser, const LGLexed *token) {
  uint32 skip;

  skip = 0;

  if (token->kind == LGT_PARAM) {
    skip = 2;
  } else if (token->kind == LGT_DOLLAR || token->kind == LGT_HASH || token->kind == LGT_AT) {
    skip = 1;
  }

  return nexus_data_arena_text_copy(parser->arena, (const char *)parser->source->text + token->offset + skip, token->width - skip);
}

/* Summary: whether the token is an @annotation whose text is that of a spec word such as "@if". */
static boolean l_genesis_annotation_is(const LGParser *parser, const LGLexed *token, LGSpecWordId word) {
  const char *text;
  uint_large  length;

  text   = logos_languages_genesis_spec_word_text_get(word);
  length = nexus_strings_string_length_get(text);

  return token->kind == LGT_AT && token->width == length &&
         nexus_memory_bytes_equal(parser->source->text + token->offset, (const byte *)text, length);
}

static boolean l_genesis_ident_is(const LGParser *parser, const LGLexed *token, const char *word) {
  uint_large length;

  if (token->kind != LGT_IDENT) {
    return FALSE;
  }

  length = nexus_strings_string_length_get(word);

  return token->width == length && nexus_memory_bytes_equal(parser->source->text + token->offset, (const byte *)word, length);
}

static LGExpr *l_genesis_expr_new(LGParser *parser, LGExprKind kind, PharosSpan span) {
  LGExpr *expr;

  expr       = (LGExpr *)nexus_data_arena_allocate(parser->arena, NEXUS_SIZEOF(*expr));
  expr->kind = kind;
  expr->span = span;

  return expr;
}

static LGDecl *l_genesis_decl_new(LGParser *parser, LGDeclKind kind, PharosSpan span) {
  LGDecl *decl;

  decl       = (LGDecl *)nexus_data_arena_allocate(parser->arena, NEXUS_SIZEOF(*decl));
  decl->kind = kind;
  decl->span = span;

  return decl;
}

/* Summary: copies a list of pointers into an arena array. */
static void *l_genesis_array(LGParser *parser, const NexusDataList *list, uint32 *out_count) {
  uint_large count;
  void     **array;
  uint_large i;

  count      = nexus_data_list_length_get(list);
  *out_count = (uint32)count;
  array      = (void **)nexus_data_arena_allocate(parser->arena, (count > 0 ? count : 1) * NEXUS_EFFECTIVE_POINTER_SIZE_BYTES);

  for (i = 0; i < count; i++) {
    array[i] = nexus_data_list_pointer_at(list, i);
  }

  return (void *)array;
}

/* ---------------------------------------------------------------------------- */
/* STRING LITERALS                                                               */
/* ---------------------------------------------------------------------------- */

/* Summary: decodes the body of a quoted literal into Unicode scalar values; reports bad escapes. */
static void l_genesis_literal_decode(LGParser *parser, const LGLexed *token, uint32 **out_codepoints, uint32 *out_count) {
  const byte *text;
  uint_large  i;
  uint_large  end;
  uint32     *codepoints;
  uint32      count;
  uint32      value;
  uint32      continuation;
  uint32      digits;

  text       = parser->source->text;
  i          = token->offset + 1;
  end        = token->offset + token->width - 1;
  codepoints = (uint32 *)nexus_data_arena_allocate(parser->arena, ((end - i) + 1) * NEXUS_SIZEOF(uint32));
  count      = 0;

  while (i < end) {
    if (text[i] == '\\') {
      i++;

      if (i >= end) {
        break;
      }

      switch (text[i]) {
      case 't':
        value = '\t';
        i++;
        break;
      case 'n':
        value = '\n';
        i++;
        break;
      case 'r':
        value = '\r';
        i++;
        break;
      case '0':
        value = 0;
        i++;
        break;
      case '\\':
      case '\'':
      case '"':
        value = text[i];
        i++;
        break;
      case 'u':
      case 'x': {
        boolean wide;

        wide = text[i] == 'u';
        i++;
        value = 0;

        if (wide && i < end && text[i] == '{') {
          i++;
          digits = 0;

          while (i < end && text[i] != '}') {
            byte digit;

            digit = text[i];
            value *= 16;

            if (digit >= '0' && digit <= '9') {
              value += digit - '0';
            } else if (digit >= 'a' && digit <= 'f') {
              value += digit - 'a' + 10;
            } else if (digit >= 'A' && digit <= 'F') {
              value += digit - 'A' + 10;
            } else {
              l_genesis_lex_error(parser, (uint32)i, 1, "a `\\u{...}` escape holds hexadecimal digits");
              break;
            }

            digits++;
            i++;
          }

          if (i < end && text[i] == '}') {
            i++;
          }

          if (digits == 0 || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) {
            l_genesis_lex_error(parser, token->offset, token->width,
                                "an escape must name a Unicode scalar value, not a surrogate or a value above U+10FFFF");
          }
        } else {
          digits = 0;

          while (digits < 2 && i < end &&
                 ((text[i] >= '0' && text[i] <= '9') || (text[i] >= 'a' && text[i] <= 'f') || (text[i] >= 'A' && text[i] <= 'F'))) {
            value *= 16;
            value += text[i] <= '9' ? text[i] - '0' : (text[i] | 0x20) - 'a' + 10;
            digits++;
            i++;
          }

          if (digits != 2) {
            l_genesis_lex_error(parser, token->offset, token->width, "a `\\x` escape needs exactly two hexadecimal digits");
          }
        }
      } break;
      default:
        l_genesis_lex_error(parser, (uint32)i - 1, 2, "unknown escape sequence");
        value = text[i];
        i++;
        break;
      }
    } else if (text[i] < 0x80) {
      value = text[i];
      i++;
    } else {
      /* A UTF-8 sequence in the source is one scalar value. */
      continuation = 1;
      value        = text[i] & 0x1FU;

      if (text[i] >= 0xF0) {
        continuation = 3;
        value        = text[i] & 0x07U;
      } else if (text[i] >= 0xE0) {
        continuation = 2;
        value        = text[i] & 0x0FU;
      }

      i++;

      while (continuation > 0 && i < end && (text[i] & 0xC0) == 0x80) {
        value = (value << 6) | (text[i] & 0x3FU);
        i++;
        continuation--;
      }
    }

    codepoints[count++] = value;
  }

  *out_codepoints = codepoints;
  *out_count      = count;
}

/* ---------------------------------------------------------------------------- */
/* EXPRESSIONS                                                                   */
/* ---------------------------------------------------------------------------- */

static LGExpr *l_genesis_parse_union(LGParser *parser);
static LGExpr *l_genesis_parse_postfix(LGParser *parser);
static LGDecl *l_genesis_parse_decl(LGParser *parser, boolean in_pratt);

/* Summary: tells whether the tokens at the position begin a definition: a name, annotations, then `=`. */
static boolean l_genesis_definition_start(const LGParser *parser) {
  const LGLexed *token;
  uint_large     ahead;
  int32          depth;

  token = l_genesis_peek(parser, 0);

  if (token->kind != LGT_DOLLAR && token->kind != LGT_HASH && token->kind != LGT_PARAM) {
    return FALSE;
  }

  ahead = 1;

  /* Annotations between the name and the `=`, with balanced parentheses. */
  for (;;) {
    token = l_genesis_peek(parser, ahead);

    if (token->kind == LGT_EQ) {
      return TRUE;
    }

    if (token->kind != LGT_AT) {
      return FALSE;
    }

    ahead++;
    token = l_genesis_peek(parser, ahead);

    if (token->kind != LGT_LPAREN) {
      return FALSE;
    }

    depth = 0;

    do {
      token = l_genesis_peek(parser, ahead);

      if (token->kind == LGT_LPAREN) {
        depth++;
      } else if (token->kind == LGT_RPAREN) {
        depth--;
      } else if (token->kind == LGT_EOF) {
        return FALSE;
      }

      ahead++;
    } while (depth > 0);
  }
}

/* Summary: tells whether the next token can begin an atom of a concatenation. */
static uint32 l_genesis_column(const LGParser *parser, uint32 offset) {
  uint32 column;

  column = 0;

  while (column < offset && parser->source->text[offset - column - 1] != '\n') {
    column++;
  }

  return column;
}

static boolean l_genesis_atom_start(const LGParser *parser) {
  const LGLexed *token;

  token = l_genesis_peek(parser, 0);

  /* Offside rule: at the top of a definition, a line that starts at its column or left of it is the next declaration. */
  if (parser->nesting == 0 && token->line_break_before > 0 && l_genesis_column(parser, token->offset) <= parser->decl_column) {
    return FALSE;
  }

  switch (token->kind) {
  case LGT_STRING:
  case LGT_DOLLAR:
  case LGT_PARAM:
  case LGT_HASH:
  case LGT_LPAREN:
  case LGT_TEXT:
  case LGT_INT:
  case LGT_IDENT:
    return !l_genesis_definition_start(parser);
  case LGT_BANG:
  case LGT_DOT:
    return TRUE;
  case LGT_AT:
    return l_genesis_annotation_is(parser, token, LGSW_IF);
  default:
    return FALSE;
  }
}

static void l_genesis_parse_arguments(LGParser *parser, LGExpr *call) {
  NexusDataList *values;
  NexusDataList *names;
  NexusDataList *spans;
  const LGLexed *token;
  const char    *name;
  PharosSpan     name_span;
  LGExpr        *value;

  parser->nesting++;
  values = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 4);
  names  = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 4);
  spans  = nexus_data_list_create(NEXUS_SIZEOF(PharosSpan), 4);

  while (!parser->panic && l_genesis_peek(parser, 0)->kind != LGT_RPAREN && l_genesis_peek(parser, 0)->kind != LGT_EOF) {
    name      = NULL;
    token     = l_genesis_peek(parser, 0);
    name_span = l_genesis_span_of(parser, token);

    if (token->kind == LGT_IDENT && l_genesis_peek(parser, 1)->kind == LGT_COLON) {
      name = l_genesis_token_name(parser, token);
      parser->position += 2;
    }

    value = l_genesis_parse_union(parser);

    if (value == NULL) {
      break;
    }

    (void)nexus_data_list_append_pointer(values, value);
    (void)nexus_data_list_append_pointer(names, name);
    (void)nexus_data_list_append(spans, &name_span);

    if (!l_genesis_accept(parser, LGT_COMMA)) {
      break;
    }
  }

  (void)l_genesis_expect(parser, LGT_RPAREN);
  parser->nesting--;
  call->items     = (LGExpr **)l_genesis_array(parser, values, &call->item_count);
  call->arg_names = (const char **)l_genesis_array(parser, names, &call->item_count);
  call->arg_spans = (PharosSpan *)nexus_data_arena_allocate(parser->arena, (call->item_count > 0 ? call->item_count : 1) * NEXUS_SIZEOF(PharosSpan));

  if (call->item_count > 0) {
    nexus_memory_bytes_copy(nexus_data_list_data_get_const(spans), call->arg_spans, (uint_large)call->item_count * NEXUS_SIZEOF(PharosSpan));
  }

  nexus_data_list_destroy(values);
  nexus_data_list_destroy(names);
  nexus_data_list_destroy(spans);
}

static LGExpr *l_genesis_parse_atom(LGParser *parser);

/* Summary: the rest of an `@if (a == b) { ... } @else ...` expression once its `@if` was consumed. */
static LGExpr *l_genesis_parse_if(LGParser *parser, PharosSpan span) {
  LGExpr        *expr;
  LGExpr        *condition;
  LGExpr        *left;
  LGExpr        *right;
  const LGLexed *next;

  expr = l_genesis_expr_new(parser, LGE_IF, span);

  if (!l_genesis_expect(parser, LGT_LPAREN)) {
    return NULL;
  }

  left      = l_genesis_parse_atom(parser);
  condition = l_genesis_expr_new(parser, LGE_COMPARE, span);

  if (left == NULL) {
    return NULL;
  }

  if (l_genesis_accept(parser, LGT_EQEQ)) {
    condition->compare_op = '=';
  } else if (l_genesis_accept(parser, LGT_NEQ)) {
    condition->compare_op = '!';
  } else {
    l_genesis_unexpected(parser, "`==` or `!=`");

    return NULL;
  }

  right = l_genesis_parse_atom(parser);

  if (right == NULL || !l_genesis_expect(parser, LGT_RPAREN) || !l_genesis_expect(parser, LGT_LBRACE)) {
    return NULL;
  }

  condition->items      = (LGExpr **)nexus_data_arena_allocate(parser->arena, 2 * NEXUS_EFFECTIVE_POINTER_SIZE_BYTES);
  condition->items[0]   = left;
  condition->items[1]   = right;
  condition->item_count = 2;
  expr->items           = (LGExpr **)nexus_data_arena_allocate(parser->arena, NEXUS_EFFECTIVE_POINTER_SIZE_BYTES);
  expr->items[0]        = condition;
  expr->item_count      = 1;
  parser->nesting++;
  expr->child = l_genesis_parse_union(parser);
  parser->nesting--;

  if (!l_genesis_expect(parser, LGT_RBRACE)) {
    return NULL;
  }

  next = l_genesis_peek(parser, 0);

  if (l_genesis_annotation_is(parser, next, LGSW_ELSE)) {
    parser->position++;
    next = l_genesis_peek(parser, 0);

    if (next->kind == LGT_IDENT && l_genesis_ident_is(parser, next, logos_languages_genesis_spec_word_text_get(LGSW_IF) + 1)) {
      parser->position++;
      expr->else_branch = l_genesis_parse_if(parser, l_genesis_span_of(parser, next));
    } else if (l_genesis_expect(parser, LGT_LBRACE)) {
      parser->nesting++;
      expr->else_branch = l_genesis_parse_union(parser);
      parser->nesting--;
      (void)l_genesis_expect(parser, LGT_RBRACE);
    }
  }

  return expr;
}

/* Summary: a value of a comparison or argument that is a single atom. */
static LGExpr *l_genesis_parse_atom(LGParser *parser) {
  const LGLexed *token;
  const LGLexed *next;
  LGExpr        *expr;
  LGExpr        *item;
  PharosSpan     span;

  token = l_genesis_peek(parser, 0);
  span  = l_genesis_span_of(parser, token);

  switch (token->kind) {
  case LGT_STRING:
    expr = l_genesis_expr_new(parser, LGE_STRING, span);
    parser->position++;
    l_genesis_literal_decode(parser, token, &expr->codepoints, &expr->codepoint_count);
    next = l_genesis_peek(parser, 0);

    if (next->kind == LGT_DOTDOT && l_genesis_peek(parser, 1)->kind == LGT_STRING) {
      const LGLexed *high;
      uint32        *high_codepoints;
      uint32         high_count;

      high = l_genesis_peek(parser, 1);
      parser->position += 2;
      l_genesis_literal_decode(parser, high, &high_codepoints, &high_count);
      expr->kind = LGE_RANGE;
      expr->span = l_genesis_span_join(span, l_genesis_span_of(parser, high));
      expr->lo   = expr->codepoint_count == 1 ? expr->codepoints[0] : 0xFFFFFFFFU;
      expr->hi   = high_count == 1 ? high_codepoints[0] : 0xFFFFFFFFU;

      if (expr->lo == 0xFFFFFFFFU || expr->hi == 0xFFFFFFFFU) {
        l_genesis_lex_error(parser, expr->span.offset, expr->span.width, "both ends of a range must be single characters");
        parser->ok = FALSE;
      } else if (expr->lo > expr->hi) {
        l_genesis_lex_error(parser, expr->span.offset, expr->span.width, "the start of a range must not be above its end");
        parser->ok = FALSE;
      }
    }

    return expr;
  case LGT_TEXT:
    expr = l_genesis_expr_new(parser, LGE_TEXT, span);
    parser->position++;
    {
      uint32 *codepoints;
      uint32  count;
      uint32  i;
      char   *bytes;
      uint32  length;

      l_genesis_literal_decode(parser, token, &codepoints, &count);
      bytes  = (char *)nexus_data_arena_allocate(parser->arena, ((uint_large)count * 4) + 1);
      length = 0;

      for (i = 0; i < count; i++) {
        uint32 value;

        value = codepoints[i];

        if (value < 0x80) {
          bytes[length++] = (char)value;
        } else if (value < 0x800) {
          bytes[length++] = (char)(0xC0 | (value >> 6));
          bytes[length++] = (char)(0x80 | (value & 0x3F));
        } else if (value < 0x10000) {
          bytes[length++] = (char)(0xE0 | (value >> 12));
          bytes[length++] = (char)(0x80 | ((value >> 6) & 0x3F));
          bytes[length++] = (char)(0x80 | (value & 0x3F));
        } else {
          bytes[length++] = (char)(0xF0 | (value >> 18));
          bytes[length++] = (char)(0x80 | ((value >> 12) & 0x3F));
          bytes[length++] = (char)(0x80 | ((value >> 6) & 0x3F));
          bytes[length++] = (char)(0x80 | (value & 0x3F));
        }
      }

      expr->name = bytes;
    }

    return expr;
  case LGT_INT:
    expr = l_genesis_expr_new(parser, LGE_INT, span);
    parser->position++;
    {
      uint_large i;

      for (i = token->offset; i < token->offset + token->width; i++) {
        expr->integer = (expr->integer * 10) + (parser->source->text[i] - '0');
      }
    }

    return expr;
  case LGT_DOT:
    /* A lone dot is the wildcard: any one character. */
    expr = l_genesis_expr_new(parser, LGE_ANY, span);
    parser->position++;

    return expr;
  case LGT_BANG:
    /* !x: any one character but those x matches; x is an atom, and a repetition after it repeats the negation. */
    expr = l_genesis_expr_new(parser, LGE_NOT, span);
    parser->position++;
    expr->child = l_genesis_parse_atom(parser);

    if (expr->child == NULL) {
      return NULL;
    }

    expr->span = l_genesis_span_join(span, expr->child->span);

    return expr;
  case LGT_MINUS:
    if (l_genesis_peek(parser, 1)->kind == LGT_INT) {
      parser->position++;
      expr = l_genesis_parse_atom(parser);

      if (expr != NULL) {
        expr->integer = -expr->integer;
        expr->span    = l_genesis_span_join(span, expr->span);
      }

      return expr;
    }

    l_genesis_unexpected(parser, "an expression");

    return NULL;
  case LGT_PARAM:
    expr       = l_genesis_expr_new(parser, LGE_PARAM, span);
    expr->name = l_genesis_token_name(parser, token);
    parser->position++;

    return expr;
  case LGT_DOLLAR:
  case LGT_HASH:
    expr        = l_genesis_expr_new(parser, token->kind == LGT_HASH ? LGE_RULE : LGE_TOKEN, span);
    expr->name  = l_genesis_token_name(parser, token);
    expr->sigil = token->kind == LGT_HASH ? '#' : '$';

    if (token->kind == LGT_DOLLAR && expr->name[0] == '_') {
      expr->kind = LGE_FRAGMENT;
    }

    parser->position++;
    next = l_genesis_peek(parser, 0);

    /* A call's parenthesis touches the name; a group after a name is separated from it. */
    if (next->kind == LGT_LPAREN && next->offset == token->offset + token->width) {
      parser->position++;
      expr->kind = LGE_CALL;
      expr->span = l_genesis_span_join(span, l_genesis_span_of(parser, next));
      l_genesis_parse_arguments(parser, expr);
      expr->span = l_genesis_span_join(span, l_genesis_previous_span(parser));
    }

    return expr;
  case LGT_IDENT:
    if (l_genesis_ident_is(parser, token, logos_languages_genesis_spec_word_text_get(LGSW_NONE))) {
      parser->position++;

      return l_genesis_expr_new(parser, LGE_NONE, span);
    }

    parser->position++;
    next = l_genesis_peek(parser, 0);

    if (next->kind == LGT_DOT) {
      const LGLexed *member;

      member = l_genesis_peek(parser, 1);

      if (member->kind == LGT_HASH) {
        expr            = l_genesis_expr_new(parser, LGE_RULE, l_genesis_span_join(span, l_genesis_span_of(parser, member)));
        expr->qualifier = l_genesis_token_name(parser, token);
        expr->name      = l_genesis_token_name(parser, member);
        expr->sigil     = '#';
        parser->position += 2;

        return expr;
      }

      if (member->kind == LGT_IDENT) {
        expr            = l_genesis_expr_new(parser, LGE_ENUM, l_genesis_span_join(span, l_genesis_span_of(parser, member)));
        expr->qualifier = l_genesis_token_name(parser, token);
        expr->name      = l_genesis_token_name(parser, member);
        parser->position += 2;

        return expr;
      }
    }

    expr       = l_genesis_expr_new(parser, LGE_IDENT, span);
    expr->name = l_genesis_token_name(parser, token);

    /* A function-like value, such as inner_bytes(4, 3). */
    if (next->kind == LGT_LPAREN && next->offset == token->offset + token->width) {
      parser->position++;
      expr->kind = LGE_CALL;
      l_genesis_parse_arguments(parser, expr);
    }

    return expr;
  case LGT_LPAREN:
    parser->position++;

    /* "()" is the empty expression: it matches nothing and always succeeds. */
    if (l_genesis_peek(parser, 0)->kind == LGT_RPAREN) {
      parser->position++;

      return l_genesis_expr_new(parser, LGE_EMPTY, l_genesis_span_join(span, l_genesis_previous_span(parser)));
    }

    parser->nesting++;
    item = l_genesis_parse_union(parser);
    parser->nesting--;

    if (item == NULL) {
      return NULL;
    }

    expr        = l_genesis_expr_new(parser, LGE_GROUP, span);
    expr->child = item;

    if (l_genesis_expect(parser, LGT_RPAREN)) {
      expr->span = l_genesis_span_join(span, l_genesis_previous_span(parser));
    }

    /* Group annotations: @choice(...) and @exclude(...). */
    for (;;) {
      const LGLexed *annotation;
      LGAnnotation  *created;
      NexusDataList *arguments;
      PharosSpan     annotation_span;

      annotation = l_genesis_peek(parser, 0);

      if (annotation->kind != LGT_AT) {
        break;
      }

      if (!(l_genesis_annotation_is(parser, annotation, LGSW_CHOICE) || l_genesis_annotation_is(parser, annotation, LGSW_EXCLUDE))) {
        break;
      }

      annotation_span = l_genesis_span_of(parser, annotation);
      parser->position++;
      created = (LGAnnotation *)nexus_data_arena_allocate(parser->arena, NEXUS_SIZEOF(LGAnnotation) * (expr->annotation_count + 1));

      if (expr->annotation_count > 0) {
        nexus_memory_bytes_copy(expr->annotations, created, NEXUS_SIZEOF(LGAnnotation) * expr->annotation_count);
      }

      created[expr->annotation_count].name = l_genesis_token_name(parser, annotation);
      created[expr->annotation_count].span = annotation_span;
      arguments                            = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 2);

      if (l_genesis_expect(parser, LGT_LPAREN)) {
        while (!parser->panic && l_genesis_peek(parser, 0)->kind != LGT_RPAREN && l_genesis_peek(parser, 0)->kind != LGT_EOF) {
          item = l_genesis_parse_atom(parser);

          if (item == NULL) {
            break;
          }

          (void)nexus_data_list_append_pointer(arguments, item);

          if (!l_genesis_accept(parser, LGT_COMMA)) {
            break;
          }
        }

        (void)l_genesis_expect(parser, LGT_RPAREN);
      }

      created[expr->annotation_count].arguments = (LGExpr **)l_genesis_array(parser, arguments, &created[expr->annotation_count].argument_count);
      nexus_data_list_destroy(arguments);
      expr->annotations = created;
      expr->annotation_count++;
    }

    return expr;
  case LGT_AT:
    if (l_genesis_annotation_is(parser, token, LGSW_IF)) {
      parser->position++;

      return l_genesis_parse_if(parser, span);
    }

    l_genesis_unexpected(parser, "an expression");

    return NULL;
  default:
    l_genesis_unexpected(parser, "an expression");

    return NULL;
  }
}

/* Summary: the value of an integer token. */
static uint32 l_genesis_int_value(const LGParser *parser, const LGLexed *token) {
  uint64 value;
  uint32 i;

  value = 0;

  for (i = 0; i < token->width && value < 0xFFFFFFFFULL; i++) {
    value = (value * 10) + (uint64)(parser->source->text[token->offset + i] - '0');
  }

  return value > 0xFFFFFFFEULL ? 0xFFFFFFFEU : (uint32)value;
}

/*
Summary: reads a bounded repetition `{n}`, `{n,}` or `{n,m}` written right after an operand, if one starts at the position.
Returns: the repeat expression around operand, or NULL when what follows is not one (nothing is consumed then).
*/
static LGExpr *l_genesis_parse_bounds(LGParser *parser, LGExpr *operand) {
  const LGLexed *open;
  const LGLexed *low;
  const LGLexed *next;
  const LGLexed *high;
  const LGLexed *close;
  LGExpr        *repeat;
  uint32         ahead;
  uint32         minimum;
  uint32         maximum;

  open = l_genesis_peek(parser, 0);
  low  = l_genesis_peek(parser, 1);
  next = l_genesis_peek(parser, 2);

  /* The brace touches the operand: `x{2}`, never `x {`, which a block of a later declaration could begin with. */
  if (open->kind != LGT_LBRACE || low->kind != LGT_INT || parser->position == 0 ||
      parser->tokens[parser->position - 1].offset + parser->tokens[parser->position - 1].width != open->offset) {
    return NULL;
  }

  minimum = l_genesis_int_value(parser, low);
  maximum = minimum;
  ahead   = 2;
  high    = NULL;

  if (next->kind == LGT_COMMA) {
    high = l_genesis_peek(parser, 3);

    if (high->kind == LGT_INT) {
      maximum = l_genesis_int_value(parser, high);
      ahead   = 4;
    } else {
      maximum = 0xFFFFFFFFU;
      ahead   = 3;
    }
  } else if (next->kind != LGT_RBRACE) {
    return NULL;
  }

  close = l_genesis_peek(parser, ahead);

  if (close->kind != LGT_RBRACE) {
    return NULL;
  }

  repeat            = l_genesis_expr_new(parser, LGE_REPEAT, l_genesis_span_join(operand->span, l_genesis_span_of(parser, close)));
  repeat->child     = operand;
  repeat->repeat_op = '{';
  repeat->lo        = minimum;
  repeat->hi        = maximum;
  parser->position += ahead + 1;

  return repeat;
}

static LGExpr *l_genesis_parse_postfix(LGParser *parser) {
  LGExpr        *atom;
  LGExpr        *repeat;
  const LGLexed *token;

  atom = l_genesis_parse_atom(parser);

  if (atom == NULL) {
    return NULL;
  }

  for (;;) {
    repeat = l_genesis_parse_bounds(parser, atom);

    if (repeat != NULL) {
      atom = repeat;
      continue;
    }

    token = l_genesis_peek(parser, 0);

    if (token->kind != LGT_QUESTION && token->kind != LGT_STAR && token->kind != LGT_PLUS) {
      break;
    }

    repeat            = l_genesis_expr_new(parser, LGE_REPEAT, l_genesis_span_join(atom->span, l_genesis_span_of(parser, token)));
    repeat->child     = atom;
    repeat->repeat_op = '+';

    if (token->kind == LGT_QUESTION) {
      repeat->repeat_op = '?';
    } else if (token->kind == LGT_STAR) {
      repeat->repeat_op = '*';
    }

    parser->position++;
    atom = repeat;
  }

  return atom;
}

static LGExpr *l_genesis_parse_concat(LGParser *parser) {
  NexusDataList *items;
  LGExpr        *first;
  LGExpr        *item;
  LGExpr        *concat;

  first = l_genesis_parse_postfix(parser);

  if (first == NULL) {
    return NULL;
  }

  if (!l_genesis_atom_start(parser)) {
    return first;
  }

  items = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 4);
  (void)nexus_data_list_append_pointer(items, first);

  while (l_genesis_atom_start(parser)) {
    item = l_genesis_parse_postfix(parser);

    if (item == NULL) {
      break;
    }

    (void)nexus_data_list_append_pointer(items, item);
  }

  concat        = l_genesis_expr_new(parser, LGE_CONCAT, first->span);
  concat->items = (LGExpr **)l_genesis_array(parser, items, &concat->item_count);
  concat->span  = l_genesis_span_join(first->span, concat->items[concat->item_count - 1]->span);
  nexus_data_list_destroy(items);

  return concat;
}

static LGExpr *l_genesis_parse_union(LGParser *parser) {
  NexusDataList *items;
  LGExpr        *first;
  LGExpr        *item;
  LGExpr        *result;

  first = l_genesis_parse_concat(parser);

  if (first == NULL || l_genesis_peek(parser, 0)->kind != LGT_UNION) {
    return first;
  }

  items = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 4);
  (void)nexus_data_list_append_pointer(items, first);

  while (l_genesis_accept(parser, LGT_UNION)) {
    item = l_genesis_parse_concat(parser);

    if (item == NULL) {
      break;
    }

    (void)nexus_data_list_append_pointer(items, item);
  }

  result        = l_genesis_expr_new(parser, LGE_UNION, first->span);
  result->items = (LGExpr **)l_genesis_array(parser, items, &result->item_count);
  result->span  = l_genesis_span_join(first->span, result->items[result->item_count - 1]->span);
  nexus_data_list_destroy(items);

  return result;
}

/* ---------------------------------------------------------------------------- */
/* DECLARATIONS                                                                  */
/* ---------------------------------------------------------------------------- */

/* Summary: skips to the next token that can begin a declaration, to recover after an error. */
static void l_genesis_recover(LGParser *parser) {
  const LGLexed *token;
  int32          depth;

  parser->panic = FALSE;
  depth         = 0;

  while (l_genesis_peek(parser, 0)->kind != LGT_EOF) {
    token = l_genesis_peek(parser, 0);

    if (token->kind == LGT_LBRACE) {
      depth++;
    } else if (token->kind == LGT_RBRACE) {
      depth--;

      if (depth <= 0) {
        parser->position++;

        return;
      }
    } else if (depth <= 0 && token->line_break_before > 0 &&
               (l_genesis_definition_start(parser) || l_genesis_ident_is(parser, token, logos_languages_genesis_spec_word_text_get(LGSW_IMPORT)) ||
                l_genesis_ident_is(parser, token, logos_languages_genesis_spec_word_text_get(LGSW_ENUM)) ||
                l_genesis_ident_is(parser, token, logos_languages_genesis_spec_word_text_get(LGSW_PARSER)) ||
                l_genesis_ident_is(parser, token, logos_languages_genesis_spec_word_text_get(LGSW_CONTEXT)) ||
                l_genesis_ident_is(parser, token, logos_languages_genesis_spec_word_text_get(LGSW_PRATT)) ||
                l_genesis_ident_is(parser, token, logos_languages_genesis_spec_word_text_get(LGSW_EMBED)) ||
                l_genesis_ident_is(parser, token, logos_languages_genesis_spec_word_text_get(LGSW_TEMPLATE)))) {
      return;
    }

    parser->position++;
  }
}

static void l_genesis_parse_annotations(LGParser *parser, LGDecl *decl) {
  const LGLexed *token;
  LGAnnotation  *created;
  NexusDataList *arguments;
  LGExpr        *argument;
  uint32         index;

  for (;;) {
    token = l_genesis_peek(parser, 0);

    if (token->kind != LGT_AT) {
      return;
    }

    index   = decl->annotation_count;
    created = (LGAnnotation *)nexus_data_arena_allocate(parser->arena, NEXUS_SIZEOF(LGAnnotation) * (index + 1));

    if (index > 0) {
      nexus_memory_bytes_copy(decl->annotations, created, NEXUS_SIZEOF(LGAnnotation) * index);
    }

    created[index].name = l_genesis_token_name(parser, token);
    created[index].span = l_genesis_span_of(parser, token);
    parser->position++;
    arguments = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 2);

    if (l_genesis_expect(parser, LGT_LPAREN)) {
      while (!parser->panic && l_genesis_peek(parser, 0)->kind != LGT_RPAREN && l_genesis_peek(parser, 0)->kind != LGT_EOF) {
        argument = l_genesis_parse_atom(parser);

        if (argument == NULL) {
          break;
        }

        (void)nexus_data_list_append_pointer(arguments, argument);

        if (!l_genesis_accept(parser, LGT_COMMA)) {
          break;
        }
      }

      (void)l_genesis_expect(parser, LGT_RPAREN);
    }

    created[index].arguments = (LGExpr **)l_genesis_array(parser, arguments, &created[index].argument_count);
    nexus_data_list_destroy(arguments);
    decl->annotations = created;
    decl->annotation_count++;
  }
}

static const char *l_genesis_text_of(const LGParser *parser, const LGLexed *token) {
  return nexus_data_arena_text_copy(parser->arena, (const char *)parser->source->text + token->offset, token->width);
}

static LGDecl *l_genesis_parse_import(LGParser *parser) {
  const LGLexed *token;
  LGDecl        *decl;
  NexusDataList *items;
  NexusDataList *spans;
  PharosSpan     span;
  PharosSpan     item_span;

  token = l_genesis_peek(parser, 0);
  span  = l_genesis_span_of(parser, token);
  decl  = l_genesis_decl_new(parser, LGD_IMPORT, span);
  parser->position++;
  token = l_genesis_peek(parser, 0);

  if (token->kind != LGT_IDENT) {
    l_genesis_unexpected(parser, "the name of the source to import");

    return NULL;
  }

  decl->name = l_genesis_token_name(parser, token);
  decl->span = l_genesis_span_join(span, l_genesis_span_of(parser, token));
  parser->position++;

  if (l_genesis_ident_is(parser, l_genesis_peek(parser, 0), "AS")) {
    parser->position++;
    token = l_genesis_peek(parser, 0);

    if (token->kind != LGT_IDENT) {
      l_genesis_unexpected(parser, "an alias");

      return NULL;
    }

    decl->alias = l_genesis_token_name(parser, token);
    parser->position++;
  }

  if (!l_genesis_expect(parser, LGT_LBRACE)) {
    return NULL;
  }

  items = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 8);
  spans = nexus_data_list_create(NEXUS_SIZEOF(PharosSpan), 8);

  while (!parser->panic && l_genesis_peek(parser, 0)->kind != LGT_RBRACE && l_genesis_peek(parser, 0)->kind != LGT_EOF) {
    token = l_genesis_peek(parser, 0);

    if (token->kind != LGT_DOLLAR && token->kind != LGT_HASH && token->kind != LGT_IDENT) {
      l_genesis_unexpected(parser, "an item to import: a `$` token, a `#` rule, a template or an enumeration");
      break;
    }

    item_span = l_genesis_span_of(parser, token);
    (void)nexus_data_list_append_pointer(items, l_genesis_text_of(parser, token));
    (void)nexus_data_list_append(spans, &item_span);
    parser->position++;

    if (!l_genesis_accept(parser, LGT_COMMA)) {
      break;
    }
  }

  (void)l_genesis_expect(parser, LGT_RBRACE);
  decl->names      = (const char **)l_genesis_array(parser, items, &decl->name_count);
  decl->name_spans = (PharosSpan *)nexus_data_arena_allocate(parser->arena, (decl->name_count > 0 ? decl->name_count : 1) * NEXUS_SIZEOF(PharosSpan));

  if (decl->name_count > 0) {
    nexus_memory_bytes_copy(nexus_data_list_data_get_const(spans), decl->name_spans, (uint_large)decl->name_count * NEXUS_SIZEOF(PharosSpan));
  }

  nexus_data_list_destroy(items);
  nexus_data_list_destroy(spans);

  return decl;
}

static LGDecl *l_genesis_parse_enum(LGParser *parser) {
  const LGLexed *token;
  LGDecl        *decl;
  NexusDataList *items;
  NexusDataList *spans;
  PharosSpan     item_span;

  token = l_genesis_peek(parser, 0);
  decl  = l_genesis_decl_new(parser, LGD_ENUM, l_genesis_span_of(parser, token));
  parser->position++;
  token = l_genesis_peek(parser, 0);

  if (token->kind != LGT_IDENT) {
    l_genesis_unexpected(parser, "the name of the enumeration");

    return NULL;
  }

  decl->name = l_genesis_token_name(parser, token);
  decl->span = l_genesis_span_join(decl->span, l_genesis_span_of(parser, token));
  parser->position++;

  if (!l_genesis_expect(parser, LGT_LBRACE)) {
    return NULL;
  }

  items = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 4);
  spans = nexus_data_list_create(NEXUS_SIZEOF(PharosSpan), 4);

  while (!parser->panic && l_genesis_peek(parser, 0)->kind == LGT_IDENT) {
    token     = l_genesis_peek(parser, 0);
    item_span = l_genesis_span_of(parser, token);
    (void)nexus_data_list_append_pointer(items, l_genesis_token_name(parser, token));
    (void)nexus_data_list_append(spans, &item_span);
    parser->position++;

    if (!l_genesis_accept(parser, LGT_COMMA)) {
      break;
    }
  }

  (void)l_genesis_expect(parser, LGT_RBRACE);
  decl->names      = (const char **)l_genesis_array(parser, items, &decl->name_count);
  decl->name_spans = (PharosSpan *)nexus_data_arena_allocate(parser->arena, (decl->name_count > 0 ? decl->name_count : 1) * NEXUS_SIZEOF(PharosSpan));

  if (decl->name_count > 0) {
    nexus_memory_bytes_copy(nexus_data_list_data_get_const(spans), decl->name_spans, (uint_large)decl->name_count * NEXUS_SIZEOF(PharosSpan));
  }

  nexus_data_list_destroy(items);
  nexus_data_list_destroy(spans);

  return decl;
}

/* Summary: parses `key = value` properties until the closing brace; used by PARSER and EMBED blocks. */
static void l_genesis_parse_properties(LGParser *parser, LGDecl *decl) {
  NexusDataList *keys;
  NexusDataList *values;
  NexusDataList *spans;
  const LGLexed *token;
  LGExpr        *value;
  PharosSpan     key_span;

  keys   = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 4);
  values = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 4);
  spans  = nexus_data_list_create(NEXUS_SIZEOF(PharosSpan), 4);

  while (!parser->panic && l_genesis_peek(parser, 0)->kind == LGT_IDENT) {
    token    = l_genesis_peek(parser, 0);
    key_span = l_genesis_span_of(parser, token);
    parser->position++;

    if (!l_genesis_expect(parser, LGT_EQ)) {
      break;
    }

    if (l_genesis_accept(parser, LGT_LBRACKET)) {
      /* A list value: [a, b, c]. */
      NexusDataList *elements;
      LGExpr        *list;
      LGExpr        *element;

      elements = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 4);
      list     = l_genesis_expr_new(parser, LGE_CONCAT, key_span);

      while (!parser->panic && l_genesis_peek(parser, 0)->kind != LGT_RBRACKET && l_genesis_peek(parser, 0)->kind != LGT_EOF) {
        element = l_genesis_parse_postfix(parser);

        if (element == NULL) {
          break;
        }

        (void)nexus_data_list_append_pointer(elements, element);

        if (!l_genesis_accept(parser, LGT_COMMA)) {
          break;
        }
      }

      (void)l_genesis_expect(parser, LGT_RBRACKET);
      list->items = (LGExpr **)l_genesis_array(parser, elements, &list->item_count);
      nexus_data_list_destroy(elements);
      value = list;
    } else {
      value = l_genesis_parse_postfix(parser);
    }

    if (value == NULL) {
      break;
    }

    (void)nexus_data_list_append_pointer(keys, l_genesis_token_name(parser, token));
    (void)nexus_data_list_append_pointer(values, value);
    (void)nexus_data_list_append(spans, &key_span);
  }

  decl->names      = (const char **)l_genesis_array(parser, keys, &decl->name_count);
  decl->values     = (LGExpr **)l_genesis_array(parser, values, &decl->name_count);
  decl->name_spans = (PharosSpan *)nexus_data_arena_allocate(parser->arena, (decl->name_count > 0 ? decl->name_count : 1) * NEXUS_SIZEOF(PharosSpan));

  if (decl->name_count > 0) {
    nexus_memory_bytes_copy(nexus_data_list_data_get_const(spans), decl->name_spans, (uint_large)decl->name_count * NEXUS_SIZEOF(PharosSpan));
  }

  nexus_data_list_destroy(keys);
  nexus_data_list_destroy(values);
  nexus_data_list_destroy(spans);
}

static LGDecl *l_genesis_parse_block_decl(LGParser *parser, LGDeclKind kind, boolean properties, boolean in_pratt) {
  const LGLexed *token;
  LGDecl        *decl;
  LGDecl        *child;
  NexusDataList *children;
  PharosSpan     span;

  token = l_genesis_peek(parser, 0);
  span  = l_genesis_span_of(parser, token);
  decl  = l_genesis_decl_new(parser, kind, span);
  parser->position++;

  if (!l_genesis_expect(parser, LGT_LANGLE)) {
    return NULL;
  }

  token = l_genesis_peek(parser, 0);

  if (kind == LGD_CONTEXT) {
    if (token->kind != LGT_IDENT) {
      l_genesis_unexpected(parser, "a context name");

      return NULL;
    }
  } else if (token->kind != LGT_HASH && token->kind != LGT_PARAM) {
    l_genesis_unexpected(parser, "a `#` rule name or a `$$` parameter");

    return NULL;
  }

  decl->name = l_genesis_token_name(parser, token);
  decl->span = l_genesis_span_join(span, l_genesis_span_of(parser, token));

  if (token->kind == LGT_PARAM) {
    decl->name_param       = l_genesis_expr_new(parser, LGE_PARAM, l_genesis_span_of(parser, token));
    decl->name_param->name = decl->name;
  }

  decl->sigil = token->kind == LGT_HASH ? '#' : 0;
  parser->position++;

  if (!l_genesis_expect(parser, LGT_RANGLE) || !l_genesis_expect(parser, LGT_LBRACE)) {
    return NULL;
  }

  if (properties) {
    l_genesis_parse_properties(parser, decl);
    (void)l_genesis_expect(parser, LGT_RBRACE);

    return decl;
  }

  children = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 8);

  while (!parser->panic && l_genesis_peek(parser, 0)->kind != LGT_RBRACE && l_genesis_peek(parser, 0)->kind != LGT_EOF) {
    child = l_genesis_parse_decl(parser, in_pratt);

    if (child == NULL) {
      l_genesis_recover(parser);
      break;
    }

    (void)nexus_data_list_append_pointer(children, child);
  }

  (void)l_genesis_expect(parser, LGT_RBRACE);
  decl->children = (LGDecl **)l_genesis_array(parser, children, &decl->child_count);
  nexus_data_list_destroy(children);

  return decl;
}

/* Summary: the PARSER block has no name: `PARSER { entry = #document }`. */
static LGDecl *l_genesis_parse_parser_block(LGParser *parser) {
  const LGLexed *token;
  LGDecl        *decl;

  token = l_genesis_peek(parser, 0);
  decl  = l_genesis_decl_new(parser, LGD_PARSER, l_genesis_span_of(parser, token));
  parser->position++;

  if (!l_genesis_expect(parser, LGT_LBRACE)) {
    return NULL;
  }

  l_genesis_parse_properties(parser, decl);
  (void)l_genesis_expect(parser, LGT_RBRACE);

  return decl;
}

static void l_genesis_parse_action(LGParser *parser, LGDecl *decl) {
  const LGLexed *token;
  const LGLexed *argument;

  token             = l_genesis_peek(parser, 0);
  decl->action_span = l_genesis_span_of(parser, token);
  parser->position++;
  token = l_genesis_peek(parser, 0);

  if (token->kind != LGT_IDENT) {
    l_genesis_unexpected(parser, "a context action: push, pop, set or replace");

    return;
  }

  decl->action_span = l_genesis_span_join(decl->action_span, l_genesis_span_of(parser, token));

  if (l_genesis_ident_is(parser, token, logos_languages_genesis_spec_word_text_get(LGSW_PUSH))) {
    decl->action = LGA_PUSH;
  } else if (l_genesis_ident_is(parser, token, logos_languages_genesis_spec_word_text_get(LGSW_POP))) {
    decl->action = LGA_POP;
  } else if (l_genesis_ident_is(parser, token, logos_languages_genesis_spec_word_text_get(LGSW_SET))) {
    decl->action = LGA_SET;
  } else if (l_genesis_ident_is(parser, token, logos_languages_genesis_spec_word_text_get(LGSW_REPLACE))) {
    decl->action = LGA_REPLACE;
  } else {
    PharosDiagnostic        *diagnostic;
    PharosSpan               span;
    static const char *const actions[] = {"push", "pop", "set", "replace"};
    const char              *suggestion;
    char                     text[64];
    uint_large               length;

    span   = l_genesis_span_of(parser, token);
    length = token->width < 63 ? token->width : 63;
    nexus_memory_bytes_copy(parser->source->text + token->offset, text, length);
    text[length] = '\0';
    diagnostic   = pharos_diagnostics_report((parser->compiler)->reporting, PDS_ERROR, "G0003", span, "unknown context action `%s`", text);
    pharos_diagnostics_label_add(diagnostic, span, TRUE, "not an action");
    suggestion = nexus_strings_string_closest(text, actions, 4);

    if (suggestion != NULL) {
      pharos_diagnostics_help_add(diagnostic, "did you mean `%s`?", suggestion);
    } else {
      pharos_diagnostics_note_add(diagnostic, "the context actions are push(CONTEXT), pop(N), set(CONTEXT) and replace(CONTEXT)");
    }

    parser->ok    = FALSE;
    parser->panic = TRUE;

    return;
  }

  parser->position++;

  if (!l_genesis_expect(parser, LGT_LPAREN)) {
    return;
  }

  argument = l_genesis_peek(parser, 0);

  if (decl->action == LGA_POP) {
    if (argument->kind != LGT_INT) {
      l_genesis_unexpected(parser, "the number of contexts to pop");

      return;
    }

    {
      uint_large i;

      for (i = argument->offset; i < argument->offset + argument->width; i++) {
        decl->action_amount = (decl->action_amount * 10) + (parser->source->text[i] - '0');
      }
    }

    parser->position++;
  } else {
    if (argument->kind != LGT_IDENT) {
      l_genesis_unexpected(parser, "a context name");

      return;
    }

    decl->action_target = l_genesis_token_name(parser, argument);
    decl->action_span   = l_genesis_span_join(decl->action_span, l_genesis_span_of(parser, argument));
    parser->position++;
  }

  (void)l_genesis_expect(parser, LGT_RPAREN);
}

static LGDecl *l_genesis_parse_template(LGParser *parser) {
  const LGLexed *token;
  LGDecl        *decl;
  LGDecl        *child;
  NexusDataList *params;
  NexusDataList *children;
  LGParam       *param;
  NexusDataList *types;

  token = l_genesis_peek(parser, 0);
  decl  = l_genesis_decl_new(parser, LGD_TEMPLATE, l_genesis_span_of(parser, token));
  parser->position++;

  if (!l_genesis_expect(parser, LGT_LANGLE)) {
    return NULL;
  }

  token = l_genesis_peek(parser, 0);

  if (token->kind != LGT_DOLLAR && token->kind != LGT_HASH) {
    l_genesis_unexpected(parser, "the template's name, such as `$keyword` or `#sequence`");

    return NULL;
  }

  decl->name  = l_genesis_token_name(parser, token);
  decl->sigil = token->kind == LGT_HASH ? '#' : '$';
  decl->span  = l_genesis_span_join(decl->span, l_genesis_span_of(parser, token));
  parser->position++;

  if (!l_genesis_expect(parser, LGT_RANGLE) || !l_genesis_expect(parser, LGT_LPAREN)) {
    return NULL;
  }

  params = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 4);

  while (!parser->panic && l_genesis_peek(parser, 0)->kind == LGT_PARAM) {
    token       = l_genesis_peek(parser, 0);
    param       = (LGParam *)nexus_data_arena_allocate(parser->arena, NEXUS_SIZEOF(*param));
    param->name = l_genesis_token_name(parser, token);
    param->span = l_genesis_span_of(parser, token);
    parser->position++;

    if (!l_genesis_expect(parser, LGT_COLON)) {
      break;
    }

    types = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 2);

    do {
      token = l_genesis_peek(parser, 0);

      if (token->kind != LGT_IDENT) {
        l_genesis_unexpected(parser, "a parameter type");
        break;
      }

      (void)nexus_data_list_append_pointer(types, l_genesis_token_name(parser, token));
      parser->position++;
    } while (l_genesis_accept(parser, LGT_BAR));

    param->types    = (const char **)l_genesis_array(parser, types, &param->type_count);
    param->optional = l_genesis_accept(parser, LGT_QUESTION);
    nexus_data_list_destroy(types);

    if (l_genesis_accept(parser, LGT_EQ)) {
      param->default_value = l_genesis_parse_postfix(parser);
    }

    (void)nexus_data_list_append_pointer(params, param);

    if (!l_genesis_accept(parser, LGT_COMMA)) {
      break;
    }
  }

  (void)l_genesis_expect(parser, LGT_RPAREN);
  decl->params = (LGParam *)nexus_data_arena_allocate(parser->arena, (nexus_data_list_length_get(params) + 1) * NEXUS_SIZEOF(LGParam));

  {
    uint_large i;

    decl->param_count = (uint32)nexus_data_list_length_get(params);

    for (i = 0; i < nexus_data_list_length_get(params); i++) {
      decl->params[i] = *(const LGParam *)nexus_data_list_pointer_at(params, i);
    }
  }

  nexus_data_list_destroy(params);

  if (!l_genesis_expect(parser, LGT_ARROW)) {
    return NULL;
  }

  token = l_genesis_peek(parser, 0);

  if (token->kind != LGT_IDENT) {
    l_genesis_unexpected(parser, "the template's result type");

    return NULL;
  }

  decl->result_type = l_genesis_token_name(parser, token);
  parser->position++;

  if (!l_genesis_expect(parser, LGT_LBRACE)) {
    return NULL;
  }

  children = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 4);

  while (!parser->panic && l_genesis_peek(parser, 0)->kind != LGT_RBRACE && l_genesis_peek(parser, 0)->kind != LGT_EOF) {
    child = l_genesis_parse_decl(parser, TRUE);

    if (child == NULL) {
      l_genesis_recover(parser);
      break;
    }

    (void)nexus_data_list_append_pointer(children, child);
  }

  (void)l_genesis_expect(parser, LGT_RBRACE);
  decl->children = (LGDecl **)l_genesis_array(parser, children, &decl->child_count);
  nexus_data_list_destroy(children);

  return decl;
}

static LGDecl *l_genesis_parse_decl(LGParser *parser, boolean in_pratt) {
  const LGLexed *token;
  const LGLexed *next;
  LGDecl        *decl;
  LGExpr        *expr;
  PharosSpan     span;

  token = l_genesis_peek(parser, 0);
  span  = l_genesis_span_of(parser, token);

  if (token->kind == LGT_IDENT) {
    if (l_genesis_ident_is(parser, token, logos_languages_genesis_spec_word_text_get(LGSW_IMPORT))) {
      return l_genesis_parse_import(parser);
    }

    if (l_genesis_ident_is(parser, token, logos_languages_genesis_spec_word_text_get(LGSW_ENUM))) {
      return l_genesis_parse_enum(parser);
    }

    if (l_genesis_ident_is(parser, token, logos_languages_genesis_spec_word_text_get(LGSW_PARSER))) {
      return l_genesis_parse_parser_block(parser);
    }

    if (l_genesis_ident_is(parser, token, logos_languages_genesis_spec_word_text_get(LGSW_CONTEXT))) {
      return l_genesis_parse_block_decl(parser, LGD_CONTEXT, FALSE, FALSE);
    }

    if (l_genesis_ident_is(parser, token, logos_languages_genesis_spec_word_text_get(LGSW_PRATT))) {
      return l_genesis_parse_block_decl(parser, LGD_PRATT, FALSE, TRUE);
    }

    if (l_genesis_ident_is(parser, token, logos_languages_genesis_spec_word_text_get(LGSW_EMBED))) {
      return l_genesis_parse_block_decl(parser, LGD_EMBED, TRUE, FALSE);
    }

    if (l_genesis_ident_is(parser, token, logos_languages_genesis_spec_word_text_get(LGSW_TEMPLATE))) {
      return l_genesis_parse_template(parser);
    }

    if (in_pratt && l_genesis_ident_is(parser, token, logos_languages_genesis_spec_word_text_get(LGSW_ATOM)) &&
        l_genesis_peek(parser, 1)->kind == LGT_EQ) {
      decl = l_genesis_decl_new(parser, LGD_RULE, span);
      parser->position += 2;
      decl->name       = "atom";
      decl->expression = l_genesis_parse_postfix(parser);

      return decl->expression != NULL ? decl : NULL;
    }

    if (in_pratt && (l_genesis_ident_is(parser, token, logos_languages_genesis_spec_word_text_get(LGSW_PREFIX)) ||
                     l_genesis_ident_is(parser, token, logos_languages_genesis_spec_word_text_get(LGSW_INFIX)) ||
                     l_genesis_ident_is(parser, token, logos_languages_genesis_spec_word_text_get(LGSW_POSTFIX)))) {
      LGExpr *call;

      decl             = l_genesis_decl_new(parser, LGD_ENTRY, span);
      decl->entry_kind = l_genesis_token_name(parser, token);
      parser->position++;
      decl->entry_token = l_genesis_parse_postfix(parser);

      if (decl->entry_token == NULL) {
        return NULL;
      }

      call = l_genesis_expr_new(parser, LGE_CALL, span);

      if (!l_genesis_expect(parser, LGT_LPAREN)) {
        return NULL;
      }

      l_genesis_parse_arguments(parser, call);

      {
        uint32 i;

        for (i = 0; i < call->item_count; i++) {
          if (call->arg_names[i] != NULL && nexus_strings_string_equals(call->arg_names[i], "left_bp")) {
            decl->left_bp = call->items[i];
          } else if (call->arg_names[i] != NULL && nexus_strings_string_equals(call->arg_names[i], "right_bp")) {
            decl->right_bp = call->items[i];
          } else {
            PharosDiagnostic *diagnostic;
            PharosSpan        argument_span;

            argument_span = call->items[i]->span;
            diagnostic    = pharos_diagnostics_report((parser->compiler)->reporting, PDS_ERROR, "G0004", argument_span,
                                                      "an operator entry takes `left_bp` and `right_bp`");
            pharos_diagnostics_label_add(diagnostic, argument_span, TRUE, "not a binding power");
            parser->ok = FALSE;
          }
        }
      }

      return decl;
    }
  }

  if (l_genesis_annotation_is(parser, token, LGSW_IF)) {
    LGExpr        *left;
    LGExpr        *right;
    LGExpr        *condition;
    NexusDataList *branch;
    LGDecl        *child;

    decl = l_genesis_decl_new(parser, LGD_IF, span);
    parser->position++;

    if (!l_genesis_expect(parser, LGT_LPAREN)) {
      return NULL;
    }

    left      = l_genesis_parse_atom(parser);
    condition = l_genesis_expr_new(parser, LGE_COMPARE, span);

    if (left == NULL) {
      return NULL;
    }

    if (l_genesis_accept(parser, LGT_EQEQ)) {
      condition->compare_op = '=';
    } else if (l_genesis_accept(parser, LGT_NEQ)) {
      condition->compare_op = '!';
    } else {
      l_genesis_unexpected(parser, "`==` or `!=`");

      return NULL;
    }

    right = l_genesis_parse_atom(parser);

    if (right == NULL || !l_genesis_expect(parser, LGT_RPAREN) || !l_genesis_expect(parser, LGT_LBRACE)) {
      return NULL;
    }

    condition->items      = (LGExpr **)nexus_data_arena_allocate(parser->arena, 2 * NEXUS_EFFECTIVE_POINTER_SIZE_BYTES);
    condition->items[0]   = left;
    condition->items[1]   = right;
    condition->item_count = 2;
    decl->condition       = condition;
    branch                = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 4);

    while (!parser->panic && l_genesis_peek(parser, 0)->kind != LGT_RBRACE && l_genesis_peek(parser, 0)->kind != LGT_EOF) {
      child = l_genesis_parse_decl(parser, in_pratt);

      if (child == NULL) {
        break;
      }

      (void)nexus_data_list_append_pointer(branch, child);
    }

    (void)l_genesis_expect(parser, LGT_RBRACE);
    decl->children = (LGDecl **)l_genesis_array(parser, branch, &decl->child_count);
    nexus_data_list_destroy(branch);

    next = l_genesis_peek(parser, 0);

    if (l_genesis_annotation_is(parser, next, LGSW_ELSE)) {
      parser->position++;
      branch = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 4);

      if (l_genesis_expect(parser, LGT_LBRACE)) {
        while (!parser->panic && l_genesis_peek(parser, 0)->kind != LGT_RBRACE && l_genesis_peek(parser, 0)->kind != LGT_EOF) {
          child = l_genesis_parse_decl(parser, in_pratt);

          if (child == NULL) {
            break;
          }

          (void)nexus_data_list_append_pointer(branch, child);
        }

        (void)l_genesis_expect(parser, LGT_RBRACE);
      }

      decl->else_children = (LGDecl **)l_genesis_array(parser, branch, &decl->else_count);
      nexus_data_list_destroy(branch);
    }

    return decl;
  }

  if (token->kind == LGT_DOLLAR || token->kind == LGT_HASH || token->kind == LGT_PARAM) {
    next = l_genesis_peek(parser, 1);

    /* A call at declaration level: the parenthesis touches the name. */
    if (next->kind == LGT_LPAREN && next->offset == token->offset + token->width) {
      decl       = l_genesis_decl_new(parser, LGD_CALL, span);
      decl->call = l_genesis_parse_atom(parser);

      return decl->call != NULL ? decl : NULL;
    }

    decl = l_genesis_decl_new(parser, token->kind == LGT_HASH ? LGD_RULE : LGD_TOKEN, span);

    decl->name = l_genesis_token_name(parser, token);

    if (token->kind == LGT_PARAM) {
      decl->name_param       = l_genesis_expr_new(parser, LGE_PARAM, span);
      decl->name_param->name = decl->name;
    }

    decl->sigil = token->kind == LGT_HASH ? '#' : '$';

    if (token->kind == LGT_DOLLAR && decl->name[0] == '_') {
      decl->kind = LGD_FRAGMENT;
    }

    parser->decl_column = l_genesis_column(parser, token->offset);
    parser->nesting     = 0;
    parser->position++;
    l_genesis_parse_annotations(parser, decl);

    if (!l_genesis_expect(parser, LGT_EQ)) {
      return NULL;
    }

    expr = l_genesis_parse_union(parser);

    if (expr == NULL) {
      return NULL;
    }

    decl->expression = expr;
    decl->span       = l_genesis_span_join(span, expr->span);

    if (decl->kind == LGD_RULE) {
      return decl;
    }

    /* Token trailers: @priority(N) and -> action, in either order. */
    for (;;) {
      next = l_genesis_peek(parser, 0);

      if (l_genesis_annotation_is(parser, next, LGSW_PRIORITY)) {
        PharosSpan priority_span;

        priority_span = l_genesis_span_of(parser, next);
        parser->position++;

        if (!l_genesis_expect(parser, LGT_LPAREN)) {
          return NULL;
        }

        decl->priority = l_genesis_parse_atom(parser);

        if (decl->priority == NULL || !l_genesis_expect(parser, LGT_RPAREN)) {
          return NULL;
        }

        decl->priority->span = l_genesis_span_join(priority_span, decl->priority->span);
      } else if (next->kind == LGT_ARROW) {
        l_genesis_parse_action(parser, decl);

        if (parser->panic) {
          return NULL;
        }
      } else {
        break;
      }
    }

    return decl;
  }

  l_genesis_unexpected(parser, "a declaration: `IMPORT`, `ENUM`, `PARSER`, `CONTEXT`, `PRATT`, `EMBED`, `TEMPLATE`, a `$` token or a `#` rule");

  return NULL;
}

/* ---------------------------------------------------------------------------- */
/* UNITS                                                                         */
/* ---------------------------------------------------------------------------- */

boolean logos_languages_genesis_syntax_parse(LGenesisCompiler *compiler, NexusDataArena *arena, uint32 source, LGUnit **out_unit) {
  LGParser          parser;
  LGUnit           *unit;
  const LGLexed    *token;
  NexusDataList    *decls;
  LGDecl           *decl;
  PharosSpan        span;
  PharosDiagnostic *diagnostic;

  NEXUS_PROGRAM_PRECONDITION(compiler != NULL && arena != NULL && out_unit != NULL, "arguments must not be NULL");

  *out_unit = NULL;

  parser.compiler    = compiler;
  parser.arena       = arena;
  parser.source      = logos_languages_genesis_compiler_source_get(compiler, source);
  parser.source_id   = source;
  parser.position    = 0;
  parser.ok          = TRUE;
  parser.panic       = FALSE;
  parser.decl_column = 0;
  parser.nesting     = 0;
  l_genesis_lex(&parser);

  unit         = (LGUnit *)nexus_data_arena_allocate(arena, NEXUS_SIZEOF(*unit));
  unit->source = source;
  token        = l_genesis_peek(&parser, 0);
  span         = l_genesis_span_of(&parser, token);

  if (l_genesis_ident_is(&parser, token, logos_languages_genesis_spec_word_text_get(LGSW_LANGUAGE))) {
    unit->kind = LGUK_LANGUAGE;
  } else if (l_genesis_ident_is(&parser, token, logos_languages_genesis_spec_word_text_get(LGSW_LIBRARY))) {
    unit->kind = LGUK_LIBRARY;
  } else {
    diagnostic =
        pharos_diagnostics_report((compiler)->reporting, PDS_ERROR, "G0005", span, "a source must begin with `Language: Name` or `Library: Name`");
    pharos_diagnostics_label_add(diagnostic, span, TRUE, "expected a header here");
    pharos_diagnostics_help_add(diagnostic, "start the file with `Language: YourLanguage` followed by `Version: 1.0.0`");

    return FALSE;
  }

  parser.position++;

  if (!l_genesis_expect(&parser, LGT_COLON)) {
    return FALSE;
  }

  token = l_genesis_peek(&parser, 0);

  if (token->kind != LGT_IDENT) {
    l_genesis_unexpected(&parser, "the name of the language or library");

    return FALSE;
  }

  unit->name      = l_genesis_token_name(&parser, token);
  unit->name_span = l_genesis_span_of(&parser, token);
  parser.position++;

  /* The version is the rest of its line, as text: it is for debugging and clients, not for Genesis. */
  token = l_genesis_peek(&parser, 0);

  if (l_genesis_ident_is(&parser, token, logos_languages_genesis_spec_word_text_get(LGSW_VERSION)) && l_genesis_peek(&parser, 1)->kind == LGT_COLON) {
    uint_large start;
    uint_large end;
    uint_large limit;

    parser.position += 2;
    start = l_genesis_peek(&parser, 0)->offset;
    limit = parser.source->size;
    end   = start;

    while (end < limit && parser.source->text[end] != '\n' &&
           !(parser.source->text[end] == '/' && end + 1 < limit && parser.source->text[end + 1] == '/')) {
      end++;
    }

    while (end > start && (parser.source->text[end - 1] == ' ' || parser.source->text[end - 1] == '\t' || parser.source->text[end - 1] == '\r')) {
      end--;
    }

    unit->version = nexus_data_arena_text_copy(arena, (const char *)parser.source->text + start, end - start);

    while (parser.position < parser.count && l_genesis_peek(&parser, 0)->offset < end && l_genesis_peek(&parser, 0)->kind != LGT_EOF) {
      parser.position++;
    }
  }

  decls = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 16);

  while (l_genesis_peek(&parser, 0)->kind != LGT_EOF) {
    decl = l_genesis_parse_decl(&parser, FALSE);

    if (decl == NULL) {
      l_genesis_recover(&parser);
      continue;
    }

    (void)nexus_data_list_append_pointer(decls, decl);
  }

  unit->decls = (LGDecl **)l_genesis_array(&parser, decls, &unit->decl_count);
  nexus_data_list_destroy(decls);
  *out_unit = unit;

  return parser.ok;
}
