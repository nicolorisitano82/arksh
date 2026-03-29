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
  const char *program_path;
  ArkshShell *shell;
  char output[ARKSH_MAX_OUTPUT];
  char trap_output[ARKSH_MAX_OUTPUT];
  int trap_status = 0;
  int arg_index = 1;
  int login_mode = 0;
  int sh_mode = 0;
  int no_sudo_escalation = 0;
  int status;

  perf_env = getenv("ARKSH_PERF");
  arksh_perf_enable(perf_env != NULL && perf_env[0] != '\0' && strcmp(perf_env, "0") != 0);

  shell = (ArkshShell *) calloc(1, sizeof(*shell));
  if (shell == NULL) {
    fputs("failed to allocate shell\n", stderr);
    return 1;
  }

  program_path = (argc > 0 && argv[0] != NULL && argv[0][0] == '-') ? argv[0] + 1 : argv[0];
  if (argc > 0 && argv[0] != NULL && argv[0][0] == '-') {
    login_mode = 1;
  }

  /* Detect sh-compatibility mode from argv[0] basename. */
  if (argc > 0 && argv[0] != NULL) {
    const char *base = strrchr(argv[0], '/');
    base = (base != NULL) ? base + 1 : argv[0];
    /* Strip leading '-' (login shell indicator) before comparing. */
    if (base[0] == '-') {
      base++;
    }
    if (strcmp(base, "sh") == 0) {
      sh_mode = 1;
    }
  }

  while (arg_index < argc) {
    if (strcmp(argv[arg_index], "--login") == 0 || strcmp(argv[arg_index], "-l") == 0) {
      login_mode = 1;
      arg_index++;
      continue;
    }
    if (strcmp(argv[arg_index], "--sh") == 0) {
      sh_mode = 1;
      arg_index++;
      continue;
    }
    if (strcmp(argv[arg_index], "--no-sudo-escalation") == 0) {
      no_sudo_escalation = 1;
      arg_index++;
      continue;
    }
    break;
  }

  if (arksh_shell_init_with_options(shell, program_path, login_mode, sh_mode) != 0) {
    fputs("failed to initialize shell\n", stderr);
    free(shell);
    return 1;
  }

  shell->no_sudo_escalation = no_sudo_escalation;

  if (arg_index < argc && (strcmp(argv[arg_index], "--version") == 0 || strcmp(argv[arg_index], "-V") == 0)) {
    printf("arksh %s\n", ARKSH_VERSION);
    arksh_shell_destroy(shell);
    free(shell);
    return 0;
  }

  if (arg_index < argc && strcmp(argv[arg_index], "--help") == 0) {
    arksh_shell_print_help(shell, output, sizeof(output));
    print_output_if_any(output);
    arksh_shell_destroy(shell);
    free(shell);
    return 0;
  }

  if (arg_index < argc && strcmp(argv[arg_index], "-c") == 0) {
    if (arg_index + 1 >= argc) {
      fputs("usage: arksh -c '<command>'\n", stderr);
      arksh_shell_destroy(shell);
      free(shell);
      return 1;
    }

    output[0] = '\0';
    status = arksh_shell_execute_line(shell, argv[arg_index + 1], output, sizeof(output));
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
  if (arg_index < argc && argv[arg_index][0] != '-') {
    int positional_count = argc - arg_index - 1;
    if (positional_count > ARKSH_MAX_ARGS) {
      positional_count = ARKSH_MAX_ARGS;
    }
    output[0] = '\0';
    status = arksh_shell_source_file(shell, argv[arg_index],
               positional_count, argv + arg_index + 1,
               output, sizeof(output));
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
