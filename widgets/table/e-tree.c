/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * e-tree.c: A graphical view of a tree.
 *
 * Author:
 *   Miguel de Icaza (miguel@ximian.com)
 *   Chris Lahey <clahey@ximian.com>
 *
 * Copyright 1999, 2000, 2001, Ximian, Inc
 */
#include <config.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdio.h>
#include "gal/util/e-i18n.h"
#include <libgnomeui/gnome-canvas.h>
#include <gtk/gtksignal.h>

#include "gal/util/e-util.h"
#include "gal/widgets/e-canvas.h"
#include "e-tree.h"
#include "e-table-header-item.h"
#include "e-table-header-utils.h"
#include "e-table-subset.h"
#include "e-table-item.h"
#include "e-table-group.h"
#include "e-table-group-leaf.h"
#include "e-table-specification.h"
#include "e-table-state.h"
#include "e-table-column-specification.h"

#include "e-table-utils.h"

#define COLUMN_HEADER_HEIGHT 16

#define PARENT_TYPE gtk_table_get_type ()

static GtkObjectClass *parent_class;

enum {
	CURSOR_CHANGE,
	CURSOR_ACTIVATED,
	SELECTION_CHANGE,
	DOUBLE_CLICK,
	RIGHT_CLICK,
	CLICK,
	KEY_PRESS,

	TREE_DRAG_BEGIN,
	TREE_DRAG_END,
	TREE_DRAG_DATA_GET,
	TREE_DRAG_DATA_DELETE,

	TREE_DRAG_LEAVE,
	TREE_DRAG_MOTION,
	TREE_DRAG_DROP,
	TREE_DRAG_DATA_RECEIVED,

	LAST_SIGNAL
};

enum {
	ARG_0,
	ARG_LENGTH_THRESHOLD,
};

static gint et_signals [LAST_SIGNAL] = { 0, };

static void et_grab_focus (GtkWidget *widget);

static void et_drag_begin (GtkWidget *widget,
			   GdkDragContext *context,
			   ETree *et);
static void et_drag_end (GtkWidget *widget,
			 GdkDragContext *context,
			 ETree *et);
static void et_drag_data_get(GtkWidget *widget,
			     GdkDragContext *context,
			     GtkSelectionData *selection_data,
			     guint info,
			     guint time,
			     ETree *et);
static void et_drag_data_delete(GtkWidget *widget,
				GdkDragContext *context,
				ETree *et);

static void et_drag_leave(GtkWidget *widget,
			  GdkDragContext *context,
			  guint time,
			  ETree *et);
static gboolean et_drag_motion(GtkWidget *widget,
			       GdkDragContext *context,
			       gint x,
			       gint y,
			       guint time,
			       ETree *et);
static gboolean et_drag_drop(GtkWidget *widget,
			     GdkDragContext *context,
			     gint x,
			     gint y,
			     guint time,
			     ETree *et);
static void et_drag_data_received(GtkWidget *widget,
				  GdkDragContext *context,
				  gint x,
				  gint y,
				  GtkSelectionData *selection_data,
				  guint info,
				  guint time,
				  ETree *et);
static gint e_tree_drag_source_event_cb (GtkWidget      *widget,
					 GdkEvent       *event,
					 ETree         *tree);

static gint et_focus (GtkContainer *container, GtkDirectionType direction);

static void
et_destroy (GtkObject *object)
{
	ETree *et = E_TREE (object);

	if (et->reflow_idle_id)
		g_source_remove(et->reflow_idle_id);
	et->reflow_idle_id = 0;

	gtk_object_unref (GTK_OBJECT (et->model));
	gtk_object_unref (GTK_OBJECT (et->sorted));
	gtk_object_unref (GTK_OBJECT (et->full_header));
	gtk_object_unref (GTK_OBJECT (et->header));
	gtk_object_unref (GTK_OBJECT (et->sort_info));
	gtk_object_unref (GTK_OBJECT (et->selection));
	if (et->spec)
		gtk_object_unref (GTK_OBJECT (et->spec));

	if (et->header_canvas != NULL)
		gtk_widget_destroy (GTK_WIDGET (et->header_canvas));

	gtk_widget_destroy (GTK_WIDGET (et->table_canvas));

	(*parent_class->destroy)(object);
}

static void
e_tree_init (GtkObject *object)
{
	ETree *e_tree = E_TREE (object);
	GtkTable *gtk_table = GTK_TABLE (object);

	GTK_WIDGET_SET_FLAGS (e_tree, GTK_CAN_FOCUS);

	gtk_table->homogeneous = FALSE;

	e_tree->sort_info = NULL;
	e_tree->reflow_idle_id = 0;

	e_tree->draw_grid = 1;
	e_tree->draw_focus = 1;
	e_tree->cursor_mode = E_CURSOR_SIMPLE;
	e_tree->length_threshold = 200;

	e_tree->horizontal_scrolling = FALSE;

	e_tree->drop_row = -1;
	e_tree->drop_path = NULL;
	e_tree->drop_col = -1;

	e_tree->drag_row = -1;
	e_tree->drag_path = NULL;
	e_tree->drag_col = -1;

	e_tree->site = NULL;
	e_tree->drag_source_button_press_event_id = 0;
	e_tree->drag_source_motion_notify_event_id = 0;

	e_tree->selection = e_table_selection_model_new();
	e_tree->spec = NULL;
}

/* Grab_focus handler for the ETree */
static void
et_grab_focus (GtkWidget *widget)
{
	ETree *e_tree;

	e_tree = E_TREE (widget);

	gtk_widget_grab_focus (GTK_WIDGET (e_tree->table_canvas));
}

/* Focus handler for the ETree */
static gint
et_focus (GtkContainer *container, GtkDirectionType direction)
{
	ETree *e_tree;

	e_tree = E_TREE (container);

	if (container->focus_child) {
		gtk_container_set_focus_child (container, NULL);
		return FALSE;
	}

	return gtk_container_focus (GTK_CONTAINER (e_tree->table_canvas), direction);
}

static void
header_canvas_size_allocate (GtkWidget *widget, GtkAllocation *alloc, ETree *e_tree)
{
	gnome_canvas_set_scroll_region (
		GNOME_CANVAS (e_tree->header_canvas),
		0, 0, alloc->width - 1, /*  COLUMN_HEADER_HEIGHT - 1 */
		E_TABLE_HEADER_ITEM (e_tree->header_item)->height - 1);

	/* When the header item is created ->height == 0,
	   as the font is only created when everything is realized.
	   So we set the usize here as well, so that the size of the
	   header is correct */
	if (GTK_WIDGET (e_tree->header_canvas)->allocation.height !=
	    E_TABLE_HEADER_ITEM (e_tree->header_item)->height)
		gtk_widget_set_usize (GTK_WIDGET (e_tree->header_canvas), -1,
				      E_TABLE_HEADER_ITEM (e_tree->header_item)->height);
}

static void
e_tree_setup_header (ETree *e_tree)
{
	char *pointer;
	e_tree->header_canvas = GNOME_CANVAS (e_canvas_new ());
	GTK_WIDGET_UNSET_FLAGS (e_tree->header_canvas, GTK_CAN_FOCUS);

	gtk_widget_show (GTK_WIDGET (e_tree->header_canvas));

	pointer = g_strdup_printf("%p", e_tree);

	e_tree->header_item = gnome_canvas_item_new (
		gnome_canvas_root (e_tree->header_canvas),
		e_table_header_item_get_type (),
		"ETableHeader", e_tree->header,
		"full_header", e_tree->full_header,
		"sort_info", e_tree->sort_info,
		"dnd_code", pointer,
		/*		"table", e_tree, FIXME*/
		NULL);

	g_free(pointer);

	gtk_signal_connect (
		GTK_OBJECT (e_tree->header_canvas), "size_allocate",
		GTK_SIGNAL_FUNC (header_canvas_size_allocate), e_tree);

	gtk_widget_set_usize (GTK_WIDGET (e_tree->header_canvas), -1,
			      E_TABLE_HEADER_ITEM (e_tree->header_item)->height);
}

