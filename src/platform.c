#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <sys/stat.h>
#include <tlhelp32.h>
#include <windows.h>
#else
#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <glob.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "arksh/ast.h"
#include "arksh/perf.h"
#include "arksh/platform.h"

#ifndef _WIN32
extern char **environ;
#endif

static char g_last_error[256];

static void set_last_error(const char *message) {
  if (message == NULL) {
    g_last_error[0] = '\0';
    return;
  }

  snprintf(g_last_error, sizeof(g_last_error), "%s", message);
}

static void copy_string(char *dest, size_t dest_size, const char *src) {
  if (dest_size == 0) {
    return;
  }

  snprintf(dest, dest_size, "%s", src == NULL ? "" : src);
}

static int is_separator(char c) {
  return c == '/' || c == '\\';
}

#ifdef _WIN32
static int is_windows_drive_path(const char *path) {
  return path != NULL && isalpha((unsigned char) path[0]) && path[1] == ':';
}
#endif

static int is_absolute_path(const char *path) {
  if (path == NULL || path[0] == '\0') {
    return 0;
  }

#ifdef _WIN32
  if (is_windows_drive_path(path) && is_separator(path[2])) {
    return 1;
  }
  if (is_separator(path[0]) && is_separator(path[1])) {
    return 1;
  }
  return 0;
#else
  return path[0] == '/';
#endif
}

static void normalize_separators(const char *src, char *dest, size_t dest_size) {
  size_t i;
  char native_sep = arksh_platform_path_separator()[0];

  if (dest_size == 0) {
    return;
  }

  for (i = 0; i + 1 < dest_size && src != NULL && src[i] != '\0'; ++i) {
    dest[i] = is_separator(src[i]) ? native_sep : src[i];
  }

  dest[i] = '\0';
}

static int push_segment(char segments[][ARKSH_MAX_NAME], int *count, const char *segment) {
  if (*count >= 64) {
    return 1;
  }

  copy_string(segments[*count], ARKSH_MAX_NAME, segment);
  (*count)++;
  return 0;
}

