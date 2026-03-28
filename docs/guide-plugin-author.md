# ARKsh Plugin Author Guide

This guide covers writing, building, loading, and distributing native arksh
plugins. Plugins are dynamic libraries (`.dylib` / `.so` / `.dll`) that extend
the shell with new commands, value resolvers, pipeline stages, object
properties, methods, and custom types.

---

## ABI Version

Current ABI: **major 5, minor 0** (`ARKSH_PLUGIN_ABI_MAJOR 5`, `ARKSH_PLUGIN_ABI_MINOR 0`).

The host validates the major version at load time. A plugin declaring a higher
major version is rejected. A plugin declaring a lower minor version is accepted
(backwards-compatible additions only happen on minor bumps).

---

## Quick Start

Copy the skeleton template to your plugin directory:

```bash
cp -r plugins/skeleton plugins/myplugin
cd plugins/myplugin
mv skeleton_plugin.c myplugin.c
```

The skeleton already has all registration examples. Edit the `TODO` markers
to replace stub logic with your own.

---

## Required Exports

A plugin must export **two** symbols:

### `arksh_plugin_query`

Called before loading. Returns plugin metadata without side effects.

```c
ARKSH_PLUGIN_EXPORT int arksh_plugin_query(ArkshPluginInfo *out_info);
```

Fill `out_info`:

```c
int arksh_plugin_query(ArkshPluginInfo *out_info) {
  if (out_info == NULL) return 1;
  memset(out_info, 0, sizeof(*out_info));
  snprintf(out_info->name,        sizeof(out_info->name),        "my-plugin");
  snprintf(out_info->version,     sizeof(out_info->version),     "1.0.0");
  snprintf(out_info->description, sizeof(out_info->description), "brief description");
  out_info->abi_major = ARKSH_PLUGIN_ABI_MAJOR;
  out_info->abi_minor = ARKSH_PLUGIN_ABI_MINOR;
  out_info->required_host_capabilities =
    ARKSH_PLUGIN_CAP_COMMANDS | ARKSH_PLUGIN_CAP_VALUE_RESOLVERS;
  out_info->plugin_capabilities =
    ARKSH_PLUGIN_CAP_COMMANDS | ARKSH_PLUGIN_CAP_VALUE_RESOLVERS;
  return 0;
}
```

Declare only the capabilities you actually use. The host checks that it
provides everything listed in `required_host_capabilities` before proceeding.

### `arksh_plugin_init`

Called after the ABI check. Register all commands and extensions here.
Return 0 on success, 1 on failure.

```c
ARKSH_PLUGIN_EXPORT int arksh_plugin_init(
    ArkshShell *shell,
    const ArkshPluginHost *host,
    ArkshPluginInfo *out_info);
```

**Optional:** `arksh_plugin_shutdown` — called when the plugin is unloaded.

```c
ARKSH_PLUGIN_EXPORT void arksh_plugin_shutdown(ArkshShell *shell);
```

---

## Registration APIs

All registration functions are available through the `ArkshPluginHost *host`
pointer passed to `arksh_plugin_init`. Each returns 0 on success, non-zero on
failure.

### Commands

```c
int (*register_command)(
    ArkshShell *shell,
    const char *name,
    const char *description,
    ArkshCommandFn fn);
```

Callback signature:

```c
typedef int (*ArkshCommandFn)(
    ArkshShell *shell,
    int argc,
    char **argv,
    char *out,
    size_t out_size);
```

- `argv[0]` is the command name; `argc >= 1` always.
- Write output to `out` (NUL-terminated, max `out_size` bytes).
- Return 0 for success, non-zero for failure.

Example:

```c
static int cmd_hello(ArkshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  const char *target = argc >= 2 ? argv[1] : "world";
  (void) shell;
  snprintf(out, out_size, "hello, %s", target);
  return 0;
}

// in arksh_plugin_init:
host->register_command(shell, "hello", "greet someone", cmd_hello);
```

Usage from the shell: `hello arksh` → prints `hello, arksh`.

---

### Value Resolvers

Value resolvers are callable as `name()` or `name(arg1, arg2, ...)` and
return typed `ArkshValue` objects usable in object pipelines.

```c
int (*register_value_resolver)(
    ArkshShell *shell,
    const char *name,
    const char *description,
    ArkshValueResolverFn fn);
```

Callback signature:

```c
typedef int (*ArkshValueResolverFn)(
    ArkshShell *shell,
    int argc,
    const ArkshValue *args,
    ArkshValue *out_value,
    char *error,
    size_t error_size);
```

