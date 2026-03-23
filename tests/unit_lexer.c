/* E8-S1-T1: unit tests for arksh_lex_line().
 *
 * Standalone executable — returns 0 on success, 1 on any failure.
 * Each EXPECT() call prints a diagnostic and sets a global failure flag.
 */

#include <stdio.h>
#include <string.h>

#include "arksh/lexer.h"

/* ------------------------------------------------------------------ helpers */

static int g_failures = 0;

#define EXPECT(cond, msg)                                             \
  do {                                                                \
    if (!(cond)) {                                                    \
      fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, (msg));\
      g_failures++;                                                   \
    }                                                                 \
  } while (0)

static int lex(const char *line, ArkshTokenStream *s) {
  char error[256];
  s->count = 0;
  return arksh_lex_line(line, s, error, sizeof(error));
}

/* Convenience: lex and assert no error */
static ArkshTokenStream lex_ok(const char *line) {
  ArkshTokenStream s;
  char error[256];
  s.count = 0;
  int rc = arksh_lex_line(line, &s, error, sizeof(error));
  if (rc != 0) {
    fprintf(stderr, "FAIL unexpected lex error for \"%s\": %s\n", line, error);
    g_failures++;
  }
  return s;
}

/* ------------------------------------------------------------------ tests */

static void test_empty_line(void) {
  ArkshTokenStream s = lex_ok("");
  EXPECT(s.count == 1, "empty line: count == 1");
  EXPECT(s.tokens[0].kind == ARKSH_TOKEN_EOF, "empty line: token is EOF");
}

static void test_blank_line(void) {
  ArkshTokenStream s = lex_ok("   ");
  EXPECT(s.count == 1, "blank line: count == 1");
  EXPECT(s.tokens[0].kind == ARKSH_TOKEN_EOF, "blank line: token is EOF");
}

static void test_single_word(void) {
  ArkshTokenStream s = lex_ok("hello");
  EXPECT(s.count == 2, "single word: count == 2");
  EXPECT(s.tokens[0].kind == ARKSH_TOKEN_WORD, "single word: kind WORD");
  EXPECT(strcmp(s.tokens[0].text, "hello") == 0, "single word: text");
  EXPECT(strcmp(s.tokens[0].raw, "hello") == 0, "single word: raw");
  EXPECT(s.tokens[0].position == 0, "single word: position == 0");
  EXPECT(s.tokens[1].kind == ARKSH_TOKEN_EOF, "single word: last is EOF");
}

static void test_two_words(void) {
  ArkshTokenStream s = lex_ok("foo bar");
  EXPECT(s.count == 3, "two words: count == 3");
  EXPECT(s.tokens[0].kind == ARKSH_TOKEN_WORD, "two words: first WORD");
  EXPECT(strcmp(s.tokens[0].text, "foo") == 0, "two words: first text");
  EXPECT(s.tokens[1].kind == ARKSH_TOKEN_WORD, "two words: second WORD");
  EXPECT(strcmp(s.tokens[1].text, "bar") == 0, "two words: second text");
  EXPECT(s.tokens[2].kind == ARKSH_TOKEN_EOF, "two words: last EOF");
}

static void test_word_position(void) {
  ArkshTokenStream s = lex_ok("  hello");
  EXPECT(s.count >= 2, "word position: at least 2 tokens");
  EXPECT(s.tokens[0].kind == ARKSH_TOKEN_WORD, "word position: first is WORD");
  EXPECT(s.tokens[0].position == 2, "word position: position == 2");
}

static void test_quoted_string(void) {
  /* Quoted strings are WORD tokens: text has quotes stripped, raw keeps them. */
  ArkshTokenStream s = lex_ok("\"hello world\"");
  EXPECT(s.count == 2, "quoted string: count == 2");
  EXPECT(s.tokens[0].kind == ARKSH_TOKEN_WORD, "quoted string: kind WORD");
  EXPECT(strcmp(s.tokens[0].text, "hello world") == 0, "quoted string: text stripped");
  EXPECT(strcmp(s.tokens[0].raw, "\"hello world\"") == 0, "quoted string: raw preserved");
}

static void test_single_quoted_string(void) {
  ArkshTokenStream s = lex_ok("'hello world'");
  EXPECT(s.count == 2, "single-quoted string: count == 2");
  EXPECT(s.tokens[0].kind == ARKSH_TOKEN_WORD, "single-quoted string: kind WORD");
  EXPECT(strcmp(s.tokens[0].text, "hello world") == 0, "single-quoted string: text");
}

