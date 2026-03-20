/*
 * point_plugin.c — E6-S2-T3: example plugin with a complete custom type.
 *
 * Registers a "Point" typed-map type:
 *
 *   point(x, y)          resolver — creates a Point value
 *   p -> type            returns "Point"
 *   p -> x               x coordinate (number)
 *   p -> y               y coordinate (number)
 *   p -> distance()      Euclidean distance from origin (sqrt(x²+y²))
 *   p -> translate(dx, dy)  returns a new Point(x+dx, y+dy)
 *   value |> as_point    pipeline stage — parses "x,y" text into a Point
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arksh/plugin.h"

#define POINT_TYPE_NAME "Point"

/* ------------------------------------------------------------------ helpers */

static int point_add_number(ArkshValue *map, const char *key, double n) {
  ArkshValue entry;
  int status;

  arksh_value_set_number(&entry, n);
  status = arksh_value_map_set(map, key, &entry);
  arksh_value_free(&entry);
  return status;
}

static double point_get_coord(const ArkshValue *point, const char *key) {
  const ArkshValueItem *entry = arksh_value_map_get_item(point, key);

  if (entry == NULL) {
    return 0.0;
  }
  if (entry->kind == ARKSH_VALUE_NUMBER) {
    return entry->number;
  }
  /* STRING fallback (e.g. after JSON roundtrip) */
  if (entry->kind == ARKSH_VALUE_STRING) {
    return atof(entry->text);
  }
  return 0.0;
}

static int make_point(ArkshValue *out, double x, double y) {
  arksh_value_set_typed_map(out, POINT_TYPE_NAME);
  if (point_add_number(out, "x", x) != 0 ||
      point_add_number(out, "y", y) != 0) {
    return 1;
  }
  return 0;
}

/* ------------------------------------------------------------------ resolver */

static int point_resolver(
  ArkshShell *shell,
  int argc,
  const ArkshValue *args,
  ArkshValue *out_value,
  char *error,
  size_t error_size
) {
  double x = 0.0;
  double y = 0.0;
  char rendered[ARKSH_MAX_OUTPUT];

  (void) shell;

  if (out_value == NULL || error == NULL || error_size == 0) {
    return 1;
  }
  if (argc != 2) {
    snprintf(error, error_size, "point(x, y) requires exactly 2 arguments");
    return 1;
  }

  if (arksh_value_render(&args[0], rendered, sizeof(rendered)) != 0) {
    snprintf(error, error_size, "point(): unable to render x argument");
    return 1;
  }
  x = atof(rendered);

  if (arksh_value_render(&args[1], rendered, sizeof(rendered)) != 0) {
    snprintf(error, error_size, "point(): unable to render y argument");
    return 1;
  }
  y = atof(rendered);

  if (make_point(out_value, x, y) != 0) {
    snprintf(error, error_size, "point(): unable to build Point value");
    return 1;
  }
  return 0;
}

/* ------------------------------------------------------------------ properties */

static int point_prop_x(ArkshShell *shell, const ArkshValue *receiver, ArkshValue *out_value, char *error, size_t error_size) {
  (void) shell;

  if (receiver == NULL || out_value == NULL || error == NULL || error_size == 0) {
    return 1;
  }
  arksh_value_set_number(out_value, point_get_coord(receiver, "x"));
  return 0;
}

static int point_prop_y(ArkshShell *shell, const ArkshValue *receiver, ArkshValue *out_value, char *error, size_t error_size) {
  (void) shell;

  if (receiver == NULL || out_value == NULL || error == NULL || error_size == 0) {
    return 1;
  }
  arksh_value_set_number(out_value, point_get_coord(receiver, "y"));
  return 0;
}

/* ------------------------------------------------------------------ methods */

static int point_method_distance(
  ArkshShell *shell,
  const ArkshValue *receiver,
  int argc,
  const ArkshValue *args,
  ArkshValue *out_value,
  char *error,
  size_t error_size
) {
  double x, y;

  (void) shell;
  (void) args;

  if (receiver == NULL || out_value == NULL || error == NULL || error_size == 0) {
    return 1;
  }
  if (argc != 0) {
    snprintf(error, error_size, "distance() does not accept arguments");
    return 1;
  }

  x = point_get_coord(receiver, "x");
  y = point_get_coord(receiver, "y");
  arksh_value_set_number(out_value, sqrt(x * x + y * y));
  return 0;
}

