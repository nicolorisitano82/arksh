#ifndef ARKSH_PROMPT_H
#define ARKSH_PROMPT_H

#include <stddef.h>

#include "arksh/object.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ARKSH_MAX_SEGMENTS 8
#define ARKSH_MAX_SEGMENT_NAME 32
#define ARKSH_MAX_PLUGIN_PATHS 8

struct ArkshShell;

typedef struct {
  char segment[ARKSH_MAX_SEGMENT_NAME];
  char color[16];
} ArkshPromptSegmentStyle;

typedef struct {
  char theme[64];
  char separator[16];
  int use_color;
  char left[ARKSH_MAX_SEGMENTS][ARKSH_MAX_SEGMENT_NAME];
  size_t left_count;
  char right[ARKSH_MAX_SEGMENTS][ARKSH_MAX_SEGMENT_NAME];
  size_t right_count;
  ArkshPromptSegmentStyle styles[ARKSH_MAX_SEGMENTS * 2];
  size_t style_count;
  char plugins[ARKSH_MAX_PLUGIN_PATHS][ARKSH_MAX_PATH];
  size_t plugin_count;
  char continuation[64];
} ArkshPromptConfig;

void arksh_prompt_config_init(ArkshPromptConfig *config);
int arksh_prompt_config_load(ArkshPromptConfig *config, const char *path);
void arksh_prompt_render(const ArkshPromptConfig *config, const struct ArkshShell *shell, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif
