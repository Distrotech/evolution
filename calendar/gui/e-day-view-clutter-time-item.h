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
 *		Srinivasa Ragavan <sragavan@gnome.org>
 *
 * Copyright (C) 2010 Intel Corporation. (www.intel.com)
 *
 */

#ifndef E_DAY_VIEW_CLUTTER_TIME_ITEM_H
#define E_DAY_VIEW_CLUTTER_TIME_ITEM_H

#include "e-day-view.h"

/* Standard GObject macros */
#define E_TYPE_DAY_VIEW_CLUTTER_TIME_ITEM \
	(e_day_view_clutter_time_item_get_type ())
#define E_DAY_VIEW_CLUTTER_TIME_ITEM(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_DAY_VIEW_CLUTTER_TIME_ITEM, EDayViewClutterTimeItem))
#define E_DAY_VIEW_CLUTTER_TIME_ITEM_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_DAY_VIEW_CLUTTER_TIME_ITEM, EDayViewClutterTimeItemClass))
#define E_IS_DAY_VIEW_CLUTTER_TIME_ITEM(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_DAY_VIEW_CLUTTER_TIME_ITEM))
#define E_IS_DAY_VIEW_CLUTTER_TIME_ITEM_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_DAY_VIEW_CLUTTER_TIME_ITEM))
#define E_DAY_VIEW_CLUTTER_TIME_ITEM_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_DAY_VIEW_CLUTTER_TIME_ITEM, EDayViewClutterTimeItemClass))

G_BEGIN_DECLS

typedef struct _EDayViewClutterTimeItem EDayViewClutterTimeItem;
typedef struct _EDayViewClutterTimeItemClass EDayViewClutterTimeItemClass;
typedef struct _EDayViewClutterTimeItemPrivate EDayViewClutterTimeItemPrivate;

struct _EDayViewClutterTimeItem {
	ClutterCairoTexture parent;
	EDayViewClutterTimeItemPrivate *priv;
	ClutterActor *stage;
};

struct _EDayViewClutterTimeItemClass {
	ClutterCairoTextureClass parent_class;
};

GType		e_day_view_clutter_time_item_get_type	(void);
EDayView *	e_day_view_clutter_time_item_get_day_view
						(EDayViewClutterTimeItem *time_item);
void		e_day_view_clutter_time_item_set_day_view
						(EDayViewClutterTimeItem *time_item,
						 EDayView *day_view);
gint		e_day_view_clutter_time_item_get_column_width
						(EDayViewClutterTimeItem *time_item);
icaltimezone *	e_day_view_clutter_time_item_get_second_zone
						(EDayViewClutterTimeItem *time_item);
void		e_day_view_clutter_time_item_redraw 
						(EDayViewClutterTimeItem *item);
void		e_day_view_clutter_time_item_update 
						(EDayViewClutterTimeItem *item);
EDayViewClutterTimeItem *	
		e_day_view_clutter_time_item_new(EDayView *day_view, 
						 gint width, 
						 gint height);
void		e_day_view_clutter_time_item_set_size 
						(EDayViewClutterTimeItem *item, 
						 int width, 
						 int height);
						

G_END_DECLS

#endif /* E_DAY_VIEW_CLUTTER_TIME_ITEM_H */
