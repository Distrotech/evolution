/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _E_TABLE_SORTED_VARIABLE_H_
#define _E_TABLE_SORTED_VARIABLE_H_

#include <gtk/gtkobject.h>
#include <gal/e-table/e-table-model.h>
#include <gal/e-table/e-table-subset-variable.h>
#include <gal/e-table/e-table-sort-info.h>
#include <gal/e-table/e-table-header.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define E_TABLE_SORTED_VARIABLE_TYPE        (e_table_sorted_variable_get_type ())
#define E_TABLE_SORTED_VARIABLE(o)          (GTK_CHECK_CAST ((o), E_TABLE_SORTED_VARIABLE_TYPE, ETableSortedVariable))
#define E_TABLE_SORTED_VARIABLE_CLASS(k)    (GTK_CHECK_CLASS_CAST((k), E_TABLE_SORTED_VARIABLE_TYPE, ETableSortedVariableClass))
#define E_IS_TABLE_SORTED_VARIABLE(o)       (GTK_CHECK_TYPE ((o), E_TABLE_SORTED_VARIABLE_TYPE))
#define E_IS_TABLE_SORTED_VARIABLE_CLASS(k) (GTK_CHECK_CLASS_TYPE ((k), E_TABLE_SORTED_VARIABLE_TYPE))

typedef struct {
	ETableSubsetVariable base;

	ETableSortInfo *sort_info;
	
	ETableHeader *full_header;

	int              table_model_changed_id;
	int              table_model_row_changed_id;
	int              table_model_cell_changed_id;
	int              sort_info_changed_id;
	int              sort_idle_id;
	int		 insert_idle_id;
	int		 insert_count;

} ETableSortedVariable;

typedef struct {
	ETableSubsetVariableClass parent_class;
} ETableSortedVariableClass;

GtkType      e_table_sorted_variable_get_type (void);
ETableModel *e_table_sorted_variable_new      (ETableModel *etm, ETableHeader *header, ETableSortInfo *sort_info);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _E_TABLE_SORTED_VARIABLE_H_ */