static gboolean
tree_canvas_reflow_idle (ETree *e_tree)
{
	gdouble height, width;
	gdouble item_height;
	GtkAllocation *alloc = &(GTK_WIDGET (e_tree->table_canvas)->allocation);

	gtk_object_get (GTK_OBJECT (e_tree->item),
			"height", &height,
			"width", &width,
			NULL);
	item_height = height;
	height = MAX ((int)height, alloc->height);
	width = MAX((int)width, alloc->width);
	/* I have no idea why this needs to be -1, but it works. */
	gnome_canvas_set_scroll_region (
		GNOME_CANVAS (e_tree->table_canvas),
		0, 0, width - 1, height - 1);
	gtk_object_set (GTK_OBJECT (e_tree->white_item),
			"y1", item_height + 1,
			"x2", width,
			"y2", height,
			NULL);
	e_tree->reflow_idle_id = 0;
	return FALSE;
}

static void
tree_canvas_size_allocate (GtkWidget *widget, GtkAllocation *alloc,
			    ETree *e_tree)
{
	gdouble width;
	gdouble height;
	gdouble item_height;

	width = alloc->width;
	gtk_object_get (GTK_OBJECT (e_tree->item),
			"height", &height,
			NULL);
	item_height = height;
	height = MAX ((int)height, alloc->height);

	gtk_object_set (GTK_OBJECT (e_tree->item),
			"width", width,
			NULL);
	gtk_object_set (GTK_OBJECT (e_tree->header),
			"width", width,
			NULL);
	gtk_object_set (GTK_OBJECT (e_tree->white_item),
			"y1", item_height + 1,
			"x2", width,
			"y2", height,
			NULL);
	if (e_tree->reflow_idle_id)
		g_source_remove(e_tree->reflow_idle_id);
	tree_canvas_reflow_idle(e_tree);
}

static void
tree_canvas_reflow (GnomeCanvas *canvas, ETree *e_tree)
{
	if (!e_tree->reflow_idle_id)
		e_tree->reflow_idle_id = g_idle_add_full (400, (GSourceFunc) tree_canvas_reflow_idle, e_tree, NULL);
}

static void
item_cursor_change (ETableItem *eti, int row, ETree *et)
{
	ETreePath path = e_tree_table_adapter_node_at_row(et->etta, row);
	path = e_tree_sorted_view_to_model_path(et->sorted, path);
	gtk_signal_emit (GTK_OBJECT (et),
			 et_signals [CURSOR_CHANGE],
			 row, path);
}

static void
item_cursor_activated (ETableItem *eti, int row, ETree *et)
{
	ETreePath path = e_tree_table_adapter_node_at_row(et->etta, row);
	path = e_tree_sorted_view_to_model_path(et->sorted, path);
	gtk_signal_emit (GTK_OBJECT (et),
			 et_signals [CURSOR_ACTIVATED],
			 row, path);
}

static void
item_double_click (ETableItem *eti, int row, int col, GdkEvent *event, ETree *et)
{
	ETreePath path = e_tree_table_adapter_node_at_row(et->etta, row);
	path = e_tree_sorted_view_to_model_path(et->sorted, path);
	gtk_signal_emit (GTK_OBJECT (et),
			 et_signals [DOUBLE_CLICK],
			 row, path, col, event);
}

static gint
item_right_click (ETableItem *eti, int row, int col, GdkEvent *event, ETree *et)
{
	int return_val = 0;
	ETreePath path = e_tree_table_adapter_node_at_row(et->etta, row);
	path = e_tree_sorted_view_to_model_path(et->sorted, path);
	gtk_signal_emit (GTK_OBJECT (et),
			 et_signals [RIGHT_CLICK],
			 row, path, col, event, &return_val);
	return return_val;
}

static gint
item_click (ETableItem *eti, int row, int col, GdkEvent *event, ETree *et)
{
	int return_val = 0;
	ETreePath path = e_tree_table_adapter_node_at_row(et->etta, row);
	path = e_tree_sorted_view_to_model_path(et->sorted, path);
	gtk_signal_emit (GTK_OBJECT (et),
			 et_signals [CLICK],
			 row, path, col, event, &return_val);
	return return_val;
}

static gint
item_key_press (ETableItem *eti, int row, int col, GdkEvent *event, ETree *et)
{
	int return_val = 0;
	ETreePath path = e_tree_table_adapter_node_at_row(et->etta, row);
	GdkEventKey *key = (GdkEventKey *) event;
	GdkEventButton click;

	path = e_tree_sorted_view_to_model_path(et->sorted, path);

	switch (key->keyval) {
	case GDK_Page_Down:
		gtk_adjustment_set_value(
			gtk_layout_get_vadjustment (GTK_LAYOUT (et->table_canvas)),
			CLAMP(gtk_layout_get_vadjustment (GTK_LAYOUT (et->table_canvas))->value +
			      (gtk_layout_get_vadjustment (GTK_LAYOUT (et->table_canvas))->page_size - 20),
			      0,
			      gtk_layout_get_vadjustment (GTK_LAYOUT (et->table_canvas))->upper -
			      gtk_layout_get_vadjustment (GTK_LAYOUT (et->table_canvas))->page_size));
		click.type = GDK_BUTTON_PRESS;
		click.window = GTK_LAYOUT (et->table_canvas)->bin_window;
		click.send_event = key->send_event;
		click.time = key->time;
		click.x = 30;
		click.y = gtk_layout_get_vadjustment (GTK_LAYOUT (et->table_canvas))->page_size - 1;
		click.state = key->state;
		click.button = 1;
		gtk_widget_event(GTK_WIDGET(et->table_canvas),
				 (GdkEvent *) &click);
		return_val = 1;
		break;
	case GDK_Page_Up:
		gtk_adjustment_set_value(
			gtk_layout_get_vadjustment (GTK_LAYOUT (et->table_canvas)),
			gtk_layout_get_vadjustment (GTK_LAYOUT (et->table_canvas))->value -
			(gtk_layout_get_vadjustment (GTK_LAYOUT (et->table_canvas))->page_size - 20));
		click.type = GDK_BUTTON_PRESS;
		click.window = GTK_LAYOUT (et->table_canvas)->bin_window;
		click.send_event = key->send_event;
		click.time = key->time;
		click.x = 30;
		click.y = 1;
		click.state = key->state;
		click.button = 1;
		gtk_widget_event(GTK_WIDGET(et->table_canvas),
				 (GdkEvent *) &click);
		return_val = 1;
		break;
	default:
		gtk_signal_emit (GTK_OBJECT (et),
				 et_signals [KEY_PRESS],
				 row, path, col, event, &return_val);
		break;
	}
	return return_val;
}

static void
et_selection_model_selection_change (ETableSelectionModel *etsm, ETable *et)
{
	gtk_signal_emit (GTK_OBJECT (et),
			 et_signals [SELECTION_CHANGE]);
}

static void
et_build_item (ETree *et)
{
	et->item = gnome_canvas_item_new(GNOME_CANVAS_GROUP (gnome_canvas_root(et->table_canvas)),
					 e_table_item_get_type(),
					 "ETableHeader", et->header,
					 "ETableModel", et->etta,
					 "table_selection_model", et->selection,
					 "drawgrid", et->draw_grid,
					 "drawfocus", et->draw_focus,
					 "cursor_mode", et->cursor_mode,
					 "length_threshold", et->length_threshold,
					 NULL);

	gtk_signal_connect (GTK_OBJECT (et->item), "cursor_change",
			    GTK_SIGNAL_FUNC (item_cursor_change), et);
	gtk_signal_connect (GTK_OBJECT (et->item), "cursor_activated",
			    GTK_SIGNAL_FUNC (item_cursor_activated), et);
	gtk_signal_connect (GTK_OBJECT (et->item), "double_click",
			    GTK_SIGNAL_FUNC (item_double_click), et);
	gtk_signal_connect (GTK_OBJECT (et->item), "right_click",
			    GTK_SIGNAL_FUNC (item_right_click), et);
	gtk_signal_connect (GTK_OBJECT (et->item), "click",
			    GTK_SIGNAL_FUNC (item_click), et);
	gtk_signal_connect (GTK_OBJECT (et->item), "key_press",
			    GTK_SIGNAL_FUNC (item_key_press), et);
}

static void
et_canvas_realize (GtkWidget *canvas, ETree *e_tree)
{
	gnome_canvas_item_set(
		e_tree->white_item,
		"fill_color_gdk", &GTK_WIDGET(e_tree->table_canvas)->style->base[GTK_STATE_NORMAL],
		NULL);
}

static void
et_canvas_button_press (GtkWidget *canvas, GdkEvent *event, ETree *e_tree)
{
	if (GTK_WIDGET_HAS_FOCUS(canvas)) {
		GnomeCanvasItem *item = GNOME_CANVAS(canvas)->focused_item;

		if (E_IS_TABLE_ITEM(item)) {
			e_table_item_leave_edit(E_TABLE_ITEM(item));
		}
	}
}

