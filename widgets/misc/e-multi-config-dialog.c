/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-multi-config-dialog.c
 *
 * Copyright (C) 2002  Ximian, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: Ettore Perazzoli <ettore@ximian.com>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "e-multi-config-dialog.h"

#include "e-clipped-label.h"

#include <gal/e-table/e-table-scrolled.h>
#include <gal/e-table/e-table-memory-store.h>
#include <gal/e-table/e-cell-pixbuf.h>
#include <gal/e-table/e-cell-vbox.h>
#include <gal/e-table/e-cell-text.h>

#include <gdk-pixbuf/gdk-pixbuf.h>


#define PARENT_TYPE gnome_dialog_get_type ()
static GnomeDialogClass *parent_class = NULL;

#define SWITCH_PAGE_INTERVAL 250



struct _EMultiConfigDialogPrivate {
	GSList *pages;

	GtkWidget *list_e_table;
	ETableModel *list_e_table_model;

	GtkWidget *notebook;

	int num_unapplied;

	int set_page_timeout_id;
	int set_page_timeout_page;
};


/* ETable stuff.  */

static char *list_e_table_spec =
	"<ETableSpecification cursor-mode=\"line\""
	"		      selection-mode=\"browse\""
	"                     no-headers=\"true\""
        "                     alternating-row-colors=\"false\""
        "                     horizontal-resize=\"true\""
        ">"
	"  <ETableColumn model_col=\"0\""
	"	         expansion=\"1.0\""
	"                cell=\"vbox\""
 	"                minimum_width=\"32\""
	"                resizable=\"true\""
	"	         _title=\"blah\""
	"                compare=\"string\"/>"
	"  <ETableState>"
	"    <column source=\"0\"/>"
	"    <grouping>"
	"    </grouping>"
	"  </ETableState>"
	"</ETableSpecification>";


/* Button handling.  */

static void
update_buttons (EMultiConfigDialog *dialog)
{
	EMultiConfigDialogPrivate *priv;

	priv = dialog->priv;

	if (priv->num_unapplied > 0) {
		gnome_dialog_set_sensitive (GNOME_DIALOG (dialog), 0, TRUE); /* OK */
		gnome_dialog_set_sensitive (GNOME_DIALOG (dialog), 1, TRUE); /* Apply */
	} else {
		gnome_dialog_set_sensitive (GNOME_DIALOG (dialog), 0, FALSE); /* OK */
		gnome_dialog_set_sensitive (GNOME_DIALOG (dialog), 1, FALSE); /* Apply */
	}
}


/* Page handling.  */

static GtkWidget *
create_page_container (const char *description,
		       GtkWidget *widget)
{
	GtkWidget *vbox;

	vbox = gtk_vbox_new (FALSE, 3);
	gtk_container_set_border_width (GTK_CONTAINER (vbox), 3);

#if 0
	label = e_clipped_label_new (description);
	gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.0);
	gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, TRUE, 0);

	separator = gtk_hseparator_new ();
	gtk_box_pack_start (GTK_BOX (vbox), separator, FALSE, TRUE, 0);

	gtk_widget_show (label);
	gtk_widget_show (separator);
#endif

	gtk_box_pack_start (GTK_BOX (vbox), widget, TRUE, TRUE, 0);

	gtk_widget_show (widget);
	gtk_widget_show (vbox);

	return vbox;
}

/* Page callbacks.  */

static void
page_changed_callback (EConfigPage *page,
		       void *data)
{
	EMultiConfigDialog *dialog;
	EMultiConfigDialogPrivate *priv;

	dialog = E_MULTI_CONFIG_DIALOG (data);
	priv = dialog->priv;

	priv->num_unapplied ++;

	update_buttons (dialog);
}

/* Timeout for switching pages (so it's more comfortable navigating with the
   keyboard).  */

static int
set_page_timeout_callback (void *data)
{
	EMultiConfigDialog *multi_config_dialog;
	EMultiConfigDialogPrivate *priv;

	multi_config_dialog = E_MULTI_CONFIG_DIALOG (data);
	priv = multi_config_dialog->priv;

	gtk_notebook_set_page (GTK_NOTEBOOK (priv->notebook), priv->set_page_timeout_page);

	priv->set_page_timeout_id = 0;
	return FALSE;
}



/* Button handling.  */

static void
do_close (EMultiConfigDialog *dialog)
{
	gnome_dialog_close (GNOME_DIALOG (dialog));
}

static void
do_apply (EMultiConfigDialog *dialog)
{
	EMultiConfigDialogPrivate *priv;
	GSList *p;

	priv = dialog->priv;

	for (p = priv->pages; p != NULL; p = p->next) {
		EConfigPage *page_widget = p->data;

		if (! e_config_page_is_applied (page_widget)) {
			e_config_page_apply (page_widget);
			priv->num_unapplied --;
		}
	}

	g_assert (priv->num_unapplied == 0);
	update_buttons (dialog);
}

static void
do_ok (EMultiConfigDialog *dialog)
{
	do_apply (dialog);
	do_close (dialog);
}




/* ETable signals.  */

static void
table_cursor_change_callback (ETable *etable,
			      int row,
			      void *data)
{
	EMultiConfigDialog *dialog;
	EMultiConfigDialogPrivate *priv;

	dialog = E_MULTI_CONFIG_DIALOG (data);
	priv = dialog->priv;

	if (priv->set_page_timeout_id == 0)
		priv->set_page_timeout_id = g_timeout_add (SWITCH_PAGE_INTERVAL,
							   set_page_timeout_callback,
							   dialog);

	priv->set_page_timeout_page = row;
}


/* GtkObject methods.  */

static void
impl_destroy (GtkObject *object)
{
	EMultiConfigDialog *dialog;
	EMultiConfigDialogPrivate *priv;

	dialog = E_MULTI_CONFIG_DIALOG (object);
	priv = dialog->priv;

	if (priv->set_page_timeout_id != 0)
		g_source_remove (priv->set_page_timeout_id);

	if (priv->list_e_table_model != NULL)
		gtk_object_unref (GTK_OBJECT (priv->list_e_table_model));

	g_slist_free (priv->pages);

	g_free (priv);

	(* GTK_OBJECT_CLASS (parent_class)->destroy) (object);
}


/* GnomeDialog methods.  */

static void
impl_clicked (GnomeDialog *dialog,
	      int button_number)
{
	EMultiConfigDialog *multi_config_dialog;
	EMultiConfigDialogPrivate *priv;

	multi_config_dialog = E_MULTI_CONFIG_DIALOG (dialog);
	priv = multi_config_dialog->priv;

	switch (button_number) {
	case 0:			/* OK */
		do_ok (multi_config_dialog);
		break;
	case 1:			/* Apply */
		do_apply (multi_config_dialog);
		break;
	case 2:			/* Close */
		do_close (multi_config_dialog);
		break;
	default:
		g_assert_not_reached ();
	}
}


/* GTK+ ctors.  */

static void
class_init (EMultiConfigDialogClass *class)
{
	GnomeDialogClass *dialog_class;
	GtkObjectClass *object_class;

	object_class = GTK_OBJECT_CLASS (class);
	object_class->destroy = impl_destroy;

	dialog_class = GNOME_DIALOG_CLASS (class);
	dialog_class->clicked = impl_clicked;

	parent_class = gtk_type_class (PARENT_TYPE);
}

#define RGB_COLOR(color) (((color).red & 0xff00) << 8 | \
			   ((color).green & 0xff00) | \
			   ((color).blue & 0xff00) >> 8)

