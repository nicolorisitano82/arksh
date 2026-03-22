/* E8-S1-T3: unit tests for the arksh executor.
 *
 * Exercises arksh_execute_ast (via arksh_shell_execute_line),
 * arksh_evaluate_line_value and arksh_execute_block through a live shell
 * instance initialised at the start of main().
 *
 * Standalone executable — returns 0 on success, 1 on any failure.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arksh/executor.h"
#include "arksh/object.h"
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

/* Strip one trailing newline in-place. */
static void trim_nl(char *s) {
  size_t n = strlen(s);
  if (n > 0 && s[n - 1] == '\n') s[n - 1] = '\0';
}

/* Run one shell line; strip trailing newline and return pointer to output. */
static const char *run(const char *line, char *out, size_t out_size) {
  out[0] = '\0';
  arksh_shell_execute_line(g_shell, line, out, out_size);
  trim_nl(out);
  return out;
}

/* Evaluate a value expression; render the result into out[]. */
static const char *eval_value(const char *expr, ArkshValue *val, char *out, size_t out_size) {
  char rendered[ARKSH_MAX_OUTPUT];
  out[0] = '\0';
  rendered[0] = '\0';
  if (arksh_evaluate_line_value(g_shell, expr, val, out, out_size) == 0) {
    arksh_value_render(val, rendered, sizeof(rendered));
    strncpy(out, rendered, out_size - 1);
    out[out_size - 1] = '\0';
  }
  return out;
}

/* ------------------------------------------------------------------ simple command */

static void test_echo(void) {
  char out[ARKSH_MAX_OUTPUT];
  run("echo hello", out, sizeof(out));
  EXPECT(strcmp(out, "hello") == 0, "echo: output == 'hello'");
}

static void test_posix_assignment(void) {
  char out[ARKSH_MAX_OUTPUT];
  run("XTEST=value", out, sizeof(out));
  const char *v = arksh_shell_get_var(g_shell, "XTEST");
  EXPECT(v != NULL && strcmp(v, "value") == 0, "posix assign: VAR=value sets variable");
}

static void test_posix_assignment_multi(void) {
  char out[ARKSH_MAX_OUTPUT];
  run("A=foo B=bar", out, sizeof(out));
  const char *va = arksh_shell_get_var(g_shell, "A");
  const char *vb = arksh_shell_get_var(g_shell, "B");
  EXPECT(va != NULL && strcmp(va, "foo") == 0, "posix assign multi: A == 'foo'");
  EXPECT(vb != NULL && strcmp(vb, "bar") == 0, "posix assign multi: B == 'bar'");
}

static void test_set_builtin(void) {
  char out[ARKSH_MAX_OUTPUT];
  run("set myvar myvalue", out, sizeof(out));
  const char *v = arksh_shell_get_var(g_shell, "myvar");
  EXPECT(v != NULL && strcmp(v, "myvalue") == 0, "set builtin: variable is set");
}

static void test_variable_expansion(void) {
  char out[ARKSH_MAX_OUTPUT];
  run("set greeting arksh", out, sizeof(out));
  run("echo $greeting", out, sizeof(out));
  EXPECT(strcmp(out, "arksh") == 0, "echo $var: variable expands correctly");
}

static void test_empty_line(void) {
  char out[ARKSH_MAX_OUTPUT];
  int rc = arksh_shell_execute_line(g_shell, "", out, sizeof(out));
  EXPECT(rc == 0, "empty line: exit code 0");
}

static void test_blank_line(void) {
  char out[ARKSH_MAX_OUTPUT];
  int rc = arksh_shell_execute_line(g_shell, "   ", out, sizeof(out));
  EXPECT(rc == 0, "blank line: exit code 0");
}

/* ------------------------------------------------------------------ control flow */

static void test_if_true(void) {
  char out[ARKSH_MAX_OUTPUT];
  run("if true\nthen\necho yes\nfi", out, sizeof(out));
  EXPECT(strcmp(out, "yes") == 0, "if true: then branch taken");
}

static void test_if_false(void) {
  char out[ARKSH_MAX_OUTPUT];
  run("if false\nthen\necho yes\nelse\necho no\nfi", out, sizeof(out));
  EXPECT(strcmp(out, "no") == 0, "if false: else branch taken");
}

static void test_if_test_numeric(void) {
  char out[ARKSH_MAX_OUTPUT];
  run("if [ 5 -gt 3 ]\nthen\necho big\nfi", out, sizeof(out));
  EXPECT(strcmp(out, "big") == 0, "if [ 5 -gt 3 ]: numeric test");
}

