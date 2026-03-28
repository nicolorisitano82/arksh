/* E14-S1-T5: unit tests for sh-compatibility mode.
 *
 * Exercises arksh --sh flag, argv[0]="sh" detection, rejection of non-POSIX
 * syntax and acceptance of valid POSIX constructs.
 *
 * Standalone executable — returns 0 on success, 1 on any failure.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arksh/shell.h"

/* ------------------------------------------------------------------ helpers */

static int g_failures = 0;

#define EXPECT(cond, msg)                                              \
  do {                                                                 \
    if (!(cond)) {                                                     \
      fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, (msg)); \
      g_failures++;                                                    \
    }                                                                  \
  } while (0)

/* ArkshShell is too large for the stack — always heap-allocate. */
static ArkshShell *g_shell;

static void trim_nl(char *s) {
  size_t n = strlen(s);
  if (n > 0 && s[n - 1] == '\n') s[n - 1] = '\0';
}

static int run(const char *line, char *out, size_t out_size) {
  int status;
  out[0] = '\0';
  status = arksh_shell_execute_line(g_shell, line, out, out_size);
  trim_nl(out);
  return status;
}

/* ------------------------------------------------------------------ T1: sh_mode flag */

static void test_sh_mode_set(void) {
  EXPECT(g_shell->sh_mode == 1, "sh_mode: flag is set after init with sh_mode=1");
}

/* ------------------------------------------------------------------ T2: rejected syntax */

static void test_reject_arrow(void) {
  char out[ARKSH_MAX_OUTPUT];
  int status = run("echo hello -> upper", out, sizeof(out));
  EXPECT(status != 0, "sh_mode: -> rejected");
  EXPECT(strstr(out, "not supported in sh mode") != NULL, "sh_mode: -> error message");
}

static void test_reject_object_pipe(void) {
  char out[ARKSH_MAX_OUTPUT];
  int status = run("ls |> count()", out, sizeof(out));
  EXPECT(status != 0, "sh_mode: |> rejected");
  EXPECT(strstr(out, "not supported in sh mode") != NULL, "sh_mode: |> error message");
}

static void test_reject_here_string(void) {
  char out[ARKSH_MAX_OUTPUT];
  int status = run("cat <<< hello", out, sizeof(out));
  EXPECT(status != 0, "sh_mode: <<< rejected");
  EXPECT(strstr(out, "not supported in sh mode") != NULL, "sh_mode: <<< error message");
}

static void test_reject_proc_subst_in(void) {
  char out[ARKSH_MAX_OUTPUT];
  int status = run("cat <(echo foo)", out, sizeof(out));
  EXPECT(status != 0, "sh_mode: <(...) rejected");
  EXPECT(strstr(out, "not supported in sh mode") != NULL, "sh_mode: <(...) error message");
}

static void test_reject_proc_subst_out(void) {
  char out[ARKSH_MAX_OUTPUT];
  int status = run("echo foo >(cat)", out, sizeof(out));
  EXPECT(status != 0, "sh_mode: >(...) rejected");
  EXPECT(strstr(out, "not supported in sh mode") != NULL, "sh_mode: >(...) error message");
}

static void test_reject_double_bracket(void) {
  char out[ARKSH_MAX_OUTPUT];
  int status = run("[[ -n hello ]]", out, sizeof(out));
  EXPECT(status != 0, "sh_mode: [[ rejected");
  EXPECT(strstr(out, "not supported in sh mode") != NULL, "sh_mode: [[ error message");
}

static void test_reject_let(void) {
  char out[ARKSH_MAX_OUTPUT];
  int status = run("let x = 1", out, sizeof(out));
  EXPECT(status != 0, "sh_mode: let rejected");
  EXPECT(strstr(out, "not supported in sh mode") != NULL, "sh_mode: let error message");
}

static void test_reject_extend(void) {
  char out[ARKSH_MAX_OUTPUT];
  int status = run("extend MyObj", out, sizeof(out));
  EXPECT(status != 0, "sh_mode: extend rejected");
  EXPECT(strstr(out, "not supported in sh mode") != NULL, "sh_mode: extend error message");
}

static void test_reject_switch(void) {
  char out[ARKSH_MAX_OUTPUT];
  int status = run("switch foo ; case foo ; then echo ok ; endswitch", out, sizeof(out));
  EXPECT(status != 0, "sh_mode: switch rejected");
  EXPECT(strstr(out, "not supported in sh mode") != NULL, "sh_mode: switch error message");
}

static void test_reject_class(void) {
  char out[ARKSH_MAX_OUTPUT];
  int status = run("class Foo ; endclass", out, sizeof(out));
  EXPECT(status != 0, "sh_mode: class rejected");
  EXPECT(strstr(out, "not supported in sh mode") != NULL, "sh_mode: class error message");
}

static void test_reject_block_literal(void) {
  char out[ARKSH_MAX_OUTPUT];
  int status = run("echo [: hello]", out, sizeof(out));
  EXPECT(status != 0, "sh_mode: block literal rejected");
  EXPECT(strstr(out, "not supported in sh mode") != NULL, "sh_mode: block literal error message");
}

/* ------------------------------------------------------------------ T2: accepted POSIX syntax */

static void test_allow_echo(void) {
  char out[ARKSH_MAX_OUTPUT];
  int status = run("echo hello", out, sizeof(out));
  EXPECT(status == 0, "sh_mode: echo accepted");
  EXPECT(strcmp(out, "hello") == 0, "sh_mode: echo output");
}

static void test_allow_assignment(void) {
  char out[ARKSH_MAX_OUTPUT];
  int status = run("SH_TEST=posix", out, sizeof(out));
  EXPECT(status == 0, "sh_mode: VAR=val accepted");
  EXPECT(strcmp(arksh_shell_get_var(g_shell, "SH_TEST"), "posix") == 0, "sh_mode: variable set");
}

static void test_allow_if(void) {
  char out[ARKSH_MAX_OUTPUT];
  int status = run("if true ; then echo yes ; fi", out, sizeof(out));
  EXPECT(status == 0, "sh_mode: if/then/fi accepted");
  EXPECT(strcmp(out, "yes") == 0, "sh_mode: if output");
}

static void test_allow_for(void) {
  char out[ARKSH_MAX_OUTPUT];
  int status = run("for x in a b ; do echo $x ; done", out, sizeof(out));
  EXPECT(status == 0, "sh_mode: for/do/done accepted");
}

static void test_allow_while(void) {
  char out[ARKSH_MAX_OUTPUT];
  int status = run("while false ; do echo no ; done", out, sizeof(out));
  EXPECT(status == 0, "sh_mode: while accepted");
}

static void test_allow_case(void) {
  char out[ARKSH_MAX_OUTPUT];
  int status = run("case foo in foo) echo match ;; esac", out, sizeof(out));
  EXPECT(status == 0, "sh_mode: case/esac accepted");
  EXPECT(strcmp(out, "match") == 0, "sh_mode: case output");
}

static void test_allow_function(void) {
  char out[ARKSH_MAX_OUTPUT];
  int status = run("greet_sh() { echo hi ; }", out, sizeof(out));
  EXPECT(status == 0, "sh_mode: POSIX function definition accepted");
  status = run("greet_sh", out, sizeof(out));
  EXPECT(status == 0, "sh_mode: POSIX function call accepted");
  EXPECT(strcmp(out, "hi") == 0, "sh_mode: function output");
}

static void test_allow_pipe_syntax(void) {
  /* Verify that | is not rejected by the sh-mode syntax filter.
   * We check the error message is NOT a sh-mode rejection; pipeline
   * execution correctness is covered by integration/golden tests. */
  char out[ARKSH_MAX_OUTPUT];
  run("true | true", out, sizeof(out));
  EXPECT(strstr(out, "not supported in sh mode") == NULL, "sh_mode: | not forbidden in sh mode");
}

static void test_allow_command_substitution(void) {
  char out[ARKSH_MAX_OUTPUT];
  int status = run("x=$(echo sub) ; echo $x", out, sizeof(out));
  EXPECT(status == 0, "sh_mode: $() accepted");
}

static void test_allow_arithmetic(void) {
  char out[ARKSH_MAX_OUTPUT];
  int status = run("echo $((2 + 3))", out, sizeof(out));
  EXPECT(status == 0, "sh_mode: $(( )) accepted");
  EXPECT(strcmp(out, "5") == 0, "sh_mode: arithmetic output");
}

static void test_allow_heredoc(void) {
  char out[ARKSH_MAX_OUTPUT];
  int status = run("cat <<EOF\nhello\nEOF", out, sizeof(out));
  EXPECT(status == 0, "sh_mode: heredoc accepted");
  EXPECT(strcmp(out, "hello") == 0, "sh_mode: heredoc output");
}

static void test_allow_test_bracket(void) {
  char out[ARKSH_MAX_OUTPUT];
  int status = run("[ -n hello ] && echo yes", out, sizeof(out));
  EXPECT(status == 0, "sh_mode: [ ] test accepted");
  EXPECT(strcmp(out, "yes") == 0, "sh_mode: [ ] output");
}

/* ------------------------------------------------------------------ T1: no-arksh-extensions policy */

static void test_no_plugins_loaded(void) {
  /* In sh_mode the plugin autoload is skipped; plugin_count reflects only
   * plugins explicitly loaded after init (none in this test). */
  EXPECT(g_shell->plugin_count == 0, "sh_mode: no plugins autoloaded");
}

int main(void) {
  g_shell = (ArkshShell *) calloc(1, sizeof(*g_shell));
  if (g_shell == NULL) {
    fprintf(stderr, "FAIL: out of memory\n");
    return 1;
  }
  if (arksh_shell_init_with_options(g_shell, "sh", 0, 1) != 0) {
    fprintf(stderr, "FAIL: arksh_shell_init_with_options(sh_mode=1) failed\n");
    free(g_shell);
    return 1;
  }

  /* T1: mode flag */
  test_sh_mode_set();
  test_no_plugins_loaded();

  /* T2: rejected non-POSIX syntax */
  test_reject_arrow();
  test_reject_object_pipe();
  test_reject_here_string();
  test_reject_proc_subst_in();
  test_reject_proc_subst_out();
  test_reject_double_bracket();
  test_reject_let();
  test_reject_extend();
  test_reject_switch();
  test_reject_class();
  test_reject_block_literal();

  /* T2: accepted POSIX syntax */
  test_allow_echo();
  test_allow_assignment();
  test_allow_if();
  test_allow_for();
  test_allow_while();
  test_allow_case();
  test_allow_function();
  test_allow_pipe_syntax();
  test_allow_command_substitution();
  test_allow_arithmetic();
  test_allow_heredoc();
  test_allow_test_bracket();

  arksh_shell_destroy(g_shell);
  free(g_shell);

  if (g_failures > 0) {
    fprintf(stderr, "%d test(s) FAILED\n", g_failures);
    return 1;
  }

  printf("unit_sh_mode: all tests passed\n");
  return 0;
}
