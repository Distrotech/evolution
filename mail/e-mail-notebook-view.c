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
 * Copyright (C) 2010 Intel corporation. (www.intel.com)
 *
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif


#include <glib.h>
#include <glib/gi18n.h>

#include "mail/e-mail-reader.h"
#include "mail/message-list.h"
#include "e-mail-notebook-view.h"
#include "e-mail-folder-pane.h"
#include "e-mail-message-pane.h"

#include <shell/e-shell-window-actions.h>

struct _EMailNotebookViewPrivate {
	GtkNotebook *book;
	EMailView *current_view;
	GHashTable *views;
};

enum {
	PROP_0,
	PROP_GROUP_BY_THREADS,
};

#define E_SHELL_WINDOW_ACTION_GROUP_MAIL(window) \
	E_SHELL_WINDOW_ACTION_GROUP ((window), "mail")

static EMailViewClass *parent_class;
static GType mail_notebook_view_type;

static void
mail_notebook_view_init (EMailNotebookView  *shell)
{
	shell->priv = g_new0(EMailNotebookViewPrivate, 1);

	shell->priv->views = g_hash_table_new (g_str_hash, g_str_equal);
}

static void
e_mail_notebook_view_finalize (GObject *object)
{
	/* EMailNotebookView *shell = (EMailNotebookView *)object; */

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
mnv_page_changed (GtkNotebook *book, GtkNotebookPage *page,
		  guint page_num, EMailNotebookView *view)
{
	EMailView *mview = (EMailView *)gtk_notebook_get_nth_page (book, page_num);

	view->priv->current_view = mview;
	/* For EMailReader related changes to EShellView*/
	g_signal_emit_by_name (view, "changed");
	/* For EMailShellContent related changes */
	g_signal_emit_by_name (view, "view-changed");

}

static void
mail_notebook_view_constructed (GObject *object)
{
	GtkWidget *widget, *container;
	EMailNotebookViewPrivate *priv;

	priv = E_MAIL_NOTEBOOK_VIEW (object)->priv;

	container = GTK_WIDGET(object);

	widget = gtk_notebook_new ();
	priv->book = (GtkNotebook *)widget;
	gtk_widget_show (widget);
	gtk_box_pack_start (GTK_BOX(container), widget, TRUE, TRUE, 0);

	g_signal_connect (widget, "switch-page", G_CALLBACK(mnv_page_changed), object);

	priv->current_view = (EMailView *)e_mail_folder_pane_new (E_MAIL_VIEW(object)->content);
	e_mail_paned_view_set_preview_visible ((EMailPanedView *)priv->current_view, FALSE);
	gtk_widget_show ((GtkWidget *)priv->current_view);
	gtk_notebook_append_page (priv->book, (GtkWidget *)priv->current_view, gtk_label_new ("Please select a folder"));
	
}

static void
mail_notebook_view_set_property (GObject *object,
                                 guint property_id,
                                 const GValue *value,
                                 GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_GROUP_BY_THREADS:
			e_mail_reader_set_group_by_threads (
				E_MAIL_READER(E_MAIL_NOTEBOOK_VIEW(object)->priv->current_view),
				g_value_get_boolean (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
mail_notebook_view_get_property (GObject *object,
                                 guint property_id,
                                 GValue *value,
                                 GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_GROUP_BY_THREADS:
			g_value_set_boolean (
				value,
				e_mail_reader_get_group_by_threads (
				E_MAIL_READER(E_MAIL_NOTEBOOK_VIEW(object)->priv->current_view)));
			return;

	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
mail_notebook_view_class_init (EMailViewClass *klass)
{
	GObjectClass * object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);
	object_class->constructed = mail_notebook_view_constructed;
	object_class->set_property = mail_notebook_view_set_property;
	object_class->get_property = mail_notebook_view_get_property;
	
	object_class->finalize = e_mail_notebook_view_finalize;

	klass->get_searchbar = e_mail_notebook_view_get_searchbar;
	klass->set_search_strings = e_mail_notebook_view_set_search_strings;
	klass->get_view_instance = e_mail_notebook_view_get_view_instance;
	klass->update_view_instance = e_mail_notebook_view_update_view_instance;
	klass->set_orientation = e_mail_notebook_view_set_orientation;
	klass->get_orientation = e_mail_notebook_view_get_orientation;
	klass->set_show_deleted = e_mail_notebook_view_set_show_deleted;
	klass->get_show_deleted = e_mail_notebook_view_get_show_deleted;
	klass->set_preview_visible = e_mail_notebook_view_set_preview_visible;
	klass->get_preview_visible = e_mail_notebook_view_get_preview_visible;

	/* Inherited from EMailReader */
	g_object_class_override_property (
		object_class,
		PROP_GROUP_BY_THREADS,
		"group-by-threads");
/*
	g_object_class_install_property (
		object_class,
		PROP_PREVIEW_VISIBLE,
		g_param_spec_boolean (
			"preview-visible",
			"Preview is Visible",
			"Whether the preview pane is visible",
			TRUE,
			G_PARAM_READWRITE));

	g_object_class_install_property (
		object_class,
		PROP_SHOW_DELETED,
		g_param_spec_boolean (
			"show-deleted",
			"Show Deleted",
			NULL,
			FALSE,
			G_PARAM_READWRITE));

	g_object_class_override_property (
		object_class, PROP_ORIENTATION, "orientation"); */	
}



GtkWidget *
e_mail_notebook_view_new (EShellContent *content)
{
	g_return_val_if_fail (E_IS_SHELL_CONTENT (content), NULL);

	return g_object_new (
		E_MAIL_NOTEBOOK_VIEW_TYPE,
		"shell-content", content, NULL);
}

static GtkActionGroup *
mail_notebook_view_get_action_group (EMailReader *reader)
{
	EMailNotebookViewPrivate *priv = E_MAIL_NOTEBOOK_VIEW (reader)->priv;
	EShellContent *shell_content;
	EShellWindow *shell_window;
	EShellView *shell_view;

	shell_content = E_MAIL_VIEW (reader)->content;
	shell_view = e_shell_content_get_shell_view (shell_content);
	shell_window = e_shell_view_get_shell_window (shell_view);

	return E_SHELL_WINDOW_ACTION_GROUP_MAIL (shell_window);	
/*	
	if (!priv->current_view)
		return NULL;

	return e_mail_reader_get_action_group (E_MAIL_READER(priv->current_view));*/
}

static EMFormatHTML *
mail_notebook_view_get_formatter (EMailReader *reader)
{
	EMailNotebookViewPrivate *priv = E_MAIL_NOTEBOOK_VIEW (reader)->priv;
	
	if (!priv->current_view)
		return NULL;

	return e_mail_reader_get_formatter (E_MAIL_READER(priv->current_view));
}

static gboolean
mail_notebook_view_get_hide_deleted (EMailReader *reader)
{
	EMailNotebookViewPrivate *priv = E_MAIL_NOTEBOOK_VIEW (reader)->priv;
	
	if (!priv->current_view)
		return FALSE;

	return e_mail_reader_get_hide_deleted (E_MAIL_READER(priv->current_view));
}

static GtkWidget *
mail_notebook_view_get_message_list (EMailReader *reader)
{
	EMailNotebookViewPrivate *priv = E_MAIL_NOTEBOOK_VIEW (reader)->priv;
	
	if (!priv->current_view)
		return NULL;

	return e_mail_reader_get_message_list (E_MAIL_READER(priv->current_view));	
}

static GtkMenu *
mail_notebook_view_get_popup_menu (EMailReader *reader)
{
	EMailNotebookViewPrivate *priv = E_MAIL_NOTEBOOK_VIEW (reader)->priv;
	
	if (!priv->current_view)
		return NULL;

	return e_mail_reader_get_popup_menu (E_MAIL_READER(priv->current_view));	
}

static EShellBackend *
mail_notebook_view_get_shell_backend (EMailReader *reader)
{
	EShellContent *shell_content;
	EShellView *shell_view;

	shell_content = E_MAIL_VIEW (reader)->content;
	shell_view = e_shell_content_get_shell_view (shell_content);

	return e_shell_view_get_shell_backend (shell_view);
}

static GtkWindow *
mail_notebook_view_get_window (EMailReader *reader)
{
	EShellContent *shell_content;
	EShellWindow *shell_window;
	EShellView *shell_view;

	shell_content = E_MAIL_VIEW (reader)->content;
	shell_view = e_shell_content_get_shell_view (shell_content);
	shell_window = e_shell_view_get_shell_window (shell_view);

	return GTK_WINDOW (shell_window);
}

static int
emnv_get_page_num (EMailNotebookView *view,
		   GtkWidget *widget)
{
	EMailNotebookViewPrivate *priv = view->priv;
	int i, n;
	
	n = gtk_notebook_get_n_pages (priv->book);

	for (i=0; i<n; i++) {
		GtkWidget *curr = gtk_notebook_get_nth_page (priv->book, i);
		if (curr == widget)
			return i;
	}

	g_warn_if_reached ();

	return;
}

static void
reconnect_changed_event (EMailReader *child, EMailReader *parent)
{
	g_signal_emit_by_name (parent, "changed");
}

static void
reconnect_folder_loaded_event (EMailReader *child, EMailReader *parent)
{
	g_signal_emit_by_name (parent, "folder-loaded");
}

static void
mail_netbook_view_open_mail (EMailView *view, const char *uid, EMailNotebookView *nview)
{
	const gchar *folder_uri;
	CamelFolder *folder;	
	EMailMessagePane *pane = e_mail_message_pane_new (E_MAIL_VIEW(nview)->content);
	int page;
	EMailNotebookViewPrivate *priv = E_MAIL_NOTEBOOK_VIEW (nview)->priv;
	

	gtk_widget_show (pane);
	folder = e_mail_reader_get_folder (E_MAIL_READER(view));
	folder_uri = e_mail_reader_get_folder_uri (E_MAIL_READER(view));

	page = gtk_notebook_append_page (priv->book, pane, gtk_label_new (_("Mail")));
	gtk_notebook_set_current_page (priv->book, page);

	g_signal_connect ( E_MAIL_READER(pane), "changed",
			   G_CALLBACK (reconnect_changed_event),
			   nview);
	g_signal_connect ( E_MAIL_READER (pane), "folder-loaded",
			   G_CALLBACK (reconnect_folder_loaded_event),
			   nview);
	e_mail_reader_set_folder (
		E_MAIL_READER (pane), folder, folder_uri);
	e_mail_reader_set_group_by_threads (
		E_MAIL_READER (pane),
		e_mail_reader_get_group_by_threads (E_MAIL_READER(view))); 

	e_mail_reader_set_message (E_MAIL_READER (pane), uid);
}

static void
mail_notebook_view_set_folder (EMailReader *reader,
                               CamelFolder *folder,
                               const gchar *folder_uri)
{
	EMailNotebookViewPrivate *priv = E_MAIL_NOTEBOOK_VIEW (reader)->priv;
	GtkWidget *new_view;

	if (!folder_uri)
		return;

	new_view = g_hash_table_lookup (priv->views, folder_uri);
	if (new_view) {
		int curr = emnv_get_page_num (E_MAIL_NOTEBOOK_VIEW (reader), new_view);
		priv->current_view = (EMailView *)new_view;
		gtk_notebook_set_current_page (priv->book, curr);
		return;
	}

	if (folder || folder_uri) {
		int page;
		
		if (g_hash_table_size (priv->views) != 0) {
			priv->current_view = e_mail_folder_pane_new (E_MAIL_VIEW(reader)->content);
			gtk_widget_show (priv->current_view);
			page = gtk_notebook_append_page (priv->book, priv->current_view, gtk_label_new (camel_folder_get_full_name(folder)));
			gtk_notebook_set_current_page (priv->book, page);
		} else
			gtk_notebook_set_tab_label (priv->book, priv->current_view, gtk_label_new (camel_folder_get_full_name(folder)));

		e_mail_reader_set_folder (E_MAIL_READER(priv->current_view), folder, folder_uri);
		g_hash_table_insert (priv->views, g_strdup(folder_uri), priv->current_view);
		g_signal_connect ( E_MAIL_READER(priv->current_view), "changed",
				   G_CALLBACK (reconnect_changed_event),
				   reader);
		g_signal_connect ( E_MAIL_READER (priv->current_view), "folder-loaded",
				   G_CALLBACK (reconnect_folder_loaded_event),
				   reader);
		g_signal_connect ( priv->current_view, "open-mail",
				   G_CALLBACK (mail_netbook_view_open_mail), reader);
	}
}

static void
mail_notebook_view_show_search_bar (EMailReader *reader)
{
	EMailNotebookViewPrivate *priv = E_MAIL_NOTEBOOK_VIEW (reader)->priv;
		
	e_mail_reader_show_search_bar (E_MAIL_READER(priv->current_view));	
}

EShellSearchbar *
e_mail_notebook_view_get_searchbar (EMailView *view)
{
	EShellView *shell_view;
	EShellContent *shell_content;
	GtkWidget *widget;

	g_return_val_if_fail (
		E_IS_MAIL_NOTEBOOK_VIEW (view), NULL);

	shell_content = E_MAIL_VIEW (view)->content;
	shell_view = e_shell_content_get_shell_view (shell_content);
	widget = e_shell_view_get_searchbar (shell_view);

	return E_SHELL_SEARCHBAR (widget);	
/*	
	if (!E_MAIL_NOTEBOOK_VIEW(view)->priv->current_view)
		return NULL;
	return e_mail_view_get_searchbar (E_MAIL_NOTEBOOK_VIEW(view)->priv->current_view); */
}

static void
mail_notebook_view_open_selected_mail (EMailReader *reader)
{
	EMailNotebookViewPrivate *priv = E_MAIL_NOTEBOOK_VIEW (reader)->priv;
	
	if (!priv->current_view)
		return ;

	return e_mail_reader_open_selected_mail (E_MAIL_READER(priv->current_view));	
}

void
e_mail_notebook_view_set_search_strings (EMailView *view,
					 GSList *search_strings)
{
	e_mail_view_set_search_strings (E_MAIL_NOTEBOOK_VIEW (view)->priv->current_view, search_strings);
}

GalViewInstance *
e_mail_notebook_view_get_view_instance (EMailView *view)
{
	if (!E_MAIL_NOTEBOOK_VIEW(view)->priv->current_view)
		return NULL;

	return e_mail_view_get_view_instance (E_MAIL_NOTEBOOK_VIEW (view)->priv->current_view);
}

void
e_mail_notebook_view_update_view_instance (EMailView *view)
{
	e_mail_view_update_view_instance (E_MAIL_NOTEBOOK_VIEW (view)->priv->current_view);
}

static void
mail_notebook_view_reader_init (EMailReaderIface *iface)
{
	iface->get_action_group = mail_notebook_view_get_action_group;
	iface->get_formatter = mail_notebook_view_get_formatter;
	iface->get_hide_deleted = mail_notebook_view_get_hide_deleted;
	iface->get_message_list = mail_notebook_view_get_message_list;
	iface->get_popup_menu = mail_notebook_view_get_popup_menu;
	iface->get_shell_backend = mail_notebook_view_get_shell_backend;
	iface->get_window = mail_notebook_view_get_window;
	iface->set_folder = mail_notebook_view_set_folder;
	iface->show_search_bar = mail_notebook_view_show_search_bar;
	iface->open_selected_mail = mail_notebook_view_open_selected_mail;
}

GType
e_mail_notebook_view_get_type (void)
{
	return mail_notebook_view_type;
}

void
e_mail_notebook_view_register_type (GTypeModule *type_module)
{
	static const GTypeInfo type_info = {
		sizeof (EMailNotebookViewClass),
		(GBaseInitFunc) NULL,
		(GBaseFinalizeFunc) NULL,
		(GClassInitFunc) mail_notebook_view_class_init,
		(GClassFinalizeFunc) NULL,
		NULL,  /* class_data */
		sizeof (EMailNotebookView),
		0,     /* n_preallocs */
		(GInstanceInitFunc) mail_notebook_view_init,
		NULL   /* value_table */
	};

	static const GInterfaceInfo reader_info = {
		(GInterfaceInitFunc) mail_notebook_view_reader_init,
		(GInterfaceFinalizeFunc) NULL,
		NULL  /* interface_data */
	};

	mail_notebook_view_type = g_type_module_register_type (
		type_module, E_MAIL_VIEW_TYPE,
		"EMailNotebookView", &type_info, 0);

	g_type_module_add_interface (
		type_module, mail_notebook_view_type,
		E_TYPE_MAIL_READER, &reader_info);
}

void
e_mail_notebook_view_set_show_deleted (EMailNotebookView *view,
                                       gboolean show_deleted)
{
	if (!view->priv->current_view)
		return;
	
	e_mail_view_set_show_deleted (view->priv->current_view, show_deleted);
}
gboolean
e_mail_notebook_view_get_show_deleted (EMailNotebookView *view)
{
	if (!view->priv->current_view)
		return FALSE;
	
	return e_mail_view_get_show_deleted (view->priv->current_view);
}
void
e_mail_notebook_view_set_preview_visible (EMailNotebookView *view,
                                          gboolean preview_visible)
{
	if (!view->priv->current_view)
		return ;

	e_mail_view_set_preview_visible (view->priv->current_view, preview_visible);
}
gboolean
e_mail_notebook_view_get_preview_visible (EMailNotebookView *view)
{
	if (!view->priv->current_view)
		return FALSE;
	
	return e_mail_view_get_preview_visible (view->priv->current_view);
}
void
e_mail_notebook_view_set_orientation (EMailNotebookView *view,
				   GtkOrientation orientation)
{
	if (!view->priv->current_view)
		return;
	
	e_mail_view_set_orientation (view->priv->current_view, orientation);
}
GtkOrientation 
e_mail_notebook_view_get_orientation (EMailNotebookView *view)
{
	if (!view->priv->current_view)
		return GTK_ORIENTATION_VERTICAL;
	
	return e_mail_view_get_orientation (view->priv->current_view);
}
