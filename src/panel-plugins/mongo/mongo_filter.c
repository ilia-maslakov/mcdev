/*
   MongoDB panel plugin -- find-style filter dialog.

   Copyright (C) 2026
   Free Software Foundation, Inc.

   Written by:
   Ilia Maslakov <il.smind@gmail.com>, 2026

   This file is part of the Midnight Commander.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <config.h>

#include <inttypes.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <bson/bson.h>

#include "lib/global.h"
#include "lib/widget.h"
#include "lib/vfs/vfs.h"

#include "src/editor/edit.h"

#include "mongo_internal.h"
#include "mongo_conn.h"
#include "mongo_filter.h"
#include "mongo_render.h"
#include "mongo_ui.h"
#include "mongo_wizard.h"

/*** forward declarations ************************************************************************/

static bson_t *mongo_sample_doc (char **err_out);

/*** file scope macros ***************************************************************************/

#define B_MONGO_FILTER_EDIT   (B_USER + 3)
#define B_MONGO_FILTER_WIZARD (B_USER + 4)

#define HIST_FILTER           "mongo-filter"
#define HIST_PROJECTION       "mongo-filter-projection"
#define HIST_SORT             "mongo-filter-sort"
#define HIST_LIMIT            "mongo-filter-limit"

/*** file scope variables ************************************************************************/

/* Active sample callback + the Filter input's widget id for the open dialog.
   quick_dialog buttons take a bcback_fn (no user data), so we stash here
   for the duration of one dialog. */
static mongo_filter_sample_fn s_sample_fn = NULL;
static gpointer s_sample_ctx = NULL;
static unsigned long s_filter_input_id = 0;

/*** file scope functions ************************************************************************/

static char *
bson_to_relaxed_json (const bson_t *b)
{
    char *raw;
    char *out;

    if (b == NULL || bson_count_keys (b) == 0)
        return g_strdup ("");
    raw = bson_as_relaxed_extended_json (b, NULL);
    if (raw == NULL)
        return g_strdup ("");
    out = g_strdup (raw);
    bson_free (raw);
    return out;
}

/* Parse @text as a BSON document. NULL/empty -> *out = NULL, returns TRUE.
   On parse failure, fills @err_label-prefixed message into @err_out (caller
   g_free) and returns FALSE. */
