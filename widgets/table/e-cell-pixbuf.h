/*
 * e-cell-pixbuf.h - An ECell that displays a GdkPixbuf
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 *
 * Authors:
 *		Vladimir Vukicevic <vladimir@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#ifndef _E_CELL_PIXBUF_H_
#define _E_CELL_PIXBUF_H_

#include <table/e-table.h>

/* Standard GObject macros */
#define E_TYPE_CELL_PIXBUF \
	(e_cell_pixbuf_get_type ())
#define E_CELL_PIXBUF(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_CELL_PIXBUF, ECellPixbuf))
#define E_CELL_PIXBUF_CLASS(cls) \
	(G_TYPE_CHECK_INSTANCE_CAST_CLASS \
	((cls), E_TYPE_CELL_PIXBUF, ECellPixbufClass))
#define E_IS_CELL_PIXBUF(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_CELL_PIXBUF))
#define E_IS_CELL_PIXBUF_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_CELL_PIXBUF))
#define E_CELL_PIXBUF_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_CELL_PIXBUF, ECellPixbufClass))

G_BEGIN_DECLS

typedef struct _ECellPixbuf ECellPixbuf;
typedef struct _ECellPixbufClass ECellPixbufClass;

struct _ECellPixbuf {
	ECell parent;

	gint selected_column;
	gint focused_column;
	gint unselected_column;
};

struct _ECellPixbufClass {
	ECellClass parent_class;
};

GType		e_cell_pixbuf_get_type		(void) G_GNUC_CONST;
ECell *		e_cell_pixbuf_new		(void);

G_END_DECLS

#endif /* _E_CELL_PIXBUF_H */