static void test_arrow(void) {
  ArkshTokenStream s = lex_ok(". -> type");
  /* tokens: WORD('.'), ARROW, WORD('type'), EOF */
  EXPECT(s.count == 4, "arrow: count == 4");
  EXPECT(s.tokens[0].kind == ARKSH_TOKEN_WORD, "arrow: first WORD");
  EXPECT(s.tokens[1].kind == ARKSH_TOKEN_ARROW, "arrow: ARROW token");
  EXPECT(strcmp(s.tokens[1].text, "->") == 0, "arrow: text is '->'");
  EXPECT(s.tokens[2].kind == ARKSH_TOKEN_WORD, "arrow: third WORD");
}

static void test_object_pipe(void) {
  ArkshTokenStream s = lex_ok(". |> count()");
  int found = 0;
  size_t i;
  for (i = 0; i < s.count; i++) {
    if (s.tokens[i].kind == ARKSH_TOKEN_OBJECT_PIPE) {
      found = 1;
      EXPECT(strcmp(s.tokens[i].text, "|>") == 0, "object pipe: text is '|>'");
      break;
    }
  }
  EXPECT(found, "object pipe: OBJECT_PIPE present");
}

static void test_shell_pipe(void) {
  ArkshTokenStream s = lex_ok("ls | cat");
  int found = 0;
  size_t i;
  for (i = 0; i < s.count; i++) {
    if (s.tokens[i].kind == ARKSH_TOKEN_SHELL_PIPE) {
      found = 1;
      EXPECT(strcmp(s.tokens[i].text, "|") == 0, "shell pipe: text is '|'");
      break;
    }
  }
  EXPECT(found, "shell pipe: SHELL_PIPE present");
}

static void test_redirect_in(void) {
  ArkshTokenStream s = lex_ok("cat < file.txt");
  int found = 0;
  size_t i;
  for (i = 0; i < s.count; i++) {
    if (s.tokens[i].kind == ARKSH_TOKEN_REDIRECT_IN) {
      found = 1;
      break;
    }
  }
  EXPECT(found, "redirect in: REDIRECT_IN present");
}

static void test_redirect_out(void) {
  ArkshTokenStream s = lex_ok("echo hi > out.txt");
  int found = 0;
  size_t i;
  for (i = 0; i < s.count; i++) {
    if (s.tokens[i].kind == ARKSH_TOKEN_REDIRECT_OUT) {
      found = 1;
      break;
    }
  }
  EXPECT(found, "redirect out: REDIRECT_OUT present");
}

static void test_redirect_append(void) {
  ArkshTokenStream s = lex_ok("echo hi >> out.txt");
  int found = 0;
  size_t i;
  for (i = 0; i < s.count; i++) {
    if (s.tokens[i].kind == ARKSH_TOKEN_REDIRECT_APPEND) {
      found = 1;
      break;
    }
  }
  EXPECT(found, "redirect append: REDIRECT_APPEND present");
}

static void test_heredoc(void) {
  ArkshTokenStream s = lex_ok("cat <<EOF");
  int found = 0;
  size_t i;
  for (i = 0; i < s.count; i++) {
    if (s.tokens[i].kind == ARKSH_TOKEN_HEREDOC) {
      found = 1;
      break;
    }
  }
  EXPECT(found, "heredoc: HEREDOC present");
}

static void test_heredoc_strip(void) {
  ArkshTokenStream s = lex_ok("cat <<-EOF");
  int found = 0;
  size_t i;
  for (i = 0; i < s.count; i++) {
    if (s.tokens[i].kind == ARKSH_TOKEN_HEREDOC_STRIP) {
      found = 1;
      break;
    }
  }
  EXPECT(found, "heredoc strip: HEREDOC_STRIP present");
}

static void test_here_string(void) {
  ArkshTokenStream s = lex_ok("cat <<< \"hello\"");
  int found = 0;
  size_t i;
  for (i = 0; i < s.count; i++) {
    if (s.tokens[i].kind == ARKSH_TOKEN_HERE_STRING) {
      found = 1;
      EXPECT(strcmp(s.tokens[i].text, "<<<") == 0, "here-string: text is '<<<'");
      break;
    }
  }
  EXPECT(found, "here-string: HERE_STRING present");
}

static void test_redirect_error(void) {
  /* "2>" is handled by match_fd_redirection_token, which produces
   * REDIRECT_FD_OUT with fd==2 — the same effect as stderr redirect. */
  ArkshTokenStream s = lex_ok("cmd 2> err.txt");
  int found = 0;
  size_t i;
  for (i = 0; i < s.count; i++) {
    if (s.tokens[i].kind == ARKSH_TOKEN_REDIRECT_FD_OUT) {
      found = 1;
      break;
    }
  }
  EXPECT(found, "redirect error (fd): REDIRECT_FD_OUT present for '2>'");
}

