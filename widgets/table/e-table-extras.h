/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * e-table-extras.h
 * Copyright 2000, 2001, Ximian, Inc.
 *
 * Authors:
 *   Chris Lahey <clahey@ximian.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License, version 2, as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#ifndef _E_TABLE_EXTRAS_H_
#define _E_TABLE_EXTRAS_H_

#include <gtk/gtkobject.h>
#include <gal/e-table/e-cell.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

BEGIN_GNOME_DECLS

#define E_TABLE_EXTRAS_TYPE        (e_table_extras_get_type ())
#define E_TABLE_EXTRAS(o)          (GTK_CHECK_CAST ((o), E_TABLE_EXTRAS_TYPE, ETableExtras))
#define E_TABLE_EXTRAS_CLASS(k)    (GTK_CHECK_CLASS_CAST((k), E_TABLE_EXTRAS_TYPE, ETableExtrasClass))
#define E_IS_TABLE_EXTRAS(o)       (GTK_CHECK_TYPE ((o), E_TABLE_EXTRAS_TYPE))
#define E_IS_TABLE_EXTRAS_CLASS(k) (GTK_CHECK_CLASS_TYPE ((k), E_TABLE_EXTRAS_TYPE))

typedef struct {
	GtkObject base;

	GHashTable *cells;
	GHashTable *compares;
	GHashTable *pixbufs;
} ETableExtras;

typedef struct {
	GtkObjectClass parent_class;
} ETableExtrasClass;

GtkType       e_table_extras_get_type     (void);
ETableExtras *e_table_extras_new          (void);

void          e_table_extras_add_cell     (ETableExtras *extras,
					   char         *id,
					   ECell        *cell);
ECell        *e_table_extras_get_cell     (ETableExtras *extras,
					   char         *id);

void          e_table_extras_add_compare  (ETableExtras *extras,
					   char         *id,
					   GCompareFunc  compare);
GCompareFunc  e_table_extras_get_compare  (ETableExtras *extras,
					   char         *id);

void          e_table_extras_add_pixbuf   (ETableExtras *extras,
					   char         *id,
					   GdkPixbuf    *pixbuf);
GdkPixbuf    *e_table_extras_get_pixbuf   (ETableExtras *extras,
					   char         *id);

END_GNOME_DECLS

#endif /* _E_TABLE_EXTRAS_H_ */
