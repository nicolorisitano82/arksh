#ifndef OOSH_PROMPT_H
#define OOSH_PROMPT_H

#include <stddef.h>

#include "oosh/object.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OOSH_MAX_SEGMENTS 8
#define OOSH_MAX_SEGMENT_NAME 32
#define OOSH_MAX_PLUGIN_PATHS 8

struct OoshShell;

typedef struct {
  char segment[OOSH_MAX_SEGMENT_NAME];
  char color[16];
} OoshPromptSegmentStyle;

typedef struct {
  char theme[64];
  char separator[16];
  int use_color;
  char left[OOSH_MAX_SEGMENTS][OOSH_MAX_SEGMENT_NAME];
  size_t left_count;
  char right[OOSH_MAX_SEGMENTS][OOSH_MAX_SEGMENT_NAME];
  size_t right_count;
  OoshPromptSegmentStyle styles[OOSH_MAX_SEGMENTS * 2];
  size_t style_count;
  char plugins[OOSH_MAX_PLUGIN_PATHS][OOSH_MAX_PATH];
  size_t plugin_count;
} OoshPromptConfig;

void oosh_prompt_config_init(OoshPromptConfig *config);
int oosh_prompt_config_load(OoshPromptConfig *config, const char *path);
void oosh_prompt_render(const OoshPromptConfig *config, const struct OoshShell *shell, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif
