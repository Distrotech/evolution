/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-shell.c
 *
 * Copyright (C) 2000, 2001 Ximian, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
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

#include <glib.h>
#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-i18n.h>
#include <libgnome/gnome-util.h>
#include <glade/glade-xml.h>

#include <gal/util/e-util.h>
#include <gal/widgets/e-unicode.h>
#include <gal/widgets/e-gui-utils.h>

#include <gal/widgets/e-scroll-frame.h>

#include "e-storage-set.h"
#include "e-storage-set-view.h"

#include "e-shell-folder-creation-dialog.h"


#define GLADE_FILE_NAME  EVOLUTION_GLADEDIR "/e-shell-folder-creation-dialog.glade"


/* Data for the callbacks.  */
struct _DialogData {
	GtkWidget *dialog;
	EShell *shell;

	GtkWidget *folder_name_entry;
	GtkWidget *storage_set_view;
	GtkWidget *folder_type_option_menu;

	GList *folder_types;

	char *folder_path;

	EShellFolderCreationDialogCallback result_callback;
	void *result_callback_data;
};
typedef struct _DialogData DialogData;

static void
dialog_data_destroy (DialogData *dialog_data)
{
	e_free_string_list (dialog_data->folder_types);
	g_free (dialog_data->folder_path);

	g_free (dialog_data);
}


/* Callback for the asynchronous folder creation function.  */

static void
async_create_cb (EStorageSet *storage_set,
		 EStorageResult result,
		 void *data)
{
	DialogData *dialog_data;

	dialog_data = (DialogData *) data;

	if (result == E_STORAGE_OK) {
		/* Success! Tell the callback of this, then return */
		if (dialog_data->result_callback != NULL)
			(* dialog_data->result_callback) (dialog_data->shell,
							  E_SHELL_FOLDER_CREATION_DIALOG_RESULT_SUCCESS,
							  dialog_data->folder_path,
							  dialog_data->result_callback_data);
		gtk_widget_destroy (dialog_data->dialog);
		return;
	} else if (result == E_STORAGE_EXISTS) {
		e_storage_set_view_set_current_folder (E_STORAGE_SET_VIEW (dialog_data->storage_set_view),
						       dialog_data->folder_path);
	}

	/* Tell the callback something failed, then popup a dialog
           explaining how it failed */
	if (dialog_data->result_callback != NULL)
		(* dialog_data->result_callback) (dialog_data->shell,
						  E_SHELL_FOLDER_CREATION_DIALOG_RESULT_FAIL,
						  dialog_data->folder_path,
						  dialog_data->result_callback_data);

	e_notice (GTK_WINDOW (dialog_data->dialog), GNOME_MESSAGE_BOX_ERROR,
		  _("Cannot create the specified folder:\n%s"),
		  e_storage_result_to_string (result));
}


/* Sanity check for the user-specified folder name.  */
/* FIXME in the future we would like not to have the `G_DIR_SEPARATOR' limitation.  */
static gboolean
entry_name_is_valid (GtkEntry *entry, char **reason)
{
	const char *name;
	
	name = gtk_entry_get_text (entry);
	
	if (name == NULL || *name == '\0') {
		*reason = _("No folder name specified.");
		return FALSE;
	}
	
	/* GtkEntry is broken - if you hit KP_ENTER you get a \r inserted... */
	if (strchr (name, '\r')) {
		*reason = _("Folder name cannot contain the Return character.");
		return FALSE;
	}
	
	if (strchr (name, G_DIR_SEPARATOR) != NULL) {
		*reason = _("Folder cannot contain the directory separator.");
		return FALSE;
	}
	
	if (strcmp (name, ".") == 0 || strcmp (name, "..") == 0) {
		*reason = _("'.' and '..' are reserved folder names.");
		return FALSE;
	}
	
	return TRUE;
}


/* Dialog signal callbacks.  */

static void
dialog_clicked_cb (GnomeDialog *dialog,
		   int button_number,
		   void *data)
{
	DialogData *dialog_data;
	EStorageSet *storage_set;
	GtkWidget *folder_type_menu_item;
	const char *folder_type;
	const char *parent_path;
	char *folder_name;
	char *reason;
	char *path;

	dialog_data = (DialogData *) data;

	if (button_number != 0) {
		if (dialog_data->result_callback != NULL)
			(* dialog_data->result_callback) (dialog_data->shell,
							  E_SHELL_FOLDER_CREATION_DIALOG_RESULT_CANCEL,
							  NULL,
							  dialog_data->result_callback_data);
		gtk_widget_destroy (GTK_WIDGET (dialog));
		return;
	}

	if (!entry_name_is_valid (GTK_ENTRY (dialog_data->folder_name_entry), &reason)) {
		e_notice (GTK_WINDOW (dialog), GNOME_MESSAGE_BOX_ERROR,
			  _("The specified folder name is not valid: %s"), reason);
		return;
	}

	parent_path = e_storage_set_view_get_current_folder
		(E_STORAGE_SET_VIEW (dialog_data->storage_set_view));
	if (parent_path == NULL) {
		if (dialog_data->result_callback != NULL)
			(* dialog_data->result_callback) (dialog_data->shell,
							  E_SHELL_FOLDER_CREATION_DIALOG_RESULT_CANCEL,
							  NULL,
							  dialog_data->result_callback_data);
		gtk_widget_destroy (GTK_WIDGET (dialog));
		return;
	}

	folder_name = e_utf8_gtk_entry_get_text (GTK_ENTRY (dialog_data->folder_name_entry));
	path = g_concat_dir_and_file (parent_path, folder_name);
	g_free (folder_name);

	storage_set = e_shell_get_storage_set (dialog_data->shell);

	folder_type_menu_item = GTK_OPTION_MENU (dialog_data->folder_type_option_menu)->menu_item;
	folder_type = gtk_object_get_data (GTK_OBJECT (folder_type_menu_item), "folder_type");

	if (folder_type == NULL) {
		g_warning ("Cannot get folder type for selected GtkOptionMenu item.");
		return;
	}

	g_free (dialog_data->folder_path);
	dialog_data->folder_path = path;

	e_storage_set_async_create_folder (storage_set,
					   path,
					   folder_type,
					   "", /* description */
					   async_create_cb, dialog_data);
}

static void
dialog_destroy_cb (GtkObject *object,
		   void *data)
{
	DialogData *dialog_data;

	dialog_data = (DialogData *) data;
	dialog_data_destroy (dialog_data);
}

static void
folder_name_entry_changed_cb (GtkEditable *editable,
			      void *data)
{
	DialogData *dialog_data;
	const char *parent_path;

	dialog_data = (DialogData *) data;

	parent_path = e_storage_set_view_get_current_folder (E_STORAGE_SET_VIEW (dialog_data->storage_set_view));

	if (parent_path != NULL
	    && GTK_ENTRY (dialog_data->folder_name_entry)->text_length > 0)
		gnome_dialog_set_sensitive (GNOME_DIALOG (dialog_data->dialog), 0, TRUE);
	else
		gnome_dialog_set_sensitive (GNOME_DIALOG (dialog_data->dialog), 0, FALSE);
}

static void
storage_set_view_folder_selected_cb (EStorageSetView *storage_set_view,
				     const char *path,
				     void *data)
{
	DialogData *dialog_data;

	dialog_data = (DialogData *) data;

	if (GTK_ENTRY (dialog_data->folder_name_entry)->text_length > 0)
		gnome_dialog_set_sensitive (GNOME_DIALOG (dialog_data->dialog), 0, TRUE);
}

static void
storage_set_view_storage_selected_cb (EStorageSetView *storage_set_view,
				      const char *name,
				      void *data)
{
	DialogData *dialog_data;

	dialog_data = (DialogData *) data;

	if (GTK_ENTRY (dialog_data->folder_name_entry)->text_length > 0)
		gnome_dialog_set_sensitive (GNOME_DIALOG (dialog_data->dialog), 0, TRUE);
}


/* Shell signal callbacks.  */

static void
shell_destroy_cb (GtkObject *object,
		  void *data)
{
	GnomeDialog *dialog;

	dialog = GNOME_DIALOG (data);
	gtk_widget_destroy (GTK_WIDGET (dialog));
}


/* Dialog setup.  */

static void
setup_dialog (GtkWidget *dialog,
	      GladeXML *gui,
	      EShell *shell,
	      GtkWindow *parent_window)
{
	if (parent_window != NULL)
		gtk_window_set_transient_for (GTK_WINDOW (dialog), parent_window);

	gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
	gtk_window_set_title (GTK_WINDOW (dialog), _("Evolution - Create new folder"));

	gnome_dialog_set_default   (GNOME_DIALOG (dialog), 0);
	gnome_dialog_set_sensitive (GNOME_DIALOG (dialog), 0, FALSE);

	gtk_widget_show (dialog);
}

