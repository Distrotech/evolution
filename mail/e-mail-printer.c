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
 * Copyright (C) 2011 Dan Vratil <dvratil@redhat.com>
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include <e-util/e-marshal.h>

#include "e-mail-printer.h"
#include "em-format-html-print.h"

static gpointer parent_class = NULL;

struct _EMailPrinterPrivate {
	EMFormatHTMLPrint *efhp;

	GtkListStore *headers;
	GtkTreeModel *sortable_headers;
	
	WebKitWebView *webview; /* WebView to print from */
};

G_DEFINE_TYPE (
	EMailPrinter, 
	e_mail_printer, 
	G_TYPE_OBJECT);

enum {
	PROP_0,
	PROP_PRINT_FORMATTER
};

enum {
	SIGNAL_DONE,
	LAST_SIGNAL
};

enum {
	COLUMN_ACTIVE,
	COLUMN_HEADER_NAME,
	COLUMN_HEADER_VALUE,
	COLUMN_HEADER_STRUCT,
	COLUMN_HEADER_SORT,
	LAST_COLUMN
};

static guint signals[LAST_SIGNAL]; 

static gint
emp_header_name_equal (const EMFormatHeader *h1,
		       const EMFormatHeader *h2)
{
	if ((h2->value == NULL) || (h1->value == NULL)) {
		return g_strcmp0 (h1->name, h2->name);
	} else {
		if ((g_strcmp0 (h1->name, h2->name) == 0) && 
	    	    (g_strcmp0 (h1->value, h2->value) == 0))
			return 0;
		else
			return 1;
	}
}

static void
emp_draw_footer (GtkPrintOperation *operation,
                 GtkPrintContext *context,
                 gint page_nr)
{
	PangoFontDescription *desc;
	PangoLayout *layout;
	gint n_pages;
	gdouble width, height;
	gchar *text;
	cairo_t *cr;

	cr = gtk_print_context_get_cairo_context (context);	
	width = gtk_print_context_get_width (context);
	height = gtk_print_context_get_height (context);

	g_object_get (operation, "n-pages", &n_pages, NULL);
	text = g_strdup_printf (_("Page %d of %d"), page_nr + 1, n_pages);

	cairo_set_source_rgb (cr, 0.1, 0.1, 0.1);
	cairo_fill (cr);

	desc = pango_font_description_from_string ("Sans Regular 10");
	layout = gtk_print_context_create_pango_layout (context);
	pango_layout_set_alignment (layout, PANGO_ALIGN_CENTER);
	pango_layout_set_font_description (layout, desc);
	pango_layout_set_text (layout, text, -1);
	pango_layout_set_width (layout, width * PANGO_SCALE);
	pango_font_description_free (desc);

	cairo_move_to (cr, 0, height + 5);
	pango_cairo_show_layout (cr, layout);

	g_object_unref (layout);
	g_free (text);
}

static void
emp_printing_done (GtkPrintOperation *operation,
		   GtkPrintOperationResult result,
		   gpointer user_data)
{
	EMailPrinter *emp = user_data;

	g_signal_emit (emp, signals[SIGNAL_DONE], 0, operation, result);
}


static void
emp_run_print_operation (EMailPrinter *emp,
		      	 GtkPrintOperation *operation)
{
	EMFormat *emf;
	SoupSession *session;
	GHashTable *formatters;
	WebKitWebFrame *frame;	
	gchar *mail_uri, *tmp;

	emf = EM_FORMAT (emp->priv->efhp);
	mail_uri = em_format_build_mail_uri (emf->folder, emf->message_uid, NULL, NULL);

	/* It's safe to assume that session exists and contains formatters table,
	 * because at least the message we are about to print now must be already
	 * there */
	session = webkit_get_default_session ();
	formatters = g_object_get_data (G_OBJECT (session), "formatters");
	g_hash_table_insert (formatters, g_strdup (mail_uri), emp->priv->efhp);

	/* Print_layout is a special PURI created by EMFormatHTMLPrint */
	tmp = g_strconcat (mail_uri, "?part_id=print_layout", NULL);
	if (emp->priv->webview == NULL) {
		emp->priv->webview = WEBKIT_WEB_VIEW (webkit_web_view_new ());
		g_object_ref_sink (emp->priv->webview);
	}

	webkit_web_view_load_uri (emp->priv->webview, tmp);

	frame = webkit_web_view_get_main_frame (emp->priv->webview);
	webkit_web_frame_print_full (frame, operation, GTK_PRINT_OPERATION_ACTION_PRINT_DIALOG, NULL);
	
	g_free (tmp);	
	g_free (mail_uri);
}