static void
e_tree_setup_table (ETree *e_tree)
{
	e_tree->table_canvas = GNOME_CANVAS (e_canvas_new ());
	gtk_signal_connect (
		GTK_OBJECT (e_tree->table_canvas), "size_allocate",
		GTK_SIGNAL_FUNC (tree_canvas_size_allocate), e_tree);
	gtk_signal_connect (
		GTK_OBJECT (e_tree->table_canvas), "focus_in_event",
		GTK_SIGNAL_FUNC (gtk_widget_queue_draw), e_tree);
	gtk_signal_connect (
		GTK_OBJECT (e_tree->table_canvas), "focus_out_event",
		GTK_SIGNAL_FUNC (gtk_widget_queue_draw), e_tree);

	gtk_signal_connect (
		GTK_OBJECT (e_tree), "drag_begin",
		GTK_SIGNAL_FUNC (et_drag_begin), e_tree);
	gtk_signal_connect (
		GTK_OBJECT (e_tree), "drag_end",
		GTK_SIGNAL_FUNC (et_drag_end), e_tree);
	gtk_signal_connect (
		GTK_OBJECT (e_tree), "drag_data_get",
		GTK_SIGNAL_FUNC (et_drag_data_get), e_tree);
	gtk_signal_connect (
		GTK_OBJECT (e_tree), "drag_data_delete",
		GTK_SIGNAL_FUNC (et_drag_data_delete), e_tree);
	gtk_signal_connect (
		GTK_OBJECT (e_tree), "drag_motion",
		GTK_SIGNAL_FUNC (et_drag_motion), e_tree);
	gtk_signal_connect (
		GTK_OBJECT (e_tree), "drag_leave",
		GTK_SIGNAL_FUNC (et_drag_leave), e_tree);
	gtk_signal_connect (
		GTK_OBJECT (e_tree), "drag_drop",
		GTK_SIGNAL_FUNC (et_drag_drop), e_tree);
	gtk_signal_connect (
		GTK_OBJECT (e_tree), "drag_data_received",
		GTK_SIGNAL_FUNC (et_drag_data_received), e_tree);

	gtk_signal_connect (GTK_OBJECT(e_tree->table_canvas), "reflow",
			    GTK_SIGNAL_FUNC (tree_canvas_reflow), e_tree);

	gtk_widget_show (GTK_WIDGET (e_tree->table_canvas));

	e_tree->white_item = gnome_canvas_item_new(
		gnome_canvas_root(e_tree->table_canvas),
		gnome_canvas_rect_get_type(),
		"x1", (double) 0,
		"y1", (double) 0,
		"x2", (double) 100,
		"y2", (double) 100,
		"fill_color_gdk", &GTK_WIDGET(e_tree->table_canvas)->style->base[GTK_STATE_NORMAL],
		NULL);

	gtk_signal_connect (
		GTK_OBJECT(e_tree->table_canvas), "realize",
		GTK_SIGNAL_FUNC(et_canvas_realize), e_tree);
	gtk_signal_connect (
		GTK_OBJECT(e_tree->table_canvas), "button_press_event",
		GTK_SIGNAL_FUNC(et_canvas_button_press), e_tree);

	et_build_item(e_tree);
}

void
e_tree_set_state_object(ETree *e_tree, ETableState *state)
{
	if (e_tree->header)
		gtk_object_unref(GTK_OBJECT(e_tree->header));
	e_tree->header = e_table_state_to_header (GTK_WIDGET(e_tree), e_tree->full_header, state);
	if (e_tree->header)
		gtk_object_ref(GTK_OBJECT(e_tree->header));

	if (e_tree->sort_info)
		gtk_object_unref(GTK_OBJECT(e_tree->sort_info));

	if (state->sort_info)
		e_tree->sort_info = e_table_sort_info_duplicate(state->sort_info);
	else
		e_tree->sort_info = NULL;

	if (e_tree->header_item)
		gtk_object_set(GTK_OBJECT(e_tree->header_item),
			       "ETableHeader", e_tree->header,
			       "sort_info", e_tree->sort_info,
			       NULL);

	if (e_tree->item)
		gtk_object_set(GTK_OBJECT(e_tree->item),
			       "ETableHeader", e_tree->header,
			       NULL);
}

/**
 * e_tree_set_state:
 * @e_tree: %ETree object that will be modified
 * @state_str: a string with the XML representation of the ETableState.
 *
 * This routine sets the state (as described by %ETableState) of the
 * %ETree object.
 */
void
e_tree_set_state (ETree      *e_tree,
		   const gchar *state_str)
{
	ETableState *state;

	g_return_if_fail(e_tree != NULL);
	g_return_if_fail(E_IS_TREE(e_tree));
	g_return_if_fail(state_str != NULL);

	state = e_table_state_new();
	e_table_state_load_from_string(state, state_str);

	if (state->col_count > 0)
		e_tree_set_state_object(e_tree, state);

	gtk_object_unref(GTK_OBJECT(state));
}

/**
 * e_tree_load_state:
 * @e_tree: %ETree object that will be modified
 * @filename: name of the file containing the state to be loaded into the %ETree
 *
 * An %ETableState will be loaded form the file pointed by @filename into the
 * @e_tree object.
 */
void
e_tree_load_state (ETree      *e_tree,
		    const gchar *filename)
{
	ETableState *state;

	g_return_if_fail(e_tree != NULL);
	g_return_if_fail(E_IS_TREE(e_tree));
	g_return_if_fail(filename != NULL);

	state = e_table_state_new();
	e_table_state_load_from_file(state, filename);

	if (state->col_count > 0)
		e_tree_set_state_object(e_tree, state);

	gtk_object_unref(GTK_OBJECT(state));
}

/**
 * e_tree_get_state_object:
 * @e_tree: %ETree object that will be modified
 *
 * Returns: the %ETreeState object that encapsulates the current
 * state of the @e_tree object
 */
ETableState *
e_tree_get_state_object (ETree *e_tree)
{
	ETableState *state;
	int full_col_count;
	int i, j;

	state = e_table_state_new();
	state->sort_info = e_tree->sort_info;
	gtk_object_ref(GTK_OBJECT(state->sort_info));

	state->col_count = e_table_header_count (e_tree->header);
	full_col_count = e_table_header_count (e_tree->full_header);
	state->columns = g_new(int, state->col_count);
	state->expansions = g_new(double, state->col_count);
	for (i = 0; i < state->col_count; i++) {
		ETableCol *col = e_table_header_get_column(e_tree->header, i);
		state->columns[i] = -1;
		for (j = 0; j < full_col_count; j++) {
			if (col->col_idx == e_table_header_index(e_tree->full_header, j)) {
				state->columns[i] = j;
				break;
			}
		}
		state->expansions[i] = col->expansion;
	}

	return state;
}

gchar          *e_tree_get_state                 (ETree               *e_tree)
{
	ETableState *state;
	gchar *string;

	state = e_tree_get_state_object(e_tree);
	string = e_table_state_save_to_string(state);
	gtk_object_unref(GTK_OBJECT(state));
	return string;
}

/**
 * e_tree_save_state:
 * @e_tree: %ETree object that will be modified
 * @filename: name of the file containing the state to be loaded into the %ETree
 *
 * This routine saves the state of the @e_tree object into the file pointed
 * by @filename
 */
void
e_tree_save_state (ETree      *e_tree,
		    const gchar *filename)
{
	ETableState *state;

	state = e_tree_get_state_object(e_tree);
	e_table_state_save_to_file(state, filename);
	gtk_object_unref(GTK_OBJECT(state));
}

