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
 * EWeekViewClutterEventItem - displays the background, times and icons for an event
 * in the week/month views. A separate EText canvas item is used to display &
 * edit the text.
 */

#ifndef E_WEEK_VIEW_CLUTTER_EVENT_ITEM_H
#define E_WEEK_VIEW_CLUTTER_EVENT_ITEM_H

#include "e-week-view.h"

/* Standard GObject macros */
#define E_TYPE_WEEK_VIEW_CLUTTER_EVENT_ITEM \
	(e_week_view_clutter_event_item_get_type ())
#define E_WEEK_VIEW_CLUTTER_EVENT_ITEM(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_WEEK_VIEW_CLUTTER_EVENT_ITEM, EWeekViewClutterEventItem))
#define E_WEEK_VIEW_CLUTTER_EVENT_ITEM_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_WEEK_VIEW_CLUTTER_EVENT_ITEM, EWeekViewClutterEventItemClass))
#define E_IS_WEEK_VIEW_CLUTTER_EVENT_ITEM(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_WEEK_VIEW_CLUTTER_EVENT_ITEM))
#define E_IS_WEEK_VIEW_CLUTTER_EVENT_ITEM_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_WEEK_VIEW_CLUTTER_EVENT_ITEM))
#define E_WEEK_VIEW_CLUTTER_EVENT_ITEM_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_WEEK_VIEW_CLUTTER_EVENT_ITEM, EWeekViewClutterEventItemClass))

G_BEGIN_DECLS

typedef struct _EWeekViewClutterEventItem EWeekViewClutterEventItem;
typedef struct _EWeekViewClutterEventItemClass EWeekViewClutterEventItemClass;
typedef struct _EWeekViewClutterEventItemPrivate EWeekViewClutterEventItemPrivate;

struct _EWeekViewClutterEventItem {
	MxBoxLayout parent;
	EWeekViewClutterEventItemPrivate *priv;
};

struct _EWeekViewClutterEventItemClass {
	MxBoxLayoutClass parent_class;
};

GType		e_week_view_clutter_event_item_get_type	(void);
gint		e_week_view_clutter_event_item_get_event_num
						(EWeekViewClutterEventItem *event_item);
void		e_week_view_clutter_event_item_set_event_num
						(EWeekViewClutterEventItem *event_item,
						 gint event_num);
gint		e_week_view_clutter_event_item_get_span_num
						(EWeekViewClutterEventItem *event_item);
void		e_week_view_clutter_event_item_set_span_num
						(EWeekViewClutterEventItem *event_item,
						 gint span_num);
void		e_week_view_clutter_event_item_redraw 
						(EWeekViewClutterEventItem *item);
const char *	e_week_view_clutter_event_item_get_text 
						(EWeekViewClutterEventItem *event_item);
void		e_week_view_clutter_event_item_set_text 
						(EWeekViewClutterEventItem *event_item,
				 	 	 const char *txt);
void 		e_week_view_clutter_event_item_switch_editing_mode 
						(EWeekViewClutterEventItem *item);
void 		e_week_view_clutter_event_item_switch_normal_mode 
						(EWeekViewClutterEventItem *item);
void 		e_week_view_clutter_event_item_switch_viewing_mode 
						(EWeekViewClutterEventItem *item);
const char *	e_week_view_clutter_event_item_get_edit_text 
						(EWeekViewClutterEventItem *item);

EWeekViewClutterEventItem * e_week_view_clutter_event_item_new (EWeekView *view);


G_END_DECLS

#endif /* E_WEEK_VIEW_CLUTTER_EVENT_ITEM_H */
