/*
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
 *		Miguel de Icaza <miguel@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#ifndef _E_CELL_CHECKBOX_H_
#define _E_CELL_CHECKBOX_H_

#include <table/e-cell-toggle.h>

/* Standard GObject macros */
#define E_TYPE_CELL_CHECKBOX \
	(e_cell_checkbox_get_type ())
#define E_CELL_CHECKBOX(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_CELL_CHECKBOX, ECellCheckbox))
#define E_CELL_CHECKBOX_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_CELL_CHECKBOX, ECellCheckboxClass))
#define E_IS_CELL_CHECKBOX(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_CELL_CHECKBOX))
#define E_IS_CELL_CHECKBOX_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_CELL_CHECKBOX))
#define E_CELL_CHECKBOX_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_CELL_CHECKBOX, ECellCheckboxClass))

G_BEGIN_DECLS

typedef struct _ECellCheckbox ECellCheckbox;
typedef struct _ECellCheckboxClass ECellCheckboxClass;

struct _ECellCheckbox {
	ECellToggle parent;
};

struct _ECellCheckboxClass {
	ECellToggleClass parent_class;
};

GType		e_cell_checkbox_get_type	(void) G_GNUC_CONST;
ECell *		e_cell_checkbox_new		(void);

G_END_DECLS

#endif /* _E_CELL_CHECKBOX_H_ */

