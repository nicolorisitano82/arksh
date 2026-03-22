#include <stdlib.h>
#include <string.h>

#define ARKSH_PERF_NO_ALLOC_MACROS
#include "arksh/arena.h"
#include "arksh/perf.h"

static ArkshPerfCounters g_perf_counters;

static unsigned long long multiply_ull(size_t left, size_t right) {
  if (left == 0 || right == 0) {
    return 0;
  }
  if (left > ((size_t) -1) / right) {
    return (unsigned long long) -1;
  }
  return (unsigned long long) (left * right);
}

void arksh_perf_enable(int enabled) {
  g_perf_counters.enabled = enabled != 0;
}

int arksh_perf_is_enabled(void) {
  return g_perf_counters.enabled;
}

void arksh_perf_reset(void) {
  int enabled = g_perf_counters.enabled;

  memset(&g_perf_counters, 0, sizeof(g_perf_counters));
  g_perf_counters.enabled = enabled;
}

void arksh_perf_snapshot(ArkshPerfCounters *out) {
  if (out == NULL) {
    return;
  }
  *out = g_perf_counters;
}

void arksh_perf_note_temp_buffer(size_t count, size_t item_size) {
  if (!g_perf_counters.enabled) {
    return;
  }
  g_perf_counters.temp_buffer_calls++;
  g_perf_counters.temp_buffer_bytes += multiply_ull(count, item_size);
}

void arksh_perf_note_value_copy(void) {
  if (!g_perf_counters.enabled) {
    return;
  }
  g_perf_counters.value_copy_calls++;
}

void arksh_perf_note_value_render(void) {
  if (!g_perf_counters.enabled) {
    return;
  }
  g_perf_counters.value_render_calls++;
}

void *arksh_perf_malloc_impl(size_t size) {
  if (g_perf_counters.enabled) {
    g_perf_counters.malloc_calls++;
    g_perf_counters.malloc_bytes += (unsigned long long) size;
  }
  return malloc(size);
}

void *arksh_perf_calloc_impl(size_t count, size_t size) {
  if (g_perf_counters.enabled) {
    g_perf_counters.calloc_calls++;
    g_perf_counters.calloc_bytes += multiply_ull(count, size);
  }
  return calloc(count, size);
}

void *arksh_perf_realloc_impl(void *ptr, size_t size) {
  if (g_perf_counters.enabled) {
    g_perf_counters.realloc_calls++;
    g_perf_counters.realloc_bytes += (unsigned long long) size;
  }
  return realloc(ptr, size);
}

void arksh_perf_free_impl(void *ptr) {
  if (ptr == NULL) {
    return;
  }

  if (arksh_scratch_active_contains(ptr)) {
    return;
  }

  if (g_perf_counters.enabled) {
    g_perf_counters.free_calls++;
  }

  free(ptr);
}
