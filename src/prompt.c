#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "arksh/platform.h"
#include "arksh/prompt.h"
#include "arksh/shell.h"

static void copy_string(char *dest, size_t dest_size, const char *src) {
  if (dest_size == 0) {
    return;
  }

  snprintf(dest, dest_size, "%s", src == NULL ? "" : src);
}

typedef struct {
  int valid;
  const struct ArkshShell *shell;
  const ArkshPromptConfig *config;
  unsigned long generation;
  unsigned long shell_generation;
  int last_status;
  size_t plugin_count;
  time_t second;
  char cwd[ARKSH_MAX_PATH];
  char rendered[ARKSH_MAX_OUTPUT];
} ArkshPromptRenderCache;

static ArkshPromptRenderCache s_prompt_cache;

static void trim_in_place(char *text) {
  size_t start = 0;
  size_t end;
  size_t len;

  if (text == NULL) {
    return;
  }

  len = strlen(text);
  while (start < len && isspace((unsigned char) text[start])) {
    start++;
  }

  end = len;
  while (end > start && isspace((unsigned char) text[end - 1])) {
    end--;
  }

  if (start > 0) {
    memmove(text, text + start, end - start);
  }
  text[end - start] = '\0';
}

static void parse_segments(const char *value, char items[][ARKSH_MAX_SEGMENT_NAME], size_t *count) {
  char buffer[256];
  char *token;
  char *saveptr = NULL;

  if (value == NULL || count == NULL) {
    return;
  }

  copy_string(buffer, sizeof(buffer), value);
  *count = 0;

  token = strtok_r(buffer, ",", &saveptr);
  while (token != NULL && *count < ARKSH_MAX_SEGMENTS) {
    trim_in_place(token);
    copy_string(items[*count], sizeof(items[*count]), token);
    (*count)++;
    token = strtok_r(NULL, ",", &saveptr);
  }
}

static const char *lookup_color(const ArkshPromptConfig *config, const char *segment) {
  size_t i;

  for (i = 0; i < config->style_count; ++i) {
    if (strcmp(config->styles[i].segment, segment) == 0) {
      return config->styles[i].color;
    }
  }

  if (strcmp(segment, "userhost") == 0) {
    return "green";
  }
  if (strcmp(segment, "cwd") == 0) {
    return "cyan";
  }
  if (strcmp(segment, "status") == 0) {
    return "yellow";
  }
  if (strcmp(segment, "os") == 0) {
    return "magenta";
  }
  if (strcmp(segment, "plugins") == 0) {
    return "blue";
  }
  if (strcmp(segment, "date") == 0) {
    return "blue";
  }
  if (strcmp(segment, "time") == 0) {
    return "yellow";
  }
  if (strcmp(segment, "datetime") == 0) {
    return "magenta";
  }
  return "white";
}

static int format_now(char *out, size_t out_size, const char *format) {
  time_t now;
  struct tm local_tm;

  if (out == NULL || out_size == 0 || format == NULL) {
    return 1;
  }

  now = time(NULL);
  if (now == (time_t) -1) {
    return 1;
  }

#ifdef _WIN32
  if (localtime_s(&local_tm, &now) != 0) {
    return 1;
  }
#else
  if (localtime_r(&now, &local_tm) == NULL) {
    return 1;
  }
#endif

  return strftime(out, out_size, format, &local_tm) == 0 ? 1 : 0;
}

static const char *ansi_color(const char *name) {
  if (strcmp(name, "black") == 0) {
    return "\033[30m";
  }
  if (strcmp(name, "red") == 0) {
    return "\033[31m";
  }
  if (strcmp(name, "green") == 0) {
    return "\033[32m";
  }
  if (strcmp(name, "yellow") == 0) {
    return "\033[33m";
  }
  if (strcmp(name, "blue") == 0) {
    return "\033[34m";
  }
  if (strcmp(name, "magenta") == 0) {
    return "\033[35m";
  }
  if (strcmp(name, "cyan") == 0) {
    return "\033[36m";
  }
  return "\033[37m";
}

