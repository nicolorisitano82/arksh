#ifndef ARKSH_SHELL_H
#define ARKSH_SHELL_H

#include <signal.h>
#include <stddef.h>

#ifndef _WIN32
#include <termios.h>
#endif

#include "arksh/arena.h"
#include "arksh/ast.h"
#include "arksh/object.h"
#include "arksh/platform.h"
#include "arksh/plugin.h"
#include "arksh/prompt.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ARKSH_MAX_COMMANDS 64
#define ARKSH_MAX_TYPE_DESCRIPTORS 16
#define ARKSH_MAX_PLUGINS 16
#define ARKSH_MAX_DESCRIPTION 160
#define ARKSH_MAX_SHELL_VARS 64
#define ARKSH_MAX_ALIASES 32
#define ARKSH_MAX_VAR_NAME 64
#define ARKSH_MAX_VAR_VALUE 1024
#define ARKSH_MAX_ALIAS_VALUE 1024
#define ARKSH_MAX_HISTORY 256
#define ARKSH_MAX_JOBS 32
#define ARKSH_MAX_VALUE_BINDINGS 64
#define ARKSH_MAX_EXTENSIONS 64
#define ARKSH_MAX_VALUE_RESOLVERS 32
#define ARKSH_MAX_PIPELINE_STAGE_HANDLERS 64
#define ARKSH_MAX_FUNCTIONS 32
#define ARKSH_MAX_POSITIONAL_PARAMS 64
#define ARKSH_MAX_CLASSES 32
#define ARKSH_MAX_CLASS_PROPERTIES 256
#define ARKSH_MAX_CLASS_METHODS 256
#define ARKSH_MAX_INSTANCES 128
#define ARKSH_MAX_PROCESS_SUBSTITUTIONS 32

/* Execution semantics classification for built-in commands.
 *
 * PURE    – reads shell state only; output is plain text; safe to run in any
 *           pipeline position (e.g. pwd, type, history, jobs).
 * MUTANT  – must run in the main shell process because it modifies shell state
 *           (variables, working directory, job table, control flow, etc.).
 *           Cannot be an intermediate pipeline stage (e.g. cd, set, eval).
 * MIXED   – behaviour depends on arguments: some invocations are read-only
 *           (list mode) while others mutate state (define/load mode).
 *           Conservative treatment: same restriction as MUTANT in pipelines.
 */
typedef enum {
  ARKSH_BUILTIN_PURE = 0,
  ARKSH_BUILTIN_MUTANT,
  ARKSH_BUILTIN_MIXED
} ArkshBuiltinKind;

typedef struct {
  char name[64];
  char description[ARKSH_MAX_DESCRIPTION];
  ArkshCommandFn fn;
  int is_plugin_command;
  int owner_plugin_index;
  ArkshBuiltinKind kind; /* execution-policy metadata; see ArkshBuiltinKind */
} ArkshCommandDef;

typedef struct {
  char name[64];
  char version[32];
  char description[ARKSH_MAX_DESCRIPTION];
  char path[ARKSH_MAX_PATH];
  unsigned int abi_major;
  unsigned int abi_minor;
  unsigned long long required_host_capabilities;
  unsigned long long plugin_capabilities;
  void *handle;
  int active;
} ArkshLoadedPlugin;

typedef struct {
  char name[ARKSH_MAX_VAR_NAME];
  char value[ARKSH_MAX_VAR_VALUE];
  int exported;
} ArkshShellVar;

typedef struct {
  char name[ARKSH_MAX_VAR_NAME];
  char value[ARKSH_MAX_ALIAS_VALUE];
} ArkshAlias;

typedef struct {
  char name[ARKSH_MAX_VAR_NAME];
  ArkshValue value;
} ArkshValueBinding;

typedef struct {
  char name[ARKSH_MAX_VAR_NAME];
  char value[ARKSH_MAX_VAR_VALUE];
  int exported;
  int deleted;
} ArkshScopedVar;

typedef struct {
  char name[ARKSH_MAX_VAR_NAME];
  ArkshValue value;
  int deleted;
} ArkshScopedBinding;

typedef struct ArkshScopeFrame {
  ArkshScopedVar *vars;
  size_t var_count;
  size_t var_capacity;
  ArkshScopedBinding *bindings;
  size_t binding_count;
  size_t binding_capacity;
  char (*positional_params)[ARKSH_MAX_VAR_VALUE];
  int positional_count;
  size_t positional_capacity;
  int has_positional;
  struct ArkshScopeFrame *previous;
} ArkshScopeFrame;

typedef struct {
  char name[ARKSH_MAX_NAME];
  char params[ARKSH_MAX_FUNCTION_PARAMS][ARKSH_MAX_NAME];
  int param_count;
  char body[ARKSH_MAX_LINE];
  char source[ARKSH_MAX_LINE];
} ArkshShellFunction;

typedef struct {
  char name[ARKSH_MAX_NAME];
  ArkshValue default_value;
} ArkshClassProperty;

typedef struct {
  char name[ARKSH_MAX_NAME];
  ArkshBlock block;
} ArkshClassMethod;

typedef struct {
  char name[ARKSH_MAX_NAME];
  char bases[ARKSH_MAX_CLASS_BASES][ARKSH_MAX_NAME];
  int base_count;
  ArkshClassProperty *properties;
  size_t property_count;
  size_t property_capacity;
  ArkshClassMethod *methods;
  size_t method_count;
  size_t method_capacity;
  char source[ARKSH_MAX_LINE];
} ArkshClassDef;

typedef struct {
  int id;
  char class_name[ARKSH_MAX_NAME];
  ArkshValue fields;
} ArkshClassInstance;

typedef struct {
  char name[ARKSH_MAX_NAME];
  char description[ARKSH_MAX_DESCRIPTION];
  ArkshValueResolverFn fn;
  int is_plugin_resolver;
  int owner_plugin_index;
} ArkshValueResolverDef;

typedef struct {
  char name[ARKSH_MAX_NAME];
  char description[ARKSH_MAX_DESCRIPTION];
  ArkshPipelineStageFn fn; /* NULL for metadata-only (built-in) entries */
  int is_plugin_stage;
  int owner_plugin_index;
} ArkshPipelineStageDef;

typedef enum {
  ARKSH_JOB_RUNNING = 0,
  ARKSH_JOB_STOPPED,
  ARKSH_JOB_DONE
} ArkshJobState;

typedef struct {
  int id;
  ArkshJobState state;
  int exit_code;
  int termination_signal; /* 0 = normal exit, >0 = signal number */
  char command[ARKSH_MAX_LINE];
  ArkshPlatformAsyncProcess process;
} ArkshJob;

typedef enum {
  ARKSH_PROC_SUBST_INPUT = 0,  /* <(cmd): command writes to a readable path */
  ARKSH_PROC_SUBST_OUTPUT       /* >(cmd): command reads from a writable path */
} ArkshProcessSubstitutionKind;

typedef struct {
  ArkshProcessSubstitutionKind kind;
  char path[ARKSH_MAX_PATH];
  ArkshPlatformAsyncProcess process;
} ArkshProcessSubstitution;

typedef enum {
  ARKSH_CONTROL_SIGNAL_NONE = 0,
  ARKSH_CONTROL_SIGNAL_BREAK,
  ARKSH_CONTROL_SIGNAL_CONTINUE,
  ARKSH_CONTROL_SIGNAL_RETURN
} ArkshControlSignalKind;

typedef enum {
  ARKSH_TRAP_EXIT  = 0,  /* pseudo: EXIT */
  ARKSH_TRAP_ERR   = 1,  /* pseudo: ERR  */
  ARKSH_TRAP_HUP   = 2,
  ARKSH_TRAP_INT   = 3,
  ARKSH_TRAP_QUIT  = 4,
  ARKSH_TRAP_ILL   = 5,
  ARKSH_TRAP_ABRT  = 6,
  ARKSH_TRAP_FPE   = 7,
  ARKSH_TRAP_SEGV  = 8,
  ARKSH_TRAP_PIPE  = 9,
  ARKSH_TRAP_ALRM  = 10,
  ARKSH_TRAP_TERM  = 11,
  ARKSH_TRAP_USR1  = 12,
  ARKSH_TRAP_USR2  = 13,
  ARKSH_TRAP_CHLD  = 14,
  ARKSH_TRAP_TSTP  = 15,
  ARKSH_TRAP_TTIN  = 16,
  ARKSH_TRAP_TTOU  = 17,
  ARKSH_TRAP_COUNT = 18
} ArkshTrapKind;

typedef struct {
  char name[ARKSH_MAX_NAME];
  char command[ARKSH_MAX_LINE];
  int active;
  int ignored;
} ArkshTrapEntry;

