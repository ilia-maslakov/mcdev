/** \file mcviewer.h
 *  \brief Header: internal file viewer
 */

#ifndef MC__VIEWER_H
#define MC__VIEWER_H

#include "lib/global.h"
#include "lib/widget.h"  // WRect

/*** typedefs(not structures) and defined constants **********************************************/

/*** enums ***************************************************************************************/

/*** structures declarations (and typedefs of structures)*****************************************/

struct WView;
typedef struct WView WView;

typedef struct
{
    gboolean wrap;      // Wrap text lines to fit them on the screen
    gboolean hex;       // Plainview or hexview
    gboolean magic;     // Preprocess the file using external programs
    gboolean nroff;     // Nroff-style highlighting
    gboolean syntax;    // SGR background-color extension (ANSI text mode)
    gboolean terminal;  // ANSI terminal replay mode (virtual screen buffer)
} mcview_mode_flags_t;

/* Source-controller interface: lets a plugin re-drive what the viewer shows
 * (kubectl logs --since=5m -> --since=1h, re-fetch with a different
 * formatter, ...) without the plugin owning subprocess/temp-file lifecycle.
 * The plugin describes the source via a spec; mcview opens / streams it.
 *
 * Either command or file must be non-NULL after prepare(). */
typedef struct
{
    char *command;               /* shell pipeline -> mc_popen + stream */
    char *file;                  /* local path -> mc_open + file load */
    gboolean auto_scroll_bottom; /* after load, position at bottom */
    char *title;                 /* optional override for status title */
} mcview_source_spec_t;

/* Result of offering an unhandled viewer key to a source controller. */
typedef enum
{
    MCV_KEY_PASS = 0,    /* controller did not consume the key */
    MCV_KEY_HANDLED,     /* consumed; no viewer-side action needed */
    MCV_KEY_OPEN_OPTIONS /* viewer should run the generic options/swap flow */
} mcv_key_result_t;

typedef struct
{
    /* Open plugin options and update plugin-side pending state. */
    gboolean (*open_options) (void *ctx, mcview_source_spec_t *draft);

    /* Validate pending state and fill draft->command or draft->file. */
    gboolean (*prepare) (void *ctx, mcview_source_spec_t *draft, char **err_out);

    /* New source opened successfully; promote pending state. */
    void (*commit) (void *ctx);

    /* Draft abandoned; drop pending state. */
    void (*rollback) (void *ctx);

    /* Final teardown on viewer exit. */
    void (*free) (void *ctx);

    /* Optional controller-owned key handling. */
    mcv_key_result_t (*handle_key) (void *ctx, int key);
} mcview_source_controller_t;

/* Spec helpers. clone() deep-copies all string fields. */
extern mcview_source_spec_t *mcview_source_spec_clone (const mcview_source_spec_t *src);
extern void mcview_source_spec_free (mcview_source_spec_t *s);

/*** global variables defined in .c file *********************************************************/

extern mcview_mode_flags_t mcview_global_flags;
extern mcview_mode_flags_t mcview_altered_flags;

extern gboolean mcview_remember_file_position;
extern int mcview_max_dirt_limit;

extern gboolean mcview_mouse_move_pages;
extern char *mcview_show_eof;

/*** declarations of public functions ************************************************************/

/* Creates a new WView object with the given properties. */
extern WView *mcview_new (const WRect *r, gboolean is_panel);

/* Shows {file} or the output of {command} in the internal viewer,
 * starting in line {start_line}.
 */
extern gboolean mcview_viewer (const char *command, const vfs_path_t *file_vpath, int start_line,
                               off_t search_start, off_t search_end);

extern gboolean mcview_load (WView *view, const char *command, const char *file, int start_line,
                             off_t search_start, off_t search_end);

/* View data from a pipe fd. */
extern gboolean mcview_viewer_fd (int fd);

/* View streaming command output (non-blocking, select-driven). */
extern gboolean mcview_viewer_stream (const char *command);

/* View a source driven by a plugin controller. Takes ownership of
 * initial_spec and ctx. */
extern gboolean mcview_viewer_with_controller (mcview_source_spec_t *initial_spec,
                                               const mcview_source_controller_t *controller,
                                               void *ctx, int start_line);

extern void mcview_clear_mode_flags (mcview_mode_flags_t *flags);

/*** inline functions ****************************************************************************/
#endif