static int normalize_path(const char *input, char *out, size_t out_size) {
  char temp[ARKSH_MAX_PATH];
  char segments[64][ARKSH_MAX_NAME];
  char prefix[16] = "";
  char *cursor;
  int count = 0;
  int absolute = 0;
  size_t pos = 0;
  char sep = arksh_platform_path_separator()[0];
  size_t i;

  if (input == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  normalize_separators(input, temp, sizeof(temp));
#ifdef _WIN32
  if (is_windows_drive_path(temp)) {
    prefix[0] = temp[0];
    prefix[1] = ':';
    prefix[2] = '\0';
    pos = 2;
    if (temp[pos] == sep) {
      absolute = 1;
      pos++;
    }
  } else if (temp[0] == sep && temp[1] == sep) {
    copy_string(prefix, sizeof(prefix), "\\\\");
    absolute = 1;
    pos = 2;
  } else if (temp[0] == sep) {
    absolute = 1;
    pos = 1;
  }
#else
  if (temp[0] == sep) {
    absolute = 1;
    pos = 1;
  }
#endif

  cursor = temp + pos;
  {
    char token[ARKSH_MAX_NAME];
    size_t token_len = 0;

    for (i = 0; i <= strlen(cursor); ++i) {
      char c = cursor[i];
      int end = (c == '\0' || c == sep);

      if (!end) {
        if (token_len + 1 < sizeof(token)) {
          token[token_len++] = c;
        }
        continue;
      }

      token[token_len] = '\0';
      if (token_len > 0) {
        if (strcmp(token, ".") == 0) {
          token_len = 0;
        } else if (strcmp(token, "..") == 0) {
          if (count > 0 && strcmp(segments[count - 1], "..") != 0) {
            count--;
          } else if (!absolute) {
            if (push_segment(segments, &count, "..") != 0) {
              return 1;
            }
          }
        } else if (push_segment(segments, &count, token) != 0) {
          return 1;
        }
      }
      token_len = 0;
    }
  }

  out[0] = '\0';

  if (prefix[0] != '\0') {
    snprintf(out + strlen(out), out_size - strlen(out), "%s", prefix);
  }

  if (absolute) {
    if (!(strlen(out) > 0 && out[strlen(out) - 1] == sep)) {
      snprintf(out + strlen(out), out_size - strlen(out), "%c", sep);
    }
  }

  if (count == 0) {
    if (out[0] == '\0') {
      copy_string(out, out_size, absolute ? arksh_platform_path_separator() : ".");
    }
    return 0;
  }

  for (i = 0; i < (size_t) count; ++i) {
    if (strlen(out) > 0 && out[strlen(out) - 1] != sep) {
      snprintf(out + strlen(out), out_size - strlen(out), "%c", sep);
    }
    snprintf(out + strlen(out), out_size - strlen(out), "%s", segments[i]);
  }

  return 0;
}

int arksh_platform_getcwd(char *out, size_t out_size) {
#ifdef _WIN32
  return _getcwd(out, (int) out_size) == NULL ? 1 : 0;
#else
  return getcwd(out, out_size) == NULL ? 1 : 0;
#endif
}

int arksh_platform_chdir(const char *path) {
#ifdef _WIN32
  return _chdir(path);
#else
  return chdir(path);
#endif
}

int arksh_platform_gethostname(char *out, size_t out_size) {
#ifdef _WIN32
  DWORD size = (DWORD) out_size;
  return GetComputerNameA(out, &size) ? 0 : 1;
#else
  return gethostname(out, out_size) == 0 ? 0 : 1;
#endif
}

int arksh_platform_resolve_path(const char *cwd, const char *input, char *out, size_t out_size) {
  char candidate[ARKSH_MAX_PATH * 2];
  char sep = arksh_platform_path_separator()[0];

  if (cwd == NULL || input == NULL || out == NULL) {
    return 1;
  }

  if (input[0] == '\0') {
    copy_string(out, out_size, cwd);
    return 0;
  }

  if (is_absolute_path(input)) {
    return normalize_path(input, out, out_size);
  }

  if (strcmp(input, ".") == 0) {
    copy_string(out, out_size, cwd);
    return 0;
  }

  snprintf(candidate, sizeof(candidate), "%s%c%s", cwd, sep, input);
  return normalize_path(candidate, out, out_size);
}

int arksh_platform_ensure_directory(const char *path) {
  char normalized[ARKSH_MAX_PATH];
  char partial[ARKSH_MAX_PATH];
  char sep = arksh_platform_path_separator()[0];
  size_t i = 0;
  size_t partial_len = 0;

  if (path == NULL || path[0] == '\0') {
    return 1;
  }

  if (normalize_path(path, normalized, sizeof(normalized)) != 0) {
    return 1;
  }

  partial[0] = '\0';

#ifdef _WIN32
  if (is_windows_drive_path(normalized)) {
    partial[0] = normalized[0];
    partial[1] = ':';
    partial[2] = '\0';
    partial_len = 2;
    i = 2;
    if (normalized[i] == sep) {
      partial[partial_len++] = sep;
      partial[partial_len] = '\0';
      i++;
    }
  } else if (normalized[0] == sep && normalized[1] == sep) {
    partial[0] = sep;
    partial[1] = sep;
    partial[2] = '\0';
    partial_len = 2;
    i = 2;
  } else if (normalized[0] == sep) {
    partial[0] = sep;
    partial[1] = '\0';
    partial_len = 1;
    i = 1;
  }
#else
  if (normalized[0] == sep) {
    partial[0] = sep;
    partial[1] = '\0';
    partial_len = 1;
    i = 1;
  }
#endif

  while (normalized[i] != '\0') {
    if (partial_len > 0 && partial[partial_len - 1] != sep) {
      if (partial_len + 1 >= sizeof(partial)) {
        return 1;
      }
      partial[partial_len++] = sep;
      partial[partial_len] = '\0';
    }

    while (normalized[i] != '\0' && normalized[i] != sep) {
      if (partial_len + 1 >= sizeof(partial)) {
        return 1;
      }
      partial[partial_len++] = normalized[i++];
      partial[partial_len] = '\0';
    }

    if (partial[0] != '\0' && strcmp(partial, ".") != 0 && strcmp(partial, arksh_platform_path_separator()) != 0) {
#ifdef _WIN32
      if (_mkdir(partial) != 0 && errno != EEXIST) {
        return 1;
      }
#else
      if (mkdir(partial, 0777) != 0 && errno != EEXIST) {
        return 1;
      }
#endif
    }

    if (normalized[i] == sep) {
      i++;
    }
  }

  return 0;
}

void arksh_platform_basename(const char *path, char *out, size_t out_size) {
  const char *cursor;
  const char *last = NULL;
  size_t len;

  if (path == NULL || out == NULL || out_size == 0) {
    return;
  }

  len = strlen(path);
  if (len == 0) {
    copy_string(out, out_size, "");
    return;
  }

  cursor = path + len;
  while (cursor > path && is_separator(cursor[-1])) {
    cursor--;
  }

  if (cursor == path) {
    copy_string(out, out_size, arksh_platform_path_separator());
    return;
  }

  while (cursor > path) {
    if (is_separator(cursor[-1])) {
      last = cursor;
      break;
    }
    cursor--;
  }

  if (last == NULL) {
    copy_string(out, out_size, path);
  } else {
    copy_string(out, out_size, last);
  }
}

void arksh_platform_dirname(const char *path, char *out, size_t out_size) {
  char temp[ARKSH_MAX_PATH];
  size_t len;

  if (path == NULL || out == NULL || out_size == 0) {
    return;
  }

  copy_string(temp, sizeof(temp), path);
  len = strlen(temp);

  while (len > 1 && is_separator(temp[len - 1])) {
    temp[len - 1] = '\0';
    len--;
  }

  while (len > 0 && !is_separator(temp[len - 1])) {
    temp[len - 1] = '\0';
    len--;
  }

  while (len > 1 && is_separator(temp[len - 1])) {
    temp[len - 1] = '\0';
    len--;
  }

#ifdef _WIN32
  if (len == 2 && is_windows_drive_path(temp)) {
    snprintf(out, out_size, "%s\\", temp);
    return;
  }
#endif

  if (temp[0] == '\0') {
    copy_string(out, out_size, ".");
    return;
  }

  copy_string(out, out_size, temp);
}

int arksh_platform_stat(const char *path, ArkshPlatformFileInfo *info) {
  char name[ARKSH_MAX_NAME];

  if (path == NULL || info == NULL) {
    return 1;
  }

  memset(info, 0, sizeof(*info));
  arksh_platform_basename(path, name, sizeof(name));

#ifdef _WIN32
  {
    struct _stat st;
    DWORD attrs;

    if (_stat(path, &st) == 0) {
      info->exists = 1;
      info->is_directory = (st.st_mode & _S_IFDIR) != 0;
      info->is_file = (st.st_mode & _S_IFREG) != 0;
      info->size = (unsigned long long) st.st_size;
    }

    attrs = GetFileAttributesA(path);
    if (attrs != INVALID_FILE_ATTRIBUTES) {
      info->hidden = (attrs & FILE_ATTRIBUTE_HIDDEN) != 0;
    } else {
      info->hidden = name[0] == '.';
    }

    info->readable = _access(path, 4) == 0;
    info->writable = _access(path, 2) == 0;

    if (is_windows_drive_path(path) && strlen(path) <= 3) {
      info->is_mount_point = 1;
    }
  }
#else
  {
    struct stat st;
    struct stat parent_st;
    char parent[ARKSH_MAX_PATH];

    if (stat(path, &st) == 0) {
      info->exists = 1;
      info->is_directory = S_ISDIR(st.st_mode);
      info->is_file = S_ISREG(st.st_mode);
      info->is_device = S_ISCHR(st.st_mode) || S_ISBLK(st.st_mode);
      info->size = (unsigned long long) st.st_size;
      info->hidden = name[0] == '.';
      info->readable = access(path, R_OK) == 0;
      info->writable = access(path, W_OK) == 0;

      if (strcmp(path, "/") == 0) {
        info->is_mount_point = 1;
      } else if (info->is_directory) {
        arksh_platform_dirname(path, parent, sizeof(parent));
        if (stat(parent, &parent_st) == 0 && (st.st_dev != parent_st.st_dev || (st.st_dev == parent_st.st_dev && st.st_ino == parent_st.st_ino))) {
          info->is_mount_point = 1;
        }
      }
    } else {
      info->hidden = name[0] == '.';
    }
  }
#endif

  return 0;
}

int arksh_platform_list_children(const char *path, char *out, size_t out_size) {
  if (path == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  out[0] = '\0';

#ifdef _WIN32
  {
    char pattern[ARKSH_MAX_PATH];
    WIN32_FIND_DATAA data;
    HANDLE handle;
    int first = 1;

    snprintf(pattern, sizeof(pattern), "%s\\*", path);
    handle = FindFirstFileA(pattern, &data);
    if (handle == INVALID_HANDLE_VALUE) {
      snprintf(out, out_size, "unable to list children for %s", path);
      return 1;
    }

    do {
      if (strcmp(data.cFileName, ".") == 0 || strcmp(data.cFileName, "..") == 0) {
        continue;
      }
      if (!first) {
        snprintf(out + strlen(out), out_size - strlen(out), "\n");
      }
      snprintf(out + strlen(out), out_size - strlen(out), "%s", data.cFileName);
      first = 0;
    } while (FindNextFileA(handle, &data));

    FindClose(handle);
  }
#else
  {
    DIR *dir = opendir(path);
    struct dirent *entry;
    int first = 1;

    if (dir == NULL) {
      snprintf(out, out_size, "unable to list children for %s", path);
      return 1;
    }

    while ((entry = readdir(dir)) != NULL) {
      if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
        continue;
      }
      if (!first) {
        snprintf(out + strlen(out), out_size - strlen(out), "\n");
      }
      snprintf(out + strlen(out), out_size - strlen(out), "%s", entry->d_name);
      first = 0;
    }

    closedir(dir);
  }
#endif

  return 0;
}

int arksh_platform_list_children_names(const char *path, char names[][ARKSH_MAX_PATH], size_t max_names, size_t *out_count) {
  size_t count = 0;

  if (path == NULL || names == NULL || out_count == NULL) {
    return 1;
  }

#ifdef _WIN32
  {
    char pattern[ARKSH_MAX_PATH];
    WIN32_FIND_DATAA data;
    HANDLE handle;

    snprintf(pattern, sizeof(pattern), "%s\\*", path);
    handle = FindFirstFileA(pattern, &data);
    if (handle == INVALID_HANDLE_VALUE) {
      return 1;
    }

    do {
      if (strcmp(data.cFileName, ".") == 0 || strcmp(data.cFileName, "..") == 0) {
        continue;
      }
      if (count < max_names) {
        copy_string(names[count], ARKSH_MAX_PATH, data.cFileName);
        count++;
      }
    } while (FindNextFileA(handle, &data));

    FindClose(handle);
  }
#else
  {
    DIR *dir = opendir(path);
    struct dirent *entry;

    if (dir == NULL) {
      return 1;
    }

    while ((entry = readdir(dir)) != NULL) {
      if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
        continue;
      }
      if (count < max_names) {
        copy_string(names[count], ARKSH_MAX_PATH, entry->d_name);
        count++;
      }
    }

    closedir(dir);
  }
#endif

  *out_count = count;
  return 0;
}

