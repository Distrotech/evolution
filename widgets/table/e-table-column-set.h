/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _E_TABLE_COLUMN_SET_H_
#define _E_TABLE_COLUMN_SET_H_

#include <gtk/gtkobject.h>
#include <gdk/gdk.h>
#include <gal/e-table/e-table-col.h>

typedef struct _ETableColumnSet ETableColumnSet;
typedef struct _ETableColumnSetClass ETableColumnSetClass;

#define E_TABLE_COLUMN_SET_TYPE        (e_table_column_set_get_type ())
#define E_TABLE_COLUMN_SET(o)          (GTK_CHECK_CAST ((o), E_TABLE_COLUMN_SET_TYPE, ETableColumnSet))
#define E_TABLE_COLUMN_SET_CLASS(k)    (GTK_CHECK_CLASS_CAST((k), E_TABLE_COLUMN_SET_TYPE, ETableColumnSetClass))
#define E_IS_TABLE_COLUMN_SET(o)       (GTK_CHECK_TYPE ((o), E_TABLE_COLUMN_SET_TYPE))
#define E_IS_TABLE_COLUMN_SET_CLASS(k) (GTK_CHECK_CLASS_TYPE ((k), E_TABLE_COLUMN_SET_TYPE))

typedef void (*ETableColumnSetFunc) (ETableColumnSet *etcs,
				     ETableCol       *col,
				     const char      *name,
				     gpointer         user_data);

/*
 * A Set of Columns
 */
struct _ETableColumnSet {
	GtkObject base;

	GHashTable *columns;
};

struct _ETableColumnSetClass{
	GtkObjectClass parent_class;
};

GtkType     e_table_column_set_get_type        (void);
ETableColumnSet  *e_table_column_set_new       (void);
					       
void        e_table_column_set_add_column      (ETableColumnSet *etcs,
					       	ETableCol *tc, const char *name);
ETableCol * e_table_column_set_get_column      (ETableColumnSet *etcs,
					       	const char *name);
void        e_table_column_set_remove_column   (ETableColumnSet *etcs, const char *name);

void        e_table_column_set_columns_foreach (ETableColumnSet     *etcs,
					       	ETableColumnSetFunc  func,
						gpointer             user_data);


#endif /* _E_TABLE_COLUMN_SET_H_ */

