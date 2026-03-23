#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#define getcwd _getcwd
#define popen _popen
#define pclose _pclose
#define PATH_SEP '\\'
#else
#include <unistd.h>
#define PATH_SEP '/'
#endif

#include "arksh/plugin.h"

#define GIT_BRANCH_MAX 128

typedef struct {
  int inside_repo;
  int detached;
  int dirty;
  int untracked;
  int conflicts;
  int ahead;
  int behind;
  char root[ARKSH_MAX_PATH];
  char git_dir[ARKSH_MAX_PATH];
  char branch[GIT_BRANCH_MAX];
  char state[8];
} GitPromptInfo;

static void copy_string(char *dest, size_t dest_size, const char *src) {
  if (dest_size == 0) {
    return;
  }
  snprintf(dest, dest_size, "%s", src == NULL ? "" : src);
}

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

static int path_is_absolute(const char *path) {
  if (path == NULL || path[0] == '\0') {
    return 0;
  }
#ifdef _WIN32
  return path[0] == '\\' || path[0] == '/' ||
         (isalpha((unsigned char) path[0]) && path[1] == ':');
#else
  return path[0] == '/';
#endif
}

static void join_path(const char *left, const char *right, char *out, size_t out_size) {
  size_t left_len;

  if (out == NULL || out_size == 0) {
    return;
  }
  if (left == NULL || left[0] == '\0') {
    copy_string(out, out_size, right);
    return;
  }
  if (right == NULL || right[0] == '\0') {
    copy_string(out, out_size, left);
    return;
  }

  left_len = strlen(left);
  if (left[left_len - 1] == '/' || left[left_len - 1] == '\\') {
    snprintf(out, out_size, "%s%s", left, right);
  } else {
    snprintf(out, out_size, "%s%c%s", left, PATH_SEP, right);
  }
}

static int file_exists(const char *path, int *is_dir) {
  struct stat st;

  if (path == NULL || stat(path, &st) != 0) {
    return 0;
  }
  if (is_dir != NULL) {
#ifdef _WIN32
    *is_dir = (st.st_mode & _S_IFDIR) != 0;
#else
    *is_dir = S_ISDIR(st.st_mode);
#endif
  }
  return 1;
}

static int is_fs_root(const char *path) {
  size_t len;

  if (path == NULL || path[0] == '\0') {
    return 1;
  }
#ifdef _WIN32
  len = strlen(path);
  if (len == 3 && isalpha((unsigned char) path[0]) && path[1] == ':' &&
      (path[2] == '\\' || path[2] == '/')) {
    return 1;
  }
#endif
  return strcmp(path, "/") == 0;
}

static void dirname_copy(const char *path, char *out, size_t out_size) {
  size_t len;

  if (out == NULL || out_size == 0) {
    return;
  }
  if (path == NULL || path[0] == '\0') {
    copy_string(out, out_size, ".");
    return;
  }

  copy_string(out, out_size, path);
  len = strlen(out);

  while (len > 1 && (out[len - 1] == '/' || out[len - 1] == '\\')) {
    out[len - 1] = '\0';
    len--;
  }

  while (len > 1 && out[len - 1] != '/' && out[len - 1] != '\\') {
    out[len - 1] = '\0';
    len--;
  }

#ifdef _WIN32
  if (len == 3 && isalpha((unsigned char) out[0]) && out[1] == ':' &&
      (out[2] == '\\' || out[2] == '/')) {
    return;
  }
#endif

  while (len > 1 && (out[len - 1] == '/' || out[len - 1] == '\\')) {
    out[len - 1] = '\0';
    len--;
  }

  if (out[0] == '\0') {
    copy_string(out, out_size, "/");
  }
}

