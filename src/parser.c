#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arksh/lexer.h"
#include "arksh/parser.h"

static void copy_string(char *dest, size_t dest_size, const char *src) {
  if (dest_size == 0) {
    return;
  }

  snprintf(dest, dest_size, "%s", src == NULL ? "" : src);
}

static int append_text(char *dest, size_t dest_size, const char *src) {
  size_t current_len;
  size_t append_len;

  if (dest == NULL || dest_size == 0 || src == NULL) {
    return 1;
  }

  current_len = strlen(dest);
  append_len = strlen(src);
  if (current_len + append_len >= dest_size) {
    return 1;
  }

  memcpy(dest + current_len, src, append_len + 1);
  return 0;
}

static void trim_copy(const char *src, char *dest, size_t dest_size) {
  size_t start = 0;
  size_t end = 0;
  size_t len;

  if (src == NULL) {
    copy_string(dest, dest_size, "");
    return;
  }

  len = strlen(src);
  while (start < len && isspace((unsigned char) src[start])) {
    start++;
  }

  end = len;
  while (end > start && isspace((unsigned char) src[end - 1])) {
    end--;
  }

  if (end <= start) {
    copy_string(dest, dest_size, "");
    return;
  }

  if (end - start >= dest_size) {
    end = start + dest_size - 1;
  }

  memcpy(dest, src + start, end - start);
  dest[end - start] = '\0';
}

static void strip_matching_quotes(char *text) {
  size_t len;

  if (text == NULL) {
    return;
  }

  len = strlen(text);
  if (len >= 2 && ((text[0] == '"' && text[len - 1] == '"') || (text[0] == '\'' && text[len - 1] == '\''))) {
    memmove(text, text + 1, len - 2);
    text[len - 2] = '\0';
  }
}

static void copy_trimmed_slice(const char *line, size_t start, size_t end, char *out, size_t out_size) {
  char buffer[ARKSH_MAX_LINE];
  size_t len;

  if (line == NULL || out == NULL || out_size == 0 || end < start) {
    return;
  }

  len = end - start;
  if (len >= sizeof(buffer)) {
    len = sizeof(buffer) - 1;
  }

  memcpy(buffer, line + start, len);
  buffer[len] = '\0';
  trim_copy(buffer, out, out_size);
}

typedef struct {
  char delimiter[ARKSH_MAX_TOKEN];
  char body[ARKSH_MAX_OUTPUT];
  int strip_tabs;
} ArkshParsedHeredoc;

typedef struct {
  ArkshParsedHeredoc items[ARKSH_MAX_REDIRECTIONS];
  size_t count;
} ArkshParsedHeredocList;

static int is_value_token(ArkshTokenKind kind);

static size_t find_first_line_end(const char *text) {
  size_t index = 0;

  if (text == NULL) {
    return 0;
  }

  while (text[index] != '\0' && text[index] != '\n' && text[index] != '\r') {
    index++;
  }
  return index;
}

static void normalize_heredoc_line(const char *src, int strip_tabs, char *out, size_t out_size) {
  size_t start = 0;

  if (out_size == 0) {
    return;
  }

  out[0] = '\0';
  if (src == NULL) {
    return;
  }

  if (strip_tabs) {
    while (src[start] == '\t') {
      start++;
    }
  }

  copy_string(out, out_size, src + start);
}

