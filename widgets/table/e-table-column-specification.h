/*
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
 *		Chris Lahey <clahey@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#ifndef _E_TABLE_COLUMN_SPECIFICATION_H_
#define _E_TABLE_COLUMN_SPECIFICATION_H_

#include <glib-object.h>
#include <libxml/tree.h>

/* Standard GObject macros */
#define E_TYPE_TABLE_COLUMN_SPECIFICATION \
	(e_table_column_specification_get_type ())
#define E_TABLE_COLUMN_SPECIFICATION(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_TABLE_COLUMN_SPECIFICATION, ETableColumnSpecification))
#define E_TABLE_COLUMN_SPECIFICATION_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_TABLE_COLUMN_SPECIFICATION, ETableColumnSpecificationClass))
#define E_IS_TABLE_COLUMN_SPECIFICATION(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_TABLE_COLUMN_SPECIFICATION))
#define E_IS_TABLE_COLUMN_SPECIFICATION_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_TABLE_COLUMN_SPECIFICATION))
#define E_TABLE_COLUMN_SPECIFICATION_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_TABLE_COLUMN_SPECIFICATION, ETableColumnSpecificationClass))

G_BEGIN_DECLS

typedef struct _ETableColumnSpecification ETableColumnSpecification;
typedef struct _ETableColumnSpecificationClass ETableColumnSpecificationClass;

struct _ETableColumnSpecification {
	GObject parent;

	gint model_col;
	gint compare_col;
	gchar *title;
	gchar *pixbuf;

	gdouble expansion;
	gint minimum_width;
	guint resizable : 1;
	guint disabled : 1;

	gchar *cell;
	gchar *compare;
	gchar *search;
	gchar *sortable;
	gint priority;
};

struct _ETableColumnSpecificationClass {
	GObjectClass parent_class;
};

GType		e_table_column_specification_get_type	(void) G_GNUC_CONST;
ETableColumnSpecification *
		e_table_column_specification_new	(void);
void		e_table_column_specification_load_from_node
					(ETableColumnSpecification *state,
					 const xmlNode *node);
xmlNode *	e_table_column_specification_save_to_node
					(ETableColumnSpecification *state,
					 xmlNode *parent);

G_END_DECLS

#endif /* _E_TABLE_COLUMN_SPECIFICATION_H_ */
