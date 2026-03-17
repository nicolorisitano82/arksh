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

/* Forward declaration needed by lookup_var_value. */
static int resolve_special_name(OoshShell *shell, const char *name, char *out, size_t out_size);

/* ---- Glob matcher (for parameter expansion patterns) ---- */

static int glob_match_full(const char *pat, const char *str) {
  if (pat == NULL || str == NULL) {
    return 0;
  }
  while (*pat != '\0') {
    if (*pat == '*') {
      pat++;
      do {
        if (glob_match_full(pat, str)) {
          return 1;
        }
      } while (*str++ != '\0');
      return 0;
    }
    if (*pat == '?') {
      if (*str == '\0') {
        return 0;
      }
      pat++;
      str++;
      continue;
    }
    if (*pat == '\\' && *(pat + 1) != '\0') {
      pat++;
    }
    if (*pat != *str) {
      return 0;
    }
    pat++;
    str++;
  }
  return *str == '\0';
}

/* Resolve a variable name to its string value.
   Tries special/positional names first, then shell vars.
   Returns 1 if the variable is "set" (even if empty), 0 if unset.  */
static int lookup_var_value(OoshShell *shell, const char *name, char *out, size_t out_size) {
  const char *v;

  if (resolve_special_name(shell, name, out, out_size)) {
    return 1; /* special vars are always "set" */
  }
  v = oosh_shell_get_var(shell, name);
  if (v == NULL) {
    out[0] = '\0';
    return 0; /* unset */
  }
  copy_string(out, out_size, v);
  return 1; /* set (may be empty) */
}

/* Apply suffix removal: shortest (%/single) or longest (%%/double). */
static void apply_suffix_trim(const char *val, const char *pat, int longest, char *out, size_t out_size) {
  size_t len = strlen(val);
  size_t k;

  if (longest) {
    /* Longest suffix: scan from the start (smallest j = largest suffix). */
    for (k = 0; k <= len; ++k) {
      if (glob_match_full(pat, val + k)) {
        /* Remove suffix starting at k. */
        size_t keep = k < out_size - 1 ? k : out_size - 1;
        memcpy(out, val, keep);
        out[keep] = '\0';
        return;
      }
    }
  } else {
    /* Shortest suffix: scan from end (largest j = shortest suffix). */
    k = len;
    for (;;) {
      if (glob_match_full(pat, val + k)) {
        size_t keep = k < out_size - 1 ? k : out_size - 1;
        memcpy(out, val, keep);
        out[keep] = '\0';
        return;
      }
      if (k == 0) {
        break;
      }
      k--;
    }
  }
  /* No match: return original. */
  copy_string(out, out_size, val);
}

/* Apply prefix removal: shortest (#/single) or longest (##/double). */
static void apply_prefix_trim(const char *val, const char *pat, int longest, char *out, size_t out_size) {
  size_t len = strlen(val);
  size_t match_len = 0;
  int found = 0;
  size_t k;
  char tmp[OOSH_MAX_OUTPUT];

  if (longest) {
    /* Longest prefix: scan from end (largest k). */
    k = len;
    for (;;) {
      size_t copy_len = k < sizeof(tmp) - 1 ? k : sizeof(tmp) - 1;
      memcpy(tmp, val, copy_len);
      tmp[copy_len] = '\0';
      if (glob_match_full(pat, tmp)) {
        match_len = k;
        found = 1;
        break;
      }
      if (k == 0) {
        break;
      }
      k--;
    }
  } else {
    /* Shortest prefix: scan from start (smallest k). */
    for (k = 0; k <= len; ++k) {
      size_t copy_len = k < sizeof(tmp) - 1 ? k : sizeof(tmp) - 1;
      memcpy(tmp, val, copy_len);
      tmp[copy_len] = '\0';
      if (glob_match_full(pat, tmp)) {
        match_len = k;
        found = 1;
        break;
      }
    }
  }

  if (found) {
    copy_string(out, out_size, val + match_len);
  } else {
    copy_string(out, out_size, val);
  }
}

static void expand_positional_list(OoshShell *shell, char *out, size_t out_size) {
  int j;
  size_t out_len = 0;

  out[0] = '\0';
  if (shell == NULL) {
    return;
  }
  for (j = 0; j < shell->positional_count; ++j) {
    size_t param_len;
    if (j > 0 && out_len + 1 < out_size) {
      out[out_len++] = ' ';
      out[out_len] = '\0';
    }
    param_len = strlen(shell->positional_params[j]);
    if (out_len + param_len < out_size) {
      memcpy(out + out_len, shell->positional_params[j], param_len);
      out_len += param_len;
      out[out_len] = '\0';
    }
  }
}

