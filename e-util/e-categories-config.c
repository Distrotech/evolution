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
 *		Rodrigo Moya <rodrigo@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#include <string.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <libedataserver/e-categories.h>
#include <libedataserverui/e-categories-dialog.h>
#include "e-categories-config.h"

/**
 * e_categories_config_get_icon_for:
 * @category: Category for which to get the icon.
 * @icon: A pointer to where the pixmap will be returned.
 * @mask: A pointer to where the mask will be returned.
 *
 * Returns the icon (and associated mask) configured for the
 * given category.
 */
gboolean
e_categories_config_get_icon_for (const gchar *category, GdkPixmap **pixmap, GdkBitmap **mask)
{
	gchar *icon_file;
	GdkPixbuf *pixbuf;
	GdkBitmap *tmp_mask;

	g_return_val_if_fail (pixmap != NULL, FALSE);

	icon_file = (gchar *) e_categories_get_icon_file_for (category);
	if (!icon_file) {
		*pixmap = NULL;
		if (mask != NULL)
			*mask = NULL;
		return FALSE;
	}

	/* load the icon in our list */
	pixbuf = gdk_pixbuf_new_from_file (icon_file, NULL);
	if (!pixbuf) {
		*pixmap = NULL;
		if (mask != NULL)
			*mask = NULL;
		return FALSE;
	}

	/* render the pixbuf to the pixmap and mask passed */
	gdk_pixbuf_render_pixmap_and_mask (pixbuf, pixmap, &tmp_mask, 1);
	if (mask != NULL)
		*mask = tmp_mask;

	g_object_unref (pixbuf);
	return TRUE;
}

/**
 * e_categories_config_open_dialog_for_entry:
 * entry: A GtkEntry on which to get/set the categories list.
 *
 * This is a self-contained function that lets you open a popup dialog for
 * the user to select a list of categories.
 *
 * The @entry parameter is used, at initialization time, as the list of
 * initial categories that are selected in the categories selection dialog.
 * Then, when the user commits its changes, the list of selected categories
 * is put back on the entry widget.
 */
void
e_categories_config_open_dialog_for_entry (GtkEntry *entry)
{
	GtkDialog *dialog;
	const gchar *text;
	gint result;

	g_return_if_fail (entry != NULL);
	g_return_if_fail (GTK_IS_ENTRY (entry));

	text = gtk_entry_get_text (GTK_ENTRY (entry));
	dialog = GTK_DIALOG (e_categories_dialog_new (text));

	gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW(gtk_widget_get_toplevel (GTK_WIDGET (entry))));

	/* run the dialog */
	result = gtk_dialog_run (dialog);

	if (result == GTK_RESPONSE_OK) {
		text = e_categories_dialog_get_categories (E_CATEGORIES_DIALOG (dialog));
		gtk_entry_set_text (GTK_ENTRY (entry), text);
	}

	gtk_object_destroy (GTK_OBJECT (dialog));
}