typedef enum {
  ARKSH_EXTENSION_TARGET_ANY = 0,
  ARKSH_EXTENSION_TARGET_VALUE_KIND,
  ARKSH_EXTENSION_TARGET_OBJECT_KIND,
  /* E6-S2-T1: custom type backed by a typed MAP.  The target_name field on
   * ArkshObjectExtension holds the expected __type__ tag value. */
  ARKSH_EXTENSION_TARGET_TYPED_MAP
} ArkshExtensionTargetKind;

typedef enum {
  ARKSH_EXTENSION_IMPL_BLOCK = 0,
  ARKSH_EXTENSION_IMPL_NATIVE
} ArkshExtensionImplKind;

typedef struct {
  char target_name[ARKSH_MAX_NAME];
  char name[ARKSH_MAX_NAME];
  ArkshMemberKind member_kind;
  ArkshExtensionTargetKind target_kind;
  ArkshValueKind value_kind;
  ArkshObjectKind object_kind;
  ArkshExtensionImplKind impl_kind;
  ArkshBlock block;
  ArkshExtensionPropertyFn property_fn;
  ArkshExtensionMethodFn method_fn;
  int is_plugin_extension;
  int owner_plugin_index;
} ArkshObjectExtension;

/* E6-S2-T1: metadata record for a plugin-defined custom type. */
typedef struct {
  char type_name[ARKSH_MAX_NAME];
  char description[ARKSH_MAX_DESCRIPTION];
} ArkshTypeDescriptor;

typedef struct {
  char current_source_path[ARKSH_MAX_PATH];
  size_t current_source_line;
  char function_name_stack[ARKSH_MAX_FUNCTIONS][ARKSH_MAX_NAME];
  int function_name_depth;
} ArkshShellMetadata;

typedef struct ArkshShell {
  int running;
  int last_status;
  char cwd[ARKSH_MAX_PATH];
  char program_path[ARKSH_MAX_PATH];
  char executable_path[ARKSH_MAX_PATH];
  ArkshShellMetadata *metadata;
  char config_dir[ARKSH_MAX_PATH];
  char cache_dir[ARKSH_MAX_PATH];
  char state_dir[ARKSH_MAX_PATH];
  char data_dir[ARKSH_MAX_PATH];
  char plugin_dir[ARKSH_MAX_PATH];
  int inherited_input_active;
  ArkshRedirectionNode inherited_input_redirection;
  ArkshPromptConfig prompt;
  ArkshCommandDef *commands;
  size_t command_count;
  size_t command_capacity;
  size_t *command_name_index;
  size_t command_name_index_capacity;
  ArkshLoadedPlugin *plugins;
  size_t plugin_count;
  size_t plugin_capacity;
  ArkshShellVar *vars;
  size_t var_count;
  size_t var_capacity;
  ArkshValueBinding *bindings;
  size_t binding_count;
  size_t binding_capacity;
  ArkshShellFunction *functions;
  size_t function_count;
  size_t function_capacity;
  ArkshClassDef *classes;
  size_t class_count;
  size_t class_capacity;
  size_t *class_name_index;
  size_t class_name_index_capacity;
  ArkshClassInstance *instances;
  size_t instance_count;
  size_t instance_capacity;
  size_t *instance_id_index;
  size_t instance_id_index_capacity;
  int next_instance_id;
  ArkshObjectExtension *extensions;
  size_t extension_count;
  size_t extension_capacity;
  ArkshValueResolverDef *value_resolvers;
  size_t value_resolver_count;
  size_t value_resolver_capacity;
  size_t *value_resolver_name_index;
  size_t value_resolver_name_index_capacity;
  ArkshPipelineStageDef *pipeline_stages;
  size_t pipeline_stage_count;
  size_t pipeline_stage_capacity;
  size_t *pipeline_stage_name_index;
  size_t pipeline_stage_name_index_capacity;
  ArkshAlias *aliases;
  size_t alias_count;
  size_t alias_capacity;
  char (*history)[ARKSH_MAX_LINE];
  size_t history_count;
  size_t history_capacity;
  char history_path[ARKSH_MAX_PATH];
  int history_dirty;
  ArkshJob *jobs;
  size_t job_count;
  size_t job_capacity;
  int next_job_id;
  ArkshProcessSubstitution *process_substitutions;
  size_t process_substitution_count;
  size_t process_substitution_capacity;
  unsigned long long next_process_substitution_id;
  int loading_plugin_index;
  ArkshControlSignalKind control_signal;
  int control_levels;
  int loop_depth;
  int function_depth;
  ArkshTrapEntry *traps;
  int running_exit_trap;
  /* E1-S6-T2: set -e/-u/-x/-o options */
  int opt_errexit;   /* -e: exit on command failure */
  int opt_nounset;   /* -u: error on unset variable */
  int opt_xtrace;    /* -x: print commands before execution */
  int opt_pipefail;  /* -o pipefail: pipeline fails if any stage fails */
  int in_condition;  /* true while evaluating if/while/until condition */
  int errexit_suppressed; /* >0 while failures must not trigger ERR trap / errexit */
  char (*positional_params)[ARKSH_MAX_VAR_VALUE];
  int positional_count;
  size_t positional_capacity;
  long long last_bg_pid;
  long long shell_pid;
  long long parent_pid;  /* E15-S1-T1: $PPID — PID of parent process (fixed at init) */
  long long shell_pgid;
  long long shell_sid;
  long long shell_tty_pgid;
  long long shell_tty_rows;
  long long shell_tty_cols;
  int shell_has_tty;
  int shell_is_session_leader;
  int shell_is_process_group_leader;
#ifdef _WIN32
  void *tty_input_handle;
  unsigned long tty_saved_input_mode;
#else
  struct termios tty_saved_state;
#endif
  int tty_saved_state_valid;
  int tty_raw_active;
  int interactive_shell;
  int login_mode;
  int sh_mode; /* 1 when running as sh-compatible shell (--sh or argv[0]=="sh") */
  int force_capture; /* 1 inside capture()/capture_lines()/bridge — always capture stdout */
  int ctx_sudo;      /* E15-S3: >0 inside "with sudo do … endwith"; external commands are prefixed with sudo */
  ArkshScratchArena scratch;
  ArkshTypeDescriptor *type_descriptors;
  size_t type_descriptor_count;
  size_t type_descriptor_capacity;
  ArkshScopeFrame *scope_frame;
  unsigned long completion_generation;
} ArkshShell;

