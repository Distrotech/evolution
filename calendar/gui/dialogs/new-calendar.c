/* Evolution calendar - Send calendar component dialog
 *
 * Copyright (C) 2001 Ximian, Inc.
 *
 * Author: JP Rosevear <jpr@ximian.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gtk/gtkdialog.h>
#include <glade/glade.h>

/**
 * new_calendar_dialog
 *
 * Displays a dialog that allows the user to create a new calendar.
 */
gboolean
new_calendar_dialog (GtkWindow *parent)
{
	GtkWidget *dialog, *cal_group, *cal_name;
	GladeXML *xml;
	gboolean result = FALSE;

	/* load the Glade file */
	xml = glade_xml_new (EVOLUTION_GLADEDIR "/new-calendar.glade", "new-calendar-dialog", NULL);
	if (!xml) {
		g_warning ("new_calendar_dialog(): cannot load Glade file");
		return FALSE;
	}

	dialog = glade_xml_get_widget (xml, "new-calendar-dialog");
	cal_group = glade_xml_get_widget (xml, "calendar-group");
	cal_name = glade_xml_get_widget (xml, "calendar-name");

	if (dialog && cal_group && cal_name) {
		/* set up widgets */

		/* run the dialog */
		if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_OK) {
		}

		gtk_widget_destroy (dialog);
	}

	/* free memory */
	g_object_unref (xml);

	return result;
}
