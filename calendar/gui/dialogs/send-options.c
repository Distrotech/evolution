/* Evolution calendar - Main page of the Groupwise send options Dialog
 *
 * Copyright (C) 2004 Novell, Inc.
 *
 * Authors: Chenthill Palanisamy <pchenthill@novell.com>
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Bangalore, MA 02111-1307, India.
 */

#include <string.h>
#include <glib.h>
#include <gtk/gtksignal.h>
#include <gtk/gtktogglebutton.h>
#include <gtk/gtk.h>
#include <comp-editor-util.h>
#include <libgnome/gnome-i18n.h>
#include <glade/glade.h>
#include "../calendar-config.h"
#include "comp-editor.h"
#include "send-options.h"


typedef struct {
	/* Glade XML data */
	GladeXML *xml;

	ECal *client;
	
	ESendOptionsData *options_data;
	/* Widgets */

	GtkWidget *main;

	GtkWidget *notebook;
	
	/* priority */
	GtkWidget *priority;
		
	/* Widgets for Reply Requestion options */
	GtkWidget *reply_request;
	GtkWidget *reply_convenient;
	GtkWidget *reply_within;
	GtkWidget *within_days;

	/* Widgets for delay delivery Option */
	GtkWidget *delay_delivery;
	GtkWidget *delay_until;

	/* Widgets for Choosing expiration date */
	GtkWidget *expiration;
	GtkWidget *expire_after;

	/* Widgets to for tracking information through sent Item */
	GtkWidget *create_sent;
	GtkWidget *delivered;
	GtkWidget *delivered_opened;
	GtkWidget *all_info;

	/* Widgets for setting the Return Notification */
	GtkWidget *when_opened;
	GtkWidget *when_declined;
	GtkWidget *when_accepted;
	GtkWidget *when_completed;
	GtkWidget *completed_label;

} SendOptionsDialog;


static void 
sensitize_widgets (SendOptionsDialog *options_dialog)
{
	ESendOptionsData *options_data;
	ESendOptionsGeneral *goptions_data;
	ESendOptionsStatusTracking *soptions_data;	

	options_data = options_dialog->options_data;
	goptions_data = options_data->general_opts;
	soptions_data = options_data->status_opts;

	if (!goptions_data->reply_enabled) {
		gtk_widget_set_sensitive (options_dialog->reply_convenient, FALSE);
		gtk_widget_set_sensitive (options_dialog->reply_within, FALSE);
		gtk_widget_set_sensitive (options_dialog->within_days, FALSE);
	}

	if (!goptions_data->expiration_enabled)
		gtk_widget_set_sensitive (options_dialog->expire_after, FALSE);

	if (!goptions_data->delay_enabled){
		gtk_widget_set_sensitive (options_dialog->delay_until, FALSE);
	}

	if (!soptions_data->tracking_enabled) {
		gtk_widget_set_sensitive (options_dialog->delivered, FALSE);
		gtk_widget_set_sensitive (options_dialog->delivered_opened, FALSE);
		gtk_widget_set_sensitive (options_dialog->all_info, FALSE);
	}
}

static void
expiration_toggled_cb (GtkWidget *toggle, gpointer data)
{
	gboolean active;
	SendOptionsDialog *options_dialog;

	options_dialog = data;
	active = GTK_TOGGLE_BUTTON (toggle)->active;

	gtk_widget_set_sensitive (options_dialog->expire_after, active);
}

static void
reply_request_toggled_cb (GtkWidget *toggle, gpointer data)
{
	gboolean active;
	SendOptionsDialog *options_dialog;

	options_dialog = data;
	active = GTK_TOGGLE_BUTTON (toggle)->active;

	gtk_widget_set_sensitive (options_dialog->reply_convenient, active);
	gtk_widget_set_sensitive (options_dialog->reply_within, active);
	gtk_widget_set_sensitive (options_dialog->within_days, active);
	
}

static void
delay_delivery_toggled_cb (GtkWidget *toggle, gpointer data)
{
	gboolean active;
	SendOptionsDialog *options_dialog;

	options_dialog = data;
	active = GTK_TOGGLE_BUTTON (toggle)->active;

	gtk_widget_set_sensitive (options_dialog->delay_until, active);
}

static void
sent_item_toggled_cb (GtkWidget *toggle, gpointer data)
{
	gboolean active;
	SendOptionsDialog *options_dialog;

	options_dialog = data;
	active = GTK_TOGGLE_BUTTON (toggle)->active;

	gtk_widget_set_sensitive (options_dialog->delivered, active);
	gtk_widget_set_sensitive (options_dialog->delivered_opened, active);
	gtk_widget_set_sensitive (options_dialog->all_info, active);

}

static void
delay_until_date_changed_cb (GtkWidget *dedit, gpointer data)
{
	SendOptionsDialog *options_dialog = data;
	ESendOptionsData *options_data = options_dialog->options_data;
	icaltimetype temp = icaltime_null_time ();

	e_date_edit_get_date (E_DATE_EDIT (options_dialog->delay_until), &temp.year, &temp.month, &temp.day);

	if ((icaltime_compare_date_only (temp, icaltime_current_time_with_zone (options_data->zone)) < 0) ||
			!e_date_edit_date_is_valid (E_DATE_EDIT (options_dialog->delay_until)))
		e_date_edit_set_time (E_DATE_EDIT (options_dialog->delay_until), 0);
	
}

static void
send_options_init_widgets (SendOptionsDialog *options_dialog)
{
	
	g_signal_connect (options_dialog->expiration, "toggled", G_CALLBACK (expiration_toggled_cb), options_dialog);
	g_signal_connect (options_dialog->reply_request, "toggled", G_CALLBACK (reply_request_toggled_cb), options_dialog);
	g_signal_connect (options_dialog->delay_delivery, "toggled", G_CALLBACK (delay_delivery_toggled_cb), options_dialog);	
	g_signal_connect (options_dialog->create_sent, "toggled", G_CALLBACK (sent_item_toggled_cb), options_dialog);

	g_signal_connect (options_dialog->delay_until, "changed", G_CALLBACK (delay_until_date_changed_cb), options_dialog);

}

static gboolean
get_widgets (SendOptionsDialog *options_dialog)
{
#define GW(name) glade_xml_get_widget (options_dialog->xml, name)

	options_dialog->main = GW ("send-options-dialog");
	if (!options_dialog->main)
		return FALSE;

	options_dialog->notebook = GW ("send-options-book");
	options_dialog->priority = GW ("combo-priority");
	options_dialog->reply_request = GW ("reply-request-button");
	options_dialog->reply_convenient = GW ("reply-convinient");
	options_dialog->reply_within = GW ("reply-within");
	options_dialog->within_days = GW ("within-days");
	options_dialog->delay_delivery = GW ("delay-delivery-button");
	options_dialog->delay_until = GW ("until-date");
	gtk_widget_show (options_dialog->delay_until);
	options_dialog->expiration = GW ("expiration-button");
	options_dialog->expire_after = GW ("expire-after");
	options_dialog->create_sent = GW ("create-sent-button");
	options_dialog->delivered = GW ("delivered");
	options_dialog->delivered_opened = GW ("delivered-opened");
	options_dialog->all_info = GW ("all-info");
	options_dialog->when_opened = GW ("open-combo");
	options_dialog->when_declined = GW ("delete-combo");
	options_dialog->when_accepted = GW ("accept-combo");
	options_dialog->when_completed = GW ("complete-combo");
	options_dialog->completed_label = GW ("completed-label");
	
#undef GW

	return (options_dialog->notebook
		&& options_dialog->priority
		&& options_dialog->reply_request
		&& options_dialog->reply_convenient
		&& options_dialog->reply_within
		&& options_dialog->within_days
		&& options_dialog->delay_delivery
		&& options_dialog->delay_until
		&& options_dialog->expiration
		&& options_dialog->expire_after
		&& options_dialog->create_sent
		&& options_dialog->delivered
		&& options_dialog->delivered_opened
		&& options_dialog->all_info
		&& options_dialog->when_opened
		&& options_dialog->when_declined
		&& options_dialog->when_accepted
		&& options_dialog->when_completed);
	
}


/* At present We set the default options manually here. The Options should be read from
   sources as soon as the Global send Options setttings are available */
static void
send_options_set_default_data (ESendOptionsData *options_data) 
{
	ESendOptionsGeneral *goptions_data;
	ESendOptionsStatusTracking *soptions_data;	
	icaltimetype current_time;
	
	goptions_data = options_data->general_opts;
	soptions_data = options_data->status_opts;
	

	
	options_data->zone = calendar_config_get_icaltimezone ();
	current_time = icaltime_current_time_with_zone (options_data->zone);
	
	goptions_data->priority = E_PRIORITY_STANDARD;
	goptions_data->reply_enabled = FALSE;
	goptions_data->expiration_enabled = FALSE;
	goptions_data->delay_enabled = FALSE;
	goptions_data->delay_until = (char *) (icaltime_as_ical_string (current_time));
	goptions_data->priority = E_PRIORITY_STANDARD;
	
	soptions_data->tracking_enabled = TRUE;
	soptions_data->track_when = E_DELIVERED_OPENED;
	soptions_data->opened = E_RETURN_NOTIFY_NONE;
	soptions_data->accepted = E_RETURN_NOTIFY_NONE;
	soptions_data->declined = E_RETURN_NOTIFY_NONE;
	soptions_data->completed = E_RETURN_NOTIFY_NONE;
	
}
	
static void
send_options_get_widgets_data (SendOptionsDialog *options_dialog)
{
	ESendOptionsData *options_data = options_dialog->options_data;
	ESendOptionsGeneral *goptions_data = options_data->general_opts;
	ESendOptionsStatusTracking *soptions_data = options_data->status_opts;
	icaltimetype time_set = icaltime_null_time ();

	goptions_data->priority = gtk_combo_box_get_active ((GtkComboBox *) options_dialog->priority);
		
	goptions_data->reply_enabled = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (options_dialog->reply_request));
	goptions_data->reply_convenient = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (options_dialog->reply_convenient));
	goptions_data->reply_within = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (options_dialog->within_days));
	
	goptions_data->expiration_enabled = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (options_dialog->expiration));
	goptions_data->expire_after = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (options_dialog->expire_after));
	goptions_data->delay_enabled = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (options_dialog->delay_delivery));
	if (e_date_edit_date_is_valid (E_DATE_EDIT (options_dialog->delay_until)))
		e_date_edit_get_date (E_DATE_EDIT (options_dialog->delay_until), &time_set.year,
				&time_set.month, &time_set.day);
	
	if (e_date_edit_time_is_valid (E_DATE_EDIT (options_dialog->delay_until)))
			e_date_edit_get_time_of_day (E_DATE_EDIT (options_dialog->delay_until), &time_set.hour,
				&time_set.minute);

	g_free (goptions_data->delay_until);
	goptions_data->delay_until = (char *) (icaltime_as_ical_string (time_set));

	soptions_data->tracking_enabled = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (options_dialog->create_sent));
	
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (options_dialog->delivered)))
		soptions_data->track_when = E_DELIVERED;
	else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (options_dialog->delivered_opened)))
		soptions_data->track_when = E_DELIVERED_OPENED;
	else
		soptions_data->track_when = E_ALL;
	
	soptions_data->opened = gtk_combo_box_get_active ((GtkComboBox *) options_dialog->when_opened);
	soptions_data->accepted = gtk_combo_box_get_active ((GtkComboBox *) options_dialog->when_accepted);
	soptions_data->declined = gtk_combo_box_get_active ((GtkComboBox *) options_dialog->when_declined);	
	soptions_data->completed = gtk_combo_box_get_active ((GtkComboBox *) options_dialog->when_completed);
}

static void
send_options_fill_widgets_with_data (SendOptionsDialog *options_dialog)
{
	ESendOptionsData *options_data = options_dialog->options_data;
	ESendOptionsGeneral *goptions_data;
	ESendOptionsStatusTracking *soptions_data;	
	icaltimetype time_to_set = icaltime_null_time ();
	
	
	goptions_data = options_data->general_opts;
	soptions_data = options_data->status_opts;	

	gtk_combo_box_set_active ((GtkComboBox *) options_dialog->priority, goptions_data->priority);
	
	if (goptions_data->reply_enabled) 
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (options_dialog->reply_request), TRUE);
	else
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (options_dialog->reply_request), FALSE);
	
	if (goptions_data->reply_convenient)
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (options_dialog->reply_convenient), TRUE);
	else 
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (options_dialog->reply_within), TRUE);
		
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (options_dialog->within_days), (gdouble) goptions_data->reply_within);
		
	if (goptions_data->expiration_enabled)	
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (options_dialog->expiration), TRUE);
	else
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (options_dialog->expiration), FALSE);

	gtk_spin_button_set_value (GTK_SPIN_BUTTON (options_dialog->expire_after), (gdouble) goptions_data->expire_after);

	/* TODO Set the delivery widget active when the feature is supported */
	gtk_widget_set_sensitive (options_dialog->delay_delivery, FALSE);
	if (goptions_data->delay_enabled) 
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (options_dialog->delay_delivery), TRUE);
	else
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (options_dialog->delay_delivery), FALSE);
	
	time_to_set = icaltime_from_string ((const char *) goptions_data->delay_until);
	e_date_edit_set_date (E_DATE_EDIT (options_dialog->delay_until), time_to_set.year,
			time_to_set.month, time_to_set.day);
	
	e_date_edit_set_time_of_day (E_DATE_EDIT (options_dialog->delay_until), time_to_set.hour,
			time_to_set.minute);
	
	if (soptions_data->tracking_enabled) 
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (options_dialog->create_sent), TRUE);
	else
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (options_dialog->create_sent), FALSE);
	
	switch (soptions_data->track_when) {
		case E_DELIVERED:
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (options_dialog->delivered), TRUE);
			break;
		case E_DELIVERED_OPENED:
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (options_dialog->delivered_opened), TRUE);
			break;
		case E_ALL:
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (options_dialog->all_info), TRUE);
	}
	
	gtk_combo_box_set_active ((GtkComboBox *) options_dialog->when_opened, soptions_data->opened);
	gtk_combo_box_set_active ((GtkComboBox *) options_dialog->when_declined, soptions_data->declined);
	gtk_combo_box_set_active ((GtkComboBox *) options_dialog->when_accepted, soptions_data->accepted);
	gtk_combo_box_set_active ((GtkComboBox *) options_dialog->when_completed, soptions_data->completed);
		
}

ESendOptionsData * 
send_options_new (void)
{
	ESendOptionsData *new_data = NULL;

	new_data = g_new0 (ESendOptionsData, 1);
	new_data->general_opts = g_new0 (ESendOptionsGeneral, 1);
	new_data->status_opts = g_new0 (ESendOptionsStatusTracking, 1);

	new_data->general_opts->delay_until = NULL;
	send_options_set_default_data (new_data);
	new_data->zone = NULL;

	return new_data;
}

void
send_options_finalize (ESendOptionsData *options_data)
{
	
	if (options_data->general_opts->delay_until)
		g_free (options_data->general_opts->delay_until);
}

GtkWidget *send_options_make_date_edit (void);

GtkWidget *
send_options_make_date_edit (void)
{
	return comp_editor_new_date_edit (TRUE, TRUE, TRUE);
}

gboolean
send_options_run_dialog (GtkWidget *parent, ECal *ecal, ESendOptionsData *options_data, gboolean is_event)
{
	SendOptionsDialog *options_dialog = NULL;
	GtkWidget *toplevel;
	int result;

	options_dialog =g_new0 (SendOptionsDialog, 1);
	options_dialog->client = ecal;
	options_dialog->options_data = options_data;
	options_dialog->xml = glade_xml_new (EVOLUTION_GLADEDIR "/send-options.glade", NULL, NULL);
	
	if (!options_dialog->xml) {
		g_message ( G_STRLOC ": Could not load the Glade XML file ");
		return FALSE;
	}

	if (!get_widgets(options_dialog)) {
		g_object_unref (options_dialog->xml);
		g_message (G_STRLOC ": Could not get the Widgets \n");
		return FALSE;
	}

	if (is_event) {
		gtk_widget_hide (options_dialog->when_completed);
		gtk_widget_hide (options_dialog->completed_label);
	}
	toplevel =  gtk_widget_get_toplevel (options_dialog->main);
	gtk_window_set_transient_for (GTK_WINDOW (toplevel),
				      GTK_WINDOW (parent));

	send_options_fill_widgets_with_data (options_dialog);
	sensitize_widgets (options_dialog);	
	send_options_init_widgets (options_dialog);
	result = gtk_dialog_run (GTK_DIALOG (options_dialog->main));
	
	if (result == GTK_RESPONSE_OK) 
		send_options_get_widgets_data (options_dialog);

	gtk_widget_hide (options_dialog->main);

	gtk_widget_destroy (options_dialog->main);
	g_object_unref (options_dialog->xml);
			
	return TRUE;
}
	
void 
send_options_fill_component (ECalComponent *comp, ESendOptionsData *options_data)
{
	int i = 1;
	icalproperty *prop;
	icalcomponent *icalcomp;
	ESendOptionsGeneral *goptions_data;
	ESendOptionsStatusTracking *soptions_data;

	goptions_data = options_data->general_opts;
	soptions_data = options_data->status_opts;

	e_cal_component_set_sequence (comp, &i);
	icalcomp = e_cal_component_get_icalcomponent (comp);

	if (goptions_data->reply_enabled) {
		if (goptions_data->reply_convenient) 
			prop = icalproperty_new_x ("convenient");	
		else 
			prop = icalproperty_new_x ((const char *) g_strdup_printf ( "%d", goptions_data->reply_within));
		icalproperty_set_x_name (prop, "X-EVOLUTION-OPTIONS-REPLY");
		icalcomponent_add_property (icalcomp, prop);
	}

	if (goptions_data->expiration_enabled && goptions_data->expire_after) {
		prop = icalproperty_new_x ((const char *) g_strdup_printf ( "%d", goptions_data->expire_after));	
		icalproperty_set_x_name (prop, "X-EVOLUTION-OPTIONS-EXPIRE");
		icalcomponent_add_property (icalcomp, prop);
	}

	if (goptions_data->delay_enabled) {
		prop = icalproperty_new_x ((const char *) goptions_data->delay_until);
		icalproperty_set_x_name (prop, "X-EVOLUTION-OPTIONS-DELAY");
		icalcomponent_add_property (icalcomp, prop);
	}

	if (soptions_data->tracking_enabled) {
		prop = icalproperty_new_x ((const char *) g_strdup_printf ( "%d", soptions_data->track_when));	
		icalproperty_set_x_name (prop, "X-EVOLUTION-OPTIONS-TRACKINFO");
		icalcomponent_add_property (icalcomp, prop);
	}	
	
	prop = icalproperty_new_x ((const char *) g_strdup_printf ("%d", soptions_data->opened));	
	icalproperty_set_x_name (prop, "X-EVOLUTION-OPTIONS-OPENED");
	icalcomponent_add_property (icalcomp, prop);
	
	prop = icalproperty_new_x ((const char *) g_strdup_printf ("%d", soptions_data->accepted));	
	icalproperty_set_x_name (prop, "X-EVOLUTION-OPTIONS-ACCEPTED");
	icalcomponent_add_property (icalcomp, prop);

	prop = icalproperty_new_x ((const char *) g_strdup_printf ("%d", soptions_data->declined));	
	icalproperty_set_x_name (prop, "X-EVOLUTION-OPTIONS-DECLINED");
	icalcomponent_add_property (icalcomp, prop);

	prop = icalproperty_new_x ((const char *) g_strdup_printf ("%d", soptions_data->completed));	
	icalproperty_set_x_name (prop, "X-EVOLUTION-OPTIONS-COMPLETED");
	icalcomponent_add_property (icalcomp, prop);

	switch (goptions_data->priority) {
		case E_PRIORITY_LOW : 
			i = 7;
			e_cal_component_set_priority (comp, &i);
			break;
		case E_PRIORITY_STANDARD :
			i = 5;
			e_cal_component_set_priority (comp, &i);
			break;
		case E_PRIORITY_HIGH :
			i = 3;
			e_cal_component_set_priority (comp, &i);
	}
}

