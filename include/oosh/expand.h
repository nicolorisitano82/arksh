#ifndef OOSH_EXPAND_H
#define OOSH_EXPAND_H

#include <stddef.h>

#include "oosh/object.h"

#ifdef __cplusplus
extern "C" {
#endif

struct OoshShell;
typedef struct OoshShell OoshShell;

typedef enum {
  OOSH_EXPAND_MODE_COMMAND = 0,
  OOSH_EXPAND_MODE_COMMAND_NAME,
  OOSH_EXPAND_MODE_OBJECT_SELECTOR,
  OOSH_EXPAND_MODE_OBJECT_ARGUMENT,
  OOSH_EXPAND_MODE_REDIRECT_TARGET
} OoshExpandMode;

int oosh_expand_word(
  OoshShell *shell,
  const char *raw,
  OoshExpandMode mode,
  char out_values[][OOSH_MAX_TOKEN],
  int max_values,
  int *out_count,
  char *error,
  size_t error_size
);

#ifdef __cplusplus
}
#endif

#endif
