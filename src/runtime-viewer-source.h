#ifndef MC_RUNTIME_VIEWER_SOURCE_H
#define MC_RUNTIME_VIEWER_SOURCE_H

#include "lib/extension-runtime.h"

gboolean runtime_viewer_controller_open (mc_runtime_plugin_context_t *context,
                                         const mc_runtime_viewer_controller_t *controller,
                                         const char **error);

#endif
