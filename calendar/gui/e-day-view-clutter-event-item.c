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
 * EDayViewClutterEventItem - displays the background, times and icons for an event
 * in the week/month views. A separate EText canvas item is used to display &
 * edit the text.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "e-util/e-categories-config.h"
#include "e-day-view-clutter-event-item.h"
#include <libedataserver/e-categories.h>

#include <gtk/gtk.h>
#include "e-calendar-view.h"
#include "calendar-config.h"
#include "comp-util.h"
#include "e-util/e-util.h"

#include "e-util/gtk-compat.h"

#define E_DAY_VIEW_CLUTTER_EVENT_ITEM_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_DAY_VIEW_CLUTTER_EVENT_ITEM, EDayViewClutterEventItemPrivate))

struct _EDayViewClutterEventItemPrivate {
	/* The event index in the EDayView events array. */
	gint event_num;

	/* The span index within the event. */
	gint day_num;

	/* Text */
	char *text;

	/* Texture */
	ClutterCairoTexture *texture;

	/* Week View*/
	EDayView *day_view;

	gboolean long_event;
	gboolean short_event;

	int x1;
	int y1;
	int x2;
	int y2;
	int spanx;
	int spany;

	ClutterActor *text_item;
};

enum {
	PROP_0,
	PROP_EVENT_NUM,
	PROP_DAY_NUM,
	PROP_TEXT
};

static gpointer parent_class;

static gboolean
can_draw_in_region (GdkRegion *draw_region,
                    gint x,
                    gint y,
                    gint width,
                    gint height)
{
	GdkRectangle rect;

	g_return_val_if_fail (draw_region != NULL, FALSE);

	rect.x = x;
	rect.y = y;
	rect.width = width;
	rect.height = height;

	return gdk_region_rect_in (draw_region, &rect) !=
		GDK_OVERLAP_RECTANGLE_OUT;
}

#if 0
static ECalendarViewPosition
day_view_clutter_event_item_get_position (EDayViewClutterEventItem *event_item,
                                   gdouble x,
                                   gdouble y)
{
	EDayView *day_view;
	GnomeCanvasItem *item;

	day_view = event_item->priv->day_view;

	if (x < event_item->priv->x1 + E_DAY_VIEW_EVENT_X_PAD
	    || x >= event_item->priv->x2 - E_DAY_VIEW_EVENT_X_PAD)
		return E_CALENDAR_VIEW_POS_NONE;

	/* Support left/right edge for long events only. */
	if (!e_day_view_is_one_day_event (day_view, event_item->priv->event_num)) {
		if (x < event_item->priv->x1 + E_DAY_VIEW_EVENT_X_PAD
		    + E_DAY_VIEW_EVENT_BORDER_WIDTH
		    + E_DAY_VIEW_EVENT_X_PAD)
			return E_CALENDAR_VIEW_POS_LEFT_EDGE;

		if (x >= event_item->priv->x2 + 1 - E_DAY_VIEW_EVENT_X_PAD
		    - E_DAY_VIEW_EVENT_BORDER_WIDTH
		    - E_DAY_VIEW_EVENT_X_PAD)
			return E_CALENDAR_VIEW_POS_RIGHT_EDGE;
	}

	return E_CALENDAR_VIEW_POS_EVENT;
}
#endif

static gboolean
day_view_clutter_event_item_double_click (EDayViewClutterEventItem *event_item,
                                   ClutterEvent *bevent)
{
	EDayView *day_view;

	day_view = event_item->priv->day_view;
#if 0
	if (!is_array_index_in_bounds (day_view->events, event_item->priv->event_num))
		return TRUE;

	event = &g_array_index (
		day_view->events, EDayViewEvent,
		event_item->priv->event_num);

	if (!is_comp_data_valid (event))
		return TRUE;

	if (day_view->editing_event_num >= 0) {
		EDayViewEvent *editing;

		if (!is_array_index_in_bounds (day_view->events, day_view->editing_event_num))
			return TRUE;

		editing = &g_array_index (
			day_view->events, EDayViewEvent,
			day_view->editing_event_num);

		/* Do not call edit of the component, if double clicked
		 * on the component, which is not on the server. */
		if (editing && event &&
			editing->comp_data == event->comp_data &&
			is_comp_data_valid (editing) &&
			(!event->comp_data ||
			 !is_icalcomp_on_the_server (
				event->comp_data->icalcomp,
				event->comp_data->client)))
			return TRUE;
	}

	e_day_view_stop_editing_event (day_view);

	e_calendar_view_edit_appointment (
		E_CALENDAR_VIEW (day_view),
		event->comp_data->client,
		event->comp_data->icalcomp, FALSE);
#endif
	return TRUE;
}

static gboolean
day_view_clutter_event_item_button_press (EDayViewClutterEventItem *event_item,
                                   ClutterEvent *bevent)
{
	EDayView *day_view;

	GdkEventButton *event;
	gboolean ret;

	day_view = event_item->priv->day_view;

	if (bevent->button.click_count > 1 )
		event = (GdkEventButton *)gdk_event_new (GDK_2BUTTON_PRESS);
	else
		event = (GdkEventButton *)gdk_event_new (GDK_BUTTON_PRESS);

	event->time = bevent->button.time;
	event->button = bevent->button.button;
	event->x = (gfloat) bevent->button.x;
	event->y = (gfloat) bevent->button.y;

	if (event_item->priv->day_num == E_DAY_VIEW_LONG_EVENT)
		ret = e_day_view_on_top_canvas_button_press (day_view->top_canvas, event, day_view);
	else
		ret = e_day_view_on_main_canvas_button_press (day_view->main_canvas, event, day_view);
	gdk_event_free ((GdkEvent *)event);

	return ret;

#if 0
	day_view = event_item->priv->day_view;

	if (!is_array_index_in_bounds (day_view->events, event_item->priv->event_num))
		return FALSE;

	event = &g_array_index (
		day_view->events, EDayViewEvent,
		event_item->priv->event_num);

	if (!is_array_index_in_bounds (
		day_view->spans, event->spans_index +
		event_item->priv->span_num))
		return FALSE;

	span = &g_array_index (day_view->spans, EDayViewEventSpan,
			       event->spans_index + event_item->priv->span_num);

	
	pos = day_view_clutter_event_item_get_position (event_item, (gdouble)(bevent->button.x-(float)event_item->priv->spanx),
						   (gdouble)(bevent->button.y - (float)event_item->priv->spany) );

	if (pos == E_CALENDAR_VIEW_POS_NONE)
		return FALSE;
	
	if (bevent->button.button == 1) {
		day_view->pressed_event_num = event_item->priv->event_num;
		day_view->pressed_span_num = event_item->priv->span_num;

		/* Ignore clicks on the event while editing. */
		if (span->text_item && E_TEXT (span->text_item)->editing)
			return FALSE;

		/* Remember the item clicked and the mouse position,
		   so we can start a drag if the mouse moves. */
		day_view->drag_event_x = (gint)bevent->button.x;
		day_view->drag_event_y = (gint)bevent->button.y;

		/* FIXME: Remember the day offset from the start of the event.
		 */

		return TRUE;
	} else if (bevent->button.button == 3) {
		GdkEventButton *gevent = (GdkEventButton *)gdk_event_new (GDK_BUTTON_PRESS);

		if (!gtk_widget_has_focus (GTK_WIDGET (day_view)) && 0) {
			gtk_widget_grab_focus (GTK_WIDGET (day_view));
			if (day_view->event_destroyed) {
				day_view->event_destroyed = FALSE;
				return FALSE;
			}

		}

		gevent->time = bevent->button.time;
		gevent->button = bevent->button.button;

		e_day_view_set_selected_time_range_visible (
			day_view, event->start, event->end);

		e_day_view_show_popup_menu (
			day_view, (GdkEventButton*) gevent,
			event_item->priv->event_num);
		gdk_event_free (gevent);
		//g_signal_stop_emission_by_name (
		//	event_item->canvas, "button_press_event");

		return TRUE;
	}

#endif
	return FALSE;
}

static gboolean
day_view_clutter_event_item_button_release (EDayViewClutterEventItem *event_item,
                                     ClutterEvent *event)
{
	EDayView *day_view;

	GdkEventButton *bevent;
	gboolean ret;

	day_view = event_item->priv->day_view;

	bevent = (GdkEventButton *)gdk_event_new (GDK_BUTTON_RELEASE);

	bevent->time = event->button.time;
	bevent->button = event->button.button;
	bevent->x = (gfloat) event->button.x;
	bevent->y = (gfloat) event->button.y;

	ret = e_day_view_on_main_canvas_button_release (day_view->main_canvas, bevent, day_view);
	gdk_event_free ((GdkEvent *)bevent);
	day_view->pressed_event_num = -1;

	day_view->editing_event_num = event_item->priv->event_num;
	day_view->editing_event_day = event_item->priv->day_num;

	return ret;

#if 0
	day_view = event_item->priv->day_view;

	if (day_view->pressed_event_num != -1
	    && day_view->pressed_event_num == event_item->priv->event_num
	    && day_view->pressed_span_num == event_item->priv->span_num) {
		if (day_view->editing_event_num != -1) {
			e_day_view_cancel_editing (event_item->priv->day_view);
			e_day_view_on_editing_stopped (event_item->priv->day_view, NULL, TRUE);
		}
		e_day_view_start_editing_event (day_view,
						 event_item->priv->event_num,
						 event_item->priv->span_num,
						 NULL);
		day_view->pressed_event_num = -1;

		day_view->editing_event_num = event_item->priv->event_num;
		day_view->editing_span_num = event_item->priv->span_num;

		return TRUE;
	}

	day_view->pressed_event_num = -1;
#endif
	return FALSE;
}

