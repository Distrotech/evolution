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

#include "em-folder-tree.h"
#include "em-folder-selector.h"

#define d(x) x


static void em_folder_selector_class_init (EMFolderSelectorClass *klass);
static void em_folder_selector_init (EMFolderSelector *emfs);
static void em_folder_selector_destroy (GtkObject *obj);
static void em_folder_selector_finalize (GObject *obj);
static void em_folder_selector_response (GtkDialog *dialog, int response);


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
	GtkDialogClass *dialog_class = GTK_DIALOG_CLASS (klass);
	
	parent_class = g_type_class_ref (GTK_TYPE_DIALOG);
	
	object_class->finalize = em_folder_selector_finalize;
	gtk_object_class->destroy = em_folder_selector_destroy;
	
	dialog_class->response = em_folder_selector_response;
}

static void
em_folder_selector_init (EMFolderSelector *emfs)
{
	;
}

static void
em_folder_selector_destroy (GtkObject *obj)
{
	GTK_OBJECT_CLASS (parent_class)->destroy (obj);
}

static void
em_folder_selector_finalize (GObject *obj)
{
	G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
em_folder_selector_response (GtkDialog *dialog, int response)
{
	EMFolderSelector *emfs = (EMFolderSelector *) dialog;
	
	switch (response) {
	case EM_FOLDER_SELECTOR_RESPONSE_NEW:
		/* FIXME: implement me */
		printf ("create new folder, default parent '%s'\n", path);
		break;
	}
}



static void
folder_selected_cb (EMFolderTree *emft, const char *path, const char *uri, EMFolderSelector *emfs)
{
	gtk_dialog_set_response_sensitive (GTK_DIALOG (emfs), GTK_RESPONSE_OK, TRUE);
}

void
em_folder_selector_construct (EMFolderSelector *emfs, EMFolderTree *tree, guint32 flags, const char *title, const char *text)
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
	if (flags & EM_FOLDER_SELECTOR_CAN_CREATE)
		gtk_dialog_add_buttons (GTK_DIALOG (emfs), GTK_STOCK_NEW, EM_FOLDER_SELECTOR_RESPONSE_NEW, NULL);
	
	gtk_dialog_add_buttons (GTK_DIALOG (emfs), GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
				GTK_STOCK_OK, GTK_RESPONSE_OK, NULL);
	
	gtk_dialog_set_response_sensitive (GTK_DIALOG (emfs), GTK_RESPONSE_OK, FALSE);
	gtk_dialog_set_default_response (GTK_DIALOG (emfs), GTK_RESPONSE_OK);
	
	emfs->emft = emft;
	gtk_widget_show ((GtkWidget *) emfs->emft);
	
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
emfs_create_name_changed (GtkEntry *entry, EMFolderSelector *emfs)
{
	gboolean active;
	
	/* FIXME: need to port this... */
	active = /* folder does not exist && */ emfs->name_entry->text_length > 0;
	
	gtk_dialog_set_response_sensitive ((GtkDialog *) emfs, GTK_RESPONSE_OK, active);
}

static void
emfs_create_name_activate (GtkEntry *entry, EMFolderSelector *emfs)
{
	/* FIXME: create the folder... */
	printf ("entry activated, woop\n");
}

GtkWidget *
em_folder_selector_create_new (EMFolderTree *emft, guint32 flags, const char *title, const char *text)
{
	EMFolderSelector *emfs;
	GtkWidget *hbox, *w;
	
	emfs = g_object_new (em_folder_selector_get_type (), NULL);
	em_folder_selector_construct (emfs, emft, flags, title, text);
	
	hbox = gtk_hbox_new (FALSE, 0);
	w = gtk_label_new_with_mnemonic (_("Folder _name"));
	gtk_box_pack_start ((GtkBox *) hbox, w, FALSE, FALSE, 6);
	emfs->name_entry = (GtkEntry *) gtk_entry_new ();
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

const char *
em_folder_selector_get_selected_uri (EMFolderSelector *emfs)
{
	const char *uri;
	
	if (!(uri = em_folder_tree_get_selected (emfs->emft))) {
		d(printf ("no selected folder?\n"));
		return NULL;
	}
	
	/* FIXME: finish porting this... */
	path = e_folder_get_physical_uri(folder);
	if (path && emfs->name_entry) {
		CamelURL *url;
		char *newpath;

		url = camel_url_new(path, NULL);
		newpath = g_strdup_printf("%s/%s", url->fragment?url->fragment:url->path, gtk_entry_get_text(emfs->name_entry));
		if (url->fragment)
			camel_url_set_fragment(url, newpath);
		else
			camel_url_set_path(url, newpath);
		g_free(emfs->selected_uri);
		emfs->selected_uri = camel_url_to_string(url, 0);
		camel_url_free(url);
		path = emfs->selected_uri;
	}

	return path;
}

