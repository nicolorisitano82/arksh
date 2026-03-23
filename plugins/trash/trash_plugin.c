/*
 * trash_plugin.c — E6-S9: integrazione cestino di sistema
 *
 * Registra:
 *   -> trash()             metodo su oggetti file/directory/path
 *   |> each_trash          stage pipeline per cestinare una lista di oggetti
 *   trash()                resolver: namespace cestino (typed-map "trash_ns")
 *   trash_ns -> count      numero di elementi nel cestino
 *   trash_ns -> items      lista di path degli oggetti nel cestino
 *   trash_ns -> empty()    svuota il cestino
 *   trash_ns -> restore(n) ripristina elemento per nome (dove supportato)
 *
 * Dipendenze per piattaforma:
 *   macOS   — -framework Foundation (NSFileManager trashItemAtURL:...)
 *   Linux   — nessuna (XDG Trash spec + fallback execvp gio)
 *   Windows — Shell32.lib (SHFileOperationW, SHEmptyRecycleBinW)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arksh/plugin.h"

#define TRASH_NS_TYPE "trash_ns"

/* ============================================================ platform layer */

#if defined(__APPLE__)

#include <objc/runtime.h>
#include <objc/message.h>
#include <dirent.h>
#include <sys/stat.h>

/* Trash a single item using NSFileManager. Returns 0 on success. */
static int platform_trash_item(
  const char *abs_path,
  char *out_trash_path,
  size_t out_size,
  char *error,
  size_t error_size)
{
  Class NSFileManager_cls = objc_getClass("NSFileManager");
  Class NSURL_cls          = objc_getClass("NSURL");
  Class NSString_cls       = objc_getClass("NSString");
  id fm, ns_path, url, result_url, error_obj;
  SEL sel;
  typedef id   (*MsgId)(id, SEL, ...);
  typedef id   (*MsgIdClass)(Class, SEL, ...);
  typedef BOOL (*MsgBool)(id, SEL, ...);

  if (!NSFileManager_cls || !NSURL_cls || !NSString_cls) {
    snprintf(error, error_size, "trash(): Foundation not available");
    return 1;
  }

  sel = sel_registerName("defaultManager");
  fm = ((MsgIdClass) objc_msgSend)(NSFileManager_cls, sel);
  if (!fm) {
    snprintf(error, error_size, "trash(): NSFileManager defaultManager failed");
    return 1;
  }

  sel = sel_registerName("stringWithUTF8String:");
  ns_path = ((MsgIdClass) objc_msgSend)(NSString_cls, sel, abs_path);

  sel = sel_registerName("fileURLWithPath:");
  url = ((MsgId) objc_msgSend)((id) NSURL_cls, sel, ns_path);

  sel    = sel_registerName("trashItemAtURL:resultingItemURL:error:");
  result_url = NULL;
  error_obj  = NULL;
  BOOL ok = ((MsgBool) objc_msgSend)(fm, sel, url, &result_url, &error_obj);

  if (!ok) {
    if (error_obj) {
      SEL desc_sel = sel_registerName("localizedDescription");
      SEL utf8_sel = sel_registerName("UTF8String");
      id  desc     = ((MsgId) objc_msgSend)(error_obj, desc_sel);
      const char *desc_str = ((const char *(*)(id, SEL)) objc_msgSend)(desc, utf8_sel);
      snprintf(error, error_size, "trash(): %s", desc_str ? desc_str : "unknown error");
    } else {
      snprintf(error, error_size, "trash(): failed to move to trash");
    }
    return 1;
  }

  if (out_trash_path && out_size > 0 && result_url) {
    SEL path_sel = sel_registerName("path");
    SEL utf8_sel = sel_registerName("UTF8String");
    id  path_str = ((MsgId) objc_msgSend)(result_url, path_sel);
    const char *path_cstr = ((const char *(*)(id, SEL)) objc_msgSend)(path_str, utf8_sel);
    if (path_cstr) {
      snprintf(out_trash_path, out_size, "%s", path_cstr);
    }
  }

  return 0;
}

