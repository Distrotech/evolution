/*
 * e-preferences-window.c
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
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#include "e-preferences-window.h"

#include <glib/gi18n.h>
#include <e-util/e-util.h>

#define SWITCH_PAGE_INTERVAL 250

#define E_PREFERENCES_WINDOW_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_PREFERENCES_WINDOW, EPreferencesWindowPrivate))

struct _EPreferencesWindowPrivate {
	GtkWidget *icon_view;
	GtkWidget *notebook;
	GHashTable *index;
};

enum {
	COLUMN_TEXT,	/* G_TYPE_STRING */
	COLUMN_PIXBUF,	/* GDK_TYPE_PIXBUF */
	COLUMN_PAGE,	/* G_TYPE_INT */
	COLUMN_SORT	/* G_TYPE_INT */
};

static gpointer parent_class;

static GdkPixbuf *
preferences_window_load_pixbuf (const gchar *icon_name)
{
	GtkIconTheme *icon_theme;
	GtkIconInfo *icon_info;
	GdkPixbuf *pixbuf;
	const gchar *filename;
	gint size;
	GError *error = NULL;

	icon_theme = gtk_icon_theme_get_default ();

	if (!gtk_icon_size_lookup (GTK_ICON_SIZE_DIALOG, &size, 0))
		return NULL;

	icon_info = gtk_icon_theme_lookup_icon (
		icon_theme, icon_name, size, 0);

	if (icon_info == NULL)
		return NULL;

	filename = gtk_icon_info_get_filename (icon_info);

	pixbuf = gdk_pixbuf_new_from_file (filename, &error);

	gtk_icon_info_free (icon_info);

	if (error != NULL) {
		g_warning ("%s", error->message);
		g_error_free (error);
	}

	return pixbuf;
}

static void
preferences_window_help_clicked_cb (GtkWindow *window)
{
	e_display_help (window, "config-prefs");
}

static void
preferences_window_selection_changed_cb (EPreferencesWindow *window)
{
	GtkIconView *icon_view;
	GtkNotebook *notebook;
	GtkTreeModel *model;
	GtkTreeIter iter;
	GList *list;
	gint page;

	icon_view = GTK_ICON_VIEW (window->priv->icon_view);
	list = gtk_icon_view_get_selected_items (icon_view);
	if (list == NULL)
		return;

	model = gtk_icon_view_get_model (icon_view);
	gtk_tree_model_get_iter (model, &iter, list->data);
	gtk_tree_model_get (model, &iter, COLUMN_PAGE, &page, -1);

	notebook = GTK_NOTEBOOK (window->priv->notebook);
	gtk_notebook_set_current_page (notebook, page);

	g_list_foreach (list, (GFunc) gtk_tree_path_free, NULL);
	g_list_free (list);

	gtk_widget_grab_focus (GTK_WIDGET (icon_view));
}

