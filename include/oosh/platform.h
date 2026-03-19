#ifndef OOSH_PLATFORM_H
#define OOSH_PLATFORM_H

#include <stddef.h>

#include "oosh/ast.h"

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
} OoshPlatformFileInfo;

typedef struct {
  int fd;
  int input_mode;
  int append_mode;
  int target_fd;
  int close_target;
  int heredoc_strip_tabs;
  char path[OOSH_MAX_PATH];
  char text[OOSH_MAX_OUTPUT];
} OoshPlatformRedirectionSpec;

typedef struct {
  char argv[OOSH_MAX_ARGS][OOSH_MAX_TOKEN];
  int argc;
  OoshPlatformRedirectionSpec redirections[OOSH_MAX_REDIRECTIONS];
  size_t redirection_count;
} OoshPlatformProcessSpec;

typedef struct {
  char name[OOSH_MAX_NAME];
  char value[OOSH_MAX_OUTPUT];
} OoshPlatformEnvEntry;

typedef struct {
  unsigned long pid;
  unsigned long ppid;
} OoshPlatformProcessInfo;

typedef struct {
#ifdef _WIN32
  void *handle;
  unsigned long pid;
  unsigned long pgid;
#else
  long long pid;
  long long pgid;
#endif
} OoshPlatformAsyncProcess;

typedef enum {
  OOSH_PLATFORM_PROCESS_UNCHANGED = 0,
  OOSH_PLATFORM_PROCESS_RUNNING,
  OOSH_PLATFORM_PROCESS_STOPPED,
  OOSH_PLATFORM_PROCESS_EXITED
} OoshPlatformProcessState;

int oosh_platform_getcwd(char *out, size_t out_size);
int oosh_platform_chdir(const char *path);
int oosh_platform_gethostname(char *out, size_t out_size);
int oosh_platform_resolve_path(const char *cwd, const char *input, char *out, size_t out_size);
int oosh_platform_ensure_directory(const char *path);
void oosh_platform_basename(const char *path, char *out, size_t out_size);
void oosh_platform_dirname(const char *path, char *out, size_t out_size);
int oosh_platform_stat(const char *path, OoshPlatformFileInfo *info);
int oosh_platform_list_children(const char *path, char *out, size_t out_size);
int oosh_platform_list_children_names(const char *path, char names[][OOSH_MAX_PATH], size_t max_names, size_t *out_count);
int oosh_platform_read_text_file(const char *path, size_t limit, char *out, size_t out_size);
int oosh_platform_write_text_file(const char *path, const char *text, int append, char *out, size_t out_size);
int oosh_platform_list_environment(OoshPlatformEnvEntry entries[], size_t max_entries, size_t *out_count);
int oosh_platform_get_process_info(OoshPlatformProcessInfo *out_info);
int oosh_platform_run_process_pipeline(
  const char *cwd,
  const OoshPlatformProcessSpec *specs,
  size_t spec_count,
  char *out,
  size_t out_size,
  int *out_exit_code,
  OoshPlatformAsyncProcess *out_stopped,
  int force_capture  /* 1 = capture stdout even when running interactively */
);
int oosh_platform_glob(
  const char *pattern,
  char matches[][OOSH_MAX_TOKEN],
  int max_matches,
  int *out_count
);
int oosh_platform_spawn_background_process(
  const char *cwd,
  char *const argv[],
  OoshPlatformAsyncProcess *out_process,
  char *error,
  size_t error_size
);
int oosh_platform_poll_background_process(
  OoshPlatformAsyncProcess *process,
  OoshPlatformProcessState *out_state,
  int *out_exit_code
);
int oosh_platform_wait_background_process(
  OoshPlatformAsyncProcess *process,
  int foreground,
  OoshPlatformProcessState *out_state,
  int *out_exit_code
);
int oosh_platform_resume_background_process(
  OoshPlatformAsyncProcess *process,
  int foreground,
  char *error,
  size_t error_size
);
void oosh_platform_close_background_process(OoshPlatformAsyncProcess *process);
const char *oosh_platform_path_separator(void);
const char *oosh_platform_plugin_extension(void);
const char *oosh_platform_os_name(void);
void *oosh_platform_library_open(const char *path);
void *oosh_platform_library_symbol(void *handle, const char *name);
void oosh_platform_library_close(void *handle);
const char *oosh_platform_last_error(void);

#ifdef __cplusplus
}
#endif

#endif
