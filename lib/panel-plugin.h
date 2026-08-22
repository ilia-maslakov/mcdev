/** \file panel-plugin.h
 *  \brief Header: panel plugin API for third-party panel content providers
 */

#ifndef MC__PANEL_PLUGIN_H
#define MC__PANEL_PLUGIN_H

#include "lib/global.h"
#include "lib/strutil.h"

/*
 * Note: get_items callback receives a dir_list* (from src/filemanager/dir.h)
 * cast to void*.  Plugin implementations should include dir.h and cast back.
 */

/*** typedefs(not structures) and defined constants **********************************************/

#define MC_PANEL_PLUGIN_API_VERSION 13
#define MC_PANEL_PLUGIN_ENTRY       "mc_panel_plugin_register"

/* Well-known target menu names for mc_pp_cmd_menu_entry_t.menu_name.
   Plugins set .menu_name = MC_PP_MENU_* to publish their entries into
   the corresponding menu. NULL menu_name defaults to "Command" for
   backward compatibility with plugins built before this field existed. */
#define MC_PP_MENU_COMMAND "Command"
#define MC_PP_MENU_PANEL   "Panel"

/*** enums ***************************************************************************************/

typedef enum
{
    MC_PPR_OK = 0,
    MC_PPR_FAILED = -1,
    MC_PPR_NOT_SUPPORTED = -2,
    MC_PPR_CLOSE = -3,
    MC_PPR_SKIPPED = -4 /* the user turned it down */
} mc_pp_result_t;

typedef enum
{
    MC_PPF_NONE = 0,
    MC_PPF_NAVIGATE = 1 << 0,  /* handles chdir/".." */
    MC_PPF_GET_FILES = 1 << 1, /* can extract files */
    MC_PPF_DELETE = 1 << 2,    /* can delete items */
    MC_PPF_CUSTOM_TITLE = 1 << 3,
    MC_PPF_CREATE = 1 << 4,             /* supports F7 (create item) */
    MC_PPF_PUT_FILES = 1 << 5,          /* can accept files (put_file/save_file) */
    MC_PPF_SHOW_IN_MENU = 1 << 6,       /* add entry to left/right panel menu */
    MC_PPF_SHOW_IN_DRIVE_MENU = 1 << 7, /* show in Alt-F1/Alt-F2 drive menu */
    MC_PPF_LOCAL_FILES = 1 << 8,        /* entries are real local paths;
                                           core uses them directly for view/edit/
                                           copy/move, bypassing get_local_copy */
    MC_PPF_NO_MOVE = 1 << 10,           /* plugin refuses F6: move deletes the source,
                                           set this when the copy cannot be confirmed */
    MC_PPF_ACCEPTS_FILE_LIST = 1 << 9,  /* plugin implements open_file_list(),
                                           i.e. can be a destination for Find
                                           results and similar list producers */
    MC_PPF_COPY_TREE = 1 << 11          /* copy_to_local() takes a directory and
                                           writes everything below it; without
                                           this the core walks the tree itself */
} mc_pp_flags_t;

/*** structures declarations (and typedefs of structures)*****************************************/

/* Forward declaration */
struct mc_panel_host_t;

/* An independently owned, re-openable byte source supplied by a panel plugin.
   It deliberately has no VFS, protocol or libarchive knowledge. A source must
   remain valid after the panel that created it has been closed. */
typedef struct mc_pp_input_stream mc_pp_input_stream_t;

typedef struct
{
    mc_pp_result_t (*open) (mc_pp_input_stream_t *stream, void **handle, GError **error);
    gssize (*read) (mc_pp_input_stream_t *stream, void *handle, void *buf, gsize size,
                    GError **error);
    /* New position, or -1 on error. NULL when the source only moves forward. */
    gint64 (*seek) (mc_pp_input_stream_t *stream, void *handle, gint64 offset, int whence,
                    GError **error);
    void (*close) (mc_pp_input_stream_t *stream, void *handle);
    void (*free) (mc_pp_input_stream_t *stream);
} mc_pp_input_stream_ops_t;

struct mc_pp_input_stream
{
    const mc_pp_input_stream_ops_t *ops;
    void *data;
};

