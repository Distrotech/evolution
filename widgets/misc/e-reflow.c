/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * e-reflow.c
 * Copyright 2000, 2001, Ximian, Inc.
 *
 * Authors:
 *   Chris Lahey <clahey@ximian.com>
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

#include "e-reflow.h"

#include <math.h>
#include <gdk/gdkkeysyms.h>
#include "e-canvas-utils.h"
#include "e-canvas.h"
#include "gal/e-text/e-text.h"
#include "gal/util/e-util.h"
#include "gal/widgets/e-unicode.h"
#include <gtk/gtksignal.h>
#include "e-selection-model-simple.h"

#include <string.h>

static gboolean e_reflow_event (GnomeCanvasItem *item, GdkEvent *event);
static void e_reflow_realize (GnomeCanvasItem *item);
static void e_reflow_unrealize (GnomeCanvasItem *item);
static void e_reflow_draw (GnomeCanvasItem *item, GdkDrawable *drawable,
				    int x, int y, int width, int height);
static void e_reflow_update (GnomeCanvasItem *item, double affine[6], ArtSVP *clip_path, gint flags);
static double e_reflow_point (GnomeCanvasItem *item, double x, double y, int cx, int cy, GnomeCanvasItem **actual_item);
static void e_reflow_reflow (GnomeCanvasItem *item, int flags);
static void set_empty(EReflow *reflow);

static void e_reflow_resize_children (GnomeCanvasItem *item);

#define E_REFLOW_DIVIDER_WIDTH 2
#define E_REFLOW_BORDER_WIDTH 7
#define E_REFLOW_FULL_GUTTER (E_REFLOW_DIVIDER_WIDTH + E_REFLOW_BORDER_WIDTH * 2)

static GnomeCanvasGroupClass *parent_class = NULL;

/* The arguments we take */
enum {
	ARG_0,
	ARG_MINIMUM_WIDTH,
	ARG_WIDTH,
	ARG_HEIGHT,
	ARG_EMPTY_MESSAGE,
	ARG_MODEL,
	ARG_COLUMN_WIDTH,
};

