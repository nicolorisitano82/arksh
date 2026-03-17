#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "oosh/expand.h"
#include "oosh/platform.h"
#include "oosh/shell.h"

static void copy_string(char *dest, size_t dest_size, const char *src) {
  if (dest_size == 0) {
    return;
  }

  snprintf(dest, dest_size, "%s", src == NULL ? "" : src);
}

static int append_char(char *dest, size_t dest_size, size_t *length, char c, char *error, size_t error_size, const char *label) {
  if (dest == NULL || length == NULL || error == NULL || error_size == 0 || label == NULL) {
    return 1;
  }

  if (*length + 1 >= dest_size) {
    snprintf(error, error_size, "%s too long", label);
    return 1;
  }

  dest[*length] = c;
  (*length)++;
  dest[*length] = '\0';
  return 0;
}

static int append_text(char *dest, size_t dest_size, size_t *length, const char *src, char *error, size_t error_size, const char *label) {
  size_t i;

  if (src == NULL) {
    return 0;
  }

  for (i = 0; src[i] != '\0'; ++i) {
    if (append_char(dest, dest_size, length, src[i], error, error_size, label) != 0) {
      return 1;
    }
  }

  return 0;
}

static int is_glob_meta_char(char c) {
  return c == '*' || c == '?' || c == '[' || c == ']';
}

static int append_pattern_text(
  char *pattern,
  size_t pattern_size,
  size_t *pattern_len,
  const char *text,
  int allow_glob,
  int *has_glob,
  char *error,
  size_t error_size
) {
  size_t i;

  if (text == NULL) {
    return 0;
  }

  for (i = 0; text[i] != '\0'; ++i) {
    if (allow_glob && is_glob_meta_char(text[i])) {
      if (has_glob != NULL) {
        *has_glob = 1;
      }
      if (append_char(pattern, pattern_size, pattern_len, text[i], error, error_size, "glob pattern") != 0) {
        return 1;
      }
      continue;
    }

    if (text[i] == '\\' || is_glob_meta_char(text[i])) {
      if (append_char(pattern, pattern_size, pattern_len, '\\', error, error_size, "glob pattern") != 0) {
        return 1;
      }
    }
    if (append_char(pattern, pattern_size, pattern_len, text[i], error, error_size, "glob pattern") != 0) {
      return 1;
    }
  }

  return 0;
}

static int append_pattern_char(
  char *pattern,
  size_t pattern_size,
  size_t *pattern_len,
  char c,
  int allow_glob,
  int *has_glob,
  char *error,
  size_t error_size
) {
  char text[2];

  text[0] = c;
  text[1] = '\0';
  return append_pattern_text(pattern, pattern_size, pattern_len, text, allow_glob, has_glob, error, error_size);
}

static const char *lookup_home_directory(OoshShell *shell) {
  const char *home = oosh_shell_get_var(shell, "HOME");

  if (home != NULL && home[0] != '\0') {
    return home;
  }

#ifdef _WIN32
  home = oosh_shell_get_var(shell, "USERPROFILE");
  if (home != NULL && home[0] != '\0') {
    return home;
  }
#endif

  return NULL;
}

static void trim_trailing_newlines(char *text) {
  size_t len;

  if (text == NULL) {
    return;
  }

  len = strlen(text);
  while (len > 0 && (text[len - 1] == '\n' || text[len - 1] == '\r')) {
    text[len - 1] = '\0';
    len--;
  }
}