static void
fill_in_pixbufs (EMultiConfigDialog *dialog, int row)
{
	GdkPixbuf *original = e_table_model_value_at (dialog->priv->list_e_table_model, 1, row);
	GtkWidget *canvas;
	guint32 colors[3];
	int i;

	if (original == NULL)
		return;

	canvas = GTK_WIDGET (e_table_scrolled_get_table (E_TABLE_SCROLLED (dialog->priv->list_e_table))->table_canvas);

	colors[0] = RGB_COLOR (canvas->style->bg [GTK_STATE_SELECTED]);
	colors[1] = RGB_COLOR (canvas->style->bg [GTK_STATE_ACTIVE]);
	colors[2] = RGB_COLOR (canvas->style->base [GTK_STATE_NORMAL]);

	for (i = 0; i < 3; i++) {
		GdkPixbuf *pixbuf = gdk_pixbuf_composite_color_simple (original,
								       gdk_pixbuf_get_width (original),
								       gdk_pixbuf_get_height (original),
								       GDK_INTERP_BILINEAR,
								       255,
								       1,
								       colors[i], colors[i]);
		e_table_model_set_value_at (dialog->priv->list_e_table_model, i + 2, row, pixbuf);
		gdk_pixbuf_unref(pixbuf);
	}
}

static void
canvas_realize (GtkWidget *widget, EMultiConfigDialog *dialog)
{
	int i;
	int row_count;
	row_count = e_table_model_row_count (dialog->priv->list_e_table_model);
	for (i = 0; i < row_count; i++) {
		fill_in_pixbufs (dialog, i);
	}
}


static ETableMemoryStoreColumnInfo columns[] = {
	E_TABLE_MEMORY_STORE_STRING,
	E_TABLE_MEMORY_STORE_PIXBUF,
	E_TABLE_MEMORY_STORE_PIXBUF,
	E_TABLE_MEMORY_STORE_PIXBUF,
	E_TABLE_MEMORY_STORE_PIXBUF,
	E_TABLE_MEMORY_STORE_TERMINATOR
};

