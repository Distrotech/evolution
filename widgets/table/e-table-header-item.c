/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * e-table-header-item.c
 * Copyright 1999, 2000, 2001, Ximian, Inc.
 *
 * Authors:
 *   Chris Lahey <clahey@ximian.com>
 *   Miguel de Icaza (miguel@gnu.org)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License, version 2, as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include <config.h>

#include "e-table-header-item.h"

#include <string.h>
#include <gtk/gtksignal.h>
#include <gtk/gtkdnd.h>
#include <libgnomeui/gnome-canvas.h>
#include <libgnomeui/gnome-canvas-util.h>
#include <libgnomeui/gnome-canvas-polygon.h>
#include <libgnomeui/gnome-canvas-rect-ellipse.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include "gal/widgets/e-cursors.h"
#include "gal/util/e-i18n.h"
#include "gal/util/e-util.h"
#include "gal/util/e-xml-utils.h"
#include "gal/widgets/e-canvas.h"
#include "gal/widgets/e-popup-menu.h"
#include "e-table-header.h"
#include "e-table-header-utils.h"
#include "e-table-col-dnd.h"
#include "e-table-defines.h"
#include "e-table-field-chooser-dialog.h"
#include "e-table-config.h"
#include "e-table.h"

#include "add-col.xpm"
#include "remove-col.xpm"
#include "arrow-up.xpm"
#include "arrow-down.xpm"

enum {
	BUTTON_PRESSED,
	LAST_SIGNAL
};

static guint ethi_signals [LAST_SIGNAL] = { 0, };

#define ARROW_DOWN_HEIGHT 16
#define ARROW_PTR          7

/* Defines the tolerance for proximity of the column division to the cursor position */
#define TOLERANCE 4

#define ETHI_RESIZING(x) ((x)->resize_col != -1)

#define PARENT_OBJECT_TYPE gnome_canvas_item_get_type ()

#define ELEMENTS(x) (sizeof (x) / sizeof (x[0]))

static GnomeCanvasItemClass *ethi_parent_class;

static void ethi_drop_table_header (ETableHeaderItem *ethi);

/*
 * They display the arrows for the drop location.
 */
	
static GtkWidget *arrow_up, *arrow_down;

/*
 * DnD icons
 */
static GdkColormap *dnd_colormap;
static GdkPixmap *remove_col_pixmap, *remove_col_mask;
static GdkPixmap *add_col_pixmap, *add_col_mask;

enum {
	ARG_0,
	ARG_TABLE_HEADER,
	ARG_FULL_HEADER,
	ARG_DND_CODE,
	ARG_TABLE_FONTSET,
	ARG_SORT_INFO,
	ARG_TABLE,
	ARG_TREE,
};

static void
ethi_destroy (GtkObject *object){
	ETableHeaderItem *ethi = E_TABLE_HEADER_ITEM (object);

	ethi_drop_table_header (ethi);

	if (ethi->dnd_code) {
		g_free (ethi->dnd_code);
		ethi->dnd_code = NULL;
	}

	if (ethi->sort_info) {
		if (ethi->sort_info_changed_id)
			gtk_signal_disconnect (GTK_OBJECT(ethi->sort_info), ethi->sort_info_changed_id);
		if (ethi->group_info_changed_id)
			gtk_signal_disconnect (GTK_OBJECT(ethi->sort_info), ethi->group_info_changed_id);
		gtk_object_unref (GTK_OBJECT(ethi->sort_info));
		ethi->sort_info = NULL;
	}

	if (ethi->full_header)
		gtk_object_unref (GTK_OBJECT(ethi->full_header));

	if (ethi->config)
		gtk_object_destroy (GTK_OBJECT (ethi->config));
	
	if (GTK_OBJECT_CLASS (ethi_parent_class)->destroy)
		(*GTK_OBJECT_CLASS (ethi_parent_class)->destroy) (object);
}

static int
e_table_header_item_get_height (ETableHeaderItem *ethi)
{
	ETableHeader *eth;
	int numcols, col;
	int maxheight;
	GtkStyle *style;

	g_return_val_if_fail (ethi != NULL, 0);
	g_return_val_if_fail (E_IS_TABLE_HEADER_ITEM (ethi), 0);
       
	eth = ethi->eth;
	numcols = e_table_header_count (eth);

	maxheight = 0;

	style = GTK_WIDGET (GNOME_CANVAS_ITEM (ethi)->canvas)->style;

	for (col = 0; col < numcols; col++) {
		ETableCol *ecol = e_table_header_get_column (eth, col);
		int height;

		height = e_table_header_compute_height (ecol, style, ethi->font);

		if (height > maxheight)
			maxheight = height;
	}

	return maxheight;
}

static void
ethi_update (GnomeCanvasItem *item, double *affine, ArtSVP *clip_path, int flags)
{
	ETableHeaderItem *ethi = E_TABLE_HEADER_ITEM (item);
	
	double   i2c [6];
	ArtPoint c1, c2, i1, i2;
	
	if (GNOME_CANVAS_ITEM_CLASS (ethi_parent_class)->update)
		(*GNOME_CANVAS_ITEM_CLASS (ethi_parent_class)->update)(item, affine, clip_path, flags);

	if (ethi->sort_info)
		ethi->group_indent_width = e_table_sort_info_grouping_get_count(ethi->sort_info) * GROUP_INDENT;
	else
		ethi->group_indent_width = 0;

	ethi->width = e_table_header_total_width (ethi->eth) + ethi->group_indent_width;

	i1.x = i1.y = 0;
	i2.x = ethi->width;
	i2.y = ethi->height;

	gnome_canvas_item_i2c_affine (item, i2c);
	art_affine_point (&c1, &i1, i2c);
	art_affine_point (&c2, &i2, i2c);

	if (item->x1 != c1.x ||
	    item->y1 != c1.y ||
	    item->x2 != c2.x ||
	    item->y2 != c2.y)
		{
			gnome_canvas_request_redraw (item->canvas, item->x1, item->y1, item->x2, item->y2);
			item->x1 = c1.x;
			item->y1 = c1.y;
			item->x2 = c2.x;
			item->y2 = c2.y;

			gnome_canvas_group_child_bounds (GNOME_CANVAS_GROUP (item->parent), item);
		}
	gnome_canvas_request_redraw (item->canvas, item->x1, item->y1, item->x2, item->y2);
}

static void
ethi_font_set (ETableHeaderItem *ethi, GdkFont *font)
{
	if (ethi->font)
		gdk_font_unref (ethi->font);

	ethi->font = font;
	gdk_font_ref (font);
	
	ethi->height = e_table_header_item_get_height (ethi);
	e_canvas_item_request_reflow(GNOME_CANVAS_ITEM(ethi));
}

static void
ethi_font_load (ETableHeaderItem *ethi, char *fontname)
{
	GdkFont *font = NULL;

	if (fontname != NULL)
		font = gdk_fontset_load (fontname);

	if (font == NULL) {
		font = GTK_WIDGET (GNOME_CANVAS_ITEM (ethi)->canvas)->style->font;
		gdk_font_ref (font);
	}
	
	ethi_font_set (ethi, font);
	gdk_font_unref (font);
}

