#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "arksh/arena.h"
#include "arksh/perf.h"
#include "arksh/shell.h"

struct ArkshScratchChunk {
  struct ArkshScratchChunk *next;
  size_t capacity;
  size_t used;
  unsigned char data[];
};

static ArkshShell *g_active_scratch_shell = NULL;

static size_t align_up(size_t value, size_t alignment) {
  size_t mask;

  if (alignment == 0) {
    return value;
  }
  mask = alignment - 1;
  return (value + mask) & ~mask;
}

static int multiply_size(size_t left, size_t right, size_t *out) {
  if (out == NULL) {
    return 1;
  }
  if (left == 0 || right == 0) {
    *out = 0;
    return 0;
  }
  if (left > ((size_t) -1) / right) {
    return 1;
  }
  *out = left * right;
  return 0;
}

static ArkshScratchChunk *allocate_chunk(size_t capacity) {
  ArkshScratchChunk *chunk;

  chunk = (ArkshScratchChunk *) malloc(sizeof(*chunk) + capacity);
  if (chunk == NULL) {
    return NULL;
  }
  chunk->next = NULL;
  chunk->capacity = capacity;
  chunk->used = 0;
  return chunk;
}

void arksh_scratch_arena_init(ArkshScratchArena *arena) {
  if (arena == NULL) {
    return;
  }

  memset(arena, 0, sizeof(*arena));
  arena->default_chunk_size = 64u * 1024u;
}

void arksh_scratch_arena_destroy(ArkshScratchArena *arena) {
  ArkshScratchChunk *chunk;
  ArkshScratchChunk *next;

  if (arena == NULL) {
    return;
  }

  chunk = arena->head;
  while (chunk != NULL) {
    next = chunk->next;
    free(chunk);
    chunk = next;
  }

  arena->head = NULL;
  arena->current = NULL;
}

void arksh_scratch_arena_reset(ArkshScratchArena *arena) {
  ArkshScratchChunk *chunk;

  if (arena == NULL) {
    return;
  }

  for (chunk = arena->head; chunk != NULL; chunk = chunk->next) {
    chunk->used = 0;
  }
  arena->current = arena->head;
}

void *arksh_scratch_alloc_zero(ArkshScratchArena *arena, size_t count, size_t item_size) {
  ArkshScratchChunk *chunk;
  size_t bytes;
  size_t offset;
  size_t alignment = sizeof(max_align_t);
  void *result;

  if (arena == NULL || count == 0 || item_size == 0) {
    return NULL;
  }

  if (multiply_size(count, item_size, &bytes) != 0) {
    return NULL;
  }

  if (arena->current == NULL) {
    if (arena->head != NULL) {
      arena->current = arena->head;
      arena->current->used = 0;
    } else {
      size_t chunk_capacity = arena->default_chunk_size;
      if (bytes + alignment > chunk_capacity) {
        chunk_capacity = bytes + alignment;
      }
      arena->head = allocate_chunk(chunk_capacity);
      if (arena->head == NULL) {
        return NULL;
      }
      arena->current = arena->head;
    }
  }

  chunk = arena->current;
  while (chunk != NULL) {
    offset = align_up(chunk->used, alignment);
    if (offset <= chunk->capacity && bytes <= chunk->capacity - offset) {
      result = chunk->data + offset;
      chunk->used = offset + bytes;
      memset(result, 0, bytes);
      arena->current = chunk;
      return result;
    }

    if (chunk->next != NULL) {
      chunk = chunk->next;
      chunk->used = 0;
      arena->current = chunk;
      continue;
    }

    {
      size_t chunk_capacity = arena->default_chunk_size;
      ArkshScratchChunk *next;

      if (bytes + alignment > chunk_capacity) {
        chunk_capacity = bytes + alignment;
      }
      next = allocate_chunk(chunk_capacity);
      if (next == NULL) {
        return NULL;
      }
      chunk->next = next;
      chunk = next;
      arena->current = chunk;
    }
  }

  return NULL;
}

void arksh_scratch_frame_begin(ArkshShell *shell, ArkshScratchFrame *frame) {
  if (frame == NULL) {
    return;
  }

  memset(frame, 0, sizeof(*frame));
  frame->shell = shell;
  frame->previous_shell = g_active_scratch_shell;
  if (shell != NULL) {
    frame->chunk = shell->scratch.current;
    frame->used = frame->chunk != NULL ? frame->chunk->used : 0;
  }
  g_active_scratch_shell = shell;
  frame->active = 1;
}

void arksh_scratch_frame_end(ArkshScratchFrame *frame) {
  ArkshScratchChunk *chunk;

  if (frame == NULL || !frame->active) {
    return;
  }

  if (frame->shell != NULL) {
    if (frame->chunk == NULL) {
      chunk = frame->shell->scratch.head;
      while (chunk != NULL) {
        chunk->used = 0;
        chunk = chunk->next;
      }
      frame->shell->scratch.current = frame->shell->scratch.head;
    } else {
      frame->chunk->used = frame->used;
      chunk = frame->chunk->next;
      while (chunk != NULL) {
        chunk->used = 0;
        chunk = chunk->next;
      }
      frame->shell->scratch.current = frame->chunk;
    }
  }

  g_active_scratch_shell = frame->previous_shell;
  frame->active = 0;
}

void *arksh_scratch_alloc_active_zero(size_t count, size_t item_size) {
  if (g_active_scratch_shell == NULL) {
    return calloc(count, item_size);
  }

  return arksh_scratch_alloc_zero(&g_active_scratch_shell->scratch, count, item_size);
}

int arksh_scratch_active_contains(const void *ptr) {
  const unsigned char *needle = (const unsigned char *) ptr;
  ArkshScratchChunk *chunk;

  if (g_active_scratch_shell == NULL || ptr == NULL) {
    return 0;
  }

  for (chunk = g_active_scratch_shell->scratch.head; chunk != NULL; chunk = chunk->next) {
    const unsigned char *start = chunk->data;
    const unsigned char *end = chunk->data + chunk->capacity;

    if (needle >= start && needle < end) {
      return 1;
    }
  }

  return 0;
}