extern volatile sig_atomic_t arksh_terminal_resize_pending;

int arksh_shell_init(ArkshShell *shell);
int arksh_shell_init_with_options(ArkshShell *shell, const char *program_path, int login_mode, int sh_mode);
void arksh_shell_destroy(ArkshShell *shell);
int arksh_shell_enter_raw_mode(ArkshShell *shell);
void arksh_shell_leave_raw_mode(ArkshShell *shell);
void arksh_shell_restore_tty(ArkshShell *shell);
int arksh_shell_run_repl(ArkshShell *shell);
int arksh_shell_execute_line(ArkshShell *shell, const char *line, char *out, size_t out_size);
void arksh_shell_refresh_terminal_state(ArkshShell *shell);
int arksh_shell_register_command(ArkshShell *shell, const char *name, const char *description, ArkshCommandFn fn, int is_plugin_command);
void arksh_shell_write_output(const char *text);
void arksh_shell_print_help(const ArkshShell *shell, char *out, size_t out_size);
void arksh_shell_set_program_path(ArkshShell *shell, const char *path);
int arksh_shell_load_config(ArkshShell *shell, const char *path, char *out, size_t out_size);
int arksh_shell_load_plugin(ArkshShell *shell, const char *path, char *out, size_t out_size);
int arksh_shell_resolve_plugin_path(const ArkshShell *shell, const char *query, char *out, size_t out_size);
const char *arksh_shell_get_var(const ArkshShell *shell, const char *name);
int arksh_shell_set_var(ArkshShell *shell, const char *name, const char *value, int exported);
int arksh_shell_unset_var(ArkshShell *shell, const char *name);
/* E15-S1-T3: nameref — declare -n / local -n */
#define ARKSH_NAMEREF_PREFIX     "__ARKSH_NAMEREF__"
#define ARKSH_NAMEREF_PREFIX_LEN 17
int  arksh_shell_set_nameref(ArkshShell *shell, const char *name, const char *target);
int  arksh_shell_resolve_nameref(const ArkshShell *shell, const char *name, char *out_target, size_t out_size);
const ArkshValue *arksh_shell_get_binding(const ArkshShell *shell, const char *name);
int arksh_shell_set_binding(ArkshShell *shell, const char *name, const ArkshValue *value);
int arksh_shell_unset_binding(ArkshShell *shell, const char *name);
int arksh_shell_push_scope_frame(ArkshShell *shell);
void arksh_shell_pop_scope_frame(ArkshShell *shell);
int arksh_shell_has_scope_frame(const ArkshShell *shell);
int arksh_shell_set_positional_argv(ArkshShell *shell, int count, const char *const *values);
int arksh_shell_set_positional_copy(ArkshShell *shell, int count, const char values[][ARKSH_MAX_VAR_VALUE]);
int arksh_shell_get_positional_count(const ArkshShell *shell);
const char *arksh_shell_get_positional(const ArkshShell *shell, int index);
int arksh_shell_shift_positional(ArkshShell *shell, int count);
int arksh_shell_clone_subshell(const ArkshShell *source, ArkshShell **out_shell, char *out, size_t out_size);
int arksh_shell_restore_after_subshell(const ArkshShell *parent, const ArkshShell *subshell, char *out, size_t out_size);
void arksh_shell_destroy_subshell(ArkshShell *shell);
const ArkshCommandDef *arksh_shell_find_command(const ArkshShell *shell, const char *name);
const ArkshShellFunction *arksh_shell_find_function(const ArkshShell *shell, const char *name);
int arksh_shell_set_function(ArkshShell *shell, const ArkshFunctionCommandNode *function_node);
const ArkshClassDef *arksh_shell_find_class(const ArkshShell *shell, const char *name);
const ArkshClassInstance *arksh_shell_find_instance(const ArkshShell *shell, int id);
int arksh_shell_set_class(ArkshShell *shell, const ArkshClassCommandNode *class_node, char *out, size_t out_size);
int arksh_shell_instantiate_class(
  ArkshShell *shell,
  const char *name,
  int argc,
  const ArkshValue *args,
  ArkshValue *out_value,
  char *out,
  size_t out_size
);
int arksh_shell_get_class_property_value(ArkshShell *shell, const ArkshValue *receiver, const char *property, ArkshValue *out_value, char *out, size_t out_size);
int arksh_shell_call_class_method(ArkshShell *shell, const ArkshValue *receiver, const char *method, int argc, const ArkshValue *args, ArkshValue *out_value, char *out, size_t out_size);
int arksh_shell_register_value_resolver(ArkshShell *shell, const char *name, const char *description, ArkshValueResolverFn fn, int is_plugin_resolver);
int arksh_shell_register_pipeline_stage(ArkshShell *shell, const char *name, const char *description, ArkshPipelineStageFn fn, int is_plugin_stage);
const ArkshValueResolverDef *arksh_shell_find_value_resolver(const ArkshShell *shell, const char *name);
const ArkshPipelineStageDef *arksh_shell_find_pipeline_stage(const ArkshShell *shell, const char *name);
int arksh_shell_register_block_property_extension(ArkshShell *shell, const char *target, const char *name, const ArkshBlock *block);
int arksh_shell_register_block_method_extension(ArkshShell *shell, const char *target, const char *name, const ArkshBlock *block);
int arksh_shell_register_native_property_extension(ArkshShell *shell, const char *target, const char *name, ArkshExtensionPropertyFn fn, int is_plugin_extension);
int arksh_shell_register_native_method_extension(ArkshShell *shell, const char *target, const char *name, ArkshExtensionMethodFn fn, int is_plugin_extension);
/* E6-S2-T1: register a custom named type so the shell can list and describe it. */
int arksh_shell_register_type_descriptor(ArkshShell *shell, const char *type_name, const char *description);
const char *arksh_shell_get_alias(const ArkshShell *shell, const char *name);
int arksh_shell_set_alias(ArkshShell *shell, const char *name, const char *value);
int arksh_shell_unset_alias(ArkshShell *shell, const char *name);
int arksh_shell_source_file(ArkshShell *shell, const char *path, int positional_count, char **positional_args, char *out, size_t out_size);
void arksh_shell_refresh_jobs(ArkshShell *shell);
int arksh_shell_start_background_job(ArkshShell *shell, const char *command_text, char *out, size_t out_size);
int arksh_shell_prepare_process_substitution(
  ArkshShell *shell,
  ArkshProcessSubstitutionKind kind,
  const char *command_text,
  char *out_path,
  size_t out_path_size,
  char *out,
  size_t out_size
);
void arksh_shell_clear_control_signal(ArkshShell *shell);
int arksh_shell_raise_control_signal(ArkshShell *shell, ArkshControlSignalKind kind, int levels);
int arksh_shell_run_exit_trap(ArkshShell *shell, char *out, size_t out_size);
int arksh_shell_collect_member_completions(
  ArkshShell *shell,
  const char *receiver_text,
  const char *prefix,
  char matches[][ARKSH_MAX_PATH],
  size_t max_matches,
  size_t *out_count
);

#ifdef __cplusplus
}
#endif

#endif
