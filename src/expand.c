#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arksh/expand.h"
#include "arksh/perf.h"
#include "arksh/platform.h"
#include "arksh/shell.h"

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

static const char *lookup_home_directory(ArkshShell *shell) {
  const char *home = arksh_shell_get_var(shell, "HOME");

  if (home != NULL && home[0] != '\0') {
    return home;
  }

#ifdef _WIN32
  home = arksh_shell_get_var(shell, "USERPROFILE");
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

static int clone_subshell(const ArkshShell *shell, ArkshShell **out_shell, char *error, size_t error_size) {
  ArkshShell *clone;
  size_t i;

  if (shell == NULL || out_shell == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  *out_shell = NULL;
  clone = (ArkshShell *) calloc(1, sizeof(*clone));
  if (clone == NULL) {
    snprintf(error, error_size, "unable to allocate command substitution shell");
    return 1;
  }

  *clone = *shell;
  for (i = 0; i < clone->binding_count; ++i) {
    memset(&clone->bindings[i].value, 0, sizeof(clone->bindings[i].value));
    if (arksh_value_copy(&clone->bindings[i].value, &shell->bindings[i].value) != 0) {
      size_t rollback;

      for (rollback = 0; rollback < i; ++rollback) {
        arksh_value_free(&clone->bindings[rollback].value);
      }
      free(clone);
      snprintf(error, error_size, "unable to clone value binding for command substitution");
      return 1;
    }
  }

  *out_shell = clone;
  return 0;
}

static void destroy_subshell(ArkshShell *shell) {
  size_t i;

  if (shell == NULL) {
    return;
  }

  for (i = 0; i < shell->binding_count; ++i) {
    arksh_value_free(&shell->bindings[i].value);
  }
  for (i = 0; i < shell->job_count; ++i) {
    arksh_platform_close_background_process(&shell->jobs[i].process);
  }

  free(shell);
}

static int parse_command_substitution(const char *raw, size_t *index, char *out, size_t out_size, char *error, size_t error_size) {
  char command[ARKSH_MAX_OUTPUT];
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

static int execute_command_substitution(ArkshShell *shell, const char *command, char *out, size_t out_size, char *error, size_t error_size) {
  char command_output[ARKSH_MAX_OUTPUT];
  ArkshShell *subshell = NULL;
  char saved_cwd[ARKSH_MAX_PATH];
  int have_saved_cwd = 0;
  int status;
  const char *p;

  if (shell == NULL || command == NULL || out == NULL || out_size == 0 || error == NULL || error_size == 0) {
    return 1;
  }

  /* E1-S6-T8: $(< file) — read file contents directly without spawning a subshell. */
  p = command;
  while (*p == ' ' || *p == '\t') { p++; }
  if (*p == '<') {
    p++;
    while (*p == ' ' || *p == '\t') { p++; }
    if (*p != '\0') {
      char path[ARKSH_MAX_PATH];
      size_t path_len = 0;
      while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '\n') {
        if (path_len + 1 < sizeof(path)) {
          path[path_len++] = *p;
        }
        p++;
      }
      path[path_len] = '\0';
      if (*p == '\0' || (*p != '<' && path_len > 0)) {
        FILE *f = fopen(path, "r");
        if (f == NULL) {
          snprintf(error, error_size, "$(< %s): cannot open file", path);
          return 1;
        }
        size_t total = 0;
        int c;
        while ((c = fgetc(f)) != EOF && total < out_size - 1) {
          out[total++] = (char) c;
        }
        out[total] = '\0';
        fclose(f);
        /* Trim trailing newlines, as all command substitutions do. */
        while (total > 0 && (out[total - 1] == '\n' || out[total - 1] == '\r')) {
          out[--total] = '\0';
        }
        return 0;
      }
    }
  }

  if (clone_subshell(shell, &subshell, error, error_size) != 0) {
    return 1;
  }

  if (arksh_platform_getcwd(saved_cwd, sizeof(saved_cwd)) == 0) {
    have_saved_cwd = 1;
  }

  command_output[0] = '\0';
  status = arksh_shell_execute_line(subshell, command, command_output, sizeof(command_output));
  trim_trailing_newlines(command_output);

  if (have_saved_cwd) {
    if (arksh_platform_chdir(saved_cwd) == 0) {
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

/* ---- Glob prefix matcher (for ${var/pat/repl}) ---- */

/* Try to match glob pat at the beginning of str (not necessarily the whole
   string).  Returns the number of characters consumed from str on success,
   or -1 if the pattern does not match at this position.  '*' is greedy. */
static int glob_match_prefix(const char *pat, const char *str) {
  if (pat == NULL || str == NULL) {
    return -1;
  }
  if (*pat == '\0') {
    return 0; /* empty pattern matches 0 chars */
  }
  if (*pat == '*') {
    /* Greedy: try to consume as many characters as possible first. */
    size_t max_len = strlen(str);
    long long k;
    for (k = (long long) max_len; k >= 0; --k) {
      int rest = glob_match_prefix(pat + 1, str + k);
      if (rest >= 0) {
        return (int) k + rest;
      }
    }
    return -1;
  }
  if (*pat == '?') {
    if (*str == '\0') {
      return -1;
    }
    int rest = glob_match_prefix(pat + 1, str + 1);
    if (rest < 0) {
      return -1;
    }
    return 1 + rest;
  }
  if (*pat == '\\' && *(pat + 1) != '\0') {
    pat++;
  }
  if (*pat != *str) {
    return -1;
  }
  {
    int rest = glob_match_prefix(pat + 1, str + 1);
    if (rest < 0) {
      return -1;
    }
    return 1 + rest;
  }
}

/* Apply pattern substitution: ${var/pat/repl} (global=0) or ${var//pat/repl}
   (global=1).  Puts result in out. */
static void apply_pattern_subst(const char *val, const char *pat, const char *repl,
                                int global, char *out, size_t out_size) {
  size_t out_pos = 0;
  size_t i = 0;
  size_t val_len;
  size_t repl_len;

  if (out_size == 0) {
    return;
  }
  if (val == NULL || pat == NULL) {
    copy_string(out, out_size, val != NULL ? val : "");
    return;
  }
  if (repl == NULL) {
    repl = "";
  }

  val_len  = strlen(val);
  repl_len = strlen(repl);

  while (i <= val_len) {
    int match_len = (i < val_len || pat[0] == '\0') ? glob_match_prefix(pat, val + i) : -1;
    if (match_len >= 0) {
      /* Append replacement. */
      size_t to_copy = repl_len;
      if (out_pos + to_copy >= out_size) {
        to_copy = out_size - 1 - out_pos;
      }
      memcpy(out + out_pos, repl, to_copy);
      out_pos += to_copy;

      if (match_len == 0 && i < val_len) {
        /* Zero-width match: also copy the current char to avoid infinite loop. */
        if (out_pos < out_size - 1) {
          out[out_pos++] = val[i];
        }
        i++;
      } else {
        i += (size_t) match_len;
      }

      if (!global) {
        /* Copy the rest verbatim. */
        while (i < val_len && out_pos < out_size - 1) {
          out[out_pos++] = val[i++];
        }
        break;
      }
    } else {
      if (i < val_len && out_pos < out_size - 1) {
        out[out_pos++] = val[i];
      }
      i++;
    }
  }
  out[out_pos] = '\0';
}

/* Forward declaration needed by lookup_var_value. */
static int resolve_special_name(ArkshShell *shell, const char *name, char *out, size_t out_size);

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
static int lookup_var_value(ArkshShell *shell, const char *name, char *out, size_t out_size) {
  const char *v;

  if (resolve_special_name(shell, name, out, out_size)) {
    return 1; /* special vars are always "set" */
  }
  v = arksh_shell_get_var(shell, name);
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
  char tmp[ARKSH_MAX_OUTPUT];

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

static void expand_positional_list(ArkshShell *shell, char *out, size_t out_size) {
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

static int resolve_special_name(ArkshShell *shell, const char *name, char *out, size_t out_size) {
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

static int resolve_variable(ArkshShell *shell, const char *raw, size_t *index, char *out, size_t out_size, char *error, size_t error_size) {
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
    char operand[ARKSH_MAX_OUTPUT];
    size_t operand_len = 0;
    int length_mode = 0;
    int colon_prefix = 0;
    char op = '\0';
    char op2 = '\0';
    char val_buf[ARKSH_MAX_OUTPUT];
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
    } else if (raw[i] == '/') {
      i++;
      op = '/';
      if (raw[i] == '/') { op2 = '/'; i++; }
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
      /* E1-S6-T2: nounset (-u): error on unset variable. */
      if (shell != NULL && shell->opt_nounset && !is_set &&
          var_name[0] != '@' && var_name[0] != '*' && var_name[0] != '#') {
        snprintf(error, error_size, "%s: unbound variable", var_name);
        return 1;
      }
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
              arksh_shell_set_var(shell, var_name, operand, 0);
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

        case '/': {
          /* E1-S6-T5: ${var/pat/repl} or ${var//pat/repl}: pattern substitution. */
          /* Split operand on first '/' to get pattern and replacement. */
          const char *slash = strchr(operand, '/');
          char pat[ARKSH_MAX_OUTPUT];
          const char *repl;
          if (slash != NULL) {
            size_t pat_len = (size_t)(slash - operand);
            if (pat_len >= sizeof(pat)) {
              pat_len = sizeof(pat) - 1;
            }
            memcpy(pat, operand, pat_len);
            pat[pat_len] = '\0';
            repl = slash + 1;
          } else {
            copy_string(pat, sizeof(pat), operand);
            repl = "";
          }
          apply_pattern_subst(val_buf, pat, repl, op2 == '/', out, out_size);
          return 0;
        }

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

  value = arksh_shell_get_var(shell, name);
  copy_string(out, out_size, value == NULL ? "" : value);
  return 0;
}

/* ---- Arithmetic expansion $(( )) ---- */

/*
 * Extract the arithmetic expression text from raw[*index] which must start
 * at the '$' of '$((...))'  On success *index is left at the second ')'.
 */
static int parse_arith_expansion(
  const char *raw,
  size_t *index,
  char *out,
  size_t out_size,
  char *error,
  size_t error_size
) {
  char expr[ARKSH_MAX_OUTPUT];
  size_t expr_len = 0;
  size_t i = *index + 3; /* skip '$', '(', '(' */
  int depth = 0;

  out[0] = '\0';

  while (raw[i] != '\0') {
    char c = raw[i];

    if (c == '(') {
      depth++;
      if (expr_len + 1 < sizeof(expr)) {
        expr[expr_len++] = c;
      }
      i++;
      continue;
    }

    if (c == ')') {
      if (depth > 0) {
        depth--;
        if (expr_len + 1 < sizeof(expr)) {
          expr[expr_len++] = c;
        }
        i++;
        continue;
      }
      /* depth == 0: this ')' is the first of the closing '))' */
      if (raw[i + 1] == ')') {
        *index = i + 1;
        expr[expr_len] = '\0';
        copy_string(out, out_size, expr);
        return 0;
      }
      snprintf(error, error_size, "unmatched ')' inside arithmetic expansion");
      return 1;
    }

    if (expr_len + 1 < sizeof(expr)) {
      expr[expr_len++] = c;
    }
    i++;
  }

  snprintf(error, error_size, "unterminated arithmetic expansion");
  return 1;
}

typedef struct {
  const char *expr;
  size_t pos;
  size_t len;
  ArkshShell *shell;
  int has_error;
  char error[256];
} ArithCtx;

static void arith_skip_ws(ArithCtx *ctx) {
  while (ctx->pos < ctx->len && isspace((unsigned char) ctx->expr[ctx->pos])) {
    ctx->pos++;
  }
}

/* Forward declaration. */
static long long arith_expr(ArithCtx *ctx);

static long long arith_primary(ArithCtx *ctx) {
  long long val = 0;

  arith_skip_ws(ctx);

  if (ctx->pos >= ctx->len) {
    ctx->has_error = 1;
    snprintf(ctx->error, sizeof(ctx->error), "unexpected end of arithmetic expression");
    return 0;
  }

  /* Parenthesised sub-expression. */
  if (ctx->expr[ctx->pos] == '(') {
    ctx->pos++;
    val = arith_expr(ctx);
    if (ctx->has_error) {
      return 0;
    }
    arith_skip_ws(ctx);
    if (ctx->pos < ctx->len && ctx->expr[ctx->pos] == ')') {
      ctx->pos++;
    } else {
      ctx->has_error = 1;
      snprintf(ctx->error, sizeof(ctx->error), "missing ')' in arithmetic expression");
    }
    return val;
  }

  /* Variable reference: bare name (POSIX) or $name. */
  if (ctx->expr[ctx->pos] == '$' ||
      isalpha((unsigned char) ctx->expr[ctx->pos]) ||
      ctx->expr[ctx->pos] == '_') {
    char name[128];
    size_t name_len = 0;
    const char *var_val;

    if (ctx->expr[ctx->pos] == '$') {
      ctx->pos++;
    }

    while (ctx->pos < ctx->len &&
           (isalnum((unsigned char) ctx->expr[ctx->pos]) || ctx->expr[ctx->pos] == '_')) {
      if (name_len + 1 < sizeof(name)) {
        name[name_len++] = ctx->expr[ctx->pos];
      }
      ctx->pos++;
    }
    name[name_len] = '\0';

    if (name_len == 0) {
      return 0;
    }

    var_val = arksh_shell_get_var(ctx->shell, name);
    if (var_val == NULL || var_val[0] == '\0') {
      return 0;
    }
    {
      char *endptr;
      long long num = strtoll(var_val, &endptr, 0);

      while (*endptr != '\0' && isspace((unsigned char) *endptr)) {
        endptr++;
      }
      return (*endptr == '\0') ? num : 0;
    }
  }

  /* Integer literal (decimal, 0x hex, 0 octal). */
  if (isdigit((unsigned char) ctx->expr[ctx->pos])) {
    char *endptr;
    val = strtoll(ctx->expr + ctx->pos, &endptr, 0);
    ctx->pos = (size_t) (endptr - ctx->expr);
    return val;
  }

  ctx->has_error = 1;
  snprintf(ctx->error, sizeof(ctx->error), "unexpected character '%c' in arithmetic expression",
           ctx->expr[ctx->pos]);
  return 0;
}

static long long arith_unary(ArithCtx *ctx) {
  arith_skip_ws(ctx);

  if (ctx->pos < ctx->len) {
    if (ctx->expr[ctx->pos] == '-') { ctx->pos++; return -arith_unary(ctx); }
    if (ctx->expr[ctx->pos] == '+') { ctx->pos++; return  arith_unary(ctx); }
    if (ctx->expr[ctx->pos] == '!') { ctx->pos++; return !arith_unary(ctx) ? 1 : 0; }
    if (ctx->expr[ctx->pos] == '~') { ctx->pos++; return ~arith_unary(ctx); }
  }
  return arith_primary(ctx);
}

static long long arith_multiplicative(ArithCtx *ctx) {
  long long left = arith_unary(ctx);

  for (;;) {
    char op;

    if (ctx->has_error) {
      return 0;
    }
    arith_skip_ws(ctx);
    if (ctx->pos >= ctx->len) {
      break;
    }
    op = ctx->expr[ctx->pos];
    if (op != '*' && op != '/' && op != '%') {
      break;
    }
    /* Don't consume '**' (power) as '*' — leave it for later. */
    if (op == '*' && ctx->pos + 1 < ctx->len && ctx->expr[ctx->pos + 1] == '*') {
      break;
    }
    ctx->pos++;
    {
      long long right = arith_unary(ctx);

      if (ctx->has_error) {
        return 0;
      }
      if (op == '*') {
        left *= right;
      } else if (right == 0) {
        ctx->has_error = 1;
        snprintf(ctx->error, sizeof(ctx->error),
                 op == '/' ? "division by zero" : "modulo by zero");
        return 0;
      } else if (op == '/') {
        left /= right;
      } else {
        left %= right;
      }
    }
  }
  return left;
}

static long long arith_additive(ArithCtx *ctx) {
  long long left = arith_multiplicative(ctx);

  for (;;) {
    char op;

    if (ctx->has_error) {
      return 0;
    }
    arith_skip_ws(ctx);
    if (ctx->pos >= ctx->len) {
      break;
    }
    op = ctx->expr[ctx->pos];
    if (op != '+' && op != '-') {
      break;
    }
    ctx->pos++;
    {
      long long right = arith_multiplicative(ctx);

      if (ctx->has_error) {
        return 0;
      }
      left = (op == '+') ? left + right : left - right;
    }
  }
  return left;
}

static long long arith_shift(ArithCtx *ctx) {
  long long left = arith_additive(ctx);

  for (;;) {
    int is_left;

    if (ctx->has_error) {
      return 0;
    }
    arith_skip_ws(ctx);
    if (ctx->pos + 1 >= ctx->len) {
      break;
    }
    if (ctx->expr[ctx->pos] == '<' && ctx->expr[ctx->pos + 1] == '<') {
      is_left = 1;
    } else if (ctx->expr[ctx->pos] == '>' && ctx->expr[ctx->pos + 1] == '>') {
      is_left = 0;
    } else {
      break;
    }
    ctx->pos += 2;
    {
      long long right = arith_additive(ctx);

      if (ctx->has_error) {
        return 0;
      }
      left = is_left ? (left << right) : (left >> right);
    }
  }
  return left;
}

static long long arith_comparison(ArithCtx *ctx) {
  long long left = arith_shift(ctx);

  for (;;) {
    int is_le, is_ge, is_lt, is_gt;

    if (ctx->has_error) {
      return 0;
    }
    arith_skip_ws(ctx);
    if (ctx->pos >= ctx->len) {
      break;
    }
    is_le = (ctx->pos + 1 < ctx->len &&
             ctx->expr[ctx->pos] == '<' && ctx->expr[ctx->pos + 1] == '=');
    is_ge = (ctx->pos + 1 < ctx->len &&
             ctx->expr[ctx->pos] == '>' && ctx->expr[ctx->pos + 1] == '=');
    is_lt = (!is_le && ctx->expr[ctx->pos] == '<' &&
             (ctx->pos + 1 >= ctx->len || ctx->expr[ctx->pos + 1] != '<'));
    is_gt = (!is_ge && ctx->expr[ctx->pos] == '>' &&
             (ctx->pos + 1 >= ctx->len || ctx->expr[ctx->pos + 1] != '>'));

    if (!is_le && !is_ge && !is_lt && !is_gt) {
      break;
    }
    ctx->pos += (is_le || is_ge) ? 2 : 1;
    {
      long long right = arith_shift(ctx);

      if (ctx->has_error) {
        return 0;
      }
      if (is_le) { left = (left <= right) ? 1 : 0; }
      else if (is_ge) { left = (left >= right) ? 1 : 0; }
      else if (is_lt) { left = (left <  right) ? 1 : 0; }
      else            { left = (left >  right) ? 1 : 0; }
    }
  }
  return left;
}

static long long arith_equality(ArithCtx *ctx) {
  long long left = arith_comparison(ctx);

  for (;;) {
    int is_eq, is_ne;

    if (ctx->has_error) {
      return 0;
    }
    arith_skip_ws(ctx);
    if (ctx->pos + 1 >= ctx->len) {
      break;
    }
    is_eq = (ctx->expr[ctx->pos] == '=' && ctx->expr[ctx->pos + 1] == '=');
    is_ne = (ctx->expr[ctx->pos] == '!' && ctx->expr[ctx->pos + 1] == '=');
    if (!is_eq && !is_ne) {
      break;
    }
    ctx->pos += 2;
    {
      long long right = arith_comparison(ctx);

      if (ctx->has_error) {
        return 0;
      }
      left = is_eq ? ((left == right) ? 1 : 0) : ((left != right) ? 1 : 0);
    }
  }
  return left;
}

static long long arith_bitwise_and(ArithCtx *ctx) {
  long long left = arith_equality(ctx);

  for (;;) {
    if (ctx->has_error) {
      return 0;
    }
    arith_skip_ws(ctx);
    if (ctx->pos >= ctx->len || ctx->expr[ctx->pos] != '&') {
      break;
    }
    if (ctx->pos + 1 < ctx->len && ctx->expr[ctx->pos + 1] == '&') {
      break; /* don't consume && here */
    }
    ctx->pos++;
    {
      long long right = arith_equality(ctx);

      if (ctx->has_error) {
        return 0;
      }
      left &= right;
    }
  }
  return left;
}

static long long arith_bitwise_xor(ArithCtx *ctx) {
  long long left = arith_bitwise_and(ctx);

  for (;;) {
    if (ctx->has_error) {
      return 0;
    }
    arith_skip_ws(ctx);
    if (ctx->pos >= ctx->len || ctx->expr[ctx->pos] != '^') {
      break;
    }
    ctx->pos++;
    {
      long long right = arith_bitwise_and(ctx);

      if (ctx->has_error) {
        return 0;
      }
      left ^= right;
    }
  }
  return left;
}

static long long arith_bitwise_or(ArithCtx *ctx) {
  long long left = arith_bitwise_xor(ctx);

  for (;;) {
    if (ctx->has_error) {
      return 0;
    }
    arith_skip_ws(ctx);
    if (ctx->pos >= ctx->len || ctx->expr[ctx->pos] != '|') {
      break;
    }
    if (ctx->pos + 1 < ctx->len && ctx->expr[ctx->pos + 1] == '|') {
      break; /* don't consume || here */
    }
    ctx->pos++;
    {
      long long right = arith_bitwise_xor(ctx);

      if (ctx->has_error) {
        return 0;
      }
      left |= right;
    }
  }
  return left;
}

static long long arith_logical_and(ArithCtx *ctx) {
  long long left = arith_bitwise_or(ctx);

  for (;;) {
    if (ctx->has_error) {
      return 0;
    }
    arith_skip_ws(ctx);
    if (ctx->pos + 1 >= ctx->len ||
        ctx->expr[ctx->pos] != '&' || ctx->expr[ctx->pos + 1] != '&') {
      break;
    }
    ctx->pos += 2;
    {
      long long right = arith_bitwise_or(ctx);

      if (ctx->has_error) {
        return 0;
      }
      left = (left && right) ? 1 : 0;
    }
  }
  return left;
}

static long long arith_logical_or(ArithCtx *ctx) {
  long long left = arith_logical_and(ctx);

  for (;;) {
    if (ctx->has_error) {
      return 0;
    }
    arith_skip_ws(ctx);
    if (ctx->pos + 1 >= ctx->len ||
        ctx->expr[ctx->pos] != '|' || ctx->expr[ctx->pos + 1] != '|') {
      break;
    }
    ctx->pos += 2;
    {
      long long right = arith_logical_and(ctx);

      if (ctx->has_error) {
        return 0;
      }
      left = (left || right) ? 1 : 0;
    }
  }
  return left;
}

static long long arith_ternary(ArithCtx *ctx) {
  long long cond = arith_logical_or(ctx);

  if (ctx->has_error) {
    return 0;
  }
  arith_skip_ws(ctx);
  if (ctx->pos < ctx->len && ctx->expr[ctx->pos] == '?') {
    long long t, f;

    ctx->pos++;
    t = arith_logical_or(ctx);
    if (ctx->has_error) {
      return 0;
    }
    arith_skip_ws(ctx);
    if (ctx->pos < ctx->len && ctx->expr[ctx->pos] == ':') {
      ctx->pos++;
    } else {
      ctx->has_error = 1;
      snprintf(ctx->error, sizeof(ctx->error), "missing ':' in ternary arithmetic expression");
      return 0;
    }
    f = arith_logical_or(ctx);
    if (ctx->has_error) {
      return 0;
    }
    return cond ? t : f;
  }
  return cond;
}

static long long arith_expr(ArithCtx *ctx) {
  return arith_ternary(ctx);
}

static int evaluate_arith(ArkshShell *shell, const char *expr, long long *result, char *error, size_t error_size) {
  ArithCtx ctx;

  if (expr == NULL || result == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  memset(&ctx, 0, sizeof(ctx));
  ctx.expr = expr;
  ctx.pos = 0;
  ctx.len = strlen(expr);
  ctx.shell = shell;
  ctx.has_error = 0;

  *result = arith_expr(&ctx);

  if (ctx.has_error) {
    snprintf(error, error_size, "%s", ctx.error);
    return 1;
  }

  arith_skip_ws(&ctx);
  if (ctx.pos < ctx.len) {
    snprintf(error, error_size, "unexpected token in arithmetic expression: '%c'",
             ctx.expr[ctx.pos]);
    return 1;
  }

  return 0;
}

static int is_ifs_char(char c, const char *ifs) {
  const char *p;

  for (p = ifs; *p != '\0'; ++p) {
    if (*p == c) {
      return 1;
    }
  }
  return 0;
}

static int is_ifs_whitespace(char c, const char *ifs) {
  return is_ifs_char(c, ifs) && isspace((unsigned char) c);
}

/*
 * Apply POSIX field splitting to an expanded string.
 *
 * split_flags[i] == 1 means expanded[i] came from an unquoted parameter or
 * command substitution and is eligible to be split by IFS characters.
 * Characters with split_flags[i] == 0 (literal or quoted) are never split.
 *
 * saw_quote_open: set to 1 if the original word opened a quote context
 * (e.g. "" or "$var" inside double-quotes).  When the expanded string is
 * empty but a quote was opened, one empty field is produced.
 */
static int apply_ifs_splitting(
  const char *expanded,
  size_t expanded_len,
  const unsigned char *split_flags,
  int saw_quote_open,
  const char *ifs,
  char out_values[][ARKSH_MAX_TOKEN],
  int max_values,
  int *out_count,
  char *error,
  size_t error_size
) {
  char field[ARKSH_MAX_TOKEN];
  size_t field_len = 0;
  size_t i;
  int after_nonwhite_ifs = 0;

  *out_count = 0;
  field[0] = '\0';

  if (ifs == NULL || ifs[0] == '\0') {
    /* IFS="" → no field splitting at all. */
    if (expanded_len > 0 || saw_quote_open) {
      copy_string(out_values[0], sizeof(out_values[0]), expanded);
      *out_count = 1;
    }
    return 0;
  }

  for (i = 0; i < expanded_len; ++i) {
    char c = expanded[i];
    int splittable = (int) split_flags[i];
    int split_here = splittable && is_ifs_char(c, ifs);

    if (split_here) {
      int is_white = is_ifs_whitespace(c, ifs);

      if (!is_white) {
        /* Non-whitespace IFS char: always a field terminator. */
        if (*out_count >= max_values) {
          snprintf(error, error_size, "too many fields after IFS splitting");
          return 1;
        }
        field[field_len] = '\0';
        copy_string(out_values[*out_count], sizeof(out_values[*out_count]), field);
        (*out_count)++;
        field_len = 0;
        after_nonwhite_ifs = 1;
      } else {
        /* Whitespace IFS char: terminates a non-empty field, skips otherwise. */
        if (field_len > 0) {
          if (*out_count >= max_values) {
            snprintf(error, error_size, "too many fields after IFS splitting");
            return 1;
          }
          field[field_len] = '\0';
          copy_string(out_values[*out_count], sizeof(out_values[*out_count]), field);
          (*out_count)++;
          field_len = 0;
        }
        after_nonwhite_ifs = 0;
      }
    } else {
      after_nonwhite_ifs = 0;
      if (field_len + 1 < sizeof(field)) {
        field[field_len++] = c;
      }
    }
  }

  /* Emit any remaining non-empty field. */
  if (field_len > 0) {
    if (*out_count >= max_values) {
      snprintf(error, error_size, "too many fields after IFS splitting");
      return 1;
    }
    field[field_len] = '\0';
    copy_string(out_values[*out_count], sizeof(out_values[*out_count]), field);
    (*out_count)++;
  } else if (after_nonwhite_ifs) {
    /* Trailing non-whitespace IFS (e.g. "a:") → emit trailing empty field. */
    if (*out_count >= max_values) {
      snprintf(error, error_size, "too many fields after IFS splitting");
      return 1;
    }
    copy_string(out_values[*out_count], sizeof(out_values[*out_count]), "");
    (*out_count)++;
  }

  /* An empty quoted string (e.g. "") must yield one empty field. */
  if (*out_count == 0 && saw_quote_open) {
    copy_string(out_values[0], sizeof(out_values[0]), "");
    *out_count = 1;
  }

  return 0;
}

static int expand_raw_token(
  ArkshShell *shell,
  const char *raw,
  ArkshExpandMode mode,
  char *expanded,
  size_t expanded_size,
  unsigned char *split_flags,
  int *saw_quote_open,
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
  if (saw_quote_open != NULL) {
    *saw_quote_open = 0;
  }

  if (raw == NULL) {
    return 0;
  }

  while (raw[i] != '\0') {
    char c = raw[i];
    int allow_glob = (mode == ARKSH_EXPAND_MODE_COMMAND && quote == '\0');

    if (quote == '\0') {
      if ((c == '\'' || c == '"')) {
        quote = c;
        saw_prefix_fragment = 1;
        if (saw_quote_open != NULL) {
          *saw_quote_open = 1;
        }
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
        char substitution[ARKSH_MAX_OUTPUT];
        size_t sub_start = expanded_len;
        int is_arith = 0;

        if (raw[i + 1] == '(' && raw[i + 2] == '(') {
          char arith_str[ARKSH_MAX_OUTPUT];
          long long arith_val;
          is_arith = 1;
          if (parse_arith_expansion(raw, &i, arith_str, sizeof(arith_str), error, error_size) != 0 ||
              evaluate_arith(shell, arith_str, &arith_val, error, error_size) != 0) {
            return 1;
          }
          snprintf(substitution, sizeof(substitution), "%lld", arith_val);
        } else if (raw[i + 1] == '(') {
          char command[ARKSH_MAX_OUTPUT];

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
        /* Mark chars from unquoted variable/command substitution as field-splittable.
           Arithmetic expansion results are not field-split (POSIX). */
        if (!is_arith && split_flags != NULL) {
          size_t k;
          for (k = sub_start; k < expanded_len; ++k) {
            split_flags[k] = 1;
          }
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
        char next;

        if (raw[i + 1] == '\0') {
          snprintf(error, error_size, "dangling escape inside double quotes");
          return 1;
        }
        next = raw[i + 1];
        /* E1-S7-T8a: POSIX §2.2.3 — inside double quotes, backslash only
           escapes $, `, ", \, and a literal newline.  For all other chars
           the backslash is retained so that "\n" stays as the two-character
           sequence \n and printf can process it. */
        if (next == '$' || next == '`' || next == '"' || next == '\\' || next == '\n') {
          c = next;
          if (append_char(expanded, expanded_size, &expanded_len, c, error, error_size, "expanded token") != 0 ||
              append_pattern_char(pattern, pattern_size, &pattern_len, c, 0, has_glob, error, error_size) != 0) {
            return 1;
          }
          i += 2;
        } else {
          /* Keep both backslash and next character. */
          if (append_char(expanded, expanded_size, &expanded_len, '\\', error, error_size, "expanded token") != 0 ||
              append_pattern_char(pattern, pattern_size, &pattern_len, '\\', 0, has_glob, error, error_size) != 0 ||
              append_char(expanded, expanded_size, &expanded_len, next, error, error_size, "expanded token") != 0 ||
              append_pattern_char(pattern, pattern_size, &pattern_len, next, 0, has_glob, error, error_size) != 0) {
            return 1;
          }
          i += 2;
        }
        saw_prefix_fragment = 1;
        continue;
      }
      if (c == '$') {
        char substitution[ARKSH_MAX_OUTPUT];

        if (raw[i + 1] == '(' && raw[i + 2] == '(') {
          char arith_str[ARKSH_MAX_OUTPUT];
          long long arith_val;
          if (parse_arith_expansion(raw, &i, arith_str, sizeof(arith_str), error, error_size) != 0 ||
              evaluate_arith(shell, arith_str, &arith_val, error, error_size) != 0) {
            return 1;
          }
          snprintf(substitution, sizeof(substitution), "%lld", arith_val);
        } else if (raw[i + 1] == '(') {
          char command[ARKSH_MAX_OUTPUT];

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

int arksh_expand_word(
  ArkshShell *shell,
  const char *raw,
  ArkshExpandMode mode,
  char out_values[][ARKSH_MAX_TOKEN],
  int max_values,
  int *out_count,
  char *error,
  size_t error_size
) {
  char expanded[ARKSH_MAX_TOKEN];
  unsigned char split_flags[ARKSH_MAX_TOKEN];
  char pattern[ARKSH_MAX_TOKEN * 2];
  int has_glob = 0;
  int saw_quote_open = 0;

  if (out_values == NULL || max_values <= 0 || out_count == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  *out_count = 0;
  memset(split_flags, 0, sizeof(split_flags));

  if (expand_raw_token(shell, raw, mode, expanded, sizeof(expanded), split_flags, &saw_quote_open, pattern, sizeof(pattern), &has_glob, error, error_size) != 0) {
    return 1;
  }

  if (mode == ARKSH_EXPAND_MODE_COMMAND && has_glob) {
    int match_count = 0;
    int glob_status;

    glob_status = arksh_platform_glob(pattern, out_values, max_values, &match_count);
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

  /* Apply IFS field splitting for command arguments. */
  if (mode == ARKSH_EXPAND_MODE_COMMAND) {
    const char *ifs = arksh_shell_get_var(shell, "IFS");
    size_t exp_len = strlen(expanded);

    if (ifs == NULL) {
      ifs = " \t\n";
    }
    return apply_ifs_splitting(expanded, exp_len, split_flags, saw_quote_open, ifs,
                               out_values, max_values, out_count, error, error_size);
  }

  copy_string(out_values[0], sizeof(out_values[0]), expanded);
  *out_count = 1;
  return 0;
}