/* Plugin action descriptor - one entry per action the plugin exposes. */
typedef struct mc_pp_action_t
{
    const char *label; /* translatable action name shown in listbox/menu */
    void *(*callback) (struct mc_panel_host_t *host, const char *open_path);
} mc_pp_action_t;

typedef enum
{
    MC_PP_FILE_OPERATION_OPEN,
    MC_PP_FILE_OPERATION_VIEW
} mc_pp_file_operation_kind_t;

/* A named operation that can be selected by magic.ini for one file.  Separate
   from actions[], whose callbacks receive a path from the plugin menu rather
   than file contents from a source panel.  An operation takes @stream only
   when it succeeds; @local_path is then the core's to view and remove. */
typedef struct mc_pp_file_operation_t
{
    const char *name;
    mc_pp_file_operation_kind_t kind;
    gboolean (*may_open_name) (const char *display_name);
    void *(*open_input_stream) (struct mc_panel_host_t *host, const char *display_name,
                                mc_pp_input_stream_t *stream);
    mc_pp_result_t (*view_input_stream) (struct mc_panel_host_t *host, const char *display_name,
                                         mc_pp_input_stream_t *stream, char **local_path);
} mc_pp_file_operation_t;

/* Entry added to the Command menu by a plugin. */
typedef struct mc_pp_cmd_menu_entry_t
{
    const char *label;     /* menu item text (with & accelerator). NULL = separator */
    int action_index;      /* index into mc_panel_plugin_t.actions[] */
    const char *shortcut;  /* shortcut text shown in menu (e.g. "S-F1"), or NULL */
    int key;               /* key code (e.g. KEY_F(11) for S-F1), or 0 for none */
    const char *menu_name; /* MC_PP_MENU_COMMAND, MC_PP_MENU_PANEL, ... NULL = "Command" */
} mc_pp_cmd_menu_entry_t;

typedef struct mc_panel_column_t
{
    const char *id;
    const char *title;
    int min_size;
    gboolean expands;
    align_crt_t default_just;
    gboolean use_in_user_format;
} mc_panel_column_t;

/* What mc provides to the plugin */
typedef struct mc_panel_host_t
{
    void (*refresh) (struct mc_panel_host_t *host);
    void (*set_hint) (struct mc_panel_host_t *host, const char *text);
    void (*message) (struct mc_panel_host_t *host, int flags, const char *title, const char *text);
    void (*run_command) (struct mc_panel_host_t *host, const char *command, int flags);
    gboolean (*open_diff) (struct mc_panel_host_t *host, const char *left_path,
                           const char *right_path);
    void (*close_plugin) (struct mc_panel_host_t *host, const char *dir_path);
    void (*add_history) (struct mc_panel_host_t *host, const char *path);
    int (*get_marked_count) (struct mc_panel_host_t *host);
    const GString *(*get_next_marked) (struct mc_panel_host_t *host, int *current);
    const GString *(*get_current) (struct mc_panel_host_t *host);
    void (*navigate_other_panel) (struct mc_panel_host_t *host, const char *dir_path,
                                  const char *focus_file);
    void *host_data; /* opaque, points to WPanel internally */

    /* Set by plugin to request cursor positioning after standalone action completes.
       The host frees this string after use. */
    char *focus_after;

    /* Set the host panel's cwd to the given path. Used by plugins (e.g. panelize)
       whose listing carries absolute paths and which need %d/%p tokens and copy
       targets to resolve against "/" or another base. */
    void (*set_cwd) (struct mc_panel_host_t *host, const char *path);
} mc_panel_host_t;

