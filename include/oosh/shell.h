#ifndef OOSH_SHELL_H
#define OOSH_SHELL_H

#include <stddef.h>

#include "oosh/ast.h"
#include "oosh/object.h"
#include "oosh/platform.h"
#include "oosh/plugin.h"
#include "oosh/prompt.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OOSH_MAX_COMMANDS 64
#define OOSH_MAX_PLUGINS 16
#define OOSH_MAX_DESCRIPTION 160
#define OOSH_MAX_SHELL_VARS 64
#define OOSH_MAX_ALIASES 32
#define OOSH_MAX_VAR_NAME 64
#define OOSH_MAX_VAR_VALUE 1024
#define OOSH_MAX_ALIAS_VALUE 1024
#define OOSH_MAX_HISTORY 256
#define OOSH_MAX_JOBS 32
#define OOSH_MAX_VALUE_BINDINGS 64
#define OOSH_MAX_EXTENSIONS 64
#define OOSH_MAX_VALUE_RESOLVERS 32
#define OOSH_MAX_PIPELINE_STAGE_HANDLERS 32
#define OOSH_MAX_FUNCTIONS 32
#define OOSH_MAX_CLASSES 32
#define OOSH_MAX_CLASS_PROPERTIES 32
#define OOSH_MAX_CLASS_METHODS 32
#define OOSH_MAX_INSTANCES 128

typedef struct {
  char name[64];
  char description[OOSH_MAX_DESCRIPTION];
  OoshCommandFn fn;
  int is_plugin_command;
  int owner_plugin_index;
} OoshCommandDef;

typedef struct {
  char name[64];
  char version[32];
  char description[OOSH_MAX_DESCRIPTION];
  char path[OOSH_MAX_PATH];
  void *handle;
  int active;
} OoshLoadedPlugin;

typedef struct {
  char name[OOSH_MAX_VAR_NAME];
  char value[OOSH_MAX_VAR_VALUE];
  int exported;
} OoshShellVar;

typedef struct {
  char name[OOSH_MAX_VAR_NAME];
  char value[OOSH_MAX_ALIAS_VALUE];
} OoshAlias;

typedef struct {
  char name[OOSH_MAX_VAR_NAME];
  OoshValue value;
} OoshValueBinding;

typedef struct {
  char name[OOSH_MAX_NAME];
  char params[OOSH_MAX_FUNCTION_PARAMS][OOSH_MAX_NAME];
  int param_count;
  char body[OOSH_MAX_LINE];
  char source[OOSH_MAX_LINE];
} OoshShellFunction;

typedef struct {
  char name[OOSH_MAX_NAME];
  OoshValue default_value;
} OoshClassProperty;

typedef struct {
  char name[OOSH_MAX_NAME];
  OoshBlock block;
} OoshClassMethod;

typedef struct {
  char name[OOSH_MAX_NAME];
  char bases[OOSH_MAX_CLASS_BASES][OOSH_MAX_NAME];
  int base_count;
  OoshClassProperty properties[OOSH_MAX_CLASS_PROPERTIES];
  size_t property_count;
  OoshClassMethod methods[OOSH_MAX_CLASS_METHODS];
  size_t method_count;
  char source[OOSH_MAX_LINE];
} OoshClassDef;

typedef struct {
  int id;
  char class_name[OOSH_MAX_NAME];
  OoshValue fields;
} OoshClassInstance;

typedef struct {
  char name[OOSH_MAX_NAME];
  OoshValueResolverFn fn;
  int is_plugin_resolver;
  int owner_plugin_index;
} OoshValueResolverDef;

typedef struct {
  char name[OOSH_MAX_NAME];
  OoshPipelineStageFn fn;
  int is_plugin_stage;
  int owner_plugin_index;
} OoshPipelineStageDef;

typedef enum {
  OOSH_JOB_RUNNING = 0,
  OOSH_JOB_STOPPED,
  OOSH_JOB_DONE
} OoshJobState;

typedef struct {
  int id;
  OoshJobState state;
  int exit_code;
  char command[OOSH_MAX_LINE];
  OoshPlatformAsyncProcess process;
} OoshJob;

