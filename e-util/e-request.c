/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-request.c
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

#include "e-request.h"

#include <libgnomeui/gnome-dialog.h>
#include <libgnomeui/gnome-stock.h>
#include <gal/widgets/e-unicode.h>

#include <gtk/gtklabel.h>
#include <gtk/gtkentry.h>


/**
 * e_request_string:
 * @parent: 
 * @title: 
 * @prompt: 
 * @default: 
 * 
 * Requst a string to the user.
 * 
 * Return value: NULL if the user cancelled the dialog, the inserted string
 * otherwise.  The string must be freed by the caller.
 **/
char *
e_request_string (GtkWindow *parent,
		  const char *title,
		  const char *prompt,
		  const char *default_string)
{
	GtkWidget *dialog;
	GtkWidget *prompt_label;
	GtkWidget *entry;
	GtkWidget *vbox;
	char *retval;

	g_return_val_if_fail (title != NULL, NULL);
	g_return_val_if_fail (prompt != NULL, NULL);

	dialog = gnome_dialog_new (title, GNOME_STOCK_BUTTON_OK, GNOME_STOCK_BUTTON_CANCEL, NULL);
	gnome_dialog_set_parent (GNOME_DIALOG (dialog), parent);
	gnome_dialog_set_default (GNOME_DIALOG (dialog), 0);
	gnome_dialog_close_hides (GNOME_DIALOG (dialog), TRUE);

	vbox = GNOME_DIALOG (dialog)->vbox;

	prompt_label = gtk_label_new (prompt);
	gtk_box_pack_start (GTK_BOX (vbox), prompt_label, TRUE, TRUE, 0);

	entry = gtk_entry_new ();
	e_utf8_gtk_entry_set_text (GTK_ENTRY (entry), default_string);
	gtk_entry_select_region (GTK_ENTRY (entry), 0, -1);
	gtk_box_pack_start (GTK_BOX (vbox), entry, TRUE, TRUE, 0);

	gtk_widget_grab_focus (entry);
	gnome_dialog_editable_enters (GNOME_DIALOG (dialog), GTK_EDITABLE (entry));

	gtk_widget_show (prompt_label);
	gtk_widget_show (entry);
	gtk_widget_show (dialog);

	switch (gnome_dialog_run (GNOME_DIALOG (dialog))) {
	case 0:
		/* OK.  */
		retval = e_utf8_gtk_entry_get_text (GTK_ENTRY (entry));
		break;
	case -1:
	case 1:
		/* Cancel.  */
		retval = NULL;
		break;
	default:
		g_assert_not_reached ();
		retval = NULL;
	}

	gtk_widget_destroy (dialog);

	return retval;
}
