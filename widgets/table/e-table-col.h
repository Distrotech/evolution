/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _E_TABLE_COL_H_
#define _E_TABLE_COL_H_

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gal/e-table/e-cell.h>

#define E_TABLE_COL_TYPE        (e_table_col_get_type ())
#define E_TABLE_COL(o)          (GTK_CHECK_CAST ((o), E_TABLE_COL_TYPE, ETableCol))
#define E_TABLE_COL_CLASS(k)    (GTK_CHECK_CLASS_CAST((k), E_TABLE_COL_TYPE, ETableColClass))
#define E_IS_TABLE_COL(o)       (GTK_CHECK_TYPE ((o), E_TABLE_COL_TYPE))
#define E_IS_TABLE_COL_CLASS(k) (GTK_CHECK_CLASS_TYPE ((k), E_TABLE_COL_TYPE))

typedef struct _ETableCol ETableCol;
typedef struct _ETableColClass ETableColClass;
typedef enum _ETableColArrow ETableColArrow;

enum _ETableColArrow {
	E_TABLE_COL_ARROW_NONE = 0,
	E_TABLE_COL_ARROW_UP,
	E_TABLE_COL_ARROW_DOWN
};

/*
 * Information about a single column
 */
struct _ETableCol {
	GtkObject    base;
	char        *text;
	GdkPixbuf   *pixbuf;
	int          min_width;
	int          width;
	double       expansion;
	short        x;
	GCompareFunc compare;
	unsigned int is_pixbuf:1;
	unsigned int selected:1;
	unsigned int resizeable:1;
	unsigned int sortable:1;
	unsigned int groupable:1;
	int          col_idx;

	GtkJustification justification;

	ECell         *ecell;
};

struct _ETableColClass {
	GtkObjectClass parent_class;
};

GtkType        e_table_col_get_type        (void);
ETableCol     *e_table_col_new             (int col_idx, const char *text,
					    double expansion, int min_width,
					    ECell *ecell, GCompareFunc compare,
					    gboolean resizable);
ETableCol     *e_table_col_new_with_pixbuf (int col_idx, GdkPixbuf *pixbuf,
					    double expansion, int min_width,
					    ECell *ecell, GCompareFunc compare,
					    gboolean resizable);
void           e_table_col_destroy         (ETableCol *etc);


#endif /* _E_TABLE_COL_H_ */

