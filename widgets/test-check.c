/*
 * Test code for the ETable package
 *
 * Author:
 *   Miguel de Icaza (miguel@gnu.org)
 */
#include <config.h>
#include <stdio.h>
#include <string.h>
#include <gnome.h>
#include "e-table-simple.h"
#include "e-table-header.h"
#include "e-table-header-item.h"
#include "e-table-item.h"
#include "e-cursors.h"
#include "e-cell-text.h"
#include "e-cell-checkbox.h"

#define LINES 4

static struct {
	int   value;
	char *string;
} my_table [LINES] = {
	{ 0, "Buy food" },
	{ 1, "Breathe " },
	{ 0, "Cancel gdb session with shrink" },
	{ 1, "Make screenshots" },
};
/*
 * ETableSimple callbacks
 */
static int
col_count (ETableModel *etc, void *data)
{
	return 2;
}

static const char *
col_name (ETableModel *etc, int col, void *data)
{
	g_assert (col < 2);

	if (col == 0)
		return "flag";
	else
		return "text";
}

static int
row_count (ETableModel *etc, void *data)
{
	return LINES;
}

static void *
value_at (ETableModel *etc, int col, int row, void *data)
{
	g_assert (col < 2);
	g_assert (row < LINES);

	if (col == 0)
		return GINT_TO_POINTER (my_table [row].value);
	else
		return my_table [row].string;
	
}

static void
set_value_at (ETableModel *etc, int col, int row, const void *val, void *data)
{
	g_assert (col < 2);
	g_assert (row < LINES);

	if (col == 0){
		my_table [row].value = GPOINTER_TO_INT (val);
		printf ("Value at %d,%d set to %d\n", col, row, GPOINTER_TO_INT (val));
	} else {
		my_table [row].string = g_strdup (val);
		printf ("Value at %d,%d set to %s\n", col, row, (char *) val);
	}
}

static gboolean
is_cell_editable (ETableModel *etc, int col, int row, void *data)
{
	return TRUE;
}

static void
set_canvas_size (GnomeCanvas *canvas, GtkAllocation *alloc)
{
	gnome_canvas_set_scroll_region (canvas, 0, 0, alloc->width, alloc->height);
}

void
check_test (void)
{
	GtkWidget *canvas, *window;
	ETableModel *e_table_model;
	ETableHeader *e_table_header;
	ETableCol *col_0, *col_1;
	ECell *cell_left_just, *cell_image_check;
	int i;

	gtk_widget_push_visual (gdk_rgb_get_visual ());
	gtk_widget_push_colormap (gdk_rgb_get_cmap ());
	
	e_table_model = e_table_simple_new (
		col_count, col_name, row_count, value_at,
		set_value_at, is_cell_editable, NULL);

	/*
	 * Header
	 */
	e_table_header = e_table_header_new ();

	cell_left_just = e_cell_text_new (e_table_model, NULL, GTK_JUSTIFY_LEFT);

	cell_image_check = e_cell_checkbox_new (e_table_model);
	col_0 = e_table_col_new ("", 18, 18, cell_image_check, g_int_equal, TRUE);
	e_table_header_add_column (e_table_header, col_0, 0);
	
	col_1 = e_table_col_new ("Item Name", 180, 20, cell_left_just, g_str_equal, TRUE);
	e_table_header_add_column (e_table_header, col_1, 1);

	/*
	 * GUI
	 */
	window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	canvas = gnome_canvas_new ();

	gtk_signal_connect (GTK_OBJECT (canvas), "size_allocate",
			    GTK_SIGNAL_FUNC (set_canvas_size), NULL);
	
	gtk_container_add (GTK_CONTAINER (window), canvas);
	gtk_widget_show_all (window);
	gnome_canvas_item_new (
		gnome_canvas_root (GNOME_CANVAS (canvas)),
		e_table_header_item_get_type (),
		"ETableHeader", e_table_header,
		"x",  0,
		"y",  0,
		NULL);

	gnome_canvas_item_new (
		gnome_canvas_root (GNOME_CANVAS (canvas)),
		e_table_item_get_type (),
		"ETableHeader", e_table_header,
		"ETableModel", e_table_model,
		"x",  0,
		"y",  30,
		"drawgrid", TRUE,
		"drawfocus", TRUE,
		"spreadsheet", TRUE,
		NULL);
	
}





