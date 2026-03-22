#ifndef ARKSH_PERF_H
#define ARKSH_PERF_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  int enabled;
  unsigned long long malloc_calls;
  unsigned long long calloc_calls;
  unsigned long long realloc_calls;
  unsigned long long free_calls;
  unsigned long long malloc_bytes;
  unsigned long long calloc_bytes;
  unsigned long long realloc_bytes;
  unsigned long long temp_buffer_calls;
  unsigned long long temp_buffer_bytes;
  unsigned long long value_copy_calls;
  unsigned long long value_render_calls;
} ArkshPerfCounters;

void arksh_perf_enable(int enabled);
int arksh_perf_is_enabled(void);
void arksh_perf_reset(void);
void arksh_perf_snapshot(ArkshPerfCounters *out);
void arksh_perf_note_temp_buffer(size_t count, size_t item_size);
void arksh_perf_note_value_copy(void);
void arksh_perf_note_value_render(void);
void *arksh_perf_malloc_impl(size_t size);
void *arksh_perf_calloc_impl(size_t count, size_t size);
void *arksh_perf_realloc_impl(void *ptr, size_t size);
void arksh_perf_free_impl(void *ptr);

#if !defined(ARKSH_PERF_NO_ALLOC_MACROS)
#define malloc(size) arksh_perf_malloc_impl((size))
#define calloc(count, size) arksh_perf_calloc_impl((count), (size))
#define realloc(ptr, size) arksh_perf_realloc_impl((ptr), (size))
#define free(ptr) arksh_perf_free_impl((ptr))
#endif

#ifdef __cplusplus
}
#endif

#endif
