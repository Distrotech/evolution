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
 *		Srinivasa Ragavan <sragavan@gnome.org>
 *
 * Copyright (C) 2010 Intel Corporation. (www.intel.com)
 *
 */

/*
 * EDayViewClutterMainItem - item which displays most of the appointment
 * data in the main Day/Work Week display.
 */

#ifndef E_DAY_VIEW_CLUTTER_MAIN_ITEM_H
#define E_DAY_VIEW_CLUTTER_MAIN_ITEM_H

#include "e-day-view.h"

/* Standard GObject macros */
#define E_TYPE_DAY_VIEW_CLUTTER_MAIN_ITEM \
	(e_day_view_clutter_main_item_get_type ())
#define E_DAY_VIEW_CLUTTER_MAIN_ITEM(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_DAY_VIEW_CLUTTER_MAIN_ITEM, EDayViewClutterMainItem))
#define E_DAY_VIEW_CLUTTER_MAIN_ITEM_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_DAY_VIEW_CLUTTER_MAIN_ITEM, EDayViewClutterMainItemClass))
#define E_IS_DAY_VIEW_CLUTTER_MAIN_ITEM(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_DAY_VIEW_CLUTTER_MAIN_ITEM))
#define E_IS_DAY_VIEW_CLUTTER_MAIN_ITEM_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_DAY_VIEW_CLUTTER_MAIN_ITEM))
#define E_DAY_VIEW_CLUTTER_MAIN_ITEM_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_TYPE \
	((obj), E_TYPE_DAY_VIEW_CLUTTER_MAIN_ITEM, EDayViewClutterMainItemClass))

G_BEGIN_DECLS

typedef struct _EDayViewClutterMainItem EDayViewClutterMainItem;
typedef struct _EDayViewClutterMainItemClass EDayViewClutterMainItemClass;
typedef struct _EDayViewClutterMainItemPrivate EDayViewClutterMainItemPrivate;

struct _EDayViewClutterMainItem {
	ClutterCairoTexture parent;
	EDayViewClutterMainItemPrivate *priv;
	ClutterActor *stage;
};

struct _EDayViewClutterMainItemClass {
	ClutterCairoTextureClass parent_class;
};

GType		e_day_view_clutter_main_item_get_type	(void);
EDayView *	e_day_view_clutter_main_item_get_day_view
						(EDayViewClutterMainItem *main_item);
void		e_day_view_clutter_main_item_set_day_view
						(EDayViewClutterMainItem *main_item,
						 EDayView *day_view);
void		e_day_view_clutter_main_item_set_size 
						(EDayViewClutterMainItem *item, 
						 int width, 
						 int height);
void		e_day_view_clutter_main_item_redraw 
						(EDayViewClutterMainItem *item);
void		e_day_view_clutter_main_item_update_marcus_bains 
						(EDayViewClutterMainItem *item);
void		e_day_view_clutter_main_item_update_selection
						(EDayViewClutterMainItem *item);

G_END_DECLS

#endif /* E_DAY_VIEW_CLUTTER_MAIN_ITEM_H */
