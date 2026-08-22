/** \file runtime-panel-provider.h
 *  \brief Host bridge from runtime panel providers to native panel plugins
 */

#ifndef MC__RUNTIME_PANEL_PROVIDER_H
#define MC__RUNTIME_PANEL_PROVIDER_H

#include "lib/extension-runtime.h"

gboolean runtime_panel_provider_register (mc_runtime_plugin_context_t *context,
                                          const mc_runtime_panel_provider_t *provider,
                                          mc_runtime_handle_t *registration, const char **error);
gboolean runtime_panel_provider_unregister (const mc_runtime_handle_t *registration,
                                            const char **error);

#endif
