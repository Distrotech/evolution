/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-msg-composer-select-file.c
 *
 * Copyright (C) 2000 Ximian, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * published by the Free Software Foundation; either version 2 of the
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

#include <gtk/gtkbox.h>
#include <gtk/gtkcheckbutton.h>
#include <gtk/gtkfilesel.h>
#include <gtk/gtkmain.h>
#include <gtk/gtksignal.h>
#include "e-msg-composer-select-file.h"


struct _FileSelectionInfo {
	GtkWidget *widget;
	GtkToggleButton *inline_checkbox;
	gboolean multiple;
	GPtrArray *selected_files;
};
typedef struct _FileSelectionInfo FileSelectionInfo;

static void
confirm (FileSelectionInfo *info)
{
	const char *filename;
	GtkCList *file_list;
	char *path;
	GList *l;
	
	file_list = GTK_CLIST (GTK_FILE_SELECTION (info->widget)->file_list);
	
	if (info->multiple && file_list->selection) {
		/* evil kludgy hack cuz the gtk file selector fucking sucks ass */
		path = g_dirname (gtk_file_selection_get_filename (GTK_FILE_SELECTION (info->widget)));
		
		l = file_list->selection;
		
		while (l) {
			int row;
			
			if (!info->selected_files)
				info->selected_files = g_ptr_array_new ();
			
			row = GPOINTER_TO_INT (l->data);
			if (gtk_clist_get_text (GTK_CLIST (file_list), row, 0, (char **) &filename))
				g_ptr_array_add (info->selected_files, g_strdup_printf ("%s/%s", path, filename));
			
			l = l->next;
		}
		
		g_free (path);
	} else {
		filename = gtk_file_selection_get_filename (GTK_FILE_SELECTION (info->widget));
		if (filename) {
			info->selected_files = g_ptr_array_new ();
			g_ptr_array_add (info->selected_files, g_strdup (filename));
		}
	}
	
	gtk_widget_hide (info->widget);
	
	gtk_main_quit ();
}

static void
cancel (FileSelectionInfo *info)
{
	g_assert (info->selected_files == NULL);
	
	gtk_widget_hide (info->widget);
	
	gtk_main_quit ();
}


/* Callbacks.  */

static void
ok_clicked_cb (GtkWidget *widget,
	       void *data)
{
	FileSelectionInfo *info;
	
	info = (FileSelectionInfo *) data;
	confirm (info);
}

static void
cancel_clicked_cb (GtkWidget *widget,
		   void *data)
{
	FileSelectionInfo *info;
	
	info = (FileSelectionInfo *) data;
	cancel (info);
}

static int
delete_event_cb (GtkWidget *widget,
		 GdkEventAny *event,
		 void *data)
{
	FileSelectionInfo *info;
	
	info = (FileSelectionInfo *) data;
	cancel (info);
	
	return TRUE;
}


/* Setup.  */