static void
ethi_drop_table_header (ETableHeaderItem *ethi)
{
	GtkObject *header;
	
	if (!ethi->eth)
		return;

	header = GTK_OBJECT (ethi->eth);
	gtk_signal_disconnect (header, ethi->structure_change_id);
	gtk_signal_disconnect (header, ethi->dimension_change_id);

	gtk_object_unref (header);
	ethi->eth = NULL;
	ethi->width = 0;
}

static void 
structure_changed (ETableHeader *header, ETableHeaderItem *ethi)
{
	gnome_canvas_item_request_update(GNOME_CANVAS_ITEM(ethi));
}

static void
dimension_changed (ETableHeader *header, int col, ETableHeaderItem *ethi)
{
	gnome_canvas_item_request_update(GNOME_CANVAS_ITEM(ethi));
}

static void
ethi_add_table_header (ETableHeaderItem *ethi, ETableHeader *header)
{
	ethi->eth = header;
	gtk_object_ref (GTK_OBJECT (ethi->eth));

	ethi->height = e_table_header_item_get_height (ethi);

	ethi->structure_change_id = gtk_signal_connect (
		GTK_OBJECT (header), "structure_change",
		GTK_SIGNAL_FUNC(structure_changed), ethi);
	ethi->dimension_change_id = gtk_signal_connect (
		GTK_OBJECT (header), "dimension_change",
		GTK_SIGNAL_FUNC(dimension_changed), ethi);
	e_canvas_item_request_reflow(GNOME_CANVAS_ITEM(ethi));
	gnome_canvas_item_request_update (GNOME_CANVAS_ITEM(ethi));
}

static void
ethi_sort_info_changed (ETableSortInfo *sort_info, ETableHeaderItem *ethi)
{
	gnome_canvas_item_request_update (GNOME_CANVAS_ITEM(ethi));
}

static void
ethi_set_arg (GtkObject *o, GtkArg *arg, guint arg_id)
{
	GnomeCanvasItem *item;
	ETableHeaderItem *ethi;

	item = GNOME_CANVAS_ITEM (o);
	ethi = E_TABLE_HEADER_ITEM (o);

	switch (arg_id){
	case ARG_TABLE_HEADER:
		ethi_drop_table_header (ethi);
		ethi_add_table_header (ethi, E_TABLE_HEADER(GTK_VALUE_OBJECT (*arg)));
		break;

	case ARG_FULL_HEADER:
		if (ethi->full_header)
			gtk_object_unref(GTK_OBJECT(ethi->full_header));
		ethi->full_header = E_TABLE_HEADER(GTK_VALUE_OBJECT (*arg));
		if (ethi->full_header)
			gtk_object_ref(GTK_OBJECT(ethi->full_header));
		break;

	case ARG_DND_CODE:
		g_free(ethi->dnd_code);
		ethi->dnd_code = g_strdup (GTK_VALUE_STRING (*arg));
		break;

	case ARG_TABLE_FONTSET:
		ethi_font_load (ethi, GTK_VALUE_STRING (*arg));
		break;

	case ARG_SORT_INFO:
		if (ethi->sort_info){
			if (ethi->sort_info_changed_id)
				gtk_signal_disconnect (
					GTK_OBJECT(ethi->sort_info),
					ethi->sort_info_changed_id);

			if (ethi->group_info_changed_id)
				gtk_signal_disconnect (
					GTK_OBJECT(ethi->sort_info),
					ethi->group_info_changed_id);
			gtk_object_unref (GTK_OBJECT(ethi->sort_info));
		}
		ethi->sort_info = GTK_VALUE_POINTER (*arg);
		gtk_object_ref (GTK_OBJECT(ethi->sort_info));
		ethi->sort_info_changed_id =
			gtk_signal_connect (
				GTK_OBJECT(ethi->sort_info), "sort_info_changed",
				GTK_SIGNAL_FUNC(ethi_sort_info_changed), ethi);
		ethi->group_info_changed_id =
			gtk_signal_connect (
				GTK_OBJECT(ethi->sort_info), "group_info_changed",
				GTK_SIGNAL_FUNC(ethi_sort_info_changed), ethi);
		break;
	case ARG_TABLE:
		if (GTK_VALUE_OBJECT(*arg))
			ethi->table = E_TABLE(GTK_VALUE_OBJECT(*arg));
		else
			ethi->table = NULL;
		break;
	case ARG_TREE:
		if (GTK_VALUE_OBJECT(*arg))
			ethi->tree = E_TREE(GTK_VALUE_OBJECT(*arg));
		else
			ethi->tree = NULL;
		break;
	}
	gnome_canvas_item_request_update(item);
}

static void
ethi_get_arg (GtkObject *o, GtkArg *arg, guint arg_id)
{
	ETableHeaderItem *ethi;

	ethi = E_TABLE_HEADER_ITEM (o);

	switch (arg_id){
	case ARG_FULL_HEADER:
		GTK_VALUE_OBJECT (*arg) = GTK_OBJECT (ethi->full_header);
		break;
	case ARG_DND_CODE:
		GTK_VALUE_STRING (*arg) = g_strdup (ethi->dnd_code);
		break;
	default:
		arg->type = GTK_TYPE_INVALID;
		break;
	}
}

static int
ethi_find_col_by_x (ETableHeaderItem *ethi, int x)
{
	const int cols = e_table_header_count (ethi->eth);
	int x1 = 0;
	int col;

	if (x < x1)
		return -1;

	x1 += ethi->group_indent_width;
	
	for (col = 0; col < cols; col++){
		ETableCol *ecol = e_table_header_get_column (ethi->eth, col);

		if ((x >= x1) && (x <= x1 + ecol->width))
			return col;

		x1 += ecol->width;
	}
	return -1;
}

static int
ethi_find_col_by_x_nearest (ETableHeaderItem *ethi, int x)
{
	const int cols = e_table_header_count (ethi->eth);
	int x1 = 0;
	int col;

	if (x < x1)
		return -1;

	x1 += ethi->group_indent_width;
	
	for (col = 0; col < cols; col++){
		ETableCol *ecol = e_table_header_get_column (ethi->eth, col);

		x1 += (ecol->width / 2);

		if (x <= x1)
			return col;

		x1 += (ecol->width + 1) / 2;
	}
	return col;
}

static void
ethi_remove_drop_marker (ETableHeaderItem *ethi)
{
	if (ethi->drag_mark == -1)
		return;

	gtk_widget_hide (arrow_up);
	gtk_widget_hide (arrow_down);
	
	ethi->drag_mark = -1;
}

