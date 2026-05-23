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

/*** forward declarations ************************************************************************/

static int mongo_filter_sample_cb (WButton *button, int action);

/*** file scope macros ***************************************************************************/

#define B_MONGO_FILTER_CLEAR  (B_USER + 1)
#define B_MONGO_FILTER_SAMPLE (B_USER + 2)

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

/* Open a sampled document in the internal editor; on close, if the file was
   modified, copy its contents into the Filter input.

   This lets the user pull one matching document, trim it down to just the
   keys/values that should drive a query, and "save -> exit" to land in
   Filter. Always returns 0 to keep the parent dialog open. */
static int
mongo_filter_sample_cb (WButton *button, int action)
{
    char *json = NULL;
    char *err = NULL;
    char *tmp_path = NULL;
    int tmp_fd;
    GError *gerr = NULL;
    edit_arg_t *arg;
    struct stat st_before, st_after;
    gboolean had_before;
    gchar *edited = NULL;
    gsize edited_len = 0;
    Widget *winput;
    gsize json_len;

    (void) action;

    if (s_sample_fn == NULL)
        return 0;
    json = s_sample_fn (s_sample_ctx, &err);
    if (json == NULL)
    {
        message (D_ERROR, _ ("MongoDB sample"), "%s",
                 err != NULL ? err : _ ("No documents in current scope."));
        g_free (err);
        return 0;
    }

    tmp_fd = g_file_open_tmp ("mc-mongo-sampleXXXXXX.json", &tmp_path, &gerr);
    if (tmp_fd < 0)
    {
        message (D_ERROR, _ ("MongoDB sample"), "%s",
                 gerr != NULL ? gerr->message : "Cannot open temp file.");
        if (gerr != NULL)
            g_error_free (gerr);
        g_free (json);
        return 0;
    }
    json_len = strlen (json);
    if (write (tmp_fd, json, json_len) != (ssize_t) json_len)
    {
        close (tmp_fd);
        unlink (tmp_path);
        g_free (tmp_path);
        g_free (json);
        message (D_ERROR, _ ("MongoDB sample"), "%s", "Failed to write temp file.");
        return 0;
    }
    close (tmp_fd);
    g_free (json);

    had_before = stat (tmp_path, &st_before) == 0;

    arg = edit_arg_new (tmp_path, 0);
    edit_file (arg);
    edit_arg_free (arg);

    if (had_before && stat (tmp_path, &st_after) == 0 && st_before.st_mtime == st_after.st_mtime
        && st_before.st_size == st_after.st_size)
    {
        unlink (tmp_path);
        g_free (tmp_path);
        return 0;
    }

    if (g_file_get_contents (tmp_path, &edited, &edited_len, NULL))
    {
        while (edited_len > 0
               && (edited[edited_len - 1] == '\n' || edited[edited_len - 1] == '\r'
                   || edited[edited_len - 1] == ' ' || edited[edited_len - 1] == '\t'))
            edited[--edited_len] = '\0';
        winput = WIDGET (button)->owner != NULL
            ? widget_find_by_id ((Widget *) WIDGET (button)->owner, s_filter_input_id)
            : NULL;
        if (winput != NULL)
            input_assign_text (INPUT (winput), edited);
        g_free (edited);
    }
    unlink (tmp_path);
    g_free (tmp_path);
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
                QUICK_BUTTON (N_ ("&Sample"), B_MONGO_FILTER_SAMPLE, mongo_filter_sample_cb, NULL),
                QUICK_BUTTON (N_ ("&Cancel"), B_CANCEL, NULL, NULL),
                QUICK_BUTTON (N_ ("C&lear"), B_MONGO_FILTER_CLEAR, NULL, NULL),
            QUICK_END,
            // clang-format on
        };
        WRect r = { -1, -1, 0, 64 };
        quick_dialog_t qdlg = {
            .rect = r,
            .title = N_ ("MongoDB find"),
            .help = "[MongoDB Plugin]",
            .help_file = MC_PLUGIN_DIR "/mongo_panel.hlp",
            .widgets = quick_widgets,
            .callback = NULL,
            .mouse_callback = NULL,
        };
        if (sample_fn == NULL)
            quick_widgets[7].state = WST_DISABLED;
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
    if (rc == B_MONGO_FILTER_CLEAR)
    {
        g_free (r_filter);
        g_free (r_proj);
        g_free (r_sort);
        g_free (r_limit);
        return MONGO_FILTER_DIALOG_CLEAR;
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
    mongo_data_t *data = (mongo_data_t *) ctx;
    bson_t *doc;
    char *raw;
    char *trimmed;
    char *err = NULL;
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
    doc = mongo_conn_sample_one_doc (
        data->client, data->db_name, coll, data->coll_name != NULL ? mongo_current_lo (data) : NULL,
        data->coll_name != NULL ? mongo_current_hi (data) : NULL, fextra, &err);
    if (doc == NULL)
    {
        if (err_out != NULL)
            *err_out = err;
        else
            g_free (err);
        return NULL;
    }

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