static void
setup_folder_name_entry (GtkWidget *dialog,
			 GladeXML *gui,
			 EShell *shell)
{
	GtkWidget *folder_name_entry;

	folder_name_entry = glade_xml_get_widget (gui, "folder_name_entry");

	gnome_dialog_editable_enters (GNOME_DIALOG (dialog), GTK_EDITABLE (folder_name_entry));
}

static GtkWidget *
add_storage_set_view (GtkWidget *dialog,
		      GladeXML *gui,
		      EShell *shell,
		      const char *default_parent_folder)
{
	EStorageSet *storage_set;
	GtkWidget *storage_set_view;
	GtkWidget *scroll_frame;
	GtkWidget *vbox;

	storage_set = e_shell_get_storage_set (shell);
	storage_set_view = e_storage_set_new_view (storage_set, NULL/*XXX*/);

	e_storage_set_view_set_allow_dnd (E_STORAGE_SET_VIEW (storage_set_view), FALSE);

	GTK_WIDGET_SET_FLAGS (storage_set_view, GTK_CAN_FOCUS);

	if (default_parent_folder != NULL)
		e_storage_set_view_set_current_folder (E_STORAGE_SET_VIEW (storage_set_view),
						       default_parent_folder);

	vbox = glade_xml_get_widget (gui, "main_vbox");

	scroll_frame = e_scroll_frame_new (NULL, NULL);
	e_scroll_frame_set_policy (E_SCROLL_FRAME (scroll_frame), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	e_scroll_frame_set_shadow_type (E_SCROLL_FRAME (scroll_frame), GTK_SHADOW_IN);
	gtk_box_pack_start (GTK_BOX (vbox), scroll_frame, TRUE, TRUE, 0);

	gtk_container_add (GTK_CONTAINER (scroll_frame), storage_set_view);

	gtk_widget_show (scroll_frame);
	gtk_widget_show (storage_set_view);

	return storage_set_view;
}

struct _TypeWithDisplayName {
	const char *type;
	const char *display_name;
};
typedef struct _TypeWithDisplayName TypeWithDisplayName;

static int
type_with_display_name_compare_func (const void *a, const void *b)
{
	const TypeWithDisplayName *val_a, *val_b;

	val_a = (const TypeWithDisplayName *) a;
	val_b = (const TypeWithDisplayName *) b;

	return g_strcasecmp (val_a->display_name, val_b->display_name);
}

static GList *
add_folder_types (GtkWidget *dialog,
		  GladeXML *gui,
		  EShell *shell,
		  const char *default_type)
{
	EFolderTypeRegistry *folder_type_registry;
	GtkWidget *folder_type_option_menu;
	GtkWidget *menu;
	GList *folder_types;
	GList *types_with_display_names;
	GList *p;
	int default_item;
	int i;

	folder_type_option_menu = glade_xml_get_widget (gui, "folder_type_option_menu");

	/* KLUDGE.  So, GtkOptionMenu is badly broken.  It calculates its size
           in `gtk_option_menu_set_menu()' instead of using `size_request()' as
           any sane widget would do.  So, in order to avoid the "narrow
           GtkOptionMenu" bug, we have to destroy the existing associated menu
           and create a new one.  Life sucks.  */

	menu = gtk_option_menu_get_menu (GTK_OPTION_MENU (folder_type_option_menu));
	g_assert (menu != NULL);
	gtk_widget_destroy (menu);

	menu = gtk_menu_new ();

	folder_type_registry = e_shell_get_folder_type_registry (shell);
	g_assert (folder_type_registry != NULL);

	folder_types = e_folder_type_registry_get_type_names (folder_type_registry);
	if (folder_types == NULL)
		return NULL;		/* Uh? */

	types_with_display_names = NULL;
	for (p = folder_types; p != NULL; p = p->next) {
		TypeWithDisplayName *new;

		new = g_new (TypeWithDisplayName, 1);
		new->type = p->data;
		new->display_name = e_folder_type_registry_get_display_name_for_type (folder_type_registry, new->type);

		types_with_display_names = g_list_prepend (types_with_display_names, new);
	}

	types_with_display_names = g_list_sort (types_with_display_names, type_with_display_name_compare_func);

	/* FIXME: Add icon (I don't feel like writing an alpha-capable thingie again).  */

	default_item = 0;
	i = 0;
	for (p = types_with_display_names; p != NULL; p = p->next) {
		const TypeWithDisplayName *type;
		GtkWidget *menu_item;

		type = (const TypeWithDisplayName *) p->data;

		if (! e_folder_type_registry_type_is_user_creatable (folder_type_registry, type->type))
			continue;

		menu_item = gtk_menu_item_new_with_label (type->display_name);
		gtk_widget_show (menu_item);
		gtk_menu_append (GTK_MENU (menu), menu_item);

		gtk_object_set_data_full (GTK_OBJECT (menu_item), "folder_type", g_strdup (type->type), g_free);

		if (strcmp (type->type, default_type ? default_type : "mail") == 0)
			default_item = i;

		i ++;
	}

	for (p = types_with_display_names; p != NULL; p = p->next)
		g_free (p->data);
	g_list_free (types_with_display_names);

	gtk_option_menu_set_menu (GTK_OPTION_MENU (folder_type_option_menu), menu);
	gtk_widget_show (menu);

	gtk_option_menu_set_history (GTK_OPTION_MENU (folder_type_option_menu), default_item);
	gtk_widget_queue_resize (folder_type_option_menu);

	return folder_types;
}

static const char *
get_type_from_parent_path (EShell *shell,
			   const char *path)
{
	EFolder *folder;
	const char *folder_type;
	EStorageSet *set;

	set = e_shell_get_storage_set (shell);
	folder = e_storage_set_get_folder (set, path);
	if (folder == NULL) {
		return NULL;
	}

	folder_type = e_folder_get_type_string (folder);
	if (folder_type == NULL) {
		return NULL;
	} else {
		return folder_type;
	}
}


/* FIXME: Currently this is modal.  I think it's OK, but if people think it is
   not, we should change it to non-modal and make sure only one of these is
   open at once.  Currently it relies on modality for this.  */
void
e_shell_show_folder_creation_dialog (EShell *shell,
				     GtkWindow *parent_window,
				     const char *default_parent_folder,
				     const char *default_type,
				     EShellFolderCreationDialogCallback result_callback,
				     void *result_callback_data)
{
	GladeXML *gui;
	GtkWidget *dialog;
	GtkWidget *storage_set_view;
	GList *folder_types;
	DialogData *dialog_data;

	g_return_if_fail (shell != NULL);
	g_return_if_fail (E_IS_SHELL (shell));

	gui = glade_xml_new (GLADE_FILE_NAME, NULL);
	if (gui == NULL) {
		g_warning ("Cannot load Glade description file for the folder creation dialog -- %s",
			   GLADE_FILE_NAME);
		return;
	}

	dialog = glade_xml_get_widget (gui, "create_folder_dialog");

	setup_dialog (dialog, gui, shell, parent_window);
	setup_folder_name_entry (dialog, gui, shell);

	storage_set_view = add_storage_set_view (dialog, gui, shell, default_parent_folder);
	if (default_type == NULL) {
		char *dt;

		dt = get_type_from_parent_path (shell, default_parent_folder);
		folder_types = add_folder_types (dialog, gui, shell, dt);
	} else {
		folder_types = add_folder_types (dialog, gui, shell, default_type);
	}

	dialog_data = g_new (DialogData, 1);
	dialog_data->dialog                  = dialog;
	dialog_data->shell                   = shell;
	dialog_data->folder_name_entry       = glade_xml_get_widget (gui, "folder_name_entry");
	dialog_data->storage_set_view        = storage_set_view;
	dialog_data->folder_type_option_menu = glade_xml_get_widget (gui, "folder_type_option_menu");
	dialog_data->folder_types            = folder_types;
	dialog_data->folder_path             = NULL;
	dialog_data->result_callback         = result_callback;
	dialog_data->result_callback_data    = result_callback_data;

	gtk_signal_connect (GTK_OBJECT (dialog), "clicked",
			    GTK_SIGNAL_FUNC (dialog_clicked_cb), dialog_data);
	gtk_signal_connect (GTK_OBJECT (dialog), "destroy",
			    GTK_SIGNAL_FUNC (dialog_destroy_cb), dialog_data);

	gtk_signal_connect (GTK_OBJECT (dialog_data->folder_name_entry), "changed",
			    GTK_SIGNAL_FUNC (folder_name_entry_changed_cb), dialog_data);

	gtk_signal_connect (GTK_OBJECT (dialog_data->storage_set_view), "folder_selected",
			    storage_set_view_folder_selected_cb, dialog_data);
	gtk_signal_connect (GTK_OBJECT (dialog_data->storage_set_view), "storage_selected",
			    storage_set_view_storage_selected_cb, dialog_data);

	gtk_signal_connect_while_alive (GTK_OBJECT (shell), "destroy",
					GTK_SIGNAL_FUNC (shell_destroy_cb), dialog_data,
					GTK_OBJECT (dialog));

	gtk_object_unref (GTK_OBJECT (gui));
}
