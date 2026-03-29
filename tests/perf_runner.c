#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "arksh/perf.h"

typedef struct {
  ArkshPerfCounters counters;
  int found_counters;
  int exit_code;
  double wall_ms;
  long long max_rss_kb;
} PerfRunSummary;

static int lookup_metric_value(const PerfRunSummary *summary, const char *name, unsigned long long *out_value) {
  if (summary == NULL || name == NULL || out_value == NULL) {
    return 1;
  }

  if (strcmp(name, "exit_code") == 0) {
    *out_value = (unsigned long long) summary->exit_code;
    return 0;
  }
  if (strcmp(name, "max_rss_kb") == 0) {
    *out_value = (unsigned long long) summary->max_rss_kb;
    return 0;
  }
  if (strcmp(name, "wall_ms") == 0) {
    *out_value = (unsigned long long) summary->wall_ms;
    return 0;
  }
  if (strcmp(name, "malloc_calls") == 0) {
    *out_value = summary->counters.malloc_calls;
    return 0;
  }
  if (strcmp(name, "calloc_calls") == 0) {
    *out_value = summary->counters.calloc_calls;
    return 0;
  }
  if (strcmp(name, "realloc_calls") == 0) {
    *out_value = summary->counters.realloc_calls;
    return 0;
  }
  if (strcmp(name, "free_calls") == 0) {
    *out_value = summary->counters.free_calls;
    return 0;
  }
  if (strcmp(name, "malloc_bytes") == 0) {
    *out_value = summary->counters.malloc_bytes;
    return 0;
  }
  if (strcmp(name, "calloc_bytes") == 0) {
    *out_value = summary->counters.calloc_bytes;
    return 0;
  }
  if (strcmp(name, "realloc_bytes") == 0) {
    *out_value = summary->counters.realloc_bytes;
    return 0;
  }
  if (strcmp(name, "temp_buffer_calls") == 0) {
    *out_value = summary->counters.temp_buffer_calls;
    return 0;
  }
  if (strcmp(name, "temp_buffer_bytes") == 0) {
    *out_value = summary->counters.temp_buffer_bytes;
    return 0;
  }
  if (strcmp(name, "value_copy_calls") == 0) {
    *out_value = summary->counters.value_copy_calls;
    return 0;
  }
  if (strcmp(name, "value_render_calls") == 0) {
    *out_value = summary->counters.value_render_calls;
    return 0;
  }

  return 1;
}

static int check_expectation(const PerfRunSummary *summary, const char *spec) {
  const char *sep;
  char name[128];
  unsigned long long actual;
  unsigned long long expected;
  char *endptr = NULL;
  size_t name_len;

  if (summary == NULL || spec == NULL) {
    return 1;
  }

  sep = strstr(spec, "<=");
  if (sep == NULL) {
    fprintf(stderr, "unsupported expectation syntax: %s\n", spec);
    return 1;
  }

  name_len = (size_t) (sep - spec);
  if (name_len == 0 || name_len >= sizeof(name)) {
    fprintf(stderr, "invalid expectation metric: %s\n", spec);
    return 1;
  }
  memcpy(name, spec, name_len);
  name[name_len] = '\0';

  expected = strtoull(sep + 2, &endptr, 10);
  if (endptr == NULL || *endptr != '\0') {
    fprintf(stderr, "invalid expectation value: %s\n", spec);
    return 1;
  }

  if (lookup_metric_value(summary, name, &actual) != 0) {
    fprintf(stderr, "unknown expectation metric: %s\n", name);
    return 1;
  }

  if (actual > expected) {
    fprintf(stderr, "expectation failed: %s (%llu > %llu)\n", name, actual, expected);
    return 1;
  }

  return 0;
}

static double now_ms(void) {
#ifdef _WIN32
  LARGE_INTEGER freq;
  LARGE_INTEGER counter;
  QueryPerformanceFrequency(&freq);
  QueryPerformanceCounter(&counter);
  return (double) counter.QuadPart * 1000.0 / (double) freq.QuadPart;
#else
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (double) tv.tv_sec * 1000.0 + (double) tv.tv_usec / 1000.0;
#endif
}