static int read_text_file_line(const char *path, char *out, size_t out_size) {
  FILE *fp;

  if (path == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  fp = fopen(path, "rb");
  if (fp == NULL) {
    return 1;
  }

  if (fgets(out, (int) out_size, fp) == NULL) {
    fclose(fp);
    out[0] = '\0';
    return 1;
  }

  fclose(fp);
  trim_in_place(out);
  return 0;
}

static void resolve_git_dir(const char *repo_root, const char *git_ref, char *out, size_t out_size) {
  if (path_is_absolute(git_ref)) {
    copy_string(out, out_size, git_ref);
  } else {
    join_path(repo_root, git_ref, out, out_size);
  }
}

static int find_git_repository(const char *start, GitPromptInfo *info) {
  char current[ARKSH_MAX_PATH];

  if (start == NULL || info == NULL) {
    return 1;
  }

  copy_string(current, sizeof(current), start);
  while (current[0] != '\0') {
    char dot_git[ARKSH_MAX_PATH];
    int is_dir = 0;

    join_path(current, ".git", dot_git, sizeof(dot_git));
    if (file_exists(dot_git, &is_dir)) {
      if (is_dir) {
        info->inside_repo = 1;
        copy_string(info->root, sizeof(info->root), current);
        copy_string(info->git_dir, sizeof(info->git_dir), dot_git);
        return 0;
      } else {
        char git_file[ARKSH_MAX_OUTPUT];

        if (read_text_file_line(dot_git, git_file, sizeof(git_file)) == 0 &&
            strncmp(git_file, "gitdir:", 7) == 0) {
          char git_dir[ARKSH_MAX_PATH];

          trim_in_place(git_file + 7);
          resolve_git_dir(current, git_file + 7, git_dir, sizeof(git_dir));
          info->inside_repo = 1;
          copy_string(info->root, sizeof(info->root), current);
          copy_string(info->git_dir, sizeof(info->git_dir), git_dir);
          return 0;
        }
      }
    }

    if (is_fs_root(current)) {
      break;
    }

    {
      char parent[ARKSH_MAX_PATH];

      dirname_copy(current, parent, sizeof(parent));
      if (strcmp(parent, current) == 0) {
        break;
      }
      copy_string(current, sizeof(current), parent);
    }
  }

  return 1;
}

static void shorten_branch_from_ref(const char *ref, char *out, size_t out_size) {
  const char *prefix = "refs/heads/";
  const char *short_ref = ref;

  if (strncmp(ref, prefix, strlen(prefix)) == 0) {
    short_ref = ref + strlen(prefix);
  }
  copy_string(out, out_size, short_ref);
}

static int read_git_head(GitPromptInfo *info) {
  char head_path[ARKSH_MAX_PATH];
  char head_line[ARKSH_MAX_OUTPUT];

  if (info == NULL || info->git_dir[0] == '\0') {
    return 1;
  }

  join_path(info->git_dir, "HEAD", head_path, sizeof(head_path));
  if (read_text_file_line(head_path, head_line, sizeof(head_line)) != 0) {
    return 1;
  }

  if (strncmp(head_line, "ref:", 4) == 0) {
    char *ref = head_line + 4;
    trim_in_place(ref);
    shorten_branch_from_ref(ref, info->branch, sizeof(info->branch));
    info->detached = 0;
    return 0;
  }

  info->detached = 1;
  if (strlen(head_line) > 7) {
    head_line[7] = '\0';
  }
  copy_string(info->branch, sizeof(info->branch), head_line);
  return 0;
}

static void escape_double_quoted_arg(const char *src, char *out, size_t out_size) {
  size_t i;
  size_t len = 0;

  if (out == NULL || out_size == 0) {
    return;
  }
  out[0] = '\0';
  if (src == NULL) {
    return;
  }

  for (i = 0; src[i] != '\0' && len + 2 < out_size; ++i) {
    if (src[i] == '"' || src[i] == '\\') {
      out[len++] = '\\';
    }
    out[len++] = src[i];
  }
  out[len] = '\0';
}

static int parse_status_line(GitPromptInfo *info, const char *line) {
  if (info == NULL || line == NULL) {
    return 1;
  }

  if (line[0] == '\0') {
    return 0;
  }

  if (line[0] == '#' && line[1] == '#') {
    if (strstr(line, "ahead ") != NULL) {
      info->ahead = 1;
    }
    if (strstr(line, "behind ") != NULL) {
      info->behind = 1;
    }
    return 0;
  }

  if (line[0] == '?' && line[1] == '?') {
    info->dirty = 1;
    info->untracked = 1;
    return 0;
  }

  if (line[0] == 'U' || line[1] == 'U' ||
      (line[0] == 'A' && line[1] == 'A') ||
      (line[0] == 'D' && line[1] == 'D')) {
    info->conflicts = 1;
    return 0;
  }

  info->dirty = 1;
  return 0;
}

static void compute_git_state_mark(GitPromptInfo *info) {
  if (info == NULL) {
    return;
  }

  if (!info->inside_repo) {
    info->state[0] = '\0';
    return;
  }
  if (info->conflicts) {
    copy_string(info->state, sizeof(info->state), "!");
    return;
  }
  if (info->dirty || info->untracked) {
    copy_string(info->state, sizeof(info->state), "*");
    return;
  }
  if (info->ahead && info->behind) {
    copy_string(info->state, sizeof(info->state), "~");
    return;
  }
  if (info->ahead) {
    copy_string(info->state, sizeof(info->state), "^");
    return;
  }
  if (info->behind) {
    copy_string(info->state, sizeof(info->state), "v");
    return;
  }
  if (info->detached) {
    copy_string(info->state, sizeof(info->state), ":");
    return;
  }
  copy_string(info->state, sizeof(info->state), "=");
}

static void inspect_git_status(GitPromptInfo *info) {
  char escaped_git_dir[ARKSH_MAX_PATH * 2];
  char escaped_root[ARKSH_MAX_PATH * 2];
  char command[ARKSH_MAX_OUTPUT];
  char line[ARKSH_MAX_OUTPUT];
  FILE *fp;

  if (info == NULL || !info->inside_repo || info->root[0] == '\0' ||
      info->git_dir[0] == '\0') {
    return;
  }

  escape_double_quoted_arg(info->git_dir, escaped_git_dir, sizeof(escaped_git_dir));
  escape_double_quoted_arg(info->root, escaped_root, sizeof(escaped_root));
#ifdef _WIN32
  snprintf(command, sizeof(command),
           "git --git-dir=\"%s\" --work-tree=\"%s\" status --porcelain --branch 2>NUL",
           escaped_git_dir, escaped_root);
#else
  snprintf(command, sizeof(command),
           "git --git-dir=\"%s\" --work-tree=\"%s\" status --porcelain --branch 2>/dev/null",
           escaped_git_dir, escaped_root);
#endif

  fp = popen(command, "r");
  if (fp == NULL) {
    return;
  }

  while (fgets(line, sizeof(line), fp) != NULL) {
    trim_in_place(line);
    parse_status_line(info, line);
  }

  pclose(fp);
}

static int load_git_prompt_info(GitPromptInfo *info) {
  char cwd[ARKSH_MAX_PATH];

  if (info == NULL) {
    return 1;
  }

  memset(info, 0, sizeof(*info));
  if (getcwd(cwd, sizeof(cwd)) == NULL) {
    return 1;
  }
  if (find_git_repository(cwd, info) != 0) {
    return 1;
  }
  if (read_git_head(info) != 0) {
    return 1;
  }

  inspect_git_status(info);
  compute_git_state_mark(info);
  return 0;
}

static int git_map_add_string(ArkshValue *map, const char *key, const char *text) {
  ArkshValue entry;
  int status;

  arksh_value_set_string(&entry, text);
  status = arksh_value_map_set(map, key, &entry);
  arksh_value_free(&entry);
  return status;
}

static int git_map_add_bool(ArkshValue *map, const char *key, int value) {
  ArkshValue entry;
  int status;

  arksh_value_set_boolean(&entry, value);
  status = arksh_value_map_set(map, key, &entry);
  arksh_value_free(&entry);
  return status;
}

static int git_string_resolver_common(
  ArkshValue *out_value,
  const char *mode,
  char *error,
  size_t error_size
) {
  GitPromptInfo info;
  char combined[ARKSH_MAX_OUTPUT];

  if (out_value == NULL || mode == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  if (load_git_prompt_info(&info) != 0) {
    arksh_value_set_string(out_value, "");
    return 0;
  }

  if (strcmp(mode, "branch") == 0) {
    arksh_value_set_string(out_value, info.branch);
    return 0;
  }
  if (strcmp(mode, "state") == 0) {
    arksh_value_set_string(out_value, info.state);
    return 0;
  }

  snprintf(combined, sizeof(combined), "%s%s", info.branch, info.state);
  arksh_value_set_string(out_value, combined);
  return 0;
}

static int git_prompt_resolver(
  ArkshShell *shell,
  int argc,
  const ArkshValue *args,
  ArkshValue *out_value,
  char *error,
  size_t error_size
) {
  (void) shell;
  (void) args;

  if (argc != 0) {
    snprintf(error, error_size, "git() does not accept arguments");
    return 1;
  }
  return git_string_resolver_common(out_value, "prompt", error, error_size);
}

static int git_branch_resolver(
  ArkshShell *shell,
  int argc,
  const ArkshValue *args,
  ArkshValue *out_value,
  char *error,
  size_t error_size
) {
  (void) args;

  if (argc != 0) {
    snprintf(error, error_size, "git_branch() does not accept arguments");
    return 1;
  }
  return git_string_resolver_common(out_value, "branch", error, error_size);
}

static int git_state_resolver(
  ArkshShell *shell,
  int argc,
  const ArkshValue *args,
  ArkshValue *out_value,
  char *error,
  size_t error_size
) {
  (void) args;

  if (argc != 0) {
    snprintf(error, error_size, "git_state() does not accept arguments");
    return 1;
  }
  return git_string_resolver_common(out_value, "state", error, error_size);
}

static int git_info_resolver(
  ArkshShell *shell,
  int argc,
  const ArkshValue *args,
  ArkshValue *out_value,
  char *error,
  size_t error_size
) {
  GitPromptInfo info;

  (void) shell;
  (void) args;

  if (out_value == NULL || error == NULL || error_size == 0) {
    return 1;
  }
  if (argc != 0) {
    snprintf(error, error_size, "git_info() does not accept arguments");
    return 1;
  }

  arksh_value_set_map(out_value);
  if (load_git_prompt_info(&info) != 0) {
    if (git_map_add_bool(out_value, "inside_repo", 0) != 0) {
      snprintf(error, error_size, "git_info() result is too large");
      return 1;
    }
    return 0;
  }

  if (git_map_add_bool(out_value, "inside_repo", 1) != 0 ||
      git_map_add_string(out_value, "root", info.root) != 0 ||
      git_map_add_string(out_value, "git_dir", info.git_dir) != 0 ||
      git_map_add_string(out_value, "branch", info.branch) != 0 ||
      git_map_add_string(out_value, "state", info.state) != 0 ||
      git_map_add_bool(out_value, "detached", info.detached) != 0 ||
      git_map_add_bool(out_value, "dirty", info.dirty) != 0 ||
      git_map_add_bool(out_value, "untracked", info.untracked) != 0 ||
      git_map_add_bool(out_value, "conflicts", info.conflicts) != 0 ||
      git_map_add_bool(out_value, "ahead", info.ahead) != 0 ||
      git_map_add_bool(out_value, "behind", info.behind) != 0) {
    snprintf(error, error_size, "git_info() result is too large");
    return 1;
  }

  return 0;
}

ARKSH_PLUGIN_EXPORT int arksh_plugin_query(ArkshPluginInfo *out_info) {
  if (out_info == NULL) {
    return 1;
  }

  memset(out_info, 0, sizeof(*out_info));
  snprintf(out_info->name, sizeof(out_info->name), "git-prompt-plugin");
  snprintf(out_info->version, sizeof(out_info->version), "0.1.0");
  snprintf(out_info->description, sizeof(out_info->description), "Git branch and state resolvers for prompt segments");
  out_info->abi_major = ARKSH_PLUGIN_ABI_MAJOR;
  out_info->abi_minor = ARKSH_PLUGIN_ABI_MINOR;
  out_info->required_host_capabilities = ARKSH_PLUGIN_CAP_VALUE_RESOLVERS;
  out_info->plugin_capabilities = ARKSH_PLUGIN_CAP_VALUE_RESOLVERS;
  return 0;
}

ARKSH_PLUGIN_EXPORT int arksh_plugin_init(ArkshShell *shell, const ArkshPluginHost *host, ArkshPluginInfo *out_info) {
  if (shell == NULL || host == NULL || out_info == NULL) {
    return 1;
  }

  if (host->abi_major != ARKSH_PLUGIN_ABI_MAJOR ||
      host->abi_minor < ARKSH_PLUGIN_ABI_MINOR ||
      host->register_value_resolver == NULL) {
    return 1;
  }

  if (arksh_plugin_query(out_info) != 0) {
    return 1;
  }

  if (host->register_value_resolver(shell, "git", "current Git branch + state mark, suitable for prompt segments", git_prompt_resolver) != 0) {
    return 1;
  }
  if (host->register_value_resolver(shell, "git_branch", "current Git branch name for the working directory", git_branch_resolver) != 0) {
    return 1;
  }
  if (host->register_value_resolver(shell, "git_state", "single-character Git state mark for the working directory", git_state_resolver) != 0) {
    return 1;
  }
  return host->register_value_resolver(shell, "git_info", "Git repository info map (branch, state, root, dirty, ahead, behind)", git_info_resolver);
}
