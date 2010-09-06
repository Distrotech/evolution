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
 * EWeekViewClutterTitlesItem - displays the 'Monday', 'Tuesday' etc. at the top of
 * the Month calendar view.
 */

#ifndef E_WEEK_VIEW_CLUTTER_TITLES_ITEM_H
#define E_WEEK_VIEW_CLUTTER_TITLES_ITEM_H

#include "e-week-view.h"

/* Standard GObject macros */
#define E_TYPE_WEEK_VIEW_CLUTTER_TITLES_ITEM \
	(e_week_view_clutter_titles_item_get_type ())
#define E_WEEK_VIEW_CLUTTER_TITLES_ITEM(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_WEEK_VIEW_CLUTTER_TITLES_ITEM, EWeekViewClutterTitlesItem))
#define E_WEEK_VIEW_CLUTTER_TITLES_ITEM_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_WEEK_VIEW_CLUTTER_TITLES_ITEM, EWeekViewClutterTitlesItemClass))
#define E_IS_WEEK_VIEW_CLUTTER_TITLES_ITEM(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_WEEK_VIEW_CLUTTER_TITLES_ITEM))
#define E_IS_WEEK_VIEW_CLUTTER_TITLES_ITEM_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_WEEK_VIEW_CLUTTER_TITLES_ITEM))
#define E_WEEK_VIEW_CLUTTER_TITLES_ITEM_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_WEEK_VIEW_CLUTTER_TITLES_ITEM, EWeekViewClutterTitlesItemClass))

G_BEGIN_DECLS

typedef struct _EWeekViewClutterTitlesItem EWeekViewClutterTitlesItem;
typedef struct _EWeekViewClutterTitlesItemClass EWeekViewClutterTitlesItemClass;
typedef struct _EWeekViewClutterTitlesItemPrivate EWeekViewClutterTitlesItemPrivate;

struct _EWeekViewClutterTitlesItem {
	ClutterCairoTexture parent;
	EWeekViewClutterTitlesItemPrivate *priv;
};

struct _EWeekViewClutterTitlesItemClass {
	ClutterCairoTextureClass parent_class;
};

GType		e_week_view_clutter_titles_item_get_type(void);
EWeekView *	e_week_view_clutter_titles_item_get_week_view
						(EWeekViewClutterTitlesItem *titles_item);
void		e_week_view_clutter_titles_item_set_week_view
						(EWeekViewClutterTitlesItem *titles_item,
						 EWeekView *week_view);
void		e_week_view_clutter_titles_item_redraw 
						(EWeekViewClutterTitlesItem *item);
void		e_week_view_clutter_titles_item_set_size 
						(EWeekViewClutterTitlesItem *item, 
						 int width, 
						 int height);
G_END_DECLS

#endif /* E_WEEK_VIEW_CLUTTER_TITLES_ITEM_H */