static GtkWidget *
make_shaped_window_from_xpm (const char **xpm)
{
	GdkPixbuf *pixbuf;
	GdkPixmap *pixmap;
	GdkBitmap *bitmap;
	GtkWidget *win, *pix;
	
	pixbuf = gdk_pixbuf_new_from_xpm_data (xpm);
	gdk_pixbuf_render_pixmap_and_mask (pixbuf, &pixmap, &bitmap, 128);
	gdk_pixbuf_unref (pixbuf);

	gtk_widget_push_visual (gdk_rgb_get_visual ());
	gtk_widget_push_colormap (gdk_rgb_get_cmap ());
	win = gtk_window_new (GTK_WINDOW_POPUP);
	pix = gtk_pixmap_new (pixmap, bitmap);
	gtk_widget_realize (win);
	gtk_container_add (GTK_CONTAINER (win), pix);
	gtk_widget_shape_combine_mask (win, bitmap, 0, 0);
	gtk_widget_pop_visual ();
	gtk_widget_pop_colormap ();
	
	gdk_pixmap_unref (pixmap);
	gdk_bitmap_unref (bitmap);
	
	return win;
}

static void
ethi_add_drop_marker (ETableHeaderItem *ethi, int col)
{
	int rx, ry;
	int x;
	
	if (ethi->drag_mark == col)
		return;

	ethi->drag_mark = col;

	x = e_table_header_col_diff (ethi->eth, 0, col);
	if (col > 0)
		x += ethi->group_indent_width;
	
	if (!arrow_up){
		arrow_up   = make_shaped_window_from_xpm (arrow_up_xpm);
		arrow_down = make_shaped_window_from_xpm (arrow_down_xpm);
	}

	gdk_window_get_origin (
		GTK_WIDGET (GNOME_CANVAS_ITEM (ethi)->canvas)->window,
		&rx, &ry);

	gtk_widget_set_uposition (arrow_down, rx + x - ARROW_PTR, ry - ARROW_DOWN_HEIGHT);
	gtk_widget_show_all (arrow_down);

	gtk_widget_set_uposition (arrow_up, rx + x - ARROW_PTR, ry + ethi->height);
	gtk_widget_show_all (arrow_up);
}

#define gray50_width    2
#define gray50_height   2
static char gray50_bits [] = {
  0x02, 0x01, };

static void
ethi_add_destroy_marker (ETableHeaderItem *ethi)
{
	double x1;
	
	if (ethi->remove_item)
		gtk_object_destroy (GTK_OBJECT (ethi->remove_item));

	if (!ethi->stipple)
		ethi->stipple = gdk_bitmap_create_from_data  (
			NULL, gray50_bits, gray50_width, gray50_height);
	
	x1 = (double) e_table_header_col_diff (ethi->eth, 0, ethi->drag_col);
	if (ethi->drag_col > 0)
		x1 += ethi->group_indent_width;
	
	ethi->remove_item = gnome_canvas_item_new (
		GNOME_CANVAS_GROUP (GNOME_CANVAS_ITEM (ethi)->canvas->root),
		gnome_canvas_rect_get_type (),
		"x1", x1 + 1,
		"y1", (double) 1,
		"x2", (double) x1 + e_table_header_col_diff (
			ethi->eth, ethi->drag_col, ethi->drag_col+1) - 2,

		"y2", (double) ethi->height - 2,
		"fill_color", "red",
		"fill_stipple", ethi->stipple,
		NULL);
}

static void
ethi_remove_destroy_marker (ETableHeaderItem *ethi)
{
	if (!ethi->remove_item)
		return;
	
	gtk_object_destroy (GTK_OBJECT (ethi->remove_item));
	ethi->remove_item = NULL;
}

#if 0
static gboolean
moved (ETableHeaderItem *ethi, guint col, guint model_col)
{
	if (col == -1)
		return TRUE;
	ecol = e_table_header_get_column (ethi->eth, col);
	if (ecol->col_idx == model_col)
		return FALSE;
	if (col > 0) {
		ecol = e_table_header_get_column (ethi->eth, col - 1);
		if (ecol->col_idx == model_col)
			return FALSE;
	}
	return TRUE;
}
#endif

static gboolean
ethi_drag_motion (GtkObject *canvas, GdkDragContext *context,
		  gint x, gint y, guint time,
		  ETableHeaderItem *ethi)
{
	char *droptype, *headertype;

	gdk_drag_status (context, 0, time);

	droptype = gdk_atom_name (GPOINTER_TO_INT (context->targets->data));
	headertype = g_strdup_printf ("%s-%s", TARGET_ETABLE_COL_TYPE,
				      ethi->dnd_code);

	if (strcmp (droptype, headertype) != 0) {
		g_free (headertype);
		return FALSE;
	}

	g_free (headertype);

	if ((x >= 0) && (x <= (ethi->width)) &&
	    (y >= 0) && (y <= (ethi->height))){
		int col;
		
		col = ethi_find_col_by_x_nearest (ethi, x);

		if (ethi->drag_col != -1 && (col == ethi->drag_col || col == ethi->drag_col + 1)) {
			if (ethi->drag_col != -1)
				ethi_remove_destroy_marker (ethi);
			
			ethi_remove_drop_marker (ethi);
			gdk_drag_status (context, context->suggested_action, time);
		} 
		else if (col != -1){
			if (ethi->drag_col != -1)
				ethi_remove_destroy_marker (ethi);

			ethi_add_drop_marker (ethi, col);
			gdk_drag_status (context, context->suggested_action, time);
		} else {
			ethi_remove_drop_marker (ethi);
			if (ethi->drag_col != -1)
				ethi_add_destroy_marker (ethi);
		}
	} else {
		ethi_remove_drop_marker (ethi);
		if (ethi->drag_col != -1)
			ethi_add_destroy_marker (ethi);
	}

	return TRUE;
}

static void
ethi_drag_end (GtkWidget *canvas, GdkDragContext *context, ETableHeaderItem *ethi)
{
	if (context->action == 0) {
		e_table_header_remove (ethi->eth, ethi->drag_col);
		gnome_canvas_item_request_update(GNOME_CANVAS_ITEM(ethi));
	}
	ethi_remove_drop_marker (ethi);
	ethi_remove_destroy_marker (ethi);
	ethi->drag_col = -1;
}

static void
ethi_drag_data_received (GtkWidget *canvas,
			 GdkDragContext *drag_context,
			 gint x,
			 gint y,
			 GtkSelectionData *data,
			 guint info,
			 guint time,
			 ETableHeaderItem *ethi)
{
	int found = FALSE;
	int count;
	int column;
	int drop_col;
	int i;

	if (data->data) {
		count = e_table_header_count(ethi->eth);
		column = atoi(data->data);
		drop_col = ethi->drop_col;
		ethi->drop_col = -1;

		if (column >= 0) {
			for (i = 0; i < count; i++) {
				ETableCol *ecol = e_table_header_get_column (ethi->eth, i);
				if (ecol->col_idx == column) {
					e_table_header_move(ethi->eth, i, drop_col);
					found = TRUE;
					break;
				}
			}
			if (!found) {
				count = e_table_header_count(ethi->full_header);
				for (i = 0; i < count; i++) {
					ETableCol *ecol = e_table_header_get_column (ethi->full_header, i);
					if (ecol->col_idx == column) {
						e_table_header_add_column (ethi->eth, ecol, drop_col);
						break;
					}
				}
			}
		}
	}
	ethi_remove_drop_marker (ethi);
	gnome_canvas_item_request_update(GNOME_CANVAS_ITEM(ethi));
}