static void
header_active_renderer_toggled_cb (GtkCellRendererToggle *renderer,
				   gchar *path,
				   EMailPrinter *emp)
{
	EMFormat *emf = (EMFormat *)emp->priv->efhp;
	GtkTreeIter sorted_iter, iter;
	gboolean active;
	EMFormatHeader *header;

	gtk_tree_model_get_iter_from_string (GTK_TREE_MODEL (emp->priv->sortable_headers),
		&sorted_iter, path);
	gtk_tree_model_sort_convert_iter_to_child_iter (
		GTK_TREE_MODEL_SORT (emp->priv->sortable_headers),
		&iter, &sorted_iter);

	gtk_tree_model_get (GTK_TREE_MODEL (emp->priv->headers), &iter,
		COLUMN_ACTIVE, &active, -1);
	gtk_tree_model_get (GTK_TREE_MODEL (emp->priv->headers), &iter, 
		COLUMN_HEADER_STRUCT, &header, -1);
	gtk_list_store_set (GTK_LIST_STORE (emp->priv->headers), &iter, 
		COLUMN_ACTIVE, !active, -1);
	
	/* If the new state is active */
	if ((!active) == TRUE) {
		em_format_add_header_struct (emf, header);

		gtk_list_store_set (GTK_LIST_STORE (emp->priv->headers), &iter,
			COLUMN_HEADER_SORT, g_queue_index (&emf->header_list, header), -1);
	} else {
		em_format_remove_header_struct (emf, header);
	}
}

static GtkWidget*
emp_get_headers_tab (GtkPrintOperation *operation,
		     EMailPrinter *emp)
{
	GtkWidget *box, *label, *scw;
	GtkTreeView *view;
	GtkTreeViewColumn *column;
	GtkCellRenderer *renderer;

	box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 5);

	label = gtk_label_new (_("Select headers you want to print"));
	gtk_box_pack_start (GTK_BOX (box), label, FALSE, FALSE, 5);

	emp->priv->sortable_headers = gtk_tree_model_sort_new_with_model (
		GTK_TREE_MODEL (emp->priv->headers));
	gtk_tree_sortable_set_sort_column_id (
		GTK_TREE_SORTABLE (emp->priv->sortable_headers), COLUMN_HEADER_SORT,
		GTK_SORT_ASCENDING);
	view = GTK_TREE_VIEW (gtk_tree_view_new_with_model (emp->priv->sortable_headers));

	renderer = gtk_cell_renderer_toggle_new ();
	g_signal_connect (renderer, "toggled",
		G_CALLBACK (header_active_renderer_toggled_cb), emp);
	column = gtk_tree_view_column_new_with_attributes (
		_("Print"), renderer, 
		"active", COLUMN_ACTIVE, NULL);
	gtk_tree_view_append_column (view, column);

	renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes (
		_("Header Name"), renderer, 
		"text", COLUMN_HEADER_NAME, NULL);
	gtk_tree_view_append_column (view, column);

	renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes (
		_("Header Value"), renderer,
		"text", COLUMN_HEADER_VALUE, NULL);
	gtk_tree_view_append_column (view, column);

	scw = gtk_scrolled_window_new (NULL, NULL);
	gtk_container_add (GTK_CONTAINER (scw), GTK_WIDGET (view));
	gtk_box_pack_start (GTK_BOX (box), scw, TRUE, TRUE, 5);
	gtk_widget_show_all (box);

	gtk_print_operation_set_custom_tab_label (operation, _("Headers"));
	return box;
}

static void
emp_set_formatter (EMailPrinter *emp,
		   EMFormatHTMLPrint *formatter)
{
	EMFormat *emf = (EMFormat *) formatter;
	CamelMediumHeader *header;
	GArray *headers;
	gint i;

	g_return_if_fail (EM_IS_FORMAT_HTML_PRINT (formatter));

	g_object_ref (formatter);

	if (emp->priv->efhp)
		g_object_unref (emp->priv->efhp);

	emp->priv->efhp = formatter;

	if (emp->priv->headers)
		g_object_unref (emp->priv->headers);
	emp->priv->headers = gtk_list_store_new (5, 
		G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_POINTER, G_TYPE_INT);

	headers = camel_medium_get_headers (CAMEL_MEDIUM (emf->message));
	for (i = 0; i < headers->len; i++) {
		GtkTreeIter iter;
		GList *found_header;
		EMFormatHeader *emfh;
		gint index;

		header = &g_array_index (headers, CamelMediumHeader, i);
		emfh = em_format_header_new (header->name, header->value);
		
		found_header = g_queue_find_custom (&EM_FORMAT (formatter)->header_list,
				emfh, (GCompareFunc) emp_header_name_equal);

		if (found_header) {
			index = g_queue_link_index (&EM_FORMAT (formatter)->header_list, 
				found_header);
		} else {
			index = G_MAXINT;
		}
		gtk_list_store_append (emp->priv->headers, &iter);

		gtk_list_store_set (emp->priv->headers, &iter,
			COLUMN_ACTIVE, (found_header != NULL),
			COLUMN_HEADER_NAME, emfh->name,
			COLUMN_HEADER_VALUE, emfh->value,
			COLUMN_HEADER_STRUCT, emfh,
		      	COLUMN_HEADER_SORT, index, -1);
	}

	camel_medium_free_headers (CAMEL_MEDIUM (emf->message), headers);
}