static int parse_unsigned_line(const char *key, const char *value, unsigned long long *target) {
  char *endptr = NULL;

  if (strcmp(key, "enabled") == 0) {
    return 1;
  }

  if (target == NULL) {
    return 0;
  }

  *target = strtoull(value, &endptr, 10);
  return endptr != NULL && *endptr == '\0';
}

static void parse_perf_output(const char *stdout_text, PerfRunSummary *summary) {
  char *copy;
  char *line;
  char *saveptr = NULL;

  if (stdout_text == NULL || summary == NULL) {
    return;
  }

  copy = (char *) malloc(strlen(stdout_text) + 1);
  if (copy == NULL) {
    return;
  }
  strcpy(copy, stdout_text);

  for (line = strtok_r(copy, "\r\n", &saveptr); line != NULL; line = strtok_r(NULL, "\r\n", &saveptr)) {
    char *eq = strchr(line, '=');
    const char *value;

    if (eq == NULL) {
      continue;
    }
    *eq = '\0';
    value = eq + 1;

    if (strcmp(line, "enabled") == 0) {
      summary->counters.enabled = atoi(value) != 0;
      summary->found_counters = 1;
      continue;
    }
    if (strcmp(line, "malloc_calls") == 0) {
      summary->found_counters |= parse_unsigned_line(line, value, &summary->counters.malloc_calls);
      continue;
    }
    if (strcmp(line, "calloc_calls") == 0) {
      summary->found_counters |= parse_unsigned_line(line, value, &summary->counters.calloc_calls);
      continue;
    }
    if (strcmp(line, "realloc_calls") == 0) {
      summary->found_counters |= parse_unsigned_line(line, value, &summary->counters.realloc_calls);
      continue;
    }
    if (strcmp(line, "free_calls") == 0) {
      summary->found_counters |= parse_unsigned_line(line, value, &summary->counters.free_calls);
      continue;
    }
    if (strcmp(line, "malloc_bytes") == 0) {
      summary->found_counters |= parse_unsigned_line(line, value, &summary->counters.malloc_bytes);
      continue;
    }
    if (strcmp(line, "calloc_bytes") == 0) {
      summary->found_counters |= parse_unsigned_line(line, value, &summary->counters.calloc_bytes);
      continue;
    }
    if (strcmp(line, "realloc_bytes") == 0) {
      summary->found_counters |= parse_unsigned_line(line, value, &summary->counters.realloc_bytes);
      continue;
    }
    if (strcmp(line, "temp_buffer_calls") == 0) {
      summary->found_counters |= parse_unsigned_line(line, value, &summary->counters.temp_buffer_calls);
      continue;
    }
    if (strcmp(line, "temp_buffer_bytes") == 0) {
      summary->found_counters |= parse_unsigned_line(line, value, &summary->counters.temp_buffer_bytes);
      continue;
    }
    if (strcmp(line, "value_copy_calls") == 0) {
      summary->found_counters |= parse_unsigned_line(line, value, &summary->counters.value_copy_calls);
      continue;
    }
    if (strcmp(line, "value_render_calls") == 0) {
      summary->found_counters |= parse_unsigned_line(line, value, &summary->counters.value_render_calls);
      continue;
    }
  }

  free(copy);
}