static FileSelectionInfo *
create_file_selection (EMsgComposer *composer, gboolean multiple)
{
	FileSelectionInfo *info;
	GtkWidget *widget;
	GtkWidget *ok_button;
	GtkWidget *cancel_button;
	GtkWidget *inline_checkbox;
	GtkWidget *file_list;
	GtkWidget *box;
	char *path;
	
	info = g_new (FileSelectionInfo, 1);
	
	widget = gtk_file_selection_new (NULL);
	path = g_strdup_printf ("%s/", g_get_home_dir ());
	gtk_file_selection_set_filename (GTK_FILE_SELECTION (widget), path);
	g_free (path);
	gtk_window_set_wmclass (GTK_WINDOW (widget), "fileselection", 
				"Evolution:composer");
	
	if (multiple) {
		file_list = GTK_FILE_SELECTION (widget)->file_list;
		gtk_clist_set_selection_mode (GTK_CLIST (file_list),
					      GTK_SELECTION_MULTIPLE);
	}
	
	ok_button     = GTK_FILE_SELECTION (widget)->ok_button;
	cancel_button = GTK_FILE_SELECTION (widget)->cancel_button;
	
	gtk_signal_connect (GTK_OBJECT (ok_button),
			    "clicked", GTK_SIGNAL_FUNC (ok_clicked_cb), info);
	gtk_signal_connect (GTK_OBJECT (cancel_button),
			    "clicked", GTK_SIGNAL_FUNC (cancel_clicked_cb), info);
	gtk_signal_connect (GTK_OBJECT (widget), "delete_event",
			    GTK_SIGNAL_FUNC (delete_event_cb), info);
	
	inline_checkbox = gtk_check_button_new_with_label (_("Suggest automatic display of attachment"));
	box = gtk_widget_get_ancestor (GTK_FILE_SELECTION (widget)->selection_entry, GTK_TYPE_BOX);
	gtk_box_pack_end (GTK_BOX (box), inline_checkbox, FALSE, FALSE, 4);
	
	info->widget          = widget;
	info->multiple        = multiple;
	info->selected_files  = NULL;
	info->inline_checkbox = GTK_TOGGLE_BUTTON (inline_checkbox);
	
	return info;
}

static void
file_selection_info_destroy_notify (void *data)
{
	FileSelectionInfo *info;
	int i;
	
	info = (FileSelectionInfo *) data;
	
	if (info->widget != NULL)
		gtk_widget_destroy (GTK_OBJECT (info->widget));
	
	if (info->selected_files) {
		for (i = 0; i < info->selected_files->len; i++)
			g_free (info->selected_files->pdata[i]);
		
		g_ptr_array_free (info->selected_files, TRUE);
	}
	
	g_free (info);
}


static GPtrArray *
select_file_internal (EMsgComposer *composer,
		      const char *title,
		      gboolean multiple,
		      gboolean *inline_p)
{
	FileSelectionInfo *info;
	GPtrArray *files;
	
	g_return_val_if_fail (E_IS_MSG_COMPOSER (composer), NULL);
	
	info = gtk_object_get_data (GTK_OBJECT (composer),
				    "e-msg-composer-file-selection-info");
	
	if (info == NULL) {
		info = create_file_selection (composer, multiple);
		gtk_object_set_data_full (GTK_OBJECT (composer),
					  "e-msg-composer-file-selection-info", info,
					  file_selection_info_destroy_notify);
	}
	
	if (GTK_WIDGET_VISIBLE (info->widget))
		return NULL;		/* Busy!  */
	
	gtk_window_set_title (GTK_WINDOW (info->widget), title);
	if (inline_p)
		gtk_widget_show (GTK_WIDGET (info->inline_checkbox));
	else
		gtk_widget_hide (GTK_WIDGET (info->inline_checkbox));
	gtk_widget_show (info->widget);
	
	GDK_THREADS_ENTER();
	gtk_main ();
	GDK_THREADS_LEAVE();
	
	files = info->selected_files;
	info->selected_files = NULL;
	
	if (inline_p) {
		*inline_p = gtk_toggle_button_get_active (info->inline_checkbox);
		gtk_toggle_button_set_active (info->inline_checkbox, FALSE);
	}
	
	return files;
}

/**
 * e_msg_composer_select_file:
 * @composer: a composer
 * @title: the title for the file selection dialog box
 *
 * This pops up a file selection dialog box with the given title
 * and allows the user to select a file.
 *
 * Return value: the selected filename, or %NULL if the user
 * cancelled.
 **/
char *
e_msg_composer_select_file (EMsgComposer *composer,
			    const char *title)
{
	GPtrArray *files;
	char *filename = NULL;
	
	files = select_file_internal (composer, title, FALSE, NULL);
	if (files) {
		filename = files->pdata[0];
		g_ptr_array_free (files, FALSE);
	}
	
	return filename;
}

GPtrArray *
e_msg_composer_select_file_attachments (EMsgComposer *composer,
					gboolean *inline_p)
{
	return select_file_internal (composer, _("Attach a file"), TRUE, inline_p);
}