typedef enum {
  OOSH_CONTROL_SIGNAL_NONE = 0,
  OOSH_CONTROL_SIGNAL_BREAK,
  OOSH_CONTROL_SIGNAL_CONTINUE,
  OOSH_CONTROL_SIGNAL_RETURN
} OoshControlSignalKind;

typedef enum {
  OOSH_TRAP_EXIT = 0,
  OOSH_TRAP_COUNT
} OoshTrapKind;

typedef struct {
  char name[OOSH_MAX_NAME];
  char command[OOSH_MAX_LINE];
  int active;
} OoshTrapEntry;

typedef enum {
  OOSH_EXTENSION_TARGET_ANY = 0,
  OOSH_EXTENSION_TARGET_VALUE_KIND,
  OOSH_EXTENSION_TARGET_OBJECT_KIND
} OoshExtensionTargetKind;

typedef enum {
  OOSH_EXTENSION_IMPL_BLOCK = 0,
  OOSH_EXTENSION_IMPL_NATIVE
} OoshExtensionImplKind;

typedef struct {
  char target_name[OOSH_MAX_NAME];
  char name[OOSH_MAX_NAME];
  OoshMemberKind member_kind;
  OoshExtensionTargetKind target_kind;
  OoshValueKind value_kind;
  OoshObjectKind object_kind;
  OoshExtensionImplKind impl_kind;
  OoshBlock block;
  OoshExtensionPropertyFn property_fn;
  OoshExtensionMethodFn method_fn;
  int is_plugin_extension;
  int owner_plugin_index;
} OoshObjectExtension;

typedef struct OoshShell {
  int running;
  int last_status;
  char cwd[OOSH_MAX_PATH];
  char program_path[OOSH_MAX_PATH];
  int inherited_input_active;
  OoshRedirectionNode inherited_input_redirection;
  OoshPromptConfig prompt;
  OoshCommandDef commands[OOSH_MAX_COMMANDS];
  size_t command_count;
  OoshLoadedPlugin plugins[OOSH_MAX_PLUGINS];
  size_t plugin_count;
  OoshShellVar vars[OOSH_MAX_SHELL_VARS];
  size_t var_count;
  OoshValueBinding bindings[OOSH_MAX_VALUE_BINDINGS];
  size_t binding_count;
  OoshShellFunction functions[OOSH_MAX_FUNCTIONS];
  size_t function_count;
  OoshClassDef classes[OOSH_MAX_CLASSES];
  size_t class_count;
  OoshClassInstance instances[OOSH_MAX_INSTANCES];
  size_t instance_count;
  int next_instance_id;
  OoshObjectExtension extensions[OOSH_MAX_EXTENSIONS];
  size_t extension_count;
  OoshValueResolverDef value_resolvers[OOSH_MAX_VALUE_RESOLVERS];
  size_t value_resolver_count;
  OoshPipelineStageDef pipeline_stages[OOSH_MAX_PIPELINE_STAGE_HANDLERS];
  size_t pipeline_stage_count;
  OoshAlias aliases[OOSH_MAX_ALIASES];
  size_t alias_count;
  char history[OOSH_MAX_HISTORY][OOSH_MAX_LINE];
  size_t history_count;
  char history_path[OOSH_MAX_PATH];
  int history_dirty;
  OoshJob jobs[OOSH_MAX_JOBS];
  size_t job_count;
  int next_job_id;
  int loading_plugin_index;
  OoshControlSignalKind control_signal;
  int control_levels;
  int loop_depth;
  int function_depth;
  OoshTrapEntry traps[OOSH_TRAP_COUNT];
  int running_exit_trap;
} OoshShell;