#ifdef _WIN32
static int run_benchmark_case(const char *arksh_path, const char *mode, const char *payload, char *captured, size_t captured_size, PerfRunSummary *summary) {
  (void) arksh_path;
  (void) mode;
  (void) payload;
  (void) captured;
  (void) captured_size;
  if (summary != NULL) {
    memset(summary, 0, sizeof(*summary));
    summary->exit_code = 1;
  }
  fputs("arksh_perf_runner is not implemented on Windows yet\n", stderr);
  return 1;
}
#else
static int run_benchmark_case(const char *arksh_path, const char *mode, const char *payload, char *captured, size_t captured_size, PerfRunSummary *summary) {
  int pipefd[2];
  pid_t pid;
  int status = 0;
  struct rusage usage;
  size_t used = 0;
  double start_ms;
  double end_ms;

  if (arksh_path == NULL || mode == NULL || payload == NULL || captured == NULL || captured_size == 0 || summary == NULL) {
    return 1;
  }

  memset(summary, 0, sizeof(*summary));
  captured[0] = '\0';

  if (pipe(pipefd) != 0) {
    perror("pipe");
    return 1;
  }

  start_ms = now_ms();
  pid = fork();
  if (pid < 0) {
    perror("fork");
    close(pipefd[0]);
    close(pipefd[1]);
    return 1;
  }

  if (pid == 0) {
    char *const command_argv[] = { (char *) arksh_path, "-c", (char *) payload, NULL };
    char *const script_argv[] = { (char *) arksh_path, (char *) payload, NULL };

    setenv("ARKSH_PERF", "1", 1);
    close(pipefd[0]);
    dup2(pipefd[1], STDOUT_FILENO);
    dup2(pipefd[1], STDERR_FILENO);
    close(pipefd[1]);

    if (strcmp(mode, "command") == 0) {
      execv(arksh_path, command_argv);
    } else if (strcmp(mode, "script") == 0) {
      execv(arksh_path, script_argv);
    }

    fprintf(stderr, "unsupported mode: %s\n", mode);
    _exit(127);
  }

  close(pipefd[1]);
  while (used + 1 < captured_size) {
    ssize_t count = read(pipefd[0], captured + used, captured_size - used - 1);
    if (count < 0) {
      if (errno == EINTR) {
        continue;
      }
      perror("read");
      close(pipefd[0]);
      return 1;
    }
    if (count == 0) {
      break;
    }
    used += (size_t) count;
  }
  captured[used] = '\0';
  close(pipefd[0]);

  memset(&usage, 0, sizeof(usage));
  if (wait4(pid, &status, 0, &usage) < 0) {
    perror("wait4");
    return 1;
  }
  end_ms = now_ms();

  if (WIFEXITED(status)) {
    summary->exit_code = WEXITSTATUS(status);
  } else if (WIFSIGNALED(status)) {
    summary->exit_code = 128 + WTERMSIG(status);
  } else {
    summary->exit_code = 1;
  }
  summary->wall_ms = end_ms - start_ms;
#ifdef __APPLE__
  summary->max_rss_kb = (long long) (usage.ru_maxrss / 1024);
#else
  summary->max_rss_kb = (long long) usage.ru_maxrss;
#endif
  parse_perf_output(captured, summary);
  return 0;
}
#endif

int main(int argc, char **argv) {
  PerfRunSummary summary;
  char captured[32768];
  int i;

  if (argc < 5) {
    fprintf(stderr, "usage: %s <arksh-path> <label> <command|script> <payload> [metric<=value ...]\n", argv[0]);
    return 1;
  }

  if (run_benchmark_case(argv[1], argv[3], argv[4], captured, sizeof(captured), &summary) != 0) {
    return 1;
  }

  printf("case=%s\n", argv[2]);
  printf("mode=%s\n", argv[3]);
  printf("exit_code=%d\n", summary.exit_code);
  printf("wall_ms=%.3f\n", summary.wall_ms);
  printf("max_rss_kb=%lld\n", summary.max_rss_kb);
  if (summary.found_counters) {
    printf("enabled=%d\n", summary.counters.enabled);
    printf("malloc_calls=%llu\n", summary.counters.malloc_calls);
    printf("calloc_calls=%llu\n", summary.counters.calloc_calls);
    printf("realloc_calls=%llu\n", summary.counters.realloc_calls);
    printf("free_calls=%llu\n", summary.counters.free_calls);
    printf("malloc_bytes=%llu\n", summary.counters.malloc_bytes);
    printf("calloc_bytes=%llu\n", summary.counters.calloc_bytes);
    printf("realloc_bytes=%llu\n", summary.counters.realloc_bytes);
    printf("temp_buffer_calls=%llu\n", summary.counters.temp_buffer_calls);
    printf("temp_buffer_bytes=%llu\n", summary.counters.temp_buffer_bytes);
    printf("value_copy_calls=%llu\n", summary.counters.value_copy_calls);
    printf("value_render_calls=%llu\n", summary.counters.value_render_calls);
  }

  for (i = 5; i < argc; ++i) {
    if (check_expectation(&summary, argv[i]) != 0) {
      return 1;
    }
  }

  return summary.exit_code == 0 ? 0 : 1;
}
