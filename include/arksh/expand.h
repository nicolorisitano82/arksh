#ifndef ARKSH_EXPAND_H
#define ARKSH_EXPAND_H

#include <stddef.h>

#include "arksh/object.h"

#ifdef __cplusplus
extern "C" {
#endif

struct ArkshShell;
typedef struct ArkshShell ArkshShell;

typedef enum {
  ARKSH_EXPAND_MODE_COMMAND = 0,
  ARKSH_EXPAND_MODE_COMMAND_NAME,
  ARKSH_EXPAND_MODE_OBJECT_SELECTOR,
  ARKSH_EXPAND_MODE_OBJECT_ARGUMENT,
  ARKSH_EXPAND_MODE_REDIRECT_TARGET
} ArkshExpandMode;

int arksh_expand_word(
  ArkshShell *shell,
  const char *raw,
  ArkshExpandMode mode,
  char out_values[][ARKSH_MAX_TOKEN],
  int max_values,
  int *out_count,
  char *error,
  size_t error_size
);

#ifdef __cplusplus
}
#endif

#endif
