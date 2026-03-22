#ifndef ARKSH_ARENA_H
#define ARKSH_ARENA_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ArkshShell;
typedef struct ArkshShell ArkshShell;

typedef struct ArkshScratchChunk ArkshScratchChunk;

typedef struct {
  ArkshScratchChunk *head;
  ArkshScratchChunk *current;
  size_t default_chunk_size;
} ArkshScratchArena;

typedef struct {
  ArkshShell *shell;
  ArkshShell *previous_shell;
  ArkshScratchChunk *chunk;
  size_t used;
  int active;
} ArkshScratchFrame;

void arksh_scratch_arena_init(ArkshScratchArena *arena);
void arksh_scratch_arena_destroy(ArkshScratchArena *arena);
void arksh_scratch_arena_reset(ArkshScratchArena *arena);
void *arksh_scratch_alloc_zero(ArkshScratchArena *arena, size_t count, size_t item_size);
void arksh_scratch_frame_begin(ArkshShell *shell, ArkshScratchFrame *frame);
void arksh_scratch_frame_end(ArkshScratchFrame *frame);
void *arksh_scratch_alloc_active_zero(size_t count, size_t item_size);
int arksh_scratch_active_contains(const void *ptr);

#ifdef __cplusplus
}
#endif

#endif
