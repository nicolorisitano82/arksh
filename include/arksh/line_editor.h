#ifndef ARKSH_LINE_EDITOR_H
#define ARKSH_LINE_EDITOR_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ArkshShell;
typedef struct ArkshShell ArkshShell;

typedef enum {
  ARKSH_LINE_READ_OK = 0,
  ARKSH_LINE_READ_EOF,
  ARKSH_LINE_READ_ERROR
} ArkshLineReadStatus;

/* Returns >0 if more input is needed, 0 if complete, <0 on parse error. */
typedef int (*ArkshLineEditorNeedsMoreFn)(const char *text);

int arksh_line_editor_is_interactive(void);
ArkshLineReadStatus arksh_line_editor_read_line(
  ArkshShell *shell,
  const char *prompt,
  const char *prompt_continue,
  ArkshLineEditorNeedsMoreFn needs_more,
  char *out,
  size_t out_size
);

#ifdef __cplusplus
}
#endif

#endif