static void
emp_set_property (GObject *object,
                  guint property_id,
                  const GValue *value,
                  GParamSpec *pspec)
{
	EMailPrinter *emp = E_MAIL_PRINTER (object);

	switch (property_id) {

		case PROP_PRINT_FORMATTER:
			emp_set_formatter (emp, g_value_get_object (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
emp_get_property (GObject *object,
		  guint property_id,
		  GValue *value,
		  GParamSpec *pspec)
{
	EMailPrinter *emp = E_MAIL_PRINTER (object);

	switch (property_id) {

		case PROP_PRINT_FORMATTER:
			g_value_set_object (value,
				e_mail_printer_get_print_formatter (emp));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
emp_finalize (GObject *object)
{
	EMailPrinterPrivate *priv = E_MAIL_PRINTER (object)->priv;

	if (priv->efhp) {
		g_object_unref (priv->efhp);
		priv->efhp = NULL;
	}

	if (priv->sortable_headers) {
		g_object_unref (priv->sortable_headers);
		priv->sortable_headers = NULL;
	}

	if (priv->headers) {
		GtkTreeIter iter;
		
		gtk_tree_model_get_iter_first (GTK_TREE_MODEL (priv->headers), &iter);
		while (gtk_tree_model_iter_next (GTK_TREE_MODEL (priv->headers), &iter)) {
			EMFormatHeader *header = NULL;
			gtk_tree_model_get (GTK_TREE_MODEL (priv->headers), &iter,
				COLUMN_HEADER_STRUCT, &header, -1);
			em_format_header_free (header);
		}
		g_object_unref (priv->headers);
		priv->headers = NULL;
	}

	if (priv->webview) {
		g_object_unref (priv->webview);
		priv->webview = NULL;
	}

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
e_mail_printer_class_init (EMailPrinterClass *klass)
{
	GObjectClass *object_class;

	parent_class = g_type_class_peek_parent (klass);
	g_type_class_add_private (klass, sizeof (EMailPrinterPrivate));

	object_class = G_OBJECT_CLASS (klass);
	object_class->set_property = emp_set_property;
	object_class->get_property = emp_get_property;
	object_class->finalize = emp_finalize;

	g_object_class_install_property (
		object_class,
		PROP_PRINT_FORMATTER,
		g_param_spec_object (
			"print-formatter",
			NULL,
			NULL,
			EM_TYPE_FORMAT_HTML_PRINT,
		        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	signals[SIGNAL_DONE] =	g_signal_new ("done",
		G_TYPE_FROM_CLASS (klass),
		G_SIGNAL_RUN_FIRST,
		G_STRUCT_OFFSET (EMailPrinterClass, done),
		NULL, NULL,
		e_marshal_VOID__OBJECT_INT,
		G_TYPE_NONE, 2,
		GTK_TYPE_PRINT_OPERATION, G_TYPE_INT);
}

static void
e_mail_printer_init (EMailPrinter *emp)
{
	emp->priv = G_TYPE_INSTANCE_GET_PRIVATE (
		emp, E_TYPE_MAIL_PRINTER, EMailPrinterPrivate);

	emp->priv->efhp = NULL;
	emp->priv->headers = NULL;
	emp->priv->webview = NULL;
}

EMailPrinter* 
e_mail_printer_new (EMFormatHTML* source,
		    GtkPrintOperationAction action)
{
	EMailPrinter *emp;
	EMFormatHTMLPrint *efhp;

	efhp = em_format_html_print_new (source, action);

	emp = g_object_new (E_TYPE_MAIL_PRINTER,
		"print-formatter", efhp, NULL);

	g_object_unref (efhp);

	return emp;
}

void
e_mail_printer_print (EMailPrinter *emp,
		      GCancellable *cancellable)
{
	GtkPrintOperation *operation;	

	g_return_if_fail (E_IS_MAIL_PRINTER (emp));

	operation = gtk_print_operation_new ();
	gtk_print_operation_set_show_progress (operation, TRUE);
	g_signal_connect (operation, "create-custom-widget",
		G_CALLBACK (emp_get_headers_tab), emp);
	g_signal_connect (operation, "done",
		G_CALLBACK (emp_printing_done), emp);
	g_signal_connect_swapped (cancellable, "cancelled",
		G_CALLBACK (gtk_print_operation_cancel), operation);
	g_signal_connect (operation, "draw-page",
		G_CALLBACK (emp_draw_footer), NULL);

	emp_run_print_operation (emp, operation);
}

EMFormatHTMLPrint*
e_mail_printer_get_print_formatter (EMailPrinter *emp)
{
	g_return_val_if_fail (E_IS_MAIL_PRINTER (emp), NULL);

	return emp->priv->efhp;
}

