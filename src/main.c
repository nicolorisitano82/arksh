#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "oosh/shell.h"

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
  OoshShell *shell;
  char output[OOSH_MAX_OUTPUT];
  char trap_output[OOSH_MAX_OUTPUT];
  int trap_status = 0;
  int status;

  shell = (OoshShell *) calloc(1, sizeof(*shell));
  if (shell == NULL) {
    fputs("failed to allocate shell\n", stderr);
    return 1;
  }

  if (oosh_shell_init(shell) != 0) {
    fputs("failed to initialize shell\n", stderr);
    free(shell);
    return 1;
  }

  oosh_shell_set_program_path(shell, argv[0]);

  if (argc > 1 && strcmp(argv[1], "--help") == 0) {
    oosh_shell_print_help(shell, output, sizeof(output));
    print_output_if_any(output);
    oosh_shell_destroy(shell);
    free(shell);
    return 0;
  }

  if (argc > 1 && strcmp(argv[1], "-c") == 0) {
    if (argc < 3) {
      fputs("usage: oosh -c '<command>'\n", stderr);
      oosh_shell_destroy(shell);
      free(shell);
      return 1;
    }

    output[0] = '\0';
    status = oosh_shell_execute_line(shell, argv[2], output, sizeof(output));
    print_output_if_any(output);
    trap_output[0] = '\0';
    trap_status = oosh_shell_run_exit_trap(shell, trap_output, sizeof(trap_output));
    print_output_if_any(trap_output);
    if (status == 0 && trap_status != 0) {
      status = trap_status;
    }
    oosh_shell_destroy(shell);
    free(shell);
    return status == 0 ? 0 : 1;
  }

  status = oosh_shell_run_repl(shell);
  trap_output[0] = '\0';
  trap_status = oosh_shell_run_exit_trap(shell, trap_output, sizeof(trap_output));
  print_output_if_any(trap_output);
  if (status == 0 && trap_status != 0) {
    status = trap_status;
  }
  oosh_shell_destroy(shell);
  free(shell);
  return status == 0 ? 0 : 1;
}
