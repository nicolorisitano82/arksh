#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arksh/perf.h"
#include "arksh/shell.h"

static void print_output_if_any(const char *text) {
  size_t len;

  if (text == NULL || text[0] == '\0') {
    return;
  }

  len = strlen(text);
  fputs(text, stdout);
  if (len == 0 || text[len - 1] != '\n') {
    fputc('\n', stdout);
  }
}

int main(int argc, char **argv) {
  const char *perf_env;
  ArkshShell *shell;
  char output[ARKSH_MAX_OUTPUT];
  char trap_output[ARKSH_MAX_OUTPUT];
  int trap_status = 0;
  int status;

  perf_env = getenv("ARKSH_PERF");
  arksh_perf_enable(perf_env != NULL && perf_env[0] != '\0' && strcmp(perf_env, "0") != 0);

  shell = (ArkshShell *) calloc(1, sizeof(*shell));
  if (shell == NULL) {
    fputs("failed to allocate shell\n", stderr);
    return 1;
  }

  if (arksh_shell_init(shell) != 0) {
    fputs("failed to initialize shell\n", stderr);
    free(shell);
    return 1;
  }

  arksh_shell_set_program_path(shell, argv[0]);

  if (argc > 1 && strcmp(argv[1], "--help") == 0) {
    arksh_shell_print_help(shell, output, sizeof(output));
    print_output_if_any(output);
    arksh_shell_destroy(shell);
    free(shell);
    return 0;
  }

  if (argc > 1 && strcmp(argv[1], "-c") == 0) {
    if (argc < 3) {
      fputs("usage: arksh -c '<command>'\n", stderr);
      arksh_shell_destroy(shell);
      free(shell);
      return 1;
    }

    output[0] = '\0';
    status = arksh_shell_execute_line(shell, argv[2], output, sizeof(output));
    print_output_if_any(output);
    trap_output[0] = '\0';
    trap_status = arksh_shell_run_exit_trap(shell, trap_output, sizeof(trap_output));
    print_output_if_any(trap_output);
    if (status == 0 && trap_status != 0) {
      status = trap_status;
    }
    arksh_shell_destroy(shell);
    free(shell);
    return status == 0 ? 0 : 1;
  }

  /* File argument: arksh script.arksh [args...] */
  if (argc > 1 && argv[1][0] != '-') {
    char source_cmd[ARKSH_MAX_PATH + 16];
    int i;

    snprintf(source_cmd, sizeof(source_cmd), "source \"%s\"", argv[1]);
    /* Expose positional parameters $1 $2 ... from remaining argv */
    for (i = 2; i < argc && i - 1 < ARKSH_MAX_ARGS; ++i) {
      char var[8];
      snprintf(var, sizeof(var), "%d", i - 1);
      arksh_shell_set_var(shell, var, argv[i], 0);
    }
    output[0] = '\0';
    status = arksh_shell_execute_line(shell, source_cmd, output, sizeof(output));
    print_output_if_any(output);
    trap_output[0] = '\0';
    trap_status = arksh_shell_run_exit_trap(shell, trap_output, sizeof(trap_output));
    print_output_if_any(trap_output);
    if (status == 0 && trap_status != 0) {
      status = trap_status;
    }
    arksh_shell_destroy(shell);
    free(shell);
    return status == 0 ? 0 : 1;
  }

  status = arksh_shell_run_repl(shell);
  trap_output[0] = '\0';
  trap_status = arksh_shell_run_exit_trap(shell, trap_output, sizeof(trap_output));
  print_output_if_any(trap_output);
  if (status == 0 && trap_status != 0) {
    status = trap_status;
  }
  arksh_shell_destroy(shell);
  free(shell);
  return status == 0 ? 0 : 1;
}
