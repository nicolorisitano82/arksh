/* E8-S3-T3: libFuzzer entry point for lexer, parser and expander.
 *
 * Build (requires clang with libFuzzer support):
 *   cmake -S . -B build-fuzz -DARKSH_FUZZ=ON -DCMAKE_BUILD_TYPE=Debug \
 *         -DCMAKE_C_COMPILER=clang
 *   cmake --build build-fuzz --target arksh_fuzz_input
 *   ./build-fuzz/arksh_fuzz_input [corpus/] [-max_len=512] [-runs=100000]
 *
 * The fuzzer feeds arbitrary bytes to:
 *   1. arksh_lex_line    — must never crash or corrupt memory
 *   2. arksh_parse_line  — must never crash on any token stream
 *   3. arksh_parse_value_line — must never crash on any input
 *
 * All inputs are NUL-terminated copies of the fuzzer data; non-printable
 * bytes are allowed (they exercise unusual escaping / quoting paths).
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "arksh/lexer.h"
#include "arksh/parser.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  /* Build a NUL-terminated copy; cap at 4096 bytes to avoid trivially OOM. */
  char line[4097];
  size_t len = size < 4096 ? size : 4096;
  memcpy(line, data, len);
  line[len] = '\0';

  /* 1. Lexer — must not crash */
  {
    ArkshTokenStream stream;
    char error[256];
    stream.count = 0;
    arksh_lex_line(line, &stream, error, sizeof(error));
  }

  /* 2. Parser (command line) — must not crash */
  {
    ArkshAst ast;
    char error[512];
    arksh_ast_init(&ast);
    arksh_parse_line(line, &ast, error, sizeof(error));
  }

  /* 3. Parser (value line) — must not crash */
  {
    ArkshAst ast;
    char error[512];
    arksh_ast_init(&ast);
    arksh_parse_value_line(line, &ast, error, sizeof(error));
  }

  return 0;
}
