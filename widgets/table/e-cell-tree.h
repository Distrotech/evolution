/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* ECellTree - Tree item for e-table.
 * Copyright (C) 2000 Ximian, Inc.
 * Author: Chris Toshok <toshok@ximian.com>
 *
 */
#ifndef _E_CELL_TREE_H_
#define _E_CELL_TREE_H_

#include <libgnomeui/gnome-canvas.h>
#include <gal/e-table/e-cell.h>

#define E_CELL_TREE_TYPE        (e_cell_tree_get_type ())
#define E_CELL_TREE(o)          (GTK_CHECK_CAST ((o), E_CELL_TREE_TYPE, ECellTree))
#define E_CELL_TREE_CLASS(k)    (GTK_CHECK_CLASS_CAST((k), E_CELL_TREE_TYPE, ECellTreeClass))
#define E_IS_CELL_TREE(o)       (GTK_CHECK_TYPE ((o), E_CELL_TREE_TYPE))
#define E_IS_CELL_TREE_CLASS(k) (GTK_CHECK_CLASS_TYPE ((k), E_CELL_TREE_TYPE))

typedef struct {
	ECell parent;

	gboolean draw_lines;

	GdkPixbuf   *open_pixbuf;
	GdkPixbuf   *closed_pixbuf;

	ECell *subcell;
} ECellTree;

typedef struct {
	ECellClass parent_class;
} ECellTreeClass;

GtkType    e_cell_tree_get_type (void);
ECell     *e_cell_tree_new      (GdkPixbuf *open_pixbuf,
				 GdkPixbuf *closed_pixbuf,
				 gboolean draw_lines,
				 ECell *subcell);
void       e_cell_tree_construct (ECellTree *ect,
				  GdkPixbuf *open_pixbuf,
				  GdkPixbuf *closed_pixbuf,
				  gboolean draw_lines,
				  ECell *subcell);

#endif /* _E_CELL_TREE_H_ */