static int clone_subshell(const OoshShell *shell, OoshShell **out_shell, char *error, size_t error_size) {
  OoshShell *clone;
  size_t i;

  if (shell == NULL || out_shell == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  *out_shell = NULL;
  clone = (OoshShell *) calloc(1, sizeof(*clone));
  if (clone == NULL) {
    snprintf(error, error_size, "unable to allocate command substitution shell");
    return 1;
  }

  *clone = *shell;
  for (i = 0; i < clone->binding_count; ++i) {
    memset(&clone->bindings[i].value, 0, sizeof(clone->bindings[i].value));
    if (oosh_value_copy(&clone->bindings[i].value, &shell->bindings[i].value) != 0) {
      size_t rollback;

      for (rollback = 0; rollback < i; ++rollback) {
        oosh_value_free(&clone->bindings[rollback].value);
      }
      free(clone);
      snprintf(error, error_size, "unable to clone value binding for command substitution");
      return 1;
    }
  }

  *out_shell = clone;
  return 0;
}

static void destroy_subshell(OoshShell *shell) {
  size_t i;

  if (shell == NULL) {
    return;
  }

  for (i = 0; i < shell->binding_count; ++i) {
    oosh_value_free(&shell->bindings[i].value);
  }
  for (i = 0; i < shell->job_count; ++i) {
    oosh_platform_close_background_process(&shell->jobs[i].process);
  }

  free(shell);
}

static int parse_command_substitution(const char *raw, size_t *index, char *out, size_t out_size, char *error, size_t error_size) {
  char command[OOSH_MAX_OUTPUT];
  size_t command_len = 0;
  size_t i;
  int depth = 1;
  char quote = '\0';

  if (raw == NULL || index == NULL || out == NULL || out_size == 0 || error == NULL || error_size == 0) {
    return 1;
  }

  out[0] = '\0';
  i = *index + 2;

  while (raw[i] != '\0') {
    char c = raw[i];

    if (quote == '\0') {
      if (c == '\'' || c == '"') {
        quote = c;
      } else if (c == '\\') {
        if (raw[i + 1] == '\0') {
          snprintf(error, error_size, "dangling escape in command substitution");
          return 1;
        }
        if (append_char(command, sizeof(command), &command_len, c, error, error_size, "command substitution") != 0 ||
            append_char(command, sizeof(command), &command_len, raw[i + 1], error, error_size, "command substitution") != 0) {
          return 1;
        }
        i += 2;
        continue;
      } else if (c == '$' && raw[i + 1] == '(') {
        depth++;
      } else if (c == ')') {
        depth--;
        if (depth == 0) {
          *index = i;
          command[command_len] = '\0';
          copy_string(out, out_size, command);
          return 0;
        }
      }
    } else if (quote == '\'') {
      if (c == '\'') {
        quote = '\0';
      }
    } else if (quote == '"') {
      if (c == '\\') {
        if (raw[i + 1] == '\0') {
          snprintf(error, error_size, "dangling escape in command substitution");
          return 1;
        }
        if (append_char(command, sizeof(command), &command_len, c, error, error_size, "command substitution") != 0 ||
            append_char(command, sizeof(command), &command_len, raw[i + 1], error, error_size, "command substitution") != 0) {
          return 1;
        }
        i += 2;
        continue;
      }
      if (c == '"') {
        quote = '\0';
      }
    }

    if (append_char(command, sizeof(command), &command_len, c, error, error_size, "command substitution") != 0) {
      return 1;
    }
    i++;
  }

  snprintf(error, error_size, "unterminated command substitution");
  return 1;
}

static int execute_command_substitution(OoshShell *shell, const char *command, char *out, size_t out_size, char *error, size_t error_size) {
  char command_output[OOSH_MAX_OUTPUT];
  OoshShell *subshell = NULL;
  char saved_cwd[OOSH_MAX_PATH];
  int have_saved_cwd = 0;
  int status;

  if (shell == NULL || command == NULL || out == NULL || out_size == 0 || error == NULL || error_size == 0) {
    return 1;
  }

  if (clone_subshell(shell, &subshell, error, error_size) != 0) {
    return 1;
  }

  if (oosh_platform_getcwd(saved_cwd, sizeof(saved_cwd)) == 0) {
    have_saved_cwd = 1;
  }

  command_output[0] = '\0';
  status = oosh_shell_execute_line(subshell, command, command_output, sizeof(command_output));
  trim_trailing_newlines(command_output);

  if (have_saved_cwd) {
    if (oosh_platform_chdir(saved_cwd) == 0) {
      copy_string(shell->cwd, sizeof(shell->cwd), saved_cwd);
    }
  }

  destroy_subshell(subshell);

  if (status != 0) {
    snprintf(error, error_size, "command substitution failed: %s", command);
    return 1;
  }

  copy_string(out, out_size, command_output);
  return 0;
}

static int resolve_variable(OoshShell *shell, const char *raw, size_t *index, char *out, size_t out_size, char *error, size_t error_size) {
  char name[128];
  size_t name_len = 0;
  size_t i;
  const char *value = NULL;

  if (raw == NULL || index == NULL || out == NULL || out_size == 0 || error == NULL || error_size == 0) {
    return 1;
  }

  out[0] = '\0';
  i = *index + 1;

  if (raw[i] == '?') {
    snprintf(out, out_size, "%d", shell == NULL ? 0 : shell->last_status);
    *index = i;
    return 0;
  }

  if (raw[i] == '{') {
    i++;
    while (raw[i] != '\0' && raw[i] != '}') {
      if (name_len + 1 >= sizeof(name)) {
        snprintf(error, error_size, "environment variable name too long");
        return 1;
      }
      name[name_len++] = raw[i++];
    }
    if (raw[i] != '}') {
      snprintf(error, error_size, "unterminated ${...} expansion");
      return 1;
    }
    name[name_len] = '\0';
    *index = i;
  } else {
    if (!(isalpha((unsigned char) raw[i]) || raw[i] == '_')) {
      copy_string(out, out_size, "$");
      return 0;
    }
    while (isalnum((unsigned char) raw[i]) || raw[i] == '_') {
      if (name_len + 1 >= sizeof(name)) {
        snprintf(error, error_size, "environment variable name too long");
        return 1;
      }
      name[name_len++] = raw[i++];
    }
    name[name_len] = '\0';
    *index = i - 1;
  }

  value = oosh_shell_get_var(shell, name);
  copy_string(out, out_size, value == NULL ? "" : value);
  return 0;
}

static int expand_raw_token(
  OoshShell *shell,
  const char *raw,
  OoshExpandMode mode,
  char *expanded,
  size_t expanded_size,
  char *pattern,
  size_t pattern_size,
  int *has_glob,
  char *error,
  size_t error_size
) {
  size_t expanded_len = 0;
  size_t pattern_len = 0;
  size_t i = 0;
  char quote = '\0';
  int saw_prefix_fragment = 0;

  if (expanded == NULL || expanded_size == 0 || pattern == NULL || pattern_size == 0 || has_glob == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  expanded[0] = '\0';
  pattern[0] = '\0';
  *has_glob = 0;

  if (raw == NULL) {
    return 0;
  }

  while (raw[i] != '\0') {
    char c = raw[i];
    int allow_glob = (mode == OOSH_EXPAND_MODE_COMMAND && quote == '\0');

    if (quote == '\0') {
      if ((c == '\'' || c == '"')) {
        quote = c;
        saw_prefix_fragment = 1;
        i++;
        continue;
      }

      if (c == '\\') {
        if (raw[i + 1] == '\0') {
          snprintf(error, error_size, "dangling escape at end of token");
          return 1;
        }
        c = raw[i + 1];
        if (append_char(expanded, expanded_size, &expanded_len, c, error, error_size, "expanded token") != 0 ||
            append_pattern_char(pattern, pattern_size, &pattern_len, c, 0, has_glob, error, error_size) != 0) {
          return 1;
        }
        saw_prefix_fragment = 1;
        i += 2;
        continue;
      }

      if (c == '~' && !saw_prefix_fragment && expanded_len == 0 && (raw[i + 1] == '\0' || raw[i + 1] == '/')) {
        const char *home = lookup_home_directory(shell);

        if (home != NULL && home[0] != '\0') {
          if (append_text(expanded, expanded_size, &expanded_len, home, error, error_size, "expanded token") != 0 ||
              append_pattern_text(pattern, pattern_size, &pattern_len, home, allow_glob, has_glob, error, error_size) != 0) {
            return 1;
          }
        } else if (append_char(expanded, expanded_size, &expanded_len, '~', error, error_size, "expanded token") != 0 ||
                   append_pattern_text(pattern, pattern_size, &pattern_len, "~", allow_glob, has_glob, error, error_size) != 0) {
          return 1;
        }
        saw_prefix_fragment = 1;
        i++;
        continue;
      }

      if (c == '$') {
        char substitution[OOSH_MAX_OUTPUT];

        if (raw[i + 1] == '(') {
          char command[OOSH_MAX_OUTPUT];

          if (parse_command_substitution(raw, &i, command, sizeof(command), error, error_size) != 0 ||
              execute_command_substitution(shell, command, substitution, sizeof(substitution), error, error_size) != 0) {
            return 1;
          }
        } else {
          if (resolve_variable(shell, raw, &i, substitution, sizeof(substitution), error, error_size) != 0) {
            return 1;
          }
        }

        if (append_text(expanded, expanded_size, &expanded_len, substitution, error, error_size, "expanded token") != 0 ||
            append_pattern_text(pattern, pattern_size, &pattern_len, substitution, allow_glob, has_glob, error, error_size) != 0) {
          return 1;
        }
        saw_prefix_fragment = 1;
        i++;
        continue;
      }
    } else if (quote == '\'') {
      if (c == '\'') {
        quote = '\0';
        i++;
        continue;
      }
      if (append_char(expanded, expanded_size, &expanded_len, c, error, error_size, "expanded token") != 0 ||
          append_pattern_char(pattern, pattern_size, &pattern_len, c, 0, has_glob, error, error_size) != 0) {
        return 1;
      }
      saw_prefix_fragment = 1;
      i++;
      continue;
    } else if (quote == '"') {
      if (c == '"') {
        quote = '\0';
        i++;
        continue;
      }
      if (c == '\\') {
        if (raw[i + 1] == '\0') {
          snprintf(error, error_size, "dangling escape inside double quotes");
          return 1;
        }
        c = raw[i + 1];
        if (append_char(expanded, expanded_size, &expanded_len, c, error, error_size, "expanded token") != 0 ||
            append_pattern_char(pattern, pattern_size, &pattern_len, c, 0, has_glob, error, error_size) != 0) {
          return 1;
        }
        saw_prefix_fragment = 1;
        i += 2;
        continue;
      }
      if (c == '$') {
        char substitution[OOSH_MAX_OUTPUT];

        if (raw[i + 1] == '(') {
          char command[OOSH_MAX_OUTPUT];

          if (parse_command_substitution(raw, &i, command, sizeof(command), error, error_size) != 0 ||
              execute_command_substitution(shell, command, substitution, sizeof(substitution), error, error_size) != 0) {
            return 1;
          }
        } else {
          if (resolve_variable(shell, raw, &i, substitution, sizeof(substitution), error, error_size) != 0) {
            return 1;
          }
        }

        if (append_text(expanded, expanded_size, &expanded_len, substitution, error, error_size, "expanded token") != 0 ||
            append_pattern_text(pattern, pattern_size, &pattern_len, substitution, 0, has_glob, error, error_size) != 0) {
          return 1;
        }
        saw_prefix_fragment = 1;
        i++;
        continue;
      }
    }

    if (append_char(expanded, expanded_size, &expanded_len, c, error, error_size, "expanded token") != 0 ||
        append_pattern_char(pattern, pattern_size, &pattern_len, c, allow_glob, has_glob, error, error_size) != 0) {
      return 1;
    }
    saw_prefix_fragment = 1;
    i++;
  }

  if (quote != '\0') {
    snprintf(error, error_size, "unterminated quoted token");
    return 1;
  }

  return 0;
}

int oosh_expand_word(
  OoshShell *shell,
  const char *raw,
  OoshExpandMode mode,
  char out_values[][OOSH_MAX_TOKEN],
  int max_values,
  int *out_count,
  char *error,
  size_t error_size
) {
  char expanded[OOSH_MAX_TOKEN];
  char pattern[OOSH_MAX_TOKEN * 2];
  int has_glob = 0;

  if (out_values == NULL || max_values <= 0 || out_count == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  *out_count = 0;

  if (expand_raw_token(shell, raw, mode, expanded, sizeof(expanded), pattern, sizeof(pattern), &has_glob, error, error_size) != 0) {
    return 1;
  }

  if (mode == OOSH_EXPAND_MODE_COMMAND && has_glob) {
    int match_count = 0;
    int glob_status;

    glob_status = oosh_platform_glob(pattern, out_values, max_values, &match_count);
    if (glob_status == 0 && match_count > 0) {
      *out_count = match_count;
      return 0;
    }
    if (glob_status == 2) {
      snprintf(error, error_size, "glob expansion produced too many matches");
      return 1;
    }
    if (glob_status != 0) {
      snprintf(error, error_size, "glob expansion failed");
      return 1;
    }
  }

  copy_string(out_values[0], sizeof(out_values[0]), expanded);
  *out_count = 1;
  return 0;
}
