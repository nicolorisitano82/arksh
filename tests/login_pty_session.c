#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef __APPLE__
# include <util.h>
#else
# include <pty.h>
#endif

static int parse_metrics(
  const char *buf,
  long *pid,
  long *pgid,
  long *sid,
  long *tty_pgid,
  char *login_mode,
  size_t login_mode_size,
  char *has_tty,
  size_t has_tty_size
) {
  const char *cursor;

  if (buf == NULL || pid == NULL || pgid == NULL || sid == NULL || tty_pgid == NULL ||
      login_mode == NULL || login_mode_size == 0 || has_tty == NULL || has_tty_size == 0) {
    return 0;
  }

  for (cursor = buf; *cursor != '\0'; ++cursor) {
    long local_pid;
    long local_pgid;
    long local_sid;
    long local_tty_pgid;
    char local_login[16];
    char local_tty[16];

    if (sscanf(cursor, "%ld|%ld|%ld|%ld|%15[^|]|%15s",
               &local_pid, &local_pgid, &local_sid, &local_tty_pgid, local_login, local_tty) == 6) {
      *pid = local_pid;
      *pgid = local_pgid;
      *sid = local_sid;
      *tty_pgid = local_tty_pgid;
      snprintf(login_mode, login_mode_size, "%s", local_login);
      snprintf(has_tty, has_tty_size, "%s", local_tty);
      return 1;
    }
  }

  return 0;
}

int main(int argc, char **argv) {
  static const char *kCommand =
    "text(\"%v|%v|%v|%v|%v|%v\") -> print(proc() -> pid, proc() -> pgid, proc() -> sid, proc() -> tty_pgid, shell() -> login_mode, shell() -> has_tty)";
  int master;
  pid_t child;
  char output[4096];
  ssize_t total = 0;
  int status = 0;
  long shell_pid = 0;
  long shell_pgid = 0;
  long shell_sid = 0;
  long tty_pgid = 0;
  char login_mode[16];
  char has_tty[16];

  if (argc < 2) {
    fprintf(stderr, "usage: %s /path/to/arksh\n", argv[0]);
    return 1;
  }

  child = forkpty(&master, NULL, NULL, NULL);
  if (child < 0) {
    perror("forkpty");
    return 1;
  }

  if (child == 0) {
    char *child_argv[] = { argv[1], "--login", "-c", (char *) kCommand, NULL };

    setenv("HOME", ".", 1);
    setenv("USERPROFILE", ".", 1);
    setenv("ARKSH_CONFIG_HOME", "./pty-config-home", 1);
    setenv("ARKSH_STATE_HOME", "./pty-state-home", 1);
    setenv("ARKSH_DATA_HOME", "./pty-data-home", 1);
    setenv("ARKSH_GLOBAL_PROFILE", "/nonexistent", 1);
    setenv("ARKSH_LOGIN_PROFILE", "/nonexistent", 1);
    execv(argv[1], child_argv);
    _exit(127);
  }

  while (total + 1 < (ssize_t) sizeof(output)) {
    ssize_t n = read(master, output + total, sizeof(output) - (size_t) total - 1);

    if (n <= 0) {
      break;
    }
    total += n;
  }
  close(master);
  output[total] = '\0';

  waitpid(child, &status, 0);
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    fprintf(stderr, "arksh PTY child failed: %s\n", output);
    return 1;
  }

  login_mode[0] = '\0';
  has_tty[0] = '\0';
  if (!parse_metrics(output, &shell_pid, &shell_pgid, &shell_sid, &tty_pgid,
                     login_mode, sizeof(login_mode), has_tty, sizeof(has_tty))) {
    fprintf(stderr, "unable to parse PTY login metrics: %s\n", output);
    return 1;
  }

  if (shell_pid != shell_pgid || shell_sid != shell_pgid || tty_pgid != shell_pgid ||
      strcmp(login_mode, "true") != 0 || strcmp(has_tty, "true") != 0) {
    fprintf(stderr, "unexpected PTY login metrics: %s\n", output);
    return 1;
  }

  puts("login_pty_session: all tests passed");
  return 0;
}
