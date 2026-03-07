/** \file table.h
 *  \brief Header: WTable widget
 */

#ifndef MC__WIDGET_TABLE_H
#define MC__WIDGET_TABLE_H

#include "lib/strutil.h" /* align_crt_t */

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
    Widget widget;

    int ncols;                    /* number of columns */
    table_column_def_t *col_defs; /* column definitions array (owned) */

    GPtrArray *rows;    /* array of (char **) row data, each is ncols strings */
    int nrows;          /* cached row count */
    int top;            /* first visible row index */
    int current;        /* current (selected) row index */
    int cursor_y;       /* cached cursor row for MSG_CURSOR */
    gboolean scrollbar; /* draw scrollbar when rows > visible lines */
    int color_idx;      /* override normal color: DLG_COLOR_* index, or -1 for default */

    GPtrArray *checks;       /* parallel to rows; gboolean[ncols] per row; NULL if no CHECK cols */
    gboolean has_check_cols; /* TRUE when at least one col has TABLE_COL_CHECK */
} WTable;

/*** global variables defined in .c file *********************************************************/

/*** declarations of public functions ************************************************************/

WTable *table_new (int y, int x, int height, int width, int ncols,
                   const table_column_def_t *col_defs);
void table_add_row (WTable *t, ...);
void table_clear (WTable *t);
int table_get_current (const WTable *t);
void table_set_current (WTable *t, int pos);
gboolean table_get_checked (const WTable *t, int row, int col);
void table_set_checked (WTable *t, int row, int col, gboolean val);

/*** inline functions ****************************************************************************/

#endif /* MC__WIDGET_TABLE_H */
