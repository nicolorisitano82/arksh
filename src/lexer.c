#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "arksh/lexer.h"

static void copy_string(char *dest, size_t dest_size, const char *src) {
  if (dest_size == 0) {
    return;
  }

  snprintf(dest, dest_size, "%s", src == NULL ? "" : src);
}

static int append_char(char *dest, size_t dest_size, size_t *length, char c, char *error, size_t error_size, const char *label) {
  if (dest == NULL || length == NULL || error == NULL || error_size == 0 || label == NULL) {
    return 1;
  }

  if (*length + 1 >= dest_size) {
    snprintf(error, error_size, "%s too long", label);
    return 1;
  }

  dest[*length] = c;
  (*length)++;
  dest[*length] = '\0';
  return 0;
}

static int push_token(ArkshTokenStream *stream, ArkshTokenKind kind, const char *text, const char *raw, size_t position) {
  if (stream->count >= ARKSH_MAX_LEXER_TOKENS) {
    return 1;
  }

  stream->tokens[stream->count].kind = kind;
  copy_string(stream->tokens[stream->count].text, sizeof(stream->tokens[stream->count].text), text);
  copy_string(stream->tokens[stream->count].raw, sizeof(stream->tokens[stream->count].raw), raw == NULL ? text : raw);
  stream->tokens[stream->count].position = position;
  stream->count++;
  return 0;
}

static int match_fd_redirection_token(const char *line, size_t index, size_t len, ArkshTokenKind *out_kind, size_t *out_length) {
  size_t cursor = index;

  if (line == NULL || out_kind == NULL || out_length == NULL || index >= len || !isdigit((unsigned char) line[index])) {
    return 1;
  }

  while (cursor < len && isdigit((unsigned char) line[cursor])) {
    cursor++;
  }
  if (cursor >= len) {
    return 1;
  }

  if (line[cursor] == '>' && cursor + 1 < len && line[cursor + 1] == '&') {
    *out_kind = ARKSH_TOKEN_REDIRECT_DUP_OUT;
    *out_length = (cursor + 2) - index;
    return 0;
  }
  if (line[cursor] == '<' && cursor + 1 < len && line[cursor + 1] == '&') {
    *out_kind = ARKSH_TOKEN_REDIRECT_DUP_IN;
    *out_length = (cursor + 2) - index;
    return 0;
  }
  if (line[cursor] == '>' && cursor + 1 < len && line[cursor + 1] == '>') {
    *out_kind = ARKSH_TOKEN_REDIRECT_FD_APPEND;
    *out_length = (cursor + 2) - index;
    return 0;
  }
  if (line[cursor] == '>') {
    *out_kind = ARKSH_TOKEN_REDIRECT_FD_OUT;
    *out_length = (cursor + 1) - index;
    return 0;
  }
  if (line[cursor] == '<') {
    *out_kind = ARKSH_TOKEN_REDIRECT_FD_IN;
    *out_length = (cursor + 1) - index;
    return 0;
  }

  return 1;
}

static int is_special_token_start(const char *line, size_t index, size_t len) {
  ArkshTokenKind fd_kind;
  size_t fd_length = 0;

  if (index >= len) {
    return 0;
  }

  if (match_fd_redirection_token(line, index, len, &fd_kind, &fd_length) == 0) {
    (void) fd_kind;
    return 1;
  }

  if (line[index] == '|' && index + 1 < len && line[index + 1] == '>') {
    return 1;
  }
  if (line[index] == '|' && index + 1 < len && line[index + 1] == '|') {
    return 1;
  }
  if (line[index] == '-' && index + 1 < len && line[index + 1] == '>') {
    return 1;
  }
  if (line[index] == '&' && index + 1 < len && line[index + 1] == '&') {
    return 1;
  }
  if (line[index] == '2' && index + 3 < len && line[index + 1] == '>' && line[index + 2] == '&' && line[index + 3] == '1') {
    return 1;
  }
  if (line[index] == '2' && index + 2 < len && line[index + 1] == '>' && line[index + 2] == '>') {
    return 1;
  }
  if (line[index] == '2' && index + 1 < len && line[index + 1] == '>') {
    return 1;
  }
  if (line[index] == '<' && index + 2 < len && line[index + 1] == '<' && line[index + 2] == '-') {
    return 1;
  }
  if (line[index] == '<' && index + 1 < len && line[index + 1] == '<') {
    return 1;
  }
  return line[index] == '|' ||
         line[index] == '&' ||
         line[index] == ';' ||
         line[index] == '<' ||
         line[index] == '>' ||
         line[index] == '(' ||
         line[index] == ')' ||
         line[index] == ',';
}

