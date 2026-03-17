#ifndef OOSH_LINE_EDITOR_H
#define OOSH_LINE_EDITOR_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct OoshShell;
typedef struct OoshShell OoshShell;

typedef enum {
  OOSH_LINE_READ_OK = 0,
  OOSH_LINE_READ_EOF,
  OOSH_LINE_READ_ERROR
} OoshLineReadStatus;

/* Returns >0 if more input is needed, 0 if complete, <0 on parse error. */
typedef int (*OoshLineEditorNeedsMoreFn)(const char *text);

int oosh_line_editor_is_interactive(void);
OoshLineReadStatus oosh_line_editor_read_line(
  OoshShell *shell,
  const char *prompt,
  const char *prompt_continue,
  OoshLineEditorNeedsMoreFn needs_more,
  char *out,
  size_t out_size
);

#ifdef __cplusplus
}
#endif

#endif