static void
ethi_drag_data_get (GtkWidget *canvas,
		    GdkDragContext     *context,
		    GtkSelectionData   *selection_data,
		    guint               info,
		    guint               time,
		    ETableHeaderItem *ethi)
{
	if (ethi->drag_col != -1) {
		ETableCol *ecol = e_table_header_get_column (ethi->eth, ethi->drag_col);
		
		gchar *string = g_strdup_printf("%d", ecol->col_idx);
		gtk_selection_data_set(selection_data,
				       GDK_SELECTION_TYPE_STRING,
				       sizeof(string[0]),
				       string,
				       strlen(string));
		g_free(string);
	}
}

static gboolean
ethi_drag_drop (GtkWidget *canvas,
		GdkDragContext *context,
		gint x,
		gint y,
		guint time,
		ETableHeaderItem *ethi)
{
	gboolean successful = FALSE;

	if ((x >= 0) && (x <= (ethi->width)) &&
	    (y >= 0) && (y <= (ethi->height))){
		int col;
		
		col = ethi_find_col_by_x_nearest (ethi, x);
		
		ethi_add_drop_marker (ethi, col);

		ethi->drop_col = col;
		
		if (col != -1) {
			char *target = g_strdup_printf ("%s-%s", TARGET_ETABLE_COL_TYPE, ethi->dnd_code);
			gtk_drag_get_data (canvas, context, gdk_atom_intern(target, FALSE), time);
			g_free (target);
		}
	}
	gtk_drag_finish (context, successful, successful, time);
	return successful;
}

static void
ethi_drag_leave (GtkWidget *widget, GdkDragContext *context, guint time, ETableHeaderItem *ethi)
{
	ethi_remove_drop_marker (ethi);
	if (ethi->drag_col != -1)
		ethi_add_destroy_marker (ethi);
}

static void
ethi_realize (GnomeCanvasItem *item)
{
	ETableHeaderItem *ethi = E_TABLE_HEADER_ITEM (item);
	GdkWindow *window;
	GtkTargetEntry  ethi_drop_types [] = {
		{ TARGET_ETABLE_COL_TYPE, 0, TARGET_ETABLE_COL_HEADER },
	};

	
	if (GNOME_CANVAS_ITEM_CLASS (ethi_parent_class)-> realize)
		(*GNOME_CANVAS_ITEM_CLASS (ethi_parent_class)->realize)(item);

	window = GTK_WIDGET (item->canvas)->window;

	if (!ethi->font)
		ethi_font_set (ethi, GTK_WIDGET (item->canvas)->style->font);

	/*
	 * Now, configure DnD
	 */
	ethi_drop_types[0].target = g_strdup_printf("%s-%s", ethi_drop_types[0].target, ethi->dnd_code);
	gtk_drag_dest_set (GTK_WIDGET (item->canvas), 0,
			   ethi_drop_types, ELEMENTS (ethi_drop_types),
			   GDK_ACTION_MOVE);
  	g_free(ethi_drop_types[0].target); 

	/* Drop signals */
	ethi->drag_motion_id = gtk_signal_connect        (GTK_OBJECT (item->canvas), "drag_motion",
							  GTK_SIGNAL_FUNC (ethi_drag_motion), ethi);
	ethi->drag_leave_id = gtk_signal_connect         (GTK_OBJECT (item->canvas), "drag_leave",
							  GTK_SIGNAL_FUNC (ethi_drag_leave), ethi);
	ethi->drag_drop_id = gtk_signal_connect          (GTK_OBJECT (item->canvas), "drag_drop",
							  GTK_SIGNAL_FUNC (ethi_drag_drop), ethi);
	ethi->drag_data_received_id = gtk_signal_connect (GTK_OBJECT (item->canvas), "drag_data_received",
							  GTK_SIGNAL_FUNC (ethi_drag_data_received), ethi);

	/* Drag signals */
	ethi->drag_end_id = gtk_signal_connect           (GTK_OBJECT (item->canvas), "drag_end",
							  GTK_SIGNAL_FUNC (ethi_drag_end), ethi);
	ethi->drag_data_get_id = gtk_signal_connect      (GTK_OBJECT (item->canvas), "drag_data_get",
							  GTK_SIGNAL_FUNC (ethi_drag_data_get), ethi);

}

static void
ethi_unrealize (GnomeCanvasItem *item)
{
	ETableHeaderItem *ethi = E_TABLE_HEADER_ITEM (item);

	gdk_font_unref (ethi->font);

	gtk_signal_disconnect (GTK_OBJECT (item->canvas), ethi->drag_motion_id);
	gtk_signal_disconnect (GTK_OBJECT (item->canvas), ethi->drag_leave_id);
	gtk_signal_disconnect (GTK_OBJECT (item->canvas), ethi->drag_drop_id);
	gtk_signal_disconnect (GTK_OBJECT (item->canvas), ethi->drag_data_received_id);

	gtk_signal_disconnect (GTK_OBJECT (item->canvas), ethi->drag_end_id);
	gtk_signal_disconnect (GTK_OBJECT (item->canvas), ethi->drag_data_get_id);

	if (ethi->stipple){
		gdk_bitmap_unref (ethi->stipple);
		ethi->stipple = NULL;
	}
	
	if (GNOME_CANVAS_ITEM_CLASS (ethi_parent_class)->unrealize)
		(*GNOME_CANVAS_ITEM_CLASS (ethi_parent_class)->unrealize)(item);
}

