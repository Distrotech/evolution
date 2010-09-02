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

/*
 * EDayViewClutterTopItem - displays the top part of the Day/Work Week calendar view.
 */

#ifndef E_DAY_VIEW_CLUTTER_TOP_ITEM_H
#define E_DAY_VIEW_CLUTTER_TOP_ITEM_H

#include "e-day-view.h"

/* Standard GObject macros */
#define E_TYPE_DAY_VIEW_CLUTTER_TOP_ITEM \
	(e_day_view_clutter_top_item_get_type ())
#define E_DAY_VIEW_CLUTTER_TOP_ITEM(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_DAY_VIEW_CLUTTER_TOP_ITEM, EDayViewClutterTopItem))
#define E_DAY_VIEW_CLUTTER_TOP_ITEM_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_DAY_VIEW_CLUTTER_TOP_ITEM, EDayViewClutterTopItemClass))
#define E_IS_DAY_VIEW_CLUTTER_TOP_ITEM(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_DAY_VIEW_CLUTTER_TOP_ITEM))
#define E_IS_DAY_VIEW_CLUTTER_TOP_ITEM_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_DAY_VIEW_CLUTTER_TOP_ITEM))
#define E_DAY_VIEW_CLUTTER_TOP_ITEM_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_DAY_VIEW_CLUTTER_TOP_ITEM, EDayViewClutterTopItemClass))

G_BEGIN_DECLS

typedef struct _EDayViewClutterTopItem EDayViewClutterTopItem;
typedef struct _EDayViewClutterTopItemClass EDayViewClutterTopItemClass;
typedef struct _EDayViewClutterTopItemPrivate EDayViewClutterTopItemPrivate;

struct _EDayViewClutterTopItem {
	ClutterCairoTexture parent;
	EDayViewClutterTopItemPrivate *priv;
};

struct _EDayViewClutterTopItemClass {
	ClutterCairoTextureClass parent_class;
};

GType		e_day_view_clutter_top_item_get_type	(void);
void		e_day_view_clutter_top_item_get_day_label
						(EDayView *day_view,
						 gint day,
						 gchar *buffer,
						 gint buffer_len);
EDayView *	e_day_view_clutter_top_item_get_day_view(EDayViewClutterTopItem *top_item);
void		e_day_view_clutter_top_item_set_day_view(EDayViewClutterTopItem *top_item,
						 EDayView *day_view);
gboolean	e_day_view_clutter_top_item_get_show_dates
						(EDayViewClutterTopItem *top_item);
void		e_day_view_clutter_top_item_set_show_dates
						(EDayViewClutterTopItem *top_item,
						 gboolean show_dates);
void		e_day_view_clutter_top_item_set_size 
						(EDayViewClutterTopItem *item, 
						 int width, 
						 int height);
void		e_day_view_clutter_top_item_redraw 
						(EDayViewClutterTopItem *item);
void		e_day_view_clutter_top_item_update_selection 
						(EDayViewClutterTopItem *item);

G_END_DECLS

#endif /* E_DAY_VIEW_CLUTTER_TOP_ITEM_H */