enum {
	SELECTION_EVENT,
	LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = {0, };

static gint
er_compare (int i1, int i2, gpointer user_data)
{
	EReflow *reflow = user_data;
	return e_reflow_model_compare (reflow->model, i1, i2);
}

static gint
e_reflow_pick_line (EReflow *reflow, double x)
{
	x += E_REFLOW_BORDER_WIDTH + E_REFLOW_DIVIDER_WIDTH;
	x /= reflow->column_width + E_REFLOW_FULL_GUTTER;
	return x;
}

static int
er_find_item (EReflow *reflow, GnomeCanvasItem *item)
{
	int i;
	for (i = 0; i < reflow->count; i++) {
		if (reflow->items[i] == item)
			return i;
	}
	return -1;
}

static void
e_reflow_resize_children (GnomeCanvasItem *item)
{
	EReflow *reflow;
	int i;
	int count;

	reflow = E_REFLOW (item);

	count = reflow->count;
	for (i = 0; i < count; i++) {
		if (reflow->items[i])
			gnome_canvas_item_set(reflow->items[i],
					      "width", (double) reflow->column_width,
					      NULL);
	}
}

static inline void
e_reflow_update_selection_row (EReflow *reflow, int row)
{
	if (reflow->items[row]) {
		gtk_object_set(GTK_OBJECT(reflow->items[row]),
			       "selected", e_selection_model_is_row_selected(E_SELECTION_MODEL(reflow->selection), row),
			       NULL);
	} else if (e_selection_model_is_row_selected (E_SELECTION_MODEL (reflow->selection), row)) {
		reflow->items[row] = e_reflow_model_incarnate (reflow->model, row, GNOME_CANVAS_GROUP (reflow));
		gtk_object_set (GTK_OBJECT (reflow->items[row]),
				"selected", e_selection_model_is_row_selected(E_SELECTION_MODEL(reflow->selection), row),
				"width", (double) reflow->column_width,
				NULL);
	}
}

static void
e_reflow_update_selection (EReflow *reflow)
{
	int i;
	int count;

	count = reflow->count;
	for (i = 0; i < count; i++) {
		e_reflow_update_selection_row (reflow, i);
	}
}

static void
selection_changed (ESelectionModel *selection, EReflow *reflow)
{
	e_reflow_update_selection (reflow);
}

static void
selection_row_changed (ESelectionModel *selection, int row, EReflow *reflow)
{
	e_reflow_update_selection_row (reflow, row);
}

static void
cursor_changed (ESelectionModel *selection, int row, int col, EReflow *reflow)
{
	int count = reflow->count;
	int old_cursor = reflow->cursor_row;

	if (old_cursor < count && old_cursor >= 0) {
		if (reflow->items[old_cursor]) {
			gtk_object_set (GTK_OBJECT (reflow->items[old_cursor]),
					"has_cursor", FALSE,
					NULL);
		}
	}

	reflow->cursor_row = row;

	if (row < count && row >= 0) {
		if (reflow->items[row]) {
			gtk_object_set (GTK_OBJECT (reflow->items[row]),
					"has_cursor", TRUE,
					NULL);
		} else {
			reflow->items[row] = e_reflow_model_incarnate (reflow->model, row, GNOME_CANVAS_GROUP (reflow));
			gtk_object_set (GTK_OBJECT (reflow->items[row]),
					"has_cursor", TRUE,
					"width", (double) reflow->column_width,
					NULL);
		}
	}
}

static void
incarnate (EReflow *reflow)
{
	int column_width;
	int first_column;
	int last_column;
	int first_cell;
	int last_cell;
	int i;
	GtkAdjustment *adjustment = gtk_layout_get_hadjustment (GTK_LAYOUT (GNOME_CANVAS_ITEM (reflow)->canvas));

	column_width = reflow->column_width;

	first_column = adjustment->value - 1 + E_REFLOW_BORDER_WIDTH;
	first_column /= column_width + E_REFLOW_FULL_GUTTER;

	last_column = adjustment->value + adjustment->page_size + 1 - E_REFLOW_BORDER_WIDTH - E_REFLOW_DIVIDER_WIDTH;
	last_column /= column_width + E_REFLOW_FULL_GUTTER;
	last_column ++;

	if (first_column >= 0 && first_column < reflow->column_count)
		first_cell = reflow->columns[first_column];
	else
		first_cell = 0;

	if (last_column >= 0 && last_column < reflow->column_count)
		last_cell = reflow->columns[last_column];
	else
		last_cell = reflow->count;

	for (i = first_cell; i < last_cell; i++) {
		int unsorted = e_sorter_sorted_to_model (E_SORTER (reflow->sorter), i);
		if (reflow->items[unsorted] == NULL) {
			if (reflow->model) {
				reflow->items[unsorted] = e_reflow_model_incarnate (reflow->model, unsorted, GNOME_CANVAS_GROUP (reflow));
				gtk_object_set (GTK_OBJECT (reflow->items[unsorted]),
						"selected", e_selection_model_is_row_selected(E_SELECTION_MODEL(reflow->selection), unsorted),
						"width", (double) reflow->column_width,
						NULL);
			}
		}
	}
	reflow->incarnate_idle_id = 0;
}

static gboolean
invoke_incarnate (gpointer user_data)
{
	EReflow *reflow = user_data;
	incarnate (reflow);
	return FALSE;
}

static void
queue_incarnate (EReflow *reflow)
{
	if (reflow->incarnate_idle_id == 0)
		reflow->incarnate_idle_id =
			g_idle_add_full (25, invoke_incarnate, reflow, NULL);
}

static void
reflow_columns (EReflow *reflow)
{
	GSList *list;
	int count;
	int i;
	int column_count;
	double running_height;

	g_free (reflow->columns);
	reflow->column_count = 0;
	reflow->columns      = NULL;

	list = NULL;

	running_height = E_REFLOW_BORDER_WIDTH;
	column_count = 1;

	count = reflow->count;
	for (i = 0; i < count; i++) {
		int unsorted = e_sorter_sorted_to_model (E_SORTER (reflow->sorter), i);
		if (reflow->heights[unsorted] == -1) {
			if (reflow->model)
				reflow->heights[unsorted] = e_reflow_model_height (reflow->model, unsorted, GNOME_CANVAS_GROUP (reflow));
			else
				reflow->heights[unsorted] = 0;
		}
		if (i != 0 && running_height + reflow->heights[unsorted] + E_REFLOW_BORDER_WIDTH > reflow->height) {
			list = g_slist_prepend (list, GINT_TO_POINTER(i));
			column_count ++;
			running_height = E_REFLOW_BORDER_WIDTH * 2 + reflow->heights[unsorted];
		} else
			running_height += reflow->heights[unsorted] + E_REFLOW_BORDER_WIDTH;
	}

	reflow->column_count = column_count;
	reflow->columns = g_new (int, column_count);
	column_count --;
	for (; column_count > 0; column_count--) {
		GSList *to_free;
		reflow->columns[column_count] = GPOINTER_TO_INT(list->data);
		to_free = list;
		list = list->next;
		g_slist_free_1 (to_free);
	}
	reflow->columns[0] = 0;

	queue_incarnate (reflow);

	reflow->need_reflow_columns = FALSE;
}

static void
item_changed (EReflowModel *model, int i, EReflow *reflow)
{
	if (i < 0 || i >= reflow->count)
		return;

	reflow->heights[i] = -1;
	if (reflow->items[i] != NULL)
		e_reflow_model_reincarnate (model, i, reflow->items[i]);
	e_sorter_array_clean (reflow->sorter);
	reflow->need_reflow_columns = TRUE;
	e_canvas_item_request_reflow(GNOME_CANVAS_ITEM (reflow));
}

static void
items_inserted (EReflowModel *model, int position, int count, EReflow *reflow)
{
	int i;
	int oldcount;
	if (position < 0 || position > reflow->count)
		return;

	oldcount = reflow->count;

	reflow->count += count;

	if (reflow->count > reflow->allocated_count) {
		while (reflow->count > reflow->allocated_count)
			reflow->allocated_count += 256;
		reflow->heights = g_renew (int, reflow->heights, reflow->allocated_count);
		reflow->items = g_renew (GnomeCanvasItem *, reflow->items, reflow->allocated_count);
	}
	memmove (reflow->heights + position + count, reflow->heights + position, (reflow->count - position - count) * sizeof (int));
	memmove (reflow->items + position + count, reflow->items + position, (reflow->count - position - count) * sizeof (GnomeCanvasItem *));
	for (i = position; i < position + count; i++) {
		reflow->items[i] = 0;
		reflow->heights[i] = -1;
	}

	e_selection_model_simple_set_row_count (E_SELECTION_MODEL_SIMPLE (reflow->selection), reflow->count);
	if (position == oldcount)
		e_sorter_array_append (reflow->sorter, count);
	else
		e_sorter_array_set_count (reflow->sorter, reflow->count);
	reflow->need_reflow_columns = TRUE;
	set_empty (reflow);
	e_canvas_item_request_reflow(GNOME_CANVAS_ITEM (reflow));
}

static void
model_changed (EReflowModel *model, EReflow *reflow)
{
	int i;
	int count;
	int oldcount;

	count = reflow->count;
	oldcount = count;

	for (i = 0; i < count; i++) {
		if (reflow->items[i])
			gtk_object_destroy (GTK_OBJECT (reflow->items[i]));
	}
	g_free (reflow->items);
	g_free (reflow->heights);
	reflow->count = e_reflow_model_count (model);
	reflow->allocated_count = reflow->count;
	reflow->items = g_new (GnomeCanvasItem *, reflow->count);
	reflow->heights = g_new (int, reflow->count);

	count = reflow->count;
	for (i = 0; i < count; i++) {
		reflow->items[i] = 0;
		reflow->heights[i] = -1;
	}

	e_selection_model_simple_set_row_count (E_SELECTION_MODEL_SIMPLE (reflow->selection), count);
	e_sorter_array_set_count (reflow->sorter, reflow->count);

	reflow->need_reflow_columns = TRUE;
	if (oldcount > reflow->count)
		reflow_columns (reflow);
	set_empty (reflow);
	e_canvas_item_request_reflow(GNOME_CANVAS_ITEM (reflow));
}

static void
set_empty(EReflow *reflow)
{
	if (reflow->count == 0) {
		if (reflow->empty_text) {
			if (reflow->empty_message) {
				char *empty_message = e_utf8_to_gtk_string (GTK_WIDGET (GNOME_CANVAS_ITEM (reflow)->canvas), reflow->empty_message);
				gnome_canvas_item_set(reflow->empty_text,
						      "width", reflow->minimum_width,
						      "text", empty_message,
						      NULL);
				e_canvas_item_move_absolute(reflow->empty_text,
							    reflow->minimum_width / 2,
							    0);
				g_free (empty_message);
			} else {
				gtk_object_destroy(GTK_OBJECT(reflow->empty_text));
				reflow->empty_text = NULL;
			}
		} else {
			if (reflow->empty_message) {
				char *empty_message = e_utf8_to_gtk_string (GTK_WIDGET (GNOME_CANVAS_ITEM (reflow)->canvas), reflow->empty_message);
				reflow->empty_text =
					gnome_canvas_item_new(GNOME_CANVAS_GROUP(reflow),
							      e_text_get_type(),
							      "anchor", GTK_ANCHOR_N,
							      "width", reflow->minimum_width,
							      "clip", TRUE,
							      "use_ellipsis", TRUE,
							      "font_gdk", GTK_WIDGET(GNOME_CANVAS_ITEM(reflow)->canvas)->style->font,
							      "fill_color", "black",
							      "justification", GTK_JUSTIFY_CENTER,
							      "text", empty_message,
							      "draw_background", FALSE,
							      NULL);
				g_free (empty_message);
				e_canvas_item_move_absolute(reflow->empty_text,
							    reflow->minimum_width / 2,
							    0);
			}
		}
	} else {
		if (reflow->empty_text) {
			gtk_object_destroy(GTK_OBJECT(reflow->empty_text));
			reflow->empty_text = NULL;
		}
	}
}

static void
disconnect_model (EReflow *reflow)
{
	if (reflow->model == NULL)
		return;

	gtk_signal_disconnect (GTK_OBJECT (reflow->model),
			       reflow->model_changed_id);
	gtk_signal_disconnect (GTK_OBJECT (reflow->model),
			       reflow->model_items_inserted_id);
	gtk_signal_disconnect (GTK_OBJECT (reflow->model),
			       reflow->model_item_changed_id);
	gtk_object_unref (GTK_OBJECT (reflow->model));

	reflow->model_changed_id        = 0;
	reflow->model_items_inserted_id = 0;
	reflow->model_item_changed_id   = 0;
	reflow->model                   = NULL;
}

static void
disconnect_selection (EReflow *reflow)
{
	if (reflow->selection == NULL)
		return;

	gtk_signal_disconnect (GTK_OBJECT (reflow->selection),
			       reflow->selection_changed_id);
	gtk_signal_disconnect (GTK_OBJECT (reflow->selection),
			       reflow->selection_row_changed_id);
	gtk_signal_disconnect (GTK_OBJECT (reflow->selection),
			       reflow->cursor_changed_id);
	gtk_object_unref (GTK_OBJECT (reflow->selection));

	reflow->selection_changed_id = 0;
	reflow->selection_row_changed_id = 0;
	reflow->cursor_changed_id = 0;
	reflow->selection            = NULL;
}

static void
connect_model (EReflow *reflow, EReflowModel *model)
{
	if (reflow->model != NULL)
		disconnect_model (reflow);

	if (model == NULL)
		return;

	reflow->model = model;
	gtk_object_ref (GTK_OBJECT (reflow->model));
	reflow->model_changed_id =
		gtk_signal_connect (GTK_OBJECT (reflow->model), "model_changed",
				    GTK_SIGNAL_FUNC (model_changed), reflow);
	reflow->model_items_inserted_id =
		gtk_signal_connect (GTK_OBJECT (reflow->model), "model_items_inserted",
				    GTK_SIGNAL_FUNC (items_inserted), reflow);
	reflow->model_item_changed_id =
		gtk_signal_connect (GTK_OBJECT (reflow->model), "model_item_changed",
				    GTK_SIGNAL_FUNC (item_changed), reflow);
	model_changed (model, reflow);
}

static void
adjustment_changed (GtkAdjustment *adjustment, EReflow *reflow)
{
	incarnate (reflow);
}

static void
disconnect_adjustment (EReflow *reflow)
{
	if (reflow->adjustment == NULL)
		return;

	gtk_signal_disconnect (GTK_OBJECT (reflow->adjustment),
			       reflow->adjustment_changed_id);
	gtk_signal_disconnect (GTK_OBJECT (reflow->adjustment),
			       reflow->adjustment_value_changed_id);

	gtk_object_unref (GTK_OBJECT (reflow->adjustment));

	reflow->adjustment_changed_id = 0;
	reflow->adjustment_value_changed_id = 0;
	reflow->adjustment = NULL;
}

static void
connect_adjustment (EReflow *reflow, GtkAdjustment *adjustment)
{
	if (reflow->adjustment != NULL)
		disconnect_adjustment (reflow);

	if (adjustment == NULL)
		return;

	reflow->adjustment = adjustment;
	reflow->adjustment_changed_id =
		gtk_signal_connect (GTK_OBJECT (adjustment), "changed",
				    adjustment_changed, reflow);
	reflow->adjustment_value_changed_id =
		gtk_signal_connect (GTK_OBJECT (adjustment), "value_changed",
				    adjustment_changed, reflow);
	gtk_object_ref (GTK_OBJECT (adjustment));
}

#if 0
static void
set_scroll_adjustments (GtkLayout *layout, GtkAdjustment *hadj, GtkAdjustment *vadj, EReflow *reflow)
{
	connect_adjustment (reflow, hadj);
}

static void
connect_set_adjustment (EReflow *reflow)
{
	reflow->set_scroll_adjustments_id =
		gtk_signal_connect (GTK_OBJECT (GNOME_CANVAS_ITEM (reflow)->canvas),
				    "set_scroll_adjustments",
				    GTK_SIGNAL_FUNC (set_scroll_adjustments), reflow);
}
#endif

static void
disconnect_set_adjustment (EReflow *reflow)
{
	if (reflow->set_scroll_adjustments_id != 0) {
		gtk_signal_disconnect (GTK_OBJECT (GNOME_CANVAS_ITEM (reflow)->canvas),
				       reflow->set_scroll_adjustments_id);
		reflow->set_scroll_adjustments_id = 0;
	}
}




/* Virtual functions */
static void
e_reflow_set_arg (GtkObject *o, GtkArg *arg, guint arg_id)
{
	GnomeCanvasItem *item;
	EReflow *reflow;

	item = GNOME_CANVAS_ITEM (o);
	reflow = E_REFLOW (o);
	
	switch (arg_id){
	case ARG_HEIGHT:
		reflow->height = GTK_VALUE_DOUBLE (*arg);
		reflow->need_reflow_columns = TRUE;
		e_canvas_item_request_reflow(item);
		break;
	case ARG_MINIMUM_WIDTH:
		reflow->minimum_width = GTK_VALUE_DOUBLE (*arg);
		if (GNOME_CANVAS_ITEM_REALIZED & GTK_OBJECT_FLAGS(o))
			set_empty(reflow);
		e_canvas_item_request_reflow(item);
		break;
	case ARG_EMPTY_MESSAGE:
		g_free(reflow->empty_message);
		reflow->empty_message = g_strdup(GTK_VALUE_STRING (*arg));
		if (GNOME_CANVAS_ITEM_REALIZED & GTK_OBJECT_FLAGS(o))
			set_empty(reflow);
		break;
	case ARG_MODEL:
		connect_model (reflow, (EReflowModel *) GTK_VALUE_OBJECT (*arg));
		break;
	case ARG_COLUMN_WIDTH:
		if (reflow->column_width != GTK_VALUE_INT (*arg)) {
			GtkAdjustment *adjustment = gtk_layout_get_hadjustment(GTK_LAYOUT(item->canvas));

			reflow->column_width = GTK_VALUE_INT (*arg);
			adjustment->step_increment = (reflow->column_width + E_REFLOW_FULL_GUTTER) / 2;
			adjustment->page_increment = adjustment->page_size - adjustment->step_increment;
			gtk_adjustment_changed(adjustment);
			e_reflow_resize_children(item);
			e_canvas_item_request_reflow(item);

			reflow->need_column_resize = TRUE;
			gnome_canvas_item_request_update(item);
		}
		break;
	}
}

static void
e_reflow_get_arg (GtkObject *object, GtkArg *arg, guint arg_id)
{
	EReflow *reflow;

	reflow = E_REFLOW (object);

	switch (arg_id) {
	case ARG_MINIMUM_WIDTH:
		GTK_VALUE_DOUBLE (*arg) = reflow->minimum_width;
		break;
	case ARG_WIDTH:
		GTK_VALUE_DOUBLE (*arg) = reflow->width;
		break;
	case ARG_HEIGHT:
		GTK_VALUE_DOUBLE (*arg) = reflow->height;
		break;
	case ARG_EMPTY_MESSAGE:
		GTK_VALUE_STRING (*arg) = g_strdup(reflow->empty_message);
		break;
	case ARG_MODEL:
		GTK_VALUE_OBJECT (*arg) = (GtkObject *) reflow->model;
		break;
	case ARG_COLUMN_WIDTH:
		GTK_VALUE_INT (*arg) = reflow->column_width;
		break;
	default:
		arg->type = GTK_TYPE_INVALID;
		break;
	}
}

static void
e_reflow_destroy (GtkObject *object)
{
	EReflow *reflow = E_REFLOW(object);

	g_free (reflow->items);
	g_free (reflow->heights);
	g_free (reflow->columns);

	reflow->items          = NULL;
	reflow->heights        = NULL;
	reflow->columns        = NULL;
	reflow->count          = 0;
	reflow->allocated_count = 0;

	if (reflow->incarnate_idle_id != 0)
		g_source_remove (reflow->incarnate_idle_id);

	disconnect_model (reflow);
	disconnect_selection (reflow);
	
	g_free(reflow->empty_message);
  
	GTK_OBJECT_CLASS(parent_class)->destroy (object);
}

static void
e_reflow_realize (GnomeCanvasItem *item)
{
	EReflow *reflow;
	GnomeCanvasGroup *group;
	GtkAdjustment *adjustment;
	int count;
	int i;

	reflow = E_REFLOW (item);
	group = GNOME_CANVAS_GROUP (item);

	if (GNOME_CANVAS_ITEM_CLASS(parent_class)->realize)
		(* GNOME_CANVAS_ITEM_CLASS(parent_class)->realize) (item);
	
	reflow->arrow_cursor = gdk_cursor_new (GDK_SB_H_DOUBLE_ARROW);
	reflow->default_cursor = gdk_cursor_new (GDK_LEFT_PTR);

	count = reflow->count;
	for(i = 0; i < count; i++) {
		if (reflow->items[i])
			gnome_canvas_item_set(reflow->items[i],
					      "width", (double) reflow->column_width,
					      NULL);
	}

	set_empty(reflow);

	reflow->need_reflow_columns = TRUE;
	e_canvas_item_request_reflow(item);

	adjustment = gtk_layout_get_hadjustment(GTK_LAYOUT(item->canvas));

#if 0
	connect_set_adjustment (reflow);
#endif
	connect_adjustment (reflow, adjustment);

	adjustment->step_increment = (reflow->column_width + E_REFLOW_FULL_GUTTER) / 2;
	adjustment->page_increment = adjustment->page_size - adjustment->step_increment;
	gtk_adjustment_changed(adjustment);
	
	if (!item->canvas->aa) {
	}
}

static void
e_reflow_unrealize (GnomeCanvasItem *item)
{
	EReflow *reflow;

	reflow = E_REFLOW (item);

	if (!item->canvas->aa) {
	}
  
	gdk_cursor_destroy (reflow->arrow_cursor);
	gdk_cursor_destroy (reflow->default_cursor);
	reflow->arrow_cursor = NULL;
	reflow->default_cursor = NULL;

	g_free (reflow->columns);
	reflow->columns = NULL;

	disconnect_set_adjustment (reflow);
	disconnect_adjustment (reflow);

	if (GNOME_CANVAS_ITEM_CLASS(parent_class)->unrealize)
		(* GNOME_CANVAS_ITEM_CLASS(parent_class)->unrealize) (item);
}

static gboolean
e_reflow_event (GnomeCanvasItem *item, GdkEvent *event)
{
	EReflow *reflow;
	int return_val = FALSE;
 
	reflow = E_REFLOW (item);

	switch( event->type )
		{
		case GDK_KEY_PRESS:
			return_val = e_selection_model_key_press(reflow->selection, (GdkEventKey *) event);
			break;
#if 0
			if (event->key.keyval == GDK_Tab || 
			    event->key.keyval == GDK_KP_Tab || 
			    event->key.keyval == GDK_ISO_Left_Tab) {
				int i;
				int count;
				count = reflow->count;
				for (i = 0; i < count; i++) {
					int unsorted = e_sorter_sorted_to_model (E_SORTER (reflow->sorter), i);
					GnomeCanvasItem *item = reflow->items[unsorted];
					EFocus has_focus;
					if (item) {
						gtk_object_get(GTK_OBJECT(item),
							       "has_focus", &has_focus,
							       NULL);
						if (has_focus) {
							if (event->key.state & GDK_SHIFT_MASK) {
								if (i == 0)
									return 0;
								i--;
							} else {
								if (i == count - 1)
									return 0;
								i++;
							}
							
							unsorted = e_sorter_sorted_to_model (E_SORTER (reflow->sorter), i);
							if (reflow->items[unsorted] == NULL) {
								reflow->items[unsorted] = e_reflow_model_incarnate (reflow->model, unsorted, GNOME_CANVAS_GROUP (reflow));
							}

							item = reflow->items[unsorted];
							gnome_canvas_item_set(item,
									      "has_focus", (event->key.state & GDK_SHIFT_MASK) ? E_FOCUS_END : E_FOCUS_START,
									      NULL);
							return 1;
						}
					}
				}
			}
#endif
			break;
		case GDK_BUTTON_PRESS:
			switch(event->button.button) 
				{
				case 1:
					{
						GdkEventButton *button = (GdkEventButton *) event;
						double n_x, max_x;
						n_x = button->x;
						n_x += E_REFLOW_BORDER_WIDTH + E_REFLOW_DIVIDER_WIDTH;
						n_x = fmod(n_x,(reflow->column_width + E_REFLOW_FULL_GUTTER));

						max_x = E_REFLOW_BORDER_WIDTH;
						max_x += (reflow->column_width + E_REFLOW_FULL_GUTTER) * reflow->column_count;
						if ( button->y >= E_REFLOW_BORDER_WIDTH && button->y <= reflow->height - E_REFLOW_BORDER_WIDTH && n_x < E_REFLOW_FULL_GUTTER && max_x > button->x ) {
							reflow->which_column_dragged = e_reflow_pick_line(reflow, button->x);
							reflow->start_x = reflow->which_column_dragged * (reflow->column_width + E_REFLOW_FULL_GUTTER) - E_REFLOW_DIVIDER_WIDTH / 2;
							reflow->temp_column_width = reflow->column_width;
							reflow->column_drag = TRUE;
						  
							gnome_canvas_item_grab (item, 
										GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK,
										reflow->arrow_cursor,
										button->time);
						  
							reflow->previous_temp_column_width = -1;
							reflow->need_column_resize = TRUE;
							gnome_canvas_item_request_update(item);
							return TRUE;
						}
					}
					break;
				case 4:
					{
						GtkAdjustment *adjustment = gtk_layout_get_hadjustment(GTK_LAYOUT(item->canvas));
						gdouble new_value = adjustment->value;
						new_value -= adjustment->step_increment;
						gtk_adjustment_set_value(adjustment, new_value);
					}
					break;
				case 5: 
					{
						GtkAdjustment *adjustment = gtk_layout_get_hadjustment(GTK_LAYOUT(item->canvas));
						gdouble new_value = adjustment->value;
						new_value += adjustment->step_increment;
						if ( new_value > adjustment->upper - adjustment->page_size )
							new_value = adjustment->upper - adjustment->page_size;
						gtk_adjustment_set_value(adjustment, new_value);
					}
					break;
				}
			break;
		case GDK_BUTTON_RELEASE:
			if (reflow->column_drag) {
				gdouble old_width = reflow->column_width;
				GdkEventButton *button = (GdkEventButton *) event;
				GtkAdjustment *adjustment = gtk_layout_get_hadjustment(GTK_LAYOUT(item->canvas));
				reflow->temp_column_width = reflow->column_width +
					(button->x - reflow->start_x)/(reflow->which_column_dragged - e_reflow_pick_line(reflow, adjustment->value));
				if ( reflow->temp_column_width < 50 )
					reflow->temp_column_width = 50;
				reflow->column_drag = FALSE;
				if ( old_width != reflow->temp_column_width ) {
					gtk_adjustment_set_value(adjustment, adjustment->value + e_reflow_pick_line(reflow, adjustment->value) * (reflow->temp_column_width - reflow->column_width));
					reflow->column_width = reflow->temp_column_width;
					adjustment->step_increment = (reflow->column_width + E_REFLOW_FULL_GUTTER) / 2;
					adjustment->page_increment = adjustment->page_size - adjustment->step_increment;
					gtk_adjustment_changed(adjustment);
					e_reflow_resize_children(item);
					e_canvas_item_request_reflow(item);
				}
				reflow->need_column_resize = TRUE;
				gnome_canvas_item_request_update(item);
				gnome_canvas_item_ungrab (item, button->time);
				return TRUE;
			}
			break;
		case GDK_MOTION_NOTIFY:
			if (reflow->column_drag) {
				double old_width = reflow->temp_column_width;
				GdkEventMotion *motion = (GdkEventMotion *) event;
				GtkAdjustment *adjustment = gtk_layout_get_hadjustment(GTK_LAYOUT(item->canvas));
				reflow->temp_column_width = reflow->column_width +
					(motion->x - reflow->start_x)/(reflow->which_column_dragged - e_reflow_pick_line(reflow, adjustment->value));
				if (reflow->temp_column_width < 50)
					reflow->temp_column_width = 50;
				if (old_width != reflow->temp_column_width) {
					reflow->need_column_resize = TRUE;
					gnome_canvas_item_request_update(item);
				}
				return TRUE;
			} else {
				GdkEventMotion *motion = (GdkEventMotion *) event;
				double n_x, max_x;

				n_x = motion->x;
				n_x += E_REFLOW_BORDER_WIDTH + E_REFLOW_DIVIDER_WIDTH;
				n_x = fmod(n_x,(reflow->column_width + E_REFLOW_FULL_GUTTER));

				max_x = E_REFLOW_BORDER_WIDTH;
				max_x += (reflow->column_width + E_REFLOW_FULL_GUTTER) * reflow->column_count;

				if ( motion->y >= E_REFLOW_BORDER_WIDTH && motion->y <= reflow->height - E_REFLOW_BORDER_WIDTH && n_x < E_REFLOW_FULL_GUTTER && max_x > motion->x) {
					if ( reflow->default_cursor_shown ) {
						gdk_window_set_cursor(GTK_WIDGET(item->canvas)->window, reflow->arrow_cursor);
						reflow->default_cursor_shown = FALSE;
					}
				} else 
					if ( ! reflow->default_cursor_shown ) {
						gdk_window_set_cursor(GTK_WIDGET(item->canvas)->window, reflow->default_cursor);
						reflow->default_cursor_shown = TRUE;
					}
			    
			}
			break;
		case GDK_ENTER_NOTIFY:
			if (!reflow->column_drag) {
				GdkEventCrossing *crossing = (GdkEventCrossing *) event;
				double n_x, max_x;
				n_x = crossing->x;
				n_x += E_REFLOW_BORDER_WIDTH + E_REFLOW_DIVIDER_WIDTH;
				n_x = fmod(n_x,(reflow->column_width + E_REFLOW_FULL_GUTTER));

				max_x = E_REFLOW_BORDER_WIDTH;
				max_x += (reflow->column_width + E_REFLOW_FULL_GUTTER) * reflow->column_count;
				if ( crossing->y >= E_REFLOW_BORDER_WIDTH && crossing->y <= reflow->height - E_REFLOW_BORDER_WIDTH && n_x < E_REFLOW_FULL_GUTTER && max_x > crossing->x) {
					if ( reflow->default_cursor_shown ) {
						gdk_window_set_cursor(GTK_WIDGET(item->canvas)->window, reflow->arrow_cursor);
						reflow->default_cursor_shown = FALSE;
					}
				}
			}
			break;
		case GDK_LEAVE_NOTIFY:
			if (!reflow->column_drag) {
				GdkEventCrossing *crossing = (GdkEventCrossing *) event;
				double n_x;
				n_x = crossing->x;
				n_x += E_REFLOW_BORDER_WIDTH + E_REFLOW_DIVIDER_WIDTH;
				n_x = fmod(n_x,(reflow->column_width + E_REFLOW_FULL_GUTTER));
				if ( !( crossing->y >= E_REFLOW_BORDER_WIDTH && crossing->y <= reflow->height - E_REFLOW_BORDER_WIDTH && n_x < E_REFLOW_FULL_GUTTER ) ) {
					if ( ! reflow->default_cursor_shown ) {
						gdk_window_set_cursor(GTK_WIDGET(item->canvas)->window, reflow->default_cursor);
						reflow->default_cursor_shown = TRUE;
					}
				}
			}
			break;
		default:
			break;
		}
	if (return_val)
		return return_val;
	else if (GNOME_CANVAS_ITEM_CLASS( parent_class )->event)
		return (* GNOME_CANVAS_ITEM_CLASS( parent_class )->event) (item, event);
	else
		return FALSE;
}

static void e_reflow_draw (GnomeCanvasItem *item, GdkDrawable *drawable,
				    int x, int y, int width, int height)
{
	int x_rect, y_rect, width_rect, height_rect;
	gdouble running_width;
	EReflow *reflow = E_REFLOW(item);
	int i;
	double column_width;

	if (GNOME_CANVAS_ITEM_CLASS(parent_class)->draw)
		GNOME_CANVAS_ITEM_CLASS(parent_class)->draw (item, drawable, x, y, width, height);
	column_width = reflow->column_width;
	running_width = E_REFLOW_BORDER_WIDTH + column_width + E_REFLOW_BORDER_WIDTH;
	x_rect = running_width;
	y_rect = E_REFLOW_BORDER_WIDTH;
	width_rect = E_REFLOW_DIVIDER_WIDTH;
	height_rect = reflow->height - (E_REFLOW_BORDER_WIDTH * 2);

	/* Compute first column to draw. */
	i = x;
	i /= column_width + E_REFLOW_FULL_GUTTER;
	running_width += i * (column_width + E_REFLOW_FULL_GUTTER);

	for ( ; i < reflow->column_count; i++) {
		if ( running_width > x + width )
			break;
		x_rect = running_width;
		gtk_paint_flat_box(GTK_WIDGET(item->canvas)->style,
				   drawable,
				   GTK_STATE_ACTIVE,
				   GTK_SHADOW_NONE,
				   NULL,
				   GTK_WIDGET(item->canvas),
				   "reflow",
				   x_rect - x,
				   y_rect - y,
				   width_rect,
				   height_rect);
		running_width += E_REFLOW_DIVIDER_WIDTH + E_REFLOW_BORDER_WIDTH + column_width + E_REFLOW_BORDER_WIDTH;
	}
	if (reflow->column_drag) {
		int start_line = e_reflow_pick_line(reflow,
						    gtk_layout_get_hadjustment(GTK_LAYOUT(item->canvas))->value); 
		i = x - start_line * (column_width + E_REFLOW_FULL_GUTTER);
		running_width = start_line * (column_width + E_REFLOW_FULL_GUTTER);
		column_width = reflow->temp_column_width;
		running_width -= start_line * (column_width + E_REFLOW_FULL_GUTTER);
		i += start_line * (column_width + E_REFLOW_FULL_GUTTER);
		running_width += E_REFLOW_BORDER_WIDTH + column_width + E_REFLOW_BORDER_WIDTH;
		x_rect = running_width;
		y_rect = E_REFLOW_BORDER_WIDTH;
		width_rect = E_REFLOW_DIVIDER_WIDTH;
		height_rect = reflow->height - (E_REFLOW_BORDER_WIDTH * 2);

		/* Compute first column to draw. */
		i /= column_width + E_REFLOW_FULL_GUTTER;
		running_width += i * (column_width + E_REFLOW_FULL_GUTTER);
		
		for ( ; i < reflow->column_count; i++) {
			if ( running_width > x + width )
				break;
			x_rect = running_width;
			gdk_draw_rectangle(drawable,
					   GTK_WIDGET(item->canvas)->style->fg_gc[GTK_STATE_NORMAL],
					   TRUE,
					   x_rect - x,
					   y_rect - y,
					   width_rect - 1,
					   height_rect - 1);					   
			running_width += E_REFLOW_DIVIDER_WIDTH + E_REFLOW_BORDER_WIDTH + column_width + E_REFLOW_BORDER_WIDTH;
		}
	}
}

static void
e_reflow_update (GnomeCanvasItem *item, double affine[6], ArtSVP *clip_path, gint flags)
{
	EReflow *reflow;
	double x0, x1, y0, y1;

	reflow = E_REFLOW (item);

	if (GNOME_CANVAS_ITEM_CLASS(parent_class)->update)
		GNOME_CANVAS_ITEM_CLASS(parent_class)->update (item, affine, clip_path, flags);
	
	x0 = item->x1;
	y0 = item->y1;
	x1 = item->x2;
	y1 = item->y2;
	if ( x1 < x0 + reflow->width )
		x1 = x0 + reflow->width;
	if ( y1 < y0 + reflow->height )
		y1 = y0 + reflow->height;
	item->x2 = x1;
	item->y2 = y1;

	if (reflow->need_height_update) {
		x0 = item->x1;
		y0 = item->y1;
		x1 = item->x2;
		y1 = item->y2;
		if ( x0 > 0 )
			x0 = 0;
		if ( y0 > 0 )
			y0 = 0;
		if ( x1 < E_REFLOW(item)->width )
			x1 = E_REFLOW(item)->width;
		if ( x1 < E_REFLOW(item)->height )
			x1 = E_REFLOW(item)->height;

		gnome_canvas_request_redraw(item->canvas, x0, y0, x1, y1);
		reflow->need_height_update = FALSE;
	} else if (reflow->need_column_resize) {
		int x_rect, y_rect, width_rect, height_rect;
		int start_line = e_reflow_pick_line(reflow,
						    gtk_layout_get_hadjustment(GTK_LAYOUT(item->canvas))->value); 
		gdouble running_width;
		int i;
		double column_width;
		
		if ( reflow->previous_temp_column_width != -1 ) {
			running_width = start_line * (reflow->column_width + E_REFLOW_FULL_GUTTER);
			column_width = reflow->previous_temp_column_width;
			running_width -= start_line * (column_width + E_REFLOW_FULL_GUTTER);
			running_width += E_REFLOW_BORDER_WIDTH + column_width + E_REFLOW_BORDER_WIDTH;
			y_rect = E_REFLOW_BORDER_WIDTH;
			width_rect = E_REFLOW_DIVIDER_WIDTH;
			height_rect = reflow->height - (E_REFLOW_BORDER_WIDTH * 2);
			
			for ( i = 0; i < reflow->column_count; i++) {
				x_rect = running_width;
				gnome_canvas_request_redraw(item->canvas, x_rect, y_rect, x_rect + width_rect, y_rect + height_rect);
				running_width += E_REFLOW_DIVIDER_WIDTH + E_REFLOW_BORDER_WIDTH + column_width + E_REFLOW_BORDER_WIDTH;
			}
		}
		
		if ( reflow->temp_column_width != -1 ) {
			running_width = start_line * (reflow->column_width + E_REFLOW_FULL_GUTTER);
			column_width = reflow->temp_column_width;
			running_width -= start_line * (column_width + E_REFLOW_FULL_GUTTER);
			running_width += E_REFLOW_BORDER_WIDTH + column_width + E_REFLOW_BORDER_WIDTH;
			y_rect = E_REFLOW_BORDER_WIDTH;
			width_rect = E_REFLOW_DIVIDER_WIDTH;
			height_rect = reflow->height - (E_REFLOW_BORDER_WIDTH * 2);
			
			for ( i = 0; i < reflow->column_count; i++) {
				x_rect = running_width;
				gnome_canvas_request_redraw(item->canvas, x_rect, y_rect, x_rect + width_rect, y_rect + height_rect);
				running_width += E_REFLOW_DIVIDER_WIDTH + E_REFLOW_BORDER_WIDTH + column_width + E_REFLOW_BORDER_WIDTH;
			}
		}
		
		reflow->previous_temp_column_width = reflow->temp_column_width;
		reflow->need_column_resize = FALSE;
	}
}

static double
e_reflow_point (GnomeCanvasItem *item,
		double x, double y, int cx, int cy,
		GnomeCanvasItem **actual_item)
{
	double distance = 1;

	*actual_item = NULL;

	if (GNOME_CANVAS_ITEM_CLASS(parent_class)->point)
		distance = GNOME_CANVAS_ITEM_CLASS(parent_class)->point (item, x, y, cx, cy, actual_item);
	if ((int) (distance * item->canvas->pixels_per_unit + 0.5) <= item->canvas->close_enough && *actual_item)
		return distance;
	
	*actual_item = item;
	return 0;
#if 0
	if (y >= E_REFLOW_BORDER_WIDTH && y <= reflow->height - E_REFLOW_BORDER_WIDTH) {
	        float n_x;
		n_x = x;
		n_x += E_REFLOW_BORDER_WIDTH + E_REFLOW_DIVIDER_WIDTH;
		n_x = fmod(n_x, (reflow->column_width + E_REFLOW_FULL_GUTTER));
		if (n_x < E_REFLOW_FULL_GUTTER) {
			*actual_item = item;
			return 0;
		}
	}
	return distance;
#endif
}

static void
e_reflow_reflow( GnomeCanvasItem *item, int flags )
{
	EReflow *reflow = E_REFLOW(item);
	gdouble old_width;
	gdouble running_width;
	gdouble running_height;
	int next_column;
	int i;

	if (! (GTK_OBJECT_FLAGS (reflow) & GNOME_CANVAS_ITEM_REALIZED))
		return;

	if (reflow->need_reflow_columns) {
		reflow_columns (reflow);
	}
		
	old_width = reflow->width;

	running_width = E_REFLOW_BORDER_WIDTH;
	running_height = E_REFLOW_BORDER_WIDTH;

	next_column = 1;

	for (i = 0; i < reflow->count; i++) {
		int unsorted = e_sorter_sorted_to_model (E_SORTER (reflow->sorter), i);
		if (next_column < reflow->column_count && i == reflow->columns[next_column]) {
			running_height = E_REFLOW_BORDER_WIDTH;
			running_width += reflow->column_width + E_REFLOW_FULL_GUTTER;
			next_column ++;
		}

		if (unsorted >= 0 && reflow->items[unsorted]) {
			e_canvas_item_move_absolute(GNOME_CANVAS_ITEM(reflow->items[unsorted]),
						    (double) running_width,
						    (double) running_height);
			running_height += reflow->heights[unsorted] + E_REFLOW_BORDER_WIDTH;
		}
	}
	reflow->width = running_width + reflow->column_width + E_REFLOW_BORDER_WIDTH;
	if ( reflow->width < reflow->minimum_width )
		reflow->width = reflow->minimum_width;
	if (old_width != reflow->width)
		e_canvas_item_request_parent_reflow(item);
}

static int
e_reflow_selection_event_real (EReflow *reflow, GnomeCanvasItem *item, GdkEvent *event)
{
	int row;
	int return_val = TRUE;
	switch (event->type) {
	case GDK_BUTTON_PRESS:
		switch (event->button.button) {
		case 1: /* Fall through. */
		case 2:
			row = er_find_item (reflow, item);
			e_selection_model_do_something(reflow->selection, row, 0, event->button.state);
			break;
		case 3:
			row = er_find_item (reflow, item);
			e_selection_model_right_click_down(reflow->selection, row, 0, 0);
			break;
		default:
			return_val = FALSE;
			break;
		}
		break;
	case GDK_KEY_PRESS:
		return_val = e_selection_model_key_press(reflow->selection, (GdkEventKey *) event);
		break;
	default:
		return_val = FALSE;
		break;
	}

	return return_val;
}

static void
e_reflow_class_init (EReflowClass *klass)
{
	GtkObjectClass *object_class;
	GnomeCanvasItemClass *item_class;

	object_class = (GtkObjectClass*) klass;
	item_class = (GnomeCanvasItemClass *) klass;

	parent_class = gtk_type_class (gnome_canvas_group_get_type ());
  
	gtk_object_add_arg_type ("EReflow::minimum_width", GTK_TYPE_DOUBLE,
				 GTK_ARG_READWRITE, ARG_MINIMUM_WIDTH);
	gtk_object_add_arg_type ("EReflow::width", GTK_TYPE_DOUBLE,
				 GTK_ARG_READABLE, ARG_WIDTH);
	gtk_object_add_arg_type ("EReflow::height", GTK_TYPE_DOUBLE,
				 GTK_ARG_READWRITE, ARG_HEIGHT);
	gtk_object_add_arg_type ("EReflow::empty_message", GTK_TYPE_STRING,
				 GTK_ARG_READWRITE, ARG_EMPTY_MESSAGE);
	gtk_object_add_arg_type ("EReflow::model", E_REFLOW_MODEL_TYPE,
				 GTK_ARG_READWRITE, ARG_MODEL);
	gtk_object_add_arg_type ("EReflow::column_width", GTK_TYPE_INT,
				 GTK_ARG_READWRITE, ARG_COLUMN_WIDTH);

	signals [SELECTION_EVENT] =
		gtk_signal_new ("selection_event",
				GTK_RUN_LAST,
				E_OBJECT_CLASS_TYPE (object_class),
				GTK_SIGNAL_OFFSET (EReflowClass, selection_event),
				e_marshal_INT__OBJECT_POINTER,
				GTK_TYPE_INT, 2, GTK_TYPE_OBJECT, GTK_TYPE_GDK_EVENT);

	E_OBJECT_CLASS_ADD_SIGNALS (object_class, signals, LAST_SIGNAL);

	object_class->set_arg  = e_reflow_set_arg;
	object_class->get_arg  = e_reflow_get_arg;
	object_class->destroy  = e_reflow_destroy;
  
	/* GnomeCanvasItem method overrides */
	item_class->event      = e_reflow_event;
	item_class->realize    = e_reflow_realize;
	item_class->unrealize  = e_reflow_unrealize;
	item_class->draw       = e_reflow_draw;
	item_class->update     = e_reflow_update;
	item_class->point      = e_reflow_point;

	klass->selection_event = e_reflow_selection_event_real;
}

static void
e_reflow_init (EReflow *reflow)
{
	reflow->model                     = NULL;
	reflow->items                     = NULL;
	reflow->heights                   = NULL;
	reflow->count                     = 0;

	reflow->columns                   = NULL;
	reflow->column_count              = 0;

	reflow->empty_text                = NULL;
	reflow->empty_message             = NULL;

	reflow->minimum_width             = 10;
	reflow->width                     = 10;
	reflow->height                    = 10;

	reflow->column_width              = 150;

	reflow->column_drag               = FALSE;

	reflow->need_height_update        = FALSE;
	reflow->need_column_resize        = FALSE;

	reflow->default_cursor_shown      = TRUE;
	reflow->arrow_cursor              = NULL;
	reflow->default_cursor            = NULL;

	reflow->cursor_row                = -1;

	reflow->incarnate_idle_id         = 0;
	reflow->set_scroll_adjustments_id = 0;

	reflow->selection                 = E_SELECTION_MODEL (e_selection_model_simple_new());
	reflow->sorter                    = e_sorter_array_new (er_compare, reflow);

	gtk_object_set (GTK_OBJECT (reflow->selection),
			"sorter", reflow->sorter,
			NULL);

	reflow->selection_changed_id = 
		gtk_signal_connect(GTK_OBJECT(reflow->selection), "selection_changed",
				   GTK_SIGNAL_FUNC(selection_changed), reflow);
	reflow->selection_row_changed_id = 
		gtk_signal_connect(GTK_OBJECT(reflow->selection), "selection_row_changed",
				   GTK_SIGNAL_FUNC(selection_row_changed), reflow);
	reflow->cursor_changed_id = 
		gtk_signal_connect(GTK_OBJECT(reflow->selection), "cursor_changed",
				   GTK_SIGNAL_FUNC(cursor_changed), reflow);

	e_canvas_item_set_reflow_callback(GNOME_CANVAS_ITEM(reflow), e_reflow_reflow);
}

GtkType
e_reflow_get_type (void)
{
	static GtkType type = 0;

	if (!type) {
		static const GtkTypeInfo info = {
			"EReflow",
			sizeof (EReflow),
			sizeof (EReflowClass),
			(GtkClassInitFunc) e_reflow_class_init,
			(GtkObjectInitFunc) e_reflow_init,
			/* reserved_1 */ NULL,
			/* reserved_2 */ NULL,
			(GtkClassInitFunc) NULL,
		};

		type = gtk_type_unique (gnome_canvas_group_get_type (), &info);
	}

	return type;
}
