/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
 *
 *  Copyright 2002 Ximian, Inc. (www.ximian.com)
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


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "mail-accounts.h"

#include <camel/camel-url.h>

#include <gtk/gtkliststore.h>
#include <gtk/gtktreeselection.h>

#include "mail.h"
#include "mail-config.h"
#include "mail-config-druid.h"
#include "mail-account-editor.h"
#include "mail-send-recv.h"

#include "art/mark.xpm"


static void mail_accounts_tab_class_init (MailAccountsTabClass *class);
static void mail_accounts_tab_init       (MailAccountsTab *prefs);
static void mail_accounts_tab_finalise   (GObject *obj);
static void mail_accounts_tab_destroy    (GtkObject *object);

static void mail_accounts_load (MailAccountsTab *tab);

static GdkPixbuf *disabled_pixbuf = NULL;
static GdkPixbuf *enabled_pixbuf = NULL;

static GtkVBoxClass *parent_class = NULL;


#define PREFS_WINDOW(prefs) GTK_WINDOW (gtk_widget_get_ancestor (GTK_WIDGET (prefs), GTK_TYPE_WINDOW))


GType
mail_accounts_tab_get_type (void)
{
	static GType type = 0;
	
	if (!type) {
		GTypeInfo type_info = {
			sizeof (MailAccountsTabClass),
			NULL, NULL,
			(GClassInitFunc) mail_accounts_tab_class_init,
			NULL, NULL,
			sizeof (MailAccountsTab),
			0,
			(GInstanceInitFunc) mail_accounts_tab_init,
		};
		
		type = g_type_register_static (gtk_vbox_get_type (), "MailAccountsTab", &type_info, 0);
	}
	
	return type;
}

static void
mail_accounts_tab_class_init (MailAccountsTabClass *klass)
{
	GtkObjectClass *gtk_object_class = (GtkObjectClass *) klass;
	GObjectClass *object_class = (GObjectClass *) klass;
	
	parent_class = g_type_class_ref (gtk_vbox_get_type ());
	
	gtk_object_class->destroy = mail_accounts_tab_destroy;
	
	object_class->finalize = mail_accounts_tab_finalise;
	
	/* setup static data */
	disabled_pixbuf = NULL;
	enabled_pixbuf = gdk_pixbuf_new_from_xpm_data ((const char **) mark_xpm);
}

static void
mail_accounts_tab_init (MailAccountsTab *prefs)
{
	prefs->druid = NULL;
	prefs->editor = NULL;
	
	gdk_pixbuf_render_pixmap_and_mask (enabled_pixbuf, &prefs->mark_pixmap, &prefs->mark_bitmap, 128);
}

static void
mail_accounts_tab_destroy (GtkObject *obj)
{
	MailAccountsTab *prefs = (MailAccountsTab *) obj;
	
	prefs->destroyed = TRUE;
	
	GTK_OBJECT_CLASS (parent_class)->destroy (obj);
}

static void
mail_accounts_tab_finalise (GObject *obj)
{
	MailAccountsTab *prefs = (MailAccountsTab *) obj;
	
	g_object_unref (prefs->gui);
	gdk_pixmap_unref (prefs->mark_pixmap);
	g_object_unref (prefs->mark_bitmap);
	
        G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
account_add_finished (MailAccountsTab *prefs, GObject *deadbeef)
{
	/* Either Cancel or Finished was clicked in the druid so reload the accounts */
	prefs->druid = NULL;
	
	if (!prefs->destroyed)
		mail_accounts_load (prefs);
	
	g_object_unref (prefs);
}

static void
account_add_clicked (GtkButton *button, gpointer user_data)
{
	MailAccountsTab *prefs = (MailAccountsTab *) user_data;
	GtkWidget *parent;
	
	if (prefs->druid == NULL) {
		prefs->druid = (GtkWidget *) mail_config_druid_new ();
		
		parent = gtk_widget_get_toplevel ((GtkWidget *) prefs);
		if (GTK_WIDGET_TOPLEVEL (parent))
			gtk_window_set_transient_for ((GtkWindow *) prefs->druid, (GtkWindow *) parent);
		
		g_object_weak_ref ((GObject *) prefs->druid,
				   (GWeakNotify) account_add_finished, prefs);
		
		gtk_widget_show (prefs->druid);
		g_object_ref (prefs);
	} else {
		gdk_window_raise (prefs->druid->window);
	}
}

static void
account_edit_finished (MailAccountsTab *prefs, GObject *deadbeef)
{
	prefs->editor = NULL;
	
	if (!prefs->destroyed)
		mail_accounts_load (prefs);
	
	g_object_unref (prefs);
}

static void
account_edit_clicked (GtkButton *button, gpointer user_data)
{
	MailAccountsTab *prefs = (MailAccountsTab *) user_data;
	
	if (prefs->editor == NULL) {
		GtkTreeSelection *selection;
		EAccount *account = NULL;
		GtkTreeModel *model;
		GtkTreeIter iter;
		
		selection = gtk_tree_view_get_selection (prefs->table);
		if (gtk_tree_selection_get_selected (selection, &model, &iter))
			gtk_tree_model_get (model, &iter, 3, &account, -1);
		
		if (account) {
			GtkWidget *parent;
			
			parent = gtk_widget_get_toplevel ((GtkWidget *) prefs);
			parent = GTK_WIDGET_TOPLEVEL (parent) ? parent : NULL;
			
			prefs->editor = (GtkWidget *) mail_account_editor_new (account, (GtkWindow *) parent, prefs);
			
			g_object_weak_ref ((GObject *) prefs->editor, (GWeakNotify) account_edit_finished, prefs);
			gtk_widget_show (prefs->editor);
			g_object_ref (prefs);
		}
	} else {
		gdk_window_raise (prefs->editor->window);
	}
}

static void
account_delete_clicked (GtkButton *button, gpointer user_data)
{
	MailAccountsTab *prefs = user_data;
	GtkTreeSelection *selection;
	EAccount *account = NULL;
	EAccountList *accounts;
	GtkTreeModel *model;
	GtkWidget *confirm;
	GtkTreeIter iter;
	int ans;
	
	selection = gtk_tree_view_get_selection (prefs->table);
	if (gtk_tree_selection_get_selected (selection, &model, &iter))
		gtk_tree_model_get (model, &iter, 3, &account, -1);
	
	/* make sure we have a valid account selected and that we aren't editing anything... */
	if (account == NULL || prefs->editor != NULL)
		return;
	
	confirm = gtk_message_dialog_new (PREFS_WINDOW (prefs),
					  GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,
					  GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE,
					  _("Are you sure you want to delete this account?"));
	
	button = (GtkButton *) gtk_button_new_from_stock (GTK_STOCK_YES);
	gtk_button_set_label (button, _("Delete"));
	gtk_dialog_add_action_widget ((GtkDialog *) confirm, (GtkWidget *) button, GTK_RESPONSE_YES);
	gtk_widget_show ((GtkWidget *) button);
	
	button = (GtkButton *) gtk_button_new_from_stock (GTK_STOCK_NO);
	gtk_button_set_label (button, _("Don't delete"));
	gtk_dialog_add_action_widget ((GtkDialog *) confirm, (GtkWidget *) button, GTK_RESPONSE_NO);
	gtk_widget_show ((GtkWidget *) button);
	
	ans = gtk_dialog_run ((GtkDialog *) confirm);
	gtk_widget_destroy (confirm);
	
	if (ans == GTK_RESPONSE_YES) {
		int len;
		
		/* remove it from the folder-tree in the shell */
		if (account->enabled && account->source && account->source->url)
			mail_remove_storage_by_uri (account->source->url);
		
		/* remove it from the config file */
		mail_config_remove_account (account);
		accounts = mail_config_get_accounts ();
		
		mail_config_write ();
		
		mail_autoreceive_setup ();
		
		gtk_list_store_remove ((GtkListStore *) model, &iter);
		
		len = e_list_length ((EList *) accounts);
		if (len > 0) {
			gtk_tree_selection_select_iter (selection, &iter);
		} else {
			gtk_widget_set_sensitive (GTK_WIDGET (prefs->mail_edit), FALSE);
			gtk_widget_set_sensitive (GTK_WIDGET (prefs->mail_delete), FALSE);
			gtk_widget_set_sensitive (GTK_WIDGET (prefs->mail_default), FALSE);
			gtk_widget_set_sensitive (GTK_WIDGET (prefs->mail_able), FALSE);
		}
	}
}

static void
account_default_clicked (GtkButton *button, gpointer user_data)
{
	MailAccountsTab *prefs = user_data;
	GtkTreeSelection *selection;
	EAccount *account = NULL;
	GtkTreeModel *model;
	GtkTreeIter iter;
	
	selection = gtk_tree_view_get_selection (prefs->table);
	if (gtk_tree_selection_get_selected (selection, &model, &iter))
		gtk_tree_model_get (model, &iter, 3, &account, -1);
	
	if (account) {
		mail_config_set_default_account (account);
		
		mail_config_write ();
		
		mail_accounts_load (prefs);
	}
}

static void
account_able_clicked (GtkButton *button, gpointer user_data)
{
	MailAccountsTab *prefs = user_data;
	GtkTreeSelection *selection;
	EAccount *account = NULL;
	GtkTreeModel *model;
	GtkTreeIter iter;
	
	selection = gtk_tree_view_get_selection (prefs->table);
	if (gtk_tree_selection_get_selected (selection, &model, &iter)) {
		gtk_tree_model_get (model, &iter, 3, &account, -1);
		account->enabled = !account->enabled;
		gtk_list_store_set ((GtkListStore *) model, &iter, 0, account->enabled, -1);
		
		gtk_button_set_label (prefs->mail_able, account->enabled ? _("Disable") : _("Enable"));
	}
	
	if (account) {
		/* if the account got disabled, remove it from the
		   folder-tree, otherwise add it to the folder-tree */
		if (account->source->url) {
			if (account->enabled)
				mail_load_storage_by_uri (prefs->shell, account->source->url, account->name);
			else
				mail_remove_storage_by_uri (account->source->url);
		}
		
		mail_autoreceive_setup ();
		
		mail_config_write ();
	}
}

static void
account_able_toggled (GtkCellRendererToggle *renderer, char *arg1, gpointer user_data)
{
	MailAccountsTab *prefs = user_data;
	GtkTreeSelection *selection;
	EAccount *account = NULL;
	GtkTreeModel *model;
	GtkTreePath *path;
	GtkTreeIter iter;
	
	path = gtk_tree_path_new_from_string (arg1);
	model = gtk_tree_view_get_model (prefs->table);
	selection = gtk_tree_view_get_selection (prefs->table);
	
	if (gtk_tree_model_get_iter (model, &iter, path)) {
		gtk_tree_model_get (model, &iter, 3, &account, -1);
		account->enabled = !account->enabled;
		gtk_list_store_set ((GtkListStore *) model, &iter, 0, account->enabled, -1);
		
		if (gtk_tree_selection_iter_is_selected (selection, &iter))
			gtk_button_set_label (prefs->mail_able, account->enabled ? _("Disable") : _("Enable"));
	}
	
	gtk_tree_path_free (path);
	
	if (account) {
		/* if the account got disabled, remove it from the
		   folder-tree, otherwise add it to the folder-tree */
		if (account->source->url) {
			if (account->enabled)
				mail_load_storage_by_uri (prefs->shell, account->source->url, account->name);
			else
				mail_remove_storage_by_uri (account->source->url);
		}
		
		mail_autoreceive_setup ();
		
		mail_config_write ();
	}
}

static void
account_double_click (GtkTreeView *treeview, GtkTreePath *path,
		      GtkTreeViewColumn *column, MailAccountsTab *prefs)
{
	account_edit_clicked (NULL, prefs);
}

static void
account_cursor_change (GtkTreeSelection *selection, MailAccountsTab *prefs)
{
	EAccount *account = NULL;
	GtkTreeModel *model;
	GtkTreeIter iter;
	int state;
	
	state = gtk_tree_selection_get_selected (selection, &model, &iter);
	if (state) {
		gtk_tree_model_get (model, &iter, 3, &account, -1);
		if (account->source && account->enabled)
			gtk_button_set_label (prefs->mail_able, _("Disable"));
		else
			gtk_button_set_label (prefs->mail_able, _("Enable"));
	} else {
		gtk_widget_grab_focus (GTK_WIDGET (prefs->mail_add));
	}
	
	gtk_widget_set_sensitive (GTK_WIDGET (prefs->mail_edit), state);
	gtk_widget_set_sensitive (GTK_WIDGET (prefs->mail_delete), state);
	gtk_widget_set_sensitive (GTK_WIDGET (prefs->mail_default), state);
	gtk_widget_set_sensitive (GTK_WIDGET (prefs->mail_able), state);
}

static void
mail_accounts_load (MailAccountsTab *prefs)
{
	EAccount *default_account;
	EAccountList *accounts;
	GtkListStore *model;
	GtkTreeIter iter;
	char *name, *val;
	EIterator *node;
	int row = 0;
	
	model = (GtkListStore *) gtk_tree_view_get_model (prefs->table);
	gtk_list_store_clear (model);
	
	default_account = mail_config_get_default_account ();
	
	accounts = mail_config_get_accounts ();
	node = e_list_get_iterator ((EList *) accounts);
	while (e_iterator_is_valid (node)) {
		EAccount *account;
		CamelURL *url;
		
		account = (EAccount *) e_iterator_get (node);
		
		url = account->source && account->source->url ? camel_url_new (account->source->url, NULL) : NULL;
		
		gtk_list_store_append (model, &iter);
		if (account == default_account) {
			/* translators: default account indicator */
			name = val = g_strdup_printf ("%s %s", account->name, _("[Default]"));
		} else {
			val = account->name;
			name = NULL;
		}
		
		gtk_list_store_set (model, &iter,
				    0, account->enabled,
				    1, val,
				    2, url && url->protocol ? url->protocol : (char *) _("None"),
				    3, account,
				    -1);
		g_free (name);
		
		if (url)
			camel_url_free (url);
		
		row++;
		
		e_iterator_next (node);
	}
	
	g_object_unref (node);
}



GtkWidget *mail_accounts_treeview_new (char *widget_name, char *string1, char *string2,
				       int int1, int int2);

GtkWidget *
mail_accounts_treeview_new (char *widget_name, char *string1, char *string2, int int1, int int2)
{
	GtkWidget *table, *scrolled;
	GtkTreeSelection *selection;
	GtkCellRenderer *renderer;
	GtkListStore *model;
	
	scrolled = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled),
					GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolled), GTK_SHADOW_IN);
	
	renderer = gtk_cell_renderer_toggle_new ();
	g_object_set ((GObject *) renderer, "activatable", TRUE, NULL);
	
	model = gtk_list_store_new (4, G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_POINTER);
	table = gtk_tree_view_new_with_model ((GtkTreeModel *) model);
	gtk_tree_view_insert_column_with_attributes ((GtkTreeView *) table, -1, _("Enabled"),
						     renderer, "active", 0, NULL);
	
	g_object_set_data ((GObject *) scrolled, "renderer", renderer);
	
	renderer = gtk_cell_renderer_text_new ();
	gtk_tree_view_insert_column_with_attributes ((GtkTreeView *) table, -1, _("Account name"),
						     renderer, "text", 1, NULL);
	gtk_tree_view_insert_column_with_attributes ((GtkTreeView *)table, -1, _("Protocol"),
						     renderer, "text", 2, NULL);
	selection = gtk_tree_view_get_selection ((GtkTreeView *) table);
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_SINGLE);
	gtk_tree_view_set_headers_visible ((GtkTreeView *) table, TRUE);
	
	/* FIXME: column auto-resize? */
	/* Is this needed?
	   gtk_tree_view_column_set_alignment (gtk_tree_view_get_column (prefs->table, 0), 1.0);*/
	
	gtk_container_add (GTK_CONTAINER (scrolled), table);
	
	g_object_set_data ((GObject *) scrolled, "table", table);
	
	gtk_widget_show (scrolled);
	gtk_widget_show (table);
	
	return scrolled;
}

static void
mail_accounts_tab_construct (MailAccountsTab *prefs)
{
	GtkWidget *toplevel, *widget;
	GtkCellRenderer *renderer;
	GladeXML *gui;
	
	gui = glade_xml_new (EVOLUTION_GLADEDIR "/mail-config.glade", "accounts_tab", NULL);
	prefs->gui = gui;
	
	/* get our toplevel widget */
	toplevel = glade_xml_get_widget (gui, "toplevel");
	
	/* reparent */
	gtk_widget_ref (toplevel);
	gtk_container_remove (GTK_CONTAINER (toplevel->parent), toplevel);
	gtk_container_add (GTK_CONTAINER (prefs), toplevel);
	gtk_widget_unref (toplevel);
	
	widget = glade_xml_get_widget (gui, "etableMailAccounts");
	
	prefs->table = (GtkTreeView *) g_object_get_data ((GObject *) widget, "table");
	g_signal_connect (gtk_tree_view_get_selection (prefs->table),
			  "changed", G_CALLBACK (account_cursor_change), prefs);
	g_signal_connect (prefs->table, "row-activated", G_CALLBACK (account_double_click), prefs);
	
	renderer = g_object_get_data ((GObject *) widget, "renderer");
	g_signal_connect (renderer, "toggled", G_CALLBACK (account_able_toggled), prefs);
	
	mail_accounts_load (prefs);
	
	prefs->mail_add = GTK_BUTTON (glade_xml_get_widget (gui, "cmdAccountAdd"));
	g_signal_connect (prefs->mail_add, "clicked", G_CALLBACK (account_add_clicked), prefs);
	
	prefs->mail_edit = GTK_BUTTON (glade_xml_get_widget (gui, "cmdAccountEdit"));
	g_signal_connect (prefs->mail_edit, "clicked", G_CALLBACK (account_edit_clicked), prefs);
	
	prefs->mail_delete = GTK_BUTTON (glade_xml_get_widget (gui, "cmdAccountDelete"));
	g_signal_connect (prefs->mail_delete, "clicked", G_CALLBACK (account_delete_clicked), prefs);
	
	prefs->mail_default = GTK_BUTTON (glade_xml_get_widget (gui, "cmdAccountDefault"));
	g_signal_connect (prefs->mail_default, "clicked", G_CALLBACK (account_default_clicked), prefs);
	
	prefs->mail_able = GTK_BUTTON (glade_xml_get_widget (gui, "cmdAccountAble"));
	g_signal_connect (prefs->mail_able, "clicked", G_CALLBACK (account_able_clicked), prefs);
}


GtkWidget *
mail_accounts_tab_new (GNOME_Evolution_Shell shell)
{
	MailAccountsTab *new;
	
	new = (MailAccountsTab *) g_object_new (mail_accounts_tab_get_type (), NULL);
	mail_accounts_tab_construct (new);
	new->shell = shell;
	
	return (GtkWidget *) new;
}


void
mail_accounts_tab_apply (MailAccountsTab *prefs)
{
	/* nothing to do here... */
}