static ETree *
et_real_construct (ETree *e_tree, ETreeModel *etm, ETableExtras *ete,
		   ETableSpecification *specification, ETableState *state)
{
	int row = 0;

	if (ete)
		gtk_object_ref(GTK_OBJECT(ete));
	else
		ete = e_table_extras_new();

	e_tree->draw_grid = specification->draw_grid;
	e_tree->draw_focus = specification->draw_focus;
	e_tree->cursor_mode = specification->cursor_mode;
	e_tree->full_header = e_table_spec_to_full_header(specification, ete);

	e_tree->header = e_table_state_to_header (GTK_WIDGET(e_tree), e_tree->full_header, state);
	e_tree->horizontal_scrolling = specification->horizontal_scrolling;

	e_tree->sort_info = state->sort_info;

	gtk_object_set(GTK_OBJECT(e_tree->header),
		       "sort_info", e_tree->sort_info,
		       NULL);

	e_tree->model = etm;
	gtk_object_ref (GTK_OBJECT (etm));

	e_tree->sorted = e_tree_sorted_new(etm, e_tree->full_header, e_tree->sort_info);

	e_tree->etta = E_TREE_TABLE_ADAPTER(e_tree_table_adapter_new(E_TREE_MODEL(e_tree->sorted)));

	gtk_object_set(GTK_OBJECT(e_tree->selection),
		       "model", E_TABLE_MODEL(e_tree->etta),
		       "selection_mode", specification->selection_mode,
		       "cursor_mode", specification->cursor_mode,
		       NULL);

	gtk_widget_push_visual (gdk_rgb_get_visual ());
	gtk_widget_push_colormap (gdk_rgb_get_cmap ());

	e_tree->sorter = e_sorter_new();

	gtk_object_set (GTK_OBJECT (e_tree->selection),
			"model", e_tree->etta,
			"sorter", e_tree->sorter,
			NULL);

	gtk_signal_connect(GTK_OBJECT(e_tree->selection), "selection_changed",
			   GTK_SIGNAL_FUNC(et_selection_model_selection_change), e_tree);

	if (!specification->no_headers) {
		e_tree_setup_header (e_tree);
	}
	e_tree_setup_table (e_tree);

	gtk_layout_get_vadjustment (GTK_LAYOUT (e_tree->table_canvas))->step_increment = 20;
	gtk_adjustment_changed(gtk_layout_get_vadjustment (GTK_LAYOUT (e_tree->table_canvas)));

	if (!specification->no_headers) {
		/*
		 * The header
		 */
		gtk_table_attach (GTK_TABLE (e_tree), GTK_WIDGET (e_tree->header_canvas),
				  0, 1, 0 + row, 1 + row,
				  GTK_FILL | GTK_EXPAND,
				  GTK_FILL, 0, 0);
		row ++;
	}
	gtk_table_attach (GTK_TABLE (e_tree), GTK_WIDGET (e_tree->table_canvas),
			  0, 1, 0 + row, 1 + row,
			  GTK_FILL | GTK_EXPAND,
			  GTK_FILL | GTK_EXPAND,
			  0, 0);

	gtk_widget_pop_colormap ();
	gtk_widget_pop_visual ();

	gtk_object_unref(GTK_OBJECT(ete));

	return e_tree;
}

ETree *
e_tree_construct (ETree *e_tree, ETreeModel *etm, ETableExtras *ete,
		   const char *spec_str, const char *state_str)
{
	ETableSpecification *specification;
	ETableState *state;

	g_return_val_if_fail(e_tree != NULL, NULL);
	g_return_val_if_fail(E_IS_TREE(e_tree), NULL);
	g_return_val_if_fail(etm != NULL, NULL);
	g_return_val_if_fail(E_IS_TREE_MODEL(etm), NULL);
	g_return_val_if_fail(ete == NULL || E_IS_TABLE_EXTRAS(ete), NULL);
	g_return_val_if_fail(spec_str != NULL, NULL);

	specification = e_table_specification_new();
	e_table_specification_load_from_string(specification, spec_str);
	if (state_str) {
		state = e_table_state_new();
		e_table_state_load_from_string(state, state_str);
		if (state->col_count <= 0) {
			gtk_object_unref(GTK_OBJECT(state));
			state = specification->state;
			gtk_object_ref(GTK_OBJECT(state));
		}
	} else {
		state = specification->state;
		gtk_object_ref(GTK_OBJECT(state));
	}

	e_tree = et_real_construct (e_tree, etm, ete, specification, state);

	e_tree->spec = specification;
	gtk_object_unref(GTK_OBJECT(state));

	return e_tree;
}

ETree *
e_tree_construct_from_spec_file (ETree *e_tree, ETreeModel *etm, ETableExtras *ete,
				  const char *spec_fn, const char *state_fn)
{
	ETableSpecification *specification;
	ETableState *state;

	g_return_val_if_fail(e_tree != NULL, NULL);
	g_return_val_if_fail(E_IS_TREE(e_tree), NULL);
	g_return_val_if_fail(etm != NULL, NULL);
	g_return_val_if_fail(E_IS_TREE_MODEL(etm), NULL);
	g_return_val_if_fail(ete == NULL || E_IS_TABLE_EXTRAS(ete), NULL);
	g_return_val_if_fail(spec_fn != NULL, NULL);

	specification = e_table_specification_new();
	if (!e_table_specification_load_from_file(specification, spec_fn)) {
		gtk_object_unref(GTK_OBJECT(specification));
		return NULL;
	}

	if (state_fn) {
		state = e_table_state_new();
		if (!e_table_state_load_from_file(state, state_fn)) {
			gtk_object_unref(GTK_OBJECT(state));
			state = specification->state;
			gtk_object_ref(GTK_OBJECT(state));
		}
		if (state->col_count <= 0) {
			gtk_object_unref(GTK_OBJECT(state));
			state = specification->state;
			gtk_object_ref(GTK_OBJECT(state));
		}
	} else {
		state = specification->state;
		gtk_object_ref(GTK_OBJECT(state));
	}

	e_tree = et_real_construct (e_tree, etm, ete, specification, state);

	e_tree->spec = specification;
	gtk_object_unref(GTK_OBJECT(state));

	return e_tree;
}

GtkWidget *
e_tree_new (ETreeModel *etm, ETableExtras *ete, const char *spec, const char *state)
{
	ETree *e_tree;

	g_return_val_if_fail(etm != NULL, NULL);
	g_return_val_if_fail(E_IS_TREE_MODEL(etm), NULL);
	g_return_val_if_fail(ete == NULL || E_IS_TABLE_EXTRAS(ete), NULL);
	g_return_val_if_fail(spec != NULL, NULL);

	e_tree = gtk_type_new (e_tree_get_type ());

	e_tree = e_tree_construct (e_tree, etm, ete, spec, state);

	return GTK_WIDGET (e_tree);
}

GtkWidget *
e_tree_new_from_spec_file (ETreeModel *etm, ETableExtras *ete, const char *spec_fn, const char *state_fn)
{
	ETree *e_tree;

	g_return_val_if_fail(etm != NULL, NULL);
	g_return_val_if_fail(E_IS_TREE_MODEL(etm), NULL);
	g_return_val_if_fail(ete == NULL || E_IS_TABLE_EXTRAS(ete), NULL);
	g_return_val_if_fail(spec_fn != NULL, NULL);

	e_tree = gtk_type_new (e_tree_get_type ());

	e_tree = e_tree_construct_from_spec_file (e_tree, etm, ete, spec_fn, state_fn);

	return GTK_WIDGET (e_tree);
}

void
e_tree_set_cursor (ETree *e_tree, ETreePath path)
{
	int row;
	g_return_if_fail(e_tree != NULL);
	g_return_if_fail(E_IS_TREE(e_tree));
	g_return_if_fail(path != NULL);

	path = e_tree_sorted_model_to_view_path(e_tree->sorted, path);

	row = e_tree_table_adapter_row_of_node(E_TREE_TABLE_ADAPTER(e_tree->etta), path);

	if (row == -1)
		return;

	gtk_object_set(GTK_OBJECT(e_tree->selection),
		       "cursor_row", row,
		       NULL);
}

ETreePath
e_tree_get_cursor (ETree *e_tree)
{
	int row;
	ETreePath path;
	g_return_val_if_fail(e_tree != NULL, NULL);
	g_return_val_if_fail(E_IS_TREE(e_tree), NULL);

	gtk_object_get(GTK_OBJECT(e_tree->selection),
		       "cursor_row", &row,
		       NULL);
	path = e_tree_table_adapter_node_at_row(E_TREE_TABLE_ADAPTER(e_tree->etta), row);
	path = e_tree_sorted_view_to_model_path(e_tree->sorted, path);
	return path;
}

void
e_tree_selected_row_foreach     (ETree *e_tree,
				  EForeachFunc callback,
				  gpointer closure)
{
	g_return_if_fail(e_tree != NULL);
	g_return_if_fail(E_IS_TREE(e_tree));

	e_selection_model_foreach(E_SELECTION_MODEL (e_tree->selection),
						     callback,
						     closure);
}

gint
e_tree_selected_count     (ETree *e_tree)
{
	g_return_val_if_fail(e_tree != NULL, -1);
	g_return_val_if_fail(E_IS_TREE(e_tree), -1);

	return e_selection_model_selected_count(E_SELECTION_MODEL (e_tree->selection));
}

void
e_tree_select_all (ETree *tree)
{
	g_return_if_fail (tree != NULL);
	g_return_if_fail (E_IS_TREE (tree));

	e_selection_model_select_all (E_SELECTION_MODEL (tree->selection));
}

void
e_tree_invert_selection (ETree *tree)
{
	g_return_if_fail (tree != NULL);
	g_return_if_fail (E_IS_TREE (tree));

	e_selection_model_invert_selection (E_SELECTION_MODEL (tree->selection));
}


EPrintable *
e_tree_get_printable (ETree *e_tree)
{
	g_return_val_if_fail(e_tree != NULL, NULL);
	g_return_val_if_fail(E_IS_TREE(e_tree), NULL);

	return e_table_item_get_printable(E_TABLE_ITEM(e_tree->item));
}

static void
et_get_arg (GtkObject *o, GtkArg *arg, guint arg_id)
{
	switch (arg_id){
	default:
		break;
	}
}

typedef struct {
	char     *arg;
	gboolean  setting;
} bool_closure;

static void
et_set_arg (GtkObject *o, GtkArg *arg, guint arg_id)
{
	ETree *etree = E_TREE (o);

	switch (arg_id){
	case ARG_LENGTH_THRESHOLD:
		etree->length_threshold = GTK_VALUE_INT (*arg);
		if (etree->item) {
			gnome_canvas_item_set (GNOME_CANVAS_ITEM(etree->item),
					       "length_threshold", GTK_VALUE_INT (*arg),
					       NULL);
		}
		break;

	}
}

static void
set_scroll_adjustments   (ETree *tree,
			  GtkAdjustment *hadjustment,
			  GtkAdjustment *vadjustment)
{
	if (vadjustment != NULL) {
		vadjustment->step_increment = 20;
		gtk_adjustment_changed(vadjustment);
	}

	gtk_layout_set_hadjustment (GTK_LAYOUT(tree->table_canvas),
				    hadjustment);
	gtk_layout_set_vadjustment (GTK_LAYOUT(tree->table_canvas),
				    vadjustment);

	if (tree->header_canvas != NULL)
		gtk_layout_set_hadjustment (GTK_LAYOUT(tree->header_canvas),
					    hadjustment);
}

gint
e_tree_get_next_row      (ETree *e_tree,
			   gint    model_row)
{
	g_return_val_if_fail(e_tree != NULL, -1);
	g_return_val_if_fail(E_IS_TREE(e_tree), -1);

	if (e_tree->sorter) {
		int i;
		i = e_sorter_model_to_sorted(E_SORTER (e_tree->sorter), model_row);
		i++;
		if (i < e_table_model_row_count(E_TABLE_MODEL(e_tree->etta))) {
			return e_sorter_sorted_to_model(E_SORTER (e_tree->sorter), i);
		} else
			return -1;
	} else
		if (model_row < e_table_model_row_count(E_TABLE_MODEL(e_tree->etta)) - 1)
			return model_row + 1;
		else
			return -1;
}

gint
e_tree_get_prev_row      (ETree *e_tree,
			  gint    model_row)
{
	g_return_val_if_fail(e_tree != NULL, -1);
	g_return_val_if_fail(E_IS_TREE(e_tree), -1);

	if (e_tree->sorter) {
		int i;
		i = e_sorter_model_to_sorted(E_SORTER (e_tree->sorter), model_row);
		i--;
		if (i >= 0)
			return e_sorter_sorted_to_model(E_SORTER (e_tree->sorter), i);
		else
			return -1;
	} else
		return model_row - 1;
}

gint
e_tree_model_to_view_row        (ETree *e_tree,
				  gint    model_row)
{
	g_return_val_if_fail(e_tree != NULL, -1);
	g_return_val_if_fail(E_IS_TREE(e_tree), -1);

	if (e_tree->sorter)
		return e_sorter_model_to_sorted(E_SORTER (e_tree->sorter), model_row);
	else
		return model_row;
}

gint
e_tree_view_to_model_row        (ETree *e_tree,
				  gint    view_row)
{
	g_return_val_if_fail(e_tree != NULL, -1);
	g_return_val_if_fail(E_IS_TREE(e_tree), -1);

	if (e_tree->sorter)
		return e_sorter_sorted_to_model (E_SORTER (e_tree->sorter), view_row);
	else
		return view_row;
}


gboolean
e_tree_node_is_expanded (ETree *et, ETreePath path)
{
	path = e_tree_sorted_model_to_view_path(et->sorted, path);

	return e_tree_table_adapter_node_is_expanded (et->etta, path);
}

void
e_tree_node_set_expanded (ETree *et, ETreePath path, gboolean expanded)
{
	g_return_if_fail (et != NULL);
	g_return_if_fail (E_IS_TREE(et));

	path = e_tree_sorted_model_to_view_path(et->sorted, path);

	e_tree_table_adapter_node_set_expanded (et->etta, path, expanded);
}

void
e_tree_node_set_expanded_recurse (ETree *et, ETreePath path, gboolean expanded)
{
	g_return_if_fail (et != NULL);
	g_return_if_fail (E_IS_TREE(et));

	path = e_tree_sorted_model_to_view_path(et->sorted, path);

	e_tree_table_adapter_node_set_expanded_recurse (et->etta, path, expanded);
}

void
e_tree_root_node_set_visible (ETree *et, gboolean visible)
{
	g_return_if_fail (et != NULL);
	g_return_if_fail (E_IS_TREE(et));

	e_tree_table_adapter_root_node_set_visible (et->etta, visible);
}

ETreePath
e_tree_node_at_row (ETree *et, int row)
{
	ETreePath path;

	path = e_tree_table_adapter_node_at_row (et->etta, row);
	path = e_tree_sorted_view_to_model_path(et->sorted, path);

	return path;
}

int
e_tree_row_of_node (ETree *et, ETreePath path)
{
	path = e_tree_sorted_model_to_view_path(et->sorted, path);
	return e_tree_table_adapter_row_of_node (et->etta, path);
}

gboolean
e_tree_root_node_is_visible(ETree *et)
{
	return e_tree_table_adapter_root_node_is_visible (et->etta);
}

void
e_tree_show_node (ETree *et, ETreePath path)
{
	g_return_if_fail (et != NULL);
	g_return_if_fail (E_IS_TREE(et));

	path = e_tree_sorted_model_to_view_path(et->sorted, path);

	e_tree_table_adapter_show_node (et->etta, path);
}

void
e_tree_save_expanded_state (ETree *et, char *filename)
{
	g_return_if_fail (et != NULL);
	g_return_if_fail (E_IS_TREE(et));

	e_tree_table_adapter_save_expanded_state (et->etta, filename);
}

void
e_tree_load_expanded_state (ETree *et, char *filename)
{
	e_tree_table_adapter_load_expanded_state (et->etta, filename);
}

gint
e_tree_row_count (ETree *et)
{
	return e_table_model_row_count (E_TABLE_MODEL(et->etta));
}

struct _ETreeDragSourceSite
{
	GdkModifierType    start_button_mask;
	GtkTargetList     *target_list;        /* Targets for drag data */
	GdkDragAction      actions;            /* Possible actions */
	GdkColormap       *colormap;	         /* Colormap for drag icon */
	GdkPixmap         *pixmap;             /* Icon for drag data */
	GdkBitmap         *mask;

	/* Stored button press information to detect drag beginning */
	gint               state;
	gint               x, y;
	gint               row, col;
};

typedef enum
{
  GTK_DRAG_STATUS_DRAG,
  GTK_DRAG_STATUS_WAIT,
  GTK_DRAG_STATUS_DROP
} GtkDragStatus;

typedef struct _GtkDragDestInfo GtkDragDestInfo;
typedef struct _GtkDragSourceInfo GtkDragSourceInfo;

struct _GtkDragDestInfo
{
  GtkWidget         *widget;	   /* Widget in which drag is in */
  GdkDragContext    *context;	   /* Drag context */
  GtkDragSourceInfo *proxy_source; /* Set if this is a proxy drag */
  GtkSelectionData  *proxy_data;   /* Set while retrieving proxied data */
  gboolean           dropped : 1;     /* Set after we receive a drop */
  guint32            proxy_drop_time; /* Timestamp for proxied drop */
  gboolean           proxy_drop_wait : 1; /* Set if we are waiting for a
					   * status reply before sending
					   * a proxied drop on.
					   */
  gint               drop_x, drop_y; /* Position of drop */
};

