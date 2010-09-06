/*
 * EWeekViewClutterTitlesItem - displays the 'Monday', 'Tuesday' etc. at the top of
 * the Month calendar view.
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
 * Authors:
 *		Srinivasa Ragavan <sragavan@gnome.org>
 *
 * Copyright (C) 2010 Intel Corporation. (www.intel.com)
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <e-util/e-util.h>
#include "e-week-view-clutter-titles-item.h"

#define E_WEEK_VIEW_CLUTTER_TITLES_ITEM_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_WEEK_VIEW_CLUTTER_TITLES_ITEM, EWeekViewClutterTitlesItemPrivate))

struct _EWeekViewClutterTitlesItemPrivate {
	EWeekView *week_view;
};

enum {
	PROP_0,
	PROP_WEEK_VIEW
};

static gpointer parent_class;

static void
week_view_clutter_titles_item_set_property (GObject *object,
                                    guint property_id,
                                    const GValue *value,
                                    GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_WEEK_VIEW:
			e_week_view_clutter_titles_item_set_week_view (
				E_WEEK_VIEW_CLUTTER_TITLES_ITEM (object),
				g_value_get_object (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
week_view_clutter_titles_item_get_property (GObject *object,
                                    guint property_id,
                                    GValue *value,
                                    GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_WEEK_VIEW:
			g_value_set_object (
				value,
				e_week_view_clutter_titles_item_get_week_view (
				E_WEEK_VIEW_CLUTTER_TITLES_ITEM (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
week_view_clutter_titles_item_dispose (GObject *object)
{
	EWeekViewClutterTitlesItemPrivate *priv;

	priv = E_WEEK_VIEW_CLUTTER_TITLES_ITEM_GET_PRIVATE (object);

	if (priv->week_view != NULL) {
		g_object_unref (priv->week_view);
		priv->week_view = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
week_view_clutter_titles_item_draw (ClutterCairoTexture *canvas_item)
{
	EWeekViewClutterTitlesItem *titles_item;
	EWeekView *week_view;
	GtkStyle *style;
	GdkColor fg, light, dark;
	gint col_width, col, date_width, date_x;
	gchar buffer[128];
	GdkRectangle clip_rect;
	gboolean abbreviated;
	gint weekday;
	PangoLayout *layout;
	gint x=0, y=0;
	gint width, height;
	cairo_t *cr;

	clutter_cairo_texture_clear (canvas_item);
	clutter_cairo_texture_get_surface_size ((ClutterCairoTexture *)canvas_item, &width, &height);
	cr = clutter_cairo_texture_create ((ClutterCairoTexture *)canvas_item);
	
	titles_item = E_WEEK_VIEW_CLUTTER_TITLES_ITEM (canvas_item);
	week_view = e_week_view_clutter_titles_item_get_week_view (titles_item);
	g_return_if_fail (week_view != NULL);

	style = gtk_widget_get_style (GTK_WIDGET (week_view));
	fg = style->fg[GTK_STATE_NORMAL];
	light = style->light[GTK_STATE_NORMAL];
	dark = style->dark[GTK_STATE_NORMAL];

	layout = gtk_widget_create_pango_layout (GTK_WIDGET (week_view), NULL);

	/* Draw the shadow around the dates. */
	cairo_save (cr);
	gdk_cairo_set_source_color (cr, &light);
	cairo_move_to (cr, 1 - x,  1 - y);
	cairo_line_to (cr, width - 2 - x, 1 - y);

	//gdk_draw_line (drawable, light_gc,
	//	       1 - x, 1 - y,
	//	       allocation.width - 2 - x, 1 - y);
	cairo_move_to (cr, 1 - x,  2 - y);
	cairo_line_to (cr, 1 - x, height - 1 - y);
	cairo_stroke (cr);
	cairo_restore (cr);

	//gdk_draw_line (drawable, light_gc,
	//	       1 - x, 2 - y,
	//	       1 - x, allocation.height - 1 - y);

	cairo_save (cr);
	gdk_cairo_set_source_color (cr, &dark);
	cairo_rectangle (cr, 0 - x, 0 - y,
			 width - 1, height);

	//gdk_draw_rectangle (drawable, dark_gc, FALSE,
	//		    0 - x, 0 - y,
	//		    allocation.width - 1, allocation.height);
	cairo_stroke (cr);
	cairo_restore (cr);

	/* Determine the format to use. */
	col_width = width / week_view->columns;
	abbreviated = (week_view->max_day_width + 2 >= col_width);

	/* Shift right one pixel to account for the shadow around the main
	   canvas. */
	//x--;

	/* Draw the date. Set a clipping rectangle so we don't draw over the
	   next day. */
	weekday = week_view->display_start_day;
	for (col = 0; col < week_view->columns; col++) {
		if (weekday == 5 && week_view->compress_weekend)
			g_snprintf (
				buffer, sizeof (buffer), "%s/%s",
				e_get_weekday_name (G_DATE_SATURDAY, TRUE),
				e_get_weekday_name (G_DATE_SUNDAY, TRUE));
		else
			g_snprintf (
				buffer, sizeof (buffer), "%s",
				e_get_weekday_name (weekday + 1, abbreviated));
		
		cairo_save (cr);

		clip_rect.x = week_view->col_offsets[col] - x;
		clip_rect.y = 2 - y;
		clip_rect.width = week_view->col_widths[col];
		clip_rect.height = height - 2;
		cairo_rectangle (cr, clip_rect.x, clip_rect.y,
				 clip_rect.width, clip_rect.height);
		cairo_clip (cr);
		//gdk_gc_set_clip_rectangle (fg_gc, &clip_rect);

		if (weekday == 5 && week_view->compress_weekend)
			date_width = week_view->abbr_day_widths[5]
				+ week_view->slash_width
				+ week_view->abbr_day_widths[6];
		else if (abbreviated)
			date_width = week_view->abbr_day_widths[weekday];
		else
			date_width = week_view->day_widths[weekday];

		date_x = week_view->col_offsets[col]
			+ (week_view->col_widths[col] - date_width) / 2;
		date_x = MAX (date_x, week_view->col_offsets[col]);

		pango_layout_set_text (layout, buffer, -1);

		gdk_cairo_set_source_color (cr, &fg);
		cairo_move_to (cr, date_x - x, 3 - y);
		pango_cairo_show_layout (cr, layout);

		//gdk_draw_layout (drawable, fg_gc,
		//		 date_x - x,
		//		 3 - y,
		//		 layout);

		//gdk_gc_set_clip_rectangle (fg_gc, NULL);
		cairo_stroke(cr);
		cairo_restore(cr);

		/* Draw the lines down the left and right of the date cols. */
		if (col != 0) {
			cairo_save(cr);
			gdk_cairo_set_source_color (cr, &light);
			cairo_move_to (cr, week_view->col_offsets[col] - x,
				       4 - y);
			cairo_line_to (cr, week_view->col_offsets[col] - x,
				       height - 4 - y);
			cairo_stroke(cr);
			cairo_restore(cr);

			//gdk_draw_line (drawable, light_gc,
			//	       week_view->col_offsets[col] - x,
			//	       4 - y,
			//	       week_view->col_offsets[col] - x,
			//	       allocation.height - 4 - y);
			cairo_save(cr);
			gdk_cairo_set_source_color (cr, &dark);

			cairo_move_to (cr, week_view->col_offsets[col] - 1 - x,
				       4 - y);
			cairo_line_to (cr, week_view->col_offsets[col] - 1 - x,
				       height - 4 - y);

			//gdk_draw_line (drawable, dark_gc,
			//	       week_view->col_offsets[col] - 1 - x,
			//	       4 - y,
			//	       week_view->col_offsets[col] - 1 - x,
			//	       allocation.height - 4 - y);

			cairo_stroke(cr);
			cairo_restore(cr);
		}

		/* Draw the lines between each column. */
		if (col != 0) {
			cairo_save (cr);
			gdk_cairo_set_source_color (cr, &style->black);
			
			cairo_move_to (cr, week_view->col_offsets[col] - x,
				       height - y);
			cairo_line_to (cr, week_view->col_offsets[col] - x,
				       height - y);

			//gdk_draw_line (drawable, style->black_gc,
			//	       week_view->col_offsets[col] - x,
			//	       allocation.height - y,
			//	       week_view->col_offsets[col] - x,
			//	       allocation.height - y);
			cairo_stroke(cr);
			cairo_restore(cr);			
		}

		if (weekday == 5 && week_view->compress_weekend)
			weekday += 2;
		else
			weekday++;

		weekday = weekday % 7;
	}

	g_object_unref (layout);
	cairo_destroy (cr);
}

