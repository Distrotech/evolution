/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* evolution-folder-selector-button.c
 *
 * Copyright (C) 2002 Ximian, Inc.
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
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "evolution-folder-selector-button.h"
#include <bonobo/bonobo-ui-toolbar-icon.h>
#include <gal/util/e-util.h>
#include <gal/widgets/e-unicode.h>
#include <gtk/gtkhbox.h>
#include <gtk/gtklabel.h>
#include <libgnome/gnome-i18n.h>


struct _EvolutionFolderSelectorButtonPrivate {
	EvolutionShellClient *shell_client;
	GNOME_Evolution_StorageRegistry corba_storage_registry;
	GNOME_Evolution_Folder *selected_folder;
	GtkWidget *icon, *label;
	char *title, **possible_types;
};

enum {
	POPPED_UP,
	SELECTED,
	CANCELED,
	LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };

#define PARENT_TYPE gtk_button_get_type ()
static GtkButtonClass *parent_class = NULL;


static GNOME_Evolution_Folder *
get_folder_for_uri (EvolutionFolderSelectorButton *folder_selector_button,
		    const char *uri)
{
	EvolutionFolderSelectorButtonPrivate *priv = folder_selector_button->priv;
	CORBA_Environment ev;
	GNOME_Evolution_Folder *folder;

	if (!uri)
		return NULL;

	CORBA_exception_init (&ev);
	folder = GNOME_Evolution_StorageRegistry_getFolderByUri (
		priv->corba_storage_registry, uri, &ev);
	if (ev._major != CORBA_NO_EXCEPTION)
		folder = CORBA_OBJECT_NIL;
	CORBA_exception_free (&ev);

	return folder;
}

static void
set_folder (EvolutionFolderSelectorButton *folder_selector_button,
	    GNOME_Evolution_Folder *folder)
{
	GtkWidget *w = GTK_WIDGET (folder_selector_button);
	EvolutionFolderSelectorButtonPrivate *priv;
	GdkPixbuf *pixbuf;
	char *folder_lname, *storage_lname, *label_text;
	const char *p;

	priv = folder_selector_button->priv;

	if (priv->selected_folder)
		CORBA_free (priv->selected_folder);
	priv->selected_folder = folder;

	if (!folder) {
		bonobo_ui_toolbar_icon_clear (BONOBO_UI_TOOLBAR_ICON (priv->icon));
		gtk_label_set_text (GTK_LABEL (priv->label),
				    _("<click here to select a folder>"));
		return;
	}

	pixbuf = evolution_shell_client_get_pixbuf_for_type (priv->shell_client, folder->type, TRUE);
	bonobo_ui_toolbar_icon_set_pixbuf (BONOBO_UI_TOOLBAR_ICON (priv->icon), pixbuf);
	gdk_pixbuf_unref (pixbuf);

	folder_lname = e_utf8_to_gtk_string (w, folder->displayName);
	storage_lname = NULL;
	p = strchr (folder->evolutionUri, '/');
	if (p) {
		p = strchr (p + 1, '/');
		if (p) {
			GNOME_Evolution_Folder *storage_folder;
			char *storage_uri;

			storage_uri = g_strndup (folder->evolutionUri,
						 p - folder->evolutionUri);
			storage_folder = get_folder_for_uri (folder_selector_button, storage_uri);
			storage_lname = e_utf8_to_gtk_string (w, storage_folder->displayName);
			CORBA_free (storage_folder);
			g_free (storage_uri);
		}
	}

	if (storage_lname) {
		label_text = g_strdup_printf (_("\"%s\" in \"%s\""), folder_lname,
					      storage_lname);
		g_free (storage_lname);
	} else
		label_text = g_strdup_printf ("\"%s\"", folder_lname);

	gtk_label_set_text (GTK_LABEL (priv->label), label_text);
	g_free (label_text);
	g_free (folder_lname);
}