static void
ethi_draw (GnomeCanvasItem *item, GdkDrawable *drawable, int x, int y, int width, int height)
{
	ETableHeaderItem *ethi = E_TABLE_HEADER_ITEM (item);
	GnomeCanvas *canvas = item->canvas;
	const int cols = e_table_header_count (ethi->eth);
	int x1, x2;
	int col;
	GHashTable *arrows = g_hash_table_new (NULL, NULL);


	if (ethi->sort_info) {
		int length = e_table_sort_info_grouping_get_count(ethi->sort_info);
		int i;
		for (i = 0; i < length; i++) {
			ETableSortColumn column = e_table_sort_info_grouping_get_nth(ethi->sort_info, i);
			g_hash_table_insert (arrows, 
					     GINT_TO_POINTER (column.column),
					     GINT_TO_POINTER (column.ascending ?
							      E_TABLE_COL_ARROW_UP : 
							      E_TABLE_COL_ARROW_DOWN));
		}
		length = e_table_sort_info_sorting_get_count(ethi->sort_info);
		for (i = 0; i < length; i++) {
			ETableSortColumn column = e_table_sort_info_sorting_get_nth(ethi->sort_info, i);
			g_hash_table_insert (arrows, 
					     GINT_TO_POINTER (column.column),
					     GINT_TO_POINTER (column.ascending ?
							      E_TABLE_COL_ARROW_UP : 
							      E_TABLE_COL_ARROW_DOWN));
		}
	}

	ethi->width = e_table_header_total_width (ethi->eth) + ethi->group_indent_width;
	x1 = x2 = 0;
	x2 += ethi->group_indent_width;
	for (col = 0; col < cols; col++, x1 = x2){
		ETableCol *ecol = e_table_header_get_column (ethi->eth, col);
		int col_width;

		col_width = ecol->width;
				
		x2 += col_width;
		
		if (x1 > (x + width))
			break;

		if (x2 < x)
			continue;

		if (x2 <= x1)
			continue;

		e_table_header_draw_button (drawable, ecol,
					    GTK_WIDGET (canvas)->style, ethi->font,
					    GTK_WIDGET_STATE (canvas),
					    GTK_WIDGET (canvas),
					    x1 - x, -y,
					    width, height,
					    x2 - x1, ethi->height,
					    (ETableColArrow) g_hash_table_lookup (
						    arrows, GINT_TO_POINTER (ecol->col_idx)));
	}

	g_hash_table_destroy (arrows);
}

static double
ethi_point (GnomeCanvasItem *item, double x, double y, int cx, int cy,
	    GnomeCanvasItem **actual_item)
{
	*actual_item = item;
	return 0.0;
}

/*
 * is_pointer_on_division:
 *
 * Returns whether @pos is a column header division;  If @the_total is not NULL,
 * then the actual position is returned here.  If @return_ecol is not NULL,
 * then the ETableCol that actually contains this point is returned here
 */
static gboolean
is_pointer_on_division (ETableHeaderItem *ethi, int pos, int *the_total, int *return_col)
{
	const int cols = e_table_header_count (ethi->eth);
	int col, total;

	total = 0;
	for (col = 0; col < cols; col++){
		ETableCol *ecol = e_table_header_get_column (ethi->eth, col);

		if (col == 0)
			total += ethi->group_indent_width;
		
		total += ecol->width;

		if ((total - TOLERANCE < pos)&& (pos < total + TOLERANCE)){
			if (return_col)
				*return_col = col;
			if (the_total)
				*the_total = total;

			return TRUE;
		}

		if (total > pos + TOLERANCE)
			return FALSE;
	}

	return FALSE;
}

#define convert(c,sx,sy,x,y) gnome_canvas_w2c (c,sx,sy,x,y)

static void
set_cursor (ETableHeaderItem *ethi, int pos)
{
	int col;
	GtkWidget *canvas = GTK_WIDGET (GNOME_CANVAS_ITEM (ethi)->canvas);
	gboolean resizable = FALSE;
		
	/* We might be invoked before we are realized */
	if (!canvas->window)
		return;

	if (is_pointer_on_division (ethi, pos, NULL, &col)) {
		int last_col = ethi->eth->col_count - 1;
		ETableCol *ecol = e_table_header_get_column (ethi->eth, col);

		/* Last column is not resizable */
		if (ecol->resizable && col != last_col) {
			int c = col + 1;

			/* Column is not resizable if all columns after it
			   are also not resizable */
			for (; c <= last_col; c++){
				ETableCol *ecol2;

				ecol2 = e_table_header_get_column (ethi->eth, c);
				if (ecol2->resizable) {
					resizable = TRUE;
					break;
				}
			}
		}
	}
	
	if (resizable)
		e_cursor_set (canvas->window, E_CURSOR_SIZE_X);
	else
		gdk_window_set_cursor (canvas->window, NULL);
	/*		e_cursor_set (canvas->window, E_CURSOR_ARROW);*/
}

static void
ethi_end_resize (ETableHeaderItem *ethi)
{
	ethi->resize_col = -1;
	ethi->resize_guide = GINT_TO_POINTER (0);

	gnome_canvas_item_request_update (GNOME_CANVAS_ITEM(ethi));
}

static gboolean
ethi_maybe_start_drag (ETableHeaderItem *ethi, GdkEventMotion *event)
{
	if (!ethi->maybe_drag)
		return FALSE;

	if (ethi->eth->col_count < 2) {
		ethi->maybe_drag = FALSE;
		return FALSE;
	}
		
	if (MAX (abs (ethi->click_x - event->x),
		 abs (ethi->click_y - event->y)) <= 3)
		return FALSE;

	return TRUE;
}

static void
ethi_start_drag (ETableHeaderItem *ethi, GdkEvent *event)
{
	GtkWidget *widget = GTK_WIDGET (GNOME_CANVAS_ITEM (ethi)->canvas);
	GtkTargetList *list;
	GdkDragContext *context;
	ETableCol *ecol;
	int col_width;
	GdkPixmap *pixmap;
	int group_indent = 0;
	GHashTable *arrows = g_hash_table_new (NULL, NULL);

	GtkTargetEntry  ethi_drag_types [] = {
		{ TARGET_ETABLE_COL_TYPE, 0, TARGET_ETABLE_COL_HEADER },
	};

	ethi->drag_col = ethi_find_col_by_x (ethi, event->motion.x);

	if (ethi->drag_col == -1)
		return;

	if (ethi->sort_info) {
		int length = e_table_sort_info_grouping_get_count(ethi->sort_info);
		int i;
		for (i = 0; i < length; i++) {
			ETableSortColumn column =
				e_table_sort_info_grouping_get_nth(
					ethi->sort_info, i);
			group_indent ++;
			g_hash_table_insert (
				arrows, 
				GINT_TO_POINTER (column.column),
				GINT_TO_POINTER (column.ascending ?
						 E_TABLE_COL_ARROW_UP : 
						 E_TABLE_COL_ARROW_DOWN));
		}
		length = e_table_sort_info_sorting_get_count(ethi->sort_info);
		for (i = 0; i < length; i++) {
			ETableSortColumn column =
				e_table_sort_info_sorting_get_nth (
					ethi->sort_info, i);

			g_hash_table_insert (
				arrows, 
				GINT_TO_POINTER (column.column),
				GINT_TO_POINTER (column.ascending ?
						 E_TABLE_COL_ARROW_UP : 
						 E_TABLE_COL_ARROW_DOWN));
		}
	}

	ethi_drag_types[0].target = g_strdup_printf(
		"%s-%s", ethi_drag_types[0].target, ethi->dnd_code);
	list = gtk_target_list_new (
		ethi_drag_types, ELEMENTS (ethi_drag_types));
	context = gtk_drag_begin (widget, list, GDK_ACTION_MOVE, 1, event);
	g_free(ethi_drag_types[0].target);

	ecol = e_table_header_get_column (ethi->eth, ethi->drag_col);
	col_width = ecol->width;
	pixmap = gdk_pixmap_new (widget->window, col_width, ethi->height, -1);

	e_table_header_draw_button (
		pixmap, ecol,
		widget->style, ethi->font,
		GTK_WIDGET_STATE (widget),
		widget,
		0, 0,
		col_width, ethi->height,
		col_width, ethi->height,
		(ETableColArrow) g_hash_table_lookup (
			arrows, GINT_TO_POINTER (ecol->col_idx)));
	gtk_drag_set_icon_pixmap (
		context,
		gdk_window_get_colormap (widget->window),
		pixmap,
		NULL,
		col_width / 2,
		ethi->height / 2);
	gdk_pixmap_unref (pixmap);

	ethi->maybe_drag = FALSE;
	g_hash_table_destroy (arrows);
}

