#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arksh/arena.h"
#include "arksh/shell.h"

static int g_failures = 0;

#define EXPECT(cond, msg)                                              \
  do {                                                                 \
    if (!(cond)) {                                                     \
      fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, (msg)); \
      g_failures++;                                                    \
    }                                                                  \
  } while (0)

static void test_arena_reset_zeroes_reused_memory(void) {
  ArkshScratchArena arena;
  int *values;
  int *again;

  arksh_scratch_arena_init(&arena);
  values = (int *) arksh_scratch_alloc_zero(&arena, 4, sizeof(*values));
  EXPECT(values != NULL, "arena alloc returns memory");
  if (values != NULL) {
    EXPECT(values[0] == 0 && values[3] == 0, "arena alloc is zeroed");
    values[0] = 11;
    values[1] = 22;
  }

  arksh_scratch_arena_reset(&arena);
  again = (int *) arksh_scratch_alloc_zero(&arena, 4, sizeof(*again));
  EXPECT(again != NULL, "arena alloc after reset returns memory");
  if (again != NULL) {
    EXPECT(again[0] == 0 && again[1] == 0, "arena reset zeroes reused memory");
  }

  arksh_scratch_arena_destroy(&arena);
}

static void test_arena_large_alloc(void) {
  ArkshScratchArena arena;
  char *buffer;
  size_t i;

  arksh_scratch_arena_init(&arena);
  buffer = (char *) arksh_scratch_alloc_zero(&arena, 128u * 1024u, sizeof(*buffer));
  EXPECT(buffer != NULL, "arena alloc grows for large request");
  if (buffer != NULL) {
    for (i = 0; i < 16; ++i) {
      EXPECT(buffer[i] == '\0', "large arena allocation starts zeroed");
    }
  }
  arksh_scratch_arena_destroy(&arena);
}

static void test_scratch_frame_reuses_inner_space(void) {
  ArkshShell *shell;
  ArkshScratchFrame outer;
  ArkshScratchFrame inner;
  char *first_inner;
  char *second_inner;

  shell = (ArkshShell *) calloc(1, sizeof(*shell));
  EXPECT(shell != NULL, "shell allocation for frame test");
  if (shell == NULL) {
    return;
  }

  arksh_scratch_arena_init(&shell->scratch);
  arksh_scratch_frame_begin(shell, &outer);
  (void) arksh_scratch_alloc_active_zero(32, 1);

  arksh_scratch_frame_begin(shell, &inner);
  first_inner = (char *) arksh_scratch_alloc_active_zero(64, 1);
  EXPECT(first_inner != NULL, "inner frame alloc");
  if (first_inner != NULL) {
    memset(first_inner, 1, 64);
  }
  arksh_scratch_frame_end(&inner);

  second_inner = (char *) arksh_scratch_alloc_active_zero(64, 1);
  EXPECT(second_inner != NULL, "reused inner frame alloc");
  EXPECT(second_inner == first_inner, "inner frame reset reuses same region");
  if (second_inner != NULL) {
    EXPECT(second_inner[0] == 0, "reused inner frame memory is zeroed");
  }

  arksh_scratch_frame_end(&outer);
  arksh_scratch_arena_destroy(&shell->scratch);
  free(shell);
}

int main(void) {
  test_arena_reset_zeroes_reused_memory();
  test_arena_large_alloc();
  test_scratch_frame_reuses_inner_space();

  if (g_failures != 0) {
    fprintf(stderr, "arena tests failed: %d\n", g_failures);
    return 1;
  }

  return 0;
}