static void test_for_loop(void) {
  char out[ARKSH_MAX_OUTPUT];
  run("for item in a b c; do; echo $item; done", out, sizeof(out));
  /* At least one item should appear in the output */
  EXPECT(strstr(out, "a") != NULL, "for loop: iterates items");
}

/* ------------------------------------------------------------------ case */

static void test_case_exact(void) {
  char out[ARKSH_MAX_OUTPUT];
  run("case hello in\nhello) echo matched;;\nesac", out, sizeof(out));
  EXPECT(strcmp(out, "matched") == 0, "case: exact match");
}

static void test_case_wildcard(void) {
  char out[ARKSH_MAX_OUTPUT];
  run("case anything in\nfoo) echo no;;\n*) echo yes;;\nesac", out, sizeof(out));
  EXPECT(strcmp(out, "yes") == 0, "case: wildcard fallthrough");
}

static void test_case_lparen_pattern(void) {
  char out[ARKSH_MAX_OUTPUT];
  run("case hi in\n(hi) echo ok;;\nesac", out, sizeof(out));
  EXPECT(strcmp(out, "ok") == 0, "case: (pattern) form");
}

/* ------------------------------------------------------------------ functions */

static void test_posix_function_basic(void) {
  char out[ARKSH_MAX_OUTPUT];
  run("greetfn() { echo greet; }", out, sizeof(out));
  run("greetfn", out, sizeof(out));
  EXPECT(strcmp(out, "greet") == 0, "POSIX function: defined and called");
}

static void test_posix_function_positional(void) {
  char out[ARKSH_MAX_OUTPUT];
  run("showargs() { echo $1 $2; }", out, sizeof(out));
  run("showargs alpha beta", out, sizeof(out));
  EXPECT(strcmp(out, "alpha beta") == 0, "POSIX function: positional args $1 $2");
}

static void test_posix_function_redefine(void) {
  char out[ARKSH_MAX_OUTPUT];
  run("myfn() { echo first; }", out, sizeof(out));
  run("myfn() { echo second; }", out, sizeof(out));
  run("myfn", out, sizeof(out));
  EXPECT(strcmp(out, "second") == 0, "POSIX function: redefinition works");
}

/* ------------------------------------------------------------------ value evaluation */

static void test_eval_string(void) {
  char out[ARKSH_MAX_OUTPUT];
  ArkshValue val;
  arksh_value_init(&val);
  eval_value("text(\"hello\")", &val, out, sizeof(out));
  EXPECT(val.kind == ARKSH_VALUE_STRING, "eval string: kind == STRING");
  EXPECT(strcmp(val.text, "hello") == 0, "eval string: text == 'hello'");
  arksh_value_free(&val);
}

static void test_eval_number(void) {
  char out[ARKSH_MAX_OUTPUT];
  ArkshValue val;
  arksh_value_init(&val);
  eval_value("number(7)", &val, out, sizeof(out));
  EXPECT(val.kind == ARKSH_VALUE_NUMBER, "eval number: kind == NUMBER");
  EXPECT(val.number == 7.0, "eval number: number == 7");
  arksh_value_free(&val);
}

static void test_eval_boolean_true(void) {
  char out[ARKSH_MAX_OUTPUT];
  ArkshValue val;
  arksh_value_init(&val);
  eval_value("true", &val, out, sizeof(out));
  EXPECT(val.kind == ARKSH_VALUE_BOOLEAN, "eval true: kind == BOOLEAN");
  EXPECT(val.boolean != 0, "eval true: boolean is non-zero");
  arksh_value_free(&val);
}

static void test_eval_boolean_false(void) {
  char out[ARKSH_MAX_OUTPUT];
  ArkshValue val;
  arksh_value_init(&val);
  eval_value("false", &val, out, sizeof(out));
  EXPECT(val.kind == ARKSH_VALUE_BOOLEAN, "eval false: kind == BOOLEAN");
  EXPECT(val.boolean == 0, "eval false: boolean is zero");
  arksh_value_free(&val);
}

/* ------------------------------------------------------------------ block execution */

static void test_block_no_params(void) {
  ArkshBlock block;
  ArkshValue result;
  char out[ARKSH_MAX_OUTPUT];

  memset(&block, 0, sizeof(block));
  arksh_value_init(&result);
  block.param_count = 0;
  strncpy(block.body, "text(\"blockout\")", sizeof(block.body) - 1);
  strncpy(block.source, "[| text(\"blockout\")]", sizeof(block.source) - 1);

  int rc = arksh_execute_block(g_shell, &block, NULL, 0, &result, out, sizeof(out));
  EXPECT(rc == 0, "block no params: no error");
  EXPECT(result.kind == ARKSH_VALUE_STRING, "block no params: result is STRING");
  EXPECT(strcmp(result.text, "blockout") == 0, "block no params: result == 'blockout'");
  arksh_value_free(&result);
}