static void
clicked (GtkButton *button)
{
	EvolutionFolderSelectorButton *folder_selector_button;
	EvolutionFolderSelectorButtonPrivate *priv;
	GNOME_Evolution_Folder *return_folder;
	GtkWindow *parent_window;
	char *initial_uri;

	parent_window = (GtkWindow *)
		gtk_widget_get_ancestor (GTK_WIDGET (button),
					 GTK_TYPE_WINDOW);

	gtk_widget_set_sensitive (GTK_WIDGET (parent_window), FALSE);
	gtk_object_ref (GTK_OBJECT (parent_window));

	folder_selector_button = EVOLUTION_FOLDER_SELECTOR_BUTTON (button);
	priv = folder_selector_button->priv;

	if (priv->selected_folder)
		initial_uri = priv->selected_folder->evolutionUri;
	else
		initial_uri = "";

	gtk_signal_emit (GTK_OBJECT (folder_selector_button),
			 signals[POPPED_UP]);

	evolution_shell_client_user_select_folder (priv->shell_client,
						   parent_window,
						   priv->title,
						   initial_uri,
						   (const char **)priv->possible_types,
						   &return_folder);

	/* If the parent gets destroyed despite our best efforts (eg,
	 * because its own parent got destroyed), then the folder
	 * selector button will have been destroyed too and we need
	 * to just bail out here.
	 */
	if (GTK_OBJECT_DESTROYED (parent_window)) {
		gtk_object_unref (GTK_OBJECT (parent_window));
		return;
	}

	gtk_widget_set_sensitive (GTK_WIDGET (parent_window), TRUE);
	gtk_object_unref (GTK_OBJECT (parent_window));

	if (!return_folder) {
		gtk_signal_emit (GTK_OBJECT (folder_selector_button),
			 	signals[CANCELED]);
		return;
	}

	set_folder (folder_selector_button, return_folder);

	gtk_signal_emit (GTK_OBJECT (folder_selector_button),
			 signals[SELECTED], return_folder);
}


/* GtkObject methods.  */

static void
destroy (GtkObject *object)
{
	EvolutionFolderSelectorButton *folder_selector_button;
	EvolutionFolderSelectorButtonPrivate *priv;
	int i;

	folder_selector_button = EVOLUTION_FOLDER_SELECTOR_BUTTON (object);
	priv = folder_selector_button->priv;

	bonobo_object_unref (BONOBO_OBJECT (priv->shell_client));
	g_free (priv->title);
	for (i = 0; priv->possible_types[i]; i++)
		g_free (priv->possible_types[i]);
	g_free (priv->possible_types);

	if (priv->selected_folder)
		CORBA_free (priv->selected_folder);

	g_free (priv);

	(* GTK_OBJECT_CLASS (parent_class)->destroy) (object);
}


static void
class_init (EvolutionFolderSelectorButtonClass *klass)
{
	GtkObjectClass *object_class;
	GtkButtonClass *button_class;

	parent_class = gtk_type_class (PARENT_TYPE);

	object_class = GTK_OBJECT_CLASS (klass);
	button_class = GTK_BUTTON_CLASS (klass);

	button_class->clicked = clicked;
	object_class->destroy = destroy;

	signals[POPPED_UP] = gtk_signal_new ("popped_up",
					    GTK_RUN_FIRST,
					    object_class->type,
					    GTK_SIGNAL_OFFSET (EvolutionFolderSelectorButtonClass, popped_up),
					    gtk_marshal_NONE__NONE,
					    GTK_TYPE_NONE, 0);
	signals[SELECTED] = gtk_signal_new ("selected",
					    GTK_RUN_FIRST,
					    object_class->type,
					    GTK_SIGNAL_OFFSET (EvolutionFolderSelectorButtonClass, selected),
					    gtk_marshal_NONE__POINTER,
					    GTK_TYPE_NONE, 1,
					    GTK_TYPE_POINTER);
	signals[CANCELED] = gtk_signal_new ("canceled",
					    GTK_RUN_FIRST,
					    object_class->type,
					    GTK_SIGNAL_OFFSET (EvolutionFolderSelectorButtonClass, canceled),
					    gtk_marshal_NONE__NONE,
					    GTK_TYPE_NONE, 0);
	gtk_object_class_add_signals (object_class, signals, LAST_SIGNAL);
}

static void
init (EvolutionFolderSelectorButton *folder_selector_button)
{
	EvolutionFolderSelectorButtonPrivate *priv;
	GtkWidget *box;

	priv = g_new0 (EvolutionFolderSelectorButtonPrivate, 1);

	priv->icon = bonobo_ui_toolbar_icon_new ();
	priv->label = gtk_label_new ("");
	gtk_label_set_justify (GTK_LABEL (priv->label), GTK_JUSTIFY_LEFT);
	gtk_misc_set_alignment (GTK_MISC (priv->label), 0.0, 0.0);
	box = gtk_hbox_new (FALSE, 4);
	gtk_box_pack_start (GTK_BOX (box), priv->icon, FALSE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (box), priv->label, TRUE, TRUE, 0);
	gtk_widget_show_all (GTK_WIDGET (box));
	gtk_container_add (GTK_CONTAINER (folder_selector_button), box);

	folder_selector_button->priv = priv;
}



/**
 * evolution_folder_selector_button_construct:
 * @folder_selector_button: 
 * @shell_client: the shell client that will be used for folder selection
 * @title: the title to use for the selection dialog
 * @initial_uri: the URI (evolution: or physical) of the
 * initially-selected folder
 * @possible_types: a %NULL-terminated array of selectable types.
 * 
 * Construct @folder_selector_button.
 **/