static void
init (EMultiConfigDialog *multi_config_dialog)
{
	EMultiConfigDialogPrivate *priv;
	ETableModel *list_e_table_model;
	GtkWidget *gnome_dialog_vbox;
	GtkWidget *hbox;
	GtkWidget *notebook;
	GtkWidget *list_e_table;
	ETableExtras *extras;
	ECell *pixbuf;
	ECell *text;
	ECell *vbox;

	hbox = gtk_hbox_new (FALSE, 2);
	gnome_dialog_vbox = GNOME_DIALOG (multi_config_dialog)->vbox;
	gtk_container_add (GTK_CONTAINER (gnome_dialog_vbox), hbox);

	list_e_table_model = e_table_memory_store_new (columns);

	vbox = e_cell_vbox_new ();

	pixbuf = e_cell_pixbuf_new();
	gtk_object_set (GTK_OBJECT (pixbuf),
			"focused_column", 2,
			"selected_column", 3,
			"unselected_column", 4,
			NULL);
	e_cell_vbox_append (E_CELL_VBOX (vbox), pixbuf, 1);
	gtk_object_unref (GTK_OBJECT (pixbuf));

	text = e_cell_text_new (NULL, GTK_JUSTIFY_CENTER);
	e_cell_vbox_append (E_CELL_VBOX (vbox), text, 0);
	gtk_object_unref (GTK_OBJECT (text));

	extras = e_table_extras_new ();
	e_table_extras_add_cell (extras, "vbox", vbox);

	list_e_table = e_table_scrolled_new (list_e_table_model, extras, list_e_table_spec, NULL);
	e_scroll_frame_set_policy (E_SCROLL_FRAME (list_e_table), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_signal_connect (GTK_OBJECT (e_table_scrolled_get_table (E_TABLE_SCROLLED (list_e_table))),
			    "cursor_change", GTK_SIGNAL_FUNC (table_cursor_change_callback),
			    multi_config_dialog);

	gtk_signal_connect (GTK_OBJECT (e_table_scrolled_get_table (E_TABLE_SCROLLED (list_e_table))->table_canvas),
			    "realize", GTK_SIGNAL_FUNC (canvas_realize),
			    multi_config_dialog);

	gtk_object_unref (GTK_OBJECT (extras));

	gtk_box_pack_start (GTK_BOX (hbox), list_e_table, FALSE, TRUE, 0);

	notebook = gtk_notebook_new ();
	gtk_notebook_set_show_tabs (GTK_NOTEBOOK (notebook), FALSE);
	gtk_notebook_set_show_border (GTK_NOTEBOOK (notebook), FALSE);
	gtk_box_pack_start (GTK_BOX (hbox), notebook, TRUE, TRUE, 0);

	gtk_widget_show (hbox);
	gtk_widget_show (notebook);
	gtk_widget_show (list_e_table);

	gnome_dialog_append_buttons (GNOME_DIALOG (multi_config_dialog),
				     GNOME_STOCK_BUTTON_OK,
				     GNOME_STOCK_BUTTON_APPLY,
				     GNOME_STOCK_BUTTON_CLOSE,
				     NULL);
	gnome_dialog_set_default (GNOME_DIALOG (multi_config_dialog), 0);

	gtk_window_set_policy (GTK_WINDOW (multi_config_dialog),
			       FALSE /* allow_shrink */,
			       TRUE /* allow_grow */,
			       FALSE /* auto_shrink */);

	priv = g_new (EMultiConfigDialogPrivate, 1);
	priv->pages                 = NULL;
	priv->list_e_table          = list_e_table;
	priv->list_e_table_model    = list_e_table_model;
	priv->notebook              = notebook;
	priv->num_unapplied         = 0;
	priv->set_page_timeout_id   = 0;
	priv->set_page_timeout_page = 0;

	multi_config_dialog->priv = priv;
}


GtkWidget *
e_multi_config_dialog_new (void)
{
	EMultiConfigDialog *dialog;

	dialog = gtk_type_new (e_multi_config_dialog_get_type ());

	return GTK_WIDGET (dialog);
}


void
e_multi_config_dialog_add_page (EMultiConfigDialog *dialog,
				const char *title,
				const char *description,
				GdkPixbuf *icon,
				EConfigPage *page_widget)
{
	EMultiConfigDialogPrivate *priv;

	g_return_if_fail (E_IS_MULTI_CONFIG_DIALOG (dialog));
	g_return_if_fail (title != NULL);
	g_return_if_fail (description != NULL);
	g_return_if_fail (E_IS_CONFIG_PAGE (page_widget));

	priv = dialog->priv;

	priv->pages = g_slist_append (priv->pages, page_widget);

	e_table_memory_store_insert (E_TABLE_MEMORY_STORE (priv->list_e_table_model), -1, NULL, title, icon, NULL, NULL, NULL);

	if (GTK_WIDGET_REALIZED (e_table_scrolled_get_table (E_TABLE_SCROLLED (dialog->priv->list_e_table))->table_canvas)) {
		fill_in_pixbufs (dialog, e_table_model_row_count (priv->list_e_table_model) - 1);
	}

	gtk_notebook_append_page (GTK_NOTEBOOK (priv->notebook),
				  create_page_container (description, GTK_WIDGET (page_widget)),
				  NULL);

	if (priv->pages->next == NULL) {
		ETable *table;

		/* FIXME: This is supposed to select the first entry by default
		   but it doesn't seem to work at all.  */
		table = e_table_scrolled_get_table (E_TABLE_SCROLLED (priv->list_e_table));
		e_table_set_cursor_row (table, 0);
		e_selection_model_select_all (e_table_get_selection_model (table));
	}

	if (! e_config_page_is_applied (page_widget))
		priv->num_unapplied ++;

	gtk_signal_connect (GTK_OBJECT (page_widget), "changed",
			    GTK_SIGNAL_FUNC (page_changed_callback), dialog);

	update_buttons (dialog);
}

void
e_multi_config_dialog_show_page (EMultiConfigDialog *dialog, int page)
{
	EMultiConfigDialogPrivate *priv;

	g_return_if_fail (dialog != NULL);
	g_return_if_fail (E_IS_MULTI_CONFIG_DIALOG (dialog));

	priv = dialog->priv;

	e_table_set_cursor_row (e_table_scrolled_get_table (E_TABLE_SCROLLED (priv->list_e_table)), page);
	gtk_notebook_set_page (GTK_NOTEBOOK (priv->notebook), page);
}


E_MAKE_TYPE (e_multi_config_dialog, "EMultiConfigDialog", EMultiConfigDialog, class_init, init, PARENT_TYPE)
