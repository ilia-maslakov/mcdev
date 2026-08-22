#ifndef MC__EVENTS_INIT_H
#define MC__EVENTS_INIT_H

/*** typedefs(not structures) and defined constants **********************************************/

/*** enums ***************************************************************************************/

/*** structures declarations (and typedefs of structures)*****************************************/

/*** global variables defined in .c file *********************************************************/

/*** declarations of public functions ************************************************************/

gboolean events_init (GError **mcerror);
gboolean events_deinit (GError **mcerror);
void events_publish_runtime_startup (void);
void events_publish_runtime_shutdown (const char *reason);
gboolean events_runtime_is_started (void);

/*** inline functions ****************************************************************************/

#endif
