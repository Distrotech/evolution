/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _E_TABLE_COLUMN_H_
#define _E_TABLE_COLUMN_H_

#include <gtk/gtkobject.h>
#include <gdk/gdk.h>
#include "e-table-sort-info.h"
#include "e-table-col.h"

typedef struct _ETableColumnSet ETableColumnSet;

#define E_TABLE_COLUMN_SET_TYPE        (e_table_column_set_get_type ())
#define E_TABLE_COLUMN_SET(o)          (GTK_CHECK_CAST ((o), E_TABLE_COLUMN_SET_TYPE, ETableColumnSet))
#define E_TABLE_COLUMN_SET_CLASS(k)    (GTK_CHECK_CLASS_CAST((k), E_TABLE_COLUMN_SET_TYPE, ETableColumnSetClass))
#define E_IS_TABLE_COLUMN_SET(o)       (GTK_CHECK_TYPE ((o), E_TABLE_COLUMN_SET_TYPE))
#define E_IS_TABLE_COLUMN_SET_CLASS(k) (GTK_CHECK_CLASS_TYPE ((k), E_TABLE_COLUMN_SET_TYPE))

/*
 * A Columnar header.
 */
struct _ETableColumnSet {
	GtkObject base;

	int col_count;
	int width;
	int nominal_width;

	ETableSortInfo *sort_info;
	int sort_info_group_change_id;

	ETableCol **columns;
	gboolean selectable;
	
	GSList *change_queue, *change_tail;
	gint idle;
};

typedef struct {
	GtkObjectClass parent_class;

	void (*structure_change) (ETableColumnSet *etcs);
	void (*dimension_change) (ETableColumnSet *etcs, int col);
} ETableColumnSetClass;

GtkType        e_table_column_set_get_type   (void);
ETableColumnSet  *e_table_column_set_new        (void);

void        e_table_column_set_add_column    (ETableColumnSet *etcs,
					      ETableCol *tc, const char *name);
ETableCol * e_table_column_set_get_column    (ETableColumnSet *etcs,
					      const char *name);
ETableCol **e_table_column_set_get_columns   (ETableColumnSet *etcs);

gboolean    e_table_column_set_selection_ok  (ETableColumnSet *etcs);
int         e_table_column_set_get_selected  (ETableColumnSet *etcs);
int         e_table_column_set_total_width   (ETableColumnSet *etcs);
void        e_table_column_set_move          (ETableColumnSet *etcs,
					      int source_index,
					      int target_index);
void        e_table_column_set_remove        (ETableColumnSet *etcs, int idx);
void        e_table_column_set_set_size      (ETableColumnSet *etcs, int idx, int size);
void        e_table_column_set_set_selection (ETableColumnSet *etcs,
					      gboolean allow_selection);

int         e_table_column_set_col_diff      (ETableColumnSet *etcs,
					      int start_col, int end_col);

void        e_table_column_set_calc_widths   (ETableColumnSet *etcs);

GList      *e_table_column_set_get_selected_indexes (ETableColumnSet *etcs);


#endif /* _E_TABLE_COLUMN_SET_H_ */