- `args` is an array of `argc` typed values (already evaluated by the shell).
- Write the result into `out_value` — do **not** free it yourself.
- Return 0 on success; write to `error` and return non-zero on failure.

Example — return a typed map:

```c
static int resolver_weather(ArkshShell *shell, int argc, const ArkshValue *args,
                             ArkshValue *out_value, char *error, size_t error_size) {
  ArkshValue temp, unit;
  (void) shell; (void) argc; (void) args;
  arksh_value_set_number(&temp, 22.5);
  arksh_value_set_string(&unit, "C");
  arksh_value_set_typed_map(out_value, "weather");
  arksh_value_map_set(out_value, "temp", &temp);
  arksh_value_map_set(out_value, "unit", &unit);
  arksh_value_free(&temp);
  arksh_value_free(&unit);
  return 0;
}
```

Usage: `weather() -> temp`

---

### Pipeline Stages

Pipeline stages are applied to a value flowing through a `|>` pipeline.
They mutate the value in-place.

```c
int (*register_pipeline_stage)(
    ArkshShell *shell,
    const char *name,
    const char *description,
    ArkshPipelineStageFn fn);
```

Callback signature:

```c
typedef int (*ArkshPipelineStageFn)(
    ArkshShell *shell,
    ArkshValue *value,
    const char *raw_args,
    char *error,
    size_t error_size);
```

- `value` — the current pipeline value; modify it in-place.
- `raw_args` — unparsed argument string from the pipeline expression (may be empty).
- Return 0 on success, non-zero on failure.

Example — uppercase all string values in a list:

```c
static int stage_upper(ArkshShell *shell, ArkshValue *value,
                        const char *raw_args, char *error, size_t error_size) {
  char buf[ARKSH_MAX_OUTPUT];
  size_t i;
  (void) shell; (void) raw_args;
  if (value->kind != ARKSH_VALUE_LIST) {
    snprintf(error, error_size, "upper: expected a list");
    return 1;
  }
  for (i = 0; i < value->list.count; ++i) {
    if (arksh_value_render(&value->list.items[i], buf, sizeof(buf)) == 0) {
      /* convert to uppercase in buf, then update */
      /* ... */
    }
  }
  return 0;
}
```

Usage: `list("hello", "world") |> upper()`

---

### Property Extensions

Add a computed read-only property to objects of a given target type.

```c
int (*register_property_extension)(
    ArkshShell *shell,
    const char *target,
    const char *name,
    ArkshExtensionPropertyFn fn);
```

**Target values:**

| Target string | Applies to |
|---------------|-----------|
| `"object"` | any filesystem object (file, directory, symlink, …) |
| `"file"` | files only |
| `"directory"` | directories only |
| `"string"` | string-typed values |
| `"number"` | numeric values |
| `"list"` | list values |
| `"dict"` | Dict typed maps |
| `"my-type"` | custom typed maps registered via `register_type_descriptor` |

Callback signature:

```c
typedef int (*ArkshExtensionPropertyFn)(
    ArkshShell *shell,
    const ArkshValue *receiver,  /* read-only */
    ArkshValue *out_value,
    char *error,
    size_t error_size);
```

Example:

```c
static int prop_md5(ArkshShell *shell, const ArkshValue *receiver,
                    ArkshValue *out_value, char *error, size_t error_size) {
  /* compute md5 of receiver's file path and set out_value */
  (void) shell;
  /* ... */
  arksh_value_set_string(out_value, "d41d8cd98f00b204e9800998ecf8427e");
  return 0;
}

// register for files only:
host->register_property_extension(shell, "file", "md5", prop_md5);
```

Usage: `README.md -> md5`

---

### Method Extensions

Add a callable method to objects of a given target type.

```c
int (*register_method_extension)(
    ArkshShell *shell,
    const char *target,
    const char *name,
    ArkshExtensionMethodFn fn);
```

Callback signature:

```c
typedef int (*ArkshExtensionMethodFn)(
    ArkshShell *shell,
    const ArkshValue *receiver,  /* read-only */
    int argc,
    const ArkshValue *args,      /* evaluated arguments */
    ArkshValue *out_value,
    char *error,
    size_t error_size);
```

Example:

```c
static int method_resize(ArkshShell *shell, const ArkshValue *receiver,
                          int argc, const ArkshValue *args,
                          ArkshValue *out_value, char *error, size_t error_size) {
  double width  = argc >= 1 ? args[0].number : 0;
  double height = argc >= 2 ? args[1].number : 0;
  (void) shell; (void) receiver;
  arksh_value_set_string(out_value, "resized");
  return 0;
}

host->register_method_extension(shell, "file", "resize", method_resize);
```