static void append_segment(char *out, size_t out_size, const char *separator, const char *segment, const char *value, int use_color, const char *color_name) {
  if (value[0] == '\0') {
    return;
  }

  if (out[0] != '\0') {
    snprintf(out + strlen(out), out_size - strlen(out), "%s", separator);
  }

  if (use_color) {
    snprintf(out + strlen(out), out_size - strlen(out), "%s%s\033[0m", ansi_color(color_name), value);
  } else {
    snprintf(out + strlen(out), out_size - strlen(out), "%s", value);
  }

  (void) segment;
}

static void resolve_segment_value(const struct ArkshShell *shell, const char *segment, char *out, size_t out_size) {
  char host[128];
  const char *user;

  if (strcmp(segment, "user") == 0) {
    user = getenv("USER");
    if (user == NULL) {
      user = getenv("USERNAME");
    }
    copy_string(out, out_size, user == NULL ? "unknown" : user);
    return;
  }

  if (strcmp(segment, "host") == 0) {
    if (arksh_platform_gethostname(host, sizeof(host)) != 0) {
      copy_string(out, out_size, "unknown-host");
    } else {
      copy_string(out, out_size, host);
    }
    return;
  }

  if (strcmp(segment, "userhost") == 0) {
    char user_value[64];
    char host_value[128];

    resolve_segment_value(shell, "user", user_value, sizeof(user_value));
    resolve_segment_value(shell, "host", host_value, sizeof(host_value));
    snprintf(out, out_size, "%s@%s", user_value, host_value);
    return;
  }

  if (strcmp(segment, "cwd") == 0) {
    copy_string(out, out_size, shell->cwd);
    return;
  }

  if (strcmp(segment, "status") == 0) {
    if (shell->last_status == 0) {
      copy_string(out, out_size, "ok");
    } else {
      snprintf(out, out_size, "err:%d", shell->last_status);
    }
    return;
  }

  if (strcmp(segment, "os") == 0) {
    copy_string(out, out_size, arksh_platform_os_name());
    return;
  }

  if (strcmp(segment, "plugins") == 0) {
    snprintf(out, out_size, "plugins:%zu", shell->plugin_count);
    return;
  }

  if (strcmp(segment, "date") == 0) {
    if (format_now(out, out_size, "%Y-%m-%d") != 0) {
      copy_string(out, out_size, "date-error");
    }
    return;
  }

  if (strcmp(segment, "time") == 0) {
    if (format_now(out, out_size, "%H:%M:%S") != 0) {
      copy_string(out, out_size, "time-error");
    }
    return;
  }

  if (strcmp(segment, "datetime") == 0) {
    if (format_now(out, out_size, "%Y-%m-%d %H:%M:%S") != 0) {
      copy_string(out, out_size, "datetime-error");
    }
    return;
  }

  if (strcmp(segment, "theme") == 0) {
    copy_string(out, out_size, shell->prompt.theme);
    return;
  }

  copy_string(out, out_size, "");
}

void arksh_prompt_config_init(ArkshPromptConfig *config) {
  if (config == NULL) {
    return;
  }

  memset(config, 0, sizeof(*config));
  config->generation = 1u;
  copy_string(config->theme, sizeof(config->theme), "default");
  copy_string(config->separator, sizeof(config->separator), " | ");
  config->use_color = 1;
  copy_string(config->left[0], sizeof(config->left[0]), "userhost");
  copy_string(config->left[1], sizeof(config->left[1]), "cwd");
  config->left_count = 2;
  copy_string(config->right[0], sizeof(config->right[0]), "status");
  copy_string(config->right[1], sizeof(config->right[1]), "os");
  copy_string(config->right[2], sizeof(config->right[2]), "datetime");
  config->right_count = 3;
  copy_string(config->continuation, sizeof(config->continuation), "... ");
}