struct _GtkDragSourceInfo
{
  GtkWidget         *widget;
  GtkTargetList     *target_list; /* Targets for drag data */
  GdkDragAction      possible_actions; /* Actions allowed by source */
  GdkDragContext    *context;	  /* drag context */
  GtkWidget         *icon_window; /* Window for drag */
  GtkWidget         *ipc_widget;  /* GtkInvisible for grab, message passing */
  GdkCursor         *cursor;	  /* Cursor for drag */
  gint hot_x, hot_y;		  /* Hot spot for drag */
  gint button;			  /* mouse button starting drag */

  GtkDragStatus      status;	  /* drag status */
  GdkEvent          *last_event;  /* motion event waiting for response */

  gint               start_x, start_y; /* Initial position */
  gint               cur_x, cur_y;     /* Current Position */

  GList             *selections;  /* selections we've claimed */

  GtkDragDestInfo   *proxy_dest;  /* Set if this is a proxy drag */

  guint              drop_timeout;     /* Timeout for aborting drop */
  guint              destroy_icon : 1; /* If true, destroy icon_window
					*/
};

/* Drag & drop stuff. */
/* Target */
void
e_tree_drag_get_data (ETree         *tree,
		      int             row,
		      int             col,
		      GdkDragContext *context,
		      GdkAtom         target,
		      guint32         time)
{
	ETreePath path;
	g_return_if_fail(tree != NULL);
	g_return_if_fail(E_IS_TREE(tree));

	path = e_tree_table_adapter_node_at_row(tree->etta, row);
	path = e_tree_sorted_view_to_model_path(tree->sorted, path);

	gtk_drag_get_data(GTK_WIDGET(tree),
			  context,
			  target,
			  time);

}

/**
 * e_tree_drag_highlight:
 * @tree:
 * @row:
 * @col:
 *
 * Set col to -1 to highlight the entire row.
 */
void
e_tree_drag_highlight (ETree *tree,
			int     row,
			int     col)
{
	g_return_if_fail(tree != NULL);
	g_return_if_fail(E_IS_TREE(tree));
}

void
e_tree_drag_unhighlight (ETree *tree)
{
	g_return_if_fail(tree != NULL);
	g_return_if_fail(E_IS_TREE(tree));
}

void e_tree_drag_dest_set   (ETree               *tree,
			     GtkDestDefaults       flags,
			     const GtkTargetEntry *targets,
			     gint                  n_targets,
			     GdkDragAction         actions)
{
	g_return_if_fail(tree != NULL);
	g_return_if_fail(E_IS_TREE(tree));

	gtk_drag_dest_set(GTK_WIDGET(tree),
			  flags,
			  targets,
			  n_targets,
			  actions);
}

void e_tree_drag_dest_set_proxy (ETree         *tree,
				 GdkWindow      *proxy_window,
				 GdkDragProtocol protocol,
				 gboolean        use_coordinates)
{
	g_return_if_fail(tree != NULL);
	g_return_if_fail(E_IS_TREE(tree));

	gtk_drag_dest_set_proxy(GTK_WIDGET(tree),
				proxy_window,
				protocol,
				use_coordinates);
}

/*
 * There probably should be functions for setting the targets
 * as a GtkTargetList
 */

void
e_tree_drag_dest_unset (GtkWidget *widget)
{
	g_return_if_fail(widget != NULL);
	g_return_if_fail(E_IS_TREE(widget));

	gtk_drag_dest_unset(widget);
}

/* Source side */

void
e_tree_drag_source_set  (ETree               *tree,
			 GdkModifierType       start_button_mask,
			 const GtkTargetEntry *targets,
			 gint                  n_targets,
			 GdkDragAction         actions)
{
	ETreeDragSourceSite *site;
	GtkWidget *canvas;

	g_return_if_fail(tree != NULL);
	g_return_if_fail(E_IS_TREE(tree));

	canvas = GTK_WIDGET(tree->table_canvas);
	site = tree->site;

	gtk_widget_add_events (canvas,
			       gtk_widget_get_events (canvas) |
			       GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
			       GDK_BUTTON_MOTION_MASK | GDK_STRUCTURE_MASK);

	if (site) {
		if (site->target_list)
			gtk_target_list_unref (site->target_list);
	} else {
		site = g_new0 (ETreeDragSourceSite, 1);

		tree->drag_source_button_press_event_id =
			gtk_signal_connect (GTK_OBJECT (canvas), "button_press_event",
					    GTK_SIGNAL_FUNC (e_tree_drag_source_event_cb),
					    tree);
		tree->drag_source_motion_notify_event_id =
			gtk_signal_connect (GTK_OBJECT (canvas), "motion_notify_event",
					    GTK_SIGNAL_FUNC (e_tree_drag_source_event_cb),
					    tree);

		tree->site = site;
	}

	site->start_button_mask = start_button_mask;

	if (targets)
		site->target_list = gtk_target_list_new (targets, n_targets);
	else
		site->target_list = NULL;

	site->actions = actions;
}

void
e_tree_drag_source_unset (ETree *tree)
{
	ETreeDragSourceSite *site;

	g_return_if_fail (tree != NULL);
	g_return_if_fail (E_IS_TREE(tree));

	site = tree->site;

	if (site) {
		gtk_signal_disconnect (
			GTK_OBJECT (tree->table_canvas),
			tree->drag_source_button_press_event_id);
		gtk_signal_disconnect (
			GTK_OBJECT (tree->table_canvas),
			tree->drag_source_motion_notify_event_id);
		g_free (site);
		tree->site = NULL;
	}
}

/* There probably should be functions for setting the targets
 * as a GtkTargetList
 */

GdkDragContext *
e_tree_drag_begin (ETree            *tree,
		   int     	       row,
		   int     	       col,
		   GtkTargetList     *targets,
		   GdkDragAction      actions,
		   gint               button,
		   GdkEvent          *event)
{
	ETreePath path;
	g_return_val_if_fail (tree != NULL, NULL);
	g_return_val_if_fail (E_IS_TREE(tree), NULL);

	path = e_tree_table_adapter_node_at_row(tree->etta, row);
	path = e_tree_sorted_view_to_model_path(tree->sorted, path);

	tree->drag_row = row;
	tree->drag_path = path;
	tree->drag_col = col;

	return gtk_drag_begin(GTK_WIDGET(tree),
			      targets,
			      actions,
			      button,
			      event);
}

/**
 * e_tree_get_cell_at:
 * @tree: An ETree widget
 * @x: X coordinate for the pixel
 * @y: Y coordinate for the pixel
 * @row_return: Pointer to return the row value
 * @col_return: Pointer to return the column value
 * 
 * Return the row and column for the cell in which the pixel at (@x, @y) is
 * contained.
 **/
void
e_tree_get_cell_at (ETree *tree,
		     int x, int y,
		     int *row_return, int *col_return)
{
	g_return_if_fail (tree != NULL);
	g_return_if_fail (E_IS_TREE (tree));
	g_return_if_fail (row_return != NULL);
	g_return_if_fail (col_return != NULL);

	/* FIXME it would be nice if it could handle a NULL row_return or
	 * col_return gracefully.  */

	x += GTK_LAYOUT(tree->table_canvas)->hadjustment->value;
	y += GTK_LAYOUT(tree->table_canvas)->vadjustment->value;
	e_table_item_compute_location(E_TABLE_ITEM(tree->item), &x, &y, row_return, col_return);
}

static void
et_drag_begin (GtkWidget *widget,
	       GdkDragContext *context,
	       ETree *et)
{
	gtk_signal_emit (GTK_OBJECT (et),
			 et_signals [TREE_DRAG_BEGIN],
			 et->drag_row,
			 et->drag_path,
			 et->drag_col,
			 context);
}

static void
et_drag_end (GtkWidget *widget,
	     GdkDragContext *context,
	     ETree *et)
{
	gtk_signal_emit (GTK_OBJECT (et),
			 et_signals [TREE_DRAG_END],
			 et->drag_row,
			 et->drag_path,
			 et->drag_col,
			 context);
}

