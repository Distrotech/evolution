/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-shell-folder-selection-dialog.c
 *
 * Copyright (C) 2000, 2001 Ximian, Inc.
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
 * Author: Ettore Perazzoli
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <libgnomeui/gnome-stock.h>
#include <libgnomeui/gnome-dialog.h>
#include <libgnome/gnome-i18n.h>

#include <gal/util/e-util.h>
#include <gal/widgets/e-scroll-frame.h>
#include <gal/widgets/e-gui-utils.h>

#include "e-shell-constants.h"
#include "e-storage-set-view.h"
#include "e-storage-set.h"

#include "e-shell-folder-creation-dialog.h"

#include "e-shell-folder-selection-dialog.h"


#define PARENT_TYPE (gnome_dialog_get_type ())
static GnomeDialogClass *parent_class = NULL;

struct _EShellFolderSelectionDialogPrivate {
	EShell *shell;
	GList *allowed_types;
	EStorageSet *storage_set;
	GtkWidget *storage_set_view;
	char *default_type;

	gboolean allow_creation;
};

enum {
	FOLDER_SELECTED,
	CANCELLED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };


static gboolean
check_folder_type (EShellFolderSelectionDialog *folder_selection_dialog)
{
	EShellFolderSelectionDialogPrivate *priv;
	const char *selected_path;
	EFolder *folder;
	const char *folder_type;
	GList *p;

	priv = folder_selection_dialog->priv;
	if (priv->allowed_types == NULL)
		return TRUE;

	selected_path = e_shell_folder_selection_dialog_get_selected_path (folder_selection_dialog);
	if (selected_path == NULL)
		return FALSE;

	folder = e_storage_set_get_folder (priv->storage_set, selected_path);
	if (folder == NULL)
		return FALSE;

	folder_type = e_folder_get_type_string (folder);

	for (p = priv->allowed_types; p != NULL; p = p->next) {
		const char *type;

		type = (const char *) p->data;
		if (strcasecmp (folder_type, type) == 0)
			return TRUE;
	}

	e_notice (GTK_WINDOW (folder_selection_dialog), GNOME_MESSAGE_BOX_ERROR,
		  _("The type of the selected folder is not valid for\n"
		    "the requested operation."));

	return FALSE;
}


/* Folder creation dialog callback.  */

static void
folder_creation_dialog_result_cb (EShell *shell,
				  EShellFolderCreationDialogResult result,
				  const char *path,
				  void *data)
{
	EShellFolderSelectionDialog *dialog;
	EShellFolderSelectionDialogPrivate *priv;

	dialog = E_SHELL_FOLDER_SELECTION_DIALOG (data);
	priv = dialog->priv;

	if (priv == NULL) {
		g_warning ("dialog->priv is NULL, and should not be");
		return;
	}

	if (result == E_SHELL_FOLDER_CREATION_DIALOG_RESULT_SUCCESS)
		e_storage_set_view_set_current_folder (E_STORAGE_SET_VIEW (priv->storage_set_view),
						       path);
}


/* GtkObject methods.  */

/* Saves the expanded state of the tree to a common filename */
static void
save_expanded_state (EShellFolderSelectionDialog *folder_selection_dialog)
{
	EShellFolderSelectionDialogPrivate *priv;
	char *filename;

	priv = folder_selection_dialog->priv;

	filename = g_strdup_printf ("%s/config/storage-set-view-expanded:folder-selection-dialog",
				    e_shell_get_local_directory (priv->shell));
	e_tree_save_expanded_state (E_TREE (priv->storage_set_view), filename);
	g_free (filename);
}

static void
impl_destroy (GtkObject *object)
{
	EShellFolderSelectionDialog *folder_selection_dialog;
	EShellFolderSelectionDialogPrivate *priv;

	folder_selection_dialog = E_SHELL_FOLDER_SELECTION_DIALOG (object);
	priv = folder_selection_dialog->priv;

	save_expanded_state (folder_selection_dialog);

	if (priv->storage_set != NULL)
		gtk_object_unref (GTK_OBJECT (priv->storage_set));

	e_free_string_list (priv->allowed_types);

	g_free (priv->default_type);
	g_free (priv);

	(* GTK_OBJECT_CLASS (parent_class)->destroy) (object);
}


/* ETable callback */
static void
dbl_click_cb (EStorageSetView *essv,
	      int row,
	      ETreePath path,
	      int col,
	      GdkEvent *event,
	      EShellFolderSelectionDialog *folder_selection_dialog)
{
	EShellFolderSelectionDialogPrivate *priv;

	g_return_if_fail (folder_selection_dialog != NULL);

	priv = folder_selection_dialog->priv;
	if (check_folder_type (folder_selection_dialog)) {
		gtk_signal_emit (GTK_OBJECT (folder_selection_dialog),
				 signals[FOLDER_SELECTED],
				 e_shell_folder_selection_dialog_get_selected_path (folder_selection_dialog));
	}

	gnome_dialog_close (GNOME_DIALOG (folder_selection_dialog));
}