static void
day_view_clutter_event_item_set_property (GObject *object,
                                   guint property_id,
                                   const GValue *value,
                                   GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_EVENT_NUM:
			e_day_view_clutter_event_item_set_event_num (
				E_DAY_VIEW_CLUTTER_EVENT_ITEM (object),
				g_value_get_int (value));
			return;

		case PROP_DAY_NUM:
			e_day_view_clutter_event_item_set_day_num (
				E_DAY_VIEW_CLUTTER_EVENT_ITEM (object),
				g_value_get_int (value));
			return;
		case PROP_TEXT:
			e_day_view_clutter_event_item_set_text (
				E_DAY_VIEW_CLUTTER_EVENT_ITEM (object),
				g_value_get_string (value));			
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
day_view_clutter_event_item_get_property (GObject *object,
                                   guint property_id,
                                   GValue *value,
                                   GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_EVENT_NUM:
			g_value_set_int (
				value,
				e_day_view_clutter_event_item_get_event_num (
				E_DAY_VIEW_CLUTTER_EVENT_ITEM (object)));
			return;

		case PROP_DAY_NUM:
			g_value_set_int (
				value,
				e_day_view_clutter_event_item_get_day_num (
				E_DAY_VIEW_CLUTTER_EVENT_ITEM (object)));
			return;
		case PROP_TEXT:
			g_value_set_string (
				value,
				e_day_view_clutter_event_item_get_text (
				E_DAY_VIEW_CLUTTER_EVENT_ITEM (object)));			
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
day_view_clutter_event_item_draw_triangle (EDayViewClutterEventItem *event_item,
                                 cairo_t *cr,
                                 gint x,
                                 gint y,
                                 gint w,
                                 gint h,
                                 gint event_num)
{
	EDayView *day_view;
	EDayViewEvent *event;
	GdkColor bg_color;
	GdkPoint points[3];
	gint c1, c2;

	day_view = event_item->priv->day_view;

	points[0].x = x;
	points[0].y = y;
	points[1].x = x + w;
	points[1].y = y + (h / 2);
	points[2].x = x;
	points[2].y = y + h - 1;

	/* If the height is odd we can use the same central point for both
	   lines. If it is even we use different end-points. */
	c1 = c2 = y + (h / 2);
	if (h % 2 == 0)
		c1--;

	if (!is_array_index_in_bounds (day_view->long_events, event_num))
		return;

	event = &g_array_index (day_view->long_events, EDayViewEvent,
				event_num);

	if (!is_comp_data_valid (event))
		return;

	cairo_save (cr);
	/* Fill it in. */
	if (gdk_color_parse (
		e_cal_model_get_color_for_component (
		e_calendar_view_get_model (E_CALENDAR_VIEW (day_view)),
		event->comp_data), &bg_color)) {
		GdkColormap *colormap;

		colormap = gtk_widget_get_colormap (GTK_WIDGET (day_view));
		if (gdk_colormap_alloc_color (colormap, &bg_color, TRUE, TRUE)) {
			gdk_cairo_set_source_color (cr, &bg_color);
		} else {
			gdk_cairo_set_source_color (
				cr, &day_view->colors
				[E_DAY_VIEW_COLOR_LONG_EVENT_BACKGROUND]);
		}
	} else {
		gdk_cairo_set_source_color (
			cr, &day_view->colors
			[E_DAY_VIEW_COLOR_LONG_EVENT_BACKGROUND]);
	}

	cairo_move_to (cr, points[0].x, points[0].y);
	cairo_line_to (cr, points[1].x, points[1].y);
	cairo_line_to (cr, points[2].x, points[2].y);
	cairo_line_to (cr, points[0].x, points[0].y);
	cairo_fill (cr);
	cairo_restore (cr);

	cairo_save (cr);
	gdk_cairo_set_source_color (
		cr, &day_view->colors[E_DAY_VIEW_COLOR_LONG_EVENT_BORDER]);
	cairo_move_to (cr, x, y);
	cairo_line_to (cr, x + w, c1);
	cairo_move_to (cr, x, y + h - 1);
	cairo_line_to (cr, x + w, c2);
	cairo_stroke (cr);
	cairo_restore (cr);

}

static void
day_view_clutter_event_item_draw_long (EDayViewClutterEventItem *main_item)
{
	gint event_num;
	cairo_t *cr;
	gint x=0;
	gint y=0;
	EDayView *day_view;
	EDayViewEvent *event;
	GtkStyle *style;
	gint start_day, end_day;
	gint item_x, item_y, item_w, item_h;
	gint icon_x, icon_y, icon_x_inc;
	gint event_x, event_y;
	ECalModel *model;
	ECalComponent *comp;
	gchar buffer[16];
	gint hour, display_hour, minute, offset, time_width, time_x;
	gint min_end_time_x, suffix_width, max_icon_x;
	const gchar *suffix;
	gboolean draw_start_triangle, draw_end_triangle;
	GdkRectangle clip_rect;
	GSList *categories_list, *elem;
	PangoLayout *layout;
	GdkColor bg_color;
	cairo_pattern_t *pat;
	guint16 red, green, blue;
	gdouble cc = 65535.0;
	gboolean gradient;
	gfloat alpha;
	gdouble x0, y0, rect_height, rect_width, radius;

	event_num = main_item->priv->event_num;

	day_view = main_item->priv->day_view;
	model = e_calendar_view_get_model (E_CALENDAR_VIEW (day_view));

	gradient = calendar_config_get_display_events_gradient ();
	alpha = calendar_config_get_display_events_alpha ();

	/* If the event is currently being dragged, don't draw it. It will
	   be drawn in the special drag items. */
	if (day_view->drag_event_day == E_DAY_VIEW_LONG_EVENT
	    && day_view->drag_event_num == event_num)
		return;

	if (!e_day_view_get_long_event_position (day_view, event_num,
						 &start_day, &end_day,
						 &item_x, &item_y,
						 &item_w, &item_h))
		return;

	if (!is_array_index_in_bounds (day_view->long_events, event_num))
		return;

	event = &g_array_index (day_view->long_events, EDayViewEvent,
				event_num);

	if (!is_comp_data_valid (event))
		return;

	clutter_cairo_texture_set_surface_size (main_item->priv->texture, item_w, item_h);
	clutter_cairo_texture_clear (main_item->priv->texture);
	clutter_actor_set_size (main_item->priv->text_item, item_w, item_h);
	clutter_actor_set_size ((ClutterActor *)main_item, item_w, item_h);

	if (event->just_added)
		clutter_actor_set_opacity ((ClutterActor *)main_item, 0);

	cr = clutter_cairo_texture_create (main_item->priv->texture);
	event_x = item_x;
	event_y = item_y;
	item_x = item_y = 0;

	style = gtk_widget_get_style (GTK_WIDGET (day_view));
	comp = e_cal_component_new ();
	e_cal_component_set_icalcomponent (
		comp, icalcomponent_new_clone (event->comp_data->icalcomp));

	if (gdk_color_parse (
		e_cal_model_get_color_for_component (
		e_calendar_view_get_model (E_CALENDAR_VIEW (day_view)),
		event->comp_data), &bg_color)) {
		GdkColormap *colormap;

		colormap = gtk_widget_get_colormap (GTK_WIDGET (day_view));
		if (gdk_colormap_alloc_color (colormap, &bg_color, TRUE, TRUE)) {
			red = bg_color.red;
			green = bg_color.green;
			blue = bg_color.blue;
		} else {
			red = day_view->colors[E_DAY_VIEW_COLOR_LONG_EVENT_BACKGROUND].red;
			green = day_view->colors[E_DAY_VIEW_COLOR_LONG_EVENT_BACKGROUND].green;
			blue = day_view->colors[E_DAY_VIEW_COLOR_LONG_EVENT_BACKGROUND].blue;
		}
	} else {
		red = day_view->colors[E_DAY_VIEW_COLOR_LONG_EVENT_BACKGROUND].red;
		green = day_view->colors[E_DAY_VIEW_COLOR_LONG_EVENT_BACKGROUND].green;
		blue = day_view->colors[E_DAY_VIEW_COLOR_LONG_EVENT_BACKGROUND].blue;
	}

	/* Fill the background with white to play with transparency */
	cairo_save (cr);
	x0	   = item_x - x + 4;
	y0	   = item_y + 1 - y;
	rect_width  = item_w - 8;
	rect_height = item_h - 2;

	radius = 12;

	draw_curved_rectangle (cr, x0, y0, rect_width, rect_height, radius);

	cairo_set_source_rgba (cr, 1, 1, 1, alpha);
	cairo_fill_preserve (cr);

	cairo_restore (cr);

	/* Draw the border around the event */

	cairo_save (cr);
	x0	   = item_x - x + 4;
	y0	   = item_y + 1 - y;
	rect_width  = item_w - 8;
	rect_height = item_h - 2;

	radius = 12;

	draw_curved_rectangle (cr, x0, y0, rect_width, rect_height, radius);

	cairo_set_source_rgb (cr, red/cc, green/cc, blue/cc);
	cairo_set_line_width (cr, 1.5);
	cairo_stroke (cr);
	cairo_restore (cr);

	/* Fill in with gradient */

	cairo_save (cr);

	x0	   = item_x - x + 5.5;
	y0	   = item_y + 2.5 - y;
	rect_width  = item_w - 11;
	rect_height = item_h - 5;

	radius = 10;

	draw_curved_rectangle (cr, x0, y0, rect_width, rect_height, radius);

	if (gradient) {
		pat = cairo_pattern_create_linear (item_x - x + 5.5, item_y + 2.5 - y,
						item_x - x + 5, item_y - y + item_h + 7.5);
		//cairo_pattern_add_color_stop_rgba (pat, 1, red/cc, green/cc, blue/cc, 0.8);		
		cairo_pattern_add_color_stop_rgba (pat, 1, red/cc, green/cc, blue/cc, 0.4);
		cairo_pattern_add_color_stop_rgba (pat, 0, red/cc, green/cc, blue/cc, 0.4);
		cairo_set_source (cr, pat);
		cairo_fill_preserve (cr);
		cairo_pattern_destroy (pat);
	} else {
		cairo_set_source_rgba (cr, red/cc, green/cc, blue/cc, 0.8);
		cairo_fill_preserve (cr);
	}
	cairo_set_source_rgba (cr, red/cc, green/cc, blue/cc, 0);
	cairo_set_line_width (cr, 0.5);
	cairo_stroke (cr);
	cairo_restore (cr);

	/* When resizing we don't draw the triangles.*/
	draw_start_triangle = TRUE;
	draw_end_triangle = TRUE;
	if (day_view->resize_drag_pos != E_CALENDAR_VIEW_POS_NONE
	    && day_view->resize_event_day == E_DAY_VIEW_LONG_EVENT
	    && day_view->resize_event_num == event_num) {
		if (day_view->resize_drag_pos == E_CALENDAR_VIEW_POS_LEFT_EDGE)
			draw_start_triangle = FALSE;

		if  (day_view->resize_drag_pos == E_CALENDAR_VIEW_POS_RIGHT_EDGE)
			draw_end_triangle = FALSE;
	}

	/* If the event starts before the first day shown, draw a triangle */
	if (draw_start_triangle
	    && event->start < day_view->day_starts[start_day]) {
		day_view_clutter_event_item_draw_triangle (
			main_item, cr, item_x - x + 4, item_y - y,
			-E_DAY_VIEW_BAR_WIDTH, item_h, event_num);
	}

	/* Similar for the event end. */
	if (draw_end_triangle
	    && event->end > day_view->day_starts[end_day + 1]) {
		day_view_clutter_event_item_draw_triangle (
			main_item, cr, item_x + item_w - 4 - x,
			item_y - y, E_DAY_VIEW_BAR_WIDTH, item_h,
			event_num);
	}

	/* If we are editing the event we don't show the icons or the start
	   & end times. */
	if (day_view->editing_event_day == E_DAY_VIEW_LONG_EVENT
	    && day_view->editing_event_num == event_num) {
		g_object_unref (comp);
		cairo_destroy (cr);
		return;
	}

	/* Determine the position of the label, so we know where to place the
	   icons. Note that since the top canvas never scrolls we don't need
	   to take the scroll offset into account. It will always be 0. */

	/* Draw the start & end times, if necessary. */
	min_end_time_x = item_x + E_DAY_VIEW_LONG_EVENT_X_PAD - x;

	time_width = e_day_view_get_time_string_width (day_view);

	time_x = item_x + E_DAY_VIEW_LONG_EVENT_X_PAD - x;

	if (event->start > day_view->day_starts[start_day]) {
		offset = day_view->first_hour_shown * 60
			+ day_view->first_minute_shown + event->start_minute;
		hour = offset / 60;
		minute = offset % 60;
		/* Calculate the actual hour number to display. For 12-hour
		   format we convert 0-23 to 12-11am/12-11pm. */
		e_day_view_convert_time_to_display (day_view, hour,
						    &display_hour,
						    &suffix, &suffix_width);
		if (e_cal_model_get_use_24_hour_format (model)) {
			g_snprintf (buffer, sizeof (buffer), "%i:%02i",
				    display_hour, minute);
		} else {
			g_snprintf (buffer, sizeof (buffer), "%i:%02i%s",
				    display_hour, minute, suffix);
		}

		clip_rect.x = item_x - x;
		clip_rect.y = item_y - y;
		clip_rect.width = item_w - E_DAY_VIEW_LONG_EVENT_BORDER_WIDTH;
		clip_rect.height = item_h;
		cairo_save (cr);
		cairo_rectangle (cr, (double)clip_rect.x , (double)clip_rect.y, (double)clip_rect.width, (double)clip_rect.height);
		cairo_clip(cr);

		if (display_hour < 10)
			time_x += day_view->digit_width;

		layout = gtk_widget_create_pango_layout (GTK_WIDGET (day_view), buffer);
		cairo_move_to (cr,  time_x,
				 item_y + E_DAY_VIEW_LONG_EVENT_BORDER_HEIGHT +
				 E_DAY_VIEW_LONG_EVENT_Y_PAD - y);
		pango_cairo_show_layout (cr, layout);
		cairo_stroke (cr);
		cairo_restore (cr);
		g_object_unref (layout);


		min_end_time_x += time_width
			+ E_DAY_VIEW_LONG_EVENT_TIME_X_PAD;
	}
	
	icon_x = min_end_time_x;
	max_icon_x = item_x + item_w - E_DAY_VIEW_LONG_EVENT_X_PAD
		- E_DAY_VIEW_ICON_WIDTH;

	if (event->end < day_view->day_starts[end_day + 1]) {
		offset = day_view->first_hour_shown * 60
			+ day_view->first_minute_shown
			+ event->end_minute;
		hour = offset / 60;
		minute = offset % 60;
		time_x =
			item_x + item_w - E_DAY_VIEW_LONG_EVENT_X_PAD -
			time_width - E_DAY_VIEW_LONG_EVENT_TIME_X_PAD - x;

		if (time_x >= min_end_time_x) {
			/* Calculate the actual hour number to display. */
			e_day_view_convert_time_to_display (day_view, hour,
							    &display_hour,
							    &suffix,
							    &suffix_width);
			if (e_cal_model_get_use_24_hour_format (model)) {
				g_snprintf (buffer, sizeof (buffer),
					    "%i:%02i", display_hour, minute);
			} else {
				g_snprintf (buffer, sizeof (buffer),
					    "%i:%02i%s", display_hour, minute,
					    suffix);
			}

			if (display_hour < 10)
				time_x += day_view->digit_width;
			
			cairo_save(cr);
			layout = gtk_widget_create_pango_layout (GTK_WIDGET (day_view), buffer);
			cairo_move_to (cr, time_x,
					 item_y + E_DAY_VIEW_LONG_EVENT_Y_PAD + 1 - y);
			pango_cairo_show_layout (cr, layout);
			cairo_stroke(cr);
			cairo_restore(cr);
			g_object_unref (layout);

			max_icon_x -= time_width + E_DAY_VIEW_LONG_EVENT_TIME_X_PAD;
		}
	}

	/* Draw the icons. */
	icon_x += E_DAY_VIEW_ICON_X_PAD;
	icon_x_inc = E_DAY_VIEW_ICON_WIDTH + E_DAY_VIEW_ICON_X_PAD;
	icon_y = item_y + E_DAY_VIEW_LONG_EVENT_BORDER_HEIGHT
		+ E_DAY_VIEW_ICON_Y_PAD - y;

	if (icon_x + icon_x_inc <= max_icon_x && (
		e_cal_component_has_recurrences (comp) ||
		e_cal_component_is_instance (comp))) {
		cairo_save (cr);
		gdk_cairo_set_source_pixbuf (cr, day_view->recurrence_icon, icon_x, icon_y);
		cairo_paint (cr);
		cairo_restore (cr);

		icon_x += icon_x_inc;
	}

	if (icon_x + icon_x_inc <= max_icon_x && e_cal_component_has_attachments (comp)) {
		cairo_save (cr);
		gdk_cairo_set_source_pixbuf (cr, day_view->attach_icon, icon_x, icon_y);
		cairo_paint (cr);
		cairo_restore (cr);

		icon_x += icon_x_inc;
	}
	if (icon_x + icon_x_inc <= max_icon_x && e_cal_component_has_alarms (comp)) {
		cairo_save (cr);
		gdk_cairo_set_source_pixbuf (cr, day_view->reminder_icon, icon_x, icon_y);
		cairo_paint (cr);
		cairo_restore (cr);

		icon_x += icon_x_inc;
	}

	if (icon_x + icon_x_inc <= max_icon_x && e_cal_component_has_attendees (comp)) {
		cairo_save (cr);
		gdk_cairo_set_source_pixbuf (cr, day_view->meeting_icon, icon_x, icon_y);
		cairo_paint (cr);
		cairo_restore (cr);

		icon_x += icon_x_inc;
	}

	/* draw categories icons */
	e_cal_component_get_categories_list (comp, &categories_list);
	for (elem = categories_list; elem; elem = elem->next) {
		gchar *category;
		const gchar *file;
		GdkPixbuf *pixbuf;

		category = (gchar *) elem->data;
		file = e_categories_get_icon_file_for (category);
		if (!file)
			continue;

		pixbuf = gdk_pixbuf_new_from_file (file, NULL);
		if (pixbuf == NULL)
			continue;

		if (icon_x + icon_x_inc <= max_icon_x) {
			cairo_save (cr);
			gdk_cairo_set_source_pixbuf (cr, pixbuf, icon_x, icon_y);
			cairo_paint (cr);
			cairo_restore (cr);
			icon_x += icon_x_inc;
		}
	}

	e_cal_component_free_categories_list (categories_list);

	/* Draw text */
	if (icon_x < max_icon_x) {
		PangoLayout *layout;
		GdkColor col = e_day_view_get_text_color (day_view, event, (GtkWidget *)day_view);
		
		cairo_save (cr);
		gdk_cairo_set_source_color (cr, &col);
		
		icon_x += E_DAY_VIEW_EVENT_X_PAD;

		cairo_rectangle (cr, icon_x , item_y + E_DAY_VIEW_LONG_EVENT_Y_PAD + 1 - y, 
				 max_icon_x-icon_x, item_h - (2 *E_DAY_VIEW_EVENT_BORDER_HEIGHT) 
					- (2 *E_DAY_VIEW_ICON_Y_PAD));
		cairo_clip (cr);
		layout = gtk_widget_create_pango_layout (GTK_WIDGET (day_view), NULL);
		pango_layout_set_width (layout, (max_icon_x - icon_x - 1) * PANGO_SCALE);
		pango_layout_set_wrap (layout, PANGO_WRAP_CHAR);

		if (main_item->priv->text)
			pango_layout_set_text (layout, main_item->priv->text, -1);
				cairo_move_to (cr,
			       icon_x,
			       item_y + E_DAY_VIEW_LONG_EVENT_Y_PAD + 1 - y);
			
		pango_cairo_show_layout (cr, layout);

		cairo_stroke (cr);
		cairo_restore (cr);
		g_object_unref (layout);
	} 

	g_object_unref (comp);
	cairo_destroy (cr);

	if (event->just_added) {
		event->just_added = FALSE;
		clutter_actor_animate ((ClutterActor *)main_item, CLUTTER_LINEAR,
					400, "opacity", 255, NULL);
	}	
}

static void
day_view_clutter_event_item_draw_normal (EDayViewClutterEventItem *main_item)
{
	cairo_t *cr;
	gint y=0;
	gint day;
	gint event_num;
	GdkRegion *draw_region;	
	EDayView *day_view;
	EDayViewEvent *event;
	ECalModel *model;
	gint item_x, item_y, item_w, item_h, bar_y1, bar_y2;
	gint event_x, event_y;
	GdkColor bg_color;
	ECalComponent *comp;
	gint num_icons=0, icon_x, icon_y, icon_x_inc = 0, icon_y_inc = 0;
	gint max_icon_w, max_icon_h;
	gboolean draw_reminder_icon, draw_recurrence_icon, draw_timezone_icon, draw_meeting_icon;
	gboolean draw_attach_icon;
	ECalComponentTransparency transparency;
	cairo_pattern_t *pat;
	cairo_font_options_t *font_options;
	guint16 red, green, blue;
	gint i;
	gdouble radius, x0, y0, rect_height, rect_width, text_x_offset = 0.0;
	gfloat alpha;
	gboolean gradient;
	gdouble cc = 65535.0;
	gdouble date_fraction;
	gboolean short_event = FALSE, resize_flag = FALSE, is_editing;
	const gchar *end_resize_suffix;
	gchar *end_regsizeime;
	gint start_hour, start_display_hour, start_minute, start_suffix_width;
	gint end_hour, end_display_hour, end_minute, end_suffix_width;
	gboolean show_span = FALSE, format_time;
	gint offset, interval;
	const gchar *start_suffix;
	const gchar *end_suffix;
	gchar *text = NULL;
	gint scroll_flag = 0;
	gint row_y;
	GdkRectangle rect;
	
	day_view =  main_item->priv->day_view;
	model = e_calendar_view_get_model (E_CALENDAR_VIEW (day_view));
	day = main_item->priv->day_num;
	event_num = main_item->priv->event_num;

	if (day == -1 || event_num == -1)
		return;

	/* If the event is currently being dragged, don't draw it. It will
	   be drawn in the special drag items. */
	if (day_view->drag_event_day == day && day_view->drag_event_num == event_num)
		return;

	/* Get the position of the event. If it is not shown skip it.*/
	if (!e_day_view_get_event_position (day_view, day, event_num,
					    &item_x, &item_y,
					    &item_w, &item_h))
		return;

	rect.x = 0;
	rect.y = 0;
	rect.width = item_w;
	rect.height = item_h;
	draw_region = gdk_region_rectangle (&rect);
	event_x = item_x;
	event_y = item_y;
	item_x = item_y = 0;

#undef E_DAY_VIEW_BAR_WIDTH
#define E_DAY_VIEW_BAR_WIDTH 0	

	if (!can_draw_in_region (draw_region, item_x, item_y, item_w, item_h))
		return;

	clutter_cairo_texture_set_surface_size (main_item->priv->texture, item_w, item_h);
	clutter_actor_set_size (main_item->priv->text_item, item_w, item_h);
	clutter_actor_set_size ((ClutterActor *)main_item, item_w, item_h);
	clutter_cairo_texture_clear (main_item->priv->texture);

	cr = clutter_cairo_texture_create (main_item->priv->texture);
	gdk_cairo_set_source_color (cr,
			&day_view->colors[E_DAY_VIEW_COLOR_EVENT_VBAR]);

	gradient = calendar_config_get_display_events_gradient ();
	alpha = calendar_config_get_display_events_alpha ();

	font_options = get_font_options ();

	if (!is_array_index_in_bounds (day_view->events[day], event_num))
		return;

	event = &g_array_index (day_view->events[day], EDayViewEvent,
				event_num);

	if (!is_comp_data_valid (event))
		return;

	if (event->just_added)
		clutter_actor_set_opacity ((ClutterActor *)main_item, 0);

	/* Fill in the event background. Note that for events in the first
	   column of the day, we might not want to paint over the vertical bar,
	   since that is used for multiple events. But then you can't see
	   where the event in the first column finishes. The border is drawn
           along with the event using cairo*/

	red = day_view->colors[E_DAY_VIEW_COLOR_EVENT_BACKGROUND].red;
	green = day_view->colors[E_DAY_VIEW_COLOR_EVENT_BACKGROUND].green;
	blue = day_view->colors[E_DAY_VIEW_COLOR_EVENT_BACKGROUND].blue;

	if (gdk_color_parse (e_cal_model_get_color_for_component (e_calendar_view_get_model (E_CALENDAR_VIEW (day_view)), event->comp_data),
			     &bg_color)) {
		GdkColormap *colormap;

		colormap = gtk_widget_get_colormap (GTK_WIDGET (day_view));
		if (gdk_colormap_alloc_color (colormap, &bg_color, TRUE, TRUE)) {
			red = bg_color.red;
			green = bg_color.green;
			blue = bg_color.blue;
			}
	}

	is_editing = day_view->editing_event_day == day && day_view->editing_event_num == event_num;

	if (event->canvas_item)
		g_object_get (G_OBJECT (event->canvas_item), "x_offset", &text_x_offset, NULL);

	/* Draw shadow around the event when selected */
	if (0 && is_editing && (gtk_widget_has_focus (day_view->main_canvas))) {
		/* For embossing Item selection */
		item_x -= 1;
		item_y -= 2;

		if (MAX (0, item_w - 31.5) != 0) {
			/* Vertical Line */
			cairo_save (cr);
			pat = cairo_pattern_create_linear (item_x + item_w - E_DAY_VIEW_BAR_WIDTH + 6.5, item_y + 13.75,
								item_x + item_w - E_DAY_VIEW_BAR_WIDTH + 13.75, item_y + 13.75);
			cairo_pattern_add_color_stop_rgba (pat, 0, 0, 0, 0, 1);
			cairo_pattern_add_color_stop_rgba (pat, 0.7, 0, 0, 0, 0.2);
			cairo_pattern_add_color_stop_rgba (pat, 1, 1, 1, 1, 0.3);
			cairo_set_source (cr, pat);
			cairo_rectangle (cr, item_x + item_w - E_DAY_VIEW_BAR_WIDTH + 6.5, item_y + 14.75, 7.0, item_h - 22.0);
			cairo_fill (cr);
			cairo_pattern_destroy (pat);

			/* Arc at the right */
			pat = cairo_pattern_create_radial (item_x + item_w - E_DAY_VIEW_BAR_WIDTH + 3, item_y + 13.5, 5.0,
						item_x + item_w - E_DAY_VIEW_BAR_WIDTH + 5, item_y + 13.5, 12.0);
			cairo_pattern_add_color_stop_rgba (pat, 1, 1, 1, 1, 0.3);
			cairo_pattern_add_color_stop_rgba (pat, 0.25, 0, 0, 0, 0.2);
			cairo_pattern_add_color_stop_rgba (pat, 0, 0, 0, 0, 1);
			cairo_set_source (cr, pat);
			cairo_arc (cr, item_x + item_w - E_DAY_VIEW_BAR_WIDTH + 5, item_y + 13.5, 8.0, 11 * M_PI / 8, M_PI / 8);
			cairo_fill (cr);
			cairo_pattern_destroy (pat);

			cairo_set_source_rgb (cr, 0, 0, 0);
			cairo_set_line_width (cr, 1.25);
			cairo_move_to (cr, item_x + item_w - E_DAY_VIEW_BAR_WIDTH + 5, item_y + 9.5);
			cairo_line_to (cr, item_x + item_w - E_DAY_VIEW_BAR_WIDTH + 9.5, item_y + 15);
			cairo_stroke (cr);

			/* Horizontal line */
			pat = cairo_pattern_create_linear (item_x + E_DAY_VIEW_BAR_WIDTH + 15, item_y + item_h,
							item_x + E_DAY_VIEW_BAR_WIDTH + 15, item_y + item_h + 7);
			cairo_pattern_add_color_stop_rgba (pat, 0, 0, 0, 0, 1);
			cairo_pattern_add_color_stop_rgba (pat, 0.7, 0, 0, 0, 0.2);
			cairo_pattern_add_color_stop_rgba (pat, 1, 1, 1, 1, 0.3);
			cairo_set_source (cr, pat);
			cairo_rectangle (cr, item_x + E_DAY_VIEW_BAR_WIDTH + 16.5, item_y + item_h, item_w - 31.5, 7.0);
			cairo_fill (cr);
			cairo_pattern_destroy (pat);

			/* Bottom arc */
			pat = cairo_pattern_create_radial (item_x + E_DAY_VIEW_BAR_WIDTH + 12.5, item_y + item_h - 5, 5.0,
						item_x + E_DAY_VIEW_BAR_WIDTH + 12.5, item_y + item_h - 5, 12.0);
			cairo_pattern_add_color_stop_rgba (pat, 1, 1, 1, 1, 0.3);
			cairo_pattern_add_color_stop_rgba (pat, 0.7, 0, 0, 0, 0.2);
			cairo_pattern_add_color_stop_rgba (pat, 0, 0, 0, 0, 1);
			cairo_set_source (cr, pat);
			cairo_arc (cr, item_x + E_DAY_VIEW_BAR_WIDTH + 13, item_y + item_h - 5, 12.0, 3 * M_PI / 8, 9 * M_PI / 8);
			cairo_fill (cr);
			cairo_pattern_destroy (pat);

			cairo_set_source_rgba (cr, 0, 0, 0, 0.5);
			cairo_set_line_width (cr, 2);
			cairo_move_to (cr, item_x + E_DAY_VIEW_BAR_WIDTH + 14, item_y + item_h + 2);
			cairo_line_to (cr, item_x + E_DAY_VIEW_BAR_WIDTH + 15.5, item_y + item_h + 3);
			cairo_stroke (cr);
			cairo_set_source_rgba (cr, 0, 0, 0, 0.27);
			cairo_move_to (cr, item_x + E_DAY_VIEW_BAR_WIDTH + 15, item_y + item_h + 3.5);
			cairo_line_to (cr, item_x + E_DAY_VIEW_BAR_WIDTH + 17, item_y + item_h + 3.5);
			cairo_stroke (cr);

			/* Arc in middle */
			pat = cairo_pattern_create_radial (item_x + item_w - E_DAY_VIEW_BAR_WIDTH + 1, item_y + item_h - 4.5, 1.0,
						item_x + item_w - E_DAY_VIEW_BAR_WIDTH + 1, item_y + item_h - 4.5, 12.0);
			cairo_pattern_add_color_stop_rgba (pat, 1, 1, 1, 1, 0.3);
			cairo_pattern_add_color_stop_rgba (pat, 0.8, 0, 0, 0, 0.2);
			cairo_pattern_add_color_stop_rgba (pat, 0, 0, 0, 0, 1);
			cairo_set_source (cr, pat);
			cairo_arc (cr, item_x + item_w - E_DAY_VIEW_BAR_WIDTH + 1, item_y + item_h - 4.5, 12.0, 15 * M_PI / 8,  5 * M_PI / 8);
			cairo_fill (cr);
			cairo_pattern_destroy (pat);

			cairo_set_source_rgba (cr, 0, 0, 0, 0.27);
			cairo_move_to (cr, item_x + item_w - E_DAY_VIEW_BAR_WIDTH - 1, item_y + item_h + 3);
			cairo_line_to (cr, item_x + item_w - E_DAY_VIEW_BAR_WIDTH , item_y + item_h + 3);
			cairo_stroke (cr);

			cairo_set_source_rgba (cr, 0, 0, 0, 0.27);
			cairo_move_to (cr, item_x + item_w - E_DAY_VIEW_BAR_WIDTH + 9, item_y + item_h - 6);
			cairo_line_to (cr, item_x + item_w - E_DAY_VIEW_BAR_WIDTH + 10, item_y + item_h - 6);
			cairo_stroke (cr);

			cairo_restore (cr);

			/* Black border */
			cairo_save (cr);
			x0	   = item_x + E_DAY_VIEW_BAR_WIDTH + 9;
			y0	   = item_y + 10;
			rect_width = MAX (item_w - E_DAY_VIEW_BAR_WIDTH - 7, 0);
			rect_height = item_h - 7;

			radius = 20;

			draw_curved_rectangle (cr, x0, y0, rect_width, rect_height, radius);

			cairo_set_source_rgb (cr, 0, 0, 0);
			cairo_fill (cr);
			cairo_restore (cr);

			/* Extra Grid lines when clicked */
			cairo_save (cr);

			x0	   = item_x + E_DAY_VIEW_BAR_WIDTH + 1;
			y0	   = item_y + 2;
			rect_width  = MAX (item_w - E_DAY_VIEW_BAR_WIDTH - 3, 0);
			rect_height = item_h - 4.;

			radius = 16;

			draw_curved_rectangle (cr, x0, y0, rect_width, rect_height, radius);

			cairo_set_source_rgb (cr, 1, 1, 1);
			cairo_fill (cr);

			gdk_cairo_set_source_color (cr,
				&day_view->colors[E_DAY_VIEW_COLOR_BG_GRID]);

			for (row_y = y0;
			     row_y < rect_height + y0;
			     row_y += day_view->row_height) {
				if (row_y >= 0 && row_y < rect_height + y0) {
					cairo_set_line_width (cr, 0.7);
					cairo_move_to (cr, item_x + E_DAY_VIEW_BAR_WIDTH + 1 , row_y);
					cairo_line_to (cr, item_x + item_w -2, row_y);
					cairo_stroke (cr);
				}
			}
			cairo_restore (cr);
		}
	}

	/* Draw the background of the event with white to play with transparency */
	cairo_save (cr);

	x0	   = item_x + E_DAY_VIEW_BAR_WIDTH + 1;
	y0	   = item_y + 2;
	rect_width  = MAX (item_w - E_DAY_VIEW_BAR_WIDTH - 3, 0);
	rect_height = item_h - 4.;

	radius = 16;

	draw_curved_rectangle (cr, x0, y0, rect_width, rect_height, radius);

	cairo_set_source_rgba (cr, 1, 1, 1, alpha);
	cairo_fill (cr);

	cairo_restore (cr);

	/* Here we draw the border in event color */
	cairo_save (cr);

	x0	   = item_x + E_DAY_VIEW_BAR_WIDTH;
	y0	   = item_y + 1.;
	rect_width  = MAX (item_w - E_DAY_VIEW_BAR_WIDTH - 1., 0);
	rect_height = item_h - 2.;

	radius = 16;

	draw_curved_rectangle (cr, x0, y0, rect_width,rect_height, radius);
	cairo_set_line_width (cr, 2.);
	cairo_set_source_rgb (cr, red/cc, green/cc, blue/cc);
	cairo_stroke (cr);
	cairo_restore (cr);

	/* Fill in the Event */

	cairo_save (cr);

	x0	   = item_x + E_DAY_VIEW_BAR_WIDTH + 1.75;
	y0	   = item_y + 2.75;
	rect_width  = item_w - E_DAY_VIEW_BAR_WIDTH - 4.5;
	rect_height = item_h - 5.5;

	radius = 14;

	draw_curved_rectangle (cr, x0, y0, rect_width, rect_height, radius);

	date_fraction = rect_height / day_view->row_height;
	interval = event->end_minute - event->start_minute;

	if ((interval/day_view->mins_per_row) >= 2)
		short_event = FALSE;
	else if ((interval%day_view->mins_per_row)==0) {
		if (((event->end_minute%day_view->mins_per_row) == 0) || ((event->start_minute%day_view->mins_per_row) == 0))
			short_event = TRUE;
		}
	else
		short_event = FALSE;

	if (is_editing)
		short_event = TRUE;

	main_item->priv->short_event = short_event;

	if (gradient) {
		pat = cairo_pattern_create_linear (item_x + E_DAY_VIEW_BAR_WIDTH + 1.75, item_y + 7.75,
							item_x + E_DAY_VIEW_BAR_WIDTH + 1.75, item_y + item_h - 7.75);
		if (!short_event) {
			cairo_pattern_add_color_stop_rgba (pat, 0, red/cc, green/cc, blue/cc, 0.8);
			cairo_pattern_add_color_stop_rgba (pat, (1/(date_fraction + (rect_height/18))) - 0.0001, red/cc, green/cc, blue/cc, 0.8);			
			cairo_pattern_add_color_stop_rgba (pat, 1/(date_fraction + (rect_height/18)), red/cc, green/cc, blue/cc, 0.4);
			//cairo_pattern_add_color_stop_rgba (pat, midpoint, red/cc, green/cc, blue/cc, 0.8);
			cairo_pattern_add_color_stop_rgba (pat, 1, red/cc, green/cc, blue/cc, 0.4);
		} else {
			cairo_pattern_add_color_stop_rgba (pat, 0, red/cc, green/cc, blue/cc, 0.4);			
			//cairo_pattern_add_color_stop_rgba (pat, 0.25, red/cc, green/cc, blue/cc, 0.8);
			//cairo_pattern_add_color_stop_rgba (pat, 0.75, red/cc, green/cc, blue/cc, 0.8);			
			cairo_pattern_add_color_stop_rgba (pat, 1, red/cc, green/cc, blue/cc, 0.4);
		}
		cairo_set_source (cr, pat);
		cairo_fill_preserve (cr);
		cairo_pattern_destroy (pat);
	} else {
		cairo_set_source_rgba (cr, red/cc, green/cc, blue/cc, 0.8);
		cairo_fill_preserve (cr);
	}

	cairo_set_source_rgba (cr, red/cc, green/cc, blue/cc, 0.2);
	cairo_set_line_width (cr, 0.5);
	cairo_stroke (cr);
	cairo_restore (cr);

	/* Draw the right edge of the vertical bar. */
	cairo_save (cr);
	gdk_cairo_set_source_color (cr,
			&day_view->colors[E_DAY_VIEW_COLOR_BG_GRID]);
	cairo_set_line_width (cr, 0.7);
	cairo_move_to (cr, item_x + E_DAY_VIEW_BAR_WIDTH - 1, item_y + 1);
	cairo_line_to (cr, item_x + E_DAY_VIEW_BAR_WIDTH - 1, item_y + item_h - 2);
	cairo_stroke (cr);
	cairo_restore (cr);

	gdk_cairo_set_source_color (cr,
			&day_view->colors[E_DAY_VIEW_COLOR_EVENT_VBAR]);

	/* Draw the vertical colored bar showing when the appointment
	   begins & ends. */
	bar_y1 = event->start_minute * day_view->row_height / day_view->mins_per_row - y;
	bar_y2 = event->end_minute * day_view->row_height / day_view->mins_per_row - y;

	scroll_flag = bar_y2;

	/* When an item is being resized, we fill the bar up to the new row. */
	if (day_view->resize_drag_pos != E_CALENDAR_VIEW_POS_NONE
	    && day_view->resize_event_day == day
	    && day_view->resize_event_num == event_num) {
		resize_flag = TRUE;

		if (day_view->resize_drag_pos == E_CALENDAR_VIEW_POS_TOP_EDGE)
			bar_y1 = item_y + 1;

		else if (day_view->resize_drag_pos == E_CALENDAR_VIEW_POS_BOTTOM_EDGE) {
			bar_y2 = item_y + item_h - 1;

			end_minute = event->end_minute;

			end_hour   = end_minute / 60;
			end_minute = end_minute % 60;

			e_day_view_convert_time_to_display (day_view, end_hour,
							    &end_display_hour,
							    &end_resize_suffix,
							    &end_suffix_width);

			cairo_save (cr);
			cairo_rectangle (cr, item_x + E_DAY_VIEW_BAR_WIDTH + 1.75, item_y + 2.75,
				item_w - E_DAY_VIEW_BAR_WIDTH - 4.5,
				item_h - 5.5);
			cairo_clip (cr);
			cairo_new_path (cr);

			if (e_cal_model_get_use_24_hour_format (model)) {
				cairo_translate (cr, item_x + item_w - E_DAY_VIEW_BAR_WIDTH - 32, item_y + item_h - 8);
				end_regsizeime = g_strdup_printf ("%2i:%02i",
					 end_display_hour, end_minute);

			} else {
				cairo_translate (cr, item_x + item_w - E_DAY_VIEW_BAR_WIDTH - 48, item_y + item_h - 8);
				end_regsizeime = g_strdup_printf ("%2i:%02i%s",
						 end_display_hour, end_minute,
						 end_resize_suffix);
			}
			cairo_set_font_size (cr, 14);
			if ((red/cc > 0.7) || (green/cc > 0.7) || (blue/cc > 0.7 ))
				cairo_set_source_rgb (cr, 0, 0, 0);
			else
				cairo_set_source_rgb (cr, 1, 1, 1);
			cairo_set_font_options (cr, font_options);
			cairo_show_text (cr, end_regsizeime);
			cairo_close_path (cr);
			cairo_restore (cr);
		}
	}

	if (bar_y2 > scroll_flag)
		event->end_minute += day_view->mins_per_row;
	else if (bar_y2 < scroll_flag)
		event->end_minute -= day_view->mins_per_row;

	comp = e_cal_component_new ();
	e_cal_component_set_icalcomponent (comp, icalcomponent_new_clone (event->comp_data->icalcomp));

	/* Only fill it in if the event isn't TRANSPARENT. */
	e_cal_component_get_transparency (comp, &transparency);
	if (1 || transparency != E_CAL_COMPONENT_TRANSP_TRANSPARENT) {
		cairo_save (cr);
		pat = cairo_pattern_create_linear (item_x+1, item_y + 1,
						item_x + E_DAY_VIEW_BAR_WIDTH, item_y + item_h - 1);
		cairo_pattern_add_color_stop_rgba (pat, 1, red/cc, green/cc, blue/cc, 0.7);
		cairo_pattern_add_color_stop_rgba (pat, 0.5, red/cc, green/cc, blue/cc, 0.7);
		cairo_pattern_add_color_stop_rgba (pat, 0, red/cc, green/cc, blue/cc, 0.2);

		cairo_rectangle (cr, item_x + 1, bar_y1,
			       E_DAY_VIEW_BAR_WIDTH - 2, bar_y2 - bar_y1);

		cairo_set_source (cr, pat);
		cairo_fill (cr);
		cairo_pattern_destroy (pat);
		cairo_restore (cr);

		/* This is for achieving the white stripes in vbar across event color */
		for (i = 0; i <= (bar_y2 - bar_y1); i+=4) {
			cairo_save(cr);
			cairo_set_source_rgb (cr, 1, 1, 1);
			cairo_set_line_width (cr, 0.3);
			cairo_move_to (cr, item_x + 1, bar_y1 + i);
			cairo_line_to (cr, item_x + E_DAY_VIEW_BAR_WIDTH - 1, bar_y1 + i);
			cairo_stroke (cr);
			cairo_restore (cr);
		}
	}

	gdk_cairo_set_source_color (cr,
			&day_view->colors[E_DAY_VIEW_COLOR_EVENT_VBAR]);

	/* Draw the reminder & recurrence icons, if needed. */
	if (!resize_flag && (!is_editing || text_x_offset > E_DAY_VIEW_ICON_X_PAD)) {
		GSList *categories_pixbufs = NULL, *pixbufs;

		num_icons = 0;
		draw_reminder_icon = FALSE;
		draw_recurrence_icon = FALSE;
		draw_timezone_icon = FALSE;
		draw_meeting_icon = FALSE;
		draw_attach_icon = FALSE;
		icon_x = item_x + E_DAY_VIEW_BAR_WIDTH + E_DAY_VIEW_ICON_X_PAD;
		icon_y = item_y + E_DAY_VIEW_EVENT_BORDER_HEIGHT
			+ E_DAY_VIEW_ICON_Y_PAD;

		if (e_cal_component_has_alarms (comp)) {
			draw_reminder_icon = TRUE;
			num_icons++;
		}

		if (e_cal_component_has_recurrences (comp) || e_cal_component_is_instance (comp)) {
			draw_recurrence_icon = TRUE;
			num_icons++;
		}
		if (e_cal_component_has_attachments (comp)) {
			draw_attach_icon = TRUE;
			num_icons++;
		}
		/* If the DTSTART or DTEND are in a different timezone to our current
		   timezone, we display the timezone icon. */
		if (event->different_timezone) {
			draw_timezone_icon = TRUE;
			num_icons++;
		}

		if (e_cal_component_has_attendees (comp)) {
			draw_meeting_icon = TRUE;
			num_icons++;
		}

		num_icons += cal_comp_util_get_n_icons (comp, &categories_pixbufs);

		if (num_icons != 0) {
			//if (item_h >= (E_DAY_VIEW_ICON_HEIGHT + E_DAY_VIEW_ICON_Y_PAD) * num_icons) 
			if (!short_event) {
				icon_x_inc = 0;
				icon_y_inc = E_DAY_VIEW_ICON_HEIGHT
					+ E_DAY_VIEW_ICON_Y_PAD;
			} else {
				icon_x_inc = E_DAY_VIEW_ICON_WIDTH
					+ E_DAY_VIEW_ICON_X_PAD;
				icon_y_inc = 0;
			}

			#define fit_in_event() (icon_x + icon_x_inc < item_x + item_w && icon_y + icon_y_inc < item_y + item_h)
			#define draw_pixbuf(pf)	\
				max_icon_w = item_x + item_w - icon_x - E_DAY_VIEW_EVENT_BORDER_WIDTH;		\
				max_icon_h = item_y + item_h - icon_y - E_DAY_VIEW_EVENT_BORDER_HEIGHT;		\
														\
				if (can_draw_in_region (draw_region, icon_x, icon_y, max_icon_w, max_icon_h)) {	\
					cairo_save (cr);							\
					cairo_rectangle (cr, icon_x, icon_y, max_icon_w, max_icon_h);		\
					cairo_clip (cr);							\
					cairo_new_path (cr);							\
					gdk_cairo_set_source_pixbuf (cr, pf, icon_x, icon_y);			\
					cairo_paint (cr);							\
					cairo_close_path (cr);							\
					cairo_restore (cr);							\
				}										\
														\
				icon_x += icon_x_inc;								\
				icon_y += icon_y_inc;

			if (draw_reminder_icon && fit_in_event ()) {
				draw_pixbuf (day_view->reminder_icon);
			}

			if (draw_recurrence_icon && fit_in_event ()) {
				draw_pixbuf (day_view->recurrence_icon);
			}
			if (draw_attach_icon && fit_in_event ()) {
				draw_pixbuf (day_view->attach_icon);
			}
			if (draw_timezone_icon && fit_in_event ()) {
				draw_pixbuf (day_view->timezone_icon);
			}

			if (draw_meeting_icon && fit_in_event ()) {
				draw_pixbuf (day_view->meeting_icon);
			}

			/* draw categories icons */
			for (pixbufs = categories_pixbufs;
			     pixbufs && fit_in_event ();
			     pixbufs = pixbufs->next) {
				GdkPixbuf *pixbuf = pixbufs->data;

				draw_pixbuf (pixbuf);
			}

			#undef draw_pixbuf
			#undef fit_in_event

		}

		/* free memory */
		g_slist_foreach (categories_pixbufs, (GFunc)g_object_unref, NULL);
		g_slist_free (categories_pixbufs);
	}

	if (!short_event)
	{
		if (event->start_minute % day_view->mins_per_row != 0
			|| (day_view->show_event_end_times
			&& event->end_minute % day_view->mins_per_row != 0)) {
				offset = day_view->first_hour_shown * 60
				+ day_view->first_minute_shown;
				show_span = TRUE;
			} else {
				offset = 0;
		}
		start_minute = offset + event->start_minute;
		end_minute = offset + event->end_minute;

		format_time = (((end_minute - start_minute)/day_view->mins_per_row) >= 2) ? TRUE : FALSE;

		start_hour = start_minute / 60;
		start_minute = start_minute % 60;

		end_hour = end_minute / 60;
		end_minute = end_minute % 60;

		e_day_view_convert_time_to_display (day_view, start_hour,
						    &start_display_hour,
						    &start_suffix,
						    &start_suffix_width);
		e_day_view_convert_time_to_display (day_view, end_hour,
						    &end_display_hour,
						    &end_suffix,
						    &end_suffix_width);

		if (e_cal_model_get_use_24_hour_format (model)) {
			if (day_view->show_event_end_times && show_span) {
				/* 24 hour format with end time. */
				text = g_strdup_printf
					("%2i:%02i-%2i:%02i",
					 start_display_hour, start_minute,
					 end_display_hour, end_minute);
			} else {
				if (format_time) {
				/* 24 hour format without end time. */
				text = g_strdup_printf
					("%2i:%02i",
					 start_display_hour, start_minute);
				}
			}
		} else {
			if (day_view->show_event_end_times && show_span) {
				/* 12 hour format with end time. */
				text = g_strdup_printf
					("%2i:%02i%s-%2i:%02i%s",
					 start_display_hour, start_minute,
					 start_suffix,
					 end_display_hour, end_minute, end_suffix);
			} else {
				/* 12 hour format without end time. */
				text = g_strdup_printf
					("%2i:%02i%s",
					 start_display_hour, start_minute,
					 start_suffix);
			}
		}

		cairo_save (cr);
		cairo_rectangle (cr, item_x + E_DAY_VIEW_BAR_WIDTH + 1.75, item_y + 2.75,
			item_w - E_DAY_VIEW_BAR_WIDTH - 4.5,
			14);

		cairo_clip (cr);
		cairo_new_path (cr);

		if (icon_x_inc == 0 && num_icons != 0)
			icon_x += E_DAY_VIEW_ICON_WIDTH
					+ E_DAY_VIEW_ICON_X_PAD;

		if (resize_flag)
			cairo_move_to (cr, item_x + E_DAY_VIEW_BAR_WIDTH + 10, item_y + 13);
		else
			cairo_move_to (cr, icon_x, item_y + 13);
		if ((red/cc > 0.7) || (green/cc > 0.7) || (blue/cc > 0.7 ))
			cairo_set_source_rgb (cr, 0, 0, 0);
		else
			cairo_set_source_rgb (cr, 1, 1, 1);
		cairo_set_font_size (cr, 14.0);
		cairo_set_font_options (cr, font_options);
		cairo_show_text (cr, text);
		cairo_close_path (cr);
		cairo_restore (cr);
	}
	
	/* Draw text */
	if (icon_x < item_w) {
		PangoLayout *layout;
		GdkColor col = e_day_view_get_text_color (day_view, event, (GtkWidget *)day_view);
		int ypad = short_event ? 0 : (E_DAY_VIEW_ICON_HEIGHT + E_DAY_VIEW_ICON_Y_PAD);

		cairo_save (cr);
		gdk_cairo_set_source_color (cr, &col);
		
		icon_x += E_DAY_VIEW_EVENT_X_PAD;

		cairo_rectangle (cr, icon_x , 2 + ypad, item_w-icon_x, item_h - 2 - (2 *E_DAY_VIEW_EVENT_BORDER_HEIGHT) 
					- (2 *E_DAY_VIEW_ICON_Y_PAD) - ypad);
		cairo_clip (cr);
		layout = gtk_widget_create_pango_layout (GTK_WIDGET (day_view), NULL);
		pango_layout_set_width (layout, (item_w - icon_x - 1) * PANGO_SCALE);
		pango_layout_set_wrap (layout, PANGO_WRAP_CHAR);

		if (main_item->priv->text)
			pango_layout_set_text (layout, main_item->priv->text, -1);
				cairo_move_to (cr,
			       icon_x,
			       2+ypad);
			
		pango_cairo_show_layout (cr, layout);

		cairo_stroke (cr);
		cairo_restore (cr);
		g_object_unref (layout);
	}


	if (font_options)
		cairo_font_options_destroy (font_options);

	g_free (text);
	g_object_unref (comp);
	cairo_destroy (cr);

	if (event->just_added) {
		event->just_added = FALSE;
		clutter_actor_animate ((ClutterActor *)main_item, CLUTTER_LINEAR,
					400, "opacity", 255, NULL);
	}
}


static void
day_view_clutter_event_item_draw (EDayViewClutterEventItem *main_item)
{
	if (!main_item->priv->long_event)
		return day_view_clutter_event_item_draw_normal (main_item);
	else
		return day_view_clutter_event_item_draw_long (main_item);

}

static gint
day_view_clutter_event_item_event (ClutterActor *item,
                            ClutterEvent *event)
{
	EDayViewClutterEventItem *event_item;

	event_item = E_DAY_VIEW_CLUTTER_EVENT_ITEM (item);

	switch (event->type) {
	case CLUTTER_BUTTON_PRESS:
		if (event->button.click_count > 1)
			return day_view_clutter_event_item_double_click (event_item, event);
		else
			return day_view_clutter_event_item_button_press (event_item, event);
	case CLUTTER_BUTTON_RELEASE:
		return day_view_clutter_event_item_button_release (event_item, event);
	case CLUTTER_MOTION:
		break;
	default:
		break;
	}

	return FALSE;
}

static void
day_view_clutter_event_item_class_init (EDayViewClutterEventItemClass *class)
{
	GObjectClass *object_class;
	MxBoxLayoutClass *item_class;

	parent_class = g_type_class_peek_parent (class);
	g_type_class_add_private (class, sizeof (EDayViewClutterEventItemPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = day_view_clutter_event_item_set_property;
	object_class->get_property = day_view_clutter_event_item_get_property;

	item_class = MX_BOX_LAYOUT_CLASS (class);
	//item_class->update = day_view_clutter_event_item_update;
	//item_class->draw = day_view_clutter_event_item_draw;
	//item_class->point = day_view_clutter_event_item_point;
	CLUTTER_ACTOR_CLASS(item_class)->event = day_view_clutter_event_item_event;

	g_object_class_install_property (
		object_class,
		PROP_EVENT_NUM,
		g_param_spec_int (
			"event-num",
			"Event Num",
			NULL,
			G_MININT,
			G_MAXINT,
			-1,
			G_PARAM_READWRITE));

	g_object_class_install_property (
		object_class,
		PROP_EVENT_NUM,
		g_param_spec_int (
			"day-num",
			"Day Num",
			NULL,
			G_MININT,
			G_MAXINT,
			-1,
			G_PARAM_READWRITE));
	g_object_class_install_property (
		object_class,
		PROP_TEXT,
		g_param_spec_string (
			"text",
			"Summry Text",
			NULL,
			NULL,
			G_PARAM_READWRITE));	
}

static void
day_view_clutter_event_item_init (EDayViewClutterEventItem *event_item)
{
	event_item->priv = E_DAY_VIEW_CLUTTER_EVENT_ITEM_GET_PRIVATE (event_item);

	event_item->priv->event_num = -1;
	event_item->priv->day_num = -1;
}

GType
e_day_view_clutter_event_item_get_type (void)
{
	static GType type = 0;

	if (G_UNLIKELY (type == 0)) {
		const GTypeInfo type_info = {
			sizeof (EDayViewClutterEventItemClass),
			(GBaseInitFunc) NULL,
			(GBaseFinalizeFunc) NULL,
			(GClassInitFunc) day_view_clutter_event_item_class_init,
			(GClassFinalizeFunc) NULL,
			NULL,  /* class_data */
			sizeof (EDayViewClutterEventItem),
			0,     /* n_preallocs */
			(GInstanceInitFunc) day_view_clutter_event_item_init,
			NULL   /* value_table */
		};

		type = g_type_register_static (
			MX_TYPE_BOX_LAYOUT, "EDayViewClutterEventItem",
			&type_info, 0);
	}

	return type;
}

gint
e_day_view_clutter_event_item_get_event_num (EDayViewClutterEventItem *event_item)
{
	g_return_val_if_fail (E_IS_WEEK_VIEW_CLUTTER_EVENT_ITEM (event_item), -1);

	return event_item->priv->event_num;
}

void
e_day_view_clutter_event_item_set_event_num (EDayViewClutterEventItem *event_item,
                                      gint event_num)
{
	g_return_if_fail (E_IS_WEEK_VIEW_CLUTTER_EVENT_ITEM (event_item));

	event_item->priv->event_num = event_num;
	day_view_clutter_event_item_draw (event_item);

	g_object_notify (G_OBJECT (event_item), "event-num");
}

const char *
e_day_view_clutter_event_item_get_text (EDayViewClutterEventItem *event_item)
{
	g_return_val_if_fail (E_IS_WEEK_VIEW_CLUTTER_EVENT_ITEM (event_item), NULL);

	return (const char *)event_item->priv->text;
}

void
e_day_view_clutter_event_item_set_text (EDayViewClutterEventItem *event_item,
				 	 const char *txt)
{
	g_return_if_fail (E_IS_WEEK_VIEW_CLUTTER_EVENT_ITEM (event_item));

	if (event_item->priv->text)
		g_free (event_item->priv->text);

	event_item->priv->text = g_strdup (txt); 
	day_view_clutter_event_item_draw (event_item);

	g_object_notify (G_OBJECT (event_item), "text");
}

gint
e_day_view_clutter_event_item_get_day_num (EDayViewClutterEventItem *event_item)
{
	g_return_val_if_fail (E_IS_WEEK_VIEW_CLUTTER_EVENT_ITEM (event_item), -1);

	return event_item->priv->day_num;
}

void
e_day_view_clutter_event_item_set_day_num (EDayViewClutterEventItem *event_item,
                                     gint day_num)
{
	g_return_if_fail (E_IS_WEEK_VIEW_CLUTTER_EVENT_ITEM (event_item));

	event_item->priv->day_num = day_num;
	day_view_clutter_event_item_draw (event_item);

	g_object_notify (G_OBJECT (event_item), "day-num");
}

static void
handle_activate (ClutterActor *actor, 
		 EDayViewClutterEventItem *item)
{
	gtk_widget_grab_focus ((GtkWidget *)item->priv->day_view);
	e_day_view_cancel_editing (item->priv->day_view);
	e_day_view_on_editing_stopped (item->priv->day_view, NULL, TRUE);	
}

static gboolean
handle_text_item_event (ClutterActor *actor, 	
			ClutterEvent *event,
			EDayViewClutterEventItem *item)
{
	EDayView *day_view = item->priv->day_view;

	switch (event->type) {

	case CLUTTER_BUTTON_PRESS:
		if (event->button.button == 3) {
			e_day_view_cancel_editing (item->priv->day_view);
			e_day_view_on_editing_stopped (item->priv->day_view, NULL, TRUE);	
			gtk_widget_grab_focus ((GtkWidget *)day_view);
			return FALSE;
		}
		return FALSE;
	case CLUTTER_KEY_PRESS:
		if (event->key.keyval == GDK_Escape) {
			e_day_view_cancel_editing (item->priv->day_view);
			if (e_day_view_on_editing_stopped (item->priv->day_view, NULL, FALSE)) {
				item->priv->day_view->editing_event_num = -1;
				item->priv->day_view->editing_event_day = -1;
			}
			gtk_widget_grab_focus ((GtkWidget *)day_view);
			return TRUE;
		}
		
		return FALSE;

	default:
		break;
	}

	return FALSE;
}

EDayViewClutterEventItem *
e_day_view_clutter_event_item_new (EDayView *view, gint day, gint event_num, gboolean long_event)
{
	EDayViewClutterEventItem *item = g_object_new (E_TYPE_DAY_VIEW_CLUTTER_EVENT_ITEM, NULL);
	MxBoxLayout *box = (MxBoxLayout *)item;

	//clutter_actor_set_name (item, "MonthEventTile");

	item->priv->long_event = long_event;
	item->priv->day_view = view;
	item->priv->texture = (ClutterCairoTexture *)clutter_cairo_texture_new (10, 10);
	item->priv->day_num = day;
	item->priv->event_num = event_num;

	clutter_actor_set_reactive ((ClutterActor *)item->priv->texture, TRUE);
	
	mx_box_layout_set_orientation (box, MX_ORIENTATION_VERTICAL);
	mx_box_layout_add_actor (box,
                               (ClutterActor *)item->priv->texture, -1);
	clutter_container_child_set (CLUTTER_CONTAINER (box),
                               (ClutterActor *)item->priv->texture,
			       "expand", TRUE,
			       "x-fill", TRUE,
			       "y-fill", TRUE,			       
                               NULL);
	clutter_actor_show_all ((ClutterActor *)box);
	clutter_actor_set_reactive ((ClutterActor *)box, TRUE);

	item->priv->text_item = clutter_text_new ();
	g_signal_connect (item->priv->text_item, "event", G_CALLBACK(handle_text_item_event), item);
	clutter_text_set_activatable ((ClutterText *)item->priv->text_item, TRUE);
	clutter_text_set_single_line_mode ((ClutterText *)item->priv->text_item, long_event ? TRUE: FALSE);
	if (!long_event)
		clutter_text_set_line_wrap_mode ((ClutterText *)item->priv->text_item, PANGO_WRAP_CHAR);
	g_signal_connect (item->priv->text_item, "activate", G_CALLBACK(handle_activate), item);
	clutter_text_set_line_wrap   ((ClutterText *)item->priv->text_item, !long_event ? TRUE: FALSE);
	clutter_text_set_editable ((ClutterText *)item->priv->text_item, TRUE);
	clutter_actor_set_reactive (item->priv->text_item, TRUE);
	clutter_actor_hide (item->priv->text_item);

	mx_box_layout_add_actor (box,
                               item->priv->text_item, -1);
	clutter_container_child_set (CLUTTER_CONTAINER (box),
                               item->priv->text_item,
			       "expand", FALSE,
			       "x-fill", FALSE,
			       "y-fill", FALSE,			       
                               NULL);

	
	return item;
}

void		
e_day_view_clutter_event_item_redraw  (EDayViewClutterEventItem *item)
{
	day_view_clutter_event_item_draw (item);
}

static void
reset_gravity (ClutterAnimation *amim, ClutterActor *item)
{
	float height=0, width=0;
	clutter_actor_get_size (item, &width, &height);
	
	clutter_actor_set_anchor_point (item, 0.0, 0.0);
	clutter_actor_move_by (item, 0, -height/2);
	clutter_actor_set_rotation (item,
                          CLUTTER_X_AXIS,  /* or CLUTTER_Y_AXIS */
                          0.0,             /* set the rotation to this angle */
                          0.0,
			  0.0,
                          0);
}


struct _anim_data {
	ClutterActor *item;
	void (*cb1) (gpointer);
	gpointer data1;
	void (*cb2) (gpointer);
	gpointer data2;
};

static void
rotate_xstage2 (ClutterAnimation *amim, struct _anim_data *data)
{
	float height=0, width=0;
	ClutterActor *item = data->item;

	clutter_actor_get_size (item, &width, &height);

	clutter_actor_set_anchor_point (item, 0.0, 0.0);
	clutter_actor_move_by (item, 0, -height/2);

	clutter_actor_set_rotation (data->item,
                          CLUTTER_X_AXIS,  /* or CLUTTER_Y_AXIS */
                          0.0,             /* set the rotation to this angle */
                          0.0,
			  0.0,
                          0.0);

	data->cb2 (data->data2);	
	
}

static void
rotate_xstage1 (ClutterAnimation *amim, struct _anim_data *data)
{
	data->cb1 (data->data1);

	clutter_actor_set_rotation (data->item,
                          CLUTTER_X_AXIS,  /* or CLUTTER_Y_AXIS */
                          270.0,             /* set the rotation to this angle */
                          0.0,
			  0.0,
                          0.0);

	clutter_actor_animate (data->item, CLUTTER_EASE_IN_SINE,
				200,
				"rotation-angle-x", 360.0,
				"signal-after::completed", rotate_xstage2, data,
				NULL);
	

}

static void
wvce_animate_rotate_x (ClutterActor *item,
		     void (*cb1) (gpointer),
		     gpointer data1,
		     void (*cb2) (gpointer),
		     gpointer data2)
		     
{
	float height=0, width=0;
	struct _anim_data *data= g_new0 (struct _anim_data, 1);

	data->item = item;
	data->cb1 = cb1;
	data->data1 = data1;
	data->cb2 = cb2;
	data->data2 = data2;

	clutter_actor_get_size (item, &width, &height);

	clutter_actor_set_anchor_point (item, 0, (float)height/2);
	clutter_actor_move_by (item, 0, height/2);

	clutter_actor_animate (item, CLUTTER_EASE_OUT_SINE,
				200,
				"rotation-angle-x", 90.0,
				"signal-after::completed", rotate_xstage1, data,
				NULL);
}

static void
rotate_ystage2 (ClutterAnimation *amim, struct _anim_data *data)
{
	float height=0, width=0;
	ClutterActor *item = data->item;

	clutter_actor_get_size (item, &width, &height);

	clutter_actor_set_anchor_point (item, 0.0, 0.0);
	clutter_actor_move_by (item, -width/2, 0);

	clutter_actor_set_rotation (data->item,
                          CLUTTER_Y_AXIS,  /* or CLUTTER_Y_AXIS */
                          0.0,             /* set the rotation to this angle */
                          0.0,
			  0.0,
                          0.0);

	data->cb2 (data->data2);	
	
}

static void
rotate_ystage1 (ClutterAnimation *amim, struct _anim_data *data)
{
	data->cb1 (data->data1);

	clutter_actor_set_rotation (data->item,
                          CLUTTER_Y_AXIS,  /* or CLUTTER_Y_AXIS */
                          270.0,             /* set the rotation to this angle */
                          0.0,
			  0.0,
                          0.0);

	clutter_actor_animate (data->item, CLUTTER_EASE_IN_SINE,
				200,
				"rotation-angle-y", 360.0,
				"signal-after::completed", rotate_ystage2, data,
				NULL);
	

}

static void
wvce_animate_rotate_y (ClutterActor *item,
		     void (*cb1) (gpointer),
		     gpointer data1,
		     void (*cb2) (gpointer),
		     gpointer data2)
		     
{
	float height=0, width=0;
	struct _anim_data *data= g_new0 (struct _anim_data, 1);

	data->item = item;
	data->cb1 = cb1;
	data->data1 = data1;
	data->cb2 = cb2;
	data->data2 = data2;

	clutter_actor_get_size (item, &width, &height);

	clutter_actor_set_anchor_point (item, (float)width/2, 0);
	clutter_actor_move_by (item, width/2, 0);

	clutter_actor_animate (item, CLUTTER_EASE_OUT_SINE,
				200,
				"rotation-angle-y", 90.0,
				"signal-after::completed", rotate_ystage1, data,
				NULL);
}

static void
wvce_set_view_editing_1 (gpointer gp)
{
	EDayViewClutterEventItem *item  = (EDayViewClutterEventItem *)gp;

	clutter_actor_destroy ((ClutterActor *)item->priv->texture);
	clutter_actor_show (item->priv->text_item);
}
static void
wvce_set_view_editing_2 (gpointer gp)
{
	EDayViewClutterEventItem *item  = (EDayViewClutterEventItem *)gp;

	clutter_grab_keyboard (item->priv->text_item);
	clutter_actor_grab_key_focus (item->priv->text_item);	
}

void 
e_day_view_clutter_event_item_switch_editing_mode (EDayViewClutterEventItem *item)
{
	guint w=0, h=0;
	
	clutter_cairo_texture_get_surface_size (item->priv->texture, &w, &h);

	clutter_text_set_text ((ClutterText *)item->priv->text_item, item->priv->text);
	clutter_grab_keyboard (item->priv->text_item);
	clutter_actor_grab_key_focus (item->priv->text_item);	
//	clutter_actor_hide (item->priv->texture);
//	clutter_actor_show (item->priv->text_item);

//	clutter_actor_set_size (item->priv->text_item, w, h);
//	clutter_actor_get_position (item->priv->text_item, &x, &y);
//	clutter_actor_set_clip              (item->priv->text_item,
//						x,y,
//						(float)w, (float)h);
	
	if (!item->priv->long_event && !item->priv->short_event)
		wvce_animate_rotate_y ((ClutterActor *)item, wvce_set_view_editing_1, item,
					wvce_set_view_editing_2, item);
	else
		wvce_animate_rotate_x ((ClutterActor *)item, wvce_set_view_editing_1, item,
					wvce_set_view_editing_2, item);


}

static void
scale_stage2 (ClutterAnimation *amim, struct _anim_data *data)
{

	data->cb2 (data->data2);	
	
}

static void
scale_stage1 (ClutterAnimation *amim, struct _anim_data *data)
{
	EDayViewClutterEventItem *eitem = (EDayViewClutterEventItem *)data->item;
	gboolean se = eitem->priv->long_event || eitem->priv->short_event;

	data->cb1 (data->data1);
	
	clutter_actor_animate (data->item, CLUTTER_EASE_IN_SINE,
				200,
				se ? "scale-y" : "scale-x", 1.0,
				"signal-after::completed", scale_stage2, data,
				NULL);
	

}

static void
wvce_animate_scale (ClutterActor *item,
		     void (*cb1) (gpointer),
		     gpointer data1,
		     void (*cb2) (gpointer),
		     gpointer data2)
		     
{
	float height=0, width=0;
	struct _anim_data *data= g_new0 (struct _anim_data, 1);
	EDayViewClutterEventItem *eitem = (EDayViewClutterEventItem *)item;
	gboolean se = eitem->priv->long_event || eitem->priv->short_event;
	data->item = item;
	data->cb1 = cb1;
	data->data1 = data1;
	data->cb2 = cb2;
	data->data2 = data2;

	clutter_actor_get_size (item, &width, &height);

	g_object_set (item, "scale-center-x", width/2, "scale-center-y", height/2, NULL);

	clutter_actor_animate (item, CLUTTER_EASE_OUT_SINE,
				200,
				se ? "scale-y" : "scale-x", 0.0,
				"signal-after::completed", scale_stage1, data,
				NULL);
}

static void
wvce_set_view_normal_1 (gpointer gp)
{
	EDayViewClutterEventItem *item  = (EDayViewClutterEventItem *)gp;

	clutter_actor_hide (item->priv->text_item);
	clutter_actor_show ((ClutterActor *)item->priv->texture);	
}
static void
wvce_set_view_normal_2 (gpointer gp)
{
	//EDayViewClutterEventItem *item  = (EDayViewClutterEventItem *)gp;

	/* Do nothing */
}

void 
e_day_view_clutter_event_item_switch_normal_mode (EDayViewClutterEventItem *item)
{
	//clutter_actor_hide (item->priv->text_item);
	//clutter_actor_show (item->priv->texture);	

	wvce_animate_scale ((ClutterActor *)item, wvce_set_view_normal_1, item,
				wvce_set_view_normal_2, item);

}

void 
e_day_view_clutter_event_item_switch_viewing_mode (EDayViewClutterEventItem *item)
{
}

const char *
e_day_view_clutter_event_item_get_edit_text (EDayViewClutterEventItem *item)
{
	return clutter_text_get_text ((ClutterText *)item->priv->text_item);
}


static void
scale_delete_stage1 (ClutterAnimation *amim, ClutterActor *item)
{
	clutter_actor_animate (item, CLUTTER_EASE_IN_SINE,
				200,
				"scale-x", 0.1,
				"signal-swapped-after::completed", clutter_actor_destroy, item,
				NULL);
	

}

static void
wvce_animate_scale_delete (ClutterActor *item)
{
	float height=0, width=0;

	clutter_actor_get_size (item, &width, &height);

	g_object_set (item, "scale-center-x", width/2, "scale-center-y", height/2, NULL);

	clutter_actor_animate ((ClutterActor *)item, CLUTTER_EASE_OUT_SINE,
				200,
				"scale-y", 0.1,
				"signal-after::completed", scale_delete_stage1, item,
				NULL);
}

void
e_day_view_clutter_event_item_scale_destroy (EDayViewClutterEventItem *item)
{
	wvce_animate_scale_delete ((ClutterActor *)item);
}


void
e_day_view_clutter_event_item_fade_destroy (EDayViewClutterEventItem *item)
{
	clutter_actor_animate ((ClutterActor *)item, CLUTTER_EASE_OUT_SINE, 200,
				"opacity", 0,
				"signal-swapped-after::completed", clutter_actor_destroy, item,
				NULL);	
}