static void
et_drag_data_get(GtkWidget *widget,
		 GdkDragContext *context,
		 GtkSelectionData *selection_data,
		 guint info,
		 guint time,
		 ETree *et)
{
	gtk_signal_emit (GTK_OBJECT (et),
			 et_signals [TREE_DRAG_DATA_GET],
			 et->drag_row,
			 et->drag_path,
			 et->drag_col,
			 context,
			 selection_data,
			 info,
			 time);
}

static void
et_drag_data_delete(GtkWidget *widget,
		    GdkDragContext *context,
		    ETree *et)
{
	gtk_signal_emit (GTK_OBJECT (et),
			 et_signals [TREE_DRAG_DATA_DELETE],
			 et->drag_row,
			 et->drag_path,
			 et->drag_col,
			 context);
}

static void
et_drag_leave(GtkWidget *widget,
	      GdkDragContext *context,
	      guint time,
	      ETree *et)
{
	gtk_signal_emit (GTK_OBJECT (et),
			 et_signals [TREE_DRAG_LEAVE],
			 et->drop_row,
			 et->drop_path,
			 et->drop_col,
			 context,
			 time);
	et->drop_row = -1;
	et->drop_col = -1;
}

static gboolean
et_drag_motion(GtkWidget *widget,
	       GdkDragContext *context,
	       gint x,
	       gint y,
	       guint time,
	       ETree *et)
{
	gboolean ret_val;
	int row, col;
	ETreePath path;
	e_tree_get_cell_at (et,
			    x,
			    y,
			    &row,
			    &col);
	if (et->drop_row >= 0 && et->drop_col >= 0 &&
	    row != et->drop_row && col != et->drop_row) {
		gtk_signal_emit (GTK_OBJECT (et),
				 et_signals [TREE_DRAG_LEAVE],
				 et->drop_row,
				 et->drop_path,
				 et->drop_col,
				 context,
				 time);
	}

	path = e_tree_table_adapter_node_at_row(et->etta, row);
	path = e_tree_sorted_view_to_model_path(et->sorted, path);

	et->drop_row = row;
	et->drop_path = path;
	et->drop_col = col;
	if (row >= 0 && col >= 0)
		gtk_signal_emit (GTK_OBJECT (et),
				 et_signals [TREE_DRAG_MOTION],
				 et->drop_row,
				 et->drop_path,
				 et->drop_col,
				 context,
				 x,
				 y,
				 time,
				 &ret_val);
	return ret_val;
}

static gboolean
et_drag_drop(GtkWidget *widget,
	     GdkDragContext *context,
	     gint x,
	     gint y,
	     guint time,
	     ETree *et)
{
	gboolean ret_val;
	int row, col;
	ETreePath path;
	e_tree_get_cell_at(et,
			   x,
			   y,
			   &row,
			   &col);
	path = e_tree_table_adapter_node_at_row(et->etta, row);
	path = e_tree_sorted_view_to_model_path(et->sorted, path);

	if (et->drop_row >= 0 && et->drop_col >= 0 &&
	    row != et->drop_row && col != et->drop_row) {
		gtk_signal_emit (GTK_OBJECT (et),
				 et_signals [TREE_DRAG_LEAVE],
				 et->drop_row,
				 et->drop_path,
				 et->drop_col,
				 context,
				 time);
		if (row >= 0 && col >= 0)
			gtk_signal_emit (GTK_OBJECT (et),
					 et_signals [TREE_DRAG_MOTION],
					 row,
					 path,
					 col,
					 context,
					 x,
					 y,
					 time,
					 &ret_val);
	}
	et->drop_row = row;
	et->drop_path = path;
	et->drop_col = col;
	if (row >= 0 && col >= 0)
		gtk_signal_emit (GTK_OBJECT (et),
				 et_signals [TREE_DRAG_DROP],
				 et->drop_row,
				 et->drop_path,
				 et->drop_col,
				 context,
				 x,
				 y,
				 time,
				 &ret_val);
	et->drop_row = -1;
	et->drop_path = NULL;
	et->drop_col = -1;
	return ret_val;
}

static void
et_drag_data_received(GtkWidget *widget,
		      GdkDragContext *context,
		      gint x,
		      gint y,
		      GtkSelectionData *selection_data,
		      guint info,
		      guint time,
		      ETree *et)
{
	int row, col;
	ETreePath path;
	e_tree_get_cell_at(et,
			   x,
			   y,
			   &row,
			   &col);
	path = e_tree_table_adapter_node_at_row(et->etta, row);
	path = e_tree_sorted_view_to_model_path(et->sorted, path);
	gtk_signal_emit (GTK_OBJECT (et),
			 et_signals [TREE_DRAG_DATA_RECEIVED],
			 row,
			 path,
			 col,
			 context,
			 x,
			 y,
			 selection_data,
			 info,
			 time);
}

static gint
e_tree_drag_source_event_cb (GtkWidget      *widget,
			      GdkEvent       *event,
			      ETree         *tree)
{
	ETreeDragSourceSite *site;
	site = tree->site;

	switch (event->type) {
	case GDK_BUTTON_PRESS:
		if ((GDK_BUTTON1_MASK << (event->button.button - 1)) & site->start_button_mask) {
			int row, col;
			e_tree_get_cell_at(tree, event->button.x, event->button.y, &row, &col);
			if (row >= 0 && col >= 0) {
				site->state |= (GDK_BUTTON1_MASK << (event->button.button - 1));
				site->x = event->button.x;
				site->y = event->button.y;
				site->row = row;
				site->col = col;
			}
		}
		break;

	case GDK_BUTTON_RELEASE:
		if ((GDK_BUTTON1_MASK << (event->button.button - 1)) & site->start_button_mask) {
			site->state &= ~(GDK_BUTTON1_MASK << (event->button.button - 1));
		}
		break;

	case GDK_MOTION_NOTIFY:
		if (site->state & event->motion.state & site->start_button_mask) {
			/* FIXME: This is really broken and can leave us
			 * with a stuck grab
			 */
			int i;
			for (i=1; i<6; i++) {
				if (site->state & event->motion.state &
				    GDK_BUTTON1_MASK << (i - 1))
					break;
			}

			if (MAX (abs (site->x - event->motion.x),
				 abs (site->y - event->motion.y)) > 3) {
				GtkDragSourceInfo *info;
				GdkDragContext *context;

				site->state = 0;
				context = e_tree_drag_begin (tree, site->row, site->col,
							     site->target_list,
							     site->actions,
							     i, event);


				info = g_dataset_get_data (context, "gtk-info");

				if (!info->icon_window) {
					if (site->pixmap)
						gtk_drag_set_icon_pixmap (context,
									  site->colormap,
									  site->pixmap,
									  site->mask, -2, -2);
					else
						gtk_drag_set_icon_default (context);
				}

				return TRUE;
			}
		}
		break;

	default:			/* hit for 2/3BUTTON_PRESS */
		break;
	}
	return FALSE;
}

