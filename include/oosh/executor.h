#ifndef OOSH_EXECUTOR_H
#define OOSH_EXECUTOR_H

#include <stddef.h>

#include "oosh/ast.h"

#ifdef __cplusplus
extern "C" {
#endif

struct OoshShell;
typedef struct OoshShell OoshShell;

int oosh_execute_ast(OoshShell *shell, const OoshAst *ast, char *out, size_t out_size);
int oosh_execute_external_command(OoshShell *shell, int argc, char **argv, char *out, size_t out_size);
int oosh_evaluate_line_value(OoshShell *shell, const char *line, OoshValue *value, char *out, size_t out_size);
int oosh_execute_block(OoshShell *shell, const OoshBlock *block, const OoshValue *args, int argc, OoshValue *out_value, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif
