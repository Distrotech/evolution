/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * e-cell-checkbox.h: Checkbox cell renderer
 * Copyright 1999, 2000, Ximian, Inc.
 *
 * Authors:
 *   Miguel de Icaza <miguel@ximian.com>
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
#ifndef _E_CELL_CHECKBOX_H_
#define _E_CELL_CHECKBOX_H_

#include <gal/e-table/e-cell-toggle.h>
#include <libgnome/gnome-defs.h>

BEGIN_GNOME_DECLS

#define E_CELL_CHECKBOX_TYPE        (e_cell_checkbox_get_type ())
#define E_CELL_CHECKBOX(o)          (GTK_CHECK_CAST ((o), E_CELL_CHECKBOX_TYPE, ECellCheckbox))
#define E_CELL_CHECKBOX_CLASS(k)    (GTK_CHECK_CLASS_CAST((k), E_CELL_CHECKBOX_TYPE, ECellCheckboxClass))
#define E_IS_CELL_CHECKBOX(o)       (GTK_CHECK_TYPE ((o), E_CELL_CHECKBOX_TYPE))
#define E_IS_CELL_CHECKBOX_CLASS(k) (GTK_CHECK_CLASS_TYPE ((k), E_CELL_CHECKBOX_TYPE))

typedef struct {
	ECellToggle parent;
} ECellCheckbox;

typedef struct {
	ECellToggleClass parent_class;
} ECellCheckboxClass;

GtkType    e_cell_checkbox_get_type (void);
ECell     *e_cell_checkbox_new      (void);

END_GNOME_DECLS

#endif /* _E_CELL_CHECKBOX_H_ */