typedef struct {
	ETableHeaderItem *ethi;
	int col;
} EthiHeaderInfo;

static void
ethi_popup_sort_ascending(GtkWidget *widget, EthiHeaderInfo *info)
{
	ETableCol *col;
	int model_col;
	int length;
	int i;
	int found = FALSE;
	ETableHeaderItem *ethi = info->ethi;

	col = e_table_header_get_column (ethi->eth, info->col);
	model_col = col->col_idx;

	length = e_table_sort_info_grouping_get_count(ethi->sort_info);
	for (i = 0; i < length; i++) {
		ETableSortColumn column = e_table_sort_info_grouping_get_nth (
			ethi->sort_info, i);

		if (model_col == column.column){
			column.ascending = 1;
			e_table_sort_info_grouping_set_nth (
				ethi->sort_info, i, column);
			found = 1;
			break;
		}
	}
	if (!found) {
		length = e_table_sort_info_sorting_get_count (
			ethi->sort_info);
		for (i = 0; i < length; i++) {
			ETableSortColumn column =
				e_table_sort_info_sorting_get_nth(
					ethi->sort_info, i);
			if (model_col == column.column){
				column.ascending = 1;
				e_table_sort_info_sorting_set_nth (
					ethi->sort_info, i, column);
				found = 1;
				break;
			}
		}
	}
	if (!found) {
		ETableSortColumn column;
		column.column = model_col;
		column.ascending =  1;
		length = e_table_sort_info_sorting_get_count(ethi->sort_info);
		if (length == 0)
			length++;
		e_table_sort_info_sorting_set_nth(ethi->sort_info, length - 1, column);
	}
}

static void
ethi_popup_sort_descending(GtkWidget *widget, EthiHeaderInfo *info)
{
	ETableCol *col;
	int model_col;
	int length;
	int i;
	int found = FALSE;
	ETableHeaderItem *ethi = info->ethi;

	col = e_table_header_get_column (ethi->eth, info->col);
	model_col = col->col_idx;

	length = e_table_sort_info_grouping_get_count(ethi->sort_info);
	for (i = 0; i < length; i++) {
		ETableSortColumn column = e_table_sort_info_grouping_get_nth(
			ethi->sort_info, i);
		if (model_col == column.column){
			column.ascending = 0;
			e_table_sort_info_grouping_set_nth(
				ethi->sort_info, i, column);
			found = 1;
			break;
		}
	}
	if (!found) {
		length = e_table_sort_info_sorting_get_count (ethi->sort_info);
		for (i = 0; i < length; i++) {
			ETableSortColumn column =
				e_table_sort_info_sorting_get_nth(
					ethi->sort_info, i);

			if (model_col == column.column){
				column.ascending = 0;
				e_table_sort_info_sorting_set_nth (
					ethi->sort_info, i, column);
				found = 1;
				break;
			}
		}
	}
	if (!found) {
		ETableSortColumn column;
		column.column = model_col;
		column.ascending = 0;
		length = e_table_sort_info_sorting_get_count (ethi->sort_info);
		if (length == 0)
			length++;
		e_table_sort_info_sorting_set_nth (
			ethi->sort_info, length - 1, column);
	}
}

static void
ethi_popup_unsort(GtkWidget *widget, EthiHeaderInfo *info)
{
	ETableHeaderItem *ethi = info->ethi;

	e_table_sort_info_grouping_truncate(ethi->sort_info, 0);
	e_table_sort_info_sorting_truncate(ethi->sort_info, 0);
}

static void
ethi_popup_group_field(GtkWidget *widget, EthiHeaderInfo *info)
{
	ETableCol *col;
	int model_col;
	ETableHeaderItem *ethi = info->ethi;
	ETableSortColumn column;

	col = e_table_header_get_column (ethi->eth, info->col);
	model_col = col->col_idx;

	column.column = model_col;
	column.ascending = 1;
	e_table_sort_info_grouping_set_nth(ethi->sort_info, 0, column);
	e_table_sort_info_grouping_truncate(ethi->sort_info, 1);
}

static void
ethi_popup_group_box(GtkWidget *widget, EthiHeaderInfo *info)
{
}

static void
ethi_popup_remove_column(GtkWidget *widget, EthiHeaderInfo *info)
{
	e_table_header_remove(info->ethi->eth, info->col);
}

static void
ethi_popup_field_chooser(GtkWidget *widget, EthiHeaderInfo *info)
{
	GtkWidget *etfcd = e_table_field_chooser_dialog_new();
	gtk_object_set(GTK_OBJECT(etfcd),
		       "full_header", info->ethi->full_header,
		       "header", info->ethi->eth,
		       "dnd_code", info->ethi->dnd_code,
		       NULL);
	gtk_widget_show(etfcd);
}

static void
ethi_popup_alignment(GtkWidget *widget, EthiHeaderInfo *info)
{
}

static void
ethi_popup_best_fit(GtkWidget *widget, EthiHeaderInfo *info)
{
	ETableHeaderItem *ethi = info->ethi;
	int width;

	gtk_signal_emit_by_name (GTK_OBJECT (ethi->eth),
				 "request_width",
				 info->col, &width);
	/* Add 10 to stop it from "..."ing */
	e_table_header_set_size (ethi->eth, info->col, width + 10);
	
	gnome_canvas_item_request_update (GNOME_CANVAS_ITEM(ethi));

}

static void
ethi_popup_format_columns(GtkWidget *widget, EthiHeaderInfo *info)
{
}

static void
config_destroyed (GtkObject *object, ETableHeaderItem *ethi)
{
	ethi->config = NULL;
}

static void
apply_changes (ETableConfig *config, ETableHeaderItem *ethi)
{
	char *state = e_table_state_save_to_string (config->state);

	if (ethi->table)
		e_table_set_state (ethi->table, state);
	if (ethi->tree)
		e_tree_set_state (ethi->tree, state);
	g_free (state);
}

static void
ethi_popup_customize_view(GtkWidget *widget, EthiHeaderInfo *info)
{
	ETableHeaderItem *ethi = info->ethi;
	ETableState *state;
	ETableSpecification *spec;

	if (ethi->config)
		e_table_config_raise (E_TABLE_CONFIG (ethi->config));
	else {
		if (ethi->table) {
			state = e_table_get_state_object(ethi->table);
			spec = ethi->table->spec;
		} else if (ethi->tree) {
			state = e_tree_get_state_object(ethi->tree);
			spec = e_tree_get_spec (ethi->tree);
		} else
			return;

		ethi->config = e_table_config_new (
				_("Configuring view: FIXME"),
				spec, state);
		gtk_signal_connect (
			GTK_OBJECT (ethi->config), "destroy",
			GTK_SIGNAL_FUNC (config_destroyed), ethi);
		gtk_signal_connect (
			GTK_OBJECT (ethi->config), "changed",
			GTK_SIGNAL_FUNC (apply_changes), ethi);
	}
}

