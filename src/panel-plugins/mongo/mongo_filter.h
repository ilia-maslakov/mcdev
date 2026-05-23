/** \file mongo_filter.h
 *  \brief MongoDB plugin: find-style filter dialog.
 */

#ifndef MC_PANEL_MONGO_FILTER_H
#define MC_PANEL_MONGO_FILTER_H

#include "lib/global.h"

struct _bson_t;
typedef struct _bson_t bson_t;

/*** structures ********************************************************************************/

typedef struct
{
    bson_t *filter;     /* {$match} body; NULL = none */
    bson_t *projection; /* find().projection; NULL = full doc */
    bson_t *sort;       /* find().sort; NULL = default {_id:1} */
    gint64 limit;       /* 0 = no override */
} mongo_filter_t;

typedef enum
{
    MONGO_FILTER_DIALOG_OK = 0,
    MONGO_FILTER_DIALOG_CANCEL,
    MONGO_FILTER_DIALOG_CLEAR,
} mongo_filter_dialog_result_t;

/*** API ***************************************************************************************/

mongo_filter_t *mongo_filter_new (void);
void mongo_filter_free (mongo_filter_t *f);

/* TRUE if no filter/projection/sort/limit/bypass set. */
gboolean mongo_filter_is_empty (const mongo_filter_t *f);

/* Optional [Sample] button callback. Returns a g_strdup'd JSON string for
   one document from the caller's scope, or NULL with @err_out set. */
typedef char *(*mongo_filter_sample_fn) (gpointer ctx, char **err_out);

/* Open the modal find-style dialog. @initial may be NULL. When @sample_fn
   is non-NULL, a [Sample] button opens one document in the editor and copies
   the saved result into the Filter input. @result_cap is shown in a notice
   that filtered results are a flat, capped list.
   On OK, *out gets a freshly-allocated mongo_filter_t (caller frees).
   On CLEAR, *out is set to NULL.
   On JSON parse error, returns CANCEL and writes a g_strdup'd message
   into @err_out (caller frees) if non-NULL. */
mongo_filter_dialog_result_t mongo_filter_dialog_run (const mongo_filter_t *initial,
                                                      mongo_filter_sample_fn sample_fn,
                                                      gpointer sample_ctx, gint64 result_cap,
                                                      mongo_filter_t **out, char **err_out);

#endif