void
evolution_folder_selector_button_construct (EvolutionFolderSelectorButton *folder_selector_button,
					    EvolutionShellClient *shell_client,
					    const char *title,
					    const char *initial_uri,
					    const char *possible_types[])
{
	EvolutionFolderSelectorButtonPrivate *priv;
	GNOME_Evolution_Folder *folder;
	int count;

	g_return_if_fail (EVOLUTION_IS_FOLDER_SELECTOR_BUTTON (folder_selector_button));
	g_return_if_fail (EVOLUTION_IS_SHELL_CLIENT (shell_client));
	g_return_if_fail (possible_types != NULL);

	priv = folder_selector_button->priv;

	priv->shell_client = shell_client;
	bonobo_object_ref (BONOBO_OBJECT (shell_client));
	priv->corba_storage_registry = evolution_shell_client_get_storage_registry_interface (shell_client);

	priv->title = g_strdup (title);

	folder = get_folder_for_uri (folder_selector_button, initial_uri);
	set_folder (folder_selector_button, folder);

	for (count = 0; possible_types[count]; count++)
		;
	priv->possible_types = g_new (char *, count + 1);
	for (count = 0; possible_types[count]; count++)
		priv->possible_types[count] = g_strdup (possible_types[count]);
	priv->possible_types[count] = NULL;
}

/**
 * evolution_folder_selector_button_new:
 * @shell_client: the shell client that will be used for folder selection
 * @title: the title to use for the selection dialog
 * @initial_uri: the URI (evolution: or physical) of the
 * initially-selected folder
 * @possible_types: a %NULL-terminated array of selectable types.
 * 
 * Return value: a new folder selector button.
 **/
GtkWidget *
evolution_folder_selector_button_new (EvolutionShellClient *shell_client,
				      const char *title,
				      const char *initial_uri,
				      const char *possible_types[])
{
	EvolutionFolderSelectorButton *folder_selector_button;

	folder_selector_button = gtk_type_new (evolution_folder_selector_button_get_type ());

	evolution_folder_selector_button_construct (folder_selector_button,
						    shell_client,
						    title,
						    initial_uri,
						    possible_types);
	return (GtkWidget *)folder_selector_button;
}

/**
 * evolution_folder_selector_button_set_uri:
 * @folder_selector_button:
 * @uri: the URI (evolution: or physical) to select, or %NULL
 *
 * Attempts to make @folder_selector_button select @uri. If @uri
 * doesn't point to a folder, or points to a folder of an incorrect
 * type for this button, then the selected URI will be unchanged.
 *
 * If @uri is %NULL, the button will be returned to an unselected
 * state.
 *
 * Return value: whether or not the URI was successfully set.
 **/
gboolean
evolution_folder_selector_button_set_uri (EvolutionFolderSelectorButton *folder_selector_button,
					  const char *uri)
{
	EvolutionFolderSelectorButtonPrivate *priv;
	GNOME_Evolution_Folder *folder;
	char *slash;
	int i;

	g_return_val_if_fail (EVOLUTION_IS_FOLDER_SELECTOR_BUTTON (folder_selector_button), FALSE);
	g_return_val_if_fail (uri != NULL, FALSE);

	priv = folder_selector_button->priv;

	if (!uri) {
		set_folder (folder_selector_button, NULL);
		return TRUE;
	}

	folder = get_folder_for_uri (folder_selector_button, uri);
	if (!folder)
		return FALSE;

	for (i = 0; priv->possible_types[i]; i++) {
		if (!strcmp (folder->type, priv->possible_types[i])) {
			set_folder (folder_selector_button, folder);
			return TRUE;
		}
		slash = strchr (priv->possible_types[i], '/');
		if (slash && slash[1] == '*' &&
		    !strncmp (folder->type, priv->possible_types[i],
			      slash - priv->possible_types[i])) {
			set_folder (folder_selector_button, folder);
			return TRUE;
		}
	}

	CORBA_free (folder);
	return FALSE;
}

/**
 * evolution_folder_selector_button_get_folder:
 * @folder_selector_button:
 *
 * Return value: the currently-selected folder, or %NULL
 **/
GNOME_Evolution_Folder *
evolution_folder_selector_button_get_folder (EvolutionFolderSelectorButton *folder_selector_button)
{
	g_return_val_if_fail (EVOLUTION_IS_FOLDER_SELECTOR_BUTTON (folder_selector_button), NULL);

	return folder_selector_button->priv->selected_folder;
}


E_MAKE_TYPE (evolution_folder_selector_button, "EvolutionFolderSelectorButton", EvolutionFolderSelectorButton, class_init, init, PARENT_TYPE)
