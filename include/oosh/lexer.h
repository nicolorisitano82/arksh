#ifndef OOSH_LEXER_H
#define OOSH_LEXER_H

#include <stddef.h>

#include "oosh/object.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OOSH_MAX_LEXER_TOKENS 128

typedef enum {
  OOSH_TOKEN_INVALID = 0,
  OOSH_TOKEN_EOF,
  OOSH_TOKEN_WORD,
  OOSH_TOKEN_STRING,
  OOSH_TOKEN_ARROW,
  OOSH_TOKEN_OBJECT_PIPE,
  OOSH_TOKEN_SHELL_PIPE,
  OOSH_TOKEN_REDIRECT_IN,
  OOSH_TOKEN_REDIRECT_OUT,
  OOSH_TOKEN_REDIRECT_APPEND,
  OOSH_TOKEN_HEREDOC,
  OOSH_TOKEN_HEREDOC_STRIP,
  OOSH_TOKEN_REDIRECT_ERROR,
  OOSH_TOKEN_REDIRECT_ERROR_APPEND,
  OOSH_TOKEN_REDIRECT_ERROR_TO_OUTPUT,
  OOSH_TOKEN_REDIRECT_FD_IN,
  OOSH_TOKEN_REDIRECT_FD_OUT,
  OOSH_TOKEN_REDIRECT_FD_APPEND,
  OOSH_TOKEN_REDIRECT_DUP_IN,
  OOSH_TOKEN_REDIRECT_DUP_OUT,
  OOSH_TOKEN_AND_IF,
  OOSH_TOKEN_OR_IF,
  OOSH_TOKEN_SEQUENCE,
  OOSH_TOKEN_BACKGROUND,
  OOSH_TOKEN_LPAREN,
  OOSH_TOKEN_RPAREN,
  OOSH_TOKEN_COMMA
} OoshTokenKind;

typedef struct {
  OoshTokenKind kind;
  char text[OOSH_MAX_TOKEN];
  char raw[OOSH_MAX_TOKEN];
  size_t position;
} OoshToken;

typedef struct {
  OoshToken tokens[OOSH_MAX_LEXER_TOKENS];
  size_t count;
} OoshTokenStream;

const char *oosh_token_kind_name(OoshTokenKind kind);
int oosh_lex_line(const char *line, OoshTokenStream *out_stream, char *error, size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
