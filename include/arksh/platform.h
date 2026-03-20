#ifndef ARKSH_PLATFORM_H
#define ARKSH_PLATFORM_H

#include <stddef.h>

#include "arksh/ast.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  int exists;
  int is_file;
  int is_directory;
  int is_device;
  int is_mount_point;
  unsigned long long size;
  int hidden;
  int readable;
  int writable;
} ArkshPlatformFileInfo;

typedef struct {
  int fd;
  int input_mode;
  int append_mode;
  int target_fd;
  int close_target;
  int heredoc_strip_tabs;
  char path[ARKSH_MAX_PATH];
  char text[ARKSH_MAX_OUTPUT];
} ArkshPlatformRedirectionSpec;

typedef struct {
  char argv[ARKSH_MAX_ARGS][ARKSH_MAX_TOKEN];
  int argc;
  ArkshPlatformRedirectionSpec redirections[ARKSH_MAX_REDIRECTIONS];
  size_t redirection_count;
} ArkshPlatformProcessSpec;

typedef struct {
  char name[ARKSH_MAX_NAME];
  char value[ARKSH_MAX_OUTPUT];
} ArkshPlatformEnvEntry;

typedef struct {
  unsigned long pid;
  unsigned long ppid;
} ArkshPlatformProcessInfo;

typedef struct {
#ifdef _WIN32
  void *handle;
  unsigned long pid;
  unsigned long pgid;
#else
  long long pid;
  long long pgid;
#endif
} ArkshPlatformAsyncProcess;

typedef enum {
  ARKSH_PLATFORM_PROCESS_UNCHANGED = 0,
  ARKSH_PLATFORM_PROCESS_RUNNING,
  ARKSH_PLATFORM_PROCESS_STOPPED,
  ARKSH_PLATFORM_PROCESS_EXITED
} ArkshPlatformProcessState;

int arksh_platform_getcwd(char *out, size_t out_size);
int arksh_platform_chdir(const char *path);
int arksh_platform_gethostname(char *out, size_t out_size);
int arksh_platform_resolve_path(const char *cwd, const char *input, char *out, size_t out_size);
int arksh_platform_ensure_directory(const char *path);
void arksh_platform_basename(const char *path, char *out, size_t out_size);
void arksh_platform_dirname(const char *path, char *out, size_t out_size);
int arksh_platform_stat(const char *path, ArkshPlatformFileInfo *info);
int arksh_platform_list_children(const char *path, char *out, size_t out_size);
int arksh_platform_list_children_names(const char *path, char names[][ARKSH_MAX_PATH], size_t max_names, size_t *out_count);
int arksh_platform_read_text_file(const char *path, size_t limit, char *out, size_t out_size);
int arksh_platform_write_text_file(const char *path, const char *text, int append, char *out, size_t out_size);
int arksh_platform_list_environment(ArkshPlatformEnvEntry entries[], size_t max_entries, size_t *out_count);
int arksh_platform_get_process_info(ArkshPlatformProcessInfo *out_info);
int arksh_platform_run_process_pipeline(
  const char *cwd,
  const ArkshPlatformProcessSpec *specs,
  size_t spec_count,
  char *out,
  size_t out_size,
  int *out_exit_code,
  ArkshPlatformAsyncProcess *out_stopped,
  int force_capture  /* 1 = capture stdout even when running interactively */
);
int arksh_platform_glob(
  const char *pattern,
  char matches[][ARKSH_MAX_TOKEN],
  int max_matches,
  int *out_count
);
int arksh_platform_spawn_background_process(
  const char *cwd,
  char *const argv[],
  ArkshPlatformAsyncProcess *out_process,
  char *error,
  size_t error_size
);
int arksh_platform_poll_background_process(
  ArkshPlatformAsyncProcess *process,
  ArkshPlatformProcessState *out_state,
  int *out_exit_code
);
int arksh_platform_wait_background_process(
  ArkshPlatformAsyncProcess *process,
  int foreground,
  ArkshPlatformProcessState *out_state,
  int *out_exit_code
);
int arksh_platform_resume_background_process(
  ArkshPlatformAsyncProcess *process,
  int foreground,
  char *error,
  size_t error_size
);
void arksh_platform_close_background_process(ArkshPlatformAsyncProcess *process);
const char *arksh_platform_path_separator(void);
const char *arksh_platform_plugin_extension(void);
const char *arksh_platform_os_name(void);
void *arksh_platform_library_open(const char *path);
void *arksh_platform_library_symbol(void *handle, const char *name);
void arksh_platform_library_close(void *handle);
const char *arksh_platform_last_error(void);

#ifdef __cplusplus
}
#endif

#endif