/* GnomeDialog methods.  */

static void
impl_clicked (GnomeDialog *dialog,
	      int button_number)
{
	EShellFolderSelectionDialog *folder_selection_dialog;
	EShellFolderSelectionDialogPrivate *priv;
	EStorageSetView *storage_set_view;
	const char *default_parent_folder;

	folder_selection_dialog = E_SHELL_FOLDER_SELECTION_DIALOG (dialog);
	priv = folder_selection_dialog->priv;

	switch (button_number) {
	case 0:			/* OK */
		if (check_folder_type (folder_selection_dialog)) {
			gtk_signal_emit (GTK_OBJECT (folder_selection_dialog), signals[FOLDER_SELECTED],
					 e_shell_folder_selection_dialog_get_selected_path (folder_selection_dialog));
			gnome_dialog_close (GNOME_DIALOG (dialog));
		}
		break;
	case 1:			/* Cancel */
		gtk_signal_emit (GTK_OBJECT (folder_selection_dialog), signals[CANCELLED]);
		gnome_dialog_close (GNOME_DIALOG (dialog));
		break;
	case 2:			/* Add */
		storage_set_view = E_STORAGE_SET_VIEW (priv->storage_set_view);
		default_parent_folder = e_storage_set_view_get_current_folder (storage_set_view);

		e_shell_show_folder_creation_dialog (priv->shell, GTK_WINDOW (dialog),
						     default_parent_folder,
						     priv->default_type,
						     folder_creation_dialog_result_cb,
						     dialog);

		break;
	}
}


/* GTK+ type initialization.  */

static void
class_init (EShellFolderSelectionDialogClass *klass)
{
	GtkObjectClass *object_class;
	GnomeDialogClass *dialog_class;

	parent_class = gtk_type_class (PARENT_TYPE);
	object_class = GTK_OBJECT_CLASS (klass);
	dialog_class = GNOME_DIALOG_CLASS (klass);

	object_class->destroy = impl_destroy;

	dialog_class->clicked = impl_clicked;

	signals[FOLDER_SELECTED]
		= gtk_signal_new ("folder_selected",
				  GTK_RUN_LAST,
				  object_class->type,
				  GTK_SIGNAL_OFFSET (EShellFolderSelectionDialogClass, folder_selected),
				  gtk_marshal_NONE__POINTER,
				  GTK_TYPE_NONE, 1,
				  GTK_TYPE_STRING);

	signals[CANCELLED]
		= gtk_signal_new ("cancelled",
				  GTK_RUN_LAST,
				  object_class->type,
				  GTK_SIGNAL_OFFSET (EShellFolderSelectionDialogClass, cancelled),
				  gtk_marshal_NONE__NONE,
				  GTK_TYPE_NONE, 0);

	gtk_object_class_add_signals (object_class, signals, LAST_SIGNAL);
}

static void
init (EShellFolderSelectionDialog *shell_folder_selection_dialog)
{
	EShellFolderSelectionDialogPrivate *priv;

	priv = g_new (EShellFolderSelectionDialogPrivate, 1);
	priv->shell            = NULL;
	priv->storage_set      = NULL;
	priv->storage_set_view = NULL;
	priv->allowed_types    = NULL;
	priv->allow_creation   = TRUE;
	priv->default_type     = NULL;

	shell_folder_selection_dialog->priv = priv;
}


static void
set_default_folder (EShellFolderSelectionDialog *shell_folder_selection_dialog,
		    const char *default_uri)
{
	EShellFolderSelectionDialogPrivate *priv;
	char *default_path;

	g_assert (default_uri != NULL);

	priv = shell_folder_selection_dialog->priv;

	if (strncmp (default_uri, E_SHELL_URI_PREFIX, E_SHELL_URI_PREFIX_LEN) == 0) {
		/* `evolution:' URI.  */
		default_path = g_strdup (default_uri + E_SHELL_URI_PREFIX_LEN);
	} else {
		/* Physical URI.  */
		default_path = e_storage_set_get_path_for_physical_uri (priv->storage_set,
									default_uri);
	}

	e_storage_set_view_set_current_folder (E_STORAGE_SET_VIEW (priv->storage_set_view),
					       default_path);

	g_free (default_path);
}