static void test_block_with_param(void) {
  ArkshBlock block;
  ArkshValue result;
  ArkshValue args[1];
  char out[ARKSH_MAX_OUTPUT];

  memset(&block, 0, sizeof(block));
  arksh_value_init(&result);
  arksh_value_init(&args[0]);
  /* Block params are bound as value bindings, accessed by name (not $name). */
  block.param_count = 1;
  strncpy(block.params[0], "arg", sizeof(block.params[0]) - 1);
  strncpy(block.body, "arg", sizeof(block.body) - 1);
  strncpy(block.source, "[:arg | arg]", sizeof(block.source) - 1);

  arksh_value_set_string(&args[0], "passed");
  int rc = arksh_execute_block(g_shell, &block, args, 1, &result, out, sizeof(out));
  EXPECT(rc == 0, "block with param: no error");
  EXPECT(result.kind == ARKSH_VALUE_STRING, "block with param: result is STRING");
  EXPECT(strcmp(result.text, "passed") == 0, "block with param: result == 'passed'");
  arksh_value_free(&result);
  arksh_value_free(&args[0]);
}

static void test_dynamic_class_member_tables(void) {
  ArkshClassCommandNode node;
  const ArkshClassDef *class_def;
  char body[ARKSH_MAX_LINE];
  char out[ARKSH_MAX_OUTPUT];
  size_t used = 0;
  int i;

  memset(&node, 0, sizeof(node));
  memset(body, 0, sizeof(body));
  strcpy(node.name, "DynamicCapacity");
  strcpy(node.source, "class DynamicCapacity do ... endclass");

  for (i = 1; i <= 33; ++i) {
    int written = snprintf(body + used, sizeof(body) - used, "property p%02d = %d\n", i, i);
    if (written < 0 || (size_t) written >= sizeof(body) - used) {
      EXPECT(0, "dynamic class tables: property body fits");
      return;
    }
    used += (size_t) written;
  }
  for (i = 1; i <= 33; ++i) {
    int written = snprintf(body + used, sizeof(body) - used, "method m%02d = [:self|%d]\n", i, i);
    if (written < 0 || (size_t) written >= sizeof(body) - used) {
      EXPECT(0, "dynamic class tables: method body fits");
      return;
    }
    used += (size_t) written;
  }

  strncpy(node.body, body, sizeof(node.body) - 1);
  out[0] = '\0';
  EXPECT(arksh_shell_set_class(g_shell, &node, out, sizeof(out)) == 0, "dynamic class tables: class definition accepted");

  class_def = arksh_shell_find_class(g_shell, "DynamicCapacity");
  EXPECT(class_def != NULL, "dynamic class tables: class registered");
  if (class_def != NULL) {
    EXPECT(class_def->property_count == 33, "dynamic class tables: property_count == 33");
    EXPECT(class_def->method_count == 33, "dynamic class tables: method_count == 33");
  }
}

/* ------------------------------------------------------------------ main */

int main(void) {
  g_shell = (ArkshShell *) calloc(1, sizeof(*g_shell));
  if (g_shell == NULL) {
    fprintf(stderr, "FAIL: out of memory\n");
    return 1;
  }
  if (arksh_shell_init(g_shell) != 0) {
    fprintf(stderr, "FAIL: arksh_shell_init failed\n");
    free(g_shell);
    return 1;
  }

  /* simple commands */
  test_echo();
  test_posix_assignment();
  test_posix_assignment_multi();
  test_set_builtin();
  test_variable_expansion();
  test_empty_line();
  test_blank_line();

  /* control flow */
  test_if_true();
  test_if_false();
  test_if_test_numeric();
  test_for_loop();

  /* case */
  test_case_exact();
  test_case_wildcard();
  test_case_lparen_pattern();

  /* functions */
  test_posix_function_basic();
  test_posix_function_positional();
  test_posix_function_redefine();

  /* value evaluation */
  test_eval_string();
  test_eval_number();
  test_eval_boolean_true();
  test_eval_boolean_false();

  /* block execution */
  test_block_no_params();
  test_block_with_param();
  test_dynamic_class_member_tables();

  arksh_shell_destroy(g_shell);
  free(g_shell);

  if (g_failures > 0) {
    fprintf(stderr, "%d test(s) FAILED\n", g_failures);
    return 1;
  }

  printf("unit_executor: all tests passed\n");
  return 0;
}