/* List items in ~/.Trash and optionally count them. */
static int platform_trash_list(ArkshValue *out_list, int *out_count) {
  const char *home = getenv("HOME");
  char trash_dir[ARKSH_MAX_PATH];
  DIR *d;
  struct dirent *de;

  if (!home) {
    return 1;
  }
  snprintf(trash_dir, sizeof(trash_dir), "%s/.Trash", home);

  d = opendir(trash_dir);
  if (!d) {
    return 1;
  }

  if (out_list) {
    out_list->kind = ARKSH_VALUE_LIST;
    out_list->list.count = 0;
  }
  if (out_count) {
    *out_count = 0;
  }

  while ((de = readdir(d)) != NULL) {
    if (de->d_name[0] == '.') continue; /* skip . .. and hidden Apple files */
    if (out_count) {
      (*out_count)++;
    }
    if (out_list) {
      char full[ARKSH_MAX_PATH];
      ArkshValue item;
      snprintf(full, sizeof(full), "%s/%s", trash_dir, de->d_name);
      arksh_value_set_string(&item, full);
      arksh_value_list_append_value(out_list, &item);
      arksh_value_free(&item);
    }
  }

  closedir(d);
  return 0;
}

static int platform_trash_empty(char *error, size_t error_size) {
  const char *home = getenv("HOME");
  char trash_dir[ARKSH_MAX_PATH];
  DIR *d;
  struct dirent *de;

  if (!home) {
    snprintf(error, error_size, "empty(): HOME not set");
    return 1;
  }

  snprintf(trash_dir, sizeof(trash_dir), "%s/.Trash", home);
  d = opendir(trash_dir);
  if (!d) {
    snprintf(error, error_size, "empty(): cannot open ~/.Trash");
    return 1;
  }

  while ((de = readdir(d)) != NULL) {
    char full[ARKSH_MAX_PATH];
    char rm_cmd[ARKSH_MAX_PATH + 32];
    if (de->d_name[0] == '.') continue;
    snprintf(full, sizeof(full), "%s/%s", trash_dir, de->d_name);
    /* Use NSFileManager removeItemAtPath: via ObjC runtime */
    {
      Class NSFileManager_cls = objc_getClass("NSFileManager");
      SEL sel;
      typedef id   (*MsgIdClass)(Class, SEL, ...);
      typedef BOOL (*MsgBool)(id, SEL, ...);
      id fm, ns_path;
      Class NSString_cls = objc_getClass("NSString");
      if (NSFileManager_cls && NSString_cls) {
        sel     = sel_registerName("defaultManager");
        fm      = ((MsgIdClass) objc_msgSend)(NSFileManager_cls, sel);
        sel     = sel_registerName("stringWithUTF8String:");
        ns_path = ((MsgIdClass) objc_msgSend)(NSString_cls, sel, full);
        sel     = sel_registerName("removeItemAtPath:error:");
        ((MsgBool) objc_msgSend)(fm, sel, ns_path, NULL);
      } else {
        snprintf(rm_cmd, sizeof(rm_cmd), "rm -rf '%s'", full);
        (void) system(rm_cmd);
      }
    }
  }

  closedir(d);
  return 0;
}

static int platform_trash_restore(const char *name, char *error, size_t error_size) {
  (void) name;
  snprintf(error, error_size, "restore(): not supported on macOS (use Finder)");
  return 1;
}

#elif defined(_WIN32)

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

static int platform_trash_item(
  const char *abs_path,
  char *out_trash_path,
  size_t out_size,
  char *error,
  size_t error_size)
{
  wchar_t wpath[MAX_PATH + 2] = {0};
  SHFILEOPSTRUCTW op;
  int result;

  if (MultiByteToWideChar(CP_UTF8, 0, abs_path, -1, wpath, MAX_PATH) == 0) {
    snprintf(error, error_size, "trash(): path conversion failed");
    return 1;
  }

  memset(&op, 0, sizeof(op));
  op.wFunc  = FO_DELETE;
  op.pFrom  = wpath;
  op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;

  result = SHFileOperationW(&op);
  if (result != 0 || op.fAnyOperationsAborted) {
    snprintf(error, error_size, "trash(): SHFileOperationW failed (code %d)", result);
    return 1;
  }

  if (out_trash_path && out_size > 0) {
    out_trash_path[0] = '\0'; /* destination not available from SHFileOperation */
  }
  return 0;
}

static int platform_trash_list(ArkshValue *out_list, int *out_count) {
  /* SHQueryRecycleBin returns the aggregate count/size only; per-item enumeration
   * is not provided without COM (IShellFolder) which is too heavy here. */
  SHQUERYRBINFO info = {0};
  info.cbSize = sizeof(info);
  if (SHQueryRecycleBinW(NULL, &info) == S_OK) {
    if (out_count) {
      *out_count = (int) info.i64NumItems;
    }
  }
  if (out_list) {
    out_list->kind = ARKSH_VALUE_LIST;
    out_list->list.count = 0;
    /* Per-item listing not available without COM — return empty list */
  }
  return 0;
}