/* What the plugin provides (callback table) */
typedef struct mc_panel_plugin_t
{
    int api_version;          /* MC_PANEL_PLUGIN_API_VERSION */
    const char *name;         /* "docker", "git-log" */
    const char *display_name; /* "Docker containers" */
    const char *proto;        /* protocol prefix for panel. for example git:/  */
    const char *prefix;       /* "docker:" or NULL */
    mc_pp_flags_t flags;

    /* Required */
    void *(*open) (mc_panel_host_t *host, const char *open_path);
    void (*close) (void *plugin_data);
    /* Populate the panel.  @list is a dir_list* (from dir.h).
       The ".." entry at index 0 is already created by the host;
       the plugin must NOT add ".." itself - only real items. */
    mc_pp_result_t (*get_items) (void *plugin_data, void *list /* dir_list* */);

    /* Optional target for an independently owned input stream. On success the
       callback takes ownership of @stream and returns its plugin data. On
       failure it returns NULL and leaves @stream with the caller. */
    void *(*open_input_stream) (mc_panel_host_t *host, const char *display_name,
                                mc_pp_input_stream_t *stream);
    /* Named operations selectable from magic.ini. */
    const mc_pp_file_operation_t *file_operations;
    int file_operation_count;

    /* Optional (NULL = not supported) */
    mc_pp_result_t (*chdir) (void *plugin_data, const char *path);
    mc_pp_result_t (*enter) (void *plugin_data, const char *fname, const struct stat *st);
    /* Optional view hook for F3/Shift-F3 flow.
       If returns MC_PPR_OK, core treats view command as handled.
       If returns MC_PPR_NOT_SUPPORTED, core uses default view behavior. */
    mc_pp_result_t (*view) (void *plugin_data, const char *fname, const struct stat *st,
                            gboolean plain_view);
    /* Optional help hook for plugin-specific help nodes/files.
       If returns MC_PPR_OK, core opens help using returned filename/node.
       filename may be NULL to use default help file; node may be NULL for default node. */
    mc_pp_result_t (*get_help_info) (void *plugin_data, const char **filename, const char **node);
    mc_pp_result_t (*get_local_copy) (void *plugin_data, const char *fname, char **local_path);
    /* Download an item directly to the given local path. The core asks about
       overwriting before calling this.
       @fname names a directory only when the plugin sets MC_PPF_COPY_TREE, and
       then everything below it is expected at @local_path; MC_PPR_NOT_SUPPORTED
       sends the core to walk it item by item.
       If NULL, core falls back to get_local_copy + copy. */
    mc_pp_result_t (*copy_to_local) (void *plugin_data, const char *fname, const char *local_path);
    mc_pp_result_t (*put_file) (void *plugin_data, const char *local_path, const char *dest_name);
    mc_pp_result_t (*save_file) (void *plugin_data, const char *local_path,
                                 const char *remote_name);
    /* Remove the named items. A name may be a directory, which goes with
       everything below it; a plugin that cannot do that says so, and the core
       leaves the source where it is after a move. */
    mc_pp_result_t (*delete_items) (void *plugin_data, const char **names, int count);
    const char *(*get_title) (void *plugin_data);
    mc_pp_result_t (*handle_key) (void *plugin_data, int key);
    mc_pp_result_t (*create_item) (void *plugin_data);
    const mc_panel_column_t *(*get_columns) (void *plugin_data, size_t *count);
    const char *(*get_column_value) (void *plugin_data, const char *fname, const char *column_id);
    /* Optional multi-action support.
       If actions != NULL, plugin selection shows a second listbox with these actions.
       Each action callback returns non-NULL to activate the plugin panel, or NULL to
       just perform a standalone operation (e.g. show a dialog). */
    const mc_pp_action_t *actions; /* NULL = legacy (use open) */
    int action_count;

    /* Optional entries injected into the Command menu.
       Each entry references an action by index into actions[]. */
    const mc_pp_cmd_menu_entry_t *cmd_menu_entries; /* NULL = none */
    int cmd_menu_entry_count;

    /* Optional footer text shown on panel bottom line near free space indicator. */
    const char *(*get_footer) (void *plugin_data);
    /* Optional preferred focus item name after plugin navigation/reload. */
    const char *(*get_focus_name) (void *plugin_data);
    /* Optional plugin-provided default panel format (e.g. "type name | status | size").
       Return NULL or empty string to use generic core fallback from get_columns(). */
    const char *(*get_default_format) (void *plugin_data);
    /* Optional default sort column ID applied once when the plugin panel is first opened.
       Use a standard mc sort ID: "name", "mtime", "size", "extension", etc.
       NULL means no preference (mc uses its own default). */
    const char *default_sort_id;
    /* TRUE = sort descending (newest-first for mtime). Ignored when default_sort_id is NULL. */
    gboolean default_sort_reverse;

    /* Optional reload hook called before get_items() on Ctrl-R. */
    mc_pp_result_t (*reload) (void *plugin_data);

    /* Open a panel from caller-owned absolute paths. The plugin must copy
       paths it keeps. Return NULL to leave the current panel unchanged. */
    void *(*open_file_list) (struct mc_panel_host_t *host, const char *const *paths, size_t count,
                             const char *label);

    /* Optional: stat an entry, and digest a byte range of it without fetching
       it. @algo is one of "sha256", "md5" or "cksum"; the digest is the text
       that algorithm normally prints, so that digests from different plugins
       are comparable. NULL for an algo the plugin cannot do, which is the
       caller's cue to try another. Caller frees the digest. */
    gboolean (*stat_entry) (void *plugin_data, const char *name, struct stat *st);
    char *(*digest_range) (void *plugin_data, const char *name, gint64 offset, gint64 length,
                           const char *algo);

    /* Optional: where this panel is, in the form open() accepts back: the same
       string the plugin puts in the directory history. Caller frees.
       NULL when the plugin has no such notion. */
    char *(*get_location) (void *plugin_data);

    /* Optional: does an entry of this name already exist here?
       NULL means "cannot say"; the core then writes without asking. */
    gboolean (*exists) (void *plugin_data, const char *name);

    /* Optional: continue a transfer that stopped part way.
       resume_offset returns how many bytes of the existing destination are a
       verified prefix of the source (possibly 0), or -1 when the transfer
       cannot be continued. @dest_local: TRUE when @dest is a local path, FALSE
       when @dest is inside the plugin.
       A plugin that cannot verify the prefix must leave these NULL.
       Neither call may truncate or delete anything. */
    gint64 (*resume_offset) (void *plugin_data, const char *src, const char *dest,
                             gboolean dest_local);
    mc_pp_result_t (*resume_copy) (void *plugin_data, const char *src, const char *dest,
                                   gboolean dest_local, gint64 offset);

    /* Optional: copy a file to another name inside the plugin's own space,
       for a relative destination, which means "here" and not the other panel.
       NULL: the core falls back to fetching the file out. */
    mc_pp_result_t (*copy_within) (void *plugin_data, const char *fname, const char *dest_name);

    /* Optional streaming transfer: moves a file from one plugin panel to
       another without a local temporary, which get_local_copy()/put_file()
       cannot do. Leaving these NULL keeps the get_local_copy()/put_file() path.
       read_open() returns a handle and reports the size through @size, or NULL
       on failure. read_chunk() returns the byte count, 0 at end of stream and
       -1 on error. read_close() releases the handle and, when @digest is not
       NULL, may store there a checksum of what was read; NULL digest means the
       plugin cannot produce one. The caller frees it. */
    void *(*read_open) (void *plugin_data, const char *fname, gint64 offset, gint64 *size);
    gssize (*read_chunk) (void *plugin_data, void *handle, void *buf, gsize size);
    gboolean (*read_close) (void *plugin_data, void *handle, char **digest);

    /* Unlike read_* above, the stream stays valid after this panel is gone. */
    mc_pp_result_t (*get_input_stream) (void *plugin_data, const char *fname,
                                        mc_pp_input_stream_t **stream);

    /* write_open() is told the final @size for protocols that must announce it
       up front. @offset is where writing starts; 0 means the beginning. */
    void *(*write_open) (void *plugin_data, const char *fname, gint64 size, gint64 offset);
    gboolean (*write_chunk) (void *plugin_data, void *handle, const void *buf, gsize size);
    gboolean (*write_close) (void *plugin_data, void *handle, char **digest);

    /* Optional: open the plugin's standalone settings/preferences dialog.
       Invoked from the Manage Plugins dialog (Enter or F4 on the plugin row).
       Operates without an open panel instance; the plugin loads/saves its own
       config. NULL = the plugin has no settings. */
    void (*configure) (void);

    /* Read-only Quick View source for entries that are not files; the core
       unlinks @local_path.  NOT_SUPPORTED = try get_local_copy(), FAILED = show
       nothing. */
    mc_pp_result_t (*get_quick_view) (void *plugin_data, const char *fname, const struct stat *st,
                                      char **local_path);

    /* API 13: host-owned proxy plugins need descriptor identity during open.
       Native plugins normally keep using open(); a proxy sets open_with_plugin
       and may leave open NULL.  @plugin_context is descriptor-owned. */
    void *plugin_context;
    void *(*open_with_plugin) (const struct mc_panel_plugin_t *plugin, mc_panel_host_t *host,
                               const char *open_path);
    void *(*run_action_with_plugin) (const struct mc_panel_plugin_t *plugin, void *plugin_data,
                                     mc_panel_host_t *host, const char *open_path,
                                     int action_index);
} mc_panel_plugin_t;

