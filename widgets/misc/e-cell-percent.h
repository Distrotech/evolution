/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * Author :
 *  Damon Chaplin <damon@ximian.com>
 *
 * Copyright 2001, Ximian, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

/*
 * ECellPercent - a subclass of ECellText used to show an integer percentage
 * in an ETable.
 */

#ifndef _E_CELL_PERCENT_H_
#define _E_CELL_PERCENT_H_

#include <gal/e-table/e-cell-text.h>

BEGIN_GNOME_DECLS

#define E_CELL_PERCENT_TYPE        (e_cell_percent_get_type ())
#define E_CELL_PERCENT(o)          (GTK_CHECK_CAST ((o), E_CELL_PERCENT_TYPE, ECellPercent))
#define E_CELL_PERCENT_CLASS(k)    (GTK_CHECK_CLASS_CAST((k), E_CELL_PERCENT_TYPE, ECellPercentClass))
#define E_IS_CELL_NUMBER(o)       (GTK_CHECK_TYPE ((o), E_CELL_PERCENT_TYPE))
#define E_IS_CELL_NUMBER_CLASS(k) (GTK_CHECK_CLASS_TYPE ((k), E_CELL_PERCENT_TYPE))

typedef struct {
	ECellText base;
} ECellPercent;

typedef struct {
	ECellTextClass parent_class;
} ECellPercentClass;

GtkType    e_cell_percent_get_type (void);
ECell     *e_cell_percent_new      (const char *fontname, GtkJustification justify);

END_GNOME_DECLS

#endif /* _E_CELL_PERCENT_H_ */