static int scan_word_token(
  const char *line,
  size_t *index,
  size_t len,
  ArkshToken *out_token,
  char *error,
  size_t error_size,
  int conditional_mode
) {
  char cooked[ARKSH_MAX_TOKEN];
  char raw[ARKSH_MAX_TOKEN];
  size_t cooked_len = 0;
  size_t raw_len = 0;
  size_t start;
  int saw_any = 0;

  if (line == NULL || index == NULL || out_token == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  cooked[0] = '\0';
  raw[0] = '\0';
  start = *index;

  while (*index < len) {
    char c = line[*index];

    if (c == '$' && *index + 1 < len && line[*index + 1] == '(') {
      int depth = 1;
      char quote = '\0';

      if (append_char(raw, sizeof(raw), &raw_len, '$', error, error_size, "raw token") != 0 ||
          append_char(raw, sizeof(raw), &raw_len, '(', error, error_size, "raw token") != 0 ||
          append_char(cooked, sizeof(cooked), &cooked_len, '$', error, error_size, "word token") != 0 ||
          append_char(cooked, sizeof(cooked), &cooked_len, '(', error, error_size, "word token") != 0) {
        return 1;
      }

      *index += 2;
      saw_any = 1;

      while (*index < len) {
        c = line[*index];

        if (quote == '\0') {
          if (c == '\'' || c == '"') {
            quote = c;
          } else if (c == '\\') {
            if (append_char(raw, sizeof(raw), &raw_len, c, error, error_size, "raw token") != 0) {
              return 1;
            }
            (*index)++;
            if (*index >= len) {
              snprintf(error, error_size, "dangling escape in command substitution");
              return 1;
            }
            c = line[*index];
          } else if (c == '$' && *index + 1 < len && line[*index + 1] == '(') {
            depth++;
          } else if (c == ')') {
            depth--;
          }
        } else if (quote == '\'') {
          if (c == '\'') {
            quote = '\0';
          }
        } else if (quote == '"') {
          if (c == '\\') {
            if (append_char(raw, sizeof(raw), &raw_len, c, error, error_size, "raw token") != 0) {
              return 1;
            }
            (*index)++;
            if (*index >= len) {
              snprintf(error, error_size, "dangling escape in command substitution");
              return 1;
            }
            c = line[*index];
          } else if (c == '"') {
            quote = '\0';
          }
        }

        if (append_char(raw, sizeof(raw), &raw_len, c, error, error_size, "raw token") != 0 ||
            append_char(cooked, sizeof(cooked), &cooked_len, c, error, error_size, "word token") != 0) {
          return 1;
        }

        (*index)++;
        if (quote == '\0' && depth == 0) {
          break;
        }
      }

      if (depth != 0) {
        snprintf(error, error_size, "unterminated command substitution");
        return 1;
      }

      continue;
    }

    if (isspace((unsigned char) c)) {
      break;
    }

    if (conditional_mode) {
      if (c == ']' && *index + 1 < len && line[*index + 1] == ']') {
        break;
      }
      if (c == '&' && *index + 1 < len && line[*index + 1] == '&') {
        break;
      }
      if (c == '|' && *index + 1 < len && line[*index + 1] == '|') {
        break;
      }
      if (c == '!' || c == '(' || c == ')') {
        break;
      }
    } else if (is_special_token_start(line, *index, len)) {
      break;
    }

    if (c == '\\') {
      if (append_char(raw, sizeof(raw), &raw_len, c, error, error_size, "raw token") != 0) {
        return 1;
      }
      (*index)++;
      if (*index >= len) {
        snprintf(error, error_size, "dangling escape at end of line");
        return 1;
      }
      c = line[*index];
      if (append_char(raw, sizeof(raw), &raw_len, c, error, error_size, "raw token") != 0 ||
          append_char(cooked, sizeof(cooked), &cooked_len, c, error, error_size, "word token") != 0) {
        return 1;
      }
      (*index)++;
      saw_any = 1;
      continue;
    }

    if (c == '\'' || c == '"') {
      char quote = c;

      if (append_char(raw, sizeof(raw), &raw_len, c, error, error_size, "raw token") != 0) {
        return 1;
      }
      (*index)++;
      saw_any = 1;

      while (*index < len && line[*index] != quote) {
        c = line[*index];
        if (quote == '"' && c == '\\') {
          if (append_char(raw, sizeof(raw), &raw_len, c, error, error_size, "raw token") != 0) {
            return 1;
          }
          (*index)++;
          if (*index >= len) {
            snprintf(error, error_size, "dangling escape inside double quotes");
            return 1;
          }
          c = line[*index];
        }

        if (append_char(raw, sizeof(raw), &raw_len, c, error, error_size, "raw token") != 0 ||
            append_char(cooked, sizeof(cooked), &cooked_len, c, error, error_size, "string token") != 0) {
          return 1;
        }
        (*index)++;
      }

      if (*index >= len || line[*index] != quote) {
        snprintf(error, error_size, "unterminated string");
        return 1;
      }

      if (append_char(raw, sizeof(raw), &raw_len, line[*index], error, error_size, "raw token") != 0) {
        return 1;
      }
      (*index)++;
      continue;
    }

    if (append_char(raw, sizeof(raw), &raw_len, c, error, error_size, "raw token") != 0 ||
        append_char(cooked, sizeof(cooked), &cooked_len, c, error, error_size, "word token") != 0) {
      return 1;
    }
    (*index)++;
    saw_any = 1;
  }

  if (!saw_any) {
    snprintf(error, error_size, "invalid token at position %zu", start);
    return 1;
  }

  memset(out_token, 0, sizeof(*out_token));
  out_token->kind = ARKSH_TOKEN_WORD;
  copy_string(out_token->text, sizeof(out_token->text), cooked);
  copy_string(out_token->raw, sizeof(out_token->raw), raw);
  out_token->position = start;
  return 0;
}

const char *arksh_token_kind_name(ArkshTokenKind kind) {
  switch (kind) {
    case ARKSH_TOKEN_EOF:
      return "eof";
    case ARKSH_TOKEN_WORD:
      return "word";
    case ARKSH_TOKEN_STRING:
      return "string";
    case ARKSH_TOKEN_ARROW:
      return "arrow";
    case ARKSH_TOKEN_OBJECT_PIPE:
      return "object-pipe";
    case ARKSH_TOKEN_SHELL_PIPE:
      return "shell-pipe";
    case ARKSH_TOKEN_REDIRECT_IN:
      return "redirect-in";
    case ARKSH_TOKEN_REDIRECT_OUT:
      return "redirect-out";
    case ARKSH_TOKEN_REDIRECT_APPEND:
      return "redirect-append";
    case ARKSH_TOKEN_HEREDOC:
      return "heredoc";
    case ARKSH_TOKEN_HEREDOC_STRIP:
      return "heredoc-strip";
    case ARKSH_TOKEN_REDIRECT_ERROR:
      return "redirect-error";
    case ARKSH_TOKEN_REDIRECT_ERROR_APPEND:
      return "redirect-error-append";
    case ARKSH_TOKEN_REDIRECT_ERROR_TO_OUTPUT:
      return "redirect-error-to-output";
    case ARKSH_TOKEN_REDIRECT_FD_IN:
      return "redirect-fd-in";
    case ARKSH_TOKEN_REDIRECT_FD_OUT:
      return "redirect-fd-out";
    case ARKSH_TOKEN_REDIRECT_FD_APPEND:
      return "redirect-fd-append";
    case ARKSH_TOKEN_REDIRECT_DUP_IN:
      return "redirect-dup-in";
    case ARKSH_TOKEN_REDIRECT_DUP_OUT:
      return "redirect-dup-out";
    case ARKSH_TOKEN_AND_IF:
      return "and-if";
    case ARKSH_TOKEN_OR_IF:
      return "or-if";
    case ARKSH_TOKEN_SEQUENCE:
      return "sequence";
    case ARKSH_TOKEN_BACKGROUND:
      return "background";
    case ARKSH_TOKEN_LPAREN:
      return "lparen";
    case ARKSH_TOKEN_RPAREN:
      return "rparen";
    case ARKSH_TOKEN_COMMA:
      return "comma";
    case ARKSH_TOKEN_INVALID:
    default:
      return "invalid";
  }
}

int arksh_lex_line(const char *line, ArkshTokenStream *out_stream, char *error, size_t error_size) {
  size_t i = 0;
  size_t len;
  int in_double_bracket = 0;
  int command_word_allowed = 1;
  int pending_redirect_target = 0;

  if (line == NULL || out_stream == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  memset(out_stream, 0, sizeof(*out_stream));
  error[0] = '\0';
  len = strlen(line);

  while (i < len) {
    char c = line[i];
    ArkshTokenKind fd_kind;
    size_t fd_length = 0;
    char fd_text[ARKSH_MAX_TOKEN];

    if (isspace((unsigned char) c)) {
      i++;
      continue;
    }

    if (in_double_bracket) {
      if (c == ']' && i + 1 < len && line[i + 1] == ']') {
        if (push_token(out_stream, ARKSH_TOKEN_WORD, "]]", "]]", i) != 0) {
          snprintf(error, error_size, "too many tokens");
          return 1;
        }
        i += 2;
        in_double_bracket = 0;
        command_word_allowed = 0;
        pending_redirect_target = 0;
        continue;
      }

      if (c == '&' && i + 1 < len && line[i + 1] == '&') {
        if (push_token(out_stream, ARKSH_TOKEN_WORD, "&&", "&&", i) != 0) {
          snprintf(error, error_size, "too many tokens");
          return 1;
        }
        i += 2;
        continue;
      }

      if (c == '|' && i + 1 < len && line[i + 1] == '|') {
        if (push_token(out_stream, ARKSH_TOKEN_WORD, "||", "||", i) != 0) {
          snprintf(error, error_size, "too many tokens");
          return 1;
        }
        i += 2;
        continue;
      }

      if (c == '!' || c == '(' || c == ')') {
        char op_text[2];
        op_text[0] = c;
        op_text[1] = '\0';
        if (push_token(out_stream, ARKSH_TOKEN_WORD, op_text, op_text, i) != 0) {
          snprintf(error, error_size, "too many tokens");
          return 1;
        }
        i++;
        continue;
      }

      if (out_stream->count >= ARKSH_MAX_LEXER_TOKENS) {
        snprintf(error, error_size, "too many tokens");
        return 1;
      }

      if (scan_word_token(line, &i, len, &out_stream->tokens[out_stream->count], error, error_size, 1) != 0) {
        return 1;
      }
      out_stream->count++;
      continue;
    }

    if (command_word_allowed &&
        pending_redirect_target == 0 &&
        c == '[' && i + 1 < len && line[i + 1] == '[' &&
        (i + 2 == len || isspace((unsigned char) line[i + 2]) || line[i + 2] == '!' || line[i + 2] == '(')) {
      if (push_token(out_stream, ARKSH_TOKEN_WORD, "[[", "[[", i) != 0) {
        snprintf(error, error_size, "too many tokens");
        return 1;
      }
      i += 2;
      in_double_bracket = 1;
      command_word_allowed = 0;
      continue;
    }

    if (match_fd_redirection_token(line, i, len, &fd_kind, &fd_length) == 0) {
      if (fd_length >= sizeof(fd_text)) {
        snprintf(error, error_size, "fd redirection token too long");
        return 1;
      }
      memcpy(fd_text, line + i, fd_length);
      fd_text[fd_length] = '\0';
      if (push_token(out_stream, fd_kind, fd_text, fd_text, i) != 0) {
        snprintf(error, error_size, "too many tokens");
        return 1;
      }
      i += fd_length;
      pending_redirect_target = 1;
      continue;
    }

    if (c == '|' && i + 1 < len && line[i + 1] == '>') {
      if (push_token(out_stream, ARKSH_TOKEN_OBJECT_PIPE, "|>", "|>", i) != 0) {
        snprintf(error, error_size, "too many tokens");
        return 1;
      }
      i += 2;
      command_word_allowed = 1;
      pending_redirect_target = 0;
      continue;
    }

    if (c == '|' && i + 1 < len && line[i + 1] == '|') {
      if (push_token(out_stream, ARKSH_TOKEN_OR_IF, "||", "||", i) != 0) {
        snprintf(error, error_size, "too many tokens");
        return 1;
      }
      i += 2;
      command_word_allowed = 1;
      pending_redirect_target = 0;
      continue;
    }

    if (c == '-' && i + 1 < len && line[i + 1] == '>') {
      if (push_token(out_stream, ARKSH_TOKEN_ARROW, "->", "->", i) != 0) {
        snprintf(error, error_size, "too many tokens");
        return 1;
      }
      i += 2;
      continue;
    }

    if (c == '|') {
      if (push_token(out_stream, ARKSH_TOKEN_SHELL_PIPE, "|", "|", i) != 0) {
        snprintf(error, error_size, "too many tokens");
        return 1;
      }
      i++;
      command_word_allowed = 1;
      pending_redirect_target = 0;
      continue;
    }

    if (c == '&' && i + 1 < len && line[i + 1] == '&') {
      if (push_token(out_stream, ARKSH_TOKEN_AND_IF, "&&", "&&", i) != 0) {
        snprintf(error, error_size, "too many tokens");
        return 1;
      }
      i += 2;
      command_word_allowed = 1;
      pending_redirect_target = 0;
      continue;
    }

    if (c == '&') {
      if (push_token(out_stream, ARKSH_TOKEN_BACKGROUND, "&", "&", i) != 0) {
        snprintf(error, error_size, "too many tokens");
        return 1;
      }
      i++;
      command_word_allowed = 1;
      pending_redirect_target = 0;
      continue;
    }

    if (c == ';') {
      if (push_token(out_stream, ARKSH_TOKEN_SEQUENCE, ";", ";", i) != 0) {
        snprintf(error, error_size, "too many tokens");
        return 1;
      }
      i++;
      command_word_allowed = 1;
      pending_redirect_target = 0;
      continue;
    }

    if (c == '2' && i + 3 < len && line[i + 1] == '>' && line[i + 2] == '&' && line[i + 3] == '1') {
      if (push_token(out_stream, ARKSH_TOKEN_REDIRECT_ERROR_TO_OUTPUT, "2>&1", "2>&1", i) != 0) {
        snprintf(error, error_size, "too many tokens");
        return 1;
      }
      i += 4;
      pending_redirect_target = 0;
      continue;
    }

    if (c == '2' && i + 2 < len && line[i + 1] == '>' && line[i + 2] == '>') {
      if (push_token(out_stream, ARKSH_TOKEN_REDIRECT_ERROR_APPEND, "2>>", "2>>", i) != 0) {
        snprintf(error, error_size, "too many tokens");
        return 1;
      }
      i += 3;
      pending_redirect_target = 1;
      continue;
    }

    if (c == '2' && i + 1 < len && line[i + 1] == '>') {
      if (push_token(out_stream, ARKSH_TOKEN_REDIRECT_ERROR, "2>", "2>", i) != 0) {
        snprintf(error, error_size, "too many tokens");
        return 1;
      }
      i += 2;
      pending_redirect_target = 1;
      continue;
    }

    if (c == '<' && i + 2 < len && line[i + 1] == '<' && line[i + 2] == '-') {
      if (push_token(out_stream, ARKSH_TOKEN_HEREDOC_STRIP, "<<-", "<<-", i) != 0) {
        snprintf(error, error_size, "too many tokens");
        return 1;
      }
      i += 3;
      pending_redirect_target = 1;
      continue;
    }

    if (c == '<' && i + 1 < len && line[i + 1] == '<') {
      if (push_token(out_stream, ARKSH_TOKEN_HEREDOC, "<<", "<<", i) != 0) {
        snprintf(error, error_size, "too many tokens");
        return 1;
      }
      i += 2;
      pending_redirect_target = 1;
      continue;
    }

    if (c == '>' && i + 1 < len && line[i + 1] == '>') {
      if (push_token(out_stream, ARKSH_TOKEN_REDIRECT_APPEND, ">>", ">>", i) != 0) {
        snprintf(error, error_size, "too many tokens");
        return 1;
      }
      i += 2;
      pending_redirect_target = 1;
      continue;
    }

    if (c == '>') {
      if (push_token(out_stream, ARKSH_TOKEN_REDIRECT_OUT, ">", ">", i) != 0) {
        snprintf(error, error_size, "too many tokens");
        return 1;
      }
      i++;
      pending_redirect_target = 1;
      continue;
    }

    if (c == '<') {
      if (push_token(out_stream, ARKSH_TOKEN_REDIRECT_IN, "<", "<", i) != 0) {
        snprintf(error, error_size, "too many tokens");
        return 1;
      }
      i++;
      pending_redirect_target = 1;
      continue;
    }

    if (c == '(') {
      if (push_token(out_stream, ARKSH_TOKEN_LPAREN, "(", "(", i) != 0) {
        snprintf(error, error_size, "too many tokens");
        return 1;
      }
      i++;
      command_word_allowed = 1;
      pending_redirect_target = 0;
      continue;
    }

    if (c == ')') {
      if (push_token(out_stream, ARKSH_TOKEN_RPAREN, ")", ")", i) != 0) {
        snprintf(error, error_size, "too many tokens");
        return 1;
      }
      i++;
      command_word_allowed = 0;
      pending_redirect_target = 0;
      continue;
    }

    if (c == ',') {
      if (push_token(out_stream, ARKSH_TOKEN_COMMA, ",", ",", i) != 0) {
        snprintf(error, error_size, "too many tokens");
        return 1;
      }
      i++;
      continue;
    }

    if (out_stream->count >= ARKSH_MAX_LEXER_TOKENS) {
      snprintf(error, error_size, "too many tokens");
      return 1;
    }

    if (scan_word_token(line, &i, len, &out_stream->tokens[out_stream->count], error, error_size, 0) != 0) {
      return 1;
    }
    if (pending_redirect_target > 0) {
      pending_redirect_target = 0;
    } else {
      command_word_allowed = 0;
    }
    out_stream->count++;
  }

  if (in_double_bracket) {
    snprintf(error, error_size, "unterminated [[ conditional");
    return 1;
  }

  if (push_token(out_stream, ARKSH_TOKEN_EOF, "", "", len) != 0) {
    snprintf(error, error_size, "too many tokens");
    return 1;
  }

  return 0;
}