static void
week_view_clutter_titles_item_class_init (EWeekViewClutterTitlesItemClass *class)
{
	GObjectClass  *object_class;
	ClutterCairoTextureClass *item_class;

	parent_class = g_type_class_peek_parent (class);
	g_type_class_add_private (class, sizeof (EWeekViewClutterTitlesItemPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = week_view_clutter_titles_item_set_property;
	object_class->get_property = week_view_clutter_titles_item_get_property;
	object_class->dispose = week_view_clutter_titles_item_dispose;

	item_class = CLUTTER_CAIRO_TEXTURE_CLASS (class);

	g_object_class_install_property (
		object_class,
		PROP_WEEK_VIEW,
		g_param_spec_object (
			"week-view",
			"Week View",
			NULL,
			E_TYPE_WEEK_VIEW,
			G_PARAM_READWRITE));
}

static void
week_view_clutter_titles_item_init (EWeekViewClutterTitlesItem *titles_item)
{
	titles_item->priv = E_WEEK_VIEW_CLUTTER_TITLES_ITEM_GET_PRIVATE (titles_item);
}

GType
e_week_view_clutter_titles_item_get_type (void)
{
	static GType type = 0;

	if (G_UNLIKELY (type == 0)) {
		const GTypeInfo type_info = {
			sizeof (EWeekViewClutterTitlesItemClass),
			(GBaseInitFunc) NULL,
			(GBaseFinalizeFunc) NULL,
			(GClassInitFunc) week_view_clutter_titles_item_class_init,
			(GClassFinalizeFunc) NULL,
			NULL,  /* class_data */
			sizeof (EWeekViewClutterTitlesItem),
			0,     /* n_preallocs */
			(GInstanceInitFunc) week_view_clutter_titles_item_init,
			NULL   /* value_table */
		};

		type = g_type_register_static (
			CLUTTER_TYPE_CAIRO_TEXTURE, "EWeekViewClutterTitlesItem",
			&type_info, 0);
	}

	return type;
}

EWeekView *
e_week_view_clutter_titles_item_get_week_view (EWeekViewClutterTitlesItem *titles_item)
{
	g_return_val_if_fail (E_IS_WEEK_VIEW_CLUTTER_TITLES_ITEM (titles_item), NULL);

	return titles_item->priv->week_view;
}

void
e_week_view_clutter_titles_item_set_week_view (EWeekViewClutterTitlesItem *titles_item,
                                       EWeekView *week_view)
{
	g_return_if_fail (E_IS_WEEK_VIEW_CLUTTER_TITLES_ITEM (titles_item));
	g_return_if_fail (E_IS_WEEK_VIEW (week_view));

	if (titles_item->priv->week_view != NULL)
		g_object_unref (titles_item->priv->week_view);

	titles_item->priv->week_view = g_object_ref (week_view);

	g_object_notify (G_OBJECT (titles_item), "week-view");
}

void
e_week_view_clutter_titles_item_redraw (EWeekViewClutterTitlesItem *item)
{
	clutter_cairo_texture_clear ((ClutterCairoTexture *)item);
	week_view_clutter_titles_item_draw ((ClutterActor *)item);
}

void
e_week_view_clutter_titles_item_set_size (EWeekViewClutterTitlesItem *item, 
					  int width, 
					  int height)
{
	clutter_cairo_texture_set_surface_size ((ClutterCairoTexture *)item, width, height);
	week_view_clutter_titles_item_draw (item);
}
