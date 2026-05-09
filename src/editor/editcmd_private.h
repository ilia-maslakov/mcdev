/*
   Internal declarations for editcmd.c exposed only for unit tests.
   Do NOT include this header in production code.
*/

#ifndef MC__EDITCMD_PRIVATE_H
#define MC__EDITCMD_PRIVATE_H

#include "editwidget.h"

gboolean edit_undo_history_get_label (WEdit *edit, gboolean is_redo, guint n, char *buf,
                                      size_t buflen);

#endif