static void test_redirect_error_append(void) {
  /* "2>>" → REDIRECT_FD_APPEND via match_fd_redirection_token */
  ArkshTokenStream s = lex_ok("cmd 2>> err.txt");
  int found = 0;
  size_t i;
  for (i = 0; i < s.count; i++) {
    if (s.tokens[i].kind == ARKSH_TOKEN_REDIRECT_FD_APPEND) {
      found = 1;
      break;
    }
  }
  EXPECT(found, "redirect error append (fd): REDIRECT_FD_APPEND present for '2>>'");
}

static void test_redirect_error_to_output(void) {
  /* "2>&" → REDIRECT_DUP_OUT via match_fd_redirection_token */
  ArkshTokenStream s = lex_ok("cmd 2>&1");
  int found = 0;
  size_t i;
  for (i = 0; i < s.count; i++) {
    if (s.tokens[i].kind == ARKSH_TOKEN_REDIRECT_DUP_OUT) {
      found = 1;
      break;
    }
  }
  EXPECT(found, "redirect error to output (dup): REDIRECT_DUP_OUT present for '2>&'");
}

static void test_redirect_fd_out(void) {
  ArkshTokenStream s = lex_ok("cmd 3> fd3.out");
  int found = 0;
  size_t i;
  for (i = 0; i < s.count; i++) {
    if (s.tokens[i].kind == ARKSH_TOKEN_REDIRECT_FD_OUT) {
      found = 1;
      break;
    }
  }
  EXPECT(found, "redirect fd out: REDIRECT_FD_OUT present");
}

static void test_redirect_fd_in(void) {
  ArkshTokenStream s = lex_ok("cmd 3< input.txt");
  int found = 0;
  size_t i;
  for (i = 0; i < s.count; i++) {
    if (s.tokens[i].kind == ARKSH_TOKEN_REDIRECT_FD_IN) {
      found = 1;
      break;
    }
  }
  EXPECT(found, "redirect fd in: REDIRECT_FD_IN present");
}

static void test_redirect_fd_append(void) {
  ArkshTokenStream s = lex_ok("cmd 3>> fd3.out");
  int found = 0;
  size_t i;
  for (i = 0; i < s.count; i++) {
    if (s.tokens[i].kind == ARKSH_TOKEN_REDIRECT_FD_APPEND) {
      found = 1;
      break;
    }
  }
  EXPECT(found, "redirect fd append: REDIRECT_FD_APPEND present");
}

static void test_redirect_dup_out(void) {
  ArkshTokenStream s = lex_ok("cmd 1>&3");
  int found = 0;
  size_t i;
  for (i = 0; i < s.count; i++) {
    if (s.tokens[i].kind == ARKSH_TOKEN_REDIRECT_DUP_OUT) {
      found = 1;
      break;
    }
  }
  EXPECT(found, "redirect dup out: REDIRECT_DUP_OUT present");
}

static void test_redirect_dup_in(void) {
  ArkshTokenStream s = lex_ok("cmd 0<&3");
  int found = 0;
  size_t i;
  for (i = 0; i < s.count; i++) {
    if (s.tokens[i].kind == ARKSH_TOKEN_REDIRECT_DUP_IN) {
      found = 1;
      break;
    }
  }
  EXPECT(found, "redirect dup in: REDIRECT_DUP_IN present");
}

static void test_and_if(void) {
  ArkshTokenStream s = lex_ok("true && false");
  int found = 0;
  size_t i;
  for (i = 0; i < s.count; i++) {
    if (s.tokens[i].kind == ARKSH_TOKEN_AND_IF) {
      found = 1;
      EXPECT(strcmp(s.tokens[i].text, "&&") == 0, "and_if: text is '&&'");
      break;
    }
  }
  EXPECT(found, "and_if: AND_IF present");
}

static void test_or_if(void) {
  ArkshTokenStream s = lex_ok("false || true");
  int found = 0;
  size_t i;
  for (i = 0; i < s.count; i++) {
    if (s.tokens[i].kind == ARKSH_TOKEN_OR_IF) {
      found = 1;
      EXPECT(strcmp(s.tokens[i].text, "||") == 0, "or_if: text is '||'");
      break;
    }
  }
  EXPECT(found, "or_if: OR_IF present");
}

static void test_sequence(void) {
  ArkshTokenStream s = lex_ok("true ; false");
  int found = 0;
  size_t i;
  for (i = 0; i < s.count; i++) {
    if (s.tokens[i].kind == ARKSH_TOKEN_SEQUENCE) {
      found = 1;
      EXPECT(strcmp(s.tokens[i].text, ";") == 0, "sequence: text is ';'");
      break;
    }
  }
  EXPECT(found, "sequence: SEQUENCE present");
}

