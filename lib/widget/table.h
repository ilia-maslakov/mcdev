/** \file table.h
 *  \brief Header: WTable widget
 */

#ifndef MC__WIDGET_TABLE_H
#define MC__WIDGET_TABLE_H

#include "lib/global.h"                /* GLib types */

/* forward declarations needed by widget-common.h */
struct Widget;
typedef struct Widget Widget;
struct WGroup;
typedef struct WGroup WGroup;

#include "lib/widget/rect.h"           /* WRect */
#include "lib/widget/widget-common.h"  /* Widget */
#include "lib/strutil.h"               /* align_crt_t */

/*** typedefs(not structures) and defined constants **********************************************/

#define TABLE(x) ((WTable *) (x))

/*** enums ***************************************************************************************/

typedef enum
{
    TABLE_COL_TEXT = 0,
    TABLE_COL_CHECK
} table_col_type_t;

/*** structures declarations (and typedefs of structures)*****************************************/

typedef struct
{
    int width;             /* column width in terminal cells */
    align_crt_t align;     /* J_LEFT, J_RIGHT, J_CENTER, J_LEFT_FIT */
    table_col_type_t type; /* TABLE_COL_TEXT or TABLE_COL_CHECK (default 0 = TEXT) */
} table_column_def_t;

typedef struct
{
    int (*get_nrows) (const void *data);
    const char *(*get_text) (const void *data, int row, int col);
    gboolean (*get_checked) (const void *data, int row, int col);
    void (*set_checked) (void *data, int row, int col, gboolean val);
    void *data;
} table_datasource_t;

typedef struct
{
    Widget widget;

    int ncols;                    /* number of columns */
    table_column_def_t *col_defs; /* column definitions array (owned) */

    table_datasource_t datasource; /* external data provider */

    int top;            /* first visible row index */
    int current;        /* current (selected) row index */
    int cursor_y;       /* cached cursor row for MSG_CURSOR */
    gboolean scrollbar; /* draw scrollbar when rows > visible lines */
    int color_idx;      /* override normal color: DLG_COLOR_* index, or -1 for default */
    gboolean has_check_cols; /* TRUE when at least one col has TABLE_COL_CHECK */
} WTable;

/*** global variables defined in .c file *********************************************************/

/*** declarations of public functions ************************************************************/

WTable *table_new (int y, int x, int height, int width, int ncols,
                   const table_column_def_t *col_defs);
void table_set_datasource (WTable *t, table_datasource_t ds);
int table_get_current (const WTable *t);
void table_set_current (WTable *t, int pos);

/*** inline functions ****************************************************************************/

#endif /* MC__WIDGET_TABLE_H */
