#ifndef ARKSH_LEXER_H
#define ARKSH_LEXER_H

#include <stddef.h>

#include "arksh/object.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ARKSH_MAX_LEXER_TOKENS 128

typedef enum {
  ARKSH_TOKEN_INVALID = 0,
  ARKSH_TOKEN_EOF,
  ARKSH_TOKEN_WORD,
  ARKSH_TOKEN_STRING,
  ARKSH_TOKEN_ARROW,
  ARKSH_TOKEN_OBJECT_PIPE,
  ARKSH_TOKEN_SHELL_PIPE,
  ARKSH_TOKEN_REDIRECT_IN,
  ARKSH_TOKEN_REDIRECT_OUT,
  ARKSH_TOKEN_REDIRECT_APPEND,
  ARKSH_TOKEN_HERE_STRING,
  ARKSH_TOKEN_HEREDOC,
  ARKSH_TOKEN_HEREDOC_STRIP,
  ARKSH_TOKEN_REDIRECT_ERROR,
  ARKSH_TOKEN_REDIRECT_ERROR_APPEND,
  ARKSH_TOKEN_REDIRECT_ERROR_TO_OUTPUT,
  ARKSH_TOKEN_REDIRECT_FD_IN,
  ARKSH_TOKEN_REDIRECT_FD_OUT,
  ARKSH_TOKEN_REDIRECT_FD_APPEND,
  ARKSH_TOKEN_REDIRECT_DUP_IN,
  ARKSH_TOKEN_REDIRECT_DUP_OUT,
  ARKSH_TOKEN_AND_IF,
  ARKSH_TOKEN_OR_IF,
  ARKSH_TOKEN_SEQUENCE,
  ARKSH_TOKEN_BACKGROUND,
  ARKSH_TOKEN_LPAREN,
  ARKSH_TOKEN_RPAREN,
  ARKSH_TOKEN_COMMA
} ArkshTokenKind;

typedef struct {
  ArkshTokenKind kind;
  char text[ARKSH_MAX_TOKEN];
  char raw[ARKSH_MAX_TOKEN];
  size_t position;
} ArkshToken;

typedef struct {
  ArkshToken tokens[ARKSH_MAX_LEXER_TOKENS];
  size_t count;
} ArkshTokenStream;

const char *arksh_token_kind_name(ArkshTokenKind kind);
int arksh_lex_line(const char *line, ArkshTokenStream *out_stream, char *error, size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