/* Bit 1 is always disabled. */
/* Bit 2 is disabled if not "sortable". */
/* Bit 4 is disabled if we don't have a pointer to our table object. */
static EPopupMenu ethi_context_menu [] = {
	{ N_("Sort Ascending"),            NULL, GTK_SIGNAL_FUNC(ethi_popup_sort_ascending),  NULL, 2},
	{ N_("Sort Descending"),           NULL, GTK_SIGNAL_FUNC(ethi_popup_sort_descending), NULL, 2},
	{ N_("Unsort"),                    NULL, GTK_SIGNAL_FUNC(ethi_popup_unsort),          NULL, 0},
	{ "",                              NULL, GTK_SIGNAL_FUNC(NULL),                       NULL, 0},
	{ N_("Group By This Field"),       NULL, GTK_SIGNAL_FUNC(ethi_popup_group_field),     NULL, 16},
	{ N_("Group By Box"),              NULL, GTK_SIGNAL_FUNC(ethi_popup_group_box),       NULL, 128},
	{ "",                              NULL, GTK_SIGNAL_FUNC(NULL),                       NULL, 1},
	{ N_("Remove This Column"),        NULL, GTK_SIGNAL_FUNC(ethi_popup_remove_column),   NULL, 8},
	{ N_("Add a Column..."),           NULL, GTK_SIGNAL_FUNC(ethi_popup_field_chooser),   NULL, 0},
	{ "",                              NULL, GTK_SIGNAL_FUNC(NULL),                       NULL, 1},
	{ N_("Alignment"),                 NULL, GTK_SIGNAL_FUNC(ethi_popup_alignment),       NULL, 128},
	{ N_("Best Fit"),                  NULL, GTK_SIGNAL_FUNC(ethi_popup_best_fit),        NULL, 2},
	{ N_("Format Columns..."),         NULL, GTK_SIGNAL_FUNC(ethi_popup_format_columns),  NULL, 128},
	{ "",                              NULL, GTK_SIGNAL_FUNC(NULL),                       NULL, 1},
	{ N_("Customize Current View..."), NULL, GTK_SIGNAL_FUNC(ethi_popup_customize_view),  NULL, 4},
	{ NULL, NULL, NULL, NULL, 0 }
};

static void
ethi_header_context_menu (ETableHeaderItem *ethi, GdkEventButton *event)
{
	EthiHeaderInfo *info = g_new(EthiHeaderInfo, 1);
	ETableCol *col;
	info->ethi = ethi;
	info->col = ethi_find_col_by_x (ethi, event->x);
	col = e_table_header_get_column (ethi->eth, info->col);
	e_popup_menu_run (ethi_context_menu, (GdkEvent *) event,
			  1 +
			  (col->sortable ? 0 : 2) +
			  ((ethi->table || ethi->tree) ? 0 : 4) + 
			  ((e_table_header_count (ethi->eth) > 1) ? 0 : 8),
			  ((e_table_sort_info_get_can_group (ethi->sort_info)) ? 0 : 16) +
			  128, info);
}

static void
ethi_button_pressed (ETableHeaderItem *ethi, GdkEventButton *event)
{
	gtk_signal_emit (GTK_OBJECT (ethi),
			 ethi_signals [BUTTON_PRESSED], event);
}

static void
ethi_change_sort_state (ETableHeaderItem *ethi, gdouble x)
{
	ETableCol *col;
	int model_col;
	int length;
	int i;
	int found = FALSE;
	
	col = e_table_header_get_column (ethi->eth, ethi_find_col_by_x (ethi, x));

	if (col == NULL)
		return;

	model_col = col->col_idx;
	
	length = e_table_sort_info_grouping_get_count(ethi->sort_info);
	for (i = 0; i < length; i++) {
		ETableSortColumn column = e_table_sort_info_grouping_get_nth(ethi->sort_info, i);
		if (model_col == column.column){
			int ascending = column.ascending;
			ascending = ! ascending;
			column.ascending = ascending;
			e_table_sort_info_grouping_set_nth(ethi->sort_info, i, column);
			found = 1;
			break;
		}
	}
	
	if (!col->sortable)
		return;
	
	if (!found) {
		length = e_table_sort_info_sorting_get_count(ethi->sort_info);
		for (i = 0; i < length; i++) {
			ETableSortColumn column = e_table_sort_info_sorting_get_nth(ethi->sort_info, i);

			if (model_col == column.column){
				int ascending = column.ascending;
				
				if (ascending == 0){
					/*
					 * This means the user has clicked twice
					 * already, lets kill sorting now.
					 */
					e_table_sort_info_sorting_truncate (ethi->sort_info, i);
				} else {
					ascending = !ascending;
					column.ascending = ascending;
					e_table_sort_info_sorting_set_nth(ethi->sort_info, i, column);
				}
				found = 1;
				break;
			}
		}
	}

	if (!found) {
		ETableSortColumn column;
		column.column = model_col;
		column.ascending = 1;
		length = e_table_sort_info_sorting_get_count(ethi->sort_info);
		if (length == 0)
			length++;
		e_table_sort_info_sorting_set_nth(ethi->sort_info, length - 1, column);
	}
}

/*
 * Handles the events on the ETableHeaderItem, particularly it handles resizing
 */
