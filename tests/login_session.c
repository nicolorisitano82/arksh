#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int parse_metrics(const char *text, long *pid, long *pgid, long *sid, char *login_mode, size_t login_mode_size) {
  const char *cursor;

  if (text == NULL || pid == NULL || pgid == NULL || sid == NULL || login_mode == NULL || login_mode_size == 0) {
    return 0;
  }

  for (cursor = text; *cursor != '\0'; ++cursor) {
    long local_pid;
    long local_pgid;
    long local_sid;
    char local_login[16];

    if (sscanf(cursor, "%ld|%ld|%ld|%15s", &local_pid, &local_pgid, &local_sid, local_login) == 4) {
      *pid = local_pid;
      *pgid = local_pgid;
      *sid = local_sid;
      snprintf(login_mode, login_mode_size, "%s", local_login);
      return 1;
    }
  }

  return 0;
}

int main(int argc, char **argv) {
  static const char *kCommand =
    "text(\"%v|%v|%v|%v\") -> print(proc() -> pid, proc() -> pgid, proc() -> sid, shell() -> login_mode)";
  int pipe_fds[2];
  pid_t pid;
  int status = 0;
  char output[4096];
  ssize_t total = 0;
  long shell_pid = 0;
  long shell_pgid = 0;
  long shell_sid = 0;
  char login_mode[16];

  if (argc < 2) {
    fprintf(stderr, "usage: %s /path/to/arksh\n", argv[0]);
    return 1;
  }

  if (pipe(pipe_fds) != 0) {
    perror("pipe");
    return 1;
  }

  pid = fork();
  if (pid < 0) {
    perror("fork");
    close(pipe_fds[0]);
    close(pipe_fds[1]);
    return 1;
  }

  if (pid == 0) {
    char *child_argv[] = { argv[1], "--login", "-c", (char *) kCommand, NULL };

    close(pipe_fds[0]);
    dup2(pipe_fds[1], STDOUT_FILENO);
    close(pipe_fds[1]);
    execv(argv[1], child_argv);
    _exit(127);
  }

  close(pipe_fds[1]);
  while (total + 1 < (ssize_t) sizeof(output)) {
    ssize_t n = read(pipe_fds[0], output + total, sizeof(output) - (size_t) total - 1);

    if (n <= 0) {
      break;
    }
    total += n;
  }
  close(pipe_fds[0]);
  output[total] = '\0';

  waitpid(pid, &status, 0);
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    fprintf(stderr, "arksh child failed: %s\n", output);
    return 1;
  }

  login_mode[0] = '\0';
  if (!parse_metrics(output, &shell_pid, &shell_pgid, &shell_sid, login_mode, sizeof(login_mode))) {
    fprintf(stderr, "unable to parse login metrics: %s\n", output);
    return 1;
  }

  if (shell_pid != shell_pgid || shell_pid != shell_sid || strcmp(login_mode, "true") != 0) {
    fprintf(stderr, "unexpected login session metrics: %s\n", output);
    return 1;
  }

  puts("login_session: all tests passed");
  return 0;
}