static gboolean
parse_bson_field (const char *text, const char *err_label, bson_t **out, char **err_out)
{
    bson_error_t berr;
    bson_t *parsed;
    const char *p = text;

    *out = NULL;
    if (p == NULL)
        return TRUE;
    while (*p == ' ' || *p == '\t')
        p++;
    if (*p == '\0')
        return TRUE;

    parsed = bson_new_from_json ((const uint8_t *) p, -1, &berr);
    if (parsed == NULL)
    {
        if (err_out != NULL)
            *err_out = g_strdup_printf ("%s: %s", err_label, berr.message);
        return FALSE;
    }
    *out = parsed;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

/* The Filter input of the open find dialog, located via its stashed widget id. */
static WInput *
mongo_filter_input_of (const WButton *button)
{
    Widget *w;

    if (button == NULL || WIDGET (button)->owner == NULL)
        return NULL;
    w = widget_find_by_id ((Widget *) WIDGET (button)->owner, s_filter_input_id);
    return w != NULL ? INPUT (w) : NULL;
}

/* Open @seed in the internal editor; return the edited text (caller g_free)
   if it changed, else NULL. */
static char *
mongo_filter_run_mcedit (const char *seed)
{
    char *tmp_path = NULL;
    int tmp_fd;
    GError *gerr = NULL;
    edit_arg_t *arg;
    struct stat sb, sa;
    gboolean had;
    gchar *edited = NULL;
    gsize elen = 0;
    gsize slen;

    tmp_fd = g_file_open_tmp ("mc-mongo-filterXXXXXX.json", &tmp_path, &gerr);
    if (tmp_fd < 0)
    {
        message (D_ERROR, _ ("MongoDB filter"), "%s",
                 gerr != NULL ? gerr->message : "Cannot open temp file.");
        if (gerr != NULL)
            g_error_free (gerr);
        return NULL;
    }
    slen = seed != NULL ? strlen (seed) : 0;
    if (slen > 0 && write (tmp_fd, seed, slen) != (ssize_t) slen)
    {
        close (tmp_fd);
        unlink (tmp_path);
        g_free (tmp_path);
        message (D_ERROR, _ ("MongoDB filter"), "%s", "Failed to write temp file.");
        return NULL;
    }
    close (tmp_fd);

    had = stat (tmp_path, &sb) == 0;
    arg = edit_arg_new (tmp_path, 0);
    edit_file (arg);
    edit_arg_free (arg);

    if (had && stat (tmp_path, &sa) == 0 && sb.st_mtime == sa.st_mtime && sb.st_size == sa.st_size)
    {
        unlink (tmp_path);
        g_free (tmp_path);
        return NULL;
    }

    if (!g_file_get_contents (tmp_path, &edited, &elen, NULL))
        edited = NULL;
    unlink (tmp_path);
    g_free (tmp_path);

    while (edited != NULL && elen > 0
           && (edited[elen - 1] == '\n' || edited[elen - 1] == '\r' || edited[elen - 1] == ' '
               || edited[elen - 1] == '\t'))
        edited[--elen] = '\0';
    return edited;
}

/* Sample one real document and open it in the editor, landing the result in the
   Filter input. */
static void
mongo_filter_sample_into (WInput *in)
{
    char *err = NULL;
    char *json;
    char *edited;

    if (in == NULL || s_sample_fn == NULL)
        return;
    json = s_sample_fn (s_sample_ctx, &err);
    if (json == NULL)
    {
        message (D_ERROR, _ ("MongoDB sample"), "%s",
                 err != NULL ? err : _ ("No documents in current scope."));
        g_free (err);
        return;
    }
    edited = mongo_filter_run_mcedit (json);
    g_free (json);
    if (edited != NULL)
    {
        input_assign_text (in, edited);
        g_free (edited);
    }
}

/* Edit as File: edit the current Filter JSON in the internal editor; when the
   field is empty, start from a real sampled document. */
static int
mongo_filter_edit_cb (WButton *button, int action)
{
    WInput *in;
    const char *cur;

    (void) action;
    in = mongo_filter_input_of (button);
    if (in == NULL)
        return 0;

    cur = input_get_ctext (in);
    if (cur != NULL && cur[0] != '\0')
    {
        /* Pretty-print the input before opening it in the editor. */
        char *pretty = mongo_render_pretty_json (cur, 2);
        char *edited = mongo_filter_run_mcedit (pretty != NULL ? pretty : cur);
        g_free (pretty);
        if (edited != NULL)
        {
            input_assign_text (in, edited);
            g_free (edited);
        }
        return 0;
    }

    /* Empty filter: seed the editor with a scaffold built from one sampled
       document (== on each top-level scalar field), so the user starts from
       the collection's shape rather than a blank field. */
    if (s_sample_fn != NULL)
    {
        char *err = NULL;
        bson_t *doc = mongo_sample_doc (&err);
        char *structure = NULL;

        g_free (err);
        if (doc != NULL)
        {
            char *raw = bson_as_relaxed_extended_json (doc, NULL);

            if (raw != NULL)
            {
                structure = mongo_wizard_doc_to_filter (raw);
                bson_free (raw);
            }
            bson_destroy (doc);
        }

        if (structure != NULL)
        {
            char *pretty = mongo_render_pretty_json (structure, 2);
            char *edited = mongo_filter_run_mcedit (pretty != NULL ? pretty : structure);

            g_free (pretty);
            g_free (structure);
            if (edited != NULL)
            {
                input_assign_text (in, edited);
                g_free (edited);
            }
        }
        else
            mongo_filter_sample_into (in); /* fall back to the raw document */
    }
    return 0;
}

/* Fetch one document from the current scope (bucket range + active filter).
   Returns a bson_t (caller bson_destroy) or NULL with @err_out set. */
static bson_t *
mongo_sample_doc (char **err_out)
{
    mongo_data_t *data = (mongo_data_t *) s_sample_ctx;
    const bson_t *fextra;
    const char *coll;

    if (err_out != NULL)
        *err_out = NULL;
    /* At COLLS level coll_name is not set yet; fall back to the F6 target. */
    coll = data != NULL ? (data->coll_name != NULL ? data->coll_name : data->pending_coll) : NULL;
    if (data == NULL || data->client == NULL || coll == NULL)
    {
        if (err_out != NULL)
            *err_out = g_strdup ("Not inside a collection.");
        return NULL;
    }

    /* When sampling the not-yet-entered collection, there is no bucket scope
       or applied filter, so query the whole collection. */
    fextra = (data->coll_name != NULL && data->filter != NULL) ? data->filter->filter : NULL;
    return mongo_conn_sample_one_doc (
        data->client, data->db_name, coll, data->coll_name != NULL ? mongo_current_lo (data) : NULL,
        data->coll_name != NULL ? mongo_current_hi (data) : NULL, fextra, err_out);
}

#define MONGO_FIELDS_MAX_DEPTH 6
#define MONGO_FIELDS_MAX_COUNT 256

/* Recursively collect dot-paths of every key reachable from @it. Nested
   sub-documents are expanded (awards -> awards.wins); arrays are listed but
   not descended into, since their element schema is not stable. */
static void
mongo_collect_fields (bson_iter_t *it, const char *prefix, GPtrArray *keys, int depth)
{
    while (keys->len < MONGO_FIELDS_MAX_COUNT && bson_iter_next (it))
    {
        const char *k = bson_iter_key (it);
        char *path = prefix != NULL ? g_strdup_printf ("%s.%s", prefix, k) : g_strdup (k);

        g_ptr_array_add (keys, path);
        if (depth + 1 < MONGO_FIELDS_MAX_DEPTH && bson_iter_type (it) == BSON_TYPE_DOCUMENT)
        {
            bson_iter_t child;
            if (bson_iter_recurse (it, &child))
                mongo_collect_fields (&child, path, keys, depth + 1);
        }
    }
}

/* Wizard value picker: distinct values of @field in the current scope. */
static char **
mongo_filter_values_cb (gpointer ctx, const char *field, gboolean *capped_out, char **err_out)
{
    mongo_data_t *data = (mongo_data_t *) ctx;
    const bson_t *fextra;
    const char *coll;

    coll = data != NULL ? (data->coll_name != NULL ? data->coll_name : data->pending_coll) : NULL;
    if (data == NULL || data->client == NULL || coll == NULL)
    {
        if (err_out != NULL)
            *err_out = g_strdup ("Not inside a collection.");
        return NULL;
    }
    fextra = (data->coll_name != NULL && data->filter != NULL) ? data->filter->filter : NULL;
    return mongo_conn_distinct_values (data->client, data->db_name, coll, field, fextra, 200,
                                       capped_out, err_out);
}

/* Wizard button: build a filter from a rule list and replace the Filter field. */
static int
mongo_filter_wizard_cb (WButton *button, int action)
{
    WInput *in;
    bson_t *doc;
    char *err = NULL;
    char **fields = NULL;
    char *current;
    char *json;

    (void) action;
    in = mongo_filter_input_of (button);
    if (in == NULL)
        return 0;

    doc = mongo_sample_doc (&err);
    g_free (err);
    if (doc != NULL)
    {
        GPtrArray *keys = g_ptr_array_new ();
        bson_iter_t it;

        if (bson_iter_init (&it, doc))
            mongo_collect_fields (&it, NULL, keys, 0);
        if (keys->len > 0)
        {
            g_ptr_array_add (keys, NULL);
            fields = (char **) g_ptr_array_free (keys, FALSE);
        }
        else
            g_ptr_array_free (keys, TRUE);

        bson_destroy (doc);
    }

    current = input_get_text (in);
    json = mongo_wizard_run ((const char *const *) fields, mongo_filter_values_cb, s_sample_ctx,
                             current);
    g_free (current);
    g_strfreev (fields);
    if (json != NULL)
    {
        input_assign_text (in, json);
        g_free (json);
    }
    return 0;
}

/* --------------------------------------------------------------------------------------------- */

/*** public functions ****************************************************************************/

mongo_filter_t *
mongo_filter_new (void)
{
    return g_new0 (mongo_filter_t, 1);
}

/* --------------------------------------------------------------------------------------------- */

void
mongo_filter_free (mongo_filter_t *f)
{
    if (f == NULL)
        return;
    if (f->filter != NULL)
        bson_destroy (f->filter);
    if (f->projection != NULL)
        bson_destroy (f->projection);
    if (f->sort != NULL)
        bson_destroy (f->sort);
    g_free (f);
}

/* --------------------------------------------------------------------------------------------- */

gboolean
mongo_filter_is_empty (const mongo_filter_t *f)
{
    if (f == NULL)
        return TRUE;
    return f->filter == NULL && f->projection == NULL && f->sort == NULL && f->limit == 0;
}

/* --------------------------------------------------------------------------------------------- */

mongo_filter_dialog_result_t
mongo_filter_dialog_run (const mongo_filter_t *initial, mongo_filter_sample_fn sample_fn,
                         gpointer sample_ctx, gint64 result_cap, mongo_filter_t **out,
                         char **err_out)
{
    char *initial_filter, *initial_proj, *initial_sort;
    char initial_limit[32] = "";
    char *r_filter = NULL, *r_proj = NULL, *r_sort = NULL, *r_limit = NULL;
    char notice[96];
    int rc;

    if (err_out != NULL)
        *err_out = NULL;
    *out = NULL;

    initial_filter = initial != NULL ? bson_to_relaxed_json (initial->filter) : g_strdup ("");
    initial_proj = initial != NULL ? bson_to_relaxed_json (initial->projection) : g_strdup ("");
    initial_sort = initial != NULL ? bson_to_relaxed_json (initial->sort) : g_strdup ("");
    if (initial != NULL && initial->limit > 0)
        g_snprintf (initial_limit, sizeof (initial_limit), "%" G_GINT64_FORMAT, initial->limit);

    g_snprintf (
        notice, sizeof (notice),
        _ ("Note: a filter shows a flat list (no folders), up to %" G_GINT64_FORMAT " documents."),
        result_cap);

    s_sample_fn = sample_fn;
    s_sample_ctx = sample_ctx;
    s_filter_input_id = 0;

    {
        quick_widget_t quick_widgets[] = {
            // clang-format off
            QUICK_LABELED_INPUT (N_ ("Filter (JSON):"), input_label_above, initial_filter,
                                 HIST_FILTER, &r_filter, &s_filter_input_id, FALSE, FALSE,
                                 INPUT_COMPLETE_NONE),
            QUICK_LABELED_INPUT (N_ ("Projection:"), input_label_above, initial_proj,
                                 HIST_PROJECTION, &r_proj, NULL, FALSE, FALSE, INPUT_COMPLETE_NONE),
            QUICK_LABELED_INPUT (N_ ("Sort:"), input_label_above, initial_sort,
                                 HIST_SORT, &r_sort, NULL, FALSE, FALSE, INPUT_COMPLETE_NONE),
            QUICK_LABELED_INPUT (N_ ("Limit (0 = no override):"), input_label_above, initial_limit,
                                 HIST_LIMIT, &r_limit, NULL, FALSE, FALSE, INPUT_COMPLETE_NONE),
            QUICK_LABEL (notice, NULL),
            QUICK_START_BUTTONS (TRUE, TRUE),
                QUICK_BUTTON (N_ ("&OK"), B_ENTER, NULL, NULL),
                QUICK_BUTTON (N_ ("&Wizard"), B_MONGO_FILTER_WIZARD, mongo_filter_wizard_cb, NULL),
                QUICK_BUTTON (N_ ("Edit as &File"), B_MONGO_FILTER_EDIT, mongo_filter_edit_cb, NULL),
                QUICK_BUTTON (N_ ("&Cancel"), B_CANCEL, NULL, NULL),
            QUICK_END,
            // clang-format on
        };
        WRect r = { -1, -1, 0, 76 };
        quick_dialog_t qdlg = {
            .rect = r,
            .title = N_ ("MongoDB find"),
            .help = "[MongoDB Plugin]",
            .help_file = MC_PLUGIN_DIR "/mongo_panel.hlp",
            .widgets = quick_widgets,
            .callback = NULL,
            .mouse_callback = NULL,
        };
        rc = quick_dialog (&qdlg);
    }

    s_sample_fn = NULL;
    s_sample_ctx = NULL;

    g_free (initial_filter);
    g_free (initial_proj);
    g_free (initial_sort);

    if (rc == B_CANCEL)
    {
        g_free (r_filter);
        g_free (r_proj);
        g_free (r_sort);
        g_free (r_limit);
        return MONGO_FILTER_DIALOG_CANCEL;
    }

    /* B_ENTER: parse the four inputs. */
    {
        mongo_filter_t *built = mongo_filter_new ();
        char *parse_err = NULL;

        if (!parse_bson_field (r_filter, "Filter", &built->filter, &parse_err)
            || !parse_bson_field (r_proj, "Projection", &built->projection, &parse_err)
            || !parse_bson_field (r_sort, "Sort", &built->sort, &parse_err))
        {
            mongo_filter_free (built);
            g_free (r_filter);
            g_free (r_proj);
            g_free (r_sort);
            g_free (r_limit);
            if (err_out != NULL)
                *err_out = parse_err;
            else
                g_free (parse_err);
            return MONGO_FILTER_DIALOG_CANCEL;
        }
        if (r_limit != NULL && r_limit[0] != '\0')
        {
            char *endp = NULL;
            long long v;

            errno = 0;
            v = strtoll (r_limit, &endp, 10);
            if (endp == r_limit || (endp != NULL && *endp != '\0') || v < 0 || errno == ERANGE)
            {
                mongo_filter_free (built);
                g_free (r_filter);
                g_free (r_proj);
                g_free (r_sort);
                g_free (r_limit);
                if (err_out != NULL)
                    *err_out = g_strdup ("Limit: not a non-negative integer");
                return MONGO_FILTER_DIALOG_CANCEL;
            }
            built->limit = (gint64) v;
        }

        g_free (r_filter);
        g_free (r_proj);
        g_free (r_sort);
        g_free (r_limit);

        if (mongo_filter_is_empty (built))
        {
            mongo_filter_free (built);
            return MONGO_FILTER_DIALOG_CLEAR;
        }
        *out = built;
        return MONGO_FILTER_DIALOG_OK;
    }
}

/* --------------------------------------------------------------------------------------------- */

/* Sample one doc from the current scope (bucket range + active filter) and
   return its relaxed-extJSON, pretty-printed and base64-truncated. */
static char *
mongo_sample_for_dialog (gpointer ctx, char **err_out)
{
    bson_t *doc;
    char *raw;
    char *trimmed;

    (void) ctx;
    if (err_out != NULL)
        *err_out = NULL;
    doc = mongo_sample_doc (err_out);
    if (doc == NULL)
        return NULL;

    raw = bson_as_relaxed_extended_json (doc, NULL);
    bson_destroy (doc);
    if (raw == NULL)
    {
        if (err_out != NULL)
            *err_out = g_strdup ("Failed to render document as JSON.");
        return NULL;
    }
    trimmed = mongo_render_truncate_base64 (raw, 24);
    bson_free (raw);
    if (trimmed != NULL)
    {
        char *pretty = mongo_render_pretty_json (trimmed, 2);
        g_free (trimmed);
        trimmed = pretty;
    }
    return trimmed;
}

/* --------------------------------------------------------------------------------------------- */

/* Rebuild the unfiltered view for the current scope: buckets if the scope is
   large enough to split, otherwise a flat list. */
static gboolean
mongo_rebuild_scope_view (mongo_data_t *data)
{
    if (mongo_should_split (data, mongo_scope_count (data)))
    {
        mongo_docs_free (data);
        if (!mongo_build_bucket_view (data))
            return FALSE;
        data->level = MONGO_LEVEL_BUCKETS;
        return TRUE;
    }
    mongo_bucket_view_free (data);
    mongo_docs_free (data);
    if (!mongo_load_page (data, data->coll_name, 0))
        return FALSE;
    data->level = MONGO_LEVEL_DOCS;
    return TRUE;
}

/* Replace data->filter with @new_filter (may be NULL) and rebuild the view.
   A non-empty filter always collapses to a flat, capped list over the WHOLE
   collection (the bucket stack is dropped) -- never folders. Clearing the
   filter restores the bucket/flat view from the collection root.
   Caller transfers ownership of @new_filter. */
static gboolean
mongo_apply_filter (mongo_data_t *data, mongo_filter_t *new_filter)
{
    mongo_filter_drop (data);
    data->filter = new_filter;

    /* Reset to the collection root either way: a filter searches the whole
       collection, and clearing returns to the top of the bucket tree. */
    mongo_bucket_stack_free (data);
    mongo_docs_free (data);

    if (new_filter != NULL)
    {
        data->level = MONGO_LEVEL_DOCS;
        if (!mongo_load_page (data, data->coll_name, 0))
            return FALSE;
        return TRUE;
    }

    return mongo_rebuild_scope_view (data);
}

/* --------------------------------------------------------------------------------------------- */

/* Run the filter dialog against the already-entered collection and apply the
   result to the current scope. */
void
mongo_run_filter_dialog (mongo_data_t *data)
{
    mongo_filter_t *new_filter = NULL;
    char *err = NULL;
    mongo_filter_dialog_result_t r;

    r = mongo_filter_dialog_run (data->filter, mongo_sample_for_dialog, data, mongo_flat_cap (data),
                                 &new_filter, &err);
    if (r == MONGO_FILTER_DIALOG_CANCEL)
    {
        if (err != NULL)
        {
            mongo_show_message (data->host, TRUE, err);
            g_free (err);
        }
        return;
    }
    (void) mongo_apply_filter (data, r == MONGO_FILTER_DIALOG_CLEAR ? NULL : new_filter);
}

/* F6 on a collection in the COLLS list: enter it (on confirm) and apply the
   filter, without requiring the user to chdir in first. */
void
mongo_filter_from_colls (mongo_data_t *data)
{
    const GString *cur;
    const char *name;
    char *target;
    mongo_filter_t *new_filter = NULL;
    char *err = NULL;
    mongo_filter_dialog_result_t r;

    cur = (data->host != NULL && data->host->get_current != NULL)
        ? data->host->get_current (data->host)
        : NULL;
    if (cur == NULL || cur->len == 0 || strcmp (cur->str, "..") == 0)
        return;
    name = cur->str;
    if (name[0] == '/')
        name++;
    if (name[0] == '\0')
        return;
    target = g_strdup (name);

    g_free (data->pending_coll);
    data->pending_coll = g_strdup (target);

    r = mongo_filter_dialog_run (NULL, mongo_sample_for_dialog, data, mongo_flat_cap (data),
                                 &new_filter, &err);

    g_free (data->pending_coll);
    data->pending_coll = NULL;

    if (r == MONGO_FILTER_DIALOG_CANCEL)
    {
        if (err != NULL)
        {
            mongo_show_message (data->host, TRUE, err);
            g_free (err);
        }
        g_free (target);
        return;
    }
    if (!mongo_enter_collection (data, target))
    {
        mongo_filter_free (new_filter);
        g_free (target);
        return;
    }
    g_free (target);
    if (r == MONGO_FILTER_DIALOG_OK)
        (void) mongo_apply_filter (data, new_filter);
}