static void
folder_selected_cb (EStorageSetView *storage_set_view,
		    const char *path,
		    void *data)
{
	GnomeDialog *dialog;

	dialog = GNOME_DIALOG (data);

	gnome_dialog_set_sensitive (dialog, 0, TRUE);
}

/**
 * e_shell_folder_selection_dialog_construct:
 * @folder_selection_dialog: A folder selection dialog widget
 * @shell: The this folder selection dialog is for
 * @title: Title of the window
 * @caption: A brief text to be put on top of the storage view
 * @default_uri: The URI of the folder to be selected by default
 * @allowed_types: List of the names of the allowed types
 * @default_type: The default type of folder that will be created if the
 * New folder button is pressed.
 * 
 * Construct @folder_selection_dialog.
 **/
void
e_shell_folder_selection_dialog_construct (EShellFolderSelectionDialog *folder_selection_dialog,
					   EShell *shell,
					   const char *title,
					   const char *caption,
					   const char *default_uri,
					   const char *allowed_types[],
					   const char *default_type)
{
	EShellFolderSelectionDialogPrivate *priv;
	GtkWidget *scroll_frame;
	GtkWidget *caption_label;
	int i;
	char *filename;

	g_return_if_fail (folder_selection_dialog != NULL);
	g_return_if_fail (E_IS_SHELL_FOLDER_SELECTION_DIALOG (folder_selection_dialog));
	g_return_if_fail (shell != NULL);
	g_return_if_fail (E_IS_SHELL (shell));

	priv = folder_selection_dialog->priv;

	if (default_type != NULL && *default_type != 0) {
		priv->default_type = g_strdup (default_type);
	} else {
		priv->default_type = NULL;
	}
	/* Basic dialog setup.  */

	gtk_window_set_policy (GTK_WINDOW (folder_selection_dialog), TRUE, TRUE, FALSE);
	gtk_window_set_default_size (GTK_WINDOW (folder_selection_dialog), 350, 300);
	gtk_window_set_modal (GTK_WINDOW (folder_selection_dialog), TRUE);
	gtk_window_set_title (GTK_WINDOW (folder_selection_dialog), title);

	gnome_dialog_append_buttons (GNOME_DIALOG (folder_selection_dialog),
				     GNOME_STOCK_BUTTON_OK,
				     GNOME_STOCK_BUTTON_CANCEL,
				     _("New..."),
				     NULL);
	gnome_dialog_set_default (GNOME_DIALOG (folder_selection_dialog), 0);
	gnome_dialog_set_sensitive (GNOME_DIALOG (folder_selection_dialog), 0, FALSE);

	/* Make sure we get destroyed if the shell gets destroyed.  */

	priv->shell = shell;
	gtk_signal_connect_object_while_alive (GTK_OBJECT (shell), "destroy",
					       GTK_SIGNAL_FUNC (gtk_widget_destroy),
					       GTK_OBJECT (folder_selection_dialog));

	/* Set up the label.  */

	if (caption != NULL) {
		caption_label = gtk_label_new (caption);
		gtk_widget_show (caption_label);

		gtk_box_pack_start (GTK_BOX (GNOME_DIALOG (folder_selection_dialog)->vbox),
				    caption_label, FALSE, TRUE, 2);
	}

	/* Set up the storage set and its view.  */

	priv->storage_set = e_shell_get_storage_set (shell);
	gtk_object_ref (GTK_OBJECT (priv->storage_set));

	priv->storage_set_view = e_storage_set_new_view (priv->storage_set, NULL /* No BonoboUIContainer */);
	e_storage_set_view_set_allow_dnd (E_STORAGE_SET_VIEW (priv->storage_set_view), FALSE);

	/* Load the expanded state for this StorageSetView */
	filename = g_strdup_printf ("%s/config/storage-set-view-expanded:folder-selection-dialog",
				    e_shell_get_local_directory (priv->shell));

	e_tree_load_expanded_state (E_TREE (priv->storage_set_view),
				    filename);

	g_free (filename);

	gtk_signal_connect (GTK_OBJECT (priv->storage_set_view), "double_click",
			    GTK_SIGNAL_FUNC (dbl_click_cb),
			    folder_selection_dialog);
	gtk_signal_connect (GTK_OBJECT (priv->storage_set_view), "folder_selected",
			    GTK_SIGNAL_FUNC (folder_selected_cb),
			    folder_selection_dialog);

	g_assert (priv->allowed_types == NULL);
	if (allowed_types != NULL) {
		for (i = 0; allowed_types[i] != NULL; i++)
			priv->allowed_types = g_list_prepend (priv->allowed_types,
							      g_strdup (allowed_types[i]));
	}

	if (default_uri != NULL)
		set_default_folder (folder_selection_dialog, default_uri);

	scroll_frame = e_scroll_frame_new (NULL, NULL);
	e_scroll_frame_set_shadow_type (E_SCROLL_FRAME (scroll_frame), GTK_SHADOW_IN);
	e_scroll_frame_set_policy (E_SCROLL_FRAME (scroll_frame),
				   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

	gtk_container_add (GTK_CONTAINER (scroll_frame), priv->storage_set_view);

	gtk_box_pack_start (GTK_BOX (GNOME_DIALOG (folder_selection_dialog)->vbox),
			    scroll_frame, TRUE, TRUE, 2);

	gtk_widget_show (priv->storage_set_view);
	gtk_widget_show (scroll_frame);

	GTK_WIDGET_SET_FLAGS (priv->storage_set_view, GTK_CAN_FOCUS);
	gtk_widget_grab_focus (priv->storage_set_view);
}

/**
 * e_shell_folder_selection_dialog_new:
 * @shell: The this folder selection dialog is for
 * @title: Title of the window
 * @caption: A brief text to be put on top of the storage view
 * @default_uri: The URI of the folder to be selected by default
 * @allowed_types: List of the names of the allowed types
 * 
 * Create a new folder selection dialog widget.  @default_uri can be either an
 * `evolution:' URI or a physical URI (all the non-`evolution:' URIs are
 * considered to be physical URIs).
 * 
 * Return value: 
 **/
GtkWidget *
e_shell_folder_selection_dialog_new (EShell *shell,
				     const char *title,
				     const char *caption,
				     const char *default_uri,
				     const char *allowed_types[],
				     const char *default_type)
{
	EShellFolderSelectionDialog *folder_selection_dialog;

	g_return_val_if_fail (shell != NULL, NULL);
	g_return_val_if_fail (E_IS_SHELL (shell), NULL);

	folder_selection_dialog = gtk_type_new (e_shell_folder_selection_dialog_get_type ());
	e_shell_folder_selection_dialog_construct (folder_selection_dialog, shell,
						   title, caption, default_uri, allowed_types, default_type);

	return GTK_WIDGET (folder_selection_dialog);
}


/**
 * e_shell_folder_selection_dialog_set_allow_creation:
 * @folder_selection_dialog: An EShellFolderSelectionDialog widget
 * @allow_creation: Boolean specifying whether the "New..." button should be
 * displayed
 * 
 * Specify whether @folder_selection_dialog should have a "New..." button to
 * create a new folder or not.
 **/
void
e_shell_folder_selection_dialog_set_allow_creation (EShellFolderSelectionDialog *folder_selection_dialog,
						    gboolean allow_creation)
{
	GList *button_list_item;
	GtkWidget *button;

	g_return_if_fail (folder_selection_dialog != NULL);
	g_return_if_fail (E_IS_SHELL_FOLDER_SELECTION_DIALOG (folder_selection_dialog));

	folder_selection_dialog->priv->allow_creation = !! allow_creation;

	button_list_item = g_list_nth (GNOME_DIALOG (folder_selection_dialog)->buttons, 2);
	g_assert (button_list_item != NULL);

	button = GTK_WIDGET (button_list_item->data);

	if (allow_creation)
		gtk_widget_show (button);
	else
		gtk_widget_hide (button);
}

/**
 * e_shell_folder_selection_dialog_get_allow_creation:
 * @folder_selection_dialog: An EShellFolderSelectionDialog widget
 * 
 * Get whether the "New..." button is displayed.
 * 
 * Return value: %TRUE if the "New..." button is displayed, %FALSE otherwise.
 **/
gboolean
e_shell_folder_selection_dialog_get_allow_creation (EShellFolderSelectionDialog *folder_selection_dialog)
{
	g_return_val_if_fail (folder_selection_dialog != NULL, FALSE);
	g_return_val_if_fail (E_IS_SHELL_FOLDER_SELECTION_DIALOG (folder_selection_dialog), FALSE);

	return folder_selection_dialog->priv->allow_creation;
}


const char *
e_shell_folder_selection_dialog_get_selected_path (EShellFolderSelectionDialog *folder_selection_dialog)
{
	EShellFolderSelectionDialogPrivate *priv;

	g_return_val_if_fail (folder_selection_dialog != NULL, NULL);
	g_return_val_if_fail (E_IS_SHELL_FOLDER_SELECTION_DIALOG (folder_selection_dialog), NULL);

	priv = folder_selection_dialog->priv;

	return e_storage_set_view_get_current_folder (E_STORAGE_SET_VIEW (priv->storage_set_view));
}


E_MAKE_TYPE (e_shell_folder_selection_dialog, "EShellFolderSelectionDialog", EShellFolderSelectionDialog,
	     class_init, init, PARENT_TYPE)