typedef const mc_panel_plugin_t *(*mc_panel_plugin_register_fn) (void);

/*** global variables defined in .c file *********************************************************/

/*** declarations of public functions ************************************************************/

/* Shared helper: build a struct stat and append one entry to the dir_list.
   Plugins that used a local add_entry()/add_fake_entry() can call this instead. */
void mc_pp_add_entry (void *list, const char *name, mode_t mode, off_t size, time_t mtime);

/* Rename temp file to preserve the original extension from fname.
   Uses the basename of fname to avoid treating directory components
   with dots (e.g. "dir.with.dot/Makefile") as an extension.
   On success, *local_path is updated to the new path and the old one
   is freed.  On rename(2) failure, *local_path is left unchanged.
   Does nothing if fname has no extension or if local_path or *local_path is NULL. */
void mc_pp_rename_with_ext (char **local_path, const char *fname);

/* Write @len bytes (@len < 0: @data is a C string) into a fresh 0600 temp file
   from template @tmpl; @local_path is the caller's to free, NULL on failure. */
gboolean mc_pp_write_temp_file (const char *tmpl, const void *data, gssize len, char **local_path);

/* TRUE while the core calls the plugin for a passive preview (Quick View):
   report the failure through the return value, do not open a dialog. */
gboolean mc_pp_quiet_messages (void);
gboolean mc_pp_set_quiet_messages (gboolean quiet);