int arksh_platform_read_text_file(const char *path, size_t limit, char *out, size_t out_size) {
  FILE *fp;
  size_t max_read;
  size_t bytes_read;

  if (path == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  fp = fopen(path, "rb");
  if (fp == NULL) {
    snprintf(out, out_size, "unable to open file: %s", path);
    return 1;
  }

  max_read = limit < out_size - 1 ? limit : out_size - 1;
  bytes_read = fread(out, 1, max_read, fp);
  out[bytes_read] = '\0';
  fclose(fp);
  return 0;
}

int arksh_platform_write_text_file(const char *path, const char *text, int append, char *out, size_t out_size) {
  FILE *fp;

  if (path == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  fp = fopen(path, append ? "ab" : "wb");
  if (fp == NULL) {
    snprintf(out, out_size, "unable to open file: %s", path);
    return 1;
  }

  if (text != NULL && text[0] != '\0') {
    fwrite(text, 1, strlen(text), fp);
  }
  fclose(fp);
  return 0;
}

int arksh_platform_list_environment(ArkshPlatformEnvEntry entries[], size_t max_entries, size_t *out_count) {
  size_t count = 0;

  if (entries == NULL || out_count == NULL) {
    return 1;
  }

#ifdef _WIN32
  {
    LPCH block = GetEnvironmentStringsA();
    LPCSTR cursor;

    if (block == NULL) {
      return 1;
    }

    cursor = block;
    while (*cursor != '\0') {
      const char *equals = strchr(cursor, '=');

      if (cursor[0] != '=' && equals != NULL && equals != cursor) {
        if (count < max_entries) {
          size_t name_len = (size_t) (equals - cursor);

          if (name_len >= sizeof(entries[count].name)) {
            name_len = sizeof(entries[count].name) - 1;
          }
          memcpy(entries[count].name, cursor, name_len);
          entries[count].name[name_len] = '\0';
          copy_string(entries[count].value, sizeof(entries[count].value), equals + 1);
          count++;
        }
      }

      cursor += strlen(cursor) + 1;
    }
    FreeEnvironmentStringsA(block);
  }
#else
  {
    size_t i;

    for (i = 0; environ != NULL && environ[i] != NULL; ++i) {
      const char *equals = strchr(environ[i], '=');

      if (equals == NULL || equals == environ[i]) {
        continue;
      }
      if (count < max_entries) {
        size_t name_len = (size_t) (equals - environ[i]);

        if (name_len >= sizeof(entries[count].name)) {
          name_len = sizeof(entries[count].name) - 1;
        }
        memcpy(entries[count].name, environ[i], name_len);
        entries[count].name[name_len] = '\0';
        copy_string(entries[count].value, sizeof(entries[count].value), equals + 1);
        count++;
      }
    }
  }
#endif

  *out_count = count;
  return 0;
}

int arksh_platform_get_process_info(ArkshPlatformProcessInfo *out_info) {
  if (out_info == NULL) {
    return 1;
  }

  memset(out_info, 0, sizeof(*out_info));

#ifdef _WIN32
  out_info->pid = GetCurrentProcessId();
  out_info->ppid = 0;
  {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32 entry;

    if (snapshot != INVALID_HANDLE_VALUE) {
      memset(&entry, 0, sizeof(entry));
      entry.dwSize = sizeof(entry);
      if (Process32First(snapshot, &entry)) {
        do {
          if (entry.th32ProcessID == out_info->pid) {
            out_info->ppid = entry.th32ParentProcessID;
            break;
          }
        } while (Process32Next(snapshot, &entry));
      }
      CloseHandle(snapshot);
    }
  }
#else
  out_info->pid = (unsigned long) getpid();
  out_info->ppid = (unsigned long) getppid();
#endif

  return 0;
}

static void build_process_argv(const ArkshPlatformProcessSpec *spec, char *argv[ARKSH_MAX_ARGS + 1]) {
  int i;

  if (spec == NULL) {
    return;
  }

  for (i = 0; i < ARKSH_MAX_ARGS + 1; ++i) {
    argv[i] = NULL;
  }

  for (i = 0; i < spec->argc && i < ARKSH_MAX_ARGS; ++i) {
    argv[i] = (char *) spec->argv[i];
  }
}

#ifdef _WIN32
static void append_text(char *dest, size_t dest_size, const char *src);
static void append_windows_quoted_arg(char *dest, size_t dest_size, const char *arg);
static int compare_string_entries(const void *lhs, const void *rhs);
static void close_handle_if_valid(HANDLE *handle);

static void build_windows_command_line_from_argv(char *const argv[], char *out, size_t out_size) {
  int i;

  if (out == NULL || out_size == 0) {
    return;
  }

  out[0] = '\0';
  if (argv == NULL) {
    return;
  }

  for (i = 0; argv[i] != NULL; ++i) {
    if (i > 0) {
      append_text(out, out_size, " ");
    }
    append_windows_quoted_arg(out, out_size, argv[i]);
  }
}

static void append_text(char *dest, size_t dest_size, const char *src) {
  size_t current_len;

  if (dest == NULL || dest_size == 0 || src == NULL) {
    return;
  }

  current_len = strlen(dest);
  if (current_len >= dest_size - 1) {
    return;
  }

  snprintf(dest + current_len, dest_size - current_len, "%s", src);
}

static void append_windows_quoted_arg(char *dest, size_t dest_size, const char *arg) {
  size_t i;
  int needs_quotes = 0;

  if (dest == NULL || dest_size == 0 || arg == NULL) {
    return;
  }

  for (i = 0; arg[i] != '\0'; ++i) {
    if (isspace((unsigned char) arg[i]) || arg[i] == '"') {
      needs_quotes = 1;
      break;
    }
  }

  if (!needs_quotes) {
    append_text(dest, dest_size, arg);
    return;
  }

  append_text(dest, dest_size, "\"");
  for (i = 0; arg[i] != '\0'; ++i) {
    char fragment[3] = {0};

    if (arg[i] == '"' || arg[i] == '\\') {
      append_text(dest, dest_size, "\\");
    }

    fragment[0] = arg[i];
    append_text(dest, dest_size, fragment);
  }
  append_text(dest, dest_size, "\"");
}

static void build_windows_command_line(const ArkshPlatformProcessSpec *spec, char *out, size_t out_size) {
  int i;

  if (out == NULL || out_size == 0) {
    return;
  }

  out[0] = '\0';
  if (spec == NULL) {
    return;
  }

  for (i = 0; i < spec->argc && i < ARKSH_MAX_ARGS; ++i) {
    if (i > 0) {
      append_text(out, out_size, " ");
    }
    append_windows_quoted_arg(out, out_size, spec->argv[i]);
  }
}

static int compare_string_entries(const void *lhs, const void *rhs) {
  const char *left = (const char *) lhs;
  const char *right = (const char *) rhs;

  return strcmp(left, right);
}

static void close_handle_if_valid(HANDLE *handle) {
  if (handle == NULL || *handle == NULL || *handle == INVALID_HANDLE_VALUE) {
    return;
  }

  CloseHandle(*handle);
  *handle = INVALID_HANDLE_VALUE;
}

static int read_handle_to_buffer(HANDLE handle, char *out, size_t out_size) {
  char chunk[512];
  DWORD bytes_read = 0;
  size_t used = 0;

  if (out == NULL || out_size == 0 || handle == NULL || handle == INVALID_HANDLE_VALUE) {
    return 1;
  }

  out[0] = '\0';
  while (ReadFile(handle, chunk, sizeof(chunk), &bytes_read, NULL) && bytes_read > 0) {
    size_t copy_len = bytes_read;

    if (used < out_size - 1) {
      if (copy_len > out_size - 1 - used) {
        copy_len = out_size - 1 - used;
      }
      memcpy(out + used, chunk, copy_len);
      used += copy_len;
      out[used] = '\0';
    }
  }

  return 0;
}
#else
static int read_fd_to_buffer(int fd, char *out, size_t out_size) {
  char chunk[512];
  ssize_t bytes_read;
  size_t used = 0;

  if (fd < 0 || out == NULL || out_size == 0) {
    return 1;
  }

  out[0] = '\0';
  while ((bytes_read = read(fd, chunk, sizeof(chunk))) > 0) {
    size_t copy_len = (size_t) bytes_read;

    if (used < out_size - 1) {
      if (copy_len > out_size - 1 - used) {
        copy_len = out_size - 1 - used;
      }
      memcpy(out + used, chunk, copy_len);
      used += copy_len;
      out[used] = '\0';
    }
  }

  return bytes_read < 0 ? 1 : 0;
}
#endif

int arksh_platform_glob(
  const char *pattern,
  char matches[][ARKSH_MAX_TOKEN],
  int max_matches,
  int *out_count
) {
  if (pattern == NULL || matches == NULL || max_matches <= 0 || out_count == NULL) {
    return 1;
  }

  *out_count = 0;

#ifdef _WIN32
  {
    char normalized_pattern[ARKSH_MAX_PATH];
    char prefix[ARKSH_MAX_PATH];
    const char *last_separator = NULL;
    WIN32_FIND_DATAA data;
    HANDLE handle;
    int count = 0;
    size_t i;

    normalize_separators(pattern, normalized_pattern, sizeof(normalized_pattern));
    prefix[0] = '\0';

    for (i = 0; normalized_pattern[i] != '\0'; ++i) {
      if (is_separator(normalized_pattern[i])) {
        last_separator = normalized_pattern + i;
      }
    }

    if (last_separator != NULL) {
      size_t prefix_len = (size_t) (last_separator - normalized_pattern + 1);

      if (prefix_len >= sizeof(prefix)) {
        return 1;
      }
      memcpy(prefix, normalized_pattern, prefix_len);
      prefix[prefix_len] = '\0';
    }

    handle = FindFirstFileA(normalized_pattern, &data);
    if (handle == INVALID_HANDLE_VALUE) {
      DWORD error_code = GetLastError();

      if (error_code == ERROR_FILE_NOT_FOUND || error_code == ERROR_PATH_NOT_FOUND) {
        return 0;
      }
      return 1;
    }

    do {
      if (strcmp(data.cFileName, ".") == 0 || strcmp(data.cFileName, "..") == 0) {
        continue;
      }

      if (count >= max_matches) {
        FindClose(handle);
        return 2;
      }

      if (prefix[0] != '\0') {
        char combined[ARKSH_MAX_PATH];

        snprintf(combined, sizeof(combined), "%s%s", prefix, data.cFileName);
        copy_string(matches[count], ARKSH_MAX_TOKEN, combined);
      } else {
        copy_string(matches[count], ARKSH_MAX_TOKEN, data.cFileName);
      }
      count++;
    } while (FindNextFileA(handle, &data));

    FindClose(handle);
    qsort(matches, (size_t) count, sizeof(matches[0]), compare_string_entries);
    *out_count = count;
    return 0;
  }
#else
  {
    glob_t glob_matches;
    int glob_status;
    size_t i;

    memset(&glob_matches, 0, sizeof(glob_matches));
    glob_status = glob(pattern, 0, NULL, &glob_matches);
    if (glob_status == GLOB_NOMATCH) {
      globfree(&glob_matches);
      return 0;
    }
    if (glob_status != 0) {
      globfree(&glob_matches);
      return 1;
    }
    if ((int) glob_matches.gl_pathc > max_matches) {
      globfree(&glob_matches);
      return 2;
    }

    for (i = 0; i < glob_matches.gl_pathc; ++i) {
      copy_string(matches[i], ARKSH_MAX_TOKEN, glob_matches.gl_pathv[i]);
    }
    *out_count = (int) glob_matches.gl_pathc;
    globfree(&glob_matches);
    return 0;
  }
#endif
}

int arksh_platform_run_process_pipeline(
  const char *cwd,
  const ArkshPlatformProcessSpec *specs,
  size_t spec_count,
  char *out,
  size_t out_size,
  int *out_exit_code,
  ArkshPlatformAsyncProcess *out_stopped,
  int force_capture,
  int use_pipefail
) {
  if (out == NULL || out_size == 0) {
    return 1;
  }

  out[0] = '\0';
  if (out_exit_code != NULL) {
    *out_exit_code = 0;
  }
  if (out_stopped != NULL) {
    memset(out_stopped, 0, sizeof(*out_stopped));
  }

  if (specs == NULL || spec_count == 0) {
    snprintf(out, out_size, "missing process specification");
    return 1;
  }

  /* E4-S4: Windows pipeline — POSIX portability notes
   *
   * The following POSIX primitives used in the #else branch have NO direct
   * Windows equivalent and are therefore omitted here:
   *
   *   setpgid / getpgrp     — Windows has no process-group hierarchy for
   *                            foreground pipelines.  Background processes get
   *                            CREATE_NEW_PROCESS_GROUP in spawn_background_process
   *                            so that Ctrl+Break does not reach them.
   *
   *   tcsetpgrp / tcgetpgrp — Windows consoles have no "foreground process
   *                            group" concept.  All processes attached to the
   *                            same console receive Ctrl+C simultaneously.
   *                            No TTY hand-off or restore is required.
   *
   *   WUNTRACED / WIFSTOPPED — Job suspension (Ctrl+Z / SIGTSTP) does not
   *                            exist on Windows.  out_stopped is always left
   *                            untouched (no stopped-job entry is created).
   *
   *   SIGINT / SIGQUIT /
   *   SIGTSTP / SIGPIPE      — Windows child processes do not inherit POSIX
   *                            signal dispositions.  Ctrl+C delivers a console
   *                            control event to every process in the console
   *                            session; no per-child reset is needed.
   *
   * What Windows *does* provide:
   *   _isatty(0)             — detects whether stdin is a console (mirrors
   *                            POSIX isatty(STDIN_FILENO)).  Used to decide
   *                            whether the last pipeline stage should redirect
   *                            stdout to a capture pipe (non-interactive /
   *                            force_capture) or let it flow to the console
   *                            directly (interactive, no force_capture).
   *
   *   pgid_leader            — set to dwProcessId of the first spawned stage.
   *                            Mirrors the POSIX pgid_leader so callers that
   *                            inspect out_stopped->pgid get a consistent value
   *                            if we ever need to extend job-table support. */
#ifdef _WIN32
  {
    SECURITY_ATTRIBUTES security_attributes;
    PROCESS_INFORMATION process_infos[ARKSH_MAX_PIPELINE_STAGES];
    DWORD stage_exit_codes[ARKSH_MAX_PIPELINE_STAGES];
    HANDLE previous_read = INVALID_HANDLE_VALUE;
    HANDLE capture_read = INVALID_HANDLE_VALUE;
    size_t process_count = 0;
    DWORD exit_code = 0;
    DWORD pgid_leader = 0;
    int interactive;
    size_t i;
    (void) out_stopped;
    int status = 0;

    /* E4-S4: mirror POSIX isatty(STDIN_FILENO) */
    interactive = _isatty(0);

    memset(&security_attributes, 0, sizeof(security_attributes));
    security_attributes.nLength = sizeof(security_attributes);
    security_attributes.bInheritHandle = TRUE;
    memset(process_infos, 0, sizeof(process_infos));
    memset(stage_exit_codes, 0, sizeof(stage_exit_codes));

    for (i = 0; i < spec_count; ++i) {
      STARTUPINFOA startup_info;
      PROCESS_INFORMATION process_info;
      HANDLE stdin_handle = GetStdHandle(STD_INPUT_HANDLE);
      HANDLE stdout_handle = GetStdHandle(STD_OUTPUT_HANDLE);
      HANDLE stderr_handle = GetStdHandle(STD_ERROR_HANDLE);
      HANDLE next_read = INVALID_HANDLE_VALUE;
      HANDLE next_write = INVALID_HANDLE_VALUE;
      HANDLE capture_write = INVALID_HANDLE_VALUE;
      HANDLE opened_stdin = INVALID_HANDLE_VALUE;
      HANDLE opened_stdout = INVALID_HANDLE_VALUE;
      HANDLE opened_stderr = INVALID_HANDLE_VALUE;
      HANDLE opened_redirects[ARKSH_MAX_REDIRECTIONS * 2];
      char command_line[ARKSH_MAX_LINE * 2];
      int needs_next_pipe = (i + 1 < spec_count);
      /* E4-S4: match POSIX logic — only redirect to a capture pipe when the
       * caller asks for captured output (force_capture) or when stdin is not a
       * console (script / piped mode).  In interactive mode without
       * force_capture the last stage writes directly to the console handle,
       * which is what the user expects. */
      int should_capture_output = (!interactive || force_capture) && (i + 1 == spec_count);
      int stage_spawned = 0;
      size_t opened_redirect_count = 0;
      size_t redirect_index;

      memset(&startup_info, 0, sizeof(startup_info));
      startup_info.cb = sizeof(startup_info);
      startup_info.dwFlags = STARTF_USESTDHANDLES;

      memset(&process_info, 0, sizeof(process_info));
      memset(opened_redirects, 0, sizeof(opened_redirects));
      build_windows_command_line(&specs[i], command_line, sizeof(command_line));
      if (command_line[0] == '\0') {
        snprintf(out, out_size, "empty command");
        status = 1;
        goto windows_stage_cleanup;
      }

      if (previous_read != INVALID_HANDLE_VALUE) {
        if (!SetHandleInformation(previous_read, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT)) {
          snprintf(out, out_size, "unable to prepare pipeline input handle");
          status = 1;
          goto windows_stage_cleanup;
        }
        stdin_handle = previous_read;
      }

      if (needs_next_pipe) {
        if (!CreatePipe(&next_read, &next_write, &security_attributes, 0)) {
          snprintf(out, out_size, "unable to create shell pipeline");
          status = 1;
          goto windows_stage_cleanup;
        }
        SetHandleInformation(next_read, HANDLE_FLAG_INHERIT, 0);
      }

      if (needs_next_pipe) {
        stdout_handle = next_write;
      } else if (should_capture_output) {
        if (!CreatePipe(&capture_read, &capture_write, &security_attributes, 0)) {
          snprintf(out, out_size, "unable to capture command output");
          status = 1;
          goto windows_stage_cleanup;
        }
        SetHandleInformation(capture_read, HANDLE_FLAG_INHERIT, 0);
        stdout_handle = capture_write;
      }

      for (redirect_index = 0; redirect_index < specs[i].redirection_count; ++redirect_index) {
        const ArkshPlatformRedirectionSpec *redirect = &specs[i].redirections[redirect_index];
        HANDLE *target_handle = NULL;

        if (redirect->fd < 0 || redirect->fd > 2 || redirect->target_fd > 2) {
          snprintf(out, out_size, "fd redirection above 2 is not supported on Windows yet");
          status = 1;
          goto windows_stage_cleanup;
        }

        switch (redirect->fd) {
          case 0:
            target_handle = &stdin_handle;
            break;
          case 1:
            target_handle = &stdout_handle;
            break;
          case 2:
            target_handle = &stderr_handle;
            break;
          default:
            break;
        }
        if (target_handle == NULL) {
          snprintf(out, out_size, "invalid redirection target fd");
          status = 1;
          goto windows_stage_cleanup;
        }

        if (redirect->close_target) {
          HANDLE nul_handle = CreateFileA(
            "NUL",
            redirect->fd == 0 ? GENERIC_READ : FILE_GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            &security_attributes,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            NULL
          );

          if (nul_handle == INVALID_HANDLE_VALUE) {
            snprintf(out, out_size, "unable to close redirected fd");
            status = 1;
            goto windows_stage_cleanup;
          }
          *target_handle = nul_handle;
          if (opened_redirect_count < sizeof(opened_redirects) / sizeof(opened_redirects[0])) {
            opened_redirects[opened_redirect_count++] = nul_handle;
          }
          continue;
        }

        if (redirect->text[0] != '\0') {
          HANDLE heredoc_read = INVALID_HANDLE_VALUE;
          HANDLE heredoc_write = INVALID_HANDLE_VALUE;
          DWORD written = 0;

          if (redirect->fd != 0) {
            snprintf(out, out_size, "heredoc currently supports only stdin on Windows");
            status = 1;
            goto windows_stage_cleanup;
          }
          if (!CreatePipe(&heredoc_read, &heredoc_write, &security_attributes, 0)) {
            snprintf(out, out_size, "unable to create heredoc pipe");
            status = 1;
            goto windows_stage_cleanup;
          }
          if (!WriteFile(heredoc_write, redirect->text, (DWORD) strlen(redirect->text), &written, NULL)) {
            close_handle_if_valid(&heredoc_read);
            close_handle_if_valid(&heredoc_write);
            snprintf(out, out_size, "unable to write heredoc input");
            status = 1;
            goto windows_stage_cleanup;
          }
          close_handle_if_valid(&heredoc_write);
          *target_handle = heredoc_read;
          if (opened_redirect_count < sizeof(opened_redirects) / sizeof(opened_redirects[0])) {
            opened_redirects[opened_redirect_count++] = heredoc_read;
          }
          continue;
        }

        if (redirect->path[0] != '\0') {
          DWORD access = redirect->input_mode ? GENERIC_READ : FILE_GENERIC_WRITE;
          DWORD creation = redirect->input_mode ? OPEN_EXISTING : (redirect->append_mode ? OPEN_ALWAYS : CREATE_ALWAYS);
          HANDLE opened_handle = CreateFileA(
            redirect->path,
            access,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            &security_attributes,
            creation,
            FILE_ATTRIBUTE_NORMAL,
            NULL
          );

          if (opened_handle == INVALID_HANDLE_VALUE) {
            snprintf(out, out_size, "unable to open redirected path: %s", redirect->path);
            status = 1;
            goto windows_stage_cleanup;
          }
          if (!redirect->input_mode && redirect->append_mode) {
            SetFilePointer(opened_handle, 0, NULL, FILE_END);
          }
          *target_handle = opened_handle;
          if (opened_redirect_count < sizeof(opened_redirects) / sizeof(opened_redirects[0])) {
            opened_redirects[opened_redirect_count++] = opened_handle;
          }
          continue;
        }

        if (redirect->target_fd >= 0) {
          switch (redirect->target_fd) {
            case 0:
              *target_handle = stdin_handle;
              break;
            case 1:
              *target_handle = stdout_handle;
              break;
            case 2:
              *target_handle = stderr_handle;
              break;
            default:
              snprintf(out, out_size, "invalid duplicated fd");
              status = 1;
              goto windows_stage_cleanup;
          }
        }
      }

      if (next_write != INVALID_HANDLE_VALUE && stdout_handle != next_write) {
        SetHandleInformation(next_write, HANDLE_FLAG_INHERIT, 0);
      }

      startup_info.hStdInput = stdin_handle;
      startup_info.hStdOutput = stdout_handle;
      startup_info.hStdError = stderr_handle;

      if (!CreateProcessA(NULL, command_line, NULL, NULL, TRUE, 0, NULL, cwd, &startup_info, &process_info)) {
        snprintf(out, out_size, "CreateProcessA failed for %s", specs[i].argv[0]);
        status = 1;
      } else {
        stage_spawned = 1;
      }

windows_stage_cleanup:
      if (stage_spawned) {
        close_handle_if_valid(&opened_stdin);
        close_handle_if_valid(&opened_stdout);
        close_handle_if_valid(&opened_stderr);
        while (opened_redirect_count > 0) {
          opened_redirect_count--;
          close_handle_if_valid(&opened_redirects[opened_redirect_count]);
        }
        close_handle_if_valid(&previous_read);
        close_handle_if_valid(&next_write);
        close_handle_if_valid(&capture_write);

        close_handle_if_valid(&process_info.hThread);
        /* E4-S4: record the first process as the pgid equivalent */
        if (process_count == 0) {
          pgid_leader = process_info.dwProcessId;
        }
        process_infos[process_count++] = process_info;
        previous_read = next_read;
        next_read = INVALID_HANDLE_VALUE;
      }

      close_handle_if_valid(&opened_stdin);
      close_handle_if_valid(&opened_stdout);
      close_handle_if_valid(&opened_stderr);
      while (opened_redirect_count > 0) {
        opened_redirect_count--;
        close_handle_if_valid(&opened_redirects[opened_redirect_count]);
      }
      close_handle_if_valid(&next_read);
      close_handle_if_valid(&next_write);
      close_handle_if_valid(&capture_write);

      if (status != 0) {
        break;
      }
    }

    close_handle_if_valid(&previous_read);

    if (status == 0 && capture_read != INVALID_HANDLE_VALUE) {
      read_handle_to_buffer(capture_read, out, out_size);
    }
    close_handle_if_valid(&capture_read);

    for (i = 0; i < process_count; ++i) {
      if (process_infos[i].hProcess != NULL && process_infos[i].hProcess != INVALID_HANDLE_VALUE) {
        DWORD process_exit_code = 0;

        WaitForSingleObject(process_infos[i].hProcess, INFINITE);
        GetExitCodeProcess(process_infos[i].hProcess, &process_exit_code);
        stage_exit_codes[i] = process_exit_code;
        if (i + 1 == process_count) {
          exit_code = process_exit_code;
        }
        CloseHandle(process_infos[i].hProcess);
        process_infos[i].hProcess = NULL;
      }
    }

    if (status != 0) {
      return 1;
    }

    if (out_exit_code != NULL) {
      if (use_pipefail) {
        DWORD aggregate = 0;

        for (i = 0; i < process_count; ++i) {
          if (stage_exit_codes[i] != 0 && stage_exit_codes[i] > aggregate) {
            aggregate = stage_exit_codes[i];
          }
        }
        *out_exit_code = (int) aggregate;
      } else {
        *out_exit_code = (int) exit_code;
      }
    }
    return 0;
  }
#else
  {
    pid_t pids[ARKSH_MAX_PIPELINE_STAGES];
    int stage_exit_codes[ARKSH_MAX_PIPELINE_STAGES];
    size_t pid_count = 0;
    int previous_read = -1;
    int capture_read = -1;
    int interactive;
    pid_t pgid_leader = 0;
    size_t i;

    /* interactive: controls tcsetpgrp / process groups / WUNTRACED.
     * When force_capture is set we still hand the TTY to the child if
     * interactive (so job control works), but we always create the
     * capture pipe so stdout is collected for the caller. */
    interactive = isatty(STDIN_FILENO);
    memset(stage_exit_codes, 0, sizeof(stage_exit_codes));

    for (i = 0; i < spec_count; ++i) {
      int next_pipe[2] = {-1, -1};
      int capture_pipe[2] = {-1, -1};
      int should_capture_output = (!interactive || force_capture) && (i + 1 == spec_count);
      pid_t target_pgid = (pid_count == 0) ? 0 : pids[0];
      pid_t pid;

      if (i + 1 < spec_count && pipe(next_pipe) != 0) {
        snprintf(out, out_size, "unable to create shell pipeline");
        return 1;
      }

      if (should_capture_output && pipe(capture_pipe) != 0) {
        if (next_pipe[0] != -1) {
          close(next_pipe[0]);
        }
        if (next_pipe[1] != -1) {
          close(next_pipe[1]);
        }
        snprintf(out, out_size, "unable to capture command output");
        return 1;
      }

      pid = fork();
      if (pid < 0) {
        if (next_pipe[0] != -1) {
          close(next_pipe[0]);
        }
        if (next_pipe[1] != -1) {
          close(next_pipe[1]);
        }
        if (capture_pipe[0] != -1) {
          close(capture_pipe[0]);
        }
        if (capture_pipe[1] != -1) {
          close(capture_pipe[1]);
        }
        snprintf(out, out_size, "unable to fork for %s", specs[i].argv[0]);
        return 1;
      }

      if (pid == 0) {
        char *argv[ARKSH_MAX_ARGS + 1];
        int opened_fds[ARKSH_MAX_REDIRECTIONS * 2];
        size_t opened_fd_count = 0;
        size_t redirect_index;

        memset(opened_fds, -1, sizeof(opened_fds));

#ifndef _WIN32
        /* E4-S3: setpgid BEFORE restoring signals.
         * The shell has SIGINT/SIGQUIT ignored (SIG_IGN); the child inherits
         * that disposition.  Resetting to SIG_DFL *before* setpgid would open
         * a window where a Ctrl-C sent to the shell's pgid can kill the child
         * before it moves to its own process group.  Moving setpgid first
         * closes that race. */
        if (interactive) {
          setpgid(0, target_pgid);
        }
        signal(SIGINT,  SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        signal(SIGPIPE, SIG_DFL); /* E4-S3: ensure SIGPIPE is not ignored */
#endif
        if (cwd != NULL && cwd[0] != '\0' && chdir(cwd) != 0) {
          _exit(127);
        }

        if (previous_read != -1) {
          dup2(previous_read, STDIN_FILENO);
        }
        if (i + 1 < spec_count && next_pipe[1] != -1) {
          dup2(next_pipe[1], STDOUT_FILENO);
        } else if ((!interactive || force_capture) && capture_pipe[1] != -1) {
          dup2(capture_pipe[1], STDOUT_FILENO);
        }

        for (redirect_index = 0; redirect_index < specs[i].redirection_count; ++redirect_index) {
          const ArkshPlatformRedirectionSpec *redirect = &specs[i].redirections[redirect_index];

          if (redirect->close_target) {
            close(redirect->fd);
            continue;
          }

          if (redirect->text[0] != '\0') {
            int heredoc_pipe[2] = {-1, -1};
            size_t remaining = strlen(redirect->text);
            const char *text_cursor = redirect->text;

            if (pipe(heredoc_pipe) != 0) {
              _exit(127);
            }
            while (remaining > 0) {
              ssize_t written = write(heredoc_pipe[1], text_cursor, remaining);

              if (written < 0) {
                close(heredoc_pipe[0]);
                close(heredoc_pipe[1]);
                _exit(127);
              }
              remaining -= (size_t) written;
              text_cursor += written;
            }
            close(heredoc_pipe[1]);
            if (dup2(heredoc_pipe[0], redirect->fd) < 0) {
              close(heredoc_pipe[0]);
              _exit(127);
            }
            if (opened_fd_count < sizeof(opened_fds) / sizeof(opened_fds[0])) {
              opened_fds[opened_fd_count++] = heredoc_pipe[0];
            }
            continue;
          }

          if (redirect->path[0] != '\0') {
            int flags;
            int opened_fd;

            if (redirect->input_mode) {
              flags = O_RDONLY;
            } else {
              flags = O_WRONLY | O_CREAT | (redirect->append_mode ? O_APPEND : O_TRUNC);
            }

            opened_fd = open(redirect->path, flags, 0666);
            if (opened_fd < 0) {
              _exit(127);
            }
            if (dup2(opened_fd, redirect->fd) < 0) {
              close(opened_fd);
              _exit(127);
            }
            if (opened_fd_count < sizeof(opened_fds) / sizeof(opened_fds[0])) {
              opened_fds[opened_fd_count++] = opened_fd;
            }
            continue;
          }

          if (redirect->target_fd >= 0) {
            if (dup2(redirect->target_fd, redirect->fd) < 0) {
              _exit(127);
            }
            continue;
          }
        }

        if (previous_read != -1) {
          close(previous_read);
        }
        if (next_pipe[0] != -1) {
          close(next_pipe[0]);
        }
        if (next_pipe[1] != -1) {
          close(next_pipe[1]);
        }
        if (capture_pipe[0] != -1) {
          close(capture_pipe[0]);
        }
        if (capture_pipe[1] != -1) {
          close(capture_pipe[1]);
        }
        while (opened_fd_count > 0) {
          opened_fd_count--;
          if (opened_fds[opened_fd_count] != -1) {
            close(opened_fds[opened_fd_count]);
          }
        }

        build_process_argv(&specs[i], argv);
        execvp(argv[0], argv);
        _exit(errno == ENOENT ? 127 : 126);
      }

      pids[pid_count++] = pid;

      /* E4-S1: assign process group (race-free: both child and parent call setpgid) */
      if (interactive) {
        if (pid_count == 1) {
          pgid_leader = pid;
          setpgid(pid, pid);
        } else {
          setpgid(pid, pgid_leader);
        }
      }

      if (previous_read != -1) {
        close(previous_read);
      }
      if (next_pipe[1] != -1) {
        close(next_pipe[1]);
      }
      if (capture_pipe[1] != -1) {
        close(capture_pipe[1]);
      }

      previous_read = next_pipe[0];
      if (capture_pipe[0] != -1) {
        capture_read = capture_pipe[0];
      }
    }

    if (previous_read != -1) {
      close(previous_read);
    }

    /* E4-S1: hand terminal control to the foreground pipeline */
    if (interactive && pgid_leader > 0) {
      tcsetpgrp(STDIN_FILENO, pgid_leader);
    }

    if (capture_read != -1) {
      read_fd_to_buffer(capture_read, out, out_size);
      close(capture_read);
    }

    {
      int any_stopped = 0;

      for (i = 0; i < pid_count; ++i) {
        int wait_status = 0;
        int wflags = interactive ? WUNTRACED : 0;
        pid_t wpid;

        /* E4-S3: retry on EINTR so a stray signal (e.g. SIGCHLD from an
         * unrelated background job) does not silently skip the wait and
         * leave the child as a zombie or mark the wrong exit code. */
        do {
          wpid = waitpid(pids[i], &wait_status, wflags);
        } while (wpid < 0 && errno == EINTR);

        if (wpid < 0) {
          continue;
        }

        if (interactive && WIFSTOPPED(wait_status)) {
          any_stopped = 1;
        }

        if (WIFEXITED(wait_status)) {
          stage_exit_codes[i] = WEXITSTATUS(wait_status);
        } else if (WIFSIGNALED(wait_status)) {
          stage_exit_codes[i] = 128 + WTERMSIG(wait_status);
        } else {
          stage_exit_codes[i] = 1;
        }

        if (i + 1 == pid_count) {
          if (any_stopped) {
            if (out_stopped != NULL) {
              out_stopped->pid  = (long long) pids[pid_count - 1];
              out_stopped->pgid = (long long) pgid_leader;
            }
          } else if (out_exit_code != NULL) {
            if (use_pipefail) {
              int aggregate = 0;
              size_t stage_index;

              for (stage_index = 0; stage_index < pid_count; ++stage_index) {
                if (stage_exit_codes[stage_index] != 0 && stage_exit_codes[stage_index] > aggregate) {
                  aggregate = stage_exit_codes[stage_index];
                }
              }
              *out_exit_code = aggregate;
            } else {
              *out_exit_code = stage_exit_codes[i];
            }
          }
        }
      }
    }

    /* E4-S1: restore terminal control to the shell.
     * We may be in a background process group at this point (we handed the
     * TTY to the child above), so tcsetpgrp would normally raise SIGTTOU.
     * Temporarily ignore it — same idiom used by bash/zsh. */
    if (interactive) {
      signal(SIGTTOU, SIG_IGN);
      tcsetpgrp(STDIN_FILENO, getpgrp());
      signal(SIGTTOU, SIG_DFL);
    }

    return 0;
  }
#endif
}

const char *arksh_platform_path_separator(void) {
#ifdef _WIN32
  return "\\";
#else
  return "/";
#endif
}

int arksh_platform_spawn_background_process(
  const char *cwd,
  char *const argv[],
  ArkshPlatformAsyncProcess *out_process,
  char *error,
  size_t error_size
) {
  if (argv == NULL || argv[0] == NULL || out_process == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  memset(out_process, 0, sizeof(*out_process));
  error[0] = '\0';

#ifdef _WIN32
  {
    SECURITY_ATTRIBUTES security_attributes;
    STARTUPINFOA startup_info;
    PROCESS_INFORMATION process_info;
    HANDLE null_input = INVALID_HANDLE_VALUE;
    HANDLE stdout_handle = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE stderr_handle = GetStdHandle(STD_ERROR_HANDLE);
    char command_line[ARKSH_MAX_LINE * 2];

    memset(&security_attributes, 0, sizeof(security_attributes));
    security_attributes.nLength = sizeof(security_attributes);
    security_attributes.bInheritHandle = TRUE;

    memset(&startup_info, 0, sizeof(startup_info));
    startup_info.cb = sizeof(startup_info);
    startup_info.dwFlags = STARTF_USESTDHANDLES;

    memset(&process_info, 0, sizeof(process_info));
    build_windows_command_line_from_argv(argv, command_line, sizeof(command_line));
    if (command_line[0] == '\0') {
      snprintf(error, error_size, "empty background command");
      return 1;
    }

    null_input = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security_attributes, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (null_input == INVALID_HANDLE_VALUE) {
      snprintf(error, error_size, "unable to open NUL for background job");
      return 1;
    }

    startup_info.hStdInput = null_input;
    startup_info.hStdOutput = stdout_handle;
    startup_info.hStdError = stderr_handle;

    if (!CreateProcessA(NULL, command_line, NULL, NULL, TRUE, CREATE_NEW_PROCESS_GROUP, NULL, cwd, &startup_info, &process_info)) {
      CloseHandle(null_input);
      snprintf(error, error_size, "CreateProcessA failed for %s", argv[0]);
      return 1;
    }

    CloseHandle(null_input);
    CloseHandle(process_info.hThread);
    out_process->handle = process_info.hProcess;
    out_process->pid = process_info.dwProcessId;
    out_process->pgid = process_info.dwProcessId;
    return 0;
  }
#else
  {
    pid_t pid = fork();

    if (pid < 0) {
      snprintf(error, error_size, "unable to fork background job");
      return 1;
    }

    if (pid == 0) {
      int null_input;

      signal(SIGINT, SIG_DFL);
      signal(SIGQUIT, SIG_DFL);
      signal(SIGTSTP, SIG_DFL);
      setpgid(0, 0);

      if (cwd != NULL && cwd[0] != '\0' && chdir(cwd) != 0) {
        _exit(127);
      }

      null_input = open("/dev/null", O_RDONLY);
      if (null_input >= 0) {
        dup2(null_input, STDIN_FILENO);
        if (null_input != STDIN_FILENO) {
          close(null_input);
        }
      }

      execvp(argv[0], argv);
      _exit(errno == ENOENT ? 127 : 126);
    }

    if (setpgid(pid, pid) != 0 && errno != EACCES) {
      snprintf(error, error_size, "unable to assign background process group");
      return 1;
    }

    out_process->pid = (long long) pid;
    out_process->pgid = (long long) pid;
    return 0;
  }
#endif
}

int arksh_platform_poll_background_process(
  ArkshPlatformAsyncProcess *process,
  ArkshPlatformProcessState *out_state,
  int *out_exit_code
) {
  if (process == NULL || out_state == NULL || out_exit_code == NULL) {
    return 1;
  }

#ifdef _WIN32
  if (process->handle == NULL) {
    *out_state = ARKSH_PLATFORM_PROCESS_EXITED;
    *out_exit_code = 0;
    return 0;
  }

  {
    DWORD exit_code = STILL_ACTIVE;

    if (!GetExitCodeProcess((HANDLE) process->handle, &exit_code)) {
      return 1;
    }

    if (exit_code == STILL_ACTIVE) {
      *out_state = ARKSH_PLATFORM_PROCESS_UNCHANGED;
      *out_exit_code = 0;
      return 0;
    }

    *out_state = ARKSH_PLATFORM_PROCESS_EXITED;
    *out_exit_code = (int) exit_code;
    CloseHandle((HANDLE) process->handle);
    process->handle = NULL;
    process->pgid = 0;
    return 0;
  }
#else
  if (process->pid <= 0) {
    *out_state = ARKSH_PLATFORM_PROCESS_EXITED;
    *out_exit_code = 0;
    return 0;
  }

  {
    int status = 0;
    pid_t result = waitpid((pid_t) process->pid, &status, WNOHANG | WUNTRACED | WCONTINUED);

    if (result == 0) {
      *out_state = ARKSH_PLATFORM_PROCESS_UNCHANGED;
      *out_exit_code = 0;
      return 0;
    }
    if (result < 0) {
      if (errno == ECHILD) {
        *out_state = ARKSH_PLATFORM_PROCESS_EXITED;
        *out_exit_code = 0;
        process->pid = 0;
        process->pgid = 0;
        return 0;
      }
      return 1;
    }

    if (WIFCONTINUED(status)) {
      *out_state = ARKSH_PLATFORM_PROCESS_RUNNING;
      *out_exit_code = 0;
    } else if (WIFSTOPPED(status)) {
      *out_state = ARKSH_PLATFORM_PROCESS_STOPPED;
      *out_exit_code = 128 + WSTOPSIG(status);
    } else if (WIFEXITED(status)) {
      *out_state = ARKSH_PLATFORM_PROCESS_EXITED;
      *out_exit_code = WEXITSTATUS(status);
      process->pid = 0;
      process->pgid = 0;
    } else if (WIFSIGNALED(status)) {
      *out_state = ARKSH_PLATFORM_PROCESS_EXITED;
      *out_exit_code = 128 + WTERMSIG(status);
      process->pid = 0;
      process->pgid = 0;
    } else {
      *out_state = ARKSH_PLATFORM_PROCESS_UNCHANGED;
      *out_exit_code = 0;
    }
    return 0;
  }
#endif
}

int arksh_platform_wait_background_process(
  ArkshPlatformAsyncProcess *process,
  int foreground,
  ArkshPlatformProcessState *out_state,
  int *out_exit_code
) {
  if (process == NULL || out_state == NULL || out_exit_code == NULL) {
    return 1;
  }

#ifdef _WIN32
  (void) foreground;
  if (process->handle == NULL) {
    *out_exit_code = 0;
    *out_state = ARKSH_PLATFORM_PROCESS_EXITED;
    return 0;
  }

  if (WaitForSingleObject((HANDLE) process->handle, INFINITE) != WAIT_OBJECT_0) {
    return 1;
  }

  {
    DWORD exit_code = 0;

    if (!GetExitCodeProcess((HANDLE) process->handle, &exit_code)) {
      return 1;
    }
    *out_exit_code = (int) exit_code;
    *out_state = ARKSH_PLATFORM_PROCESS_EXITED;
    CloseHandle((HANDLE) process->handle);
    process->handle = NULL;
    process->pgid = 0;
    return 0;
  }
#else
  pid_t shell_pgid;

  if (process->pid <= 0) {
    *out_exit_code = 0;
    *out_state = ARKSH_PLATFORM_PROCESS_EXITED;
    return 0;
  }

  shell_pgid = getpgrp();
  if (foreground && process->pgid > 0 && isatty(STDIN_FILENO)) {
    tcsetpgrp(STDIN_FILENO, (pid_t) process->pgid);
  }

  {
    int status = 0;
    pid_t result;

    do {
      result = waitpid((pid_t) process->pid, &status, WUNTRACED);
    } while (result < 0 && errno == EINTR);

    if (foreground && isatty(STDIN_FILENO)) {
      tcsetpgrp(STDIN_FILENO, shell_pgid);
    }

    if (result < 0) {
      return 1;
    }
    if (WIFSTOPPED(status)) {
      *out_state = ARKSH_PLATFORM_PROCESS_STOPPED;
      *out_exit_code = 128 + WSTOPSIG(status);
    } else if (WIFEXITED(status)) {
      *out_state = ARKSH_PLATFORM_PROCESS_EXITED;
      *out_exit_code = WEXITSTATUS(status);
      process->pid = 0;
      process->pgid = 0;
    } else if (WIFSIGNALED(status)) {
      *out_state = ARKSH_PLATFORM_PROCESS_EXITED;
      *out_exit_code = 128 + WTERMSIG(status);
      process->pid = 0;
      process->pgid = 0;
    } else {
      *out_state = ARKSH_PLATFORM_PROCESS_UNCHANGED;
      *out_exit_code = 0;
    }
    return 0;
  }
#endif
}

int arksh_platform_resume_background_process(
  ArkshPlatformAsyncProcess *process,
  int foreground,
  char *error,
  size_t error_size
) {
  if (process == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  error[0] = '\0';

#ifdef _WIN32
  (void) foreground;
  return 0;
#else
  if (process->pgid <= 0) {
    snprintf(error, error_size, "invalid process group");
    return 1;
  }

  if (foreground && isatty(STDIN_FILENO) && tcsetpgrp(STDIN_FILENO, (pid_t) process->pgid) != 0) {
    snprintf(error, error_size, "unable to hand terminal to job");
    return 1;
  }

  if (kill(-(pid_t) process->pgid, SIGCONT) != 0 && errno != ESRCH) {
    if (foreground && isatty(STDIN_FILENO)) {
      tcsetpgrp(STDIN_FILENO, getpgrp());
    }
    snprintf(error, error_size, "unable to continue job");
    return 1;
  }

  return 0;
#endif
}

void arksh_platform_close_background_process(ArkshPlatformAsyncProcess *process) {
  if (process == NULL) {
    return;
  }

#ifdef _WIN32
  if (process->handle != NULL) {
    CloseHandle((HANDLE) process->handle);
    process->handle = NULL;
  }
  process->pid = 0;
  process->pgid = 0;
#else
  process->pid = 0;
  process->pgid = 0;
#endif
}

const char *arksh_platform_plugin_extension(void) {
#ifdef _WIN32
  return ".dll";
#elif defined(__APPLE__)
  return ".dylib";
#else
  return ".so";
#endif
}

const char *arksh_platform_os_name(void) {
#ifdef _WIN32
  return "windows";
#elif defined(__APPLE__)
  return "macos";
#elif defined(__linux__)
  return "linux";
#else
  return "unknown";
#endif
}

void *arksh_platform_library_open(const char *path) {
  set_last_error(NULL);

#ifdef _WIN32
  {
    HMODULE handle = LoadLibraryA(path);
    if (handle == NULL) {
      set_last_error("LoadLibraryA failed");
    }
    return (void *) handle;
  }
#else
  {
    void *handle = dlopen(path, RTLD_NOW);
    if (handle == NULL) {
      set_last_error(dlerror());
    }
    return handle;
  }
#endif
}

void *arksh_platform_library_symbol(void *handle, const char *name) {
  set_last_error(NULL);

#ifdef _WIN32
  {
    FARPROC symbol = GetProcAddress((HMODULE) handle, name);
    if (symbol == NULL) {
      set_last_error("GetProcAddress failed");
    }
    return (void *) symbol;
  }
#else
  {
    void *symbol = dlsym(handle, name);
    if (symbol == NULL) {
      set_last_error(dlerror());
    }
    return symbol;
  }
#endif
}

void arksh_platform_library_close(void *handle) {
  if (handle == NULL) {
    return;
  }

#ifdef _WIN32
  FreeLibrary((HMODULE) handle);
#else
  dlclose(handle);
#endif
}

const char *arksh_platform_last_error(void) {
  return g_last_error[0] == '\0' ? "unknown error" : g_last_error;
}
