#ifndef OOSH_PARSER_H
#define OOSH_PARSER_H

#include "oosh/ast.h"

#ifdef __cplusplus
extern "C" {
#endif

int oosh_parse_line(const char *line, OoshAst *out_ast, char *error, size_t error_size);
int oosh_parse_value_line(const char *line, OoshAst *out_ast, char *error, size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
