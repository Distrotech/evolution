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

#include <bonobo/bonobo-i18n.h>
#include <gtk/gtkdialog.h>
#include <gtk/gtkentry.h>
#include <gtk/gtkmenu.h>
#include <gtk/gtkmenuitem.h>
#include <gtk/gtkmessagedialog.h>
#include <gtk/gtkoptionmenu.h>
#include <glade/glade.h>
#include <e-util/e-dialog-utils.h>
#include <e-util/e-source-list.h>
#include "new-calendar.h"

static gboolean
create_new_source_with_group (GtkWindow *parent,
			      ESourceGroup *group,
			      const char *source_name)
{
	ESource *source;
	char *new_dir;

	if (e_source_group_peek_source_by_name (group, source_name)) {
		e_notice (parent, GTK_MESSAGE_ERROR,
			  _("Source with name '%s' already exists in the selected group"),
			  source_name);
		return FALSE;
	}

	/* create the new source */
	new_dir = g_build_filename (e_source_group_peek_base_uri (group),
				    source_name, NULL);
	if (e_mkdir_hier (new_dir, 0700)) {
		g_free (new_dir);
		e_notice (parent, GTK_MESSAGE_ERROR,
			  _("Could not create directory for new calendar"));
		return FALSE;
	}

	source = e_source_new (source_name, source_name);
	e_source_group_add_source (group, source, -1);

	g_free (new_dir);

	return TRUE;
}

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
	ESourceList *source_list;
	GConfClient *gconf_client;
	GSList *groups, *sl;
	gboolean result = FALSE, retry = TRUE;

	/* load the Glade file */
	xml = glade_xml_new (EVOLUTION_GLADEDIR "/new-calendar.glade", "new-calendar-dialog", NULL);
	if (!xml) {
		g_warning ("new_calendar_dialog(): cannot load Glade file");
		return FALSE;
	}

	dialog = glade_xml_get_widget (xml, "new-calendar-dialog");
	cal_group = glade_xml_get_widget (xml, "calendar-group");
	cal_name = glade_xml_get_widget (xml, "calendar-name");

	/* set up widgets */
	gconf_client = gconf_client_get_default ();
	source_list = e_source_list_new_for_gconf (gconf_client, "/apps/evolution/calendar/sources");

	groups = e_source_list_peek_groups (source_list);
	for (sl = groups; sl != NULL; sl = sl->next) {
		GtkWidget *menu_item, *menu;
		ESourceGroup *group = sl->data;

		menu = gtk_option_menu_get_menu (GTK_OPTION_MENU (cal_group));
		if (!GTK_IS_MENU (menu)) {
			menu = gtk_menu_new ();
			gtk_option_menu_set_menu (GTK_OPTION_MENU (cal_group), menu);
			gtk_widget_show (menu);
		}

		menu_item = gtk_menu_item_new_with_label (e_source_group_peek_name (group));
		gtk_widget_show (menu_item);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);
	}

	if (groups)
		gtk_option_menu_set_history (GTK_OPTION_MENU (cal_group), 0);

	/* run the dialog */
	do {
		if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_OK) {
			char *name;

			name = gtk_entry_get_text (GTK_ENTRY (cal_name));
			sl = g_slist_nth (groups, gtk_option_menu_get_history (GTK_OPTION_MENU (cal_group)));
			if (sl) {
				if (create_new_source_with_group (GTK_WINDOW (dialog),
								  sl->data,
								  name))
					retry = FALSE;
			} else {
				e_notice (dialog, GTK_MESSAGE_ERROR,
					  _("A group must be selected"));
				continue;
			}
		}
	} while (retry);

	/* free memory */
	g_object_unref (gconf_client);
	g_object_unref (source_list);
	gtk_widget_destroy (dialog);

	/* free memory */
	g_object_unref (xml);

	return result;
}