static void
e_tree_class_init (ETreeClass *class)
{
	GtkObjectClass *object_class;
	GtkWidgetClass *widget_class;
	GtkContainerClass *container_class;

	object_class = (GtkObjectClass *) class;
	widget_class = (GtkWidgetClass *) class;
	container_class = (GtkContainerClass *) class;

	parent_class = gtk_type_class (PARENT_TYPE);

	object_class->destroy           = et_destroy;
	object_class->set_arg           = et_set_arg;
	object_class->get_arg           = et_get_arg;

	widget_class->grab_focus = et_grab_focus;

	container_class->focus = et_focus;

	class->cursor_change            = NULL;
	class->cursor_activated            = NULL;
	class->selection_change         = NULL;
	class->double_click             = NULL;
	class->right_click              = NULL;
	class->click                    = NULL;
	class->key_press                = NULL;

	class->tree_drag_begin         = NULL;
	class->tree_drag_end           = NULL;
	class->tree_drag_data_get      = NULL;
	class->tree_drag_data_delete   = NULL;

	class->tree_drag_leave         = NULL;
	class->tree_drag_motion        = NULL;
	class->tree_drag_drop          = NULL;
	class->tree_drag_data_received = NULL;

	et_signals [CURSOR_CHANGE] =
		gtk_signal_new ("cursor_change",
				GTK_RUN_LAST,
				object_class->type,
				GTK_SIGNAL_OFFSET (ETreeClass, cursor_change),
				gtk_marshal_NONE__POINTER_INT,
				GTK_TYPE_NONE, 2, GTK_TYPE_POINTER, GTK_TYPE_INT);

	et_signals [CURSOR_ACTIVATED] =
		gtk_signal_new ("cursor_activated",
				GTK_RUN_LAST,
				object_class->type,
				GTK_SIGNAL_OFFSET (ETreeClass, cursor_activated),
				gtk_marshal_NONE__POINTER_INT,
				GTK_TYPE_NONE, 2, GTK_TYPE_POINTER, GTK_TYPE_INT);

	et_signals [SELECTION_CHANGE] =
		gtk_signal_new ("selection_change",
				GTK_RUN_LAST,
				object_class->type,
				GTK_SIGNAL_OFFSET (ETreeClass, selection_change),
				gtk_marshal_NONE__NONE,
				GTK_TYPE_NONE, 0);

	et_signals [DOUBLE_CLICK] =
		gtk_signal_new ("double_click",
				GTK_RUN_LAST,
				object_class->type,
				GTK_SIGNAL_OFFSET (ETreeClass, double_click),
				e_marshal_NONE__INT_POINTER_INT_POINTER,
				GTK_TYPE_NONE, 4, GTK_TYPE_INT, GTK_TYPE_POINTER, GTK_TYPE_INT, GTK_TYPE_POINTER);

	et_signals [RIGHT_CLICK] =
		gtk_signal_new ("right_click",
				GTK_RUN_LAST,
				object_class->type,
				GTK_SIGNAL_OFFSET (ETreeClass, right_click),
				e_marshal_INT__INT_POINTER_INT_POINTER,
				GTK_TYPE_INT, 4, GTK_TYPE_INT, GTK_TYPE_POINTER, GTK_TYPE_INT, GTK_TYPE_POINTER);

	et_signals [CLICK] =
		gtk_signal_new ("click",
				GTK_RUN_LAST,
				object_class->type,
				GTK_SIGNAL_OFFSET (ETreeClass, click),
				e_marshal_INT__INT_POINTER_INT_POINTER,
				GTK_TYPE_INT, 4, GTK_TYPE_INT, GTK_TYPE_POINTER, GTK_TYPE_INT, GTK_TYPE_POINTER);

	et_signals [KEY_PRESS] =
		gtk_signal_new ("key_press",
				GTK_RUN_LAST,
				object_class->type,
				GTK_SIGNAL_OFFSET (ETreeClass, key_press),
				e_marshal_INT__INT_POINTER_INT_POINTER,
				GTK_TYPE_INT, 4, GTK_TYPE_INT, GTK_TYPE_POINTER, GTK_TYPE_INT, GTK_TYPE_POINTER);

	et_signals[TREE_DRAG_BEGIN] =
		gtk_signal_new ("tree_drag_begin",
				GTK_RUN_LAST,
				object_class->type,
				GTK_SIGNAL_OFFSET (ETreeClass, tree_drag_begin),
				e_marshal_NONE__INT_POINTER_INT_POINTER,
				GTK_TYPE_NONE, 4,
				GTK_TYPE_INT,
				GTK_TYPE_POINTER,
				GTK_TYPE_INT,
				GTK_TYPE_GDK_DRAG_CONTEXT);
	et_signals[TREE_DRAG_END] =
		gtk_signal_new ("tree_drag_end",
				GTK_RUN_LAST,
				object_class->type,
				GTK_SIGNAL_OFFSET (ETreeClass, tree_drag_end),
				e_marshal_NONE__INT_POINTER_INT_POINTER,
				GTK_TYPE_NONE, 4,
				GTK_TYPE_INT,
				GTK_TYPE_POINTER,
				GTK_TYPE_INT,
				GTK_TYPE_GDK_DRAG_CONTEXT);
	et_signals[TREE_DRAG_DATA_GET] =
		gtk_signal_new ("tree_drag_data_get",
				GTK_RUN_LAST,
				object_class->type,
				GTK_SIGNAL_OFFSET (ETreeClass, tree_drag_data_get),
				e_marshal_NONE__INT_POINTER_INT_POINTER_POINTER_UINT_UINT,
				GTK_TYPE_NONE, 7,
				GTK_TYPE_INT,
				GTK_TYPE_POINTER,
				GTK_TYPE_INT,
				GTK_TYPE_GDK_DRAG_CONTEXT,
				GTK_TYPE_SELECTION_DATA,
				GTK_TYPE_UINT,
				GTK_TYPE_UINT);
	et_signals[TREE_DRAG_DATA_DELETE] =
		gtk_signal_new ("tree_drag_data_delete",
				GTK_RUN_LAST,
				object_class->type,
				GTK_SIGNAL_OFFSET (ETreeClass, tree_drag_data_delete),
				e_marshal_NONE__INT_POINTER_INT_POINTER,
				GTK_TYPE_NONE, 4,
				GTK_TYPE_INT,
				GTK_TYPE_POINTER,
				GTK_TYPE_INT,
				GTK_TYPE_GDK_DRAG_CONTEXT);

	et_signals[TREE_DRAG_LEAVE] =
		gtk_signal_new ("tree_drag_leave",
				GTK_RUN_LAST,
				object_class->type,
				GTK_SIGNAL_OFFSET (ETreeClass, tree_drag_leave),
				e_marshal_NONE__INT_POINTER_INT_POINTER_UINT,
				GTK_TYPE_NONE, 5,
				GTK_TYPE_INT,
				GTK_TYPE_POINTER,
				GTK_TYPE_INT,
				GTK_TYPE_GDK_DRAG_CONTEXT,
				GTK_TYPE_UINT);
	et_signals[TREE_DRAG_MOTION] =
		gtk_signal_new ("tree_drag_motion",
				GTK_RUN_LAST,
				object_class->type,
				GTK_SIGNAL_OFFSET (ETreeClass, tree_drag_motion),
				e_marshal_BOOL__INT_POINTER_INT_POINTER_INT_INT_UINT,
				GTK_TYPE_BOOL, 7,
				GTK_TYPE_INT,
				GTK_TYPE_POINTER,
				GTK_TYPE_INT,
				GTK_TYPE_GDK_DRAG_CONTEXT,
				GTK_TYPE_INT,
				GTK_TYPE_INT,
				GTK_TYPE_UINT);
	et_signals[TREE_DRAG_DROP] =
		gtk_signal_new ("tree_drag_drop",
				GTK_RUN_LAST,
				object_class->type,
				GTK_SIGNAL_OFFSET (ETreeClass, tree_drag_drop),
				e_marshal_BOOL__INT_POINTER_INT_POINTER_INT_INT_UINT,
				GTK_TYPE_BOOL, 7,
				GTK_TYPE_INT,
				GTK_TYPE_POINTER,
				GTK_TYPE_INT,
				GTK_TYPE_GDK_DRAG_CONTEXT,
				GTK_TYPE_INT,
				GTK_TYPE_INT,
				GTK_TYPE_UINT);
	et_signals[TREE_DRAG_DATA_RECEIVED] =
		gtk_signal_new ("tree_drag_data_received",
				GTK_RUN_LAST,
				object_class->type,
				GTK_SIGNAL_OFFSET (ETreeClass, tree_drag_data_received),
				e_marshal_NONE__INT_POINTER_INT_POINTER_INT_INT_POINTER_UINT_UINT,
				GTK_TYPE_NONE, 9,
				GTK_TYPE_INT,
				GTK_TYPE_POINTER,
				GTK_TYPE_INT,
				GTK_TYPE_GDK_DRAG_CONTEXT,
				GTK_TYPE_INT,
				GTK_TYPE_INT,
				GTK_TYPE_SELECTION_DATA,
				GTK_TYPE_UINT,
				GTK_TYPE_UINT);

	gtk_object_class_add_signals (object_class, et_signals, LAST_SIGNAL);

	class->set_scroll_adjustments = set_scroll_adjustments;

	widget_class->set_scroll_adjustments_signal =
		gtk_signal_new ("set_scroll_adjustments",
				GTK_RUN_LAST,
				object_class->type,
				GTK_SIGNAL_OFFSET (ETreeClass, set_scroll_adjustments),
				gtk_marshal_NONE__POINTER_POINTER,
				GTK_TYPE_NONE, 2, GTK_TYPE_ADJUSTMENT, GTK_TYPE_ADJUSTMENT);

	gtk_object_add_arg_type ("ETree::length_threshold", GTK_TYPE_INT,
				 GTK_ARG_WRITABLE, ARG_LENGTH_THRESHOLD);
}

E_MAKE_TYPE(e_tree, "ETree", ETree, e_tree_class_init, e_tree_init, PARENT_TYPE);