void mc_pp_input_stream_free (mc_pp_input_stream_t *stream);
/* @own_file unlinks @path when the stream is freed. */
mc_pp_input_stream_t *mc_pp_input_stream_new_for_file (const char *path, gboolean own_file);
/* The file behind @stream, NULL when it has none; @is_temporary: it dies with the stream. */
const char *mc_pp_input_stream_local_path (const mc_pp_input_stream_t *stream,
                                           gboolean *is_temporary);

/* Registry */
gboolean mc_panel_plugin_add (const mc_panel_plugin_t *plugin);
gboolean mc_panel_plugin_remove (const mc_panel_plugin_t *plugin);
const GSList *mc_panel_plugin_list (void);
const mc_panel_plugin_t *mc_panel_plugin_find_by_name (const char *name);
const mc_panel_plugin_t *mc_panel_plugin_find_by_prefix (const char *prefix);

/* Loader */
void mc_panel_plugins_load (void);
void mc_panel_plugins_shutdown (void);

/*** inline functions ****************************************************************************/

/**
 * Join two UNIX path components: base + "/" + name.
 * Avoids double slash when base is "/".
 * Returns newly allocated string.
 */
static inline char *
mc_pp_join_path (const char *base, const char *name)
{
    if (strcmp (base, "/") == 0)
        return g_strdup_printf ("/%s", name);

    return g_strdup_printf ("%s/%s", base, name);
}

/* --------------------------------------------------------------------------------------------- */

/**
 * Return parent directory of a UNIX path.
 * Returns NULL if path is "/" or NULL (already at root).
 * Returns newly allocated string.
 */
static inline char *
mc_pp_path_up (const char *path)
{
    const char *last;

    if (path == NULL || strcmp (path, "/") == 0)
        return NULL;

    last = strrchr (path, '/');
    if (last == NULL || last == path)
        return g_strdup ("/");

    return g_strndup (path, (gsize) (last - path));
}

/* --------------------------------------------------------------------------------------------- */

#endif /* MC__PANEL_PLUGIN_H */