static int
ethi_event (GnomeCanvasItem *item, GdkEvent *e)
{
	ETableHeaderItem *ethi = E_TABLE_HEADER_ITEM (item);
	GnomeCanvas *canvas = item->canvas;
	const gboolean resizing = ETHI_RESIZING (ethi);
	int x, y, start, col;
	int was_maybe_drag = 0;
	
	switch (e->type){
	case GDK_ENTER_NOTIFY:
		convert (canvas, e->crossing.x, e->crossing.y, &x, &y);
		set_cursor (ethi, x);
		break;

	case GDK_LEAVE_NOTIFY:
		gdk_window_set_cursor (GTK_WIDGET (canvas)->window, NULL);
		/*		e_cursor_set (GTK_WIDGET (canvas)->window, E_CURSOR_ARROW);*/
		break;
			    
	case GDK_MOTION_NOTIFY:

		convert (canvas, e->motion.x, e->motion.y, &x, &y);
		if (resizing){
			int new_width;
			
			if (ethi->resize_guide == NULL){
				/* Quick hack until I actually bind the views */
				ethi->resize_guide = GINT_TO_POINTER (1);
				
				gnome_canvas_item_grab (item,
							GDK_POINTER_MOTION_MASK |
							GDK_BUTTON_RELEASE_MASK,
							e_cursor_get (E_CURSOR_SIZE_X),
							e->button.time);
			}

			new_width = x - ethi->resize_start_pos;

			e_table_header_set_size (ethi->eth, ethi->resize_col, new_width);

			gnome_canvas_item_request_update (GNOME_CANVAS_ITEM(ethi));
		} else if (ethi_maybe_start_drag (ethi, &e->motion)){
			ethi_start_drag (ethi, e);
		} else
			set_cursor (ethi, x);
		break;
		
	case GDK_BUTTON_PRESS:
		if (e->button.button > 3)
			return FALSE;

		convert (canvas, e->button.x, e->button.y, &x, &y);
		    
		if (is_pointer_on_division (ethi, x, &start, &col) && e->button.button == 1){
			ETableCol *ecol;
				
				/*
				 * Record the important bits.
				 *
				 * By setting resize_pos to a non -1 value,
				 * we know that we are being resized (used in the
				 * other event handlers).
				 */
			ecol = e_table_header_get_column (ethi->eth, col);
			
			if (!ecol->resizable)
				break;
			ethi->resize_col = col;
			ethi->resize_start_pos = start - ecol->width;
			ethi->resize_min_width = ecol->min_width;
		} else {
			if (e->button.button == 1){
				ethi->click_x = e->button.x;
				ethi->click_y = e->button.y;
				ethi->maybe_drag = TRUE;
			} else if (e->button.button == 3){
				ethi_header_context_menu (ethi, &e->button);
			} else
				ethi_button_pressed (ethi, &e->button);
		}
		break;
		
	case GDK_2BUTTON_PRESS:
		if (!resizing)
			break;
		
		if (e->button.button != 1)
			break;
		else {
			int width = 0;
			gtk_signal_emit_by_name (GTK_OBJECT (ethi->eth),
						 "request_width",
						 (int)ethi->resize_col, &width);
			/* Add 10 to stop it from "..."ing */
			e_table_header_set_size (ethi->eth, ethi->resize_col, width + 10);

			gnome_canvas_item_request_update (GNOME_CANVAS_ITEM(ethi));
			ethi->maybe_drag = FALSE;
		}
		break;
		
	case GDK_BUTTON_RELEASE: {
		gboolean needs_ungrab = FALSE;
		
		was_maybe_drag = ethi->maybe_drag;
		
		ethi->maybe_drag = FALSE;

		if (ethi->resize_col != -1){
			needs_ungrab = (ethi->resize_guide != NULL);
			ethi_end_resize (ethi);
		} else if (was_maybe_drag && ethi->sort_info) 
			ethi_change_sort_state (ethi, e->button.x);
		
		if (needs_ungrab)
			gnome_canvas_item_ungrab (item, e->button.time);

		break;
	}
	
	default:
		return FALSE;
	}
	return TRUE;
}

static void
ethi_class_init (GtkObjectClass *object_class)
{
	GnomeCanvasItemClass *item_class = (GnomeCanvasItemClass *) object_class;

	ethi_parent_class = gtk_type_class (PARENT_OBJECT_TYPE);
	
	object_class->destroy = ethi_destroy;
	object_class->set_arg = ethi_set_arg;
	object_class->get_arg = ethi_get_arg;

	item_class->update      = ethi_update;
	item_class->realize     = ethi_realize;
	item_class->unrealize   = ethi_unrealize;
	item_class->draw        = ethi_draw;
	item_class->point       = ethi_point;
	item_class->event       = ethi_event;
	
	gtk_object_add_arg_type ("ETableHeaderItem::ETableHeader", GTK_TYPE_OBJECT,
				 GTK_ARG_WRITABLE, ARG_TABLE_HEADER);
	gtk_object_add_arg_type ("ETableHeaderItem::full_header", GTK_TYPE_OBJECT,
				 GTK_ARG_READWRITE, ARG_FULL_HEADER);
	gtk_object_add_arg_type ("ETableHeaderItem::dnd_code", GTK_TYPE_STRING,
				 GTK_ARG_READWRITE, ARG_DND_CODE);
	gtk_object_add_arg_type ("ETableHeaderItem::fontset", GTK_TYPE_STRING,
				 GTK_ARG_WRITABLE, ARG_TABLE_FONTSET);
	gtk_object_add_arg_type ("ETableHeaderItem::sort_info", GTK_TYPE_OBJECT,
				 GTK_ARG_WRITABLE, ARG_SORT_INFO);
	gtk_object_add_arg_type ("ETableHeaderItem::table", GTK_TYPE_OBJECT,
				 GTK_ARG_WRITABLE, ARG_TABLE);
	gtk_object_add_arg_type ("ETableHeaderItem::tree", E_TREE_TYPE,
				 GTK_ARG_WRITABLE, ARG_TREE);

	/*
	 * Create our pixmaps for DnD
	 */
	dnd_colormap = gtk_widget_get_default_colormap ();
	remove_col_pixmap = gdk_pixmap_colormap_create_from_xpm_d (
		NULL, dnd_colormap,
		&remove_col_mask, NULL, remove_col_xpm);

	add_col_pixmap = gdk_pixmap_colormap_create_from_xpm_d (
		NULL, dnd_colormap,
		&add_col_mask, NULL, add_col_xpm);

	ethi_signals [BUTTON_PRESSED] =
		gtk_signal_new ("button_pressed",
				GTK_RUN_LAST,
				E_OBJECT_CLASS_TYPE (object_class),
				GTK_SIGNAL_OFFSET (ETableHeaderItemClass, button_pressed),
				gtk_marshal_NONE__POINTER,
				GTK_TYPE_NONE, 1, GTK_TYPE_GDK_EVENT);
		
	E_OBJECT_CLASS_ADD_SIGNALS (object_class, ethi_signals, LAST_SIGNAL);
}

static void
ethi_init (GnomeCanvasItem *item)
{
	ETableHeaderItem *ethi = E_TABLE_HEADER_ITEM (item);

	ethi->resize_col = -1;

	item->x1 = 0;
	item->y1 = 0;
	item->x2 = 0;
	item->y2 = 0;

	ethi->drag_col = -1;
	ethi->drag_mark = -1;
	
	ethi->sort_info = NULL;

	ethi->sort_info_changed_id = 0;
	ethi->group_info_changed_id = 0;

	ethi->group_indent_width = 0;
	ethi->table = NULL;
	ethi->tree = NULL;
}

GtkType
e_table_header_item_get_type (void)
{
	static GtkType type = 0;

	if (!type){
		GtkTypeInfo info = {
			"ETableHeaderItem",
			sizeof (ETableHeaderItem),
			sizeof (ETableHeaderItemClass),
			(GtkClassInitFunc) ethi_class_init,
			(GtkObjectInitFunc) ethi_init,
			NULL, /* reserved 1 */
			NULL, /* reserved 2 */
			(GtkClassInitFunc) NULL
		};

		type = gtk_type_unique (PARENT_OBJECT_TYPE, &info);
	}

	return type;
}