static int point_method_translate(
  ArkshShell *shell,
  const ArkshValue *receiver,
  int argc,
  const ArkshValue *args,
  ArkshValue *out_value,
  char *error,
  size_t error_size
) {
  double dx = 0.0, dy = 0.0;
  char rendered[ARKSH_MAX_OUTPUT];

  (void) shell;

  if (receiver == NULL || out_value == NULL || error == NULL || error_size == 0) {
    return 1;
  }
  if (argc != 2) {
    snprintf(error, error_size, "translate(dx, dy) requires exactly 2 arguments");
    return 1;
  }

  if (arksh_value_render(&args[0], rendered, sizeof(rendered)) != 0) {
    snprintf(error, error_size, "translate(): unable to render dx");
    return 1;
  }
  dx = atof(rendered);

  if (arksh_value_render(&args[1], rendered, sizeof(rendered)) != 0) {
    snprintf(error, error_size, "translate(): unable to render dy");
    return 1;
  }
  dy = atof(rendered);

  if (make_point(out_value,
                 point_get_coord(receiver, "x") + dx,
                 point_get_coord(receiver, "y") + dy) != 0) {
    snprintf(error, error_size, "translate(): unable to build result Point");
    return 1;
  }
  return 0;
}

/* ------------------------------------------------------------------ pipeline stage */

static int point_stage_as_point(
  ArkshShell *shell,
  ArkshValue *value,
  const char *raw_args,
  char *error,
  size_t error_size
) {
  char text[ARKSH_MAX_OUTPUT];
  double x = 0.0, y = 0.0;
  const char *comma;

  (void) shell;
  (void) raw_args;

  if (value == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  if (arksh_value_render(value, text, sizeof(text)) != 0) {
    snprintf(error, error_size, "as_point: unable to render input value");
    return 1;
  }

  /* Accept "x,y" or "x y" format */
  comma = strchr(text, ',');
  if (comma == NULL) {
    comma = strchr(text, ' ');
  }
  if (comma == NULL) {
    snprintf(error, error_size, "as_point: expected \"x,y\" format, got: %s", text);
    return 1;
  }

  x = atof(text);
  y = atof(comma + 1);

  if (make_point(value, x, y) != 0) {
    snprintf(error, error_size, "as_point: unable to build Point value");
    return 1;
  }
  return 0;
}

/* ------------------------------------------------------------------ init */

ARKSH_PLUGIN_EXPORT int arksh_plugin_init(ArkshShell *shell, const ArkshPluginHost *host, ArkshPluginInfo *out_info) {
  if (shell == NULL || host == NULL || out_info == NULL) {
    return 1;
  }

  if (host->api_version != ARKSH_PLUGIN_API_VERSION ||
      host->register_command == NULL ||
      host->register_property_extension == NULL ||
      host->register_method_extension == NULL ||
      host->register_value_resolver == NULL ||
      host->register_pipeline_stage == NULL ||
      host->register_type_descriptor == NULL) {
    return 1;
  }

  snprintf(out_info->name,        sizeof(out_info->name),        "point-plugin");
  snprintf(out_info->version,     sizeof(out_info->version),     "1.0.0");
  snprintf(out_info->description, sizeof(out_info->description),
           "2D Point typed-map type: point(x,y), -> x/y, -> distance(), -> translate(dx,dy), |> as_point");

  /* Describe the type to the host (T1) */
  if (host->register_type_descriptor(shell, POINT_TYPE_NAME,
        "2D point with x and y coordinates") != 0) {
    return 1;
  }

  /* Resolver: point(x, y) */
  if (host->register_value_resolver(shell, "point", "2D point value (x, y) with distance and translate", point_resolver) != 0) {
    return 1;
  }

  /* Properties targeting "Point" */
  if (host->register_property_extension(shell, POINT_TYPE_NAME, "x", point_prop_x) != 0 ||
      host->register_property_extension(shell, POINT_TYPE_NAME, "y", point_prop_y) != 0) {
    return 1;
  }

  /* Methods targeting "Point" */
  if (host->register_method_extension(shell, POINT_TYPE_NAME, "distance",  point_method_distance)  != 0 ||
      host->register_method_extension(shell, POINT_TYPE_NAME, "translate", point_method_translate) != 0) {
    return 1;
  }

  /* Pipeline stage */
  if (host->register_pipeline_stage(shell, "as_point", "convert a list(x, y) value to a Point", point_stage_as_point) != 0) {
    return 1;
  }

  return 0;
}
