/* Evolution calendar - Recurring calendar component dialog
 *
 * Copyright (C) 2002 Ximian, Inc.
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

#include <glib.h>
#include <gtk/gtk.h>
#include <libgnome/gnome-i18n.h>
#include <libgnomeui/gnome-uidefs.h>
#include <gal/widgets/e-unicode.h>
#include "recur-comp.h"



gboolean
recur_component_dialog (CalComponent *comp,
			CalObjModType *mod,
			GtkWindow *parent)
{
	char *str;
	GtkWidget *dialog, *rb1, *rb2, *rb3, *hbox;
	CalComponentVType vtype;
	gboolean ret;
	
	g_return_val_if_fail (IS_CAL_COMPONENT (comp), CALOBJ_MOD_THIS);

	vtype = cal_component_get_vtype (comp);
	
	switch (vtype) {
	case CAL_COMPONENT_EVENT:
		str = g_strdup_printf (_("You are modifying a recurring event, what would you like to modify?"));
		break;

	case CAL_COMPONENT_TODO:
		str = g_strdup_printf (_("You are modifying a recurring task, what would you like to modify?"));
		break;

	case CAL_COMPONENT_JOURNAL:
		str = g_strdup_printf (_("You are modifying a recurring journal entry, what would you like to modify?"));
		break;

	default:
		g_message ("recur_component_dialog(): Cannot handle object of type %d", vtype);
		return CALOBJ_MOD_THIS;
	}


	dialog = gtk_message_dialog_new (parent, 0, GTK_MESSAGE_QUESTION, GTK_BUTTONS_OK_CANCEL, "%s", str);
	g_free (str);
	
	hbox = gtk_hbox_new (FALSE, 2);
	gtk_container_add (GTK_CONTAINER (GTK_DIALOG (dialog)->vbox), hbox);
	rb1 = gtk_radio_button_new_with_label (NULL, _("This Instance Only"));
	gtk_container_add (GTK_CONTAINER (hbox), rb1);

	rb2 = gtk_radio_button_new_with_label_from_widget (GTK_RADIO_BUTTON (rb1), _("This and Future Instances"));
	gtk_container_add (GTK_CONTAINER (hbox), rb2);	
	rb3 = gtk_radio_button_new_with_label_from_widget (GTK_RADIO_BUTTON (rb1), _("All Instances"));
	gtk_container_add (GTK_CONTAINER (hbox), rb3);

	gtk_widget_show_all (hbox);

	ret = gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_OK;

	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (rb1)))
		*mod = CALOBJ_MOD_THIS;
	else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (rb2)))
		*mod = CALOBJ_MOD_THISANDFUTURE;
	else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (rb3)))
		*mod = CALOBJ_MOD_ALL;

	gtk_widget_destroy (dialog);

	return ret;
}