Usage: `photo.jpg -> resize(800, 600)`

---

### Type Descriptors

Register a named custom type so that property/method extensions can target it,
and so `help types` lists it.

```c
int (*register_type_descriptor)(
    ArkshShell *shell,
    const char *type_name,
    const char *description);
```

After registration, create typed-map values with:

```c
arksh_value_set_typed_map(out_value, "my-type");
arksh_value_map_set(out_value, "field", &some_value);
```

Extensions registered for target `"my-type"` then apply to values of this type.

---

## Memory Ownership Rules

These rules prevent double-frees and use-after-free bugs.

**`out_value` is owned by the core.** The core calls `arksh_value_init` before
the callback and `arksh_value_free` after it returns. Never free `out_value`.

```c
// CORRECT
arksh_value_set_string(out_value, "result");
return 0;

// WRONG — do not free out_value
arksh_value_free(out_value);   // crash
```

**Nested values passed to `arksh_value_map_set` are copied.** You can
stack-allocate them and free them after the call.

```c
ArkshValue entry;
arksh_value_set_string(&entry, "hello");
arksh_value_map_set(map, "key", &entry);
arksh_value_free(&entry);    // safe: map holds its own copy
```

**`receiver` is read-only.** Do not write to it.

**`value` in pipeline stage callbacks is mutable.** Replace its content in-place
to transform the pipeline value.

---

## CMake Integration

```cmake
cmake_minimum_required(VERSION 3.20)
project(my_plugin C)

find_package(arksh REQUIRED)   # or set include/lib manually

add_library(my_plugin SHARED myplugin.c)
target_include_directories(my_plugin PRIVATE ${ARKSH_INCLUDE_DIRS})

if(APPLE)
  target_link_options(my_plugin PRIVATE "-undefined" "dynamic_lookup")
endif()

install(TARGETS my_plugin
  LIBRARY DESTINATION "${ARKSH_PLUGIN_INSTALL_DIR}"
  RUNTIME DESTINATION "${ARKSH_PLUGIN_INSTALL_DIR}"
)
```

Build:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Output on macOS: `build/libmy_plugin.dylib`
Output on Linux: `build/libmy_plugin.so`

---

## Testing a Plugin

Load interactively:

```bash
plugin load build/libmy_plugin.dylib
plugin info my-plugin
plugin list
```

From a test script:

```bash
arksh -c 'plugin load build/libmy_plugin.dylib ; hello arksh'
```

Autoload during development (edit `~/.config/arksh/plugin-autoload.conf`):

```ini
/absolute/path/to/build/libmy_plugin.dylib
```

---

## Autoload Configuration

The plugin autoload file lists one plugin per line (name or path):

```ini
# ~/.config/arksh/plugin-autoload.conf
git-prompt-plugin
/home/user/.local/share/arksh/plugins/libmy_plugin.so
```

Check what is loaded:

```text
plugin autoload list
plugin list
```

---

## Capability Checklist

Before releasing a plugin, verify:

- [ ] `arksh_plugin_query` compiles and fills all fields
- [ ] `abi_major` == `ARKSH_PLUGIN_ABI_MAJOR`
- [ ] `required_host_capabilities` lists only what the plugin uses
- [ ] `plugin_capabilities` lists only what the plugin provides
- [ ] All registered names are unique and follow `snake_case` or `kebab-case`
- [ ] All callbacks return non-zero and write to `error` on failure
- [ ] `out_value` is never freed inside a callback
- [ ] Nested `ArkshValue` temporaries are freed after `arksh_value_map_set`
- [ ] Tested with `plugin info <name>` showing correct ABI and capabilities
- [ ] Tested with `plugin disable` / `plugin enable` round-trip
- [ ] ABI rejection tested: build a copy with `abi_major` bumped and verify `plugin load` fails

---

## Example Plugins in the Repository

| Plugin | Path | What it shows |
|--------|------|---------------|
| `skeleton-plugin` | `plugins/skeleton/` | Annotated template with all registration types |
| `sample-plugin` | `plugins/sample/` | Complete working example |
| `git-prompt-plugin` | `plugins/git/` | Real-world integration with external tool |
| `point-plugin` | `plugins/point/` | Custom type + typed map resolver + method |
| `trash-plugin` | `plugins/trash/` | Platform-specific API (macOS Finder trash) |
