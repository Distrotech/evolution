/*
 * e-week-view-clutter-main-item.h
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
 *		Srinivasa Ragavan <sragavan@gnome.org>
 *
 * Copyright (C) 2010 Intel Corporation. (www.intel.com)
 *
 */

/*
 * EWeekViewClutterMainItem - displays the background grid and dates for the Week and
 * Month calendar views.
 */

#ifndef E_WEEK_VIEW_CLUTTER_MAIN_ITEM_H
#define E_WEEK_VIEW_CLUTTER_MAIN_ITEM_H

#include "e-week-view.h"

/* Standard GObject macros */
#define E_TYPE_WEEK_VIEW_CLUTTER_MAIN_ITEM \
	(e_week_view_clutter_main_item_get_type ())
#define E_WEEK_VIEW_CLUTTER_MAIN_ITEM(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_WEEK_VIEW_CLUTTER_MAIN_ITEM, EWeekViewClutterMainItem))
#define E_WEEK_VIEW_CLUTTER_MAIN_ITEM_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_WEEK_VIEW_CLUTTER_MAIN_ITEM, EWeekViewClutterMainItemClass))
#define E_IS_WEEK_VIEW_CLUTTER_MAIN_ITEM(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_WEEK_VIEW_CLUTTER_MAIN_ITEM))
#define E_IS_WEEK_VIEW_CLUTTER_MAIN_ITEM_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_WEEK_VIEW_CLUTTER_MAIN_ITEM))
#define E_WEEK_VIEW_CLUTTER_MAIN_ITEM_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_WEEK_VIEW_CLUTTER_MAIN_ITEM, EWeekViewClutterMainItemClass))

G_BEGIN_DECLS

typedef struct _EWeekViewClutterMainItem EWeekViewClutterMainItem;
typedef struct _EWeekViewClutterMainItemClass EWeekViewClutterMainItemClass;
typedef struct _EWeekViewClutterMainItemPrivate EWeekViewClutterMainItemPrivate;

struct _EWeekViewClutterMainItem {
	ClutterCairoTexture parent;
	EWeekViewClutterMainItemPrivate *priv;
};

struct _EWeekViewClutterMainItemClass {
	ClutterCairoTextureClass parent_class;
};

GType		e_week_view_clutter_main_item_get_type	(void);
EWeekView *	e_week_view_clutter_main_item_get_week_view
						(EWeekViewClutterMainItem *main_item);
void		e_week_view_clutter_main_item_set_week_view
						(EWeekViewClutterMainItem *main_item,
						 EWeekView *week_view);
void		e_week_view_clutter_main_item_redraw 
						(EWeekViewClutterMainItem *item);
void		e_week_view_clutter_main_item_set_size 
						(EWeekViewClutterMainItem *item, 
						 int width, 
						 int height);
void		e_week_view_clutter_main_item_update_selection 
						(EWeekViewClutterMainItem *item);

G_END_DECLS

#endif /* E_WEEK_VIEW_CLUTTER_MAIN_ITEM_H */
