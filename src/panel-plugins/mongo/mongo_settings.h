/** \file mongo_settings.h
 *  \brief MongoDB plugin: general-options settings dialog.
 */

#ifndef MC_PANEL_MONGO_SETTINGS_H
#define MC_PANEL_MONGO_SETTINGS_H

#include "lib/global.h"

#include "mongo_config.h"

/*** API ***************************************************************************************/

/* Open the modal settings dialog for the general options (bucketing knobs +
   timeouts + default cluster). On OK, updates @cfg in place and writes the
   [General] section back to mongo.ini. Connection/cluster definitions are
   out of scope (managed separately). Returns TRUE if settings were saved. */
gboolean mongo_settings_dialog_run (mongo_config_t *cfg);

/* Modal editor for a single connection. Shows name / URI / description /
   read-only and, on OK with a non-empty name and URI, updates @c in place and
   returns TRUE. @is_new only affects the dialog title. The caller persists the
   result (mongo_config_save_cluster) and owns @c. */
gboolean mongo_connection_dialog_run (mongo_cluster_t *c, gboolean is_new);

#endif