static int platform_trash_empty(char *error, size_t error_size) {
  HRESULT hr = SHEmptyRecycleBinW(NULL, NULL, SHERB_NOCONFIRMATION | SHERB_NOPROGRESSUI | SHERB_NOSOUND);
  if (FAILED(hr)) {
    snprintf(error, error_size, "empty(): SHEmptyRecycleBinW failed (hr=0x%lx)", (unsigned long) hr);
    return 1;
  }
  return 0;
}

static int platform_trash_restore(const char *name, char *error, size_t error_size) {
  (void) name;
  snprintf(error, error_size, "restore(): not supported on Windows");
  return 1;
}

#else /* Linux / BSD / other Unix */

#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static void ensure_dir(const char *path) {
  struct stat st;
  if (stat(path, &st) != 0) {
    mkdir(path, 0700);
  }
}

static const char *basename_of(const char *path) {
  const char *p = strrchr(path, '/');
  return p ? p + 1 : path;
}

static int try_gio_trash(const char *abs_path, char *error, size_t error_size) {
  char *args[] = { "gio", "trash", "--", (char *) abs_path, NULL };
  pid_t pid;
  int status;

  pid = fork();
  if (pid < 0) {
    return 1; /* fork failed, fall through to XDG */
  }
  if (pid == 0) {
    execvp("gio", args);
    _exit(127);
  }
  waitpid(pid, &status, 0);
  if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
    return 0; /* success */
  }
  (void) error; (void) error_size;
  return 1; /* gio not available or failed */
}

static int platform_trash_item(
  const char *abs_path,
  char *out_trash_path,
  size_t out_size,
  char *error,
  size_t error_size)
{
  const char *home = getenv("HOME");
  char trash_files[ARKSH_MAX_PATH], trash_info[ARKSH_MAX_PATH];
  char dest[ARKSH_MAX_PATH], info_path[ARKSH_MAX_PATH];
  const char *base;
  FILE *f;
  time_t now;
  struct tm *tm_info;
  char dt[32];

  /* Try gio first — it handles all edge cases (different filesystems, etc.) */
  if (try_gio_trash(abs_path, error, error_size) == 0) {
    if (out_trash_path && out_size > 0) {
      out_trash_path[0] = '\0';
    }
    return 0;
  }

  /* Fallback: XDG Trash spec */
  if (!home) {
    snprintf(error, error_size, "trash(): HOME environment variable not set");
    return 1;
  }

  snprintf(trash_files, sizeof(trash_files), "%s/.local/share/Trash/files", home);
  snprintf(trash_info,  sizeof(trash_info),  "%s/.local/share/Trash/info",  home);

  ensure_dir(trash_files);
  ensure_dir(trash_info);

  base = basename_of(abs_path);
  snprintf(dest,      sizeof(dest),      "%s/%s",        trash_files, base);
  snprintf(info_path, sizeof(info_path), "%s/%s.trashinfo", trash_info, base);

  if (rename(abs_path, dest) != 0) {
    snprintf(error, error_size, "trash(): rename failed: %s", strerror(errno));
    return 1;
  }

  /* Write .trashinfo */
  f = fopen(info_path, "w");
  if (f) {
    now     = time(NULL);
    tm_info = localtime(&now);
    strftime(dt, sizeof(dt), "%Y-%m-%dT%H:%M:%S", tm_info);
    fprintf(f, "[Trash Info]\nPath=%s\nDeletionDate=%s\n", abs_path, dt);
    fclose(f);
  }

  if (out_trash_path && out_size > 0) {
    snprintf(out_trash_path, out_size, "%s", dest);
  }
  return 0;
}