int oosh_shell_init(OoshShell *shell);
void oosh_shell_destroy(OoshShell *shell);
int oosh_shell_run_repl(OoshShell *shell);
int oosh_shell_execute_line(OoshShell *shell, const char *line, char *out, size_t out_size);
int oosh_shell_register_command(OoshShell *shell, const char *name, const char *description, OoshCommandFn fn, int is_plugin_command);
void oosh_shell_write_output(const char *text);
void oosh_shell_print_help(const OoshShell *shell, char *out, size_t out_size);
void oosh_shell_set_program_path(OoshShell *shell, const char *path);
int oosh_shell_load_config(OoshShell *shell, const char *path, char *out, size_t out_size);
int oosh_shell_load_plugin(OoshShell *shell, const char *path, char *out, size_t out_size);
const char *oosh_shell_get_var(const OoshShell *shell, const char *name);
int oosh_shell_set_var(OoshShell *shell, const char *name, const char *value, int exported);
int oosh_shell_unset_var(OoshShell *shell, const char *name);
const OoshValue *oosh_shell_get_binding(const OoshShell *shell, const char *name);
int oosh_shell_set_binding(OoshShell *shell, const char *name, const OoshValue *value);
int oosh_shell_unset_binding(OoshShell *shell, const char *name);
const OoshShellFunction *oosh_shell_find_function(const OoshShell *shell, const char *name);
int oosh_shell_set_function(OoshShell *shell, const OoshFunctionCommandNode *function_node);
const OoshClassDef *oosh_shell_find_class(const OoshShell *shell, const char *name);
const OoshClassInstance *oosh_shell_find_instance(const OoshShell *shell, int id);
int oosh_shell_set_class(OoshShell *shell, const OoshClassCommandNode *class_node, char *out, size_t out_size);
int oosh_shell_instantiate_class(
  OoshShell *shell,
  const char *name,
  int argc,
  const OoshValue *args,
  OoshValue *out_value,
  char *out,
  size_t out_size
);
int oosh_shell_get_class_property_value(OoshShell *shell, const OoshValue *receiver, const char *property, OoshValue *out_value, char *out, size_t out_size);
int oosh_shell_call_class_method(OoshShell *shell, const OoshValue *receiver, const char *method, int argc, const OoshValue *args, OoshValue *out_value, char *out, size_t out_size);
int oosh_shell_register_value_resolver(OoshShell *shell, const char *name, OoshValueResolverFn fn, int is_plugin_resolver);
int oosh_shell_register_pipeline_stage(OoshShell *shell, const char *name, OoshPipelineStageFn fn, int is_plugin_stage);
const OoshValueResolverDef *oosh_shell_find_value_resolver(const OoshShell *shell, const char *name);
const OoshPipelineStageDef *oosh_shell_find_pipeline_stage(const OoshShell *shell, const char *name);
int oosh_shell_register_block_property_extension(OoshShell *shell, const char *target, const char *name, const OoshBlock *block);
int oosh_shell_register_block_method_extension(OoshShell *shell, const char *target, const char *name, const OoshBlock *block);
int oosh_shell_register_native_property_extension(OoshShell *shell, const char *target, const char *name, OoshExtensionPropertyFn fn, int is_plugin_extension);
int oosh_shell_register_native_method_extension(OoshShell *shell, const char *target, const char *name, OoshExtensionMethodFn fn, int is_plugin_extension);
const char *oosh_shell_get_alias(const OoshShell *shell, const char *name);
int oosh_shell_set_alias(OoshShell *shell, const char *name, const char *value);
int oosh_shell_unset_alias(OoshShell *shell, const char *name);
int oosh_shell_source_file(OoshShell *shell, const char *path, char *out, size_t out_size);
void oosh_shell_refresh_jobs(OoshShell *shell);
int oosh_shell_start_background_job(OoshShell *shell, const char *command_text, char *out, size_t out_size);
void oosh_shell_clear_control_signal(OoshShell *shell);
int oosh_shell_raise_control_signal(OoshShell *shell, OoshControlSignalKind kind, int levels);
int oosh_shell_run_exit_trap(OoshShell *shell, char *out, size_t out_size);
int oosh_shell_collect_member_completions(
  OoshShell *shell,
  const char *receiver_text,
  const char *prefix,
  char matches[][OOSH_MAX_PATH],
  size_t max_matches,
  size_t *out_count
);

#ifdef __cplusplus
}
#endif

#endif
