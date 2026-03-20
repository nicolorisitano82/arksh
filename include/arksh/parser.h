#ifndef ARKSH_PARSER_H
#define ARKSH_PARSER_H

#include "arksh/ast.h"

#ifdef __cplusplus
extern "C" {
#endif

int arksh_parse_line(const char *line, ArkshAst *out_ast, char *error, size_t error_size);
int arksh_parse_value_line(const char *line, ArkshAst *out_ast, char *error, size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