static int platform_trash_list(ArkshValue *out_list, int *out_count) {
  const char *home = getenv("HOME");
  char trash_files[ARKSH_MAX_PATH];
  DIR *d;
  struct dirent *de;

  if (!home) { return 1; }

  snprintf(trash_files, sizeof(trash_files), "%s/.local/share/Trash/files", home);

  d = opendir(trash_files);
  if (!d) {
    if (out_count) *out_count = 0;
    if (out_list) { out_list->kind = ARKSH_VALUE_LIST; out_list->list.count = 0; }
    return 0; /* trash dir doesn't exist = empty */
  }

  if (out_list) { out_list->kind = ARKSH_VALUE_LIST; out_list->list.count = 0; }
  if (out_count) *out_count = 0;

  while ((de = readdir(d)) != NULL) {
    if (de->d_name[0] == '.') continue;
    if (out_count) (*out_count)++;
    if (out_list) {
      char full[ARKSH_MAX_PATH];
      ArkshValue item;
      snprintf(full, sizeof(full), "%s/%s", trash_files, de->d_name);
      arksh_value_set_string(&item, full);
      arksh_value_list_append_value(out_list, &item);
      arksh_value_free(&item);
    }
  }

  closedir(d);
  return 0;
}

static int platform_trash_empty(char *error, size_t error_size) {
  const char *home = getenv("HOME");
  char cmd[ARKSH_MAX_PATH * 2];

  if (!home) {
    snprintf(error, error_size, "empty(): HOME not set");
    return 1;
  }

  snprintf(cmd, sizeof(cmd),
    "rm -rf '%s/.local/share/Trash/files/'* "
    "      '%s/.local/share/Trash/info/'* 2>/dev/null ; true",
    home, home);

  if (system(cmd) != 0) {
    /* system() can return non-zero even on partial success; ignore */
  }
  return 0;
}

static int platform_trash_restore(const char *name, char *error, size_t error_size) {
  const char *home = getenv("HOME");
  char files_path[ARKSH_MAX_PATH], info_path[ARKSH_MAX_PATH];
  char orig_path[ARKSH_MAX_PATH] = {0};
  char line[ARKSH_MAX_PATH];
  FILE *f;

  if (!home) {
    snprintf(error, error_size, "restore(): HOME not set");
    return 1;
  }

  snprintf(files_path, sizeof(files_path), "%s/.local/share/Trash/files/%s", home, name);
  snprintf(info_path,  sizeof(info_path),  "%s/.local/share/Trash/info/%s.trashinfo", home, name);

  /* Read original path from .trashinfo */
  f = fopen(info_path, "r");
  if (!f) {
    snprintf(error, error_size, "restore(): %s not found in trash", name);
    return 1;
  }

  while (fgets(line, sizeof(line), f)) {
    if (strncmp(line, "Path=", 5) == 0) {
      size_t len;
      snprintf(orig_path, sizeof(orig_path), "%s", line + 5);
      len = strlen(orig_path);
      while (len > 0 && (orig_path[len - 1] == '\n' || orig_path[len - 1] == '\r')) {
        orig_path[--len] = '\0';
      }
      break;
    }
  }
  fclose(f);

  if (orig_path[0] == '\0') {
    snprintf(error, error_size, "restore(): malformed .trashinfo for %s", name);
    return 1;
  }

  if (rename(files_path, orig_path) != 0) {
    snprintf(error, error_size, "restore(): rename failed: %s", strerror(errno));
    return 1;
  }

  /* Remove .trashinfo */
  remove(info_path);
  return 0;
}

#endif /* platform */

/* ============================================================ helpers */

static int trash_ns_add_string(ArkshValue *map, const char *key, const char *text) {
  ArkshValue v;
  int r;
  arksh_value_set_string(&v, text);
  r = arksh_value_map_set(map, key, &v);
  arksh_value_free(&v);
  return r;
}

/* ============================================================ object method: -> trash() */

static int object_method_trash(
  ArkshShell *shell,
  const ArkshValue *receiver,
  int argc,
  const ArkshValue *args,
  ArkshValue *out_value,
  char *error,
  size_t error_size)
{
  char trash_dest[ARKSH_MAX_PATH] = {0};

  (void) shell;
  (void) args;

  if (receiver == NULL || out_value == NULL || error == NULL || error_size == 0) {
    return 1;
  }
  if (argc != 0) {
    snprintf(error, error_size, "trash() does not accept arguments");
    return 1;
  }
  if (receiver->kind != ARKSH_VALUE_OBJECT) {
    snprintf(error, error_size, "trash() is only valid on filesystem objects");
    return 1;
  }
  if (!arksh_value_object_ref(receiver)->exists) {
    snprintf(error, error_size, "trash(): path does not exist: %s", arksh_value_object_ref(receiver)->path);
    return 1;
  }

  if (platform_trash_item(arksh_value_object_ref(receiver)->path, trash_dest, sizeof(trash_dest), error, error_size) != 0) {
    return 1;
  }

  if (trash_dest[0] != '\0') {
    arksh_value_set_string(out_value, trash_dest);
  } else {
    arksh_value_set_string(out_value, arksh_value_object_ref(receiver)->path);
  }
  return 0;
}

/* ============================================================ pipeline stage: |> each_trash */

static int stage_each_trash(
  ArkshShell *shell,
  ArkshValue *value,
  const char *raw_args,
  char *error,
  size_t error_size)
{
  size_t i;
  int failed = 0;
  char fail_msg[ARKSH_MAX_OUTPUT] = {0};

  (void) shell;
  (void) raw_args;

  if (value == NULL || error == NULL || error_size == 0) {
    return 1;
  }
  if (value->kind != ARKSH_VALUE_LIST) {
    snprintf(error, error_size, "each_trash: expects a list of filesystem objects");
    return 1;
  }

  for (i = 0; i < value->list.count; i++) {
    const ArkshValueItem *item = &value->list.items[i];
    char item_error[256] = {0};
    char trash_dest[ARKSH_MAX_PATH] = {0};
    const char *path = NULL;

    if (item->kind == ARKSH_VALUE_OBJECT) {
      path = arksh_value_item_object_ref(item)->path;
    } else if (item->kind == ARKSH_VALUE_STRING) {
      path = arksh_value_item_text_cstr(item);
    } else {
      snprintf(item_error, sizeof(item_error), "item %zu: not a path or object", i);
      failed++;
      if (fail_msg[0] != '\0') {
        strncat(fail_msg, "; ", sizeof(fail_msg) - strlen(fail_msg) - 1);
      }
      strncat(fail_msg, item_error, sizeof(fail_msg) - strlen(fail_msg) - 1);
      continue;
    }

    if (platform_trash_item(path, trash_dest, sizeof(trash_dest), item_error, sizeof(item_error)) != 0) {
      failed++;
      if (fail_msg[0] != '\0') {
        strncat(fail_msg, "; ", sizeof(fail_msg) - strlen(fail_msg) - 1);
      }
      strncat(fail_msg, item_error, sizeof(fail_msg) - strlen(fail_msg) - 1);
    }
  }

  if (failed > 0) {
    snprintf(error, error_size, "each_trash: %d item(s) failed: %s", failed, fail_msg);
    return 1;
  }

  /* Replace pipeline value with count of trashed items */
  arksh_value_set_number(value, (double) value->list.count);
  return 0;
}

/* ============================================================ resolver: trash() */

static int resolver_trash(
  ArkshShell *shell,
  int argc,
  const ArkshValue *args,
  ArkshValue *out_value,
  char *error,
  size_t error_size)
{
  (void) shell;
  (void) args;

  if (out_value == NULL || error == NULL || error_size == 0) {
    return 1;
  }
  if (argc != 0) {
    snprintf(error, error_size, "trash() does not accept arguments");
    return 1;
  }

  arksh_value_set_typed_map(out_value, TRASH_NS_TYPE);
  return 0;
}

/* ============================================================ trash_ns properties */

static int trash_ns_prop_count(
  ArkshShell *shell,
  const ArkshValue *receiver,
  ArkshValue *out_value,
  char *error,
  size_t error_size)
{
  int count = 0;

  (void) shell;
  (void) receiver;

  if (out_value == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  if (platform_trash_list(NULL, &count) != 0) {
    snprintf(error, error_size, "trash count: unable to inspect trash");
    return 1;
  }

  arksh_value_set_number(out_value, (double) count);
  return 0;
}

static int trash_ns_prop_items(
  ArkshShell *shell,
  const ArkshValue *receiver,
  ArkshValue *out_value,
  char *error,
  size_t error_size)
{
  (void) shell;
  (void) receiver;

  if (out_value == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  if (platform_trash_list(out_value, NULL) != 0) {
    snprintf(error, error_size, "trash items: unable to list trash");
    return 1;
  }

  return 0;
}

/* ============================================================ trash_ns methods */

static int trash_ns_method_empty(
  ArkshShell *shell,
  const ArkshValue *receiver,
  int argc,
  const ArkshValue *args,
  ArkshValue *out_value,
  char *error,
  size_t error_size)
{
  (void) shell;
  (void) receiver;
  (void) args;

  if (out_value == NULL || error == NULL || error_size == 0) {
    return 1;
  }
  if (argc != 0) {
    snprintf(error, error_size, "empty() does not accept arguments");
    return 1;
  }

  if (platform_trash_empty(error, error_size) != 0) {
    return 1;
  }

  arksh_value_set_boolean(out_value, 1);
  return 0;
}

static int trash_ns_method_restore(
  ArkshShell *shell,
  const ArkshValue *receiver,
  int argc,
  const ArkshValue *args,
  ArkshValue *out_value,
  char *error,
  size_t error_size)
{
  char name[ARKSH_MAX_PATH];

  (void) shell;
  (void) receiver;

  if (out_value == NULL || error == NULL || error_size == 0) {
    return 1;
  }
  if (argc != 1) {
    snprintf(error, error_size, "restore(name) requires exactly one string argument");
    return 1;
  }
  if (arksh_value_render(&args[0], name, sizeof(name)) != 0) {
    snprintf(error, error_size, "restore(): unable to render name argument");
    return 1;
  }

  if (platform_trash_restore(name, error, error_size) != 0) {
    return 1;
  }

  arksh_value_set_boolean(out_value, 1);
  return 0;
}

/* ============================================================ init */

ARKSH_PLUGIN_EXPORT int arksh_plugin_query(ArkshPluginInfo *out_info) {
  if (out_info == NULL) {
    return 1;
  }

  memset(out_info, 0, sizeof(*out_info));
  snprintf(out_info->name,        sizeof(out_info->name),        "trash-plugin");
  snprintf(out_info->version,     sizeof(out_info->version),     "1.0.0");
  snprintf(out_info->description, sizeof(out_info->description),
    "System trash integration: -> trash(), |> each_trash, trash() namespace");
  out_info->abi_major = ARKSH_PLUGIN_ABI_MAJOR;
  out_info->abi_minor = ARKSH_PLUGIN_ABI_MINOR;
  out_info->required_host_capabilities =
    ARKSH_PLUGIN_CAP_METHOD_EXTENSIONS |
    ARKSH_PLUGIN_CAP_VALUE_RESOLVERS |
    ARKSH_PLUGIN_CAP_PIPELINE_STAGES |
    ARKSH_PLUGIN_CAP_TYPE_DESCRIPTORS;
  out_info->plugin_capabilities = out_info->required_host_capabilities;
  return 0;
}

ARKSH_PLUGIN_EXPORT int arksh_plugin_init(
  ArkshShell *shell,
  const ArkshPluginHost *host,
  ArkshPluginInfo *out_info)
{
  if (shell == NULL || host == NULL || out_info == NULL) {
    return 1;
  }

  if (host->abi_major != ARKSH_PLUGIN_ABI_MAJOR ||
      host->abi_minor < ARKSH_PLUGIN_ABI_MINOR ||
      host->register_command == NULL ||
      host->register_property_extension == NULL ||
      host->register_method_extension == NULL ||
      host->register_value_resolver == NULL ||
      host->register_pipeline_stage == NULL ||
      host->register_type_descriptor == NULL) {
    return 1;
  }

  if (arksh_plugin_query(out_info) != 0) {
    return 1;
  }

  /* Register the trash_ns typed-map type */
  if (host->register_type_descriptor(shell, TRASH_NS_TYPE,
        "system trash namespace (count, items, empty, restore)") != 0) {
    return 1;
  }

  /* -> trash() method on any filesystem object */
  if (host->register_method_extension(shell, "object", "trash", object_method_trash) != 0) {
    return 1;
  }

  /* |> each_trash pipeline stage */
  if (host->register_pipeline_stage(shell, "each_trash",
        "move each filesystem object in a list to the system trash",
        stage_each_trash) != 0) {
    return 1;
  }

  /* trash() resolver → typed-map trash_ns */
  if (host->register_value_resolver(shell, "trash",
        "system trash namespace (count, items, empty(), restore(name))",
        resolver_trash) != 0) {
    return 1;
  }

  /* Properties on trash_ns */
  if (host->register_property_extension(shell, TRASH_NS_TYPE, "count", trash_ns_prop_count) != 0 ||
      host->register_property_extension(shell, TRASH_NS_TYPE, "items", trash_ns_prop_items) != 0) {
    return 1;
  }

  /* Methods on trash_ns */
  if (host->register_method_extension(shell, TRASH_NS_TYPE, "empty",   trash_ns_method_empty)   != 0 ||
      host->register_method_extension(shell, TRASH_NS_TYPE, "restore", trash_ns_method_restore) != 0) {
    return 1;
  }

  return 0;
}
