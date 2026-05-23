/** \file mongo_ui.h
 *  \brief MongoDB plugin: message dialog wrapping and modeless status dialog.
 */

#ifndef MC_PANEL_MONGO_UI_H
#define MC_PANEL_MONGO_UI_H

#include "lib/global.h"
#include "lib/widget.h"        // simple_status_msg_t
#include "lib/panel-plugin.h"  // mc_panel_host_t

/* Modeless status dialog shown around blocking libmongoc calls. */
typedef struct
{
    simple_status_msg_t base;
    gboolean active;
} mongo_status_t;

/* Show a host message dialog, word-wrapped so a long one-line text does not
   stretch the dialog across the whole screen. */
void mongo_show_message (mc_panel_host_t *host, gboolean error, const char *text);

void mongo_status_open (mongo_status_t *st, const char *title);
void mongo_status_set (mongo_status_t *st, const char *text);
void mongo_status_close (mongo_status_t *st);

#endif
