/** \file src/runtime-host.h
 *  \brief Host services exposed to application-wide runtime extensions
 */

#ifndef MC__RUNTIME_HOST_H
#define MC__RUNTIME_HOST_H

/*** declarations of public functions ************************************************************/

struct WEdit;
struct WView;

void runtime_host_services_init (void);
void runtime_host_set_current_editor (struct WEdit *edit);
void runtime_host_clear_current_editor (struct WEdit *edit);
void runtime_host_set_current_viewer (struct WView *view);
void runtime_host_clear_current_viewer (struct WView *view);

#endif