static void test_background(void) {
  ArkshTokenStream s = lex_ok("sleep 1 &");
  int found = 0;
  size_t i;
  for (i = 0; i < s.count; i++) {
    if (s.tokens[i].kind == ARKSH_TOKEN_BACKGROUND) {
      found = 1;
      EXPECT(strcmp(s.tokens[i].text, "&") == 0, "background: text is '&'");
      break;
    }
  }
  EXPECT(found, "background: BACKGROUND present");
}

static void test_parens(void) {
  ArkshTokenStream s = lex_ok("( true )");
  int found_l = 0, found_r = 0;
  size_t i;
  for (i = 0; i < s.count; i++) {
    if (s.tokens[i].kind == ARKSH_TOKEN_LPAREN) found_l = 1;
    if (s.tokens[i].kind == ARKSH_TOKEN_RPAREN) found_r = 1;
  }
  EXPECT(found_l, "parens: LPAREN present");
  EXPECT(found_r, "parens: RPAREN present");
}

static void test_comma(void) {
  ArkshTokenStream s = lex_ok("list(1, 2)");
  int found = 0;
  size_t i;
  for (i = 0; i < s.count; i++) {
    if (s.tokens[i].kind == ARKSH_TOKEN_COMMA) {
      found = 1;
      EXPECT(strcmp(s.tokens[i].text, ",") == 0, "comma: text is ','");
      break;
    }
  }
  EXPECT(found, "comma: COMMA present");
}

static void test_eof_always_appended(void) {
  /* Even a multi-token line must end with EOF */
  ArkshTokenStream s = lex_ok("a b c");
  EXPECT(s.count >= 1, "eof always: at least 1 token");
  EXPECT(s.tokens[s.count - 1].kind == ARKSH_TOKEN_EOF, "eof always: last token is EOF");
}

static void test_token_kind_name(void) {
  /* arksh_token_kind_name() must not return NULL for any defined kind */
  ArkshTokenKind kinds[] = {
    ARKSH_TOKEN_INVALID, ARKSH_TOKEN_EOF, ARKSH_TOKEN_WORD, ARKSH_TOKEN_STRING,
    ARKSH_TOKEN_ARROW, ARKSH_TOKEN_OBJECT_PIPE, ARKSH_TOKEN_SHELL_PIPE,
    ARKSH_TOKEN_REDIRECT_IN, ARKSH_TOKEN_REDIRECT_OUT, ARKSH_TOKEN_REDIRECT_APPEND,
    ARKSH_TOKEN_HERE_STRING,
    ARKSH_TOKEN_HEREDOC, ARKSH_TOKEN_HEREDOC_STRIP,
    ARKSH_TOKEN_REDIRECT_ERROR, ARKSH_TOKEN_REDIRECT_ERROR_APPEND,
    ARKSH_TOKEN_REDIRECT_ERROR_TO_OUTPUT,
    ARKSH_TOKEN_REDIRECT_FD_IN, ARKSH_TOKEN_REDIRECT_FD_OUT,
    ARKSH_TOKEN_REDIRECT_FD_APPEND, ARKSH_TOKEN_REDIRECT_DUP_IN,
    ARKSH_TOKEN_REDIRECT_DUP_OUT,
    ARKSH_TOKEN_AND_IF, ARKSH_TOKEN_OR_IF, ARKSH_TOKEN_SEQUENCE,
    ARKSH_TOKEN_BACKGROUND, ARKSH_TOKEN_LPAREN, ARKSH_TOKEN_RPAREN, ARKSH_TOKEN_COMMA
  };
  size_t n = sizeof(kinds) / sizeof(kinds[0]);
  size_t i;
  for (i = 0; i < n; i++) {
    const char *name = arksh_token_kind_name(kinds[i]);
    EXPECT(name != NULL, "token_kind_name: not NULL");
    EXPECT(name[0] != '\0', "token_kind_name: not empty string");
  }
}

/* ------------------------------------------------------------------ main */

int main(void) {
  test_empty_line();
  test_blank_line();
  test_single_word();
  test_two_words();
  test_word_position();
  test_quoted_string();
  test_single_quoted_string();
  test_arrow();
  test_object_pipe();
  test_shell_pipe();
  test_redirect_in();
  test_redirect_out();
  test_redirect_append();
  test_here_string();
  test_heredoc();
  test_heredoc_strip();
  test_redirect_error();
  test_redirect_error_append();
  test_redirect_error_to_output();
  test_redirect_fd_out();
  test_redirect_fd_in();
  test_redirect_fd_append();
  test_redirect_dup_out();
  test_redirect_dup_in();
  test_and_if();
  test_or_if();
  test_sequence();
  test_background();
  test_parens();
  test_comma();
  test_eof_always_appended();
  test_token_kind_name();

  if (g_failures > 0) {
    fprintf(stderr, "%d test(s) FAILED\n", g_failures);
    return 1;
  }

  printf("unit_lexer: all tests passed\n");
  return 0;
}