static int collect_leading_heredocs(
  const char *line,
  char *out_header,
  size_t out_header_size,
  ArkshParsedHeredocList *out_heredocs,
  char *error,
  size_t error_size
) {
  size_t header_end;
  char header_line[ARKSH_MAX_LINE];
  ArkshTokenStream stream;
  size_t index;
  const char *cursor;

  if (line == NULL || out_header == NULL || out_header_size == 0 || out_heredocs == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  out_header[0] = '\0';
  memset(out_heredocs, 0, sizeof(*out_heredocs));
  error[0] = '\0';

  header_end = find_first_line_end(line);
  copy_trimmed_slice(line, 0, header_end, header_line, sizeof(header_line));
  if (header_line[0] == '\0') {
    return 1;
  }

  if (arksh_lex_line(header_line, &stream, error, error_size) != 0) {
    return 2;
  }

  for (index = 0; stream.tokens[index].kind != ARKSH_TOKEN_EOF; ++index) {
    ArkshTokenKind kind = stream.tokens[index].kind;

    if (kind != ARKSH_TOKEN_HEREDOC && kind != ARKSH_TOKEN_HEREDOC_STRIP) {
      continue;
    }
    if (index + 1 >= stream.count || !is_value_token(stream.tokens[index + 1].kind)) {
      snprintf(error, error_size, "heredoc %s expects a delimiter", arksh_token_kind_name(kind));
      return 2;
    }
    if (out_heredocs->count >= ARKSH_MAX_REDIRECTIONS) {
      snprintf(error, error_size, "too many heredoc redirections");
      return 2;
    }

    copy_string(
      out_heredocs->items[out_heredocs->count].delimiter,
      sizeof(out_heredocs->items[out_heredocs->count].delimiter),
      stream.tokens[index + 1].text
    );
    out_heredocs->items[out_heredocs->count].strip_tabs = (kind == ARKSH_TOKEN_HEREDOC_STRIP);
    out_heredocs->count++;
    index++;
  }

  if (out_heredocs->count == 0) {
    return 1;
  }

  if (line[header_end] == '\0') {
    snprintf(error, error_size, "unterminated heredoc: missing delimiter %s", out_heredocs->items[0].delimiter);
    return 2;
  }

  cursor = line + header_end;
  if (*cursor == '\r') {
    cursor++;
  }
  if (*cursor == '\n') {
    cursor++;
  }

  for (index = 0; index < out_heredocs->count; ++index) {
    int found_delimiter = 0;

    while (*cursor != '\0') {
      const char *line_end = cursor;
      char raw_line[ARKSH_MAX_LINE];
      char normalized[ARKSH_MAX_LINE];
      size_t raw_len;
      int has_newline = 0;

      while (*line_end != '\0' && *line_end != '\n' && *line_end != '\r') {
        line_end++;
      }

      raw_len = (size_t) (line_end - cursor);
      if (raw_len >= sizeof(raw_line)) {
        raw_len = sizeof(raw_line) - 1;
      }
      memcpy(raw_line, cursor, raw_len);
      raw_line[raw_len] = '\0';

      normalize_heredoc_line(raw_line, out_heredocs->items[index].strip_tabs, normalized, sizeof(normalized));
      if (strcmp(normalized, out_heredocs->items[index].delimiter) == 0) {
        found_delimiter = 1;
        cursor = line_end;
        if (*cursor == '\r') {
          cursor++;
        }
        if (*cursor == '\n') {
          cursor++;
        }
        break;
      }

      if (append_text(out_heredocs->items[index].body, sizeof(out_heredocs->items[index].body), normalized) != 0) {
        snprintf(error, error_size, "heredoc body too large");
        return 2;
      }

      if (*line_end == '\r' || *line_end == '\n') {
        has_newline = 1;
      }
      cursor = line_end;
      if (*cursor == '\r') {
        cursor++;
      }
      if (*cursor == '\n') {
        cursor++;
      }
      if (has_newline && append_text(out_heredocs->items[index].body, sizeof(out_heredocs->items[index].body), "\n") != 0) {
        snprintf(error, error_size, "heredoc body too large");
        return 2;
      }
    }

    if (!found_delimiter) {
      snprintf(error, error_size, "unterminated heredoc: missing delimiter %s", out_heredocs->items[index].delimiter);
      return 2;
    }
  }

  copy_string(out_header, out_header_size, header_line);
  return 0;
}

static int parse_command_list_tokens(
  const char *line,
  const ArkshTokenStream *stream,
  ArkshCommandListNode *out_list,
  char *error,
  size_t error_size
);
static int is_value_token(ArkshTokenKind kind);
static int has_top_level_command_boundary(const char *line, const ArkshTokenStream *stream);
static int text_range_contains_newline(const char *text, size_t start, size_t end);
static int token_starts_command(const char *line, const ArkshTokenStream *stream, size_t index);
static int keyword_is_compound_open(const char *word);
static int keyword_is_compound_close(const char *word);
static int contains_top_level_list_operator(const char *text);

static void analyze_text_nesting_before(
  const char *text,
  size_t position,
  int *out_paren_depth,
  int *out_brace_depth,
  int *out_bracket_depth
) {
  size_t i;
  char quote = '\0';
  int paren_depth = 0;
  int brace_depth = 0;
  int bracket_depth = 0;

  if (out_paren_depth != NULL) {
    *out_paren_depth = 0;
  }
  if (out_brace_depth != NULL) {
    *out_brace_depth = 0;
  }
  if (out_bracket_depth != NULL) {
    *out_bracket_depth = 0;
  }
  if (text == NULL) {
    return;
  }

  for (i = 0; text[i] != '\0' && i < position; ++i) {
    char c = text[i];

    if (quote != '\0') {
      if (c == quote) {
        quote = '\0';
      } else if (quote == '"' && c == '\\' && text[i + 1] != '\0') {
        i++;
      }
      continue;
    }

    if (c == '"' || c == '\'') {
      quote = c;
      continue;
    }
    if (c == '\\' && text[i + 1] != '\0') {
      i++;
      continue;
    }
    if (c == '(') {
      paren_depth++;
      continue;
    }
    if (c == ')' && paren_depth > 0) {
      paren_depth--;
      continue;
    }
    if (c == '{') {
      brace_depth++;
      continue;
    }
    if (c == '}' && brace_depth > 0) {
      brace_depth--;
      continue;
    }
    if (c == '[') {
      bracket_depth++;
      continue;
    }
    if (c == ']' && bracket_depth > 0) {
      bracket_depth--;
      continue;
    }
  }

  if (out_paren_depth != NULL) {
    *out_paren_depth = paren_depth;
  }
  if (out_brace_depth != NULL) {
    *out_brace_depth = brace_depth;
  }
  if (out_bracket_depth != NULL) {
    *out_bracket_depth = bracket_depth;
  }
}

static int position_is_inside_nested_structure(const char *text, size_t position) {
  int paren_depth = 0;
  int brace_depth = 0;
  int bracket_depth = 0;

  analyze_text_nesting_before(text, position, &paren_depth, &brace_depth, &bracket_depth);
  return paren_depth > 0 || brace_depth > 0 || bracket_depth > 0;
}

static int is_value_token(ArkshTokenKind kind) {
  return kind == ARKSH_TOKEN_WORD || kind == ARKSH_TOKEN_STRING;
}

static int token_is_quoted_string(const ArkshToken *token) {
  size_t len;

  if (token == NULL) {
    return 0;
  }

  if (token->kind == ARKSH_TOKEN_STRING) {
    return 1;
  }

  len = strlen(token->raw);
  if (len < 2) {
    return 0;
  }

  return (token->raw[0] == '"' && token->raw[len - 1] == '"') ||
         (token->raw[0] == '\'' && token->raw[len - 1] == '\'');
}

static int is_numeric_text(const char *text) {
  char *endptr = NULL;

  if (text == NULL || text[0] == '\0') {
    return 0;
  }

  strtod(text, &endptr);
  return endptr != text && *endptr == '\0';
}

static int is_identifier_text(const char *text) {
  size_t i;

  if (text == NULL || text[0] == '\0') {
    return 0;
  }

  if (!(isalpha((unsigned char) text[0]) || text[0] == '_')) {
    return 0;
  }

  for (i = 1; text[i] != '\0'; ++i) {
    if (!(isalnum((unsigned char) text[i]) || text[i] == '_')) {
      return 0;
    }
  }

  return 1;
}

static int starts_with_compound_construct(const char *text) {
  char leading[ARKSH_MAX_NAME];
  size_t len = 0;

  if (text == NULL) {
    return 0;
  }
  while (*text != '\0' && isspace((unsigned char) *text)) {
    text++;
  }
  if (*text == '{' || *text == '(') {
    return 1;
  }
  while (text[len] != '\0' &&
         !isspace((unsigned char) text[len]) &&
         text[len] != ';' &&
         len + 1 < sizeof(leading)) {
    leading[len] = text[len];
    len++;
  }
  leading[len] = '\0';
  return keyword_is_compound_open(leading);
}

static int is_redirect_token(ArkshTokenKind kind) {
  return kind == ARKSH_TOKEN_REDIRECT_IN ||
         kind == ARKSH_TOKEN_REDIRECT_OUT ||
         kind == ARKSH_TOKEN_REDIRECT_APPEND ||
         kind == ARKSH_TOKEN_HEREDOC ||
         kind == ARKSH_TOKEN_HEREDOC_STRIP ||
         kind == ARKSH_TOKEN_REDIRECT_ERROR ||
         kind == ARKSH_TOKEN_REDIRECT_ERROR_APPEND ||
         kind == ARKSH_TOKEN_REDIRECT_ERROR_TO_OUTPUT ||
         kind == ARKSH_TOKEN_REDIRECT_FD_IN ||
         kind == ARKSH_TOKEN_REDIRECT_FD_OUT ||
         kind == ARKSH_TOKEN_REDIRECT_FD_APPEND ||
         kind == ARKSH_TOKEN_REDIRECT_DUP_IN ||
         kind == ARKSH_TOKEN_REDIRECT_DUP_OUT;
}

static int is_list_control_token(ArkshTokenKind kind) {
  return kind == ARKSH_TOKEN_SEQUENCE ||
         kind == ARKSH_TOKEN_AND_IF ||
         kind == ARKSH_TOKEN_OR_IF ||
         kind == ARKSH_TOKEN_BACKGROUND;
}

static int contains_top_level_list_operator(const char *text) {
  size_t i;
  char quote = '\0';
  int paren_depth = 0;
  int brace_depth = 0;
  int bracket_depth = 0;

  if (text == NULL) {
    return 0;
  }

  for (i = 0; text[i] != '\0'; ++i) {
    char c = text[i];

    if (quote != '\0') {
      if (c == quote) {
        quote = '\0';
      } else if (quote == '"' && c == '\\' && text[i + 1] != '\0') {
        i++;
      }
      continue;
    }

    if (c == '"' || c == '\'') {
      quote = c;
      continue;
    }
    if (c == '(') {
      paren_depth++;
      continue;
    }
    if (c == ')' && paren_depth > 0) {
      paren_depth--;
      continue;
    }
    if (c == '{') {
      brace_depth++;
      continue;
    }
    if (c == '}' && brace_depth > 0) {
      brace_depth--;
      continue;
    }
    if (c == '[') {
      bracket_depth++;
      continue;
    }
    if (c == ']' && bracket_depth > 0) {
      bracket_depth--;
      continue;
    }

    if (paren_depth == 0 && brace_depth == 0 && bracket_depth == 0) {
      if (c == ';' || c == '&') {
        return 1;
      }
      if (c == '|' && text[i + 1] == '|') {
        return 1;
      }
    }
  }

  return 0;
}

static int find_top_level_ternary_operator(const char *text, size_t *out_question, size_t *out_colon) {
  size_t i;
  char quote = '\0';
  int paren_depth = 0;
  int bracket_depth = 0;
  int saw_question = 0;

  if (text == NULL || out_question == NULL || out_colon == NULL) {
    return 1;
  }

  for (i = 0; text[i] != '\0'; ++i) {
    char c = text[i];

    if (quote != '\0') {
      if (c == quote) {
        quote = '\0';
      } else if (quote == '"' && c == '\\' && text[i + 1] != '\0') {
        i++;
      }
      continue;
    }

    if (c == '"' || c == '\'') {
      quote = c;
      continue;
    }
    if (c == '(') {
      paren_depth++;
      continue;
    }
    if (c == ')' && paren_depth > 0) {
      paren_depth--;
      continue;
    }
    if (c == '[') {
      bracket_depth++;
      continue;
    }
    if (c == ']' && bracket_depth > 0) {
      bracket_depth--;
      continue;
    }

    if (paren_depth == 0 && bracket_depth == 0) {
      if (c == '?' && !saw_question) {
        *out_question = i;
        saw_question = 1;
      } else if (c == ':' && saw_question) {
        *out_colon = i;
        return 0;
      }
    }
  }

  return 1;
}

/* Find the rightmost occurrence of a binary operator at paren/bracket depth 0,
 * not inside a quoted string, and not part of '->' or '--'.
 * If additive_only is non-zero, only '+' and '-' are considered.
 * Otherwise only '*' and '/' are considered.
 * Returns 0 on success (found), 1 if not found.
 * *out_pos   = index of the operator in text
 * *out_op    = operator character
 */
static int find_top_level_binary_op(
  const char *text,
  size_t *out_pos,
  char *out_op,
  int additive_only
) {
  size_t i;
  size_t len;
  char quote = '\0';
  int paren_depth = 0;
  int bracket_depth = 0;
  int found = 0;

  if (text == NULL || out_pos == NULL || out_op == NULL) {
    return 1;
  }

  len = strlen(text);

  /* Scan right-to-left so we find the rightmost low-precedence operator first,
     giving correct left-associativity when the expression is evaluated
     recursively (right subtree gets the higher-precedence sub-expression). */
  for (i = len; i > 0; --i) {
    size_t idx = i - 1;
    char c = text[idx];

    /* Track quotes (right-to-left: just skip; full tracking is too complex
       scanning backwards, so we do a two-pass: first scan left-to-right to
       build a depth/quote map, then use it).
       For simplicity we do a left-to-right re-scan with guards instead. */
    (void) c;
    break; /* exit the backwards loop — use forward scan below */
  }

  /* Forward scan: track nesting, record the LAST operator seen at depth 0. */
  found = 0;
  for (i = 0; text[i] != '\0'; ++i) {
    char c = text[i];

    if (quote != '\0') {
      if (c == quote) {
        quote = '\0';
      } else if (quote == '"' && c == '\\' && text[i + 1] != '\0') {
        i++;
      }
      continue;
    }

    if (c == '"' || c == '\'') {
      quote = c;
      continue;
    }
    if (c == '(') { paren_depth++;   continue; }
    if (c == ')' && paren_depth   > 0) { paren_depth--;   continue; }
    if (c == '[') { bracket_depth++; continue; }
    if (c == ']' && bracket_depth > 0) { bracket_depth--; continue; }

    if (paren_depth != 0 || bracket_depth != 0) {
      continue;
    }

    if (additive_only) {
      if (c == '+') {
        *out_pos = i;
        *out_op  = '+';
        found    = 1;
      } else if (c == '-') {
        /* Skip '->' and '--' */
        if (text[i + 1] == '>' || text[i + 1] == '-') {
          i++;
          continue;
        }
        *out_pos = i;
        *out_op  = '-';
        found    = 1;
      }
    } else {
      if (c == '*') {
        *out_pos = i;
        *out_op  = '*';
        found    = 1;
      } else if (c == '/') {
        *out_pos = i;
        *out_op  = '/';
        found    = 1;
      }
    }
  }

  return found ? 0 : 1;
}

static int parse_method_args_csv(
  const char *src,
  char argv[ARKSH_MAX_ARGS][ARKSH_MAX_TOKEN],
  char raw_argv[ARKSH_MAX_ARGS][ARKSH_MAX_TOKEN],
  int *out_argc
) {
  int argc = 0;
  int in_quote = 0;
  char quote_char = '\0';
  int paren_depth = 0;
  int bracket_depth = 0;
  char token[ARKSH_MAX_TOKEN];
  char raw_token[ARKSH_MAX_TOKEN];
  size_t token_len = 0;
  size_t raw_len = 0;
  size_t i;
  size_t len;
  int saw_any = 0;

  if (src == NULL || argv == NULL || raw_argv == NULL || out_argc == NULL) {
    return 1;
  }

  len = strlen(src);
  for (i = 0; i <= len; ++i) {
    char c = src[i];
    int is_end = (c == '\0');

    if (in_quote) {
      if (c == quote_char) {
        if (raw_len + 1 >= sizeof(raw_token)) {
          return 1;
        }
        raw_token[raw_len++] = c;
        in_quote = 0;
      } else if (!is_end && token_len + 1 < sizeof(token) && raw_len + 1 < sizeof(raw_token)) {
        raw_token[raw_len++] = c;
        token[token_len++] = c;
        saw_any = 1;
      }
      continue;
    }

    if (c == '"' || c == '\'') {
      if (raw_len + 1 >= sizeof(raw_token)) {
        return 1;
      }
      raw_token[raw_len++] = c;
      in_quote = 1;
      quote_char = c;
      saw_any = 1;
      continue;
    }

    if (!is_end) {
      if (c == '(') {
        paren_depth++;
      } else if (c == ')' && paren_depth > 0) {
        paren_depth--;
      } else if (c == '[') {
        bracket_depth++;
      } else if (c == ']' && bracket_depth > 0) {
        bracket_depth--;
      }
    }

    if (is_end || (c == ',' && paren_depth == 0 && bracket_depth == 0)) {
      if (saw_any) {
        if (argc >= ARKSH_MAX_ARGS) {
          return 1;
        }
        token[token_len] = '\0';
        raw_token[raw_len] = '\0';
        trim_copy(token, argv[argc], sizeof(argv[argc]));
        trim_copy(raw_token, raw_argv[argc], sizeof(raw_argv[argc]));
        argc++;
        token_len = 0;
        raw_len = 0;
        saw_any = 0;
      }
      continue;
    }

    if (raw_len + 1 < sizeof(raw_token)) {
      raw_token[raw_len++] = c;
    }
    if (token_len + 1 < sizeof(token)) {
      token[token_len++] = c;
    }
    saw_any = 1;
  }

  if (in_quote) {
    return 1;
  }

  *out_argc = argc;
  return 0;
}

static int parse_member_suffix(const char *selector_src, const char *suffix_src, ArkshObjectExpressionNode *out_expression, int legacy_syntax) {
  char member_and_args[ARKSH_MAX_LINE];
  char args[ARKSH_MAX_LINE];
  const char *open_args = NULL;
  const char *close_args = NULL;
  size_t member_len;

  if (selector_src == NULL || suffix_src == NULL || out_expression == NULL) {
    return 1;
  }

  memset(out_expression, 0, sizeof(*out_expression));
  trim_copy(selector_src, out_expression->raw_selector, sizeof(out_expression->raw_selector));
  trim_copy(selector_src, out_expression->selector, sizeof(out_expression->selector));
  strip_matching_quotes(out_expression->selector);
  out_expression->legacy_syntax = legacy_syntax;

  if (out_expression->selector[0] == '\0') {
    return 1;
  }

  trim_copy(suffix_src, member_and_args, sizeof(member_and_args));
  if (member_and_args[0] == '\0') {
    return 1;
  }

  open_args = strchr(member_and_args, '(');
  if (open_args == NULL) {
    copy_string(out_expression->member, sizeof(out_expression->member), member_and_args);
    out_expression->member_kind = ARKSH_MEMBER_PROPERTY;
    return out_expression->member[0] == '\0' ? 1 : 0;
  }

  close_args = strrchr(member_and_args, ')');
  if (close_args == NULL || close_args < open_args) {
    return 1;
  }

  member_len = (size_t) (open_args - member_and_args);
  if (member_len >= sizeof(out_expression->member)) {
    return 1;
  }

  memcpy(out_expression->member, member_and_args, member_len);
  out_expression->member[member_len] = '\0';
  trim_copy(out_expression->member, out_expression->member, sizeof(out_expression->member));
  if (out_expression->member[0] == '\0') {
    return 1;
  }

  if ((size_t) (close_args - open_args - 1) >= sizeof(args)) {
    return 1;
  }

  memcpy(args, open_args + 1, (size_t) (close_args - open_args - 1));
  args[close_args - open_args - 1] = '\0';
  if (parse_method_args_csv(args, out_expression->argv, out_expression->raw_argv, &out_expression->argc) != 0) {
    return 1;
  }

  out_expression->member_kind = ARKSH_MEMBER_METHOD;
  return 0;
}

static int parse_legacy_object_expression(const char *line, ArkshObjectExpressionNode *out_expression) {
  char trimmed[ARKSH_MAX_LINE];
  char selector[ARKSH_MAX_PATH];
  const char *close = NULL;
  const char *suffix;
  size_t selector_len;

  if (line == NULL || out_expression == NULL) {
    return 1;
  }

  trim_copy(line, trimmed, sizeof(trimmed));
  if (strncmp(trimmed, "obj(", 4) != 0) {
    return 1;
  }

  close = strchr(trimmed + 4, ')');
  if (close == NULL) {
    return 1;
  }

  selector_len = (size_t) (close - (trimmed + 4));
  if (selector_len >= sizeof(selector)) {
    return 1;
  }

  memcpy(selector, trimmed + 4, selector_len);
  selector[selector_len] = '\0';

  suffix = close + 1;
  if (*suffix != '.') {
    return 1;
  }

  return parse_member_suffix(selector, suffix + 1, out_expression, 1);
}

static int parse_object_expression_text(const char *line, ArkshObjectExpressionNode *out_expression, char *error, size_t error_size) {
  ArkshTokenStream stream;
  size_t arrow_index = 0;
  int found_arrow = 0;
  char selector[ARKSH_MAX_LINE];
  char suffix[ARKSH_MAX_LINE];
  size_t i;

  if (line == NULL || out_expression == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  if (parse_legacy_object_expression(line, out_expression) == 0) {
    return 0;
  }

  if (arksh_lex_line(line, &stream, error, error_size) != 0) {
    return 1;
  }

  for (i = 0; i < stream.count; ++i) {
    if (stream.tokens[i].kind == ARKSH_TOKEN_ARROW) {
      arrow_index = i;
      found_arrow = 1;
      break;
    }
  }

  if (!found_arrow) {
    return 1;
  }
  if (arrow_index == 0 || stream.tokens[arrow_index + 1].kind == ARKSH_TOKEN_EOF) {
    snprintf(error, error_size, "expected receiver and member around ->");
    return 1;
  }

  copy_trimmed_slice(line, 0, stream.tokens[arrow_index].position, selector, sizeof(selector));
  copy_trimmed_slice(
    line,
    stream.tokens[arrow_index].position + strlen(stream.tokens[arrow_index].text),
    strlen(line),
    suffix,
    sizeof(suffix)
  );
  return parse_member_suffix(selector, suffix, out_expression, 0);
}

static int find_block_separator(const char *text, size_t len, size_t *out_index) {
  size_t i;
  char quote = '\0';

  if (text == NULL || out_index == NULL) {
    return 1;
  }

  for (i = 0; i < len; ++i) {
    char c = text[i];

    if (quote != '\0') {
      if (c == quote) {
        quote = '\0';
      } else if (quote == '"' && c == '\\' && i + 1 < len) {
        i++;
      }
      continue;
    }

    if (c == '\'' || c == '"') {
      quote = c;
      continue;
    }

    if (c == '|') {
      *out_index = i;
      return 0;
    }
  }

  return 1;
}

static int parse_block_params_text(const char *text, ArkshBlock *out_block, char *error, size_t error_size) {
  size_t i = 0;
  size_t len;

  if (text == NULL || out_block == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  len = strlen(text);
  out_block->param_count = 0;

  while (i < len) {
    size_t start;
    size_t end;

    while (i < len && isspace((unsigned char) text[i])) {
      i++;
    }
    if (i >= len) {
      break;
    }
    if (text[i] != ':') {
      snprintf(error, error_size, "block parameters must use Smalltalk-style :name syntax");
      return 1;
    }
    i++;
    start = i;
    while (i < len && (isalnum((unsigned char) text[i]) || text[i] == '_')) {
      i++;
    }
    end = i;
    if (end <= start) {
      char name[ARKSH_MAX_NAME];

      name[0] = '\0';
      snprintf(error, error_size, "invalid block parameter: %s", name);
      return 1;
    }
    {
      char name[ARKSH_MAX_NAME];
      size_t copy_len = end - start;

      if (copy_len >= sizeof(name)) {
        copy_len = sizeof(name) - 1;
      }
      memcpy(name, text + start, copy_len);
      name[copy_len] = '\0';
      if (!is_identifier_text(name)) {
        snprintf(error, error_size, "invalid block parameter: %s", name);
        return 1;
      }
    }
    if (out_block->param_count >= ARKSH_MAX_BLOCK_PARAMS) {
      snprintf(error, error_size, "too many block parameters");
      return 1;
    }
    if ((end - start) >= sizeof(out_block->params[out_block->param_count])) {
      snprintf(error, error_size, "block parameter name too long");
      return 1;
    }
    memcpy(out_block->params[out_block->param_count], text + start, end - start);
    out_block->params[out_block->param_count][end - start] = '\0';
    out_block->param_count++;
  }

  return 0;
}

static int parse_block_literal_text(const char *line, ArkshBlock *out_block, char *error, size_t error_size) {
  char trimmed[ARKSH_MAX_LINE];
  size_t len;
  size_t separator_index;
  char params_text[ARKSH_MAX_LINE];
  char body_text[ARKSH_MAX_LINE];

  if (line == NULL || out_block == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  memset(out_block, 0, sizeof(*out_block));
  trim_copy(line, trimmed, sizeof(trimmed));
  len = strlen(trimmed);
  if (len < 2 || trimmed[0] != '[' || trimmed[len - 1] != ']') {
    return 1;
  }

  if (find_block_separator(trimmed + 1, len - 2, &separator_index) != 0) {
    snprintf(error, error_size, "block literal requires a '|' separator");
    return 1;
  }

  copy_string(out_block->source, sizeof(out_block->source), trimmed);

  separator_index += 1;
  copy_trimmed_slice(trimmed, 1, separator_index, params_text, sizeof(params_text));
  copy_trimmed_slice(trimmed, separator_index + 1, len - 1, body_text, sizeof(body_text));
  if (body_text[0] == '\0') {
    snprintf(error, error_size, "block literal requires a body");
    return 1;
  }

  if (parse_block_params_text(params_text, out_block, error, error_size) != 0) {
    return 1;
  }

  copy_string(out_block->body, sizeof(out_block->body), body_text);
  return 0;
}

static int parse_value_source_call_text(const char *line, ArkshValueSourceNode *out_source, char *error, size_t error_size) {
  char trimmed[ARKSH_MAX_LINE];
  const char *open = NULL;
  const char *close = NULL;
  const char *cursor;
  size_t name_len;
  char name[ARKSH_MAX_NAME];
  char args[ARKSH_MAX_LINE];

  if (line == NULL || out_source == NULL || error == NULL || error_size == 0) {
    return 2;
  }

  trim_copy(line, trimmed, sizeof(trimmed));
  if (trimmed[0] == '\0') {
    return 1;
  }

  open = strchr(trimmed, '(');
  close = strrchr(trimmed, ')');
  if (open == NULL || close == NULL || close < open) {
    return 1;
  }

  cursor = close + 1;
  while (*cursor != '\0') {
    if (!isspace((unsigned char) *cursor)) {
      return 1;
    }
    cursor++;
  }

  name_len = (size_t) (open - trimmed);
  if (name_len == 0 || name_len >= sizeof(name)) {
    return 1;
  }
  memcpy(name, trimmed, name_len);
  name[name_len] = '\0';
  trim_copy(name, name, sizeof(name));
  if (!is_identifier_text(name)) {
    return 1;
  }

  if ((size_t) (close - open - 1) >= sizeof(args)) {
    return 2;
  }
  memcpy(args, open + 1, (size_t) (close - open - 1));
  args[close - open - 1] = '\0';

  memset(out_source, 0, sizeof(*out_source));
  copy_string(out_source->text, sizeof(out_source->text), name);
  copy_string(out_source->raw_text, sizeof(out_source->raw_text), trimmed);
  if (parse_method_args_csv(args, out_source->argv, out_source->raw_argv, &out_source->argc) != 0) {
    return 2;
  }

  if (strcmp(name, "text") == 0 || strcmp(name, "string") == 0) {
    if (out_source->argc != 1) {
      snprintf(error, error_size, "%s() expects exactly one argument", name);
      return 2;
    }
    out_source->kind = ARKSH_VALUE_SOURCE_STRING_LITERAL;
    copy_string(out_source->text, sizeof(out_source->text), out_source->argv[0]);
    copy_string(out_source->raw_text, sizeof(out_source->raw_text), out_source->raw_argv[0]);
    return 0;
  }
  if (strcmp(name, "number") == 0) {
    if (out_source->argc != 1) {
      snprintf(error, error_size, "number() expects exactly one argument");
      return 2;
    }
    out_source->kind = ARKSH_VALUE_SOURCE_NUMBER_LITERAL;
    copy_string(out_source->text, sizeof(out_source->text), out_source->argv[0]);
    copy_string(out_source->raw_text, sizeof(out_source->raw_text), out_source->raw_argv[0]);
    return 0;
  }
  if (strcmp(name, "bool") == 0) {
    if (out_source->argc != 1) {
      snprintf(error, error_size, "bool() expects exactly one argument");
      return 2;
    }
    out_source->kind = ARKSH_VALUE_SOURCE_BOOLEAN_LITERAL;
    copy_string(out_source->text, sizeof(out_source->text), out_source->argv[0]);
    copy_string(out_source->raw_text, sizeof(out_source->raw_text), out_source->raw_argv[0]);
    return 0;
  }
  if (strcmp(name, "list") == 0 || strcmp(name, "array") == 0) {
    out_source->kind = ARKSH_VALUE_SOURCE_LIST_LITERAL;
    return 0;
  }
  if (strcmp(name, "capture") == 0) {
    if (out_source->argc != 1) {
      snprintf(error, error_size, "capture() expects exactly one argument");
      return 2;
    }
    out_source->kind = ARKSH_VALUE_SOURCE_CAPTURE_TEXT;
    copy_string(out_source->text, sizeof(out_source->text), out_source->argv[0]);
    copy_string(out_source->raw_text, sizeof(out_source->raw_text), out_source->raw_argv[0]);
    return 0;
  }
  if (strcmp(name, "capture_lines") == 0) {
    if (out_source->argc != 1) {
      snprintf(error, error_size, "capture_lines() expects exactly one argument");
      return 2;
    }
    out_source->kind = ARKSH_VALUE_SOURCE_CAPTURE_LINES;
    copy_string(out_source->text, sizeof(out_source->text), out_source->argv[0]);
    copy_string(out_source->raw_text, sizeof(out_source->raw_text), out_source->raw_argv[0]);
    return 0;
  }

  out_source->kind = ARKSH_VALUE_SOURCE_RESOLVER_CALL;
  return 0;
}

static int parse_non_object_value_source_tokens(const ArkshTokenStream *stream, ArkshValueSourceNode *out_source, char *error, size_t error_size) {
  if (stream == NULL || out_source == NULL || error == NULL || error_size == 0) {
    return 2;
  }

  memset(out_source, 0, sizeof(*out_source));

  if (token_is_quoted_string(&stream->tokens[0]) && stream->tokens[1].kind == ARKSH_TOKEN_EOF) {
    out_source->kind = ARKSH_VALUE_SOURCE_STRING_LITERAL;
    copy_string(out_source->text, sizeof(out_source->text), stream->tokens[0].text);
    copy_string(out_source->raw_text, sizeof(out_source->raw_text), stream->tokens[0].raw);
    return 0;
  }

  if (stream->tokens[0].kind == ARKSH_TOKEN_WORD && stream->tokens[1].kind == ARKSH_TOKEN_EOF) {
    if (strcmp(stream->tokens[0].text, "true") == 0 || strcmp(stream->tokens[0].text, "false") == 0) {
      out_source->kind = ARKSH_VALUE_SOURCE_BOOLEAN_LITERAL;
      copy_string(out_source->text, sizeof(out_source->text), stream->tokens[0].text);
      copy_string(out_source->raw_text, sizeof(out_source->raw_text), stream->tokens[0].raw);
      return 0;
    }
    if (is_numeric_text(stream->tokens[0].text)) {
      out_source->kind = ARKSH_VALUE_SOURCE_NUMBER_LITERAL;
      copy_string(out_source->text, sizeof(out_source->text), stream->tokens[0].text);
      copy_string(out_source->raw_text, sizeof(out_source->raw_text), stream->tokens[0].raw);
      return 0;
    }
  }

  return 1;
}

static int parse_value_source_text_ex(const char *line, ArkshValueSourceNode *out_source, int allow_binding_ref, char *error, size_t error_size) {
  ArkshTokenStream stream;
  int status;
  char trimmed[ARKSH_MAX_LINE];
  size_t question_index = 0;
  size_t colon_index = 0;

  if (line == NULL || out_source == NULL || error == NULL || error_size == 0) {
    return 2;
  }

  trim_copy(line, trimmed, sizeof(trimmed));
  if (trimmed[0] == '\0') {
    return 1;
  }

  memset(out_source, 0, sizeof(*out_source));

  if (parse_block_literal_text(trimmed, &out_source->block, error, error_size) == 0) {
    out_source->kind = ARKSH_VALUE_SOURCE_BLOCK_LITERAL;
    copy_string(out_source->text, sizeof(out_source->text), out_source->block.source);
    copy_string(out_source->raw_text, sizeof(out_source->raw_text), out_source->block.source);
    return 0;
  }

  /* E3-S4 T2: boolean literals must be recognized before binding lookup so that
     `true -> value` yields boolean(true) rather than a path object, and
     `while true` / `if true` continue to work correctly. */
  if (strcmp(trimmed, "true") == 0 || strcmp(trimmed, "false") == 0) {
    out_source->kind = ARKSH_VALUE_SOURCE_BOOLEAN_LITERAL;
    copy_string(out_source->text, sizeof(out_source->text), trimmed);
    copy_string(out_source->raw_text, sizeof(out_source->raw_text), trimmed);
    return 0;
  }

  if (allow_binding_ref && is_identifier_text(trimmed)) {
    out_source->kind = ARKSH_VALUE_SOURCE_BINDING;
    copy_string(out_source->binding, sizeof(out_source->binding), trimmed);
    copy_string(out_source->text, sizeof(out_source->text), trimmed);
    copy_string(out_source->raw_text, sizeof(out_source->raw_text), trimmed);
    return 0;
  }

  if (find_top_level_ternary_operator(trimmed, &question_index, &colon_index) == 0) {
    char condition[ARKSH_MAX_LINE];
    char true_branch[ARKSH_MAX_LINE];
    char false_branch[ARKSH_MAX_LINE];

    copy_trimmed_slice(trimmed, 0, question_index, condition, sizeof(condition));
    copy_trimmed_slice(trimmed, question_index + 1, colon_index, true_branch, sizeof(true_branch));
    copy_trimmed_slice(trimmed, colon_index + 1, strlen(trimmed), false_branch, sizeof(false_branch));

    if (condition[0] == '\0' || true_branch[0] == '\0' || false_branch[0] == '\0') {
      snprintf(error, error_size, "ternary expression requires condition, true branch and false branch");
      return 2;
    }

    out_source->kind = ARKSH_VALUE_SOURCE_TERNARY;
    copy_string(out_source->text, sizeof(out_source->text), trimmed);
    copy_string(out_source->raw_text, sizeof(out_source->raw_text), trimmed);
    return 0;
  }

  /* Binary operator: additive (+/-) has lower precedence, check first. */
  {
    size_t op_pos = 0;
    char op_char = '\0';

    if (find_top_level_binary_op(trimmed, &op_pos, &op_char, 1) == 0 ||
        find_top_level_binary_op(trimmed, &op_pos, &op_char, 0) == 0) {
      char left_buf[ARKSH_MAX_LINE];
      char right_buf[ARKSH_MAX_LINE];

      /* Re-run to get the correct precedence level: additive first. */
      if (find_top_level_binary_op(trimmed, &op_pos, &op_char, 1) != 0) {
        find_top_level_binary_op(trimmed, &op_pos, &op_char, 0);
      }

      copy_trimmed_slice(trimmed, 0, op_pos, left_buf, sizeof(left_buf));
      copy_trimmed_slice(trimmed, op_pos + 1, strlen(trimmed), right_buf, sizeof(right_buf));

      /* Guard: only treat as binary op if the LEFT operand looks like a
         structured value expression. Rules:
           1. Resolver call: contains '(' with no whitespace before the '('.
              Spaces inside the parens are fine (e.g. "number( 3 )").
           2. Single double-quoted string literal: starts with '"' AND ends
              with '"' (no unquoted text after the closing quote).
           3. Single single-quoted string literal: same, with '\''.
           4. Bare number literal: starts with a digit and has no spaces.
         This rejects shell commands like:
           '"./prog" tests/fixtures/glob/*.fixture' (starts with '"' but
           does NOT end with '"') and plain shell words like "source". */
      {
        int left_is_value = 0;
        if (left_buf[0] != '\0' && right_buf[0] != '\0') {
          size_t llen = strlen(left_buf);
          if (strchr(left_buf, '(') != NULL) {
            /* Resolver call: no space before the first '(' */
            const char *paren = strchr(left_buf, '(');
            int space_before = 0;
            for (const char *p = left_buf; p < paren; p++) {
              if (*p == ' ' || *p == '\t') { space_before = 1; break; }
            }
            left_is_value = !space_before;
          } else if (left_buf[0] == '"' && llen >= 2 && left_buf[llen - 1] == '"') {
            left_is_value = 1; /* single double-quoted string */
          } else if (left_buf[0] == '\'' && llen >= 2 && left_buf[llen - 1] == '\'') {
            left_is_value = 1; /* single single-quoted string */
          } else if (left_buf[0] >= '0' && left_buf[0] <= '9' &&
                     strchr(left_buf, ' ') == NULL) {
            left_is_value = 1; /* bare number literal */
          }
        }

        if (left_is_value) {
          out_source->kind = ARKSH_VALUE_SOURCE_BINARY_OP;
          out_source->binary_op = op_char;
          copy_string(out_source->binary_left,  sizeof(out_source->binary_left),  left_buf);
          copy_string(out_source->binary_right, sizeof(out_source->binary_right), right_buf);
          copy_string(out_source->text,     sizeof(out_source->text),     trimmed);
          copy_string(out_source->raw_text, sizeof(out_source->raw_text), trimmed);
          return 0;
        }
      }
    }
  }

  if (parse_object_expression_text(line, &out_source->object_expression, error, error_size) == 0) {
    out_source->kind = ARKSH_VALUE_SOURCE_OBJECT_EXPRESSION;
    return 0;
  }

  status = parse_value_source_call_text(trimmed, out_source, error, error_size);
  if (status == 0) {
    return 0;
  }
  if (status == 2) {
    return 2;
  }

  if (arksh_lex_line(line, &stream, error, error_size) != 0) {
    return 2;
  }

  status = parse_non_object_value_source_tokens(&stream, out_source, error, error_size);
  return status;
}

static int parse_value_source_text(const char *line, ArkshValueSourceNode *out_source, char *error, size_t error_size) {
  return parse_value_source_text_ex(line, out_source, 0, error, error_size);
}

static int parse_pipeline_stage_text(const char *text, ArkshPipelineStageNode *out_stage);
static int position_is_inside_block_literal(const char *text, size_t position);
static void trim_compound_segment(const char *line, size_t start, size_t end, char *out, size_t out_size);
static int parse_group_command_text(const char *line, ArkshCompoundCommandNode *out_group, char *error, size_t error_size);
static int parse_subshell_command_text(const char *line, ArkshCompoundCommandNode *out_subshell, char *error, size_t error_size);

int arksh_parse_value_line(const char *line, ArkshAst *out_ast, char *error, size_t error_size) {
  ArkshTokenStream stream;
  ArkshValueSourceNode source;
  char trimmed[ARKSH_MAX_LINE];
  int has_object_pipe = 0;
  size_t first_object_pipe_index = 0;
  size_t i;
  int value_status;

  if (line == NULL || out_ast == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  memset(out_ast, 0, sizeof(*out_ast));
  out_ast->kind = ARKSH_AST_EMPTY;
  error[0] = '\0';

  trim_copy(line, trimmed, sizeof(trimmed));
  if (trimmed[0] == '\0') {
    return 0;
  }

  value_status = parse_value_source_text_ex(trimmed, &source, 1, error, error_size);
  if (value_status == 0) {
    if (source.kind == ARKSH_VALUE_SOURCE_OBJECT_EXPRESSION) {
      out_ast->kind = ARKSH_AST_OBJECT_EXPRESSION;
      out_ast->as.object_expression = source.object_expression;
    } else {
      out_ast->kind = ARKSH_AST_VALUE_EXPRESSION;
      out_ast->as.value_expression = source;
    }
    return 0;
  }
  if (value_status == 2) {
    return 1;
  }

  if (arksh_lex_line(trimmed, &stream, error, error_size) != 0) {
    return 1;
  }

  for (i = 0; i < stream.count; ++i) {
    if (stream.tokens[i].kind == ARKSH_TOKEN_OBJECT_PIPE) {
      has_object_pipe = 1;
      first_object_pipe_index = i;
      break;
    }
  }

  if (has_object_pipe) {
    char source_text[ARKSH_MAX_LINE];
    size_t previous_position;

    out_ast->kind = ARKSH_AST_OBJECT_PIPELINE;
    copy_trimmed_slice(trimmed, 0, stream.tokens[first_object_pipe_index].position, source_text, sizeof(source_text));
    if (parse_value_source_text_ex(source_text, &out_ast->as.pipeline.source, 1, error, error_size) != 0) {
      if (error[0] == '\0') {
        snprintf(error, error_size, "invalid pipeline source");
      }
      return 1;
    }

    previous_position = stream.tokens[first_object_pipe_index].position + strlen(stream.tokens[first_object_pipe_index].text);
    for (i = first_object_pipe_index + 1; i < stream.count; ++i) {
      if (stream.tokens[i].kind == ARKSH_TOKEN_OBJECT_PIPE || stream.tokens[i].kind == ARKSH_TOKEN_EOF) {
        char stage_text[ARKSH_MAX_LINE];

        if (out_ast->as.pipeline.stage_count >= ARKSH_MAX_PIPELINE_STAGES) {
          snprintf(error, error_size, "too many pipeline stages");
          return 1;
        }

        copy_trimmed_slice(trimmed, previous_position, stream.tokens[i].position, stage_text, sizeof(stage_text));
        if (parse_pipeline_stage_text(stage_text, &out_ast->as.pipeline.stages[out_ast->as.pipeline.stage_count]) != 0) {
          snprintf(error, error_size, "invalid pipeline stage: %s", stage_text);
          return 1;
        }

        out_ast->as.pipeline.stage_count++;
        previous_position = stream.tokens[i].position + strlen(stream.tokens[i].text);
      }
    }

    if (out_ast->as.pipeline.stage_count == 0) {
      snprintf(error, error_size, "object pipeline requires at least one stage");
      return 1;
    }
    return 0;
  }

  if (error[0] == '\0') {
    snprintf(error, error_size, "expression does not produce a value");
  }
  return 1;
}

static int parse_pipeline_stage_text(const char *text, ArkshPipelineStageNode *out_stage) {
  char trimmed[ARKSH_MAX_LINE];
  const char *open = NULL;
  const char *close = NULL;
  const char *cursor;
  size_t name_len;
  size_t args_len;

  if (text == NULL || out_stage == NULL) {
    return 1;
  }

  memset(out_stage, 0, sizeof(*out_stage));
  trim_copy(text, trimmed, sizeof(trimmed));
  if (trimmed[0] == '\0') {
    return 1;
  }

  open = strchr(trimmed, '(');
  if (open == NULL) {
    copy_string(out_stage->name, sizeof(out_stage->name), trimmed);
    return out_stage->name[0] == '\0' ? 1 : 0;
  }

  close = strrchr(trimmed, ')');
  if (close == NULL || close < open) {
    return 1;
  }

  cursor = close + 1;
  while (*cursor != '\0') {
    if (!isspace((unsigned char) *cursor)) {
      return 1;
    }
    cursor++;
  }

  name_len = (size_t) (open - trimmed);
  if (name_len >= sizeof(out_stage->name)) {
    return 1;
  }

  memcpy(out_stage->name, trimmed, name_len);
  out_stage->name[name_len] = '\0';
  trim_copy(out_stage->name, out_stage->name, sizeof(out_stage->name));

  args_len = (size_t) (close - open - 1);
  if (args_len >= sizeof(out_stage->raw_args)) {
    return 1;
  }

  memcpy(out_stage->raw_args, open + 1, args_len);
  out_stage->raw_args[args_len] = '\0';
  trim_copy(out_stage->raw_args, out_stage->raw_args, sizeof(out_stage->raw_args));
  return out_stage->name[0] == '\0' ? 1 : 0;
}

static int parse_simple_command_tokens(const ArkshTokenStream *stream, ArkshSimpleCommandNode *out_command, char *error, size_t error_size) {
  size_t index;

  if (stream == NULL || out_command == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  memset(out_command, 0, sizeof(*out_command));

  for (index = 0; stream->tokens[index].kind != ARKSH_TOKEN_EOF; ++index) {
    if (!is_value_token(stream->tokens[index].kind)) {
      snprintf(error, error_size, "unexpected token in command: %s", arksh_token_kind_name(stream->tokens[index].kind));
      return 1;
    }

    if (out_command->argc >= ARKSH_MAX_ARGS) {
      snprintf(error, error_size, "too many command arguments");
      return 1;
    }

    copy_string(out_command->argv[out_command->argc], sizeof(out_command->argv[out_command->argc]), stream->tokens[index].text);
    copy_string(out_command->raw_argv[out_command->argc], sizeof(out_command->raw_argv[out_command->argc]), stream->tokens[index].raw);
    out_command->argc++;
  }

  return 0;
}

static int append_redirection(
  ArkshCommandStageNode *stage,
  ArkshRedirectionKind kind,
  int fd,
  int target_fd,
  int heredoc_strip_tabs,
  const char *target,
  const char *raw_target,
  const char *heredoc_delimiter,
  const char *heredoc_body,
  char *error,
  size_t error_size
) {
  if (stage == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  if (stage->redirection_count >= ARKSH_MAX_REDIRECTIONS) {
    snprintf(error, error_size, "too many redirections");
    return 1;
  }

  stage->redirections[stage->redirection_count].kind = kind;
  stage->redirections[stage->redirection_count].fd = fd;
  stage->redirections[stage->redirection_count].target_fd = target_fd;
  stage->redirections[stage->redirection_count].heredoc_strip_tabs = heredoc_strip_tabs;
  copy_string(stage->redirections[stage->redirection_count].target, sizeof(stage->redirections[stage->redirection_count].target), target);
  copy_string(stage->redirections[stage->redirection_count].raw_target, sizeof(stage->redirections[stage->redirection_count].raw_target), raw_target == NULL ? target : raw_target);
  copy_string(
    stage->redirections[stage->redirection_count].heredoc_delimiter,
    sizeof(stage->redirections[stage->redirection_count].heredoc_delimiter),
    heredoc_delimiter == NULL ? "" : heredoc_delimiter
  );
  copy_string(
    stage->redirections[stage->redirection_count].heredoc_body,
    sizeof(stage->redirections[stage->redirection_count].heredoc_body),
    heredoc_body == NULL ? "" : heredoc_body
  );
  stage->redirection_count++;
  return 0;
}

static int parse_fd_token_value(const char *token_text, int *out_fd) {
  char *endptr = NULL;
  long value;

  if (token_text == NULL || out_fd == NULL || !isdigit((unsigned char) token_text[0])) {
    return 1;
  }

  value = strtol(token_text, &endptr, 10);
  if (endptr == token_text || value < 0 || value > 1024) {
    return 1;
  }

  *out_fd = (int) value;
  return 0;
}

static int parse_command_stage_tokens(
  const ArkshTokenStream *stream,
  size_t start_index,
  size_t end_index,
  const ArkshParsedHeredocList *heredocs,
  size_t *io_heredoc_index,
  ArkshCommandStageNode *out_stage,
  char *error,
  size_t error_size
) {
  size_t index;

  if (stream == NULL || out_stage == NULL || error == NULL || error_size == 0 || end_index < start_index) {
    return 1;
  }

  memset(out_stage, 0, sizeof(*out_stage));

  for (index = start_index; index < end_index; ++index) {
    ArkshTokenKind kind = stream->tokens[index].kind;

    if (is_value_token(kind)) {
      if (out_stage->argc >= ARKSH_MAX_ARGS) {
        snprintf(error, error_size, "too many command arguments");
        return 1;
      }

      copy_string(out_stage->argv[out_stage->argc], sizeof(out_stage->argv[out_stage->argc]), stream->tokens[index].text);
      copy_string(out_stage->raw_argv[out_stage->argc], sizeof(out_stage->raw_argv[out_stage->argc]), stream->tokens[index].raw);
      out_stage->argc++;
      continue;
    }

    if (kind == ARKSH_TOKEN_REDIRECT_ERROR_TO_OUTPUT) {
      if (append_redirection(out_stage, ARKSH_REDIRECT_ERROR_TO_OUTPUT, 2, 1, 0, "", "", "", "", error, error_size) != 0) {
        return 1;
      }
      continue;
    }

    if (is_redirect_token(kind)) {
      ArkshRedirectionKind redirect_kind;
      int fd = -1;
      int target_fd = -1;
      int heredoc_strip_tabs = 0;

      if (kind == ARKSH_TOKEN_HEREDOC || kind == ARKSH_TOKEN_HEREDOC_STRIP) {
        size_t heredoc_index = io_heredoc_index == NULL ? 0 : *io_heredoc_index;

        if (index + 1 >= end_index || !is_value_token(stream->tokens[index + 1].kind)) {
          snprintf(error, error_size, "redirection %s expects a delimiter", arksh_token_kind_name(kind));
          return 1;
        }
        if (heredocs == NULL || heredoc_index >= heredocs->count) {
          snprintf(error, error_size, "unterminated heredoc: missing delimiter %s", stream->tokens[index + 1].text);
          return 1;
        }
        if (strcmp(heredocs->items[heredoc_index].delimiter, stream->tokens[index + 1].text) != 0) {
          snprintf(error, error_size, "heredoc delimiter mismatch: expected %s", stream->tokens[index + 1].text);
          return 1;
        }

        heredoc_strip_tabs = (kind == ARKSH_TOKEN_HEREDOC_STRIP);
        if (append_redirection(
              out_stage,
              ARKSH_REDIRECT_HEREDOC,
              0,
              -1,
              heredoc_strip_tabs,
              "",
              "",
              heredocs->items[heredoc_index].delimiter,
              heredocs->items[heredoc_index].body,
              error,
              error_size) != 0) {
          return 1;
        }
        if (io_heredoc_index != NULL) {
          *io_heredoc_index = heredoc_index + 1;
        }
        index++;
        continue;
      }

      switch (kind) {
        case ARKSH_TOKEN_REDIRECT_IN:
          redirect_kind = ARKSH_REDIRECT_INPUT;
          fd = 0;
          break;
        case ARKSH_TOKEN_REDIRECT_OUT:
          redirect_kind = ARKSH_REDIRECT_OUTPUT_TRUNCATE;
          fd = 1;
          break;
        case ARKSH_TOKEN_REDIRECT_APPEND:
          redirect_kind = ARKSH_REDIRECT_OUTPUT_APPEND;
          fd = 1;
          break;
        case ARKSH_TOKEN_REDIRECT_ERROR:
          redirect_kind = ARKSH_REDIRECT_ERROR_TRUNCATE;
          fd = 2;
          break;
        case ARKSH_TOKEN_REDIRECT_ERROR_APPEND:
          redirect_kind = ARKSH_REDIRECT_ERROR_APPEND;
          fd = 2;
          break;
        case ARKSH_TOKEN_REDIRECT_FD_IN:
          if (parse_fd_token_value(stream->tokens[index].text, &fd) != 0) {
            snprintf(error, error_size, "invalid fd redirection: %s", stream->tokens[index].text);
            return 1;
          }
          redirect_kind = ARKSH_REDIRECT_FD_INPUT;
          break;
        case ARKSH_TOKEN_REDIRECT_FD_OUT:
          if (parse_fd_token_value(stream->tokens[index].text, &fd) != 0) {
            snprintf(error, error_size, "invalid fd redirection: %s", stream->tokens[index].text);
            return 1;
          }
          redirect_kind = ARKSH_REDIRECT_FD_OUTPUT_TRUNCATE;
          break;
        case ARKSH_TOKEN_REDIRECT_FD_APPEND:
          if (parse_fd_token_value(stream->tokens[index].text, &fd) != 0) {
            snprintf(error, error_size, "invalid fd redirection: %s", stream->tokens[index].text);
            return 1;
          }
          redirect_kind = ARKSH_REDIRECT_FD_OUTPUT_APPEND;
          break;
        case ARKSH_TOKEN_REDIRECT_DUP_IN:
          if (parse_fd_token_value(stream->tokens[index].text, &fd) != 0) {
            snprintf(error, error_size, "invalid fd duplication: %s", stream->tokens[index].text);
            return 1;
          }
          if (index + 1 >= end_index || !is_value_token(stream->tokens[index + 1].kind)) {
            snprintf(error, error_size, "redirection %s expects a source fd", arksh_token_kind_name(kind));
            return 1;
          }
          if (strcmp(stream->tokens[index + 1].text, "-") == 0) {
            redirect_kind = ARKSH_REDIRECT_FD_CLOSE;
          } else {
            if (parse_fd_token_value(stream->tokens[index + 1].text, &target_fd) != 0) {
              snprintf(error, error_size, "invalid source fd: %s", stream->tokens[index + 1].text);
              return 1;
            }
            redirect_kind = ARKSH_REDIRECT_FD_DUP_INPUT;
          }
          break;
        case ARKSH_TOKEN_REDIRECT_DUP_OUT:
          if (parse_fd_token_value(stream->tokens[index].text, &fd) != 0) {
            snprintf(error, error_size, "invalid fd duplication: %s", stream->tokens[index].text);
            return 1;
          }
          if (index + 1 >= end_index || !is_value_token(stream->tokens[index + 1].kind)) {
            snprintf(error, error_size, "redirection %s expects a source fd", arksh_token_kind_name(kind));
            return 1;
          }
          if (strcmp(stream->tokens[index + 1].text, "-") == 0) {
            redirect_kind = ARKSH_REDIRECT_FD_CLOSE;
          } else {
            if (parse_fd_token_value(stream->tokens[index + 1].text, &target_fd) != 0) {
              snprintf(error, error_size, "invalid source fd: %s", stream->tokens[index + 1].text);
              return 1;
            }
            redirect_kind = ARKSH_REDIRECT_FD_DUP_OUTPUT;
          }
          break;
        default:
          snprintf(error, error_size, "unsupported redirection token: %s", arksh_token_kind_name(kind));
          return 1;
      }

      if (redirect_kind == ARKSH_REDIRECT_FD_DUP_INPUT ||
          redirect_kind == ARKSH_REDIRECT_FD_DUP_OUTPUT ||
          redirect_kind == ARKSH_REDIRECT_FD_CLOSE) {
        if (append_redirection(
              out_stage,
              redirect_kind,
              fd,
              target_fd,
              0,
              "",
              "",
              "",
              "",
              error,
              error_size) != 0) {
          return 1;
        }
        index++;
        continue;
      }

      if (index + 1 >= end_index || !is_value_token(stream->tokens[index + 1].kind)) {
        snprintf(error, error_size, "redirection %s expects a target path", arksh_token_kind_name(kind));
        return 1;
      }

      if (append_redirection(
            out_stage,
            redirect_kind,
            fd,
            -1,
            0,
            stream->tokens[index + 1].text,
            stream->tokens[index + 1].raw,
            "",
            "",
            error,
            error_size) != 0) {
        return 1;
      }
      index++;
      continue;
    }

    snprintf(error, error_size, "unexpected token in command stage: %s", arksh_token_kind_name(kind));
    return 1;
  }

  if (out_stage->argc == 0) {
    snprintf(error, error_size, "each shell pipeline stage requires a command");
    return 1;
  }

  return 0;
}

static int parse_command_pipeline_tokens(
  const ArkshTokenStream *stream,
  const ArkshParsedHeredocList *heredocs,
  ArkshCommandPipelineNode *out_pipeline,
  char *error,
  size_t error_size
) {
  size_t stage_start = 0;
  size_t index = 0;
  size_t heredoc_index = 0;

  if (stream == NULL || out_pipeline == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  memset(out_pipeline, 0, sizeof(*out_pipeline));

  while (1) {
    if (stream->tokens[index].kind == ARKSH_TOKEN_SHELL_PIPE || stream->tokens[index].kind == ARKSH_TOKEN_EOF) {
      if (out_pipeline->stage_count >= ARKSH_MAX_PIPELINE_STAGES) {
        snprintf(error, error_size, "too many shell pipeline stages");
        return 1;
      }

      if (parse_command_stage_tokens(
            stream,
            stage_start,
            index,
            heredocs,
            &heredoc_index,
            &out_pipeline->stages[out_pipeline->stage_count],
            error,
            error_size) != 0) {
        return 1;
      }

      out_pipeline->stage_count++;
      if (stream->tokens[index].kind == ARKSH_TOKEN_EOF) {
        break;
      }

      stage_start = index + 1;
    }

    index++;
  }

  return out_pipeline->stage_count == 0 ? 1 : 0;
}

static int parse_command_list_tokens(
  const char *line,
  const ArkshTokenStream *stream,
  ArkshCommandListNode *out_list,
  char *error,
  size_t error_size
) {
  size_t segment_start = 0;
  size_t index = 0;
  ArkshTokenKind last_control = ARKSH_TOKEN_INVALID;
  ArkshListCondition next_condition = ARKSH_LIST_CONDITION_ALWAYS;
  int compound_depth = 0;

  if (line == NULL || stream == NULL || out_list == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  memset(out_list, 0, sizeof(*out_list));

  while (1) {
    ArkshTokenKind kind = stream->tokens[index].kind;

    if (kind != ARKSH_TOKEN_EOF && position_is_inside_nested_structure(line, stream->tokens[index].position)) {
      index++;
      continue;
    }

    if (kind != ARKSH_TOKEN_EOF &&
        stream->tokens[index].kind == ARKSH_TOKEN_WORD &&
        token_starts_command(line, stream, index)) {
      if (compound_depth == 0 && index > 0) {
        size_t previous = index - 1;

        if (text_range_contains_newline(
              line,
              stream->tokens[previous].position + strlen(stream->tokens[previous].raw),
              stream->tokens[index].position
            )) {
          char segment_text[ARKSH_MAX_LINE];

          copy_trimmed_slice(line, segment_start, stream->tokens[index].position, segment_text, sizeof(segment_text));
          if (segment_text[0] == '\0') {
            snprintf(error, error_size, "unexpected control operator in command list");
            return 1;
          }
          if (out_list->count >= ARKSH_MAX_LIST_ENTRIES) {
            snprintf(error, error_size, "too many command list entries");
            return 1;
          }

          copy_string(out_list->entries[out_list->count].text, sizeof(out_list->entries[out_list->count].text), segment_text);
          out_list->entries[out_list->count].condition = next_condition;
          out_list->entries[out_list->count].run_in_background = 0;
          out_list->count++;
          segment_start = stream->tokens[index].position;
          last_control = ARKSH_TOKEN_SEQUENCE;
          next_condition = ARKSH_LIST_CONDITION_ALWAYS;
        }
      }

      if (keyword_is_compound_open(stream->tokens[index].text)) {
        compound_depth++;
      } else if (keyword_is_compound_close(stream->tokens[index].text) && compound_depth > 0) {
        compound_depth--;
      }
    }

    if ((compound_depth == 0 && is_list_control_token(kind)) || kind == ARKSH_TOKEN_EOF) {
      char segment_text[ARKSH_MAX_LINE];

      copy_trimmed_slice(line, segment_start, kind == ARKSH_TOKEN_EOF ? strlen(line) : stream->tokens[index].position, segment_text, sizeof(segment_text));
      if (segment_text[0] == '\0') {
        if (kind == ARKSH_TOKEN_EOF && last_control == ARKSH_TOKEN_BACKGROUND && out_list->count > 0) {
          return 0;
        }
        snprintf(error, error_size, "unexpected control operator in command list");
        return 1;
      }

      if (out_list->count >= ARKSH_MAX_LIST_ENTRIES) {
        snprintf(error, error_size, "too many command list entries");
        return 1;
      }

      copy_string(out_list->entries[out_list->count].text, sizeof(out_list->entries[out_list->count].text), segment_text);
      out_list->entries[out_list->count].condition = next_condition;
      out_list->entries[out_list->count].run_in_background = kind == ARKSH_TOKEN_BACKGROUND;
      out_list->count++;

      if (kind == ARKSH_TOKEN_EOF) {
        return 0;
      }

      segment_start = stream->tokens[index].position + strlen(stream->tokens[index].text);
      last_control = kind;

      switch (kind) {
        case ARKSH_TOKEN_AND_IF:
          next_condition = ARKSH_LIST_CONDITION_ON_SUCCESS;
          break;
        case ARKSH_TOKEN_OR_IF:
          next_condition = ARKSH_LIST_CONDITION_ON_FAILURE;
          break;
        case ARKSH_TOKEN_SEQUENCE:
        case ARKSH_TOKEN_BACKGROUND:
        default:
          next_condition = ARKSH_LIST_CONDITION_ALWAYS;
          break;
      }
    }

    index++;
  }
}

static int has_top_level_command_boundary(const char *line, const ArkshTokenStream *stream) {
  size_t index;
  int compound_depth = 0;

  if (line == NULL || stream == NULL) {
    return 0;
  }

  for (index = 0; index < stream->count; ++index) {
    const ArkshToken *token = &stream->tokens[index];

    if (token->kind == ARKSH_TOKEN_EOF) {
      break;
    }
    if (position_is_inside_nested_structure(line, token->position)) {
      continue;
    }
    if (token->kind == ARKSH_TOKEN_WORD && token_starts_command(line, stream, index)) {
      if (compound_depth == 0 && index > 0) {
        size_t previous = index - 1;

        if (text_range_contains_newline(
              line,
              stream->tokens[previous].position + strlen(stream->tokens[previous].raw),
              token->position
            )) {
          return 1;
        }
      }

      if (keyword_is_compound_open(token->text)) {
        compound_depth++;
      } else if (keyword_is_compound_close(token->text) && compound_depth > 0) {
        compound_depth--;
      }
    }
  }

  return 0;
}

static int find_matching_delimiter(const char *text, char open_char, char close_char, size_t *out_index) {
  size_t i;
  char quote = '\0';
  int depth = 0;

  if (text == NULL || out_index == NULL || text[0] != open_char) {
    return 1;
  }

  for (i = 0; text[i] != '\0'; ++i) {
    char c = text[i];

    if (quote != '\0') {
      if (c == quote) {
        quote = '\0';
      } else if (quote == '"' && c == '\\' && text[i + 1] != '\0') {
        i++;
      }
      continue;
    }

    if (c == '"' || c == '\'') {
      quote = c;
      continue;
    }
    if (c == '\\' && text[i + 1] != '\0') {
      i++;
      continue;
    }
    if (c == open_char) {
      depth++;
      continue;
    }
    if (c == close_char) {
      depth--;
      if (depth == 0) {
        *out_index = i;
        return 0;
      }
      continue;
    }
  }

  return 1;
}

static int parse_compound_redirection_tail(
  const char *tail,
  const char *closing_token,
  const ArkshParsedHeredocList *heredocs,
  size_t *io_heredoc_index,
  ArkshRedirectionNode redirections[ARKSH_MAX_REDIRECTIONS],
  size_t *out_count,
  char *error,
  size_t error_size
) {
  ArkshTokenStream stream;
  ArkshCommandStageNode stage;
  size_t index;
  char trimmed_tail[ARKSH_MAX_LINE];

  if (redirections == NULL || out_count == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  *out_count = 0;
  trim_copy(tail, trimmed_tail, sizeof(trimmed_tail));
  if (trimmed_tail[0] == '\0') {
    return 0;
  }

  if (arksh_lex_line(trimmed_tail, &stream, error, error_size) != 0) {
    return 1;
  }

  memset(&stage, 0, sizeof(stage));
  for (index = 0; stream.tokens[index].kind != ARKSH_TOKEN_EOF; ++index) {
    ArkshTokenKind kind = stream.tokens[index].kind;
    ArkshRedirectionKind redirect_kind;
    int fd = -1;
    int target_fd = -1;

    if (kind == ARKSH_TOKEN_REDIRECT_ERROR_TO_OUTPUT) {
      if (append_redirection(&stage, ARKSH_REDIRECT_ERROR_TO_OUTPUT, 2, 1, 0, "", "", "", "", error, error_size) != 0) {
        return 1;
      }
      continue;
    }

    if (!is_redirect_token(kind)) {
      snprintf(error, error_size, "unexpected token after %s", closing_token == NULL ? "compound command" : closing_token);
      return 1;
    }

    switch (kind) {
      case ARKSH_TOKEN_REDIRECT_IN:
        redirect_kind = ARKSH_REDIRECT_INPUT;
        fd = 0;
        break;
      case ARKSH_TOKEN_REDIRECT_OUT:
        redirect_kind = ARKSH_REDIRECT_OUTPUT_TRUNCATE;
        fd = 1;
        break;
      case ARKSH_TOKEN_REDIRECT_APPEND:
        redirect_kind = ARKSH_REDIRECT_OUTPUT_APPEND;
        fd = 1;
        break;
      case ARKSH_TOKEN_REDIRECT_ERROR:
        redirect_kind = ARKSH_REDIRECT_ERROR_TRUNCATE;
        fd = 2;
        break;
      case ARKSH_TOKEN_REDIRECT_ERROR_APPEND:
        redirect_kind = ARKSH_REDIRECT_ERROR_APPEND;
        fd = 2;
        break;
      case ARKSH_TOKEN_REDIRECT_FD_IN:
        if (parse_fd_token_value(stream.tokens[index].text, &fd) != 0) {
          snprintf(error, error_size, "invalid fd redirection: %s", stream.tokens[index].text);
          return 1;
        }
        redirect_kind = ARKSH_REDIRECT_FD_INPUT;
        break;
      case ARKSH_TOKEN_REDIRECT_FD_OUT:
        if (parse_fd_token_value(stream.tokens[index].text, &fd) != 0) {
          snprintf(error, error_size, "invalid fd redirection: %s", stream.tokens[index].text);
          return 1;
        }
        redirect_kind = ARKSH_REDIRECT_FD_OUTPUT_TRUNCATE;
        break;
      case ARKSH_TOKEN_REDIRECT_FD_APPEND:
        if (parse_fd_token_value(stream.tokens[index].text, &fd) != 0) {
          snprintf(error, error_size, "invalid fd redirection: %s", stream.tokens[index].text);
          return 1;
        }
        redirect_kind = ARKSH_REDIRECT_FD_OUTPUT_APPEND;
        break;
      case ARKSH_TOKEN_REDIRECT_DUP_IN:
        if (parse_fd_token_value(stream.tokens[index].text, &fd) != 0 ||
            index + 1 >= stream.count ||
            !is_value_token(stream.tokens[index + 1].kind)) {
          snprintf(error, error_size, "unexpected token after %s", closing_token == NULL ? "compound command" : closing_token);
          return 1;
        }
        if (strcmp(stream.tokens[index + 1].text, "-") == 0) {
          redirect_kind = ARKSH_REDIRECT_FD_CLOSE;
        } else {
          if (parse_fd_token_value(stream.tokens[index + 1].text, &target_fd) != 0) {
            snprintf(error, error_size, "invalid source fd: %s", stream.tokens[index + 1].text);
            return 1;
          }
          redirect_kind = ARKSH_REDIRECT_FD_DUP_INPUT;
        }
        break;
      case ARKSH_TOKEN_REDIRECT_DUP_OUT:
        if (parse_fd_token_value(stream.tokens[index].text, &fd) != 0 ||
            index + 1 >= stream.count ||
            !is_value_token(stream.tokens[index + 1].kind)) {
          snprintf(error, error_size, "unexpected token after %s", closing_token == NULL ? "compound command" : closing_token);
          return 1;
        }
        if (strcmp(stream.tokens[index + 1].text, "-") == 0) {
          redirect_kind = ARKSH_REDIRECT_FD_CLOSE;
        } else {
          if (parse_fd_token_value(stream.tokens[index + 1].text, &target_fd) != 0) {
            snprintf(error, error_size, "invalid source fd: %s", stream.tokens[index + 1].text);
            return 1;
          }
          redirect_kind = ARKSH_REDIRECT_FD_DUP_OUTPUT;
        }
        break;
      case ARKSH_TOKEN_HEREDOC:
      case ARKSH_TOKEN_HEREDOC_STRIP: {
        size_t heredoc_index = io_heredoc_index == NULL ? 0 : *io_heredoc_index;

        if (index + 1 >= stream.count || !is_value_token(stream.tokens[index + 1].kind)) {
          snprintf(error, error_size, "unexpected token after %s", closing_token == NULL ? "compound command" : closing_token);
          return 1;
        }
        if (heredocs == NULL || heredoc_index >= heredocs->count) {
          snprintf(error, error_size, "unterminated heredoc: missing delimiter %s", stream.tokens[index + 1].text);
          return 1;
        }
        if (strcmp(heredocs->items[heredoc_index].delimiter, stream.tokens[index + 1].text) != 0) {
          snprintf(error, error_size, "heredoc delimiter mismatch: expected %s", stream.tokens[index + 1].text);
          return 1;
        }
        if (append_redirection(
              &stage,
              ARKSH_REDIRECT_HEREDOC,
              0,
              -1,
              kind == ARKSH_TOKEN_HEREDOC_STRIP,
              "",
              "",
              heredocs->items[heredoc_index].delimiter,
              heredocs->items[heredoc_index].body,
              error,
              error_size) != 0) {
          return 1;
        }
        if (io_heredoc_index != NULL) {
          *io_heredoc_index = heredoc_index + 1;
        }
        index++;
        continue;
      }
      default:
        snprintf(error, error_size, "unexpected token after %s", closing_token == NULL ? "compound command" : closing_token);
        return 1;
    }

    if (redirect_kind == ARKSH_REDIRECT_FD_DUP_INPUT ||
        redirect_kind == ARKSH_REDIRECT_FD_DUP_OUTPUT ||
        redirect_kind == ARKSH_REDIRECT_FD_CLOSE) {
      if (append_redirection(&stage, redirect_kind, fd, target_fd, 0, "", "", "", "", error, error_size) != 0) {
        return 1;
      }
      index++;
      continue;
    }

    if (index + 1 >= stream.count || !is_value_token(stream.tokens[index + 1].kind)) {
      snprintf(error, error_size, "unexpected token after %s", closing_token == NULL ? "compound command" : closing_token);
      return 1;
    }

    if (append_redirection(
          &stage,
          redirect_kind,
          fd,
          -1,
          0,
          stream.tokens[index + 1].text,
          stream.tokens[index + 1].raw,
          "",
          "",
          error,
          error_size) != 0) {
      return 1;
    }
    index++;
  }

  memcpy(redirections, stage.redirections, stage.redirection_count * sizeof(redirections[0]));
  *out_count = stage.redirection_count;
  return 0;
}

static int parse_group_command_text(const char *line, ArkshCompoundCommandNode *out_group, char *error, size_t error_size) {
  size_t close_index;
  char tail[ARKSH_MAX_LINE];
  ArkshParsedHeredocList heredocs;
  size_t heredoc_index = 0;

  if (line == NULL || out_group == NULL || error == NULL || error_size == 0) {
    return 1;
  }
  if (line[0] != '{') {
    return 1;
  }

  memset(out_group, 0, sizeof(*out_group));
  if (find_matching_delimiter(line, '{', '}', &close_index) != 0) {
    snprintf(error, error_size, "unterminated group command: missing }");
    return 1;
  }

  trim_compound_segment(line, 1, close_index, out_group->body, sizeof(out_group->body));
  if (out_group->body[0] == '\0') {
    snprintf(error, error_size, "group command requires a body");
    return 1;
  }

  copy_trimmed_slice(line, close_index + 1, strlen(line), tail, sizeof(tail));
  {
    char tail_header[ARKSH_MAX_LINE];
    int heredoc_status = collect_leading_heredocs(tail, tail_header, sizeof(tail_header), &heredocs, error, error_size);

    if (heredoc_status == 0) {
      copy_string(tail, sizeof(tail), tail_header);
    } else if (heredoc_status == 2) {
      return 1;
    } else {
      memset(&heredocs, 0, sizeof(heredocs));
    }
  }
  if (parse_compound_redirection_tail(tail, "}", &heredocs, &heredoc_index, out_group->redirections, &out_group->redirection_count, error, error_size) != 0) {
    return 1;
  }

  return 0;
}

static int parse_subshell_command_text(const char *line, ArkshCompoundCommandNode *out_subshell, char *error, size_t error_size) {
  size_t close_index;
  char tail[ARKSH_MAX_LINE];
  ArkshParsedHeredocList heredocs;
  size_t heredoc_index = 0;

  if (line == NULL || out_subshell == NULL || error == NULL || error_size == 0) {
    return 1;
  }
  if (line[0] != '(') {
    return 1;
  }

  memset(out_subshell, 0, sizeof(*out_subshell));
  if (find_matching_delimiter(line, '(', ')', &close_index) != 0) {
    snprintf(error, error_size, "unterminated subshell command: missing ')'");
    return 1;
  }

  trim_compound_segment(line, 1, close_index, out_subshell->body, sizeof(out_subshell->body));
  if (out_subshell->body[0] == '\0') {
    snprintf(error, error_size, "subshell command requires a body");
    return 1;
  }

  copy_trimmed_slice(line, close_index + 1, strlen(line), tail, sizeof(tail));
  {
    char tail_header[ARKSH_MAX_LINE];
    int heredoc_status = collect_leading_heredocs(tail, tail_header, sizeof(tail_header), &heredocs, error, error_size);

    if (heredoc_status == 0) {
      copy_string(tail, sizeof(tail), tail_header);
    } else if (heredoc_status == 2) {
      return 1;
    } else {
      memset(&heredocs, 0, sizeof(heredocs));
    }
  }
  if (parse_compound_redirection_tail(tail, ")", &heredocs, &heredoc_index, out_subshell->redirections, &out_subshell->redirection_count, error, error_size) != 0) {
    return 1;
  }

  return 0;
}

static void trim_compound_segment(const char *line, size_t start, size_t end, char *out, size_t out_size) {
  trim_copy("", out, out_size);
  copy_trimmed_slice(line, start, end, out, out_size);

  while (out[0] != '\0') {
    size_t len = strlen(out);

    if (len == 0 || out[len - 1] != ';') {
      break;
    }
    out[len - 1] = '\0';
    trim_copy(out, out, out_size);
  }
}

static int token_is_word_value(const ArkshTokenStream *stream, size_t index, const char *value) {
  return stream != NULL &&
         value != NULL &&
         stream->tokens[index].kind == ARKSH_TOKEN_WORD &&
         strcmp(stream->tokens[index].text, value) == 0;
}

static int keyword_is_compound_open(const char *word) {
  return word != NULL &&
         (strcmp(word, "if") == 0 ||
          strcmp(word, "while") == 0 ||
          strcmp(word, "until") == 0 ||
          strcmp(word, "for") == 0 ||
          strcmp(word, "case") == 0 ||
          strcmp(word, "switch") == 0 ||
          strcmp(word, "function") == 0 ||
          strcmp(word, "class") == 0);
}

static int keyword_is_compound_close(const char *word) {
  return word != NULL &&
         (strcmp(word, "fi") == 0 ||
          strcmp(word, "done") == 0 ||
          strcmp(word, "esac") == 0 ||
          strcmp(word, "endswitch") == 0 ||
          strcmp(word, "endfunction") == 0 ||
          strcmp(word, "endclass") == 0);
}

static int text_range_contains_newline(const char *text, size_t start, size_t end) {
  size_t i;

  if (text == NULL || end <= start) {
    return 0;
  }

  for (i = start; i < end; ++i) {
    if (text[i] == '\n' || text[i] == '\r') {
      return 1;
    }
  }

  return 0;
}

static int find_top_level_case_in_keyword(const char *text, size_t start, size_t *out_position) {
  size_t i;
  char quote = '\0';
  int paren_depth = 0;
  int bracket_depth = 0;

  if (text == NULL || out_position == NULL) {
    return 1;
  }

  for (i = start; text[i] != '\0'; ++i) {
    char c = text[i];

    if (quote != '\0') {
      if (c == quote) {
        quote = '\0';
      } else if (quote == '"' && c == '\\' && text[i + 1] != '\0') {
        i++;
      }
      continue;
    }

    if (c == '"' || c == '\'') {
      quote = c;
      continue;
    }
    if (c == '\\' && text[i + 1] != '\0') {
      i++;
      continue;
    }
    if (c == '(') {
      paren_depth++;
      continue;
    }
    if (c == ')' && paren_depth > 0) {
      paren_depth--;
      continue;
    }
    if (c == '[') {
      bracket_depth++;
      continue;
    }
    if (c == ']' && bracket_depth > 0) {
      bracket_depth--;
      continue;
    }

    if (paren_depth == 0 && bracket_depth == 0 &&
        c == 'i' && text[i + 1] == 'n' &&
        (i == 0 || isspace((unsigned char) text[i - 1])) &&
        (text[i + 2] == '\0' || isspace((unsigned char) text[i + 2]))) {
      *out_position = i;
      return 0;
    }
  }

  return 1;
}

static int position_is_inside_block_literal(const char *text, size_t position) {
  int bracket_depth = 0;
  analyze_text_nesting_before(text, position, NULL, NULL, &bracket_depth);
  return bracket_depth > 0;
}

static int token_starts_command(const char *line, const ArkshTokenStream *stream, size_t index) {
  size_t previous;

  if (stream == NULL || index >= stream->count) {
    return 0;
  }

  if (index == 0) {
    return 1;
  }

  previous = index - 1;
  if (line != NULL &&
      text_range_contains_newline(
        line,
        stream->tokens[previous].position + strlen(stream->tokens[previous].raw),
        stream->tokens[index].position
      )) {
    return 1;
  }

  switch (stream->tokens[previous].kind) {
    case ARKSH_TOKEN_SEQUENCE:
    case ARKSH_TOKEN_AND_IF:
    case ARKSH_TOKEN_OR_IF:
    case ARKSH_TOKEN_BACKGROUND:
    case ARKSH_TOKEN_SHELL_PIPE:
      return 1;
    case ARKSH_TOKEN_WORD:
      return strcmp(stream->tokens[previous].text, "then") == 0 ||
             strcmp(stream->tokens[previous].text, "do") == 0 ||
             strcmp(stream->tokens[previous].text, "else") == 0;
    default:
      return 0;
  }
}

static int parse_if_command_tokens(const char *line, const ArkshTokenStream *stream, ArkshIfCommandNode *out_if, char *error, size_t error_size) {
  size_t then_index = stream->count;
  size_t else_index = stream->count;
  size_t fi_index = stream->count;
  size_t i;
  int nested_depth = 0;

  if (line == NULL || stream == NULL || out_if == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  if (!token_is_word_value(stream, 0, "if")) {
    return 1;
  }

  memset(out_if, 0, sizeof(*out_if));
  for (i = 1; i < stream->count; ++i) {
    const ArkshToken *token = &stream->tokens[i];

    if (!token_starts_command(line, stream, i) || token->kind != ARKSH_TOKEN_WORD) {
      continue;
    }

    if (nested_depth == 0 && strcmp(token->text, "then") == 0) {
      then_index = i;
      break;
    }

    if (keyword_is_compound_open(token->text)) {
      nested_depth++;
    } else if (keyword_is_compound_close(token->text) && nested_depth > 0) {
      nested_depth--;
    }
  }

  if (then_index >= stream->count) {
    snprintf(error, error_size, "unterminated if command: missing then");
    return 1;
  }

  nested_depth = 0;
  for (i = then_index + 1; i < stream->count; ++i) {
    const ArkshToken *token = &stream->tokens[i];

    if (!token_starts_command(line, stream, i) || token->kind != ARKSH_TOKEN_WORD) {
      continue;
    }

    if (nested_depth == 0 && strcmp(token->text, "else") == 0) {
      else_index = i;
      continue;
    }
    if (nested_depth == 0 && strcmp(token->text, "fi") == 0) {
      fi_index = i;
      break;
    }

    if (keyword_is_compound_open(token->text)) {
      nested_depth++;
    } else if (keyword_is_compound_close(token->text) && nested_depth > 0) {
      nested_depth--;
    }
  }

  if (fi_index >= stream->count) {
    snprintf(error, error_size, "unterminated if command: missing fi");
    return 1;
  }

  trim_compound_segment(
    line,
    stream->tokens[0].position + strlen(stream->tokens[0].raw),
    stream->tokens[then_index].position,
    out_if->condition,
    sizeof(out_if->condition)
  );
  trim_compound_segment(
    line,
    stream->tokens[then_index].position + strlen(stream->tokens[then_index].raw),
    (else_index < fi_index ? stream->tokens[else_index].position : stream->tokens[fi_index].position),
    out_if->then_branch,
    sizeof(out_if->then_branch)
  );
  if (else_index < fi_index) {
    trim_compound_segment(
      line,
      stream->tokens[else_index].position + strlen(stream->tokens[else_index].raw),
      stream->tokens[fi_index].position,
      out_if->else_branch,
      sizeof(out_if->else_branch)
    );
    out_if->has_else_branch = out_if->else_branch[0] != '\0';
  }

  if (out_if->condition[0] == '\0' || out_if->then_branch[0] == '\0') {
    snprintf(error, error_size, "if command requires a condition and a then branch");
    return 1;
  }

  if (stream->tokens[fi_index + 1].kind != ARKSH_TOKEN_EOF) {
    snprintf(error, error_size, "unexpected token after fi");
    return 1;
  }

  return 0;
}

static int parse_while_command_tokens(const char *line, const ArkshTokenStream *stream, ArkshWhileCommandNode *out_while, char *error, size_t error_size) {
  size_t do_index = stream->count;
  size_t done_index = stream->count;
  size_t i;
  int nested_depth = 0;

  if (line == NULL || stream == NULL || out_while == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  if (!token_is_word_value(stream, 0, "while")) {
    return 1;
  }

  memset(out_while, 0, sizeof(*out_while));
  for (i = 1; i < stream->count; ++i) {
    const ArkshToken *token = &stream->tokens[i];

    if (!token_starts_command(line, stream, i) || token->kind != ARKSH_TOKEN_WORD) {
      continue;
    }

    if (nested_depth == 0 && strcmp(token->text, "do") == 0) {
      do_index = i;
      break;
    }

    if (keyword_is_compound_open(token->text)) {
      nested_depth++;
    } else if (keyword_is_compound_close(token->text) && nested_depth > 0) {
      nested_depth--;
    }
  }

  if (do_index >= stream->count) {
    snprintf(error, error_size, "unterminated while command: missing do");
    return 1;
  }

  nested_depth = 0;
  for (i = do_index + 1; i < stream->count; ++i) {
    const ArkshToken *token = &stream->tokens[i];

    if (!token_starts_command(line, stream, i) || token->kind != ARKSH_TOKEN_WORD) {
      continue;
    }

    if (nested_depth == 0 && strcmp(token->text, "done") == 0) {
      done_index = i;
      break;
    }

    if (keyword_is_compound_open(token->text)) {
      nested_depth++;
    } else if (keyword_is_compound_close(token->text) && nested_depth > 0) {
      nested_depth--;
    }
  }

  if (done_index >= stream->count) {
    snprintf(error, error_size, "unterminated while command: missing done");
    return 1;
  }

  trim_compound_segment(
    line,
    stream->tokens[0].position + strlen(stream->tokens[0].raw),
    stream->tokens[do_index].position,
    out_while->condition,
    sizeof(out_while->condition)
  );
  trim_compound_segment(
    line,
    stream->tokens[do_index].position + strlen(stream->tokens[do_index].raw),
    stream->tokens[done_index].position,
    out_while->body,
    sizeof(out_while->body)
  );

  if (out_while->condition[0] == '\0' || out_while->body[0] == '\0') {
    snprintf(error, error_size, "while command requires a condition and a body");
    return 1;
  }

  if (stream->tokens[done_index + 1].kind != ARKSH_TOKEN_EOF) {
    snprintf(error, error_size, "unexpected token after done");
    return 1;
  }

  return 0;
}

static int parse_until_command_tokens(const char *line, const ArkshTokenStream *stream, ArkshUntilCommandNode *out_until, char *error, size_t error_size) {
  size_t do_index = stream->count;
  size_t done_index = stream->count;
  size_t i;
  int nested_depth = 0;

  if (line == NULL || stream == NULL || out_until == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  if (!token_is_word_value(stream, 0, "until")) {
    return 1;
  }

  memset(out_until, 0, sizeof(*out_until));
  for (i = 1; i < stream->count; ++i) {
    const ArkshToken *token = &stream->tokens[i];

    if (!token_starts_command(line, stream, i) || token->kind != ARKSH_TOKEN_WORD) {
      continue;
    }

    if (nested_depth == 0 && strcmp(token->text, "do") == 0) {
      do_index = i;
      break;
    }

    if (keyword_is_compound_open(token->text)) {
      nested_depth++;
    } else if (keyword_is_compound_close(token->text) && nested_depth > 0) {
      nested_depth--;
    }
  }

  if (do_index >= stream->count) {
    snprintf(error, error_size, "unterminated until command: missing do");
    return 1;
  }

  nested_depth = 0;
  for (i = do_index + 1; i < stream->count; ++i) {
    const ArkshToken *token = &stream->tokens[i];

    if (!token_starts_command(line, stream, i) || token->kind != ARKSH_TOKEN_WORD) {
      continue;
    }

    if (nested_depth == 0 && strcmp(token->text, "done") == 0) {
      done_index = i;
      break;
    }

    if (keyword_is_compound_open(token->text)) {
      nested_depth++;
    } else if (keyword_is_compound_close(token->text) && nested_depth > 0) {
      nested_depth--;
    }
  }

  if (done_index >= stream->count) {
    snprintf(error, error_size, "unterminated until command: missing done");
    return 1;
  }

  trim_compound_segment(
    line,
    stream->tokens[0].position + strlen(stream->tokens[0].raw),
    stream->tokens[do_index].position,
    out_until->condition,
    sizeof(out_until->condition)
  );
  trim_compound_segment(
    line,
    stream->tokens[do_index].position + strlen(stream->tokens[do_index].raw),
    stream->tokens[done_index].position,
    out_until->body,
    sizeof(out_until->body)
  );

  if (out_until->condition[0] == '\0' || out_until->body[0] == '\0') {
    snprintf(error, error_size, "until command requires a condition and a body");
    return 1;
  }

  if (stream->tokens[done_index + 1].kind != ARKSH_TOKEN_EOF) {
    snprintf(error, error_size, "unexpected token after done");
    return 1;
  }

  return 0;
}

static int parse_for_command_tokens(const char *line, const ArkshTokenStream *stream, ArkshForCommandNode *out_for, char *error, size_t error_size) {
  size_t do_index = stream->count;
  size_t done_index = stream->count;
  size_t i;
  int nested_depth = 0;

  if (line == NULL || stream == NULL || out_for == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  if (!token_is_word_value(stream, 0, "for")) {
    return 1;
  }
  if (stream->tokens[1].kind != ARKSH_TOKEN_WORD || !is_identifier_text(stream->tokens[1].text)) {
    snprintf(error, error_size, "for command expects an identifier after 'for'");
    return 1;
  }
  if (!token_is_word_value(stream, 2, "in")) {
    snprintf(error, error_size, "for command expects 'in' after the loop variable");
    return 1;
  }

  memset(out_for, 0, sizeof(*out_for));
  copy_string(out_for->variable, sizeof(out_for->variable), stream->tokens[1].text);

  for (i = 3; i < stream->count; ++i) {
    const ArkshToken *token = &stream->tokens[i];

    if (!token_starts_command(line, stream, i) || token->kind != ARKSH_TOKEN_WORD) {
      continue;
    }

    if (nested_depth == 0 && strcmp(token->text, "do") == 0) {
      do_index = i;
      break;
    }

    if (keyword_is_compound_open(token->text)) {
      nested_depth++;
    } else if (keyword_is_compound_close(token->text) && nested_depth > 0) {
      nested_depth--;
    }
  }

  if (do_index >= stream->count) {
    snprintf(error, error_size, "unterminated for command: missing do");
    return 1;
  }

  nested_depth = 0;
  for (i = do_index + 1; i < stream->count; ++i) {
    const ArkshToken *token = &stream->tokens[i];

    if (!token_starts_command(line, stream, i) || token->kind != ARKSH_TOKEN_WORD) {
      continue;
    }

    if (nested_depth == 0 && strcmp(token->text, "done") == 0) {
      done_index = i;
      break;
    }

    if (keyword_is_compound_open(token->text)) {
      nested_depth++;
    } else if (keyword_is_compound_close(token->text) && nested_depth > 0) {
      nested_depth--;
    }
  }

  if (done_index >= stream->count) {
    snprintf(error, error_size, "unterminated for command: missing done");
    return 1;
  }

  trim_compound_segment(
    line,
    stream->tokens[2].position + strlen(stream->tokens[2].raw),
    stream->tokens[do_index].position,
    out_for->source,
    sizeof(out_for->source)
  );
  trim_compound_segment(
    line,
    stream->tokens[do_index].position + strlen(stream->tokens[do_index].raw),
    stream->tokens[done_index].position,
    out_for->body,
    sizeof(out_for->body)
  );

  if (out_for->source[0] == '\0' || out_for->body[0] == '\0') {
    snprintf(error, error_size, "for command requires an item source and a body");
    return 1;
  }

  if (stream->tokens[done_index + 1].kind != ARKSH_TOKEN_EOF) {
    snprintf(error, error_size, "unexpected token after done");
    return 1;
  }

  return 0;
}

static int parse_case_command_tokens(const char *line, const ArkshTokenStream *stream, ArkshCaseCommandNode *out_case, char *error, size_t error_size) {
  size_t in_position = 0;
  size_t in_index = stream->count;
  size_t esac_index = stream->count;
  size_t section_index;
  size_t i;
  int nested_depth = 0;

  if (line == NULL || stream == NULL || out_case == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  if (!token_is_word_value(stream, 0, "case")) {
    return 1;
  }

  memset(out_case, 0, sizeof(*out_case));
  if (find_top_level_case_in_keyword(line, strlen(stream->tokens[0].raw), &in_position) != 0) {
    snprintf(error, error_size, "unterminated case command: missing in");
    return 1;
  }

  for (i = 1; i < stream->count; ++i) {
    if (stream->tokens[i].position == in_position && token_is_word_value(stream, i, "in")) {
      in_index = i;
      break;
    }
  }

  if (in_index >= stream->count) {
    snprintf(error, error_size, "unterminated case command: missing in");
    return 1;
  }

  trim_compound_segment(
    line,
    stream->tokens[0].position + strlen(stream->tokens[0].raw),
    stream->tokens[in_index].position,
    out_case->expression,
    sizeof(out_case->expression)
  );
  if (out_case->expression[0] == '\0') {
    snprintf(error, error_size, "case command requires an expression");
    return 1;
  }

  section_index = in_index + 1;
  while (stream->tokens[section_index].kind == ARKSH_TOKEN_SEQUENCE) {
    section_index++;
  }

  while (section_index < stream->count) {
    size_t pattern_start_index = section_index;
    size_t pattern_end_index = stream->count;
    size_t next_section_index = stream->count;
    size_t body_end_position = strlen(line);
    ArkshCaseBranchNode *branch;

    if (token_is_word_value(stream, section_index, "esac")) {
      esac_index = section_index;
      break;
    }

    if (stream->tokens[pattern_start_index].kind == ARKSH_TOKEN_LPAREN) {
      pattern_start_index++;
    }

    for (i = pattern_start_index; i < stream->count; ++i) {
      const ArkshToken *token = &stream->tokens[i];

      if (position_is_inside_nested_structure(line, token->position)) {
        continue;
      }
      if (token->kind == ARKSH_TOKEN_RPAREN) {
        pattern_end_index = i;
        break;
      }
      if (token_is_word_value(stream, i, "esac")) {
        break;
      }
    }

    if (pattern_end_index >= stream->count) {
      snprintf(error, error_size, "case branch requires a closing ')'");
      return 1;
    }
    if (pattern_start_index >= pattern_end_index) {
      snprintf(error, error_size, "case branch requires at least one pattern");
      return 1;
    }

    if (out_case->branch_count >= ARKSH_MAX_CASE_BRANCHES) {
      snprintf(error, error_size, "too many case branches");
      return 1;
    }
    branch = &out_case->branches[out_case->branch_count];
    trim_compound_segment(
      line,
      stream->tokens[pattern_start_index].position,
      stream->tokens[pattern_end_index].position,
      branch->patterns,
      sizeof(branch->patterns)
    );
    if (branch->patterns[0] == '\0') {
      snprintf(error, error_size, "case branch requires at least one pattern");
      return 1;
    }

    nested_depth = 0;
    for (i = pattern_end_index + 1; i < stream->count; ++i) {
      const ArkshToken *token = &stream->tokens[i];

      if (position_is_inside_nested_structure(line, token->position)) {
        continue;
      }
      if (token_starts_command(line, stream, i) && token->kind == ARKSH_TOKEN_WORD) {
        if (nested_depth == 0 && strcmp(token->text, "esac") == 0) {
          body_end_position = token->position;
          next_section_index = i;
          break;
        }
        if (keyword_is_compound_open(token->text)) {
          nested_depth++;
        } else if (keyword_is_compound_close(token->text) && nested_depth > 0) {
          nested_depth--;
        }
      }

      if (nested_depth == 0 &&
          token->kind == ARKSH_TOKEN_SEQUENCE &&
          i + 1 < stream->count &&
          stream->tokens[i + 1].kind == ARKSH_TOKEN_SEQUENCE) {
        body_end_position = token->position;
        next_section_index = i + 2;
        break;
      }
    }

    if (next_section_index >= stream->count) {
      snprintf(error, error_size, "unterminated case command: missing ;; or esac");
      return 1;
    }

    trim_compound_segment(
      line,
      stream->tokens[pattern_end_index].position + strlen(stream->tokens[pattern_end_index].raw),
      body_end_position,
      branch->body,
      sizeof(branch->body)
    );
    out_case->branch_count++;

    section_index = next_section_index;
    while (stream->tokens[section_index].kind == ARKSH_TOKEN_SEQUENCE) {
      section_index++;
    }
  }

  if (esac_index >= stream->count) {
    snprintf(error, error_size, "unterminated case command: missing esac");
    return 1;
  }
  if (out_case->branch_count == 0) {
    snprintf(error, error_size, "case command requires at least one branch");
    return 1;
  }
  if (stream->tokens[esac_index + 1].kind != ARKSH_TOKEN_EOF) {
    snprintf(error, error_size, "unexpected token after esac");
    return 1;
  }

  return 0;
}

static int parse_switch_command_tokens(const char *line, const ArkshTokenStream *stream, ArkshSwitchCommandNode *out_switch, char *error, size_t error_size) {
  size_t first_branch_index = stream->count;
  size_t section_index;
  size_t i;
  int nested_depth = 0;

  if (line == NULL || stream == NULL || out_switch == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  if (!token_is_word_value(stream, 0, "switch")) {
    return 1;
  }

  memset(out_switch, 0, sizeof(*out_switch));

  for (i = 1; i < stream->count; ++i) {
    const ArkshToken *token = &stream->tokens[i];

    if (!token_starts_command(line, stream, i) || token->kind != ARKSH_TOKEN_WORD) {
      continue;
    }

    if (nested_depth == 0 &&
        (strcmp(token->text, "case") == 0 ||
         strcmp(token->text, "default") == 0 ||
         strcmp(token->text, "endswitch") == 0)) {
      first_branch_index = i;
      break;
    }

    if (keyword_is_compound_open(token->text)) {
      nested_depth++;
    } else if (keyword_is_compound_close(token->text) && nested_depth > 0) {
      nested_depth--;
    }
  }

  if (first_branch_index >= stream->count) {
    snprintf(error, error_size, "unterminated switch command: missing case, default or endswitch");
    return 1;
  }

  if (token_is_word_value(stream, first_branch_index, "endswitch")) {
    snprintf(error, error_size, "switch command requires at least one case or default branch");
    return 1;
  }

  trim_compound_segment(
    line,
    stream->tokens[0].position + strlen(stream->tokens[0].raw),
    stream->tokens[first_branch_index].position,
    out_switch->expression,
    sizeof(out_switch->expression)
  );
  if (out_switch->expression[0] == '\0') {
    snprintf(error, error_size, "switch command requires an expression");
    return 1;
  }

  section_index = first_branch_index;
  while (section_index < stream->count) {
    size_t then_index = stream->count;
    size_t next_section_index = stream->count;
    size_t branch_boundary_index = stream->count;
    const ArkshToken *section_token = &stream->tokens[section_index];

    if (section_token->kind != ARKSH_TOKEN_WORD) {
      snprintf(error, error_size, "invalid switch branch");
      return 1;
    }
    if (strcmp(section_token->text, "endswitch") == 0) {
      break;
    }

    nested_depth = 0;
    for (i = section_index + 1; i < stream->count; ++i) {
      const ArkshToken *token = &stream->tokens[i];

      if (!token_starts_command(line, stream, i) || token->kind != ARKSH_TOKEN_WORD) {
        continue;
      }

      if (nested_depth == 0 &&
          (strcmp(token->text, "case") == 0 ||
           strcmp(token->text, "default") == 0 ||
           strcmp(token->text, "endswitch") == 0)) {
        branch_boundary_index = i;
        break;
      }

      if (nested_depth == 0 && strcmp(token->text, "then") == 0) {
        then_index = i;
        break;
      }

      if (keyword_is_compound_open(token->text)) {
        nested_depth++;
      } else if (keyword_is_compound_close(token->text) && nested_depth > 0) {
        nested_depth--;
      }
    }

    if (then_index >= stream->count) {
      if (branch_boundary_index < stream->count) {
        snprintf(error, error_size, "switch branch requires then");
      } else {
        snprintf(error, error_size, "unterminated switch command: missing then");
      }
      return 1;
    }

    nested_depth = 0;
    for (i = then_index + 1; i < stream->count; ++i) {
      const ArkshToken *token = &stream->tokens[i];

      if (!token_starts_command(line, stream, i) || token->kind != ARKSH_TOKEN_WORD) {
        continue;
      }

      if (nested_depth == 0 &&
          (strcmp(token->text, "case") == 0 ||
           strcmp(token->text, "default") == 0 ||
           strcmp(token->text, "endswitch") == 0)) {
        next_section_index = i;
        break;
      }

      if (keyword_is_compound_open(token->text)) {
        nested_depth++;
      } else if (keyword_is_compound_close(token->text) && nested_depth > 0) {
        nested_depth--;
      }
    }

    if (next_section_index >= stream->count) {
      snprintf(error, error_size, "unterminated switch command: missing endswitch");
      return 1;
    }

    if (strcmp(section_token->text, "case") == 0) {
      ArkshSwitchCaseNode *case_node;

      if (out_switch->case_count >= ARKSH_MAX_SWITCH_CASES) {
        snprintf(error, error_size, "too many switch cases");
        return 1;
      }

      case_node = &out_switch->cases[out_switch->case_count];
      trim_compound_segment(
        line,
        section_token->position + strlen(section_token->raw),
        stream->tokens[then_index].position,
        case_node->match,
        sizeof(case_node->match)
      );
      trim_compound_segment(
        line,
        stream->tokens[then_index].position + strlen(stream->tokens[then_index].raw),
        stream->tokens[next_section_index].position,
        case_node->body,
        sizeof(case_node->body)
      );

      if (case_node->match[0] == '\0' || case_node->body[0] == '\0') {
        snprintf(error, error_size, "switch case requires a match expression and a body");
        return 1;
      }

      out_switch->case_count++;
    } else if (strcmp(section_token->text, "default") == 0) {
      char default_header[ARKSH_MAX_LINE];

      if (out_switch->has_default_branch) {
        snprintf(error, error_size, "switch command only supports one default branch");
        return 1;
      }
      if (!token_is_word_value(stream, next_section_index, "endswitch")) {
        snprintf(error, error_size, "default branch must be the last branch in switch");
        return 1;
      }

      trim_compound_segment(
        line,
        section_token->position + strlen(section_token->raw),
        stream->tokens[then_index].position,
        default_header,
        sizeof(default_header)
      );
      trim_compound_segment(
        line,
        stream->tokens[then_index].position + strlen(stream->tokens[then_index].raw),
        stream->tokens[next_section_index].position,
        out_switch->default_branch,
        sizeof(out_switch->default_branch)
      );

      if (default_header[0] != '\0') {
        snprintf(error, error_size, "default branch does not accept a match expression");
        return 1;
      }
      if (out_switch->default_branch[0] == '\0') {
        snprintf(error, error_size, "default branch requires a body");
        return 1;
      }
      out_switch->has_default_branch = 1;
    } else {
      snprintf(error, error_size, "invalid switch branch: %s", section_token->text);
      return 1;
    }

    section_index = next_section_index;
    if (token_is_word_value(stream, section_index, "endswitch")) {
      break;
    }
  }

  if (section_index >= stream->count || !token_is_word_value(stream, section_index, "endswitch")) {
    snprintf(error, error_size, "unterminated switch command: missing endswitch");
    return 1;
  }

  if (out_switch->case_count == 0 && !out_switch->has_default_branch) {
    snprintf(error, error_size, "switch command requires at least one case or default branch");
    return 1;
  }

  if (stream->tokens[section_index + 1].kind != ARKSH_TOKEN_EOF) {
    snprintf(error, error_size, "unexpected token after endswitch");
    return 1;
  }

  return 0;
}

static int parse_function_params_tokens(
  const ArkshTokenStream *stream,
  size_t start_index,
  size_t end_index,
  ArkshFunctionCommandNode *out_function,
  char *error,
  size_t error_size
) {
  size_t index = start_index;

  if (stream == NULL || out_function == NULL || error == NULL || error_size == 0 || end_index < start_index) {
    return 1;
  }

  out_function->param_count = 0;
  if (index == end_index) {
    return 0;
  }

  while (index < end_index) {
    if (stream->tokens[index].kind != ARKSH_TOKEN_WORD || !is_identifier_text(stream->tokens[index].text)) {
      snprintf(error, error_size, "function parameters must be valid identifiers");
      return 1;
    }
    if (out_function->param_count >= ARKSH_MAX_FUNCTION_PARAMS) {
      snprintf(error, error_size, "too many function parameters");
      return 1;
    }
    for (int existing = 0; existing < out_function->param_count; ++existing) {
      if (strcmp(out_function->params[existing], stream->tokens[index].text) == 0) {
        snprintf(error, error_size, "duplicate function parameter: %s", stream->tokens[index].text);
        return 1;
      }
    }

    copy_string(
      out_function->params[out_function->param_count],
      sizeof(out_function->params[out_function->param_count]),
      stream->tokens[index].text
    );
    out_function->param_count++;
    index++;

    if (index == end_index) {
      break;
    }

    if (stream->tokens[index].kind != ARKSH_TOKEN_COMMA) {
      snprintf(error, error_size, "function parameter list expects commas between parameters");
      return 1;
    }
    index++;
    if (index == end_index) {
      snprintf(error, error_size, "function parameter list cannot end with a comma");
      return 1;
    }
  }

  return 0;
}

static int parse_function_command_tokens(
  const char *line,
  const ArkshTokenStream *stream,
  ArkshFunctionCommandNode *out_function,
  char *error,
  size_t error_size
) {
  size_t header_index = 2;
  size_t do_index = stream->count;
  size_t endfunction_index = stream->count;
  size_t i;
  int nested_depth = 0;

  if (line == NULL || stream == NULL || out_function == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  if (!token_is_word_value(stream, 0, "function")) {
    return 1;
  }

  if (stream->tokens[1].kind == ARKSH_TOKEN_EOF) {
    error[0] = '\0';
    return 1;
  }
  if (stream->tokens[1].kind != ARKSH_TOKEN_WORD || !is_identifier_text(stream->tokens[1].text)) {
    snprintf(error, error_size, "function command expects an identifier after 'function'");
    return 1;
  }

  memset(out_function, 0, sizeof(*out_function));
  copy_string(out_function->name, sizeof(out_function->name), stream->tokens[1].text);
  copy_string(out_function->source, sizeof(out_function->source), line);

  if (stream->tokens[header_index].kind == ARKSH_TOKEN_LPAREN) {
    size_t close_index = stream->count;

    for (i = header_index + 1; i < stream->count; ++i) {
      if (stream->tokens[i].kind == ARKSH_TOKEN_RPAREN) {
        close_index = i;
        break;
      }
    }

    if (close_index >= stream->count) {
      snprintf(error, error_size, "unterminated function command: missing ')'");
      return 1;
    }
    if (parse_function_params_tokens(stream, header_index + 1, close_index, out_function, error, error_size) != 0) {
      return 1;
    }
    header_index = close_index + 1;
  }

  while (stream->tokens[header_index].kind == ARKSH_TOKEN_SEQUENCE) {
    header_index++;
  }

  if (!token_is_word_value(stream, header_index, "do")) {
    snprintf(error, error_size, "unterminated function command: missing do");
    return 1;
  }
  do_index = header_index;

  for (i = do_index + 1; i < stream->count; ++i) {
    const ArkshToken *token = &stream->tokens[i];

    if (!token_starts_command(line, stream, i) || token->kind != ARKSH_TOKEN_WORD) {
      continue;
    }

    if (nested_depth == 0 && strcmp(token->text, "endfunction") == 0) {
      endfunction_index = i;
      break;
    }

    if (keyword_is_compound_open(token->text)) {
      nested_depth++;
    } else if (keyword_is_compound_close(token->text) && nested_depth > 0) {
      nested_depth--;
    }
  }

  if (endfunction_index >= stream->count) {
    snprintf(error, error_size, "unterminated function command: missing endfunction");
    return 1;
  }

  trim_compound_segment(
    line,
    stream->tokens[do_index].position + strlen(stream->tokens[do_index].raw),
    stream->tokens[endfunction_index].position,
    out_function->body,
    sizeof(out_function->body)
  );

  if (out_function->body[0] == '\0') {
    snprintf(error, error_size, "function command requires a body");
    return 1;
  }

  if (stream->tokens[endfunction_index + 1].kind != ARKSH_TOKEN_EOF) {
    snprintf(error, error_size, "unexpected token after endfunction");
    return 1;
  }

  return 0;
}

static int parse_class_command_tokens(
  const char *line,
  const ArkshTokenStream *stream,
  ArkshClassCommandNode *out_class,
  char *error,
  size_t error_size
) {
  size_t header_index = 2;
  size_t do_index = stream->count;
  size_t endclass_index = stream->count;
  size_t i;
  int nested_depth = 0;

  if (line == NULL || stream == NULL || out_class == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  if (!token_is_word_value(stream, 0, "class")) {
    return 1;
  }

  if (stream->tokens[1].kind == ARKSH_TOKEN_EOF) {
    error[0] = '\0';
    return 1;
  }
  if (stream->tokens[1].kind != ARKSH_TOKEN_WORD || !is_identifier_text(stream->tokens[1].text)) {
    snprintf(error, error_size, "class command expects an identifier after 'class'");
    return 1;
  }

  memset(out_class, 0, sizeof(*out_class));
  copy_string(out_class->name, sizeof(out_class->name), stream->tokens[1].text);
  copy_string(out_class->source, sizeof(out_class->source), line);

  while (stream->tokens[header_index].kind == ARKSH_TOKEN_SEQUENCE) {
    header_index++;
  }

  if (token_is_word_value(stream, header_index, "extends")) {
    int expect_base = 1;

    header_index++;
    while (stream->tokens[header_index].kind != ARKSH_TOKEN_EOF) {
      if (token_is_word_value(stream, header_index, "do")) {
        break;
      }
      if (stream->tokens[header_index].kind == ARKSH_TOKEN_SEQUENCE) {
        header_index++;
        continue;
      }

      if (expect_base) {
        int existing;

        if (stream->tokens[header_index].kind != ARKSH_TOKEN_WORD || !is_identifier_text(stream->tokens[header_index].text)) {
          snprintf(error, error_size, "class extends list expects base class identifiers");
          return 1;
        }
        if (out_class->base_count >= ARKSH_MAX_CLASS_BASES) {
          snprintf(error, error_size, "too many base classes");
          return 1;
        }
        for (existing = 0; existing < out_class->base_count; ++existing) {
          if (strcmp(out_class->bases[existing], stream->tokens[header_index].text) == 0) {
            snprintf(error, error_size, "duplicate base class: %s", stream->tokens[header_index].text);
            return 1;
          }
        }

        copy_string(
          out_class->bases[out_class->base_count],
          sizeof(out_class->bases[out_class->base_count]),
          stream->tokens[header_index].text
        );
        out_class->base_count++;
        expect_base = 0;
        header_index++;
        continue;
      }

      if (stream->tokens[header_index].kind != ARKSH_TOKEN_COMMA) {
        snprintf(error, error_size, "class extends list expects commas between base classes");
        return 1;
      }
      expect_base = 1;
      header_index++;
    }

    if (out_class->base_count == 0) {
      snprintf(error, error_size, "class extends list requires at least one base class");
      return 1;
    }
    if (expect_base) {
      snprintf(error, error_size, "class extends list cannot end with a comma");
      return 1;
    }
  }

  while (stream->tokens[header_index].kind == ARKSH_TOKEN_SEQUENCE) {
    header_index++;
  }

  if (!token_is_word_value(stream, header_index, "do")) {
    snprintf(error, error_size, "unterminated class command: missing do");
    return 1;
  }
  do_index = header_index;

  for (i = do_index + 1; i < stream->count; ++i) {
    const ArkshToken *token = &stream->tokens[i];

    if (token->kind != ARKSH_TOKEN_WORD) {
      continue;
    }

    if (nested_depth == 0 && strcmp(token->text, "endclass") == 0) {
      endclass_index = i;
      break;
    }

    if (!token_starts_command(line, stream, i)) {
      continue;
    }

    if (keyword_is_compound_open(token->text)) {
      nested_depth++;
    } else if (keyword_is_compound_close(token->text) && nested_depth > 0) {
      nested_depth--;
    }
  }

  if (endclass_index >= stream->count) {
    snprintf(error, error_size, "unterminated class command: missing endclass");
    return 1;
  }

  trim_compound_segment(
    line,
    stream->tokens[do_index].position + strlen(stream->tokens[do_index].raw),
    stream->tokens[endclass_index].position,
    out_class->body,
    sizeof(out_class->body)
  );

  if (stream->tokens[endclass_index + 1].kind != ARKSH_TOKEN_EOF) {
    snprintf(error, error_size, "unexpected token after endclass");
    return 1;
  }

  return 0;
}

void arksh_ast_init(ArkshAst *ast) {
  if (ast == NULL) {
    return;
  }

  memset(ast, 0, sizeof(*ast));
  ast->kind = ARKSH_AST_EMPTY;
}

int arksh_parse_line(const char *line, ArkshAst *out_ast, char *error, size_t error_size) {
  ArkshTokenStream stream;
  ArkshParsedHeredocList heredocs;
  size_t first_object_pipe_index = 0;
  int has_object_pipe = 0;
  int has_arrow = 0;
  int has_shell_pipe = 0;
  int has_redirection = 0;
  int has_list_control = 0;
  int has_command_boundary = 0;
  size_t i;
  char trimmed[ARKSH_MAX_LINE];

  if (line == NULL || out_ast == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  arksh_ast_init(out_ast);
  error[0] = '\0';
  trim_copy(line, trimmed, sizeof(trimmed));
  copy_string(out_ast->line, sizeof(out_ast->line), trimmed);

  if (trimmed[0] == '\0') {
    return 0;
  }

  {
    char heredoc_header[ARKSH_MAX_LINE];
    int heredoc_status = 1;

    if (!starts_with_compound_construct(trimmed) && !contains_top_level_list_operator(trimmed)) {
      heredoc_status = collect_leading_heredocs(trimmed, heredoc_header, sizeof(heredoc_header), &heredocs, error, error_size);
    }

    if (heredoc_status == 0) {
      copy_string(trimmed, sizeof(trimmed), heredoc_header);
      copy_string(out_ast->line, sizeof(out_ast->line), heredoc_header);
    } else if (heredoc_status == 2) {
      return 1;
    } else {
      memset(&heredocs, 0, sizeof(heredocs));
    }
  }

  if (arksh_lex_line(trimmed, &stream, error, error_size) != 0) {
    return 1;
  }

  if (parse_if_command_tokens(trimmed, &stream, &out_ast->as.if_command, error, error_size) == 0) {
    out_ast->kind = ARKSH_AST_IF_COMMAND;
    return 0;
  }
  if (strncmp(error, "unterminated if command", 22) == 0 || strncmp(error, "if command ", 11) == 0 || strcmp(error, "unexpected token after fi") == 0) {
    return 1;
  }

  if (parse_while_command_tokens(trimmed, &stream, &out_ast->as.while_command, error, error_size) == 0) {
    out_ast->kind = ARKSH_AST_WHILE_COMMAND;
    return 0;
  }
  if (strncmp(error, "unterminated while command", 25) == 0 || strncmp(error, "while command ", 14) == 0 || strcmp(error, "unexpected token after done") == 0) {
    return 1;
  }

  if (parse_until_command_tokens(trimmed, &stream, &out_ast->as.until_command, error, error_size) == 0) {
    out_ast->kind = ARKSH_AST_UNTIL_COMMAND;
    return 0;
  }
  if (strncmp(error, "unterminated until command", 25) == 0 || strncmp(error, "until command ", 14) == 0 || strcmp(error, "unexpected token after done") == 0) {
    return 1;
  }

  if (parse_for_command_tokens(trimmed, &stream, &out_ast->as.for_command, error, error_size) == 0) {
    out_ast->kind = ARKSH_AST_FOR_COMMAND;
    return 0;
  }
  if (strncmp(error, "unterminated for command", 23) == 0 || strncmp(error, "for command ", 12) == 0 || strcmp(error, "unexpected token after done") == 0) {
    return 1;
  }

  if (parse_case_command_tokens(trimmed, &stream, &out_ast->as.case_command, error, error_size) == 0) {
    out_ast->kind = ARKSH_AST_CASE_COMMAND;
    return 0;
  }
  if (strncmp(error, "unterminated case command", 24) == 0 ||
      strncmp(error, "case branch ", 12) == 0 ||
      strncmp(error, "case command ", 13) == 0 ||
      strcmp(error, "unexpected token after esac") == 0) {
    return 1;
  }

  if (parse_switch_command_tokens(trimmed, &stream, &out_ast->as.switch_command, error, error_size) == 0) {
    out_ast->kind = ARKSH_AST_SWITCH_COMMAND;
    return 0;
  }
  if (strncmp(error, "unterminated switch command", 27) == 0 ||
      strncmp(error, "switch command ", 15) == 0 ||
      strncmp(error, "switch branch ", 14) == 0 ||
      strcmp(error, "unexpected token after endswitch") == 0 ||
      strcmp(error, "default branch does not accept a match expression") == 0 ||
      strcmp(error, "default branch must be the last branch in switch") == 0) {
    return 1;
  }

  if (parse_function_command_tokens(trimmed, &stream, &out_ast->as.function_command, error, error_size) == 0) {
    out_ast->kind = ARKSH_AST_FUNCTION_COMMAND;
    return 0;
  }
  if (token_is_word_value(&stream, 0, "function") && error[0] != '\0') {
    return 1;
  }

  if (parse_class_command_tokens(trimmed, &stream, &out_ast->as.class_command, error, error_size) == 0) {
    out_ast->kind = ARKSH_AST_CLASS_COMMAND;
    return 0;
  }
  if (token_is_word_value(&stream, 0, "class") && error[0] != '\0') {
    return 1;
  }

  if (parse_group_command_text(trimmed, &out_ast->as.group_command, error, error_size) == 0) {
    out_ast->kind = ARKSH_AST_GROUP_COMMAND;
    return 0;
  }
  if (trimmed[0] == '{' && error[0] != '\0' && strcmp(error, "unexpected token after }") != 0) {
    return 1;
  }

  if (parse_subshell_command_text(trimmed, &out_ast->as.subshell_command, error, error_size) == 0) {
    out_ast->kind = ARKSH_AST_SUBSHELL_COMMAND;
    return 0;
  }
  if (trimmed[0] == '(' && error[0] != '\0' && strcmp(error, "unexpected token after )") != 0) {
    return 1;
  }

  error[0] = '\0';

  for (i = 0; i < stream.count; ++i) {
    if (is_list_control_token(stream.tokens[i].kind) && !position_is_inside_nested_structure(trimmed, stream.tokens[i].position)) {
      has_list_control = 1;
      break;
    }
  }

  has_command_boundary = has_top_level_command_boundary(trimmed, &stream);

  if (has_list_control || has_command_boundary) {
    if (parse_command_list_tokens(trimmed, &stream, &out_ast->as.command_list, error, error_size) != 0) {
      return 1;
    }
    out_ast->kind = ARKSH_AST_COMMAND_LIST;
    return 0;
  }

  for (i = 0; i < stream.count; ++i) {
    if (stream.tokens[i].kind == ARKSH_TOKEN_OBJECT_PIPE) {
      has_object_pipe = 1;
      first_object_pipe_index = i;
      break;
    }
    if (stream.tokens[i].kind == ARKSH_TOKEN_ARROW) {
      has_arrow = 1;
    }
  }

  if (has_object_pipe) {
    char source_text[ARKSH_MAX_LINE];
    size_t previous_position;

    out_ast->kind = ARKSH_AST_OBJECT_PIPELINE;
    copy_trimmed_slice(trimmed, 0, stream.tokens[first_object_pipe_index].position, source_text, sizeof(source_text));
    if (parse_value_source_text_ex(source_text, &out_ast->as.pipeline.source, 1, error, error_size) != 0) {
      /* E3-S3 bridge: unrecognized source → treat as shell command, capture stdout.
         Enables `ls -la |> lines |> count` without extra quoting. */
      if (source_text[0] != '\0') {
        error[0] = '\0';
        memset(&out_ast->as.pipeline.source, 0, sizeof(out_ast->as.pipeline.source));
        out_ast->as.pipeline.source.kind = ARKSH_VALUE_SOURCE_CAPTURE_SHELL;
        copy_string(out_ast->as.pipeline.source.text,     sizeof(out_ast->as.pipeline.source.text),     source_text);
        copy_string(out_ast->as.pipeline.source.raw_text, sizeof(out_ast->as.pipeline.source.raw_text), source_text);
      } else {
        if (error[0] == '\0') {
          snprintf(error, error_size, "invalid pipeline source");
        }
        return 1;
      }
    }

    previous_position = stream.tokens[first_object_pipe_index].position + strlen(stream.tokens[first_object_pipe_index].text);

    for (i = first_object_pipe_index + 1; i < stream.count; ++i) {
      if (stream.tokens[i].kind == ARKSH_TOKEN_OBJECT_PIPE || stream.tokens[i].kind == ARKSH_TOKEN_EOF) {
        char stage_text[ARKSH_MAX_LINE];

        if (out_ast->as.pipeline.stage_count >= ARKSH_MAX_PIPELINE_STAGES) {
          snprintf(error, error_size, "too many pipeline stages");
          return 1;
        }

        copy_trimmed_slice(trimmed, previous_position, stream.tokens[i].position, stage_text, sizeof(stage_text));
        if (parse_pipeline_stage_text(stage_text, &out_ast->as.pipeline.stages[out_ast->as.pipeline.stage_count]) != 0) {
          snprintf(error, error_size, "invalid pipeline stage: %s", stage_text);
          return 1;
        }

        out_ast->as.pipeline.stage_count++;
        previous_position = stream.tokens[i].position + strlen(stream.tokens[i].text);
      }
    }

    return out_ast->as.pipeline.stage_count == 0 ? 1 : 0;
  }

  if (parse_object_expression_text(trimmed, &out_ast->as.object_expression, error, error_size) == 0) {
    out_ast->kind = ARKSH_AST_OBJECT_EXPRESSION;
    return 0;
  }

  error[0] = '\0';
  {
    int value_source_status = parse_value_source_text(trimmed, &out_ast->as.value_expression, error, error_size);
    if (value_source_status == 0 && out_ast->as.value_expression.kind != ARKSH_VALUE_SOURCE_OBJECT_EXPRESSION) {
      out_ast->kind = ARKSH_AST_VALUE_EXPRESSION;
      return 0;
    }
    if (value_source_status == 2) {
      return 1;
    }
  }

  for (i = 0; i < stream.count; ++i) {
    if (stream.tokens[i].kind == ARKSH_TOKEN_ARROW) {
      has_arrow = 1;
    }
    if (stream.tokens[i].kind == ARKSH_TOKEN_SHELL_PIPE) {
      has_shell_pipe = 1;
    }
    if (is_redirect_token(stream.tokens[i].kind)) {
      has_redirection = 1;
    }
  }

  if (has_arrow || strncmp(trimmed, "obj(", 4) == 0) {
    if (has_shell_pipe || has_redirection) {
      snprintf(error, error_size, "shell pipe and redirection are not supported after object expressions yet");
    } else if (error[0] == '\0') {
      snprintf(error, error_size, "invalid object expression");
    }
    return 1;
  }

  error[0] = '\0';
  if (has_shell_pipe || has_redirection) {
    if (parse_command_pipeline_tokens(&stream, &heredocs, &out_ast->as.command_pipeline, error, error_size) != 0) {
      return 1;
    }
    out_ast->kind = ARKSH_AST_COMMAND_PIPELINE;
    return 0;
  }

  if (parse_simple_command_tokens(&stream, &out_ast->as.command, error, error_size) != 0) {
    return 1;
  }

  out_ast->kind = out_ast->as.command.argc == 0 ? ARKSH_AST_EMPTY : ARKSH_AST_SIMPLE_COMMAND;
  return 0;
}
