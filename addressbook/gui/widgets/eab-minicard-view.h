/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Copyright (C) 2004  Novell, Inc.
 * Author: Chris Toshok <toshok@ximian.com>
 */
#ifndef __EAB_MINICARD_VIEW_H__
#define __EAB_MINICARD_VIEW_H__

#include "e-addressbook-reflow-adapter.h"
#include <gal/widgets/e-selection-model-simple.h>
#include <libebook/e-book.h>

G_BEGIN_DECLS

#define EAB_TYPE_MINICARD_VIEW			(eab_minicard_view_get_type ())
#define EAB_MINICARD_VIEW(obj)			(G_TYPE_CHECK_INSTANCE_CAST ((obj), EAB_TYPE_MINICARD_VIEW, EABMinicardView))
#define EAB_MINICARD_VIEW_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST ((klass), EAB_TYPE_MINICARD_VIEW, EABMinicardViewClass))
#define EAB_IS_MINICARD_VIEW(obj) 		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), EAB_TYPE_MINICARD_VIEW))
#define EAB_IS_MINICARD_VIEW_CLASS(klass) 	(G_TYPE_CHECK_CLASS_TYPE ((obj), EAB_TYPE_MINICARD_VIEW))


typedef struct _EABMinicardView       EABMinicardView;
typedef struct _EABMinicardViewClass  EABMinicardViewClass;
typedef struct _EABMinicardViewPrivate EABMinicardViewPrivate;

struct _EABMinicardView
{
	GtkWidget parent;

	EABMinicardViewPrivate *priv;
};

struct _EABMinicardViewClass
{
	GtkWidgetClass parent_class;

	void (*selection_change)     (EABMinicardView *view);
	void (*column_width_changed) (EABMinicardView *view, int width);
	void (*right_click)          (EABMinicardView *view, GdkEvent *event);
};

GtkWidget *eab_minicard_view_new              (void);
GtkWidget *eab_minicard_view_new_with_adapter (EAddressbookReflowAdapter *adapter);
GType      eab_minicard_view_get_type         (void);

ESelectionModel *eab_minicard_view_get_selection_model  (EABMinicardView       *view);

G_END_DECLS

#endif /* __EAB_MINICARD_VIEW_H__ */
