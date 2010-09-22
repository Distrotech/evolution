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
 * EDayViewClutterEventItem - displays the background, times and icons for an event
 * in the week/month views. A separate EText canvas item is used to display &
 * edit the text.
 */

#ifndef E_DAY_VIEW_CLUTTER_EVENT_ITEM_H
#define E_DAY_VIEW_CLUTTER_EVENT_ITEM_H

#include "e-day-view.h"

/* Standard GObject macros */
#define E_TYPE_DAY_VIEW_CLUTTER_EVENT_ITEM \
	(e_day_view_clutter_event_item_get_type ())
#define E_DAY_VIEW_CLUTTER_EVENT_ITEM(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_DAY_VIEW_CLUTTER_EVENT_ITEM, EDayViewClutterEventItem))
#define E_DAY_VIEW_CLUTTER_EVENT_ITEM_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_DAY_VIEW_CLUTTER_EVENT_ITEM, EDayViewClutterEventItemClass))
#define E_IS_WEEK_VIEW_CLUTTER_EVENT_ITEM(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_DAY_VIEW_CLUTTER_EVENT_ITEM))
#define E_IS_WEEK_VIEW_CLUTTER_EVENT_ITEM_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_DAY_VIEW_CLUTTER_EVENT_ITEM))
#define E_DAY_VIEW_CLUTTER_EVENT_ITEM_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_DAY_VIEW_CLUTTER_EVENT_ITEM, EDayViewClutterEventItemClass))

G_BEGIN_DECLS

typedef struct _EDayViewClutterEventItem EDayViewClutterEventItem;
typedef struct _EDayViewClutterEventItemClass EDayViewClutterEventItemClass;
typedef struct _EDayViewClutterEventItemPrivate EDayViewClutterEventItemPrivate;

struct _EDayViewClutterEventItem {
	MxBoxLayout parent;
	EDayViewClutterEventItemPrivate *priv;
};

struct _EDayViewClutterEventItemClass {
	MxBoxLayoutClass parent_class;
};

GType		e_day_view_clutter_event_item_get_type	(void);
gint		e_day_view_clutter_event_item_get_event_num
						(EDayViewClutterEventItem *event_item);
void		e_day_view_clutter_event_item_set_event_num
						(EDayViewClutterEventItem *event_item,
						 gint event_num);
gint		e_day_view_clutter_event_item_get_day_num
						(EDayViewClutterEventItem *event_item);
void		e_day_view_clutter_event_item_set_day_num
						(EDayViewClutterEventItem *event_item,
						 gint span_num);
void		e_day_view_clutter_event_item_redraw 
						(EDayViewClutterEventItem *item);
const char *	e_day_view_clutter_event_item_get_text 
						(EDayViewClutterEventItem *event_item);
void		e_day_view_clutter_event_item_set_text 
						(EDayViewClutterEventItem *event_item,
				 	 	 const char *txt);
void 		e_day_view_clutter_event_item_switch_editing_mode 
						(EDayViewClutterEventItem *item);
void 		e_day_view_clutter_event_item_switch_normal_mode 
						(EDayViewClutterEventItem *item);
void 		e_day_view_clutter_event_item_switch_viewing_mode 
						(EDayViewClutterEventItem *item);
const char *	e_day_view_clutter_event_item_get_edit_text 
						(EDayViewClutterEventItem *item);

EDayViewClutterEventItem * e_day_view_clutter_event_item_new 
						(EDayView *view,
						 gint day,
						 gint event_num);

void		e_day_view_clutter_event_item_scale_destroy 
						(EDayViewClutterEventItem *item);
void		e_day_view_clutter_event_item_fade_destroy 
						(EDayViewClutterEventItem *item);

G_END_DECLS

#endif /* E_DAY_VIEW_CLUTTER_EVENT_ITEM_H */
