#ifndef _E_TABLE_SORTED_H_
#define _E_TABLE_SORTED_H_

#include <gtk/gtkobject.h>
#include <gal/e-table/e-table-model.h>
#include <gal/e-table/e-table-subset.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define E_TABLE_SORTED_TYPE        (e_table_sorted_get_type ())
#define E_TABLE_SORTED(o)          (GTK_CHECK_CAST ((o), E_TABLE_SORTED_TYPE, ETableSorted))
#define E_TABLE_SORTED_CLASS(k)    (GTK_CHECK_CLASS_CAST((k), E_TABLE_SORTED_TYPE, ETableSortedClass))
#define E_IS_TABLE_SORTED(o)       (GTK_CHECK_TYPE ((o), E_TABLE_SORTED_TYPE))
#define E_IS_TABLE_SORTED_CLASS(k) (GTK_CHECK_CLASS_TYPE ((k), E_TABLE_SORTED_TYPE))

typedef struct {
	ETableSubset base;

	short         sort_col;
	GCompareFunc  compare;
} ETableSorted;

typedef struct {
	ETableSubsetClass parent_class;
} ETableSortedClass;

GtkType      e_table_sorted_get_type (void);
ETableModel *e_table_sorted_new      (ETableModel *etm, int col, GCompareFunc compare);
void         e_table_sorted_resort   (ETableSorted *ets, int col, GCompareFunc compare);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _E_TABLE_SORTED_H_ */
