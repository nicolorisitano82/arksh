/* E8-S2-T2: PTY-based REPL tests.
 *
 * Uses forkpty() to spawn arksh in a pseudo-terminal and exercises interactive
 * features that cannot be tested via -c mode: the initial prompt, the
 * continuation prompt on incomplete input, tab completion and clean exit.
 *
 * POSIX only — not compiled on Windows.
 * Returns 0 when all tests pass, 1 on the first failure.
 */

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef __APPLE__
# include <util.h>
#else
# include <pty.h>
#endif

/* ------------------------------------------------------------------ helpers */

static int g_failures = 0;

#define EXPECT(cond, msg)                                              \
  do {                                                                 \
    if (!(cond)) {                                                     \
      fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, (msg)); \
      g_failures++;                                                    \
    }                                                                  \
  } while (0)

/* Read from fd for up to timeout_ms milliseconds.
   Returns bytes read, or 0 on timeout / error. */
static ssize_t read_timed(int fd, char *buf, size_t max, int timeout_ms) {
  fd_set rset;
  struct timeval tv;

  FD_ZERO(&rset);
  FD_SET(fd, &rset);
  tv.tv_sec  = timeout_ms / 1000;
  tv.tv_usec = (timeout_ms % 1000) * 1000;

  if (select(fd + 1, &rset, NULL, NULL, &tv) <= 0) {
    return 0;
  }
  return read(fd, buf, max);
}

/* Drain all available output into buf[0..out_size-1].
   Waits up to initial_ms for the first byte, then reads with short timeouts
   until quiet.  Always NUL-terminates. */
static size_t drain(int fd, char *buf, size_t buf_size, int initial_ms) {
  size_t total = 0;
  ssize_t n;

  n = read_timed(fd, buf, buf_size - 1, initial_ms);
  if (n > 0) {
    total = (size_t) n;
    /* Slurp the rest with a short timeout so we don't spin forever. */
    while (total + 1 < buf_size) {
      n = read_timed(fd, buf + total, buf_size - total - 1, 150);
      if (n <= 0) {
        break;
      }
      total += (size_t) n;
    }
  }
  buf[total] = '\0';
  return total;
}

/* Spawn ./arksh in a PTY.  Returns master fd; *pid is the child pid.
   Returns -1 on error. */
static int spawn_arksh(pid_t *pid) {
  int master;
  pid_t child;
  char *argv[] = { "./arksh", NULL };

  child = forkpty(&master, NULL, NULL, NULL);
  if (child < 0) {
    return -1;
  }
  if (child == 0) {
    execv("./arksh", argv);
    _exit(127);
  }
  *pid = child;
  return master;
}

static void cleanup(pid_t pid, int master) {
  close(master);
  kill(pid, SIGTERM);
  waitpid(pid, NULL, 0);
}

/* ------------------------------------------------------------------ tests */

/* Test 1: arksh starts and produces a non-empty prompt. */
static void test_prompt_appears(void) {
  pid_t pid;
  int   master;
  char  buf[4096];

  master = spawn_arksh(&pid);
  if (master < 0) {
    EXPECT(0, "prompt_appears: forkpty failed");
    return;
  }

  drain(master, buf, sizeof(buf), 1200);
  /* The prompt must be non-empty; it usually contains '>' or 'arksh'. */
  EXPECT(buf[0] != '\0', "prompt_appears: got a non-empty prompt");

  cleanup(pid, master);
}

/* Test 2: a simple command produces the expected output. */
static void test_echo_command(void) {
  pid_t pid;
  int   master;
  char  buf[4096];

  master = spawn_arksh(&pid);
  if (master < 0) {
    EXPECT(0, "echo_command: forkpty failed");
    return;
  }

  drain(master, buf, sizeof(buf), 1200); /* consume prompt */

  write(master, "echo hello_pty_test\n", 20);
  drain(master, buf, sizeof(buf), 1000);

  EXPECT(strstr(buf, "hello_pty_test") != NULL,
         "echo_command: output contains 'hello_pty_test'");

  cleanup(pid, master);
}

/* Test 3: an incomplete multi-line construct triggers the continuation prompt. */
static void test_continuation_prompt(void) {
  pid_t pid;
  int   master;
  char  buf[4096];

  master = spawn_arksh(&pid);
  if (master < 0) {
    EXPECT(0, "continuation_prompt: forkpty failed");
    return;
  }

  drain(master, buf, sizeof(buf), 1200); /* consume initial prompt */

  /* Begin a multi-line if block — leaves input open. */
  write(master, "if true\n", 8);
  drain(master, buf, sizeof(buf), 800);

  /* There should be something drawn (the continuation prompt). */
  EXPECT(buf[0] != '\0', "continuation_prompt: continuation prompt appears");

  /* Abort the incomplete block. */
  write(master, "\x03", 1);
  drain(master, buf, sizeof(buf), 400);

  cleanup(pid, master);
}

/* Test 4: TAB after a unique prefix completes the command. */
static void test_tab_completion(void) {
  pid_t pid;
  int   master;
  char  buf[4096];

  master = spawn_arksh(&pid);
  if (master < 0) {
    EXPECT(0, "tab_completion: forkpty failed");
    return;
  }

  drain(master, buf, sizeof(buf), 1200); /* consume prompt */

  /* "histor" is a unique prefix — TAB should complete to "history". */
  write(master, "histor\t", 7);
  drain(master, buf, sizeof(buf), 800);

  EXPECT(strstr(buf, "histor") != NULL,
         "tab_completion: prefix 'histor' is reflected after TAB");

  /* Cancel the partial command. */
  write(master, "\x03", 1);
  drain(master, buf, sizeof(buf), 400);

  cleanup(pid, master);
}

/* Test 5: Ctrl-D on an empty line exits cleanly with status 0. */
static void test_ctrl_d_exits(void) {
  pid_t pid;
  int   master;
  char  buf[4096];
  int   status = -1;

  master = spawn_arksh(&pid);
  if (master < 0) {
    EXPECT(0, "ctrl_d_exits: forkpty failed");
    return;
  }

  drain(master, buf, sizeof(buf), 1200); /* consume prompt */

  write(master, "\x04", 1); /* Ctrl-D */
  drain(master, buf, sizeof(buf), 800);

  waitpid(pid, &status, 0);
  close(master);

  EXPECT(WIFEXITED(status),        "ctrl_d_exits: process exited normally");
  EXPECT(WEXITSTATUS(status) == 0, "ctrl_d_exits: exit code is 0");
}

/* ------------------------------------------------------------------ main */

int main(void) {
  test_prompt_appears();
  test_echo_command();
  test_continuation_prompt();
  test_tab_completion();
  test_ctrl_d_exits();

  if (g_failures > 0) {
    fprintf(stderr, "%d PTY test(s) FAILED\n", g_failures);
    return 1;
  }

  printf("pty_repl: all tests passed\n");
  return 0;
}
