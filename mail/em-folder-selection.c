/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* em-folder-selection.c - UI for selecting folders.
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
 *
 * Author: Ettore Perazzoli <ettore@ximian.com>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "em-folder-selection.h"

#include "mail-component.h"
#include "mail-tools.h"

#include "shell/e-folder-selection-dialog.h"


CamelFolder *
em_folder_selection_run_dialog (GtkWindow *parent_window,
				const char *title,
				const char *caption,
				CamelFolder *default_folder)
{
	EStorageSet *storage_set = mail_component_peek_storage_set (mail_component_peek ());
	char *default_path = NULL;
	CamelStore *default_store;
	GtkWidget *dialog;
	EFolder *selected_e_folder;
	CamelFolder *selected_camel_folder;
	int response;

	default_store = camel_folder_get_parent_store (default_folder);
	if (default_store != NULL) {
		EStorage *storage = mail_component_lookup_storage (mail_component_peek (), default_store);

		if (storage != NULL) {
			default_path = g_strconcat ("/",
						    e_storage_get_name (storage),
						    "/",
						    camel_folder_get_full_name (default_folder),
						    NULL);
		}
	}

	/* EPFIXME: Allowed types?  */
	dialog = e_folder_selection_dialog_new (storage_set, title, caption, default_path, NULL, FALSE);
	g_free(default_path);
	response = gtk_dialog_run (GTK_DIALOG (dialog));

	if (response != GTK_RESPONSE_OK) {
		gtk_widget_destroy (dialog);
		return NULL;
	}

	selected_e_folder = e_storage_set_get_folder (storage_set,
						      e_folder_selection_dialog_get_selected_path (E_FOLDER_SELECTION_DIALOG (dialog)));
	if (selected_e_folder == NULL) {
		gtk_widget_destroy (dialog);
		return NULL;
	}

	selected_camel_folder = mail_tool_uri_to_folder (e_folder_get_physical_uri (selected_e_folder), 0, NULL);
	gtk_widget_destroy (dialog);

	return selected_camel_folder;
}

/* FIXME: This isn't the way to do it, but then neither is the above, really ... */
char *
em_folder_selection_run_dialog_uri(GtkWindow *parent_window,
				   const char *title,
				   const char *caption,
				   const char *default_folder_uri)
{
	EStorageSet *storage_set = mail_component_peek_storage_set (mail_component_peek ());
	char *default_path;
	GtkWidget *dialog;
	EFolder *selected_e_folder;
	int response;

	default_path = e_storage_set_get_path_for_physical_uri(storage_set, default_folder_uri);
	dialog = e_folder_selection_dialog_new (storage_set, title, caption, default_path, NULL, FALSE);
	g_free(default_path);
	response = gtk_dialog_run (GTK_DIALOG (dialog));

	if (response != GTK_RESPONSE_OK) {
		gtk_widget_destroy (dialog);
		return NULL;
	}

	selected_e_folder = e_storage_set_get_folder (storage_set,
						      e_folder_selection_dialog_get_selected_path (E_FOLDER_SELECTION_DIALOG (dialog)));
	gtk_widget_destroy (dialog);
	if (selected_e_folder == NULL)
		return NULL;

	return g_strdup(e_folder_get_physical_uri(selected_e_folder));
}
