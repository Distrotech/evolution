/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Chris Toshok <toshok@ximian.com>
 *
 *  Copyright (C) 2004 Novell, Inc. (www.novell.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Street #330, Boston, MA 02111-1307, USA.
 *
 */

#include <string.h>

#include "eab-marshal.h"
#include "eab-minicard-view.h"
#include "e-addressbook-reflow-adapter.h"
#include "util/eab-book-util.h"

#include <libgnome/gnome-i18n.h>
#include <gal/util/e-sorter-array.h>
#include <gtk/gtkselection.h>
#include <gtk/gtkdnd.h>

#define PARENT_TYPE (gtk_widget_get_type ())
static gpointer parent_class = NULL;

#define VBORDER 10
#define HBORDER 10

#define SELECTION_SPACING 3

#define MIN_COLUMN_WIDTH 50
#define MAX_COLUMN_WIDTH 300
#define DEFAULT_COLUMN_WIDTH 150

#define LINE_WIDTH 3

#define COLUMN_SPACING 10
#define INTERCONTACT_SPACING 10

#define COLUMN_LINE_WIDTH 

/* The arguments we take */
enum {
	PROP_0,
	PROP_ADAPTER,
	PROP_BOOK,
	PROP_QUERY,
	PROP_EDITABLE,
	PROP_COLUMN_WIDTH
};

enum {
	SELECTION_CHANGE,
	COLUMN_WIDTH_CHANGED,
	RIGHT_CLICK,
	LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = {0, };

#define MAX_MINICARD_FIELDS 5
typedef struct {
	EContact *contact;
	int x, y;
	int total_height;
	int num_fields;
	int header_height;
	EContactField fields[MAX_MINICARD_FIELDS];
	int field_heights[MAX_MINICARD_FIELDS];
} EABMinicard;

struct _EABMinicardViewPrivate {
	EAddressbookReflowAdapter *adapter;

	gboolean tooltip_shown;
	int tooltip_timeout_id;

	int writable_status_id;
	int model_changed_id;
	int comparison_changed_id;
	int model_items_inserted_id;
	int model_item_removed_id;
	int model_item_changed_id;

	int column_width;

	int orig_column_x;
	int orig_column_width;

	int reflow_on_idle_id;
	int reflow_on_idle_from_column;

	int prev_height;

	gboolean in_column_drag;
	gint column_dragged;

	gboolean drag_button_down;
	int button_x;
	int button_y;

	GList *drag_list;

	gboolean column_cursor_shown;

	GArray *column_starts;
	GArray *minicards;

	gboolean maybe_did_something;
	ESelectionModel *selection;
	guint selection_changed_id;
	guint selection_row_changed_id;
	guint cursor_changed_id;

	PangoLayout *label_layout_cache [E_CONTACT_LAST_SIMPLE_STRING];

	ESorterArray *sorter;
};



enum DndTargetType {
	DND_TARGET_TYPE_VCARD_LIST,
	DND_TARGET_TYPE_SOURCE_VCARD_LIST
};
#define VCARD_LIST_TYPE "text/x-vcard"
#define SOURCE_VCARD_LIST_TYPE "text/x-source-vcard"
static GtkTargetEntry drag_types[] = {
	{ SOURCE_VCARD_LIST_TYPE, 0, DND_TARGET_TYPE_SOURCE_VCARD_LIST },
	{ VCARD_LIST_TYPE, 0, DND_TARGET_TYPE_VCARD_LIST }
};
static gint num_drag_types = sizeof(drag_types) / sizeof(drag_types[0]);

static void
eab_minicard_view_drag_data_get(GtkWidget *widget,
				GdkDragContext *context,
				GtkSelectionData *selection_data,
				guint info,
				guint time)
{
	EABMinicardViewPrivate *priv;

	if (!EAB_IS_MINICARD_VIEW(widget))
		return;

	priv = EAB_MINICARD_VIEW (widget)->priv;

	switch (info) {
	case DND_TARGET_TYPE_VCARD_LIST: {
		char *value;
		
		value = eab_contact_list_to_string (priv->drag_list);

		gtk_selection_data_set (selection_data,
					selection_data->target,
					8,
					value, strlen (value));
		break;
	}
	case DND_TARGET_TYPE_SOURCE_VCARD_LIST: {
		EBook *book;
		char *value;
		
		g_object_get (priv->adapter, "book", &book, NULL);
		value = eab_book_and_contact_list_to_string (book, priv->drag_list);

		gtk_selection_data_set (selection_data,
					selection_data->target,
					8,
					value, strlen (value));
		break;
	}
	}
}

static void
clear_drag_data (EABMinicardView *view)
{
	EABMinicardViewPrivate *priv = view->priv;

	g_list_foreach (priv->drag_list, (GFunc)g_object_unref, NULL);
	g_list_free (priv->drag_list);
	priv->drag_list = NULL;
}


typedef struct {
	GList *list;
	EAddressbookReflowAdapter *adapter;
} ModelAndList;

static void
add_to_list (int index, gpointer closure)
{
	ModelAndList *mal = closure;
	mal->list = g_list_prepend (mal->list, g_object_ref (e_addressbook_reflow_adapter_contact_at (mal->adapter, index)));
}

static GList *
eab_minicard_view_get_card_list (EABMinicardView *view)
{
	EABMinicardViewPrivate *priv = view->priv;
	ModelAndList mal;

	mal.adapter = priv->adapter;
	mal.list = NULL;

	e_selection_model_foreach (priv->selection, add_to_list, &mal);

	mal.list = g_list_reverse (mal.list);
	return mal.list;
}

static int
eab_minicard_view_drag_begin (EABMinicardView *view, GdkEvent *event)
{
	EABMinicardViewPrivate *priv = view->priv;
	GdkDragContext *context;
	GtkTargetList *target_list;
	GdkDragAction actions = GDK_ACTION_MOVE | GDK_ACTION_COPY;

	clear_drag_data (view);
	
	priv->drag_list = eab_minicard_view_get_card_list (view);

	g_print ("dragging %d card(s)\n", g_list_length (priv->drag_list));

	target_list = gtk_target_list_new (drag_types, num_drag_types);

	context = gtk_drag_begin (GTK_WIDGET (view),
				  target_list, actions, 1/*XXX*/, event);

	gtk_drag_set_icon_default (context);

	return TRUE;
}



static int
text_height (PangoLayout *layout, const gchar *text)
{
	int height;

	pango_layout_set_text (layout, text, -1);

	pango_layout_get_pixel_size (layout, NULL, &height);

	return height;
}

static PangoLayout *
get_label_layout (EABMinicardView *view, EContactField field_id)
{
	EABMinicardViewPrivate *priv = view->priv;

	g_return_val_if_fail (field_id > 0 && field_id <= E_CONTACT_LAST_SIMPLE_STRING, NULL);

	if (!priv->label_layout_cache[field_id]) {
		char *label = g_strdup_printf ("%s:", e_contact_pretty_name (field_id));
		priv->label_layout_cache[field_id] = gtk_widget_create_pango_layout (GTK_WIDGET (view), label);
		g_free (label);
	}

	return priv->label_layout_cache[field_id];
}

static void
measure_minicard (EABMinicardView *view, EABMinicard *minicard)
{
	EContactField field;
	int count = 0;
	char *string;
	EContact *contact = minicard->contact;
	PangoLayout *layout = gtk_widget_create_pango_layout (GTK_WIDGET (view), "");
	int height;

	string = e_contact_get_const (contact, E_CONTACT_FILE_AS);
	minicard->header_height = text_height (layout, string ? string : "") + 10;

	height = minicard->header_height;

	for(field = E_CONTACT_FULL_NAME; field != E_CONTACT_LAST_SIMPLE_STRING && count < MAX_MINICARD_FIELDS; field++) {

		if (field == E_CONTACT_FAMILY_NAME || field == E_CONTACT_GIVEN_NAME)
			continue;

		string = e_contact_get_const (contact, field);
		if (string && *string) {
			int this_height;
			int field_text_height;
			PangoLayout *label_layout = get_label_layout (view, field);

			pango_layout_get_pixel_size (label_layout, NULL, &this_height);

			field_text_height = text_height (layout, string);
			if (this_height < field_text_height)
				this_height = field_text_height;

			this_height += 3;

			minicard->fields[count] = field;
			minicard->field_heights[count] = this_height;

			height += this_height;
			count ++;
		}
	}
	height += 2;

	minicard->num_fields = count;
	minicard->total_height = height;

	g_object_unref (layout);
}

static void
draw_minicard_fields (EABMinicardView *view, EABMinicard *minicard,
		      int x, int y)
{
	EABMinicardViewPrivate *priv = view->priv;
	int i;
	GdkGC *text_gc;
	GdkRectangle clip_rect;
	PangoLayout *layout = gtk_widget_create_pango_layout (GTK_WIDGET (view), "");

	text_gc = GTK_WIDGET (view)->style->text_gc[GTK_STATE_NORMAL];

	for (i = 0; i < minicard->num_fields; i ++) {
		PangoLayout *label_layout;
		const char *field_value;

		field_value = e_contact_get_const (minicard->contact, minicard->fields[i]);

		clip_rect.x = x;
		clip_rect.y = y;
		clip_rect.width = priv->column_width / 2 - 2;
		clip_rect.height = minicard->field_heights[i];

		gdk_gc_set_clip_rectangle (text_gc, &clip_rect);

		label_layout = get_label_layout (view, minicard->fields[i]);
		gdk_draw_layout (GTK_WIDGET (view)->window, text_gc, x, y, label_layout);

		clip_rect.x = x + priv->column_width / 2;
		gdk_gc_set_clip_rectangle (text_gc, &clip_rect);

		pango_layout_set_text (layout, field_value, -1);
		gdk_draw_layout (GTK_WIDGET (view)->window, text_gc, x + priv->column_width / 2, y, layout);

		y += minicard->field_heights[i];
	}

	gdk_gc_set_clip_rectangle (text_gc, NULL);
	g_object_unref (layout);
}

static void
draw_minicard (EABMinicardView *view, EABMinicard *minicard, gboolean selected)
{
	EABMinicardViewPrivate *priv = view->priv;
	PangoLayout *layout;
	GdkGC *header_gc, *text_gc;
	const char *string;
	GdkRectangle clip_rect;
	int x, y;

	x = minicard->x;
	y = minicard->y;

	if (selected) {
		gdk_draw_rectangle (GTK_WIDGET (view)->window,
				    GTK_WIDGET (view)->style->bg_gc[GTK_STATE_SELECTED],
				    FALSE, x - SELECTION_SPACING, y - SELECTION_SPACING, priv->column_width + 2*SELECTION_SPACING, minicard->total_height + 2*SELECTION_SPACING);

		header_gc = GTK_WIDGET (view)->style->bg_gc[GTK_STATE_SELECTED];
		text_gc = GTK_WIDGET (view)->style->text_gc[GTK_STATE_SELECTED];
	}
	else {
		header_gc = GTK_WIDGET (view)->style->bg_gc[GTK_STATE_ACTIVE];
		text_gc = GTK_WIDGET (view)->style->text_gc[GTK_STATE_NORMAL];
	}

	gdk_draw_rectangle (GTK_WIDGET (view)->window, header_gc, TRUE, x, y, priv->column_width, minicard->header_height);

	clip_rect.x = x + 5;
	clip_rect.y = y + 5;
	clip_rect.width = priv->column_width - 10;
	clip_rect.height = minicard->header_height - 10;

	gdk_gc_set_clip_rectangle (text_gc, &clip_rect);

	string = e_contact_get_const(minicard->contact, E_CONTACT_FILE_AS);
	layout = gtk_widget_create_pango_layout (GTK_WIDGET (view), string ? string : "");
	gdk_draw_layout (GTK_WIDGET (view)->window, text_gc, x + 5, y + 5, layout);

	gdk_gc_set_clip_rectangle (text_gc, NULL);

	g_object_unref (layout);

	draw_minicard_fields (view, minicard, x, y + minicard->header_height);
}

static void
reflow_columns (EABMinicardView *view, int from_column)
{
	EABMinicardViewPrivate *priv = view->priv;
	int count;
	int start;
	int i;
	int c, column_start;
	double running_height;
	EABMinicard *start_minicard;

	if (from_column <= 1) {
		start = 0;
		column_start = 0;
		c = 1;
	}
	else {
		/* we start one column before the earliest new entry,
		   so we can handle the case where the new entry is
		   inserted at the start of the column */
		column_start = from_column - 1;
		start = g_array_index (priv->column_starts, int, column_start);
		c = column_start;
	}

	g_array_set_size (priv->column_starts, c - 1);

	running_height = VBORDER;

	count = priv->minicards->len - start;
	g_array_append_val (priv->column_starts, start);
	for (i = start; i < count; i++) {
		int unsorted = e_sorter_sorted_to_model (E_SORTER (priv->sorter), i);
		EABMinicard *minicard = &g_array_index (priv->minicards, EABMinicard, unsorted);
		if (i != start && running_height + minicard->total_height + VBORDER > view->parent.allocation.height) {
			g_array_append_val (priv->column_starts, i);
			c ++;

			running_height = VBORDER * 2 + minicard->total_height;
		} else
			running_height += minicard->total_height + VBORDER;
	}

	/* queue up a redraw for the columns we've reflowed */
	if (priv->minicards->len) {
		start_minicard = &g_array_index (priv->minicards, EABMinicard, start);
		gtk_widget_queue_draw_area (GTK_WIDGET (view),
					    start_minicard->x, 0,
					    GTK_WIDGET (view)->allocation.width,
					    GTK_WIDGET (view)->allocation.height);
	}
}

static gboolean
reflow_on_idle_handler (gpointer data)
{
	EABMinicardView *view = data;
	EABMinicardViewPrivate *priv = view->priv;
	int oldcolumncount;

	oldcolumncount = priv->column_starts->len;

	reflow_columns (view, priv->reflow_on_idle_from_column);

	if (oldcolumncount != priv->column_starts->len)
		gtk_widget_queue_resize (GTK_WIDGET (view));

	priv->reflow_on_idle_id = 0;
	return FALSE;
}

static void
reflow_on_idle (EABMinicardView *view, int from_column)
{
	EABMinicardViewPrivate *priv = view->priv;
	gboolean new_reflow = FALSE;

	if (priv->reflow_on_idle_id == 0)
		new_reflow = TRUE;

	if (new_reflow)
		priv->reflow_on_idle_id = g_idle_add (reflow_on_idle_handler, view);
	if (new_reflow || priv->reflow_on_idle_from_column > from_column)
		priv->reflow_on_idle_from_column = from_column;
}

static void
item_changed (EReflowModel *model, int i, EABMinicardView *view)
{
	EABMinicardViewPrivate *priv = view->priv;
	EABMinicard *minicard;

	if (i < 0 || i >= priv->minicards->len)
		return;

	minicard = &g_array_index (priv->minicards, EABMinicard, i);
	if (minicard->contact)
		g_object_unref (minicard->contact);
	minicard->contact = g_object_ref (e_addressbook_reflow_adapter_contact_at (priv->adapter, i));
	measure_minicard (view, minicard);
	e_sorter_array_clean (priv->sorter);

	reflow_columns (view, -1);
}

static void
item_removed (EReflowModel *model, int i, EABMinicardView *view)
{
	EABMinicardViewPrivate *priv = view->priv;
	int c;
	int sorted;
	int from_column;
	int oldcolumncount;

	if (i < 0 || i >= priv->minicards->len)
		return;

	oldcolumncount = priv->column_starts->len;

	sorted = e_sorter_model_to_sorted (E_SORTER (priv->sorter), i);
	for (c = priv->column_starts->len - 1; c >= 0; c--) {
		int start_of_column = g_array_index (priv->column_starts, int, c);

		if (start_of_column <= sorted) {
			if (from_column > c)
				from_column = c;
		}
	}

	g_array_remove_index (priv->minicards, i);

	reflow_columns (view, from_column);

	if (oldcolumncount != priv->column_starts->len)
		gtk_widget_queue_resize (GTK_WIDGET (view));

	e_sorter_array_set_count (priv->sorter, priv->minicards->len);

	e_selection_model_simple_delete_rows (E_SELECTION_MODEL_SIMPLE (priv->selection), i, 1);
}

static void
items_inserted (EReflowModel *model, int position, int count, EABMinicardView *view)
{
	EABMinicardViewPrivate *priv = view->priv;
	EABMinicard *values;
	int i, oldcount, from_column;
	int oldcolumncount;

	if (position < 0 || position > priv->minicards->len)
		return;

	oldcolumncount = priv->column_starts->len;
	oldcount = priv->minicards->len;

	values = g_new (EABMinicard, count);

	for (i = 0; i < count; i ++) {
		values[i].contact = g_object_ref (e_addressbook_reflow_adapter_contact_at (priv->adapter, position + i));
		measure_minicard (view, &values[i]);
	}

	g_array_insert_vals (priv->minicards, position, values, count);
	
	e_selection_model_simple_set_row_count (E_SELECTION_MODEL_SIMPLE (priv->selection), priv->minicards->len);
	if (position == oldcount)
		e_sorter_array_append (priv->sorter, count);
	else
		e_sorter_array_set_count (priv->sorter, priv->minicards->len);

	for (i = position; i < position + count; i ++) {
		int sorted = e_sorter_model_to_sorted (E_SORTER (priv->sorter), i);
		int c;

		for (c = priv->column_starts->len - 1; c >= 0; c--) {
			int start_of_column = g_array_index (priv->column_starts, int, c);

			if (start_of_column <= sorted) {
				if (from_column > c)
					from_column = c;
				break;
			}
		}
	}

	reflow_columns (view, from_column);

	if (oldcolumncount != priv->column_starts->len)
		gtk_widget_queue_resize (GTK_WIDGET (view));
}

static void
model_changed (EReflowModel *model, EABMinicardView *view)
{
	int i;
	EABMinicardViewPrivate *priv = view->priv;
	int oldcolumncount;

	oldcolumncount = priv->column_starts->len;

	for (i = 0; i < priv->minicards->len; i ++) {
		EABMinicard *minicard = &g_array_index (priv->minicards, EABMinicard, i);
		g_object_unref (minicard->contact);
	}

	g_array_set_size (priv->minicards, e_reflow_model_count (E_REFLOW_MODEL (priv->adapter)));
	e_sorter_array_set_count (priv->sorter, priv->minicards->len);

	for (i = 0; i < priv->minicards->len; i++) {
		int unsorted = e_sorter_sorted_to_model (E_SORTER (priv->sorter), i);
		EABMinicard *minicard = &g_array_index (priv->minicards, EABMinicard, unsorted);
		minicard->contact = g_object_ref (e_addressbook_reflow_adapter_contact_at (priv->adapter, unsorted));
		measure_minicard (view, minicard);
	}

	e_selection_model_simple_set_row_count (E_SELECTION_MODEL_SIMPLE (priv->selection), priv->minicards->len);
	e_sorter_array_set_count (priv->sorter, priv->minicards->len);

	reflow_columns (view, -1);

	if (oldcolumncount != priv->column_starts->len)
		gtk_widget_queue_resize (GTK_WIDGET (view));
}

static void
comparison_changed (EReflowModel *model, EABMinicardView *view)
{
	EABMinicardViewPrivate *priv = view->priv;
	e_sorter_array_clean (priv->sorter);
	reflow_columns (view, -1);
}




GtkWidget*
eab_minicard_view_new (void)
{
	return g_object_new (EAB_TYPE_MINICARD_VIEW, NULL);
}

GtkWidget*
eab_minicard_view_new_with_adapter (EAddressbookReflowAdapter *adapter)
{
	return g_object_new (EAB_TYPE_MINICARD_VIEW,
			     "adapter", adapter,
			     NULL);
}

ESelectionModel *
eab_minicard_view_get_selection_model (EABMinicardView *view)
{
	g_return_val_if_fail (EAB_IS_MINICARD_VIEW (view), NULL);

	return view->priv->selection;
}


static void
selection_changed (ESelectionModel *selection, EABMinicardView *view)
{
	printf ("selection_changed\n");
	gtk_widget_queue_draw (GTK_WIDGET (view));
	g_signal_emit (view, signals[SELECTION_CHANGE], 0, NULL);
}

static void
selection_row_changed (ESelectionModel *selection, int row, EABMinicardView *view)
{
	EABMinicardViewPrivate *priv = view->priv;
	EABMinicard *minicard;

	minicard = &g_array_index(priv->minicards, EABMinicard, row);

	/* redraw the contact */
	gtk_widget_queue_draw_area (GTK_WIDGET (view),
				    minicard->x - SELECTION_SPACING,
				    minicard->y - SELECTION_SPACING,
				    priv->column_width + 2 * SELECTION_SPACING + 1,
				    minicard->total_height + 2 * SELECTION_SPACING + 1);
	g_signal_emit (view, signals[SELECTION_CHANGE], 0, NULL);
}

static void
cursor_changed (ESelectionModel *selection, int row, int col, EABMinicardView *view)
{
	//	printf ("cursor_changed\n");
}



static gint
er_compare (int i1, int i2, gpointer user_data)
{
	EABMinicardView *view = user_data;
	EABMinicardViewPrivate *priv = view->priv;
	return e_reflow_model_compare (E_REFLOW_MODEL (priv->adapter), i1, i2);
}

static void
eab_minicard_view_init (GObject *object)
{
	EABMinicardView *view = EAB_MINICARD_VIEW (object);

	GTK_WIDGET_UNSET_FLAGS (view, GTK_NO_WINDOW);

	view->priv = g_new0 (EABMinicardViewPrivate, 1);

	view->priv->column_starts = g_array_new (FALSE, FALSE, sizeof (int));
	view->priv->minicards = g_array_new (FALSE, FALSE, sizeof (EABMinicard));

	view->priv->column_width = DEFAULT_COLUMN_WIDTH;

	view->priv->selection    = E_SELECTION_MODEL (e_selection_model_simple_new());
	view->priv->sorter       = e_sorter_array_new (er_compare, view);

	g_object_set (view->priv->selection,
		      "sorter", view->priv->sorter,
		      NULL);

	view->priv->selection_changed_id = 
		g_signal_connect(view->priv->selection, "selection_changed",
				 G_CALLBACK (selection_changed), view);
	view->priv->selection_row_changed_id = 
		g_signal_connect(view->priv->selection, "selection_row_changed",
				 G_CALLBACK (selection_row_changed), view);
	view->priv->cursor_changed_id = 
		g_signal_connect(view->priv->selection, "cursor_changed",
				 G_CALLBACK (cursor_changed), view);

	view->priv->tooltip_shown = FALSE;
	view->priv->tooltip_timeout_id = 0;
}



static void
eab_minicard_view_set_property (GObject *object,
				guint prop_id,
				const GValue *value,
				GParamSpec *pspec)
{
	EABMinicardView *view;
	EABMinicardViewPrivate *priv;

	view = EAB_MINICARD_VIEW (object);
	priv = view->priv;

	switch (prop_id){
	case PROP_ADAPTER:
		if (priv->adapter) {
			g_signal_handler_disconnect (priv->adapter, priv->model_changed_id);
			g_signal_handler_disconnect (priv->adapter, priv->comparison_changed_id);
			g_signal_handler_disconnect (priv->adapter, priv->model_items_inserted_id);
			g_signal_handler_disconnect (priv->adapter, priv->model_item_removed_id);
			g_signal_handler_disconnect (priv->adapter, priv->model_item_changed_id);

			if (priv->writable_status_id) {
				EABModel *model;
				g_object_get (priv->adapter,
					      "model", &model,
					      NULL);
				if (model) {
					g_signal_handler_disconnect (model, priv->writable_status_id);
				}
			}

			g_object_unref (priv->adapter);
		}
		priv->writable_status_id = 0;
		priv->adapter = g_value_get_object (value);
		g_object_ref (priv->adapter);
		if (priv->adapter) {
			EABModel *model;

			priv->model_changed_id = g_signal_connect (priv->adapter,
								   "model_changed", G_CALLBACK (model_changed), view);
			priv->comparison_changed_id = g_signal_connect (priv->adapter, "comparison_changed",
									G_CALLBACK (comparison_changed), view);
			priv->model_items_inserted_id = g_signal_connect (priv->adapter,
									  "model_items_inserted",
									  G_CALLBACK (items_inserted), view);
			priv->model_item_removed_id = g_signal_connect (priv->adapter,
									"model_item_removed",
									G_CALLBACK (item_removed), view);
			priv->model_item_changed_id = g_signal_connect (priv->adapter,
									"model_item_changed",
									G_CALLBACK (item_changed), view);

			g_object_get (priv->adapter,
				      "model", &model,
				      NULL);

			if (model) {
#if notyet
				priv->writable_status_id =
					g_signal_connect (model, "writable_status",
							  G_CALLBACK (writable_status_change), view);
#endif
			}
							    
		}
		break;
	case PROP_BOOK:
		g_object_set (priv->adapter,
			      "book", g_value_get_object (value),
			      NULL);
#if notyet
		set_empty_message (view);
#endif
		break;
	case PROP_QUERY:
		g_object_set (priv->adapter,
			      "query", g_value_get_string (value),
			      NULL);
		break;
	case PROP_EDITABLE:
		g_object_set (priv->adapter,
			      "editable", g_value_get_boolean (value),
			      NULL);
#if notyet
		set_empty_message (view);
#endif
		break;
	case PROP_COLUMN_WIDTH: {
		int new_width = g_value_get_int (value);
		if (new_width != priv->column_width) {
			priv->column_width = new_width;
			/* XXX emit column width changed signal */
		}
		break;
	}
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
eab_minicard_view_get_property (GObject *object,
				guint prop_id,
				GValue *value,
				GParamSpec *pspec)
{
	EABMinicardView *view;
	EABMinicardViewPrivate *priv;

	view = EAB_MINICARD_VIEW (object);
	priv = view->priv;

	switch (prop_id) {
	case PROP_ADAPTER:
		g_value_set_object (value, priv->adapter);
		break;
	case PROP_BOOK:
		g_object_get_property (G_OBJECT (priv->adapter),
				       "book", value);
		break;
	case PROP_QUERY:
		g_object_get_property (G_OBJECT (priv->adapter),
				       "query", value);
		break;
	case PROP_EDITABLE:
		g_object_get_property (G_OBJECT (priv->adapter),
				       "editable", value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
eab_minicard_view_dispose (GObject *object)
{
	EABMinicardView *view = EAB_MINICARD_VIEW (object);

	if (view->priv) {
		int i;

		g_array_free (view->priv->column_starts, TRUE);
		g_array_free (view->priv->minicards, TRUE);

		for (i = 0; i < E_CONTACT_LAST_SIMPLE_STRING; i ++) {
			if (view->priv->label_layout_cache[i])
				g_object_unref (view->priv->label_layout_cache[i]);
		}
		g_free (view->priv);
		view->priv = NULL;
	}
}

static gboolean
eab_minicard_view_expose (GtkWidget *widget,
			  GdkEventExpose *event)
{
	EABMinicardView *view = EAB_MINICARD_VIEW (widget);
	EABMinicardViewPrivate *priv = view->priv;
	int x, i;

	x = HBORDER;

	for (i = 0; i < priv->column_starts->len; i ++) {
		int y;
		int j;
		int start_idx, end_idx;
		GdkRectangle column_rect, intersection;

		start_idx = g_array_index (priv->column_starts, int, i);
		if (i < priv->column_starts->len - 1)
			end_idx = g_array_index (priv->column_starts, int, i+1);
		else
			end_idx = priv->minicards->len;

		column_rect.x = x;
		column_rect.y = VBORDER;
		column_rect.width = priv->column_width;
		column_rect.height = widget->allocation.height - 2 * VBORDER;

		y = VBORDER;
		if (gdk_rectangle_intersect (&column_rect, &event->area, &intersection)) {
			for (j = start_idx; j < end_idx; j ++) {
				int unsorted = e_sorter_sorted_to_model (E_SORTER (priv->sorter), j);
				EABMinicard *minicard;
				GdkRectangle minicard_rect, unused;

				minicard = &g_array_index(priv->minicards, EABMinicard, unsorted);

				minicard_rect.x = x;
				minicard_rect.y = y;
				minicard_rect.width = priv->column_width;
				minicard_rect.height = minicard->total_height;

				if (gdk_rectangle_intersect (&intersection, &minicard_rect, &unused)) {
					minicard->x = x;
					minicard->y = y;
					/* XXX we should use the resulting intersection as a clip rect, shouldn't we? */
					draw_minicard (view, minicard,
						       e_selection_model_is_row_selected (priv->selection, unsorted));
				}

				y += INTERCONTACT_SPACING + minicard->total_height;
			}
		}

		x += priv->column_width + COLUMN_SPACING;
		if (i < priv->column_starts->len - 1) {
			/* draw a separator line */
			GdkGC *gc;
			GdkColor col;

			gc = gdk_gc_new (widget->window);

			col.pixel = 0; /* XXX */
			gdk_gc_set_foreground(gc, &col);

			gdk_draw_rectangle (widget->window,
					    widget->style->bg_gc[GTK_STATE_ACTIVE],
					    TRUE, x, VBORDER, LINE_WIDTH, widget->allocation.height - 2 * VBORDER);

			g_object_unref (gc);

			x += COLUMN_SPACING;
		}
	}

	return TRUE;
}

static void
eab_minicard_view_size_request (GtkWidget      *widget,
				GtkRequisition *requisition)
{
	EABMinicardView *view = EAB_MINICARD_VIEW (widget);
	EABMinicardViewPrivate *priv = view->priv;

	requisition->height = 0;
	requisition->width = 2 * HBORDER + priv->column_starts->len * priv->column_width + (priv->column_starts->len - 1) * 2 * COLUMN_SPACING;
}

static void
eab_minicard_view_size_allocate (GtkWidget     *widget,
				 GtkAllocation *allocation)
{
	EABMinicardView *view = EAB_MINICARD_VIEW (widget);
	EABMinicardViewPrivate *priv = view->priv;

	if (priv->prev_height != allocation->height)
		reflow_on_idle (EAB_MINICARD_VIEW (widget), -1);

	priv->prev_height = allocation->height;

	if (GTK_WIDGET_CLASS (parent_class)->size_allocate)
		GTK_WIDGET_CLASS (parent_class)->size_allocate (widget, allocation);
}

static void
eab_minicard_view_realize (GtkWidget *widget)
{
	EABMinicardView *view;
	GdkWindowAttr attributes;
	gint attributes_mask;

	view = EAB_MINICARD_VIEW (widget);
	GTK_WIDGET_SET_FLAGS (widget, GTK_REALIZED);

	attributes.window_type = GDK_WINDOW_CHILD;
	attributes.x = widget->allocation.x;
	attributes.y = widget->allocation.y;
	attributes.width = widget->allocation.width;
	attributes.height = widget->allocation.height;
	attributes.wclass = GDK_INPUT_OUTPUT;
	attributes.visual = gtk_widget_get_visual (widget);
	attributes.colormap = gtk_widget_get_colormap (widget);
	attributes.event_mask = (gtk_widget_get_events (widget) |
				 GDK_EXPOSURE_MASK |
				 GDK_POINTER_MOTION_MASK |
				 GDK_BUTTON_PRESS_MASK |
				 GDK_BUTTON_RELEASE_MASK);

	attributes_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL | GDK_WA_COLORMAP;

	widget->window = gdk_window_new (gtk_widget_get_parent_window (widget), &attributes, attributes_mask);
	gdk_window_set_user_data (widget->window, view);

	widget->style = gtk_style_attach (widget->style, widget->window);
	gtk_style_set_background (widget->style, widget->window, GTK_STATE_NORMAL);
}

static gboolean
_over_column_handle (EABMinicardView *view,
		     gint x,
		     gint *handle_num)
{
	EABMinicardViewPrivate *priv = view->priv;
	int i;

	for (i = 0; i < priv->column_starts->len - 1; i ++) {
		int column_center = HBORDER + (i + 1) * priv->column_width + ((i + 1) * 2 - 1) * COLUMN_SPACING;
		int distance = column_center - x;
		int half_width = LINE_WIDTH / 2;

		if (distance >= (-1 * half_width) && distance <= half_width) {
			*handle_num = i;
			return TRUE;
		}
	}

	*handle_num = 0;
	return FALSE;
}

/* returns the MODEL index of the contact that contains (x,y) in its
   rectangle */
static int
_over_contact (EABMinicardView *view,
	       int x, int y)
{
	EABMinicardViewPrivate *priv = view->priv;
	int col;

	/* easy case - if we aren't in the correct Y range return immediately */
	if (y < VBORDER || y > GTK_WIDGET (view)->allocation.height - VBORDER) {
		printf ("click outside of vertical area\n");
		return -1;
	}

	for (col = 0; col < priv->column_starts->len; col ++) {
		int column_x = HBORDER + col * (priv->column_width + 2 * COLUMN_SPACING);

		if (column_x <= x && x <= column_x + priv->column_width) {
			/* k, we're in this column (col).  find out if we're over one of the contacts in it */
			int start_idx, end_idx;
			int view_idx;

			printf ("click in column %d\n", col);

			start_idx = g_array_index (priv->column_starts, int, col);

			if (col < priv->column_starts->len - 1)
				end_idx = g_array_index (priv->column_starts, int, col+1);
			else
				end_idx = priv->minicards->len;

			printf ("extents in view indices [%d, %d]\n", start_idx, end_idx);

			for (view_idx = start_idx; view_idx < end_idx; view_idx ++) {
				int unsorted = e_sorter_sorted_to_model (E_SORTER (priv->sorter), view_idx);
				EABMinicard *minicard = &g_array_index(priv->minicards, EABMinicard, unsorted);

				if (minicard->y <= y && y <= minicard->y + minicard->total_height) {
					/* k, found the contact */
					return unsorted;
				}
			}
		}
	}

	return -1;
}

static gboolean
eab_minicard_view_button_press (GtkWidget      *widget,
				GdkEventButton *event)
{
	EABMinicardView *view = EAB_MINICARD_VIEW (widget);
	EABMinicardViewPrivate *priv = view->priv;
	int x, y;

	if (event->button == 1) {
		int column_num;
		gdk_window_get_pointer (widget->window, &x, &y, NULL);

		if (event->type == GDK_2BUTTON_PRESS) {
			printf ("double click at %d,%d\n", x, y);
		}
		else {
			printf ("button press at %d,%d\n", x, y);

			if (_over_column_handle (view, x, &column_num)) {
				/* start a column drag */
				printf ("STARTING COLUMN_DRAG\n");
				priv->in_column_drag = TRUE;
				priv->column_dragged = column_num;

				gdk_pointer_grab (widget->window,
						  FALSE,
						  GDK_POINTER_MOTION_MASK | GDK_BUTTON_RELEASE_MASK,
						  NULL, NULL, GDK_CURRENT_TIME);

				priv->orig_column_x = x;
				priv->orig_column_width = priv->column_width;
			}
			else {
				/* are we over a contact?  if so, select it */
				int model_idx = _over_contact (view, x, y);

				if (model_idx != -1) {
					printf (" + model_idx = %d\n", model_idx);

					e_selection_model_maybe_do_something(priv->selection, model_idx, 0, event->state);

					priv->drag_button_down = TRUE;
				}
			}
		}
	}
	else if (event->button == 3) {
		int rv;
		g_signal_emit (view, signals[RIGHT_CLICK], 0, event, &rv);
		return rv;
	}

	return TRUE;
}

static gboolean
eab_minicard_view_button_release (GtkWidget *widget,
				  GdkEventButton *event)
{
	EABMinicardView *view = EAB_MINICARD_VIEW (widget);
	EABMinicardViewPrivate *priv = view->priv;

	if (event->button == 1) {
		if (priv->in_column_drag) {
			printf ("STOPPING COLUMN_DRAG\n");
			gdk_display_pointer_ungrab (gdk_drawable_get_display (event->window), GDK_CURRENT_TIME);
			priv->in_column_drag = FALSE;
		}
		else {
			priv->drag_button_down = FALSE;
		}
	}

	return TRUE;
}

static gboolean
eab_minicard_view_motion (GtkWidget      *widget,
			  GdkEventMotion *event)
{
	EABMinicardView *view = EAB_MINICARD_VIEW (widget);
	EABMinicardViewPrivate *priv = view->priv;
	gint x, y;

	gdk_window_get_pointer (widget->window, &x, &y, NULL);

	if (priv->in_column_drag) {
		int delta = (x - priv->orig_column_x) / (priv->column_dragged + 1);
		int new_column_width = priv->orig_column_width + delta;

		if (new_column_width < MIN_COLUMN_WIDTH)
			new_column_width = MIN_COLUMN_WIDTH;
		else if (new_column_width > MAX_COLUMN_WIDTH)
			new_column_width = MAX_COLUMN_WIDTH;

		if (new_column_width != priv->column_width) {
			priv->column_width = new_column_width;

			gtk_widget_queue_resize (widget);
		}
	}
	else if (priv->drag_button_down && event->state & GDK_BUTTON1_MASK) {
		if (MAX (abs (priv->button_x - x),
			 abs (priv->button_y - y)) > 3) {
			gint ret_val;

			ret_val = eab_minicard_view_drag_begin(view, (GdkEvent*)event);

			priv->drag_button_down = FALSE;

			return ret_val;
		}
	}
	else {
		int view_idx;
		gint column_num;
		gboolean over_column;

		/* if we're over a column line, change the cursor */
		over_column = _over_column_handle (view, x, &column_num);
		if (over_column ^ priv->column_cursor_shown) {
			priv->column_cursor_shown = over_column;
			gdk_window_set_cursor (widget->window,
					       over_column ? gdk_cursor_new_for_display (gtk_widget_get_display (widget),
											 GDK_SB_H_DOUBLE_ARROW) : NULL);
		}
	}
  
	return TRUE;
}

static void
eab_minicard_view_class_init (GObjectClass *object_class)
{
	GtkWidgetClass *widget_class  = GTK_WIDGET_CLASS (object_class);

	parent_class = g_type_class_peek_parent (object_class);

	object_class->set_property    = eab_minicard_view_set_property;
	object_class->get_property    = eab_minicard_view_get_property;
	object_class->dispose         = eab_minicard_view_dispose;

	widget_class->expose_event    = eab_minicard_view_expose;
	widget_class->button_press_event  = eab_minicard_view_button_press;
	widget_class->button_release_event  = eab_minicard_view_button_release;
	widget_class->motion_notify_event = eab_minicard_view_motion;
	widget_class->realize         = eab_minicard_view_realize;
	widget_class->size_request    = eab_minicard_view_size_request;
	widget_class->size_allocate   = eab_minicard_view_size_allocate;
	widget_class->drag_data_get   = eab_minicard_view_drag_data_get;

	g_object_class_install_property (object_class, PROP_ADAPTER, 
					 g_param_spec_object ("adapter",
							      _("Adapter"),
							      /*_( */"XXX blurb" /*)*/,
							      E_TYPE_ADDRESSBOOK_REFLOW_ADAPTER,
							      G_PARAM_READWRITE));

	g_object_class_install_property (object_class, PROP_BOOK, 
					 g_param_spec_object ("book",
							      _("Book"),
							      /*_( */"XXX blurb" /*)*/,
							      E_TYPE_BOOK,
							      G_PARAM_READWRITE));

	g_object_class_install_property (object_class, PROP_QUERY, 
					 g_param_spec_string ("query",
							      _("Query"),
							      /*_( */"XXX blurb" /*)*/,
							      NULL,
							      G_PARAM_READWRITE));

	g_object_class_install_property (object_class, PROP_EDITABLE, 
					 g_param_spec_boolean ("editable",
							       _("Editable"),
							       /*_( */"XXX blurb" /*)*/,
							       FALSE,
							       G_PARAM_READWRITE));

	g_object_class_install_property (object_class, PROP_COLUMN_WIDTH, 
					 g_param_spec_int ("column_width",
							   _("Column Width"),
							   /*_( */"XXX blurb" /*)*/,
							   MIN_COLUMN_WIDTH, MAX_COLUMN_WIDTH,
							   DEFAULT_COLUMN_WIDTH,
							   G_PARAM_READWRITE));

	signals [SELECTION_CHANGE] =
		g_signal_new ("selection_change",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (EABMinicardViewClass, selection_change),
			      NULL, NULL,
			      eab_marshal_NONE__NONE,
			      G_TYPE_NONE, 0);

	signals [COLUMN_WIDTH_CHANGED] =
		g_signal_new ("column_width_changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (EABMinicardViewClass, column_width_changed),
			      NULL, NULL,
			      eab_marshal_NONE__DOUBLE,
			      G_TYPE_NONE, 1, G_TYPE_INT);

	signals [RIGHT_CLICK] =
		g_signal_new ("right_click",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (EABMinicardViewClass, right_click),
			      NULL, NULL,
			      eab_marshal_INT__POINTER,
			      G_TYPE_INT, 1, G_TYPE_POINTER);
}

GType
eab_minicard_view_get_type (void)
{
	static GType type = 0;

	if (!type) {
		static const GTypeInfo info =  {
			sizeof (EABMinicardViewClass),
			NULL,           /* base_init */
			NULL,           /* base_finalize */
			(GClassInitFunc) eab_minicard_view_class_init,
			NULL,           /* class_finalize */
			NULL,           /* class_data */
			sizeof (EABMinicardView),
			0,             /* n_preallocs */
			(GInstanceInitFunc) eab_minicard_view_init,
		};

		type = g_type_register_static (PARENT_TYPE, "EABMinicardView", &info, 0);
	}

	return type;
}



#if 0
	case PROP_COLUMN_WIDTH:
		if (reflow->column_width != g_value_get_double (value)) {
			GtkAdjustment *adjustment = gtk_layout_get_hadjustment(GTK_LAYOUT(item->canvas));
			double old_width = reflow->column_width;

			reflow->column_width = g_value_get_double (value);
			adjustment->step_increment = (reflow->column_width + E_REFLOW_FULL_GUTTER) / 2;
			adjustment->page_increment = adjustment->page_size - adjustment->step_increment;
			gtk_adjustment_changed(adjustment);
			e_reflow_resize_children(item);
			e_canvas_item_request_reflow(item);

			reflow->need_column_resize = TRUE;
			gnome_canvas_item_request_update(item);

			if (old_width != reflow->column_width)
				column_width_changed (reflow);
		}
		break;
	}
#endif

#if notyet
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
					int unsorted = e_sorter_sorted_to_model (E_SORTER (priv->sorter), i);
					GnomeCanvasItem *item = reflow->items[unsorted];
					EFocus has_focus;
					if (item) {
						g_object_get(item,
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
							
							unsorted = e_sorter_sorted_to_model (E_SORTER (priv->sorter), i);
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
						if ( button->y >= E_REFLOW_BORDER_WIDTH && button->y <= reflow->total_height - E_REFLOW_BORDER_WIDTH && n_x < E_REFLOW_FULL_GUTTER && max_x > button->x ) {
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
					gnome_canvas_request_redraw(item->canvas, 0, 0, reflow->width, reflow->total_height);
					column_width_changed (reflow);
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

				if ( motion->y >= E_REFLOW_BORDER_WIDTH && motion->y <= reflow->total_height - E_REFLOW_BORDER_WIDTH && n_x < E_REFLOW_FULL_GUTTER && max_x > motion->x) {
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
				if ( crossing->y >= E_REFLOW_BORDER_WIDTH && crossing->y <= reflow->total_height - E_REFLOW_BORDER_WIDTH && n_x < E_REFLOW_FULL_GUTTER && max_x > crossing->x) {
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
				if ( !( crossing->y >= E_REFLOW_BORDER_WIDTH && crossing->y <= reflow->total_height - E_REFLOW_BORDER_WIDTH && n_x < E_REFLOW_FULL_GUTTER ) ) {
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
#endif

#if notyet
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
	height_rect = reflow->total_height - (E_REFLOW_BORDER_WIDTH * 2);

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
		height_rect = reflow->total_height - (E_REFLOW_BORDER_WIDTH * 2);

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
	if ( y1 < y0 + reflow->total_height )
		y1 = y0 + reflow->total_height;
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
		if ( x1 < E_REFLOW(item)->total_height )
			x1 = E_REFLOW(item)->total_height;

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
			height_rect = reflow->total_height - (E_REFLOW_BORDER_WIDTH * 2);
			
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
			height_rect = reflow->total_height - (E_REFLOW_BORDER_WIDTH * 2);
			
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
		int unsorted = e_sorter_sorted_to_model (E_SORTER (priv->sorter), i);
		if (next_column < reflow->column_count && i == reflow->columns[next_column]) {
			running_height = E_REFLOW_BORDER_WIDTH;
			running_width += reflow->column_width + E_REFLOW_FULL_GUTTER;
			next_column ++;
		}

		if (unsorted >= 0 && reflow->items[unsorted]) {
			e_canvas_item_move_absolute(GNOME_CANVAS_ITEM(reflow->items[unsorted]),
						    (double) running_width,
						    (double) running_height);
			running_height += reflow->total_heights[unsorted] + E_REFLOW_BORDER_WIDTH;
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
			if (event->button.button == 1) {
				reflow->maybe_did_something = 
					e_selection_model_maybe_do_something(reflow->selection, row, 0, event->button.state);
				reflow->maybe_in_drag = TRUE;
			} else {
				e_selection_model_do_something(reflow->selection, row, 0, event->button.state);
			}
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
	case GDK_BUTTON_RELEASE:
		if (event->button.button == 1) {
			if (reflow->maybe_in_drag) {
				reflow->maybe_in_drag = FALSE;
				if (!reflow->maybe_did_something) {
					row = er_find_item (reflow, item);
					e_selection_model_do_something(reflow->selection, row, 0, event->button.state);
				}
			}
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
	GObjectClass *object_class;
	GnomeCanvasItemClass *item_class;

	object_class = (GObjectClass*) klass;
	item_class = (GnomeCanvasItemClass *) klass;

	parent_class = g_type_class_ref (PARENT_TYPE);

	object_class->set_property  = e_reflow_set_property;
	object_class->get_property  = e_reflow_get_property;
	object_class->dispose  = e_reflow_dispose;
  
	/* GnomeCanvasItem method overrides */
	item_class->event      = e_reflow_event;
	item_class->realize    = e_reflow_realize;
	item_class->unrealize  = e_reflow_unrealize;
	item_class->draw       = e_reflow_draw;
	item_class->update     = e_reflow_update;
	item_class->point      = e_reflow_point;

	klass->selection_event = e_reflow_selection_event_real;
	klass->column_width_changed = NULL;

	g_object_class_install_property (object_class, PROP_MINIMUM_WIDTH,
					 g_param_spec_double ("minimum_width",
							      _( "Minimum width" ),
							      _( "Minimum Width" ),
							      0.0, G_MAXDOUBLE, 0.0,
							      G_PARAM_READWRITE));

	g_object_class_install_property (object_class, PROP_WIDTH,
					 g_param_spec_double ("width",
							      _( "Width" ),
							      _( "Width" ),
							      0.0, G_MAXDOUBLE, 0.0,
							      G_PARAM_READABLE));


	g_object_class_install_property (object_class, PROP_HEIGHT,
					 g_param_spec_double ("height",
							      _( "Height" ),
							      _( "Height" ),
							      0.0, G_MAXDOUBLE, 0.0,
							      G_PARAM_READWRITE));

	g_object_class_install_property (object_class, PROP_EMPTY_MESSAGE,
					 g_param_spec_string ("empty_message",
							      _( "Empty message" ),
							      _( "Empty message" ),
							      NULL,
							      G_PARAM_READWRITE));

	g_object_class_install_property (object_class, PROP_MODEL,
					 g_param_spec_object ("model",
							      _( "Reflow model" ),
							      _( "Reflow model" ),
							      E_REFLOW_MODEL_TYPE,
							      G_PARAM_READWRITE));

	g_object_class_install_property (object_class, PROP_COLUMN_WIDTH,
					 g_param_spec_double ("column_width",
							      _( "Column width" ),
							      _( "Column width" ),
							      0.0, G_MAXDOUBLE, 150.0,
							      G_PARAM_READWRITE));

	signals [SELECTION_EVENT] =
		g_signal_new ("selection_event",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (EReflowClass, selection_event),
			      NULL, NULL,
			      e_marshal_INT__OBJECT_BOXED,
			      G_TYPE_INT, 2, G_TYPE_OBJECT,
			      GDK_TYPE_EVENT);

	signals [COLUMN_WIDTH_CHANGED] =
		g_signal_new ("column_width_changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (EReflowClass, column_width_changed),
			      NULL, NULL,
			      e_marshal_NONE__DOUBLE,
			      G_TYPE_NONE, 1, G_TYPE_DOUBLE);
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
	reflow->need_reflow_columns       = FALSE;

	reflow->maybe_did_something       = FALSE;
	reflow->maybe_in_drag             = FALSE;

	reflow->default_cursor_shown      = TRUE;
	reflow->arrow_cursor              = NULL;
	reflow->default_cursor            = NULL;

	reflow->cursor_row                = -1;

	reflow->incarnate_idle_id         = 0;
	reflow->set_scroll_adjustments_id = 0;

	reflow->selection                 = E_SELECTION_MODEL (e_selection_model_simple_new());
	reflow->sorter                    = e_sorter_array_new (er_compare, reflow);

	g_object_set (reflow->selection,
		      "sorter", reflow->sorter,
		      NULL);

	reflow->selection_changed_id = 
		g_signal_connect(reflow->selection, "selection_changed",
				 G_CALLBACK (selection_changed), reflow);
	reflow->selection_row_changed_id = 
		g_signal_connect(reflow->selection, "selection_row_changed",
				 G_CALLBACK (selection_row_changed), reflow);
	reflow->cursor_changed_id = 
		g_signal_connect(reflow->selection, "cursor_changed",
				 G_CALLBACK (cursor_changed), reflow);

	e_canvas_item_set_reflow_callback(GNOME_CANVAS_ITEM(reflow), e_reflow_reflow);
}
#endif
