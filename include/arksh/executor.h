#ifndef ARKSH_EXECUTOR_H
#define ARKSH_EXECUTOR_H

#include <stddef.h>

#include "arksh/ast.h"

#ifdef __cplusplus
extern "C" {
#endif

struct ArkshShell;
typedef struct ArkshShell ArkshShell;

int arksh_execute_ast(ArkshShell *shell, const ArkshAst *ast, char *out, size_t out_size);
int arksh_execute_external_command(ArkshShell *shell, int argc, char **argv, char *out, size_t out_size);
int arksh_evaluate_line_value(ArkshShell *shell, const char *line, ArkshValue *value, char *out, size_t out_size);
int arksh_execute_block(ArkshShell *shell, const ArkshBlock *block, const ArkshValue *args, int argc, ArkshValue *out_value, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif
