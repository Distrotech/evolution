/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
 *
 *  Copyright 2003 Ximian, Inc. (www.ximian.com)
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

#include <string.h>

#include <libgnome/gnome-i18n.h>

#include <gal/util/e-util.h>
#include <gal/widgets/e-gui-utils.h>

#include <gtk/gtkentry.h>
#include <gtk/gtkbox.h>
#include <gtk/gtkhbox.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkscrolledwindow.h>
#include <gtk/gtksignal.h>
#include <gtk/gtkstock.h>

#include <camel/camel-url.h>
#include <camel/camel-store.h>
#include <camel/camel-session.h>

#include "em-folder-tree.h"
#include "em-folder-selector.h"

#define d(x) x


extern CamelSession *session;


static void em_folder_selector_class_init (EMFolderSelectorClass *klass);
static void em_folder_selector_init (EMFolderSelector *emfs);
static void em_folder_selector_destroy (GtkObject *obj);
static void em_folder_selector_finalize (GObject *obj);


static GtkDialogClass *parent_class = NULL;


GType
em_folder_selector_get_type (void)
{
	static GType type = 0;
	
	if (!type) {
		static const GTypeInfo info = {
			sizeof (EMFolderSelectorClass),
			NULL, /* base_class_init */
			NULL, /* base_class_finalize */
			(GClassInitFunc) em_folder_selector_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (EMFolderSelector),
			0,    /* n_preallocs */
			(GInstanceInitFunc) em_folder_selector_init,
		};
		
		type = g_type_register_static (GTK_TYPE_DIALOG, "EMFolderSelector", &info, 0);
	}
	
	return type;
}

static void
em_folder_selector_class_init (EMFolderSelectorClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkObjectClass *gtk_object_class = GTK_OBJECT_CLASS (klass);
	
	parent_class = g_type_class_ref (GTK_TYPE_DIALOG);
	
	object_class->finalize = em_folder_selector_finalize;
	gtk_object_class->destroy = em_folder_selector_destroy;
}

static void
em_folder_selector_init (EMFolderSelector *emfs)
{
	emfs->selected_path = NULL;
	emfs->selected_uri = NULL;
}

static void
em_folder_selector_destroy (GtkObject *obj)
{
	GTK_OBJECT_CLASS (parent_class)->destroy (obj);
}

static void
em_folder_selector_finalize (GObject *obj)
{
	EMFolderSelector *emfs = (EMFolderSelector *) obj;
	
	g_free (emfs->selected_path);
	g_free (emfs->selected_uri);
	
	G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
emfs_response (GtkWidget *dialog, int response, EMFolderSelector *emfs)
{
	EMFolderTreeModel *model;
	const char *path, *uri;
	EMFolderTree *emft;
	
	if (response != EM_FOLDER_SELECTOR_RESPONSE_NEW)
		return;
	
	model = em_folder_tree_get_model (emfs->emft);
	emft = (EMFolderTree *) em_folder_tree_new_with_model (model);
	dialog = em_folder_selector_create_new (emft, 0, _("Create New Folder"), _("Specify where to create the folder:"));
	gtk_window_set_transient_for ((GtkWindow *) dialog, (GtkWindow *) emfs);
	
	if (gtk_dialog_run ((GtkDialog *) dialog) == GTK_RESPONSE_OK) {
		uri = em_folder_selector_get_selected_uri ((EMFolderSelector *) dialog);
		path = em_folder_selector_get_selected_path ((EMFolderSelector *) dialog);
		
		if (em_folder_tree_create_folder (emfs->emft, path, uri))
			em_folder_tree_set_selected (emfs->emft, uri);
	}
	
	gtk_widget_destroy (dialog);
	
	g_signal_stop_emission_by_name (emfs, "response");
}

static void
emfs_create_name_changed (GtkEntry *entry, EMFolderSelector *emfs)
{
	const char *path, *text = NULL;
	gboolean active;
	
	if (emfs->name_entry->text_length > 0)
		text = gtk_entry_get_text (emfs->name_entry);
	
	path = em_folder_tree_get_selected_path (emfs->emft);
	
	active = text && path && !strchr (text, '/');
	
	gtk_dialog_set_response_sensitive ((GtkDialog *) emfs, GTK_RESPONSE_OK, active);
}

static void
folder_selected_cb (EMFolderTree *emft, const char *path, const char *uri, EMFolderSelector *emfs)
{
	if (emfs->name_entry)
		emfs_create_name_changed (emfs->name_entry, emfs);
	else
		gtk_dialog_set_response_sensitive (GTK_DIALOG (emfs), GTK_RESPONSE_OK, TRUE);
}

void
em_folder_selector_construct (EMFolderSelector *emfs, EMFolderTree *emft, guint32 flags, const char *title, const char *text)
{
	GtkWidget *scrolled_window;
	GtkWidget *label;
	
	gtk_window_set_default_size (GTK_WINDOW (emfs), 350, 300);
	gtk_window_set_modal (GTK_WINDOW (emfs), TRUE);
	gtk_window_set_title (GTK_WINDOW (emfs), title);
	gtk_container_set_border_width (GTK_CONTAINER (emfs), 6);
	
	gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (emfs)->vbox), 6);
	gtk_container_set_border_width (GTK_CONTAINER (GTK_DIALOG (emfs)->vbox), 6);
	
	emfs->flags = flags;
	if (flags & EM_FOLDER_SELECTOR_CAN_CREATE) {
		gtk_dialog_add_button (GTK_DIALOG (emfs), GTK_STOCK_NEW, EM_FOLDER_SELECTOR_RESPONSE_NEW);
		g_signal_connect (emfs, "response", G_CALLBACK (emfs_response), emfs);
	}
	
	gtk_dialog_add_buttons (GTK_DIALOG (emfs), GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
				GTK_STOCK_OK, GTK_RESPONSE_OK, NULL);
	
	gtk_dialog_set_response_sensitive (GTK_DIALOG (emfs), GTK_RESPONSE_OK, FALSE);
	gtk_dialog_set_default_response (GTK_DIALOG (emfs), GTK_RESPONSE_OK);
	
	emfs->emft = emft;
	gtk_widget_show ((GtkWidget *) emft);
	
	g_signal_connect (emfs->emft, "folder-selected", G_CALLBACK (folder_selected_cb), emfs);
	
	scrolled_window = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolled_window), GTK_SHADOW_IN);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
					GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_widget_show (scrolled_window);
	
	gtk_container_add (GTK_CONTAINER (scrolled_window), (GtkWidget *) emft);
        
	gtk_box_pack_end (GTK_BOX (GTK_DIALOG (emfs)->vbox), scrolled_window, TRUE, TRUE, 6);
	
	if (text != NULL) {
		label = gtk_label_new (text);
		gtk_label_set_justify (GTK_LABEL (label), GTK_JUSTIFY_LEFT); 
		gtk_widget_show (label);
		
		gtk_box_pack_end (GTK_BOX (GTK_DIALOG (emfs)->vbox), label, FALSE, TRUE, 6);
	}
	
	GTK_WIDGET_SET_FLAGS ((GtkWidget *) emfs->emft, GTK_CAN_FOCUS);
	gtk_widget_grab_focus ((GtkWidget *) emfs->emft);
}

GtkWidget *
em_folder_selector_new (EMFolderTree *emft, guint32 flags, const char *title, const char *text)
{
	EMFolderSelector *emfs;
	
	emfs = g_object_new (em_folder_selector_get_type (), NULL);
	em_folder_selector_construct (emfs, emft, flags, title, text);
	
	return (GtkWidget *) emfs;
}


static void
emfs_create_name_activate (GtkEntry *entry, EMFolderSelector *emfs)
{
	g_signal_emit_by_name (emfs, "response", GTK_RESPONSE_OK);
}

GtkWidget *
em_folder_selector_create_new (EMFolderTree *emft, guint32 flags, const char *title, const char *text)
{
	EMFolderSelector *emfs;
	GtkWidget *hbox, *w;
	
	/* remove the CREATE flag if it is there since that's the
	 * whole purpose of this dialog */
	flags &= ~EM_FOLDER_SELECTOR_CAN_CREATE;
	
	emfs = g_object_new (em_folder_selector_get_type (), NULL);
	em_folder_selector_construct (emfs, emft, flags, title, text);
	
	hbox = gtk_hbox_new (FALSE, 0);
	w = gtk_label_new_with_mnemonic (_("Folder _name:"));
	gtk_box_pack_start ((GtkBox *) hbox, w, FALSE, FALSE, 6);
	emfs->name_entry = (GtkEntry *) gtk_entry_new ();
	gtk_label_set_mnemonic_widget (GTK_LABEL (w), (GtkWidget *) emfs->name_entry);
	g_signal_connect (emfs->name_entry, "changed", G_CALLBACK (emfs_create_name_changed), emfs);
	g_signal_connect (emfs->name_entry, "activate", G_CALLBACK (emfs_create_name_activate), emfs);
	gtk_box_pack_start ((GtkBox *) hbox, (GtkWidget *) emfs->name_entry, TRUE, FALSE, 6);
	gtk_widget_show_all (hbox);
	
	gtk_box_pack_start ((GtkBox *) ((GtkDialog *) emfs)->vbox, hbox, FALSE, TRUE, 0);
	
	return (GtkWidget *) emfs;
}


void
em_folder_selector_set_selected (EMFolderSelector *emfs, const char *uri)
{
	em_folder_tree_set_selected (emfs->emft, uri);
}

void
em_folder_selector_set_selected_list (EMFolderSelector *emfs, GList *list)
{
	em_folder_tree_set_selected_list (emfs->emft, list);
}

const char *
em_folder_selector_get_selected_uri (EMFolderSelector *emfs)
{
	const char *uri, *name;
	
	if (!(uri = em_folder_tree_get_selected_uri (emfs->emft))) {
		d(printf ("no selected folder?\n"));
		return NULL;
	}
	
	if (uri && emfs->name_entry) {
		CamelProvider *provider;
		CamelException ex;
		CamelURL *url;
		char *newpath;
		
		camel_exception_init (&ex);
		provider = camel_session_get_provider (session, uri, &ex);
		camel_exception_clear (&ex);
		
		name = gtk_entry_get_text (emfs->name_entry);
		
		url = camel_url_new (uri, NULL);
		if (provider && (provider->url_flags & CAMEL_URL_FRAGMENT_IS_PATH)) {
			if (url->fragment)
				newpath = g_strdup_printf ("%s/%s", url->fragment, name);
			else
				newpath = g_strdup (name);
			
			camel_url_set_fragment (url, newpath);
		} else {
			newpath = g_strdup_printf ("%s/%s", url->path ? url->path : "", name);
			camel_url_set_path (url, newpath);
		}
		
		g_free (emfs->selected_path);
		emfs->selected_path = newpath;
		
		g_free (emfs->selected_uri);
		emfs->selected_uri = camel_url_to_string (url, 0);
		
		camel_url_free (url);
		uri = emfs->selected_uri;
	}
	
	return uri;
}

GList *
em_folder_selector_get_selected_uris (EMFolderSelector *emfs)
{
	return em_folder_tree_get_selected_uris (emfs->emft);
}

GList *
em_folder_selector_get_selected_paths (EMFolderSelector *emfs)
{
	return em_folder_tree_get_selected_paths (emfs->emft);
}

const char *
em_folder_selector_get_selected_path (EMFolderSelector *emfs)
{
	const char *path;
	
	if (emfs->selected_path) {
		/* already did the work in a previous call */
		return emfs->selected_path;
	}
	
	if (!(path = em_folder_tree_get_selected_path (emfs->emft))) {
		d(printf ("no selected folder?\n"));
		return NULL;
	}
	
	if (path && emfs->name_entry) {
		const char *name;
		char *newpath;
		
		name = gtk_entry_get_text (emfs->name_entry);
		if (strcmp (path, "/") != 0)
			newpath = g_strdup_printf ("%s/%s", path, name);
		else
			newpath = g_strdup_printf ("/%s", name);
		
		path = emfs->selected_path = newpath;
	}
	
	return path;
}