static int resolve_special_name(OoshShell *shell, const char *name, char *out, size_t out_size) {
  int all_digits = 1;
  size_t k;
  size_t name_len = strlen(name);

  /* Single-char special variables. */
  if (name_len == 1) {
    switch (name[0]) {
      case '?':
        snprintf(out, out_size, "%d", shell == NULL ? 0 : shell->last_status);
        return 1;
      case '#':
        snprintf(out, out_size, "%d", shell == NULL ? 0 : shell->positional_count);
        return 1;
      case '$':
        snprintf(out, out_size, "%lld", shell == NULL ? 0LL : shell->shell_pid);
        return 1;
      case '!':
        if (shell != NULL && shell->last_bg_pid >= 0) {
          snprintf(out, out_size, "%lld", shell->last_bg_pid);
        } else {
          out[0] = '\0';
        }
        return 1;
      case '-':
        /* Minimal: return empty string (no shell option flags tracked yet). */
        out[0] = '\0';
        return 1;
      case '@':
      case '*':
        expand_positional_list(shell, out, out_size);
        return 1;
      default:
        break;
    }
  }

  /* Numeric name: positional parameter. */
  for (k = 0; k < name_len; ++k) {
    if (!isdigit((unsigned char) name[k])) {
      all_digits = 0;
      break;
    }
  }
  if (all_digits && name_len > 0) {
    int idx = atoi(name);
    out[0] = '\0';
    if (shell != NULL) {
      if (idx == 0) {
        copy_string(out, out_size, shell->program_path);
      } else if (idx >= 1 && idx <= shell->positional_count) {
        copy_string(out, out_size, shell->positional_params[idx - 1]);
      }
    }
    return 1;
  }

  return 0; /* Not a special name. */
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

  /* Single-char specials handled before the ${} / alpha path. */
  switch (raw[i]) {
    case '?':
      snprintf(out, out_size, "%d", shell == NULL ? 0 : shell->last_status);
      *index = i;
      return 0;
    case '#':
      snprintf(out, out_size, "%d", shell == NULL ? 0 : shell->positional_count);
      *index = i;
      return 0;
    case '$':
      snprintf(out, out_size, "%lld", shell == NULL ? 0LL : shell->shell_pid);
      *index = i;
      return 0;
    case '!':
      if (shell != NULL && shell->last_bg_pid >= 0) {
        snprintf(out, out_size, "%lld", shell->last_bg_pid);
      }
      *index = i;
      return 0;
    case '-':
      out[0] = '\0';
      *index = i;
      return 0;
    case '@':
    case '*':
      expand_positional_list(shell, out, out_size);
      *index = i;
      return 0;
    default:
      break;
  }

  /* Single-digit positional parameter: $0 .. $9 */
  if (isdigit((unsigned char) raw[i])) {
    int idx = raw[i] - '0';
    if (shell != NULL) {
      if (idx == 0) {
        copy_string(out, out_size, shell->program_path);
      } else if (idx >= 1 && idx <= shell->positional_count) {
        copy_string(out, out_size, shell->positional_params[idx - 1]);
      }
    }
    *index = i;
    return 0;
  }

  if (raw[i] == '{') {
    char var_name[128];
    size_t var_name_len = 0;
    char operand[OOSH_MAX_OUTPUT];
    size_t operand_len = 0;
    int length_mode = 0;
    int colon_prefix = 0;
    char op = '\0';
    char op2 = '\0';
    char val_buf[OOSH_MAX_OUTPUT];
    int is_set;

    i++; /* skip '{' */

    /* ${#var}: length-of-var.  Distinguish from ${#} (positional count). */
    if (raw[i] == '#' && raw[i + 1] != '}' && raw[i + 1] != '\0') {
      char next = raw[i + 1];
      if (isalpha((unsigned char) next) || next == '_' || isdigit((unsigned char) next)
          || next == '@' || next == '*' || next == '?' || next == '!' || next == '-' || next == '$') {
        length_mode = 1;
        i++; /* skip '#' */
      }
    }

    /* Read variable name. */
    if (isalpha((unsigned char) raw[i]) || raw[i] == '_') {
      while (isalnum((unsigned char) raw[i]) || raw[i] == '_') {
        if (var_name_len + 1 >= sizeof(var_name)) {
          snprintf(error, error_size, "variable name too long in ${...}");
          return 1;
        }
        var_name[var_name_len++] = raw[i++];
      }
    } else if (isdigit((unsigned char) raw[i])) {
      while (isdigit((unsigned char) raw[i])) {
        if (var_name_len + 1 >= sizeof(var_name)) {
          snprintf(error, error_size, "variable name too long in ${...}");
          return 1;
        }
        var_name[var_name_len++] = raw[i++];
      }
    } else {
      switch (raw[i]) {
        case '@': case '*': case '#': case '$': case '!': case '?': case '-':
          var_name[var_name_len++] = raw[i++];
          break;
        default:
          break;
      }
    }
    var_name[var_name_len] = '\0';

    /* Detect operator after name. */
    if (raw[i] == ':' && (raw[i + 1] == '-' || raw[i + 1] == '='
                          || raw[i + 1] == '+' || raw[i + 1] == '?')) {
      colon_prefix = 1;
      i++;
      op = raw[i++];
    } else if (raw[i] == '-' || raw[i] == '=' || raw[i] == '+' || raw[i] == '?') {
      op = raw[i++];
    } else if (raw[i] == '%') {
      i++;
      op = '%';
      if (raw[i] == '%') { op2 = '%'; i++; }
    } else if (raw[i] == '#' && !length_mode) {
      i++;
      op = '#';
      if (raw[i] == '#') { op2 = '#'; i++; }
    }

    /* Read operand until '}'. */
    while (raw[i] != '\0' && raw[i] != '}') {
      if (operand_len + 1 >= sizeof(operand)) {
        snprintf(error, error_size, "parameter expansion operand too long");
        return 1;
      }
      operand[operand_len++] = raw[i++];
    }
    operand[operand_len] = '\0';

    if (raw[i] != '}') {
      snprintf(error, error_size, "unterminated ${...} expansion");
      return 1;
    }
    *index = i;

    /* Resolve variable value. */
    is_set = lookup_var_value(shell, var_name, val_buf, sizeof(val_buf));

    /* ${#var}: return length. */
    if (length_mode) {
      snprintf(out, out_size, "%zu", strlen(val_buf));
      return 0;
    }

    /* No operator: simple lookup. */
    if (op == '\0') {
      copy_string(out, out_size, val_buf);
      return 0;
    }

    /* Determine condition for default/assign/alt/error operators.
       colon_prefix=1 → trigger if unset OR empty.
       colon_prefix=0 → trigger only if unset.               */
    {
      int trigger = colon_prefix ? (!is_set || val_buf[0] == '\0') : !is_set;

      switch (op) {
        case '-':
          /* ${var:-default}: if unset/empty, use operand; else use var value. */
          copy_string(out, out_size, trigger ? operand : val_buf);
          return 0;

        case '=':
          /* ${var:=default}: if unset/empty, assign operand to var and use it. */
          if (trigger) {
            if (shell != NULL) {
              oosh_shell_set_var(shell, var_name, operand, 0);
            }
            copy_string(out, out_size, operand);
          } else {
            copy_string(out, out_size, val_buf);
          }
          return 0;

        case '+':
          /* ${var:+alt}: if set (and non-empty with colon), use operand; else empty. */
          copy_string(out, out_size, trigger ? "" : operand);
          return 0;

        case '?':
          /* ${var:?message}: if unset/empty, error with message. */
          if (trigger) {
            snprintf(error, error_size, "%s: %s",
                     var_name[0] != '\0' ? var_name : "parameter",
                     operand[0] != '\0' ? operand : "parameter null or not set");
            return 1;
          }
          copy_string(out, out_size, val_buf);
          return 0;

        case '%':
          /* ${var%pat} or ${var%%pat}: suffix trim. */
          apply_suffix_trim(val_buf, operand, op2 == '%', out, out_size);
          return 0;

        case '#':
          /* ${var#pat} or ${var##pat}: prefix trim. */
          apply_prefix_trim(val_buf, operand, op2 == '#', out, out_size);
          return 0;

        default:
          copy_string(out, out_size, val_buf);
          return 0;
      }
    }
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