static void
preferences_window_dispose (GObject *object)
{
	EPreferencesWindowPrivate *priv;

	priv = E_PREFERENCES_WINDOW_GET_PRIVATE (object);

	if (priv->icon_view != NULL) {
		g_object_unref (priv->icon_view);
		priv->icon_view = NULL;
	}

	if (priv->notebook != NULL) {
		g_object_unref (priv->notebook);
		priv->notebook = NULL;
	}

	g_hash_table_remove_all (priv->index);

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
preferences_window_finalize (GObject *object)
{
	EPreferencesWindowPrivate *priv;

	priv = E_PREFERENCES_WINDOW_GET_PRIVATE (object);

	g_hash_table_destroy (priv->index);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
preferences_window_show (GtkWidget *widget)
{
	EPreferencesWindowPrivate *priv;

	priv = E_PREFERENCES_WINDOW_GET_PRIVATE (widget);

	gtk_widget_grab_focus (priv->icon_view);

	/* Chain up to parent's show() method. */
	GTK_WIDGET_CLASS (parent_class)->show (widget);
}

static void
preferences_window_class_init (EPreferencesWindowClass *class)
{
	GObjectClass *object_class;
	GtkWidgetClass *widget_class;

	parent_class = g_type_class_peek_parent (class);
	g_type_class_add_private (class, sizeof (EPreferencesWindowPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = preferences_window_dispose;
	object_class->finalize = preferences_window_finalize;

	widget_class = GTK_WIDGET_CLASS (class);
	widget_class->show = preferences_window_show;
}

static void
preferences_window_init (EPreferencesWindow *window)
{
	GtkListStore *store;
	GtkWidget *container;
	GtkWidget *hbox;
	GtkWidget *vbox;
	GtkWidget *widget;
	GHashTable *index;
	const gchar *title;

	index = g_hash_table_new_full (
		g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) gtk_tree_row_reference_free);

	window->priv = E_PREFERENCES_WINDOW_GET_PRIVATE (window);
	window->priv->index = index;

	store = gtk_list_store_new (
		4, G_TYPE_STRING, GDK_TYPE_PIXBUF, G_TYPE_INT, G_TYPE_INT);
	gtk_tree_sortable_set_sort_column_id (
		GTK_TREE_SORTABLE (store), COLUMN_SORT, GTK_SORT_ASCENDING);

	title = _("Evolution Preferences");
	gtk_window_set_title (GTK_WINDOW (window), title);
	gtk_window_set_resizable (GTK_WINDOW (window), TRUE);
	gtk_container_set_border_width (GTK_CONTAINER (window), 12);

	g_signal_connect (
		window, "delete-event",
		G_CALLBACK (gtk_widget_hide_on_delete), NULL);

	widget = gtk_vbox_new (FALSE, 12);
	gtk_container_add (GTK_CONTAINER (window), widget);
	gtk_widget_show (widget);

	vbox = widget;

	widget = gtk_hbox_new (FALSE, 12);
	gtk_box_pack_start (GTK_BOX (vbox), widget, TRUE, TRUE, 0);
	gtk_widget_show (widget);

	hbox = widget;

	widget = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (
		GTK_SCROLLED_WINDOW (widget),
		GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type (
		GTK_SCROLLED_WINDOW (widget), GTK_SHADOW_IN);
	gtk_box_pack_start (GTK_BOX (hbox), widget, FALSE, TRUE, 0);
	gtk_widget_show (widget);

	container = widget;

	widget = gtk_icon_view_new_with_model (GTK_TREE_MODEL (store));
	gtk_icon_view_set_columns (GTK_ICON_VIEW (widget), 1);
	gtk_icon_view_set_text_column (GTK_ICON_VIEW (widget), COLUMN_TEXT);
	gtk_icon_view_set_pixbuf_column (GTK_ICON_VIEW (widget), COLUMN_PIXBUF);
	g_signal_connect_swapped (
		widget, "selection-changed",
		G_CALLBACK (preferences_window_selection_changed_cb), window);
	gtk_container_add (GTK_CONTAINER (container), widget);
	window->priv->icon_view = g_object_ref (widget);
	gtk_widget_show (widget);
	g_object_unref (store);

	widget = gtk_notebook_new ();
	gtk_notebook_set_show_tabs (GTK_NOTEBOOK (widget), FALSE);
	gtk_notebook_set_show_border (GTK_NOTEBOOK (widget), FALSE);
	gtk_box_pack_start (GTK_BOX (hbox), widget, TRUE, TRUE, 0);
	window->priv->notebook = g_object_ref (widget);
	gtk_widget_show (widget);

	widget = gtk_hbutton_box_new ();
	gtk_button_box_set_layout (
		GTK_BUTTON_BOX (widget), GTK_BUTTONBOX_END);
	gtk_box_pack_start (GTK_BOX (vbox), widget, FALSE, FALSE, 0);
	gtk_widget_show (widget);

	container = widget;

	widget = gtk_button_new_from_stock (GTK_STOCK_HELP);
	g_signal_connect_swapped (
		widget, "clicked",
		G_CALLBACK (preferences_window_help_clicked_cb), window);
	gtk_box_pack_start (GTK_BOX (container), widget, FALSE, FALSE, 0);
	gtk_button_box_set_child_secondary (
		GTK_BUTTON_BOX (container), widget, TRUE);
	gtk_widget_show (widget);

	widget = gtk_button_new_from_stock (GTK_STOCK_CLOSE);
	g_signal_connect_swapped (
		widget, "clicked",
		G_CALLBACK (gtk_widget_hide), window);
	GTK_WIDGET_SET_FLAGS (widget, GTK_CAN_DEFAULT);
	gtk_box_pack_start (GTK_BOX (container), widget, FALSE, FALSE, 0);
	gtk_widget_grab_default (widget);
	gtk_widget_show (widget);
}

GType
e_preferences_window_get_type (void)
{
	static GType type = 0;

	if (G_UNLIKELY (type == 0)) {
		const GTypeInfo type_info = {
			sizeof (EPreferencesWindowClass),
			(GBaseInitFunc) NULL,
			(GBaseFinalizeFunc) NULL,
			(GClassInitFunc) preferences_window_class_init,
			(GClassFinalizeFunc) NULL,
			NULL,  /* class_data */
			sizeof (EPreferencesWindow),
			0,     /* n_preallocs */
			(GInstanceInitFunc) preferences_window_init,
			NULL   /* value_table */
		};

		type = g_type_register_static (
			GTK_TYPE_WINDOW, "EPreferencesWindow", &type_info, 0);
	}

	return type;
}

GtkWidget *
e_preferences_window_new (void)
{
	return g_object_new (E_TYPE_PREFERENCES_WINDOW, NULL);
}

void
e_preferences_window_add_page (EPreferencesWindow *window,
                               const gchar *page_name,
                               const gchar *icon_name,
                               const gchar *caption,
                               GtkWidget *widget,
                               gint sort_order)
{
	GtkTreeRowReference *reference;
	GtkIconView *icon_view;
	GtkNotebook *notebook;
	GtkTreeModel *model;
	GtkTreePath *path;
	GHashTable *index;
	GdkPixbuf *pixbuf;
	GtkTreeIter iter;
	gint page;

	g_return_if_fail (E_IS_PREFERENCES_WINDOW (window));
	g_return_if_fail (page_name != NULL);
	g_return_if_fail (icon_name != NULL);
	g_return_if_fail (caption != NULL);
	g_return_if_fail (GTK_IS_WIDGET (widget));

	icon_view = GTK_ICON_VIEW (window->priv->icon_view);
	notebook = GTK_NOTEBOOK (window->priv->notebook);

	page = gtk_notebook_get_n_pages (notebook);
	model = gtk_icon_view_get_model (icon_view);
	pixbuf = preferences_window_load_pixbuf (icon_name);

	gtk_list_store_append (GTK_LIST_STORE (model), &iter);

	gtk_list_store_set (
		GTK_LIST_STORE (model), &iter,
		COLUMN_TEXT, caption, COLUMN_PIXBUF, pixbuf,
		COLUMN_PAGE, page, COLUMN_SORT, sort_order, -1);

	index = window->priv->index;
	path = gtk_tree_model_get_path (model, &iter);
	reference = gtk_tree_row_reference_new (model, path);
	g_hash_table_insert (index, g_strdup (page_name), reference);
	gtk_tree_path_free (path);

	gtk_notebook_append_page (notebook, widget, NULL);

	if (page == 0)
		e_preferences_window_show_page (window, page_name);
}

void
e_preferences_window_show_page (EPreferencesWindow *window,
                                const gchar *page_name)
{
	GtkTreeRowReference *reference;
	GtkIconView *icon_view;
	GtkTreePath *path;

	g_return_if_fail (E_IS_PREFERENCES_WINDOW (window));
	g_return_if_fail (page_name != NULL);

	icon_view = GTK_ICON_VIEW (window->priv->icon_view);
	reference = g_hash_table_lookup (window->priv->index, page_name);
	g_return_if_fail (reference != NULL);

	path = gtk_tree_row_reference_get_path (reference);
	gtk_icon_view_select_path (icon_view, path);
	gtk_icon_view_scroll_to_path (icon_view, path, FALSE, 0.0, 0.0);
	gtk_tree_path_free (path);
}