int arksh_prompt_config_load(ArkshPromptConfig *config, const char *path) {
  FILE *fp;
  char line[512];

  if (config == NULL || path == NULL) {
    return 1;
  }

  arksh_prompt_config_init(config);

  fp = fopen(path, "r");
  if (fp == NULL) {
    return 1;
  }

  while (fgets(line, sizeof(line), fp) != NULL) {
    char *eq;
    char *key;
    char *value;

    trim_in_place(line);
    if (line[0] == '\0' || line[0] == '#') {
      continue;
    }

    eq = strchr(line, '=');
    if (eq == NULL) {
      continue;
    }

    *eq = '\0';
    key = line;
    value = eq + 1;
    trim_in_place(key);
    trim_in_place(value);

    if (strcmp(key, "theme") == 0) {
      copy_string(config->theme, sizeof(config->theme), value);
    } else if (strcmp(key, "left") == 0) {
      parse_segments(value, config->left, &config->left_count);
    } else if (strcmp(key, "right") == 0) {
      parse_segments(value, config->right, &config->right_count);
    } else if (strcmp(key, "separator") == 0) {
      copy_string(config->separator, sizeof(config->separator), value);
    } else if (strcmp(key, "use_color") == 0) {
      config->use_color = atoi(value) != 0;
    } else if (strncmp(key, "color.", 6) == 0) {
      if (config->style_count < ARKSH_MAX_SEGMENTS * 2) {
        copy_string(config->styles[config->style_count].segment, sizeof(config->styles[config->style_count].segment), key + 6);
        copy_string(config->styles[config->style_count].color, sizeof(config->styles[config->style_count].color), value);
        config->style_count++;
      }
    } else if (strcmp(key, "plugin") == 0) {
      if (config->plugin_count < ARKSH_MAX_PLUGIN_PATHS) {
        copy_string(config->plugins[config->plugin_count], sizeof(config->plugins[config->plugin_count]), value);
        config->plugin_count++;
      }
    } else if (strcmp(key, "continuation") == 0) {
      copy_string(config->continuation, sizeof(config->continuation), value);
    }
  }

  fclose(fp);
  return 0;
}

void arksh_prompt_render(const ArkshPromptConfig *config, const struct ArkshShell *shell, char *out, size_t out_size) {
  char left[ARKSH_MAX_OUTPUT];
  char right[ARKSH_MAX_OUTPUT];
  time_t now_second;
  size_t i;

  if (config == NULL || shell == NULL || out == NULL || out_size == 0) {
    return;
  }

  now_second = time(NULL);
  if (now_second != (time_t) -1 &&
      s_prompt_cache.valid &&
      s_prompt_cache.shell == shell &&
      s_prompt_cache.config == config &&
      s_prompt_cache.generation == config->generation &&
      s_prompt_cache.shell_generation == shell->completion_generation &&
      s_prompt_cache.last_status == shell->last_status &&
      s_prompt_cache.plugin_count == shell->plugin_count &&
      s_prompt_cache.second == now_second &&
      strcmp(s_prompt_cache.cwd, shell->cwd) == 0) {
    copy_string(out, out_size, s_prompt_cache.rendered);
    return;
  }

  left[0] = '\0';
  right[0] = '\0';

  for (i = 0; i < config->left_count; ++i) {
    char value[256];
    resolve_segment_value(shell, config->left[i], value, sizeof(value));
    append_segment(left, sizeof(left), config->separator, config->left[i], value, config->use_color, lookup_color(config, config->left[i]));
  }

  for (i = 0; i < config->right_count; ++i) {
    char value[256];
    resolve_segment_value(shell, config->right[i], value, sizeof(value));
    append_segment(right, sizeof(right), config->separator, config->right[i], value, config->use_color, lookup_color(config, config->right[i]));
  }

  if (right[0] != '\0') {
    snprintf(out, out_size, "[%s] %s || %s > ", config->theme, left, right);
  } else {
    snprintf(out, out_size, "[%s] %s > ", config->theme, left);
  }

  s_prompt_cache.valid = 1;
  s_prompt_cache.shell = shell;
  s_prompt_cache.config = config;
  s_prompt_cache.generation = config->generation;
  s_prompt_cache.shell_generation = shell->completion_generation;
  s_prompt_cache.last_status = shell->last_status;
  s_prompt_cache.plugin_count = shell->plugin_count;
  s_prompt_cache.second = now_second;
  copy_string(s_prompt_cache.cwd, sizeof(s_prompt_cache.cwd), shell->cwd);
  copy_string(s_prompt_cache.rendered, sizeof(s_prompt_cache.rendered), out);
}
