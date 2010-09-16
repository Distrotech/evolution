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
 * EWeekViewClutterEventItem - displays the background, times and icons for an event
 * in the week/month views. A separate EText canvas item is used to display &
 * edit the text.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "e-util/e-categories-config.h"
#include "e-week-view-clutter-event-item.h"

#include <gtk/gtk.h>
#include "e-calendar-view.h"
#include "calendar-config.h"
#include "comp-util.h"

#include <text/e-text.h>

#include "e-util/gtk-compat.h"

#define E_WEEK_VIEW_CLUTTER_EVENT_ITEM_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_WEEK_VIEW_CLUTTER_EVENT_ITEM, EWeekViewClutterEventItemPrivate))

struct _EWeekViewClutterEventItemPrivate {
	/* The event index in the EWeekView events array. */
	gint event_num;

	/* The span index within the event. */
	gint span_num;

	/* Text */
	char *text;

	/* Texture */
	ClutterCairoTexture *texture;

	/* Week View*/
	EWeekView *week_view;

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
	PROP_SPAN_NUM,
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

static ECalendarViewPosition
week_view_clutter_event_item_get_position (EWeekViewClutterEventItem *event_item,
                                   gdouble x,
                                   gdouble y)
{
	EWeekView *week_view;
	GnomeCanvasItem *item;

	week_view = event_item->priv->week_view;

	if (x < event_item->priv->x1 + E_WEEK_VIEW_EVENT_L_PAD
	    || x >= event_item->priv->x2 - E_WEEK_VIEW_EVENT_R_PAD)
		return E_CALENDAR_VIEW_POS_NONE;

	/* Support left/right edge for long events only. */
	if (!e_week_view_is_one_day_event (week_view, event_item->priv->event_num)) {
		if (x < event_item->priv->x1 + E_WEEK_VIEW_EVENT_L_PAD
		    + E_WEEK_VIEW_EVENT_BORDER_WIDTH
		    + E_WEEK_VIEW_EVENT_EDGE_X_PAD)
			return E_CALENDAR_VIEW_POS_LEFT_EDGE;

		if (x >= event_item->priv->x2 + 1 - E_WEEK_VIEW_EVENT_R_PAD
		    - E_WEEK_VIEW_EVENT_BORDER_WIDTH
		    - E_WEEK_VIEW_EVENT_EDGE_X_PAD)
			return E_CALENDAR_VIEW_POS_RIGHT_EDGE;
	}

	return E_CALENDAR_VIEW_POS_EVENT;
}

static gboolean
week_view_clutter_event_item_double_click (EWeekViewClutterEventItem *event_item,
                                   ClutterEvent *bevent)
{
	EWeekView *week_view;
	EWeekViewEvent *event;
	GnomeCanvasItem *item;

	week_view = event_item->priv->week_view;

	if (!is_array_index_in_bounds (week_view->events, event_item->priv->event_num))
		return TRUE;

	event = &g_array_index (
		week_view->events, EWeekViewEvent,
		event_item->priv->event_num);

	if (!is_comp_data_valid (event))
		return TRUE;

	if (week_view->editing_event_num >= 0) {
		EWeekViewEvent *editing;

		if (!is_array_index_in_bounds (week_view->events, week_view->editing_event_num))
			return TRUE;

		editing = &g_array_index (
			week_view->events, EWeekViewEvent,
			week_view->editing_event_num);

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

	e_week_view_stop_editing_event (week_view);

	e_calendar_view_edit_appointment (
		E_CALENDAR_VIEW (week_view),
		event->comp_data->client,
		event->comp_data->icalcomp, FALSE);

	return TRUE;
}

gboolean
week_view_clutter_event_item_button_press (EWeekViewClutterEventItem *event_item,
                                   ClutterEvent *bevent)
{
	EWeekView *week_view;
	ECalendarViewPosition pos;
	EWeekViewEvent *event;
	EWeekViewEventSpan *span;

	week_view = event_item->priv->week_view;

	if (!is_array_index_in_bounds (week_view->events, event_item->priv->event_num))
		return FALSE;

	event = &g_array_index (
		week_view->events, EWeekViewEvent,
		event_item->priv->event_num);

	if (!is_array_index_in_bounds (
		week_view->spans, event->spans_index +
		event_item->priv->span_num))
		return FALSE;

	span = &g_array_index (week_view->spans, EWeekViewEventSpan,
			       event->spans_index + event_item->priv->span_num);

	pos = week_view_clutter_event_item_get_position (event_item, (gdouble)(bevent->button.x-(float)event_item->priv->spanx),
						   (gdouble)(bevent->button.y - (float)event_item->priv->spany) );

	if (pos == E_CALENDAR_VIEW_POS_NONE)
		return FALSE;
	
	if (bevent->button.button == 1) {
		week_view->pressed_event_num = event_item->priv->event_num;
		week_view->pressed_span_num = event_item->priv->span_num;

		/* Ignore clicks on the event while editing. */
		if (span->text_item && E_TEXT (span->text_item)->editing)
			return FALSE;

		/* Remember the item clicked and the mouse position,
		   so we can start a drag if the mouse moves. */
		week_view->drag_event_x = (gint)bevent->button.x;
		week_view->drag_event_y = (gint)bevent->button.y;

		/* FIXME: Remember the day offset from the start of the event.
		 */

		return TRUE;
	} else if (bevent->button.button == 3) {
		GdkEventButton *gevent = (GdkEventButton *)gdk_event_new (GDK_BUTTON_PRESS);

		if (!gtk_widget_has_focus (GTK_WIDGET (week_view)) && 0) {
			gtk_widget_grab_focus (GTK_WIDGET (week_view));
			if (week_view->event_destroyed) {
				week_view->event_destroyed = FALSE;
				return FALSE;
			}

		}

		gevent->time = bevent->button.time;
		gevent->button = bevent->button.button;

		e_week_view_set_selected_time_range_visible (
			week_view, event->start, event->end);

		e_week_view_show_popup_menu (
			week_view, (GdkEventButton*) gevent,
			event_item->priv->event_num);
		gdk_event_free (gevent);
		//g_signal_stop_emission_by_name (
		//	event_item->canvas, "button_press_event");

		return TRUE;
	}

	return FALSE;
}

static gboolean
week_view_clutter_event_item_button_release (EWeekViewClutterEventItem *event_item,
                                     ClutterEvent *event)
{
	EWeekView *week_view;


	week_view = event_item->priv->week_view;

	if (week_view->pressed_event_num != -1
	    && week_view->pressed_event_num == event_item->priv->event_num
	    && week_view->pressed_span_num == event_item->priv->span_num) {
		if (week_view->editing_event_num != -1) {
			e_week_view_cancel_editing (event_item->priv->week_view);
			e_week_view_on_editing_stopped (event_item->priv->week_view, NULL, TRUE);
		}
		e_week_view_start_editing_event (week_view,
						 event_item->priv->event_num,
						 event_item->priv->span_num,
						 NULL);
		week_view->pressed_event_num = -1;

		week_view->editing_event_num = event_item->priv->event_num;
		week_view->editing_span_num = event_item->priv->span_num;

		return TRUE;
	}

	week_view->pressed_event_num = -1;

	return FALSE;
}

static void
week_view_draw_time (EWeekView *week_view,
                     cairo_t *cr,
		     gint time_x,
                     gint time_y,
                     gint hour,
                     gint minute)
{
	ECalModel *model;
	GtkStyle *style;
	gint hour_to_display, suffix_width;
	gint time_y_normal_font, time_y_small_font;
	const gchar *suffix;
	gchar buffer[128];
	GdkColor *fg;
	PangoLayout *layout;
	PangoFontDescription *small_font_desc;

	model = e_calendar_view_get_model (E_CALENDAR_VIEW (week_view));

	style = gtk_widget_get_style (GTK_WIDGET (week_view));
	small_font_desc = week_view->small_font_desc;

	fg = &week_view->colors[E_WEEK_VIEW_COLOR_EVENT_TEXT];

	layout = gtk_widget_create_pango_layout (GTK_WIDGET (week_view), NULL);

	time_y_normal_font = time_y_small_font = time_y;
	if (small_font_desc)
		time_y_small_font = time_y;

	e_week_view_convert_time_to_display (week_view, hour, &hour_to_display,
					     &suffix, &suffix_width);

	if (week_view->use_small_font && week_view->small_font_desc) {
		g_snprintf (buffer, sizeof (buffer), "%2i:%02i",
			    hour_to_display, minute);


		cairo_save (cr);
		gdk_cairo_set_source_color (cr, &fg);
		/* Draw the hour. */
		if (hour_to_display < 10) {
			pango_layout_set_text (layout, buffer + 1, 1);
			cairo_move_to (cr,
				       time_x + week_view->digit_width,
				       time_y_normal_font);
			pango_cairo_show_layout (cr, layout);
		} else {
			pango_layout_set_text (layout, buffer, 2);
			cairo_move_to (cr,			
				       time_x,
				       time_y_normal_font);
			pango_cairo_show_layout (cr, layout);			
		}
		cairo_stroke (cr);
		cairo_restore (cr);

		time_x += week_view->digit_width * 2;

		cairo_save (cr);
		gdk_cairo_set_source_color (cr, &fg);
		/* Draw the start minute, in the small font. */
		pango_layout_set_font_description (layout, week_view->small_font_desc);
		pango_layout_set_text (layout, buffer + 3, 2);
		cairo_move_to (cr,
			       time_x,
			       time_y_small_font);
		pango_cairo_show_layout (cr, layout);			
		cairo_stroke (cr);
		cairo_restore (cr);


		cairo_save (cr);
		gdk_cairo_set_source_color (cr, &fg);
		pango_layout_set_font_description (layout, style->font_desc);

		time_x += week_view->small_digit_width * 2;

		/* Draw the 'am'/'pm' suffix, if 12-hour format. */
		if (!e_cal_model_get_use_24_hour_format (model)) {
			pango_layout_set_text (layout, suffix, -1);
			cairo_move_to (cr,
				       time_x,
				       time_y_normal_font);
			pango_cairo_show_layout (cr, layout);					
		}
		cairo_stroke (cr);
		cairo_restore (cr);

	} else {

		cairo_save (cr);
		gdk_cairo_set_source_color (cr, &fg);
		/* Draw the start time in one go. */
		g_snprintf (buffer, sizeof (buffer), "%2i:%02i%s",
			    hour_to_display, minute, suffix);
		if (hour_to_display < 10) {
			pango_layout_set_text (layout, buffer + 1, -1);
			cairo_move_to (cr,		
				       time_x + week_view->digit_width,
				       time_y_normal_font);
			pango_cairo_show_layout (cr, layout);					
		} else {
			pango_layout_set_text (layout, buffer, -1);
			cairo_move_to (cr,					
				       time_x,
				       time_y_normal_font);
			pango_cairo_show_layout (cr, layout);					
		}
		cairo_stroke (cr);
		cairo_restore (cr);

	}
	g_object_unref (layout);
}

static gint
week_view_clutter_event_item_draw_icons (EWeekViewClutterEventItem *event_item,
                                 cairo_t *cr,
                                 gint icon_x,
                                 gint icon_y,
                                 gint x2,
                                 gboolean right_align,
                                 GdkRegion *draw_region)
{
	EWeekView *week_view;
	EWeekViewEvent *event;
	ECalComponent *comp;
	GnomeCanvas *canvas;
	gint num_icons = 0, icon_x_inc;
	gboolean draw_reminder_icon = FALSE, draw_recurrence_icon = FALSE;
	gboolean draw_timezone_icon = FALSE, draw_attach_icon = FALSE;
	gboolean draw_meeting_icon = FALSE;
	GSList *categories_pixbufs = NULL, *pixbufs;

	week_view = event_item->priv->week_view;

	if (!is_array_index_in_bounds (week_view->events, event_item->priv->event_num))
		return icon_x;

	event = &g_array_index (week_view->events, EWeekViewEvent,
				event_item->priv->event_num);

	if (!is_comp_data_valid (event))
		return icon_x;

	comp = e_cal_component_new ();
	e_cal_component_set_icalcomponent (
		comp, icalcomponent_new_clone (event->comp_data->icalcomp));

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

	if (e_cal_component_has_attendees (comp)) {
		draw_meeting_icon = TRUE;
		num_icons++;
	}

	if (event->different_timezone) {
		draw_timezone_icon = TRUE;
		num_icons++;
	}

	num_icons += cal_comp_util_get_n_icons (comp, &categories_pixbufs);

	icon_x_inc = E_WEEK_VIEW_ICON_WIDTH + E_WEEK_VIEW_ICON_X_PAD;

	if (right_align)
		icon_x -= icon_x_inc * num_icons;

	#define draw_pixbuf(pf)							\
		if (can_draw_in_region (draw_region, icon_x, icon_y,		\
		    E_WEEK_VIEW_ICON_WIDTH, E_WEEK_VIEW_ICON_HEIGHT)) {		\
			cairo_save (cr);					\
			gdk_cairo_set_source_pixbuf (cr, pf, icon_x, icon_y);	\
			cairo_paint (cr);					\
			cairo_restore (cr);					\
		}								\
										\
		icon_x += icon_x_inc;

	if (draw_reminder_icon && icon_x + E_WEEK_VIEW_ICON_WIDTH <= x2) {
		draw_pixbuf (week_view->reminder_icon);
	}

	if (draw_attach_icon && icon_x + E_WEEK_VIEW_ICON_WIDTH <= x2) {
		draw_pixbuf (week_view->attach_icon);
	}

	if (draw_recurrence_icon && icon_x + E_WEEK_VIEW_ICON_WIDTH <= x2) {
		draw_pixbuf (week_view->recurrence_icon);
	}

	if (draw_timezone_icon && icon_x + E_WEEK_VIEW_ICON_WIDTH <= x2) {
		draw_pixbuf (week_view->timezone_icon);
	}

	if (draw_meeting_icon && icon_x + E_WEEK_VIEW_ICON_WIDTH <= x2) {
		draw_pixbuf (week_view->meeting_icon);
	}

	/* draw categories icons */
	for (pixbufs = categories_pixbufs;
	     pixbufs;
	     pixbufs = pixbufs->next) {
		GdkPixbuf *pixbuf = pixbufs->data;

		draw_pixbuf (pixbuf);
	}

	#undef draw_pixbuf

	g_slist_foreach (categories_pixbufs, (GFunc)g_object_unref, NULL);
	g_slist_free (categories_pixbufs);

	g_object_unref(comp);

	return num_icons ? icon_x : -1;
}

/* This draws a little triangle to indicate that an event extends past
   the days visible on screen. */
static void
week_view_clutter_event_item_draw_triangle (EWeekViewClutterEventItem *event_item,
                                    cairo_t *cr,
                                    GdkColor bg_color,
                                    gint x,
                                    gint y,
                                    gint w,
                                    gint h,
                                    GdkRegion *draw_region)
{
	ECalModel *model;
	EWeekView *week_view;
	EWeekViewEvent *event;
	GdkPoint points[3];
	const gchar *color_spec;
	gint c1, c2;

	if (!can_draw_in_region (draw_region, x, y, w, h))
		return;

	week_view = event_item->priv->week_view;

	if (!is_array_index_in_bounds (week_view->events, event_item->priv->event_num))
		return;

	event = &g_array_index (week_view->events, EWeekViewEvent,
				event_item->priv->event_num);

	if (!is_comp_data_valid (event))
		return;

	points[0].x = x;
	points[0].y = y;
	points[1].x = x + w;
	points[1].y = y + (h / 2);
	points[2].x = x;
	points[2].y = y + h - 1;

	model = e_calendar_view_get_model (E_CALENDAR_VIEW (week_view));

	color_spec =
		e_cal_model_get_color_for_component (model, event->comp_data);

	if (gdk_color_parse (color_spec, &bg_color)) {
		GdkColormap *colormap;

		colormap = gtk_widget_get_colormap (GTK_WIDGET (week_view));
		if (gdk_colormap_alloc_color (colormap, &bg_color, TRUE, TRUE)) {
			gdk_cairo_set_source_color (cr, &bg_color);
		} else {
			EWeekViewColors wvc;
			GdkColor *color;

			wvc = E_WEEK_VIEW_COLOR_EVENT_BACKGROUND;
			color = &week_view->colors[wvc];

			gdk_cairo_set_source_color (cr, color);
		}
	} else {
		EWeekViewColors wvc;
		GdkColor *color;

		wvc = E_WEEK_VIEW_COLOR_EVENT_BACKGROUND;
		color = &week_view->colors[wvc];

		gdk_cairo_set_source_color (cr, color);
	}

	cairo_save (cr);
	cairo_set_line_width (cr, 0.7);
	cairo_move_to (cr, points[0].x, points[0].y);
	cairo_line_to (cr, points[1].x, points[1].y);
	cairo_line_to (cr, points[2].x, points[2].y);
	cairo_line_to (cr, points[0].x, points[0].y);
	cairo_fill (cr);
	cairo_restore (cr);

	cairo_save (cr);
	gdk_cairo_set_source_color (cr, &week_view->colors[E_WEEK_VIEW_COLOR_EVENT_BORDER]);

	/* If the height is odd we can use the same central point for both
	   lines. If it is even we use different end-points. */
	c1 = c2 = y + (h / 2);
	if (h % 2 == 0)
		c1--;

	cairo_set_line_width (cr, 0.7);
	cairo_move_to (cr, x, y);
	cairo_line_to (cr, x + w, c1);
	cairo_move_to (cr, x, y + h - 1);
	cairo_line_to (cr, x + w, c2);
	cairo_restore (cr);

}

static void
week_view_clutter_event_item_set_property (GObject *object,
                                   guint property_id,
                                   const GValue *value,
                                   GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_EVENT_NUM:
			e_week_view_clutter_event_item_set_event_num (
				E_WEEK_VIEW_CLUTTER_EVENT_ITEM (object),
				g_value_get_int (value));
			return;

		case PROP_SPAN_NUM:
			e_week_view_clutter_event_item_set_span_num (
				E_WEEK_VIEW_CLUTTER_EVENT_ITEM (object),
				g_value_get_int (value));
			return;
		case PROP_TEXT:
			e_week_view_clutter_event_item_set_text (
				E_WEEK_VIEW_CLUTTER_EVENT_ITEM (object),
				g_value_get_string (value));			
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
week_view_clutter_event_item_get_property (GObject *object,
                                   guint property_id,
                                   GValue *value,
                                   GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_EVENT_NUM:
			g_value_set_int (
				value,
				e_week_view_clutter_event_item_get_event_num (
				E_WEEK_VIEW_CLUTTER_EVENT_ITEM (object)));
			return;

		case PROP_SPAN_NUM:
			g_value_set_int (
				value,
				e_week_view_clutter_event_item_get_span_num (
				E_WEEK_VIEW_CLUTTER_EVENT_ITEM (object)));
			return;
		case PROP_TEXT:
			g_value_set_string (
				value,
				e_week_view_clutter_event_item_get_text (
				E_WEEK_VIEW_CLUTTER_EVENT_ITEM (object)));			
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
week_view_clutter_event_item_draw (EWeekViewClutterEventItem *canvas_item)
{
	EWeekViewClutterEventItem *event_item;
	EWeekView *week_view;
	EWeekViewEvent *event;
	EWeekViewEventSpan *span;
	ECalModel *model;
	GdkGC *gc;
	gint x1, y1, x2, y2, time_x, time_y;
	gint icon_x, icon_y, time_width, min_end_time_x, max_icon_x;
	gint rect_x, rect_w, rect_x2 = 0;
	gboolean one_day_event, editing_span = FALSE;
	gint start_hour, start_minute, end_hour, end_minute;
	gboolean draw_start, draw_end;
	gboolean draw_start_triangle = FALSE, draw_end_triangle = FALSE;
	GdkRectangle clip_rect;
	GdkColor bg_color;
	cairo_t *cr;
	cairo_pattern_t *pat;
	guint16 red, green, blue;
	gdouble radius, cx0, cy0, rect_height, rect_width;
	gboolean gradient;
	gdouble cc = 65535.0;
	GdkRegion *draw_region;
	GdkRectangle rect;
	const gchar *color_spec;
	int x=0,y=0;
	int width, height;
	gint span_x, span_y, span_w;

	event_item = E_WEEK_VIEW_CLUTTER_EVENT_ITEM (canvas_item);

	week_view = event_item->priv->week_view;

	if (event_item->priv->event_num == -1 || event_item->priv->span_num == -1)
		return;

	g_return_if_fail (event_item->priv->event_num < week_view->events->len);

	if (!is_array_index_in_bounds (week_view->events, event_item->priv->event_num))
		return;

	event = &g_array_index (week_view->events, EWeekViewEvent,
				event_item->priv->event_num);

	if (!is_comp_data_valid (event))
		return;

	g_return_if_fail (
		event->spans_index + event_item->priv->span_num <
		week_view->spans->len);

	if (!is_array_index_in_bounds (
		week_view->spans, event->spans_index +
		event_item->priv->span_num))
		return;

	span = &g_array_index (
		week_view->spans, EWeekViewEventSpan,
		event->spans_index + event_item->priv->span_num);

	if (e_week_view_get_span_position (
		week_view, event_item->priv->event_num, event_item->priv->span_num,
		&span_x, &span_y, &span_w)) {
		x1 = 0;
		y1 = 0;
		x2 = span_w - 1;
		y2 = week_view->row_height - 1;
	} 
	
	event_item->priv->x1 = 0;
	event_item->priv->y1 = 0;
	event_item->priv->x2 = span_w;
	event_item->priv->y2 = week_view->row_height;
	event_item->priv->spanx = span_x;
	event_item->priv->spany = span_y;
	gc = week_view->main_gc;

	if (x1 == x2 || y1 == y2)
		return;

	rect.x = 0;
	rect.y = 0;
	rect.width = x2-x1;
	rect.height = y2-y1;
	draw_region = gdk_region_rectangle (&rect);
	x1 = 0; y1 = 0;
	x2 = rect.width;
	clutter_cairo_texture_set_surface_size (event_item->priv->texture, rect.width, rect.height);
	clutter_actor_set_size (event_item->priv->text_item, rect.width, rect.height);
	clutter_actor_set_position (event_item, span_x, span_y);
	if (!can_draw_in_region (draw_region, x1, y1, x2 - x1, y2 - y1)) {
		gdk_region_destroy (draw_region);
		return;
	}

	clutter_cairo_texture_clear (event_item->priv->texture);	
	cr = clutter_cairo_texture_create (event_item->priv->texture);
	gradient = calendar_config_get_display_events_gradient ();

	icon_y = y1 + E_WEEK_VIEW_EVENT_BORDER_HEIGHT + E_WEEK_VIEW_ICON_Y_PAD;

	/* Get the start & end times in 24-hour format. */
	start_hour = event->start_minute / 60;
	start_minute = event->start_minute % 60;

	/* Modulo 24 because a midnight end time will be '24' */
	end_hour = (event->end_minute / 60) % 24;
	end_minute = event->end_minute % 60;

	time_y = y1 + E_WEEK_VIEW_EVENT_BORDER_HEIGHT
		+ E_WEEK_VIEW_EVENT_TEXT_Y_PAD;

	time_width = e_week_view_get_time_string_width (week_view);

	one_day_event = e_week_view_is_one_day_event (week_view, event_item->priv->event_num);

	model = e_calendar_view_get_model (E_CALENDAR_VIEW (week_view));

	color_spec =
		e_cal_model_get_color_for_component (model, event->comp_data);

	bg_color = week_view->colors[E_WEEK_VIEW_COLOR_EVENT_BACKGROUND];
	if (gdk_color_parse (color_spec, &bg_color)) {
		GdkColormap *colormap;

		colormap = gtk_widget_get_colormap (GTK_WIDGET (week_view));
		if (!gdk_colormap_alloc_color (colormap, &bg_color, TRUE, TRUE)) {
			bg_color = week_view->colors[E_WEEK_VIEW_COLOR_EVENT_BACKGROUND];
		}
	}

	red = bg_color.red;
	green = bg_color.green;
	blue = bg_color.blue;

	if (one_day_event) {
		gint val;

		time_x = x1 + E_WEEK_VIEW_EVENT_L_PAD + 1;
		rect_x = x1 + E_WEEK_VIEW_EVENT_L_PAD;
		rect_w = x2 - x1 - E_WEEK_VIEW_EVENT_L_PAD - E_WEEK_VIEW_EVENT_R_PAD + 1;

		/* Here we draw the border around the event*/
		cx0	   = rect_x;
		cy0	   = y1 + 1;
		rect_width  = rect_w;
		rect_height = y2 - y1 - 1;

		radius = 12;

		if (can_draw_in_region (draw_region, cx0, cy0, rect_width, rect_height)) {
			cairo_save (cr);
			draw_curved_rectangle (cr, cx0, cy0, rect_width, rect_height, radius);
			cairo_set_line_width (cr, 2.0);
			cairo_set_source_rgb (cr, red/cc, green/cc, blue/cc);
			cairo_stroke (cr);
			cairo_restore (cr);
		}

		/* Fill it in the Event */

		cx0	   = rect_x + 1.5;
		cy0	   = y1 + 2.75;
		rect_width  = rect_w - 3.;
		rect_height = y2 - y1 - 4.5;

		radius = 8;

		if (can_draw_in_region (draw_region, cx0, cy0, rect_width, rect_height)) {
			cairo_save (cr);
			draw_curved_rectangle (cr, cx0, cy0, rect_width, rect_height, radius);

			if (gradient) {
				pat = cairo_pattern_create_linear (rect_x + 2, y1 + 1, rect_x + 2, y2 - 7.25);
				cairo_pattern_add_color_stop_rgba (pat, 1, red/cc, green/cc, blue/cc, 0.8);
				cairo_pattern_add_color_stop_rgba (pat, 0, red/cc, green/cc, blue/cc, 0.4);
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
		}

		/* Draw the start and end times, as required. */
		switch (week_view->time_format) {
		case E_WEEK_VIEW_TIME_BOTH_SMALL_MIN:
		case E_WEEK_VIEW_TIME_BOTH:
			draw_start = TRUE;
			draw_end = TRUE;
			break;

		case E_WEEK_VIEW_TIME_START_SMALL_MIN:
		case E_WEEK_VIEW_TIME_START:
			draw_start = TRUE;
			draw_end = FALSE;
			break;

		case E_WEEK_VIEW_TIME_NONE:
			draw_start = FALSE;
			draw_end = FALSE;
			break;
		default:
			g_return_if_reached ();
			draw_start = FALSE;
			draw_end = FALSE;
			break;
		}

		if (draw_start) {
			week_view_draw_time (
				week_view, cr, time_x,
				time_y, start_hour, start_minute);
			time_x += time_width;
		}

		if (draw_end) {
			time_x += E_WEEK_VIEW_EVENT_TIME_SPACING;
			week_view_draw_time (
				week_view, cr, time_x,
				time_y, end_hour, end_minute);
			time_x += time_width;
		}

		icon_x = time_x;
		if (draw_start)
			icon_x += E_WEEK_VIEW_EVENT_TIME_X_PAD;

		/* Draw the icons. */
		val = week_view_clutter_event_item_draw_icons (
			event_item, cr, icon_x,
			icon_y, x2, FALSE, draw_region);
		icon_x = (val == -1) ? icon_x : val;

		/* Draw text */
		if (icon_x < x2) {
			PangoLayout *layout;
			GdkColor col = e_week_view_get_text_color (week_view, event, week_view);
			
			cairo_save (cr);
			gdk_cairo_set_source_color (cr, &col);
			
			icon_x += E_WEEK_VIEW_EVENT_TIME_X_PAD;

			cairo_rectangle (cr, icon_x , 0, x2-icon_x, week_view->row_height);
			cairo_clip (cr);
			layout = gtk_widget_create_pango_layout (GTK_WIDGET (week_view), NULL);
			if (event_item->priv->text)
				pango_layout_set_text (layout, event_item->priv->text, -1);
			cairo_move_to (cr,
				       icon_x, 
				       E_WEEK_VIEW_EVENT_BORDER_HEIGHT + E_WEEK_VIEW_ICON_Y_PAD);
			
			pango_cairo_show_layout (cr, layout);

			cairo_stroke (cr);
			cairo_restore (cr);
			g_object_unref (layout);
		}

		
	} else {
		rect_x = x1 + E_WEEK_VIEW_EVENT_L_PAD;
		rect_w = x2 - x1 - E_WEEK_VIEW_EVENT_L_PAD
			- E_WEEK_VIEW_EVENT_R_PAD + 1;

		/* Draw the triangles at the start & end, if needed.
		   They also use the first few pixels at the edge of the
		   event so we update rect_x & rect_w so we don't draw over
		   them. */
		if (event->start < week_view->day_starts[span->start_day]) {
			draw_start_triangle = TRUE;
			rect_x += 2;
			rect_w -= 2;
		}

		if (event->end > week_view->day_starts[span->start_day
						      + span->num_days]) {
			draw_end_triangle = TRUE;
			rect_w -= 2;
		}

		/* Here we draw the border around the event */

		cx0	   = rect_x;
		cy0	   = y1 + 1;
		rect_width  = rect_w;
		rect_height = y2 - y1 - 1;

		radius = 12;

		if (can_draw_in_region (draw_region, cx0, cy0, rect_width, rect_height)) {
			cairo_save (cr);
			draw_curved_rectangle (cr, cx0, cy0, rect_width, rect_height, radius);
			cairo_set_line_width (cr, 2.0);
			cairo_set_source_rgb (cr, red/cc, green/cc, blue/cc);
			cairo_stroke (cr);
			cairo_restore (cr);
		}

		/* Here we fill it in the event*/

		cx0	   = rect_x + 1.5;
		cy0	   = y1 + 2.75;
		rect_width  = rect_w - 3.;
		rect_height = y2 - y1 - 4.5;

		radius = 8;

		if (can_draw_in_region (draw_region, cx0, cy0, rect_width, rect_height)) {
			cairo_save (cr);
			draw_curved_rectangle (cr, cx0, cy0, rect_width, rect_height, radius);

			if (gradient) {
				pat = cairo_pattern_create_linear (rect_x + 2, y1 + 1, rect_x + 2, y2 - 7.25);
				cairo_pattern_add_color_stop_rgba (pat, 1, red/cc, green/cc, blue/cc, 0.8);
				cairo_pattern_add_color_stop_rgba (pat, 0, red/cc, green/cc, blue/cc, 0.4);
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
		}

		if (draw_start_triangle) {
			week_view_clutter_event_item_draw_triangle (
				event_item, cr, bg_color,
				x1 + E_WEEK_VIEW_EVENT_L_PAD + 2,
				y1, -3, y2 - y1 + 1, draw_region);
		} else if (can_draw_in_region (draw_region, rect_x, y1, 1, y2 - y1)) {
			EWeekViewColors wvc;
			GdkColor *color;

			wvc = E_WEEK_VIEW_COLOR_EVENT_BORDER;
			color = &week_view->colors[wvc];

			cairo_save (cr);
			gdk_cairo_set_source_color (cr, color);
			cairo_set_line_width (cr, 0.7);
			cairo_move_to (cr, rect_x, y1);
			cairo_line_to (cr, rect_x, y2);
			cairo_stroke (cr);
			cairo_restore (cr);
		}

		if (draw_end_triangle) {
			week_view_clutter_event_item_draw_triangle (
				event_item, cr, bg_color,
				x2 - E_WEEK_VIEW_EVENT_R_PAD - 2,
				y1, 3, y2 - y1 + 1, draw_region);
		} else if (can_draw_in_region (draw_region, rect_x2, y2, 1, 1)) {
			EWeekViewColors wvc;
			GdkColor *color;

			wvc = E_WEEK_VIEW_COLOR_EVENT_BORDER;
			color = &week_view->colors[wvc];

			cairo_save (cr);
			gdk_cairo_set_source_color (cr, color);
			cairo_set_line_width (cr, 0.7);
			/* rect_x2 is used uninitialized here */
			cairo_move_to (cr, rect_x2, y2);
			cairo_line_to (cr, rect_x2, y2);
			cairo_stroke (cr);
			cairo_restore (cr);
		}

		if (span->text_item && E_TEXT (span->text_item)->editing)
			editing_span = TRUE;

		/* Draw the start & end times, if they are not on day
		   boundaries. The start time would always be shown if it was
		   needed, though it may be clipped as the window shrinks.
		   The end time is only displayed if there is enough room.
		   We calculate the minimum position for the end time, which
		   depends on whether the start time is displayed. If the end
		   time doesn't fit, then we don't draw it. */
		min_end_time_x = x1 + E_WEEK_VIEW_EVENT_L_PAD
			+ E_WEEK_VIEW_EVENT_BORDER_WIDTH
			+ E_WEEK_VIEW_EVENT_EDGE_X_PAD;
		time_x = x1 + E_WEEK_VIEW_EVENT_L_PAD
				+ E_WEEK_VIEW_EVENT_BORDER_WIDTH
				+ E_WEEK_VIEW_EVENT_EDGE_X_PAD;
		if (!editing_span
		    && event->start > week_view->day_starts[span->start_day]) {
			time_x = x1 + E_WEEK_VIEW_EVENT_L_PAD
				+ E_WEEK_VIEW_EVENT_BORDER_WIDTH
				+ E_WEEK_VIEW_EVENT_EDGE_X_PAD;
				
			clip_rect.x = x1;
			clip_rect.y = y1;
			clip_rect.width = x2 - x1 - E_WEEK_VIEW_EVENT_R_PAD
				- E_WEEK_VIEW_EVENT_BORDER_WIDTH + 1;
			clip_rect.height = y2 - y1 + 1;

			week_view_draw_time (
				week_view, cr, time_x,
				time_y, start_hour, start_minute);

			/* We don't want the end time to be drawn over the
			   start time, so we increase the minimum position. */
			min_end_time_x += time_width
				+ E_WEEK_VIEW_EVENT_TIME_X_PAD;
		} 

		max_icon_x = x2 + 1 - E_WEEK_VIEW_EVENT_R_PAD
			- E_WEEK_VIEW_EVENT_BORDER_WIDTH
			- E_WEEK_VIEW_EVENT_EDGE_X_PAD;
		
		time_x = x2 + 1 - E_WEEK_VIEW_EVENT_R_PAD
				- E_WEEK_VIEW_EVENT_BORDER_WIDTH
				- E_WEEK_VIEW_EVENT_EDGE_X_PAD;
		if (!editing_span
		    && event->end < week_view->day_starts[span->start_day
							 + span->num_days]) {
			/* Calculate where the end time should be displayed. */
			time_x = x2 + 1 - E_WEEK_VIEW_EVENT_R_PAD
				- E_WEEK_VIEW_EVENT_BORDER_WIDTH
				- E_WEEK_VIEW_EVENT_EDGE_X_PAD
				- time_width;

			/* Draw the end time, if the position is greater than
			   the minimum calculated above. */
			if (time_x >= min_end_time_x) {
				week_view_draw_time (
					week_view, cr, time_x,
					time_y, end_hour, end_minute);
				max_icon_x -= time_width
					+ E_WEEK_VIEW_EVENT_TIME_X_PAD;
			}
		}
		
		icon_x = x1 + E_WEEK_VIEW_EVENT_L_PAD
			+ E_WEEK_VIEW_EVENT_BORDER_WIDTH;

		/* Draw the icons. */
		if ( 
		     (week_view->editing_event_num != event_item->priv->event_num
			|| week_view->editing_span_num != event_item->priv->span_num)) {
			gint val;
			icon_x = min_end_time_x;
			icon_x += E_WEEK_VIEW_EVENT_TIME_X_PAD;
			val = week_view_clutter_event_item_draw_icons (
				event_item, cr, icon_x,
				icon_y, max_icon_x, FALSE, draw_region);
		
			if (val == -1)
				icon_x = x1 + E_WEEK_VIEW_EVENT_L_PAD
					+ E_WEEK_VIEW_EVENT_BORDER_WIDTH;
			else 
				icon_x = val;
		}

		/* Draw text */
		if (icon_x < time_x) {
			PangoLayout *layout;
			GdkColor col = e_week_view_get_text_color (week_view, event, week_view);
			
			cairo_save (cr);
			gdk_cairo_set_source_color (cr, &col);
			
			icon_x += E_WEEK_VIEW_EVENT_TIME_X_PAD;

			cairo_rectangle (cr, icon_x , 0, time_x-icon_x, week_view->row_height);
			cairo_clip (cr);
			layout = gtk_widget_create_pango_layout (GTK_WIDGET (week_view), NULL);
			if (event_item->priv->text)
				pango_layout_set_text (layout, event_item->priv->text, -1);
			cairo_move_to (cr,
				       icon_x, 
				       E_WEEK_VIEW_EVENT_BORDER_HEIGHT + E_WEEK_VIEW_ICON_Y_PAD);
			
			pango_cairo_show_layout (cr, layout);

			cairo_stroke (cr);
			cairo_restore (cr);
			g_object_unref (layout);
		}
		
	}
	cairo_destroy (cr);

	gdk_region_destroy (draw_region);
}

static gint
week_view_clutter_event_item_event (GnomeCanvasItem *item,
                            ClutterEvent *event)
{
	EWeekViewClutterEventItem *event_item;

	event_item = E_WEEK_VIEW_CLUTTER_EVENT_ITEM (item);

	switch (event->type) {
	case CLUTTER_BUTTON_PRESS:
		if (event->button.click_count > 1)
			return week_view_clutter_event_item_double_click (event_item, event);
		else
			return week_view_clutter_event_item_button_press (event_item, event);
	case CLUTTER_BUTTON_RELEASE:
		return week_view_clutter_event_item_button_release (event_item, event);
	case CLUTTER_MOTION:
		break;
	default:
		break;
	}

	return FALSE;
}

static void
week_view_clutter_event_item_class_init (EWeekViewClutterEventItemClass *class)
{
	GObjectClass *object_class;
	MxBoxLayoutClass *item_class;
	ClutterActorClass *widget_class;

	parent_class = g_type_class_peek_parent (class);
	g_type_class_add_private (class, sizeof (EWeekViewClutterEventItemPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = week_view_clutter_event_item_set_property;
	object_class->get_property = week_view_clutter_event_item_get_property;

	item_class = MX_BOX_LAYOUT_CLASS (class);
	//item_class->update = week_view_clutter_event_item_update;
	//item_class->draw = week_view_clutter_event_item_draw;
	//item_class->point = week_view_clutter_event_item_point;
	CLUTTER_ACTOR_CLASS(item_class)->event = week_view_clutter_event_item_event;

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
		PROP_SPAN_NUM,
		g_param_spec_int (
			"span-num",
			"Span Num",
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
week_view_clutter_event_item_init (EWeekViewClutterEventItem *event_item)
{
	event_item->priv = E_WEEK_VIEW_CLUTTER_EVENT_ITEM_GET_PRIVATE (event_item);

	event_item->priv->event_num = -1;
	event_item->priv->span_num = -1;
}

GType
e_week_view_clutter_event_item_get_type (void)
{
	static GType type = 0;

	if (G_UNLIKELY (type == 0)) {
		const GTypeInfo type_info = {
			sizeof (EWeekViewClutterEventItemClass),
			(GBaseInitFunc) NULL,
			(GBaseFinalizeFunc) NULL,
			(GClassInitFunc) week_view_clutter_event_item_class_init,
			(GClassFinalizeFunc) NULL,
			NULL,  /* class_data */
			sizeof (EWeekViewClutterEventItem),
			0,     /* n_preallocs */
			(GInstanceInitFunc) week_view_clutter_event_item_init,
			NULL   /* value_table */
		};

		type = g_type_register_static (
			MX_TYPE_BOX_LAYOUT, "EWeekViewClutterEventItem",
			&type_info, 0);
	}

	return type;
}

gint
e_week_view_clutter_event_item_get_event_num (EWeekViewClutterEventItem *event_item)
{
	g_return_val_if_fail (E_IS_WEEK_VIEW_CLUTTER_EVENT_ITEM (event_item), -1);

	return event_item->priv->event_num;
}

void
e_week_view_clutter_event_item_set_event_num (EWeekViewClutterEventItem *event_item,
                                      gint event_num)
{
	g_return_if_fail (E_IS_WEEK_VIEW_CLUTTER_EVENT_ITEM (event_item));

	event_item->priv->event_num = event_num;
	week_view_clutter_event_item_draw (event_item);

	g_object_notify (G_OBJECT (event_item), "event-num");
}

const char *
e_week_view_clutter_event_item_get_text (EWeekViewClutterEventItem *event_item)
{
	g_return_val_if_fail (E_IS_WEEK_VIEW_CLUTTER_EVENT_ITEM (event_item), -1);

	return event_item->priv->text;
}

void
e_week_view_clutter_event_item_set_text (EWeekViewClutterEventItem *event_item,
				 	 const char *txt)
{
	g_return_if_fail (E_IS_WEEK_VIEW_CLUTTER_EVENT_ITEM (event_item));

	if (event_item->priv->text)
		g_free (event_item->priv->text);

	event_item->priv->text = g_strdup (txt); 
	week_view_clutter_event_item_draw (event_item);

	g_object_notify (G_OBJECT (event_item), "text");
}

gint
e_week_view_clutter_event_item_get_span_num (EWeekViewClutterEventItem *event_item)
{
	g_return_val_if_fail (E_IS_WEEK_VIEW_CLUTTER_EVENT_ITEM (event_item), -1);

	return event_item->priv->span_num;
}

void
e_week_view_clutter_event_item_set_span_num (EWeekViewClutterEventItem *event_item,
                                     gint span_num)
{
	g_return_if_fail (E_IS_WEEK_VIEW_CLUTTER_EVENT_ITEM (event_item));

	event_item->priv->span_num = span_num;
	week_view_clutter_event_item_draw (event_item);

	g_object_notify (G_OBJECT (event_item), "span-num");
}

static void
handle_activate (ClutterActor *actor, 
		 EWeekViewClutterEventItem *item)
{
	gtk_widget_grab_focus (item->priv->week_view);
	e_week_view_cancel_editing (item->priv->week_view);
	e_week_view_on_editing_stopped (item->priv->week_view, NULL, TRUE);	
}

static gboolean
handle_text_item_event (ClutterActor *actor, 	
			ClutterEvent *event,
			EWeekViewClutterEventItem *item)
{
	EWeekView *week_view = item->priv->week_view;

	switch (event->type) {
	case CLUTTER_BUTTON_PRESS:
		if (event->button.button == 3) {
			e_week_view_cancel_editing (item->priv->week_view);
			e_week_view_on_editing_stopped (item->priv->week_view, NULL, TRUE);	
			gtk_widget_grab_focus (week_view);
			return FALSE;
		}
		return FALSE;
	case CLUTTER_KEY_PRESS:
		if (event->key.keyval == GDK_Escape) {
			e_week_view_cancel_editing (item->priv->week_view);
			if (e_week_view_on_editing_stopped (item->priv->week_view, NULL, FALSE)) {
				item->priv->week_view->editing_event_num = -1;
				item->priv->week_view->editing_span_num = -1;
			}
			gtk_widget_grab_focus (week_view);
			return TRUE;
		}
		
		return FALSE;
	default:
		break;
	}

	return FALSE;
}

EWeekViewClutterEventItem *
e_week_view_clutter_event_item_new (EWeekView *view)
{
	EWeekViewClutterEventItem *item = g_object_new (E_TYPE_WEEK_VIEW_CLUTTER_EVENT_ITEM, NULL);
	MxBoxLayout *box = (MxBoxLayout *)item;

	item->priv->week_view = view;
	item->priv->texture = clutter_cairo_texture_new (10, view->row_height);
	clutter_actor_set_reactive (item->priv->texture, TRUE);
	
	mx_box_layout_set_orientation (box, MX_ORIENTATION_VERTICAL);
	mx_box_layout_add_actor (box,
                               item->priv->texture, -1);
	clutter_container_child_set (CLUTTER_CONTAINER (box),
                               item->priv->texture,
			       "expand", TRUE,
			       "x-fill", TRUE,
			       "y-fill", TRUE,			       
                               NULL);
	clutter_actor_show_all (box);
	clutter_actor_set_reactive (box, TRUE);

	item->priv->text_item = clutter_text_new ();
	g_signal_connect (item->priv->text_item, "event", G_CALLBACK(handle_text_item_event), item);
	clutter_text_set_activatable (item->priv->text_item, TRUE);
	clutter_text_set_single_line_mode (item->priv->text_item, TRUE);
	g_signal_connect (item->priv->text_item, "activate", G_CALLBACK(handle_activate), item);
	clutter_text_set_line_wrap   (item->priv->text_item, FALSE);
	clutter_text_set_editable (item->priv->text_item, TRUE);
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
e_week_view_clutter_event_item_redraw  (EWeekViewClutterEventItem *item)
{
	week_view_clutter_event_item_draw (item);
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
rotate_stage2 (ClutterAnimation *amim, struct _anim_data *data)
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
rotate_stage1 (ClutterAnimation *amim, struct _anim_data *data)
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
				"signal-after::completed", rotate_stage2, data,
				NULL);
	

}

static void
wvce_animate_rotate (ClutterActor *item,
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
				"signal-after::completed", rotate_stage1, data,
				NULL);
}

static void
wvce_set_view_editing_1 (EWeekViewClutterEventItem *item)
{
	clutter_actor_hide (item->priv->texture);
	clutter_actor_show (item->priv->text_item);
}
static void
wvce_set_view_editing_2 (EWeekViewClutterEventItem *item)
{
	clutter_grab_keyboard (item->priv->text_item);
	clutter_actor_grab_key_focus (item->priv->text_item);	
}

void 
e_week_view_clutter_event_item_switch_editing_mode (EWeekViewClutterEventItem *item)
{
	float height=0, width=0;

	clutter_text_set_text (item->priv->text_item, item->priv->text);

	wvce_animate_rotate (item, wvce_set_view_editing_1, item,
				wvce_set_view_editing_2, item);
	//wvce_set_view_editing_1 (item);
	//wvce_set_view_editing_2 (item);
#if 0	
	clutter_actor_get_size (item, &width, &height);
	//clutter_actor_set_name (item, "MonthEventTile");
	clutter_text_set_text (item->priv->text_item, item->priv->text);
	clutter_actor_hide (item->priv->texture);
	clutter_actor_set_anchor_point (item, 0, (float)height/2);
	clutter_actor_move_by (item, 0, height/2);

	clutter_actor_set_rotation (item,
                          CLUTTER_X_AXIS,  /* or CLUTTER_Y_AXIS */
                          180.0,             /* set the rotation to this angle */
                          0.0,
			  0.0,
                          0);
	clutter_actor_animate (item, CLUTTER_LINEAR,
				500,
				"rotation-angle-x", 360.0,
				"signal::completed", reset_gravity, item,
				NULL);

	clutter_actor_show (item->priv->text_item);
	clutter_grab_keyboard (item->priv->text_item);
	clutter_actor_grab_key_focus (item->priv->text_item);
#endif
}

static void
scale_stage2 (ClutterAnimation *amim, struct _anim_data *data)
{

	data->cb2 (data->data2);	
	
}

static void
scale_stage1 (ClutterAnimation *amim, struct _anim_data *data)
{
	data->cb1 (data->data1);

	clutter_actor_animate (data->item, CLUTTER_EASE_IN_SINE,
				200,
				"scale-x", 1.0,
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

	data->item = item;
	data->cb1 = cb1;
	data->data1 = data1;
	data->cb2 = cb2;
	data->data2 = data2;

	clutter_actor_get_size (item, &width, &height);

	g_object_set (item, "scale-center-x", width/2, "scale-center-y", height/2, NULL);

	clutter_actor_animate (item, CLUTTER_EASE_OUT_SINE,
				200,
				"scale-x", 0.0,
				"signal-after::completed", scale_stage1, data,
				NULL);
}

static void
wvce_set_view_normal_1 (EWeekViewClutterEventItem *item)
{
	clutter_actor_hide (item->priv->text_item);
	clutter_actor_show (item->priv->texture);	
}
static void
wvce_set_view_normal_2 (EWeekViewClutterEventItem *item)
{
	/* Do nothing */
}

void 
e_week_view_clutter_event_item_switch_normal_mode (EWeekViewClutterEventItem *item)
{

	wvce_animate_scale (item, wvce_set_view_normal_1, item,
				wvce_set_view_normal_2, item);

	//clutter_actor_hide (item->priv->text_item);
	//clutter_actor_show (item->priv->texture);	
}

void 
e_week_view_clutter_event_item_switch_viewing_mode (EWeekViewClutterEventItem *item)
{
}

const char *
e_week_view_clutter_event_item_get_edit_text (EWeekViewClutterEventItem *item)
{
	return clutter_text_get_text (item->priv->text_item);
}


static void
scale_delete_stage2 (ClutterAnimation *amim, ClutterActor *actor)
{
	clutter_actor_destroy (actor);
}

static void
scale_delete_stage1 (ClutterAnimation *amim, ClutterActor *item)
{
	clutter_actor_animate (item, CLUTTER_EASE_IN_SINE,
				200,
				"scale-x", 0.1,
				"signal-after::completed", scale_delete_stage2, item,
				NULL);
	

}

static void
wvce_animate_scale_delete (ClutterActor *item)
{
	float height=0, width=0;

	clutter_actor_get_size (item, &width, &height);

	g_object_set (item, "scale-center-x", width/2, "scale-center-y", height/2, NULL);

	clutter_actor_animate (item, CLUTTER_EASE_OUT_SINE,
				200,
				"scale-y", 0.1,
				"signal-after::completed", scale_delete_stage1, item,
				NULL);
}

void
e_week_view_clutter_event_item_scale_destroy (EWeekViewClutterEventItem *item)
{
	wvce_animate_scale_delete (item);
}


static void
fade_delete_stage1 (ClutterAnimation *amim, ClutterActor *actor)
{
	clutter_actor_destroy (actor);
}

void
e_week_view_clutter_event_item_fade_destroy (EWeekViewClutterEventItem *item)
{
	clutter_actor_animate (item, CLUTTER_EASE_OUT_SINE,
				200,
				"opacity", 0.0,
				"signal-after::completed", fade_delete_stage1, item,
				NULL);	
}

