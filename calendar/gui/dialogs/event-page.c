/* Evolution calendar - Main page of the event editor dialog
 *
 * Copyright (C) 2001 Ximian, Inc.
 *
 * Authors: Federico Mena-Quintero <federico@ximian.com>
 *          Miguel de Icaza <miguel@ximian.com>
 *          Seth Alves <alves@hungry.com>
 *          JP Rosevear <jpr@ximian.com>
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

#include <gtk/gtksignal.h>
#include <gtk/gtktogglebutton.h>
#include <glade/glade.h>
#include <gal/widgets/e-unicode.h>
#include <gal/widgets/e-categories.h>
#include "e-util/e-categories-config.h"
#include "e-util/e-dialog-widgets.h"
#include "widgets/misc/e-dateedit.h"
#include "cal-util/timeutil.h"
#include "../calendar-config.h"
#include "../e-timezone-entry.h"
#include "comp-editor-util.h"
#include "event-page.h"



/* Private part of the EventPage structure */
struct _EventPagePrivate {
	/* Glade XML data */
	GladeXML *xml;

	/* Widgets from the Glade file */

	GtkWidget *main;

	GtkWidget *summary;

	GtkWidget *start_time;
	GtkWidget *end_time;
	GtkWidget *start_timezone;
	GtkWidget *end_timezone;
	GtkWidget *all_day_event;

	GtkWidget *description;

	GtkWidget *classification_public;
	GtkWidget *classification_private;
	GtkWidget *classification_confidential;

	GtkWidget *show_time_as_free;
	GtkWidget *show_time_as_busy;

	GtkWidget *contacts_btn;
	GtkWidget *contacts_box;

	GtkWidget *categories_btn;
	GtkWidget *categories;

	gboolean updating;

	/* This is TRUE if both the start & end timezone are the same. If the
	   start timezone is then changed, we updated the end timezone to the
	   same value, since 99% of events start and end in one timezone. */
	gboolean sync_timezones;

	/* The Corba component for selecting contacts, and the entry field
	   which we place in the dialog. */
	GNOME_Evolution_Addressbook_SelectNames corba_select_names;
	GtkWidget *contacts_entry;
};



static void event_page_class_init (EventPageClass *class);
static void event_page_init (EventPage *epage);
static void event_page_destroy (GtkObject *object);

static GtkWidget *event_page_get_widget (CompEditorPage *page);
static void event_page_focus_main_widget (CompEditorPage *page);
static void event_page_fill_widgets (CompEditorPage *page, CalComponent *comp);
static gboolean event_page_fill_component (CompEditorPage *page, CalComponent *comp);
static void event_page_set_summary (CompEditorPage *page, const char *summary);
static void event_page_set_dates (CompEditorPage *page, CompEditorPageDates *dates);

static CompEditorPageClass *parent_class = NULL;



/**
 * event_page_get_type:
 * 
 * Registers the #EventPage class if necessary, and returns the type ID
 * associated to it.
 * 
 * Return value: The type ID of the #EventPage class.
 **/
GtkType
event_page_get_type (void)
{
	static GtkType event_page_type;

	if (!event_page_type) {
		static const GtkTypeInfo event_page_info = {
			"EventPage",
			sizeof (EventPage),
			sizeof (EventPageClass),
			(GtkClassInitFunc) event_page_class_init,
			(GtkObjectInitFunc) event_page_init,
			NULL, /* reserved_1 */
			NULL, /* reserved_2 */
			(GtkClassInitFunc) NULL
		};

		event_page_type = gtk_type_unique (TYPE_COMP_EDITOR_PAGE, 
						   &event_page_info);
	}

	return event_page_type;
}

/* Class initialization function for the event page */
static void
event_page_class_init (EventPageClass *class)
{
	CompEditorPageClass *editor_page_class;
	GtkObjectClass *object_class;

	editor_page_class = (CompEditorPageClass *) class;
	object_class = (GtkObjectClass *) class;

	parent_class = gtk_type_class (TYPE_COMP_EDITOR_PAGE);

	editor_page_class->get_widget = event_page_get_widget;
	editor_page_class->focus_main_widget = event_page_focus_main_widget;
	editor_page_class->fill_widgets = event_page_fill_widgets;
	editor_page_class->fill_component = event_page_fill_component;
	editor_page_class->set_summary = event_page_set_summary;
	editor_page_class->set_dates = event_page_set_dates;

	object_class->destroy = event_page_destroy;
}

/* Object initialization function for the event page */
static void
event_page_init (EventPage *epage)
{
	EventPagePrivate *priv;

	priv = g_new0 (EventPagePrivate, 1);
	epage->priv = priv;

	priv->xml = NULL;

	priv->main = NULL;
	priv->summary = NULL;
	priv->start_time = NULL;
	priv->end_time = NULL;
	priv->start_timezone = NULL;
	priv->end_timezone = NULL;
	priv->all_day_event = NULL;
	priv->description = NULL;
	priv->classification_public = NULL;
	priv->classification_private = NULL;
	priv->classification_confidential = NULL;
	priv->show_time_as_free = NULL;
	priv->show_time_as_busy = NULL;
	priv->contacts_btn = NULL;
	priv->contacts_box = NULL;
	priv->categories_btn = NULL;
	priv->categories = NULL;

	priv->updating = FALSE;
	priv->sync_timezones = FALSE;

	priv->corba_select_names = CORBA_OBJECT_NIL;
	priv->contacts_entry = NULL;
}

/* Destroy handler for the event page */
static void
event_page_destroy (GtkObject *object)
{
	EventPage *epage;
	EventPagePrivate *priv;

	g_return_if_fail (object != NULL);
	g_return_if_fail (IS_EVENT_PAGE (object));

	epage = EVENT_PAGE (object);
	priv = epage->priv;

	if (priv->corba_select_names != CORBA_OBJECT_NIL) {
		CORBA_Environment ev;

		CORBA_exception_init (&ev);
		CORBA_Object_release (priv->corba_select_names, &ev);
		CORBA_exception_free (&ev);
	}

	if (priv->xml) {
		gtk_object_unref (GTK_OBJECT (priv->xml));
		priv->xml = NULL;
	}

	g_free (priv);
	epage->priv = NULL;

	if (GTK_OBJECT_CLASS (parent_class)->destroy)
		(* GTK_OBJECT_CLASS (parent_class)->destroy) (object);
}



static const int classification_map[] = {
	CAL_COMPONENT_CLASS_PUBLIC,
	CAL_COMPONENT_CLASS_PRIVATE,
	CAL_COMPONENT_CLASS_CONFIDENTIAL,
	-1
};

static const int transparency_map[] = {
	CAL_COMPONENT_TRANSP_TRANSPARENT,
	CAL_COMPONENT_TRANSP_OPAQUE,
	-1
};

/* get_widget handler for the event page */
static GtkWidget *
event_page_get_widget (CompEditorPage *page)
{
	EventPage *epage;
	EventPagePrivate *priv;

	epage = EVENT_PAGE (page);
	priv = epage->priv;

	return priv->main;
}

/* focus_main_widget handler for the event page */
static void
event_page_focus_main_widget (CompEditorPage *page)
{
	EventPage *epage;
	EventPagePrivate *priv;

	epage = EVENT_PAGE (page);
	priv = epage->priv;

	gtk_widget_grab_focus (priv->summary);
}

/* Sets the 'All Day Event' flag to the given value (without emitting signals),
 * and shows or hides the widgets as appropriate. */
static void
set_all_day (EventPage *epage, gboolean all_day)
{
	EventPagePrivate *priv;

	priv = epage->priv;

	gtk_signal_handler_block_by_data (GTK_OBJECT (priv->all_day_event),
					  epage);
	e_dialog_toggle_set (priv->all_day_event, all_day);
	gtk_signal_handler_unblock_by_data (GTK_OBJECT (priv->all_day_event),
					    epage);

	e_date_edit_set_show_time (E_DATE_EDIT (priv->start_time), !all_day);
	e_date_edit_set_show_time (E_DATE_EDIT (priv->end_time), !all_day);

	/* DATE values do not have timezones, so we hide the fields. */
	if (all_day) {
		gtk_widget_hide (priv->start_timezone);
		gtk_widget_hide (priv->end_timezone);
	} else {
		gtk_widget_show (priv->start_timezone);
		gtk_widget_show (priv->end_timezone);
	}
}

static void
update_time (EventPage *epage, CalComponentDateTime *start_date, CalComponentDateTime *end_date)
{
	EventPagePrivate *priv;
	struct icaltimetype *start_tt, *end_tt;
	icaltimezone *start_zone = NULL, *end_zone = NULL;
	CalClientGetStatus status;
	gboolean all_day_event;

	priv = epage->priv;

	/* Note that if we are creating a new event, the timezones may not be
	   on the server, so we try to get the builtin timezone with the TZID
	   first. */
	start_zone = icaltimezone_get_builtin_timezone_from_tzid (start_date->tzid);
	if (!start_zone) {
		status = cal_client_get_timezone (COMP_EDITOR_PAGE (epage)->client,
						  start_date->tzid,
						  &start_zone);
		/* FIXME: Handle error better. */
		if (status != CAL_CLIENT_GET_SUCCESS)
			g_warning ("Couldn't get timezone from server: %s",
				   start_date->tzid ? start_date->tzid : "");
	}

	end_zone = icaltimezone_get_builtin_timezone_from_tzid (end_date->tzid);
	if (!end_zone) {
		status = cal_client_get_timezone (COMP_EDITOR_PAGE (epage)->client,
						  end_date->tzid,
						  &end_zone);
		/* FIXME: Handle error better. */
		if (status != CAL_CLIENT_GET_SUCCESS)
		  g_warning ("Couldn't get timezone from server: %s",
			     end_date->tzid ? end_date->tzid : "");
	}

	/* If both times are DATE values, we set the 'All Day Event' checkbox.
	   Also, if DTEND is after DTSTART, we subtract 1 day from it. */
	all_day_event = FALSE;
	start_tt = start_date->value;
	end_tt = end_date->value;
	if (start_tt->is_date && end_tt->is_date) {
		all_day_event = TRUE;
		if (icaltime_compare_date_only (*end_tt, *start_tt) > 0) {
			icaltime_adjust (end_tt, -1, 0, 0, 0);
		}
	}

	set_all_day (epage, all_day_event);

	/* If it is an all day event, we set both timezones to the current
	   timezone, so that if the user toggles the 'All Day Event' checkbox
	   the event uses the current timezone rather than none at all. */
	if (all_day_event) {
		char *location = calendar_config_get_timezone ();
		start_zone = end_zone = icaltimezone_get_builtin_timezone (location);
	}


	gtk_signal_handler_block_by_data (GTK_OBJECT (priv->start_time),
					  epage);
	gtk_signal_handler_block_by_data (GTK_OBJECT (priv->end_time), epage);

	e_date_edit_set_date (E_DATE_EDIT (priv->start_time), start_tt->year,
			      start_tt->month, start_tt->day);
	e_date_edit_set_time_of_day (E_DATE_EDIT (priv->start_time),
				     start_tt->hour, start_tt->minute);

	e_date_edit_set_date (E_DATE_EDIT (priv->end_time), end_tt->year,
			      end_tt->month, end_tt->day);
	e_date_edit_set_time_of_day (E_DATE_EDIT (priv->end_time),
				     end_tt->hour, end_tt->minute);

	gtk_signal_handler_unblock_by_data (GTK_OBJECT (priv->start_time),
					    epage);
	gtk_signal_handler_unblock_by_data (GTK_OBJECT (priv->end_time),
					    epage);

	/* Set the timezones, and set sync_timezones to TRUE if both timezones
	   are the same. */
	gtk_signal_handler_block_by_data (GTK_OBJECT (priv->start_timezone),
					  epage);
	gtk_signal_handler_block_by_data (GTK_OBJECT (priv->end_timezone), epage);
	
	e_timezone_entry_set_timezone (E_TIMEZONE_ENTRY (priv->start_timezone),
				       start_zone);
	e_timezone_entry_set_timezone (E_TIMEZONE_ENTRY (priv->end_timezone),
				       end_zone);

	gtk_signal_handler_unblock_by_data (GTK_OBJECT (priv->start_timezone),
					    epage);
	gtk_signal_handler_unblock_by_data (GTK_OBJECT (priv->end_timezone),
					    epage);

	priv->sync_timezones = (start_zone == end_zone) ? TRUE : FALSE;

}

/* Fills the widgets with default values */
static void
clear_widgets (EventPage *epage)
{
	EventPagePrivate *priv;

	priv = epage->priv;

	/* Summary, description */
	e_dialog_editable_set (priv->summary, NULL);
	e_dialog_editable_set (priv->description, NULL);

	/* Start and end times */
	gtk_signal_handler_block_by_data (GTK_OBJECT (priv->start_time),
					  epage);
	gtk_signal_handler_block_by_data (GTK_OBJECT (priv->end_time), epage);

	e_date_edit_set_time (E_DATE_EDIT (priv->start_time), 0);
	e_date_edit_set_time (E_DATE_EDIT (priv->end_time), 0);

	gtk_signal_handler_unblock_by_data (GTK_OBJECT (priv->start_time),
					    epage);
	gtk_signal_handler_unblock_by_data (GTK_OBJECT (priv->end_time),
					    epage);

	set_all_day (epage, FALSE);

	/* Classification */
	e_dialog_radio_set (priv->classification_public,
			    CAL_COMPONENT_CLASS_PRIVATE, classification_map);

	/* Show Time As (Transparency) */
	e_dialog_radio_set (priv->show_time_as_free,
			    CAL_COMPONENT_TRANSP_OPAQUE, transparency_map);

	/* Categories */
	e_dialog_editable_set (priv->categories, NULL);
}


static void
contacts_changed_cb (BonoboListener    *listener,
		     char              *event_name,
		     CORBA_any         *arg,
		     CORBA_Environment *ev,
		     gpointer           data)
{
	EventPage *epage;
	EventPagePrivate *priv;
	
	epage = EVENT_PAGE (data);
	priv = epage->priv;
	
#if 0
	g_print ("In contacts_changed_cb\n");
#endif

	if (!priv->updating)
		comp_editor_page_notify_changed (COMP_EDITOR_PAGE (epage));
}


/* fill_widgets handler for the event page */
static void
event_page_fill_widgets (CompEditorPage *page, CalComponent *comp)
{
	EventPage *epage;
	EventPagePrivate *priv;
	CalComponentText text;
	CalComponentClassification cl;
	CalComponentTransparency transparency;
	CalComponentDateTime start_date, end_date;
	const char *categories;
	GSList *l;

	g_return_if_fail (page->client != NULL);

	epage = EVENT_PAGE (page);
	priv = epage->priv;

	/* Don't send off changes during this time */
	priv->updating = TRUE;

	/* Clean the page */
	clear_widgets (epage);

	/* Summary, description(s) */

	cal_component_get_summary (comp, &text);
	e_dialog_editable_set (priv->summary, text.value);

	cal_component_get_description_list (comp, &l);
	if (l) {
		text = *(CalComponentText *)l->data;
		e_dialog_editable_set (priv->description, text.value);
	}
	cal_component_free_text_list (l);

	/* Start and end times */

	cal_component_get_dtstart (comp, &start_date);
	cal_component_get_dtend (comp, &end_date);

	update_time (epage, &start_date, &end_date);
	
	cal_component_free_datetime (&start_date);
	cal_component_free_datetime (&end_date);

	/* Classification */

	cal_component_get_classification (comp, &cl);

	switch (cl) {
	case CAL_COMPONENT_CLASS_PUBLIC:
	    	e_dialog_radio_set (priv->classification_public,
				    CAL_COMPONENT_CLASS_PUBLIC,
				    classification_map);
		break;

	case CAL_COMPONENT_CLASS_PRIVATE:
	    	e_dialog_radio_set (priv->classification_public,
				    CAL_COMPONENT_CLASS_PRIVATE,
				    classification_map);
		break;

	case CAL_COMPONENT_CLASS_CONFIDENTIAL:
	    	e_dialog_radio_set (priv->classification_public,
				    CAL_COMPONENT_CLASS_CONFIDENTIAL,
				    classification_map);
		break;

	default:
		/* What do do?  We can't g_assert_not_reached() since it is a
		 * value from an external file.
		 */
	}


	/* Show Time As (Transparency) */
	cal_component_get_transparency (comp, &transparency);
	switch (transparency) {
	case CAL_COMPONENT_TRANSP_TRANSPARENT:
	    	e_dialog_radio_set (priv->show_time_as_free,
				    CAL_COMPONENT_TRANSP_TRANSPARENT,
				    transparency_map);
		break;

	default:
	    	e_dialog_radio_set (priv->show_time_as_free,
				    CAL_COMPONENT_TRANSP_OPAQUE,
				    transparency_map);
		break;
	}



	/* Categories */
	cal_component_get_categories (comp, &categories);
	e_dialog_editable_set (priv->categories, categories);

	/* Contacts */
	comp_editor_contacts_to_widget (priv->contacts_entry, comp);

	/* We connect the contacts changed signal here, as we have to be a bit
	   more careful with it due to the use of Corba. The priv->updating
	   flag won't work as we won't get the changed event immediately.
	   FIXME: Unfortunately this doesn't work either. We never get the
	   changed event now. */
	comp_editor_connect_contacts_changed (priv->contacts_entry,
					      contacts_changed_cb, epage);

	priv->updating = FALSE;
}

/* fill_component handler for the event page */
static gboolean
event_page_fill_component (CompEditorPage *page, CalComponent *comp)
{
	EventPage *epage;
	EventPagePrivate *priv;
	CalComponentDateTime start_date, end_date;
	struct icaltimetype start_tt, end_tt;
	gboolean all_day_event, start_date_set, end_date_set;
	char *cat, *str;
	CalComponentClassification classif;
	CalComponentTransparency transparency;

	epage = EVENT_PAGE (page);
	priv = epage->priv;

	/* Summary */

	str = e_dialog_editable_get (priv->summary);
	if (!str || strlen (str) == 0)
		cal_component_set_summary (comp, NULL);
	else {
		CalComponentText text;

		text.value = str;
		text.altrep = NULL;

		cal_component_set_summary (comp, &text);
	}

	if (str)
		g_free (str);

	/* Description */

	str = e_dialog_editable_get (priv->description);
	if (!str || strlen (str) == 0)
		cal_component_set_description_list (comp, NULL);
	else {
		GSList l;
		CalComponentText text;

		text.value = str;
		text.altrep = NULL;
		l.data = &text;
		l.next = NULL;

		cal_component_set_description_list (comp, &l);
	}

	if (str)
		g_free (str);

	/* Dates */

	start_tt = icaltime_null_time ();
	start_date.value = &start_tt;
	start_date.tzid = NULL;

	end_tt = icaltime_null_time ();
	end_date.value = &end_tt;
	end_date.tzid = NULL;

	start_date_set = e_date_edit_get_date (E_DATE_EDIT (priv->start_time),
					       &start_tt.year,
					       &start_tt.month,
					       &start_tt.day);
	g_assert (start_date_set);

	end_date_set = e_date_edit_get_date (E_DATE_EDIT (priv->end_time),
					     &end_tt.year,
					     &end_tt.month,
					     &end_tt.day);
	g_assert (end_date_set);

	/* If the all_day toggle is set, we use DATE values for DTSTART and
	   DTEND. If not, we fetch the hour & minute from the widgets. */
	all_day_event = e_dialog_toggle_get (priv->all_day_event);

	if (all_day_event) {
		start_tt.is_date = TRUE;
		end_tt.is_date = TRUE;

		/* We have to add 1 day to DTEND, as it is not inclusive. */
		icaltime_adjust (&end_tt, 1, 0, 0, 0);
	} else {
		icaltimezone *start_zone, *end_zone;

		e_date_edit_get_time_of_day (E_DATE_EDIT (priv->start_time),
					     &start_tt.hour,
					     &start_tt.minute);
		e_date_edit_get_time_of_day (E_DATE_EDIT (priv->end_time),
					     &end_tt.hour,
					     &end_tt.minute);
		start_zone = e_timezone_entry_get_timezone (E_TIMEZONE_ENTRY (priv->start_timezone));
		start_date.tzid = icaltimezone_get_tzid (start_zone);
		end_zone = e_timezone_entry_get_timezone (E_TIMEZONE_ENTRY (priv->end_timezone));
		end_date.tzid = icaltimezone_get_tzid (end_zone);
	}

	cal_component_set_dtstart (comp, &start_date);
	cal_component_set_dtend (comp, &end_date);


	/* Categories */

	cat = e_dialog_editable_get (priv->categories);
	str = comp_editor_strip_categories (cat);
	if (cat)
		g_free (cat);

	cal_component_set_categories (comp, str);

	if (str)
		g_free (str);

	/* Classification */

	classif = e_dialog_radio_get (priv->classification_public,
				      classification_map);
	cal_component_set_classification (comp, classif);

	/* Show Time As (Transparency) */

	transparency = e_dialog_radio_get (priv->show_time_as_free,
					   transparency_map);
	cal_component_set_transparency (comp, transparency);

	/* Contacts */

	comp_editor_contacts_to_component (priv->contacts_entry, comp);

	return TRUE;
}

/* set_summary handler for the event page */
static void
event_page_set_summary (CompEditorPage *page, const char *summary)
{
	/* nothing */
}

static void
event_page_set_dates (CompEditorPage *page, CompEditorPageDates *dates)
{	
	update_time (EVENT_PAGE (page), dates->start, dates->end);
}



/* Gets the widgets from the XML file and returns if they are all available. */
static gboolean
get_widgets (EventPage *epage)
{
	CompEditorPage *page = COMP_EDITOR_PAGE (epage);
	EventPagePrivate *priv;
	GSList *accel_groups;
	GtkWidget *toplevel;

	priv = epage->priv;

#define GW(name) glade_xml_get_widget (priv->xml, name)

	priv->main = GW ("event-page");
	if (!priv->main)
		return FALSE;

	/* Get the GtkAccelGroup from the toplevel window, so we can install
	   it when the notebook page is mapped. */
	toplevel = gtk_widget_get_toplevel (priv->main);
	accel_groups = gtk_accel_groups_from_object (GTK_OBJECT (toplevel));
	if (accel_groups) {
		page->accel_group = accel_groups->data;
		gtk_accel_group_ref (page->accel_group);
	}

	gtk_widget_ref (priv->main);
	gtk_widget_unparent (priv->main);

	priv->summary = GW ("general-summary");

	priv->start_time = GW ("start-time");
	priv->end_time = GW ("end-time");
	priv->start_timezone = GW ("start-timezone");
	priv->end_timezone = GW ("end-timezone");
	priv->all_day_event = GW ("all-day-event");

	priv->description = GW ("description");

	priv->classification_public = GW ("classification-public");
	priv->classification_private = GW ("classification-private");
	priv->classification_confidential = GW ("classification-confidential");

	priv->show_time_as_free = GW ("show-time-as-free");
	priv->show_time_as_busy = GW ("show-time-as-busy");

	priv->contacts_btn = GW ("contacts-button");
	priv->contacts_box = GW ("contacts-box");

	priv->categories_btn = GW ("categories-button");
	priv->categories = GW ("categories");

#undef GW

	return (priv->summary
		&& priv->start_time
		&& priv->end_time
		&& priv->start_timezone
		&& priv->end_timezone
		&& priv->all_day_event
		&& priv->description
		&& priv->classification_public
		&& priv->classification_private
		&& priv->classification_confidential
		&& priv->show_time_as_free
		&& priv->show_time_as_busy
		&& priv->contacts_btn
		&& priv->contacts_box
		&& priv->categories_btn
		&& priv->categories);
}

/* Callback used when the summary changes; we emit the notification signal. */
static void
summary_changed_cb (GtkEditable *editable, gpointer data)
{
	EventPage *epage;
	EventPagePrivate *priv;
	gchar *summary;
	
	epage = EVENT_PAGE (data);
	priv = epage->priv;
	
	if (priv->updating)
		return;
	
	summary = e_dialog_editable_get (GTK_WIDGET (editable));
	comp_editor_page_notify_summary_changed (COMP_EDITOR_PAGE (epage),
						 summary);
	g_free (summary);
}


/* Note that this assumes that the start_tt and end_tt passed to it are the
   dates visible to the user. For DATE values, we have to add 1 day to the
   end_tt before emitting the signal. */
static void
notify_dates_changed (EventPage *epage, struct icaltimetype *start_tt,
		      struct icaltimetype *end_tt)
{
	EventPagePrivate *priv;
	CompEditorPageDates dates;
	CalComponentDateTime start_dt, end_dt;
	gboolean all_day_event;
	icaltimezone *start_zone = NULL, *end_zone = NULL;

	priv = epage->priv;
	
	all_day_event = e_dialog_toggle_get (priv->all_day_event);

	start_dt.value = start_tt;
	end_dt.value = end_tt;

	if (all_day_event) {
		/* The actual DTEND is 1 day after the displayed date for
		   DATE values. */
		icaltime_adjust (end_tt, 1, 0, 0, 0);
	} else {
		start_zone = e_timezone_entry_get_timezone (E_TIMEZONE_ENTRY (priv->start_timezone));
		end_zone = e_timezone_entry_get_timezone (E_TIMEZONE_ENTRY (priv->end_timezone));
	}

	start_dt.tzid = start_zone ? icaltimezone_get_tzid (start_zone) : NULL;
	end_dt.tzid = end_zone ? icaltimezone_get_tzid (end_zone) : NULL;

	dates.start = &start_dt;
	dates.end = &end_dt;

	dates.due = NULL;
	dates.complete = NULL;

	comp_editor_page_notify_dates_changed (COMP_EDITOR_PAGE (epage),
					       &dates);
}


static gboolean
check_start_before_end (struct icaltimetype *start_tt,
			icaltimezone *start_zone,
			struct icaltimetype *end_tt,
			icaltimezone *end_zone,
			gboolean adjust_end_time)
{
	struct icaltimetype end_tt_copy;
	int cmp;

	/* Convert the end time to the same timezone as the start time. */
	end_tt_copy = *end_tt;
	icaltimezone_convert_time (&end_tt_copy, end_zone, start_zone);

	/* Now check if the start time is after the end time. If it is,
	   we need to modify one of the times. */
	cmp = icaltime_compare (*start_tt, end_tt_copy);
	if (cmp > 0) {
		if (adjust_end_time) {
			/* Modify the end time, to be the start + 1 hour. */
			*end_tt = *start_tt;
			icaltime_adjust (end_tt, 0, 1, 0, 0);
			icaltimezone_convert_time (end_tt, start_zone,
						   end_zone);
		} else {
			/* Modify the start time, to be the end - 1 hour. */
			*start_tt = *end_tt;
			icaltime_adjust (start_tt, 0, -1, 0, 0);
			icaltimezone_convert_time (start_tt, end_zone,
						   start_zone);
		}
		return TRUE;
	}

	return FALSE;
}


/*
 * This is called whenever the start or end dates or timezones is changed.
 * It makes sure that the start date < end date. It also emits the notification
 * signals so the other event editor pages update their labels etc.
 *
 * If adjust_end_time is TRUE, if the start time < end time it will adjust
 * the end time. If FALSE it will adjust the start time. If the user sets the
 * start or end time, the other time is adjusted to make it valid.
 */
static void
times_updated (EventPage *epage, gboolean adjust_end_time)
{
	EventPagePrivate *priv;
	struct icaltimetype start_tt = icaltime_null_time();
	struct icaltimetype end_tt = icaltime_null_time();
	gboolean date_set, all_day_event;
	gboolean set_start_date = FALSE, set_end_date = FALSE;
	icaltimezone *start_zone, *end_zone;
	
	priv = epage->priv;

	if (priv->updating)
		return;

	/* Fetch the start and end times and timezones from the widgets. */
	all_day_event = e_dialog_toggle_get (priv->all_day_event);

	date_set = e_date_edit_get_date (E_DATE_EDIT (priv->start_time),
					 &start_tt.year,
					 &start_tt.month,
					 &start_tt.day);
	g_assert (date_set);

	date_set = e_date_edit_get_date (E_DATE_EDIT (priv->end_time),
					 &end_tt.year,
					 &end_tt.month,
					 &end_tt.day);
	g_assert (date_set);

	if (all_day_event) {
		/* All Day Events are simple. We just compare the dates and if
		   start > end we copy one of them to the other. */
		int cmp = icaltime_compare_date_only (start_tt, end_tt);
		if (cmp > 0) {
			if (adjust_end_time) {
				end_tt = start_tt;
				set_end_date = TRUE;
			} else {
				start_tt = end_tt;
				set_start_date = TRUE;
			}
		}

		start_tt.is_date = TRUE;
		end_tt.is_date = TRUE;
	} else {
		/* For DATE-TIME events, we have to convert to the same
		   timezone before comparing. */
		e_date_edit_get_time_of_day (E_DATE_EDIT (priv->start_time),
					     &start_tt.hour,
					     &start_tt.minute);
		e_date_edit_get_time_of_day (E_DATE_EDIT (priv->end_time),
					     &end_tt.hour,
					     &end_tt.minute);

		start_zone = e_timezone_entry_get_timezone (E_TIMEZONE_ENTRY (priv->start_timezone));
		end_zone = e_timezone_entry_get_timezone (E_TIMEZONE_ENTRY (priv->end_timezone));

		if (check_start_before_end (&start_tt, start_zone,
					    &end_tt, end_zone,
					    adjust_end_time)) {
			if (adjust_end_time)
				set_end_date = TRUE;
			else
				set_start_date = TRUE;
		}
	}


	if (set_start_date) {
		gtk_signal_handler_block_by_data (GTK_OBJECT (priv->start_time), epage);
		e_date_edit_set_date (E_DATE_EDIT (priv->start_time),
				      start_tt.year, start_tt.month,
				      start_tt.day);
		e_date_edit_set_time_of_day (E_DATE_EDIT (priv->start_time),
					     start_tt.hour, start_tt.minute);
		gtk_signal_handler_unblock_by_data (GTK_OBJECT (priv->start_time), epage);
	}

	if (set_end_date) {
		gtk_signal_handler_block_by_data (GTK_OBJECT (priv->end_time), epage);
		e_date_edit_set_date (E_DATE_EDIT (priv->end_time),
				      end_tt.year, end_tt.month, end_tt.day);
		e_date_edit_set_time_of_day (E_DATE_EDIT (priv->end_time),
					     end_tt.hour, end_tt.minute);
		gtk_signal_handler_unblock_by_data (GTK_OBJECT (priv->end_time), epage);
	}

	/* Notify upstream */
	notify_dates_changed (epage, &start_tt, &end_tt);
}

/* Callback used when the start or end date widgets change.  We check that the
 * start date < end date and we set the "all day event" button as appropriate.
 */
static void
date_changed_cb (GtkWidget *dedit, gpointer data)
{
	EventPage *epage;
	
	epage = EVENT_PAGE (data);

	times_updated (epage, dedit == epage->priv->start_time);
}


/* Callback used when the start timezone is changed. If sync_timezones is set,
 * we set the end timezone to the same value. It also updates the start time
 * labels on the other notebook pages.
 */
static void
start_timezone_changed_cb (GtkWidget *widget, gpointer data)
{
	EventPage *epage;
	EventPagePrivate *priv;
	icaltimezone *zone;

	epage = EVENT_PAGE (data);
	priv = epage->priv;

	if (priv->sync_timezones) {
		zone = e_timezone_entry_get_timezone (E_TIMEZONE_ENTRY (priv->start_timezone));
		priv->updating = TRUE;
		e_timezone_entry_set_timezone (E_TIMEZONE_ENTRY (priv->end_timezone), zone);
		priv->updating = FALSE;
	}

	times_updated (epage, TRUE);
}


/* Callback used when the end timezone is changed. It checks if the end
 * timezone is the same as the start timezone and sets sync_timezones if so.
 */
static void
end_timezone_changed_cb (GtkWidget *widget, gpointer data)
{
	EventPage *epage;
	EventPagePrivate *priv;
	icaltimezone *start_zone, *end_zone;
	
	epage = EVENT_PAGE (data);
	priv = epage->priv;

	start_zone = e_timezone_entry_get_timezone (E_TIMEZONE_ENTRY (priv->start_timezone));
	end_zone = e_timezone_entry_get_timezone (E_TIMEZONE_ENTRY (priv->end_timezone));

	priv->sync_timezones = (start_zone == end_zone) ? TRUE : FALSE;

	times_updated (epage, TRUE);
}

/* Callback: all day event button toggled.
 * Note that this should only be called when the user explicitly toggles the
 * button. Be sure to block this handler when the toggle button's state is set
 * within the code.
 */
static void
all_day_event_toggled_cb (GtkWidget *toggle, gpointer data)
{
	EventPage *epage;
	EventPagePrivate *priv;
	gboolean all_day;
	struct icaltimetype start_tt = icaltime_null_time();
	struct icaltimetype end_tt = icaltime_null_time();
	gboolean date_set;

	epage = EVENT_PAGE (data);
	priv = epage->priv;

	/* When the all_day toggle is turned on, the start date is
	 * rounded down to the start of the day, and end date is
	 * rounded down to the start of the day on which the event
	 * ends. The event is then taken to be inclusive of the days
	 * between the start and end days.  Note that if the event end
	 * is at midnight, we round it down to the previous day, so the
	 * event times stay the same.
	 *
	 * When the all_day_toggle is turned off, then if the event is within
	 * one day, we set the event start to the start of the working day,
	 * and set the event end to one hour after it. If the event is longer
	 * than one day, we set the event end to the end of the day it is on,
	 * so that the actual event times remain the same.
	 *
	 * This may need tweaking to work well with different timezones used
	 * in the event start & end.
	 */
	all_day = GTK_TOGGLE_BUTTON (toggle)->active;

	set_all_day (epage, all_day);

	date_set = e_date_edit_get_date (E_DATE_EDIT (priv->start_time),
					 &start_tt.year,
					 &start_tt.month,
					 &start_tt.day);
	e_date_edit_get_time_of_day (E_DATE_EDIT (priv->start_time),
				     &start_tt.hour,
				     &start_tt.minute);
	g_assert (date_set);

	date_set = e_date_edit_get_date (E_DATE_EDIT (priv->end_time),
					 &end_tt.year,
					 &end_tt.month,
					 &end_tt.day);
	e_date_edit_get_time_of_day (E_DATE_EDIT (priv->end_time),
				     &end_tt.hour,
				     &end_tt.minute);
	g_assert (date_set);

	if (all_day) {
		/* Round down to the start of the day. */
		start_tt.hour = 0;
		start_tt.minute  = 0;
		start_tt.second  = 0;
		start_tt.is_date = TRUE;

		/* Round down to the start of the day, or the start of the
		   previous day if it is midnight. */
		icaltime_adjust (&end_tt, 0, 0, 0, -1);
		end_tt.hour = 0;
		end_tt.minute  = 0;
		end_tt.second  = 0;
		end_tt.is_date = TRUE;
	} else {
		icaltimezone *start_zone, *end_zone;

		if (end_tt.year == start_tt.year
		    && end_tt.month == start_tt.month
		    && end_tt.day == start_tt.day) {
			/* The event is within one day, so we set the event
			   start to the start of the working day, and the end
			   to one hour later. */
			start_tt.hour = calendar_config_get_day_start_hour ();
			start_tt.minute  = calendar_config_get_day_start_minute ();
			start_tt.second  = 0;

			end_tt = start_tt;
			icaltime_adjust (&end_tt, 0, 1, 0, 0);
		} else {
			/* The event is longer than 1 day, so we keep exactly
			   the same times, just using DATE-TIME rather than
			   DATE. */
			icaltime_adjust (&end_tt, 1, 0, 0, 0);
		}

		/* Make sure that end > start using the timezones. */
		start_zone = e_timezone_entry_get_timezone (E_TIMEZONE_ENTRY (priv->start_timezone));
		end_zone = e_timezone_entry_get_timezone (E_TIMEZONE_ENTRY (priv->end_timezone));
		check_start_before_end (&start_tt, start_zone,
					&end_tt, end_zone,
					TRUE);
	}

	gtk_signal_handler_block_by_data (GTK_OBJECT (priv->start_time),
					  epage);
	gtk_signal_handler_block_by_data (GTK_OBJECT (priv->end_time),
					  epage);

	e_date_edit_set_date (E_DATE_EDIT (priv->start_time), start_tt.year,
			      start_tt.month, start_tt.day);
	e_date_edit_set_time_of_day (E_DATE_EDIT (priv->start_time),
				     start_tt.hour, start_tt.minute);

	e_date_edit_set_date (E_DATE_EDIT (priv->end_time), end_tt.year,
			      end_tt.month, end_tt.day);
	e_date_edit_set_time_of_day (E_DATE_EDIT (priv->end_time),
				     end_tt.hour, end_tt.minute);

	gtk_signal_handler_unblock_by_data (GTK_OBJECT (priv->start_time),
					    epage);
	gtk_signal_handler_unblock_by_data (GTK_OBJECT (priv->end_time),
					    epage);

	/* Notify upstream */
	notify_dates_changed (epage, &start_tt, &end_tt);
}

/* Callback used when the contacts button is clicked; we must bring up the
 * contact list dialog.
 */
static void
contacts_clicked_cb (GtkWidget *button, gpointer data)
{
	EventPage *epage;
	EventPagePrivate *priv;

	epage = EVENT_PAGE (data);
	priv = epage->priv;

	comp_editor_show_contacts_dialog (priv->corba_select_names);

	/* FIXME: Currently we aren't getting the changed event from the
	   SelectNames component correctly, so we aren't saving the event
	   if just the contacts are changed. To work around that, we assume
	   that if the contacts button is clicked it is changed. */
	comp_editor_page_notify_changed (COMP_EDITOR_PAGE (epage));
}

/* Callback used when the categories button is clicked; we must bring up the
 * category list dialog.
 */
static void
categories_clicked_cb (GtkWidget *button, gpointer data)
{
	EventPage *epage;
	EventPagePrivate *priv;
	GtkWidget *entry;

	epage = EVENT_PAGE (data);
	priv = epage->priv;

	entry = priv->categories;
	e_categories_config_open_dialog_for_entry (GTK_ENTRY (entry));
}

/* This is called when any field is changed; it notifies upstream. */
static void
field_changed_cb (GtkWidget *widget, gpointer data)
{
	EventPage *epage;
	EventPagePrivate *priv;
	
	epage = EVENT_PAGE (data);
	priv = epage->priv;
	
	if (!priv->updating)
		comp_editor_page_notify_changed (COMP_EDITOR_PAGE (epage));
}

/* Hooks the widget signals */
static gboolean
init_widgets (EventPage *epage)
{
	EventPagePrivate *priv;
	char *location;
	icaltimezone *zone;

	priv = epage->priv;

	/* Make sure the EDateEdit widgets use our timezones to get the
	   current time. */
	e_date_edit_set_get_time_callback (E_DATE_EDIT (priv->start_time),
					   (EDateEditGetTimeCallback) comp_editor_get_current_time,
					   epage, NULL);
	e_date_edit_set_get_time_callback (E_DATE_EDIT (priv->end_time),
					   (EDateEditGetTimeCallback) comp_editor_get_current_time,
					   epage, NULL);

	/* Summary */
	gtk_signal_connect (GTK_OBJECT (priv->summary), "changed",
			    GTK_SIGNAL_FUNC (summary_changed_cb), epage);

	/* Description - turn on word wrap. */
	gtk_text_set_word_wrap (GTK_TEXT (priv->description), TRUE);

	/* Start and end times */
	gtk_signal_connect (GTK_OBJECT (priv->start_time), "changed",
			    GTK_SIGNAL_FUNC (date_changed_cb), epage);
	gtk_signal_connect (GTK_OBJECT (priv->end_time), "changed",
			    GTK_SIGNAL_FUNC (date_changed_cb), epage);

	gtk_signal_connect (GTK_OBJECT (priv->start_timezone), "changed",
			    GTK_SIGNAL_FUNC (start_timezone_changed_cb), epage);
	gtk_signal_connect (GTK_OBJECT (priv->end_timezone), "changed",
			    GTK_SIGNAL_FUNC (end_timezone_changed_cb), epage);

	gtk_signal_connect (GTK_OBJECT (priv->all_day_event), "toggled",
			    GTK_SIGNAL_FUNC (all_day_event_toggled_cb), epage);

	/* Contacts button */
	gtk_signal_connect (GTK_OBJECT (priv->contacts_btn), "clicked",
			    GTK_SIGNAL_FUNC (contacts_clicked_cb), epage);

	/* Categories button */
	gtk_signal_connect (GTK_OBJECT (priv->categories_btn), "clicked",
			    GTK_SIGNAL_FUNC (categories_clicked_cb), epage);

	/* Connect the default signal handler to use to make sure we notify
	 * upstream of changes to the widget values.
	 */

	gtk_signal_connect (GTK_OBJECT (priv->summary), "changed",
			    GTK_SIGNAL_FUNC (field_changed_cb), epage);
	gtk_signal_connect (GTK_OBJECT (priv->start_time), "changed",
			    GTK_SIGNAL_FUNC (field_changed_cb), epage);
	gtk_signal_connect (GTK_OBJECT (priv->end_time), "changed",
			    GTK_SIGNAL_FUNC (field_changed_cb), epage);
	gtk_signal_connect (GTK_OBJECT (priv->start_timezone), "changed",
			    GTK_SIGNAL_FUNC (field_changed_cb), epage);
	gtk_signal_connect (GTK_OBJECT (priv->end_timezone), "changed",
			    GTK_SIGNAL_FUNC (field_changed_cb), epage);
	gtk_signal_connect (GTK_OBJECT (priv->all_day_event), "toggled",
			    GTK_SIGNAL_FUNC (field_changed_cb), epage);
	gtk_signal_connect (GTK_OBJECT (priv->description), "changed",
			    GTK_SIGNAL_FUNC (field_changed_cb), epage);
	gtk_signal_connect (GTK_OBJECT (priv->classification_public),
			    "toggled", GTK_SIGNAL_FUNC (field_changed_cb),
			    epage);
	gtk_signal_connect (GTK_OBJECT (priv->classification_private),
			    "toggled", GTK_SIGNAL_FUNC (field_changed_cb),
			    epage);
	gtk_signal_connect (GTK_OBJECT (priv->classification_confidential),
			    "toggled", GTK_SIGNAL_FUNC (field_changed_cb),
			    epage);
	gtk_signal_connect (GTK_OBJECT (priv->show_time_as_free),
			    "toggled", GTK_SIGNAL_FUNC (field_changed_cb),
			    epage);
	gtk_signal_connect (GTK_OBJECT (priv->show_time_as_busy),
			    "toggled", GTK_SIGNAL_FUNC (field_changed_cb),
			    epage);
	gtk_signal_connect (GTK_OBJECT (priv->categories), "changed",
			    GTK_SIGNAL_FUNC (field_changed_cb), epage);

	/* Create the contacts entry, a corba control from the address book. */
	priv->corba_select_names = comp_editor_create_contacts_component ();
	if (priv->corba_select_names == CORBA_OBJECT_NIL)
		return FALSE;

	priv->contacts_entry = comp_editor_create_contacts_control (priv->corba_select_names);
	if (priv->contacts_entry == NULL)
		return FALSE;

	gtk_container_add (GTK_CONTAINER (priv->contacts_box),
			   priv->contacts_entry);

	/* Set the default timezone, so the timezone entry may be hidden. */
	location = calendar_config_get_timezone ();
	zone = icaltimezone_get_builtin_timezone (location);
	e_timezone_entry_set_default_timezone (E_TIMEZONE_ENTRY (priv->start_timezone), zone);
	e_timezone_entry_set_default_timezone (E_TIMEZONE_ENTRY (priv->end_timezone), zone);

	return TRUE;
}



/**
 * event_page_construct:
 * @epage: An event page.
 * 
 * Constructs an event page by loading its Glade data.
 * 
 * Return value: The same object as @epage, or NULL if the widgets could not be
 * created.
 **/
EventPage *
event_page_construct (EventPage *epage)
{
	EventPagePrivate *priv;

	priv = epage->priv;

	priv->xml = glade_xml_new (EVOLUTION_GLADEDIR "/event-page.glade", 
				   NULL);
	if (!priv->xml) {
		g_message ("event_page_construct(): " 
			   "Could not load the Glade XML file!");
		return NULL;
	}

	if (!get_widgets (epage)) {
		g_message ("event_page_construct(): " 
			   "Could not find all widgets in the XML file!");
		return NULL;
	}

	if (!init_widgets (epage)) {
		g_message ("event_page_construct(): " 
			   "Could not initialize the widgets!");
		return NULL;
	}

	return epage;
}

/**
 * event_page_new:
 * 
 * Creates a new event page.
 * 
 * Return value: A newly-created event page, or NULL if the page could
 * not be created.
 **/
EventPage *
event_page_new (void)
{
	EventPage *epage;

	epage = gtk_type_new (TYPE_EVENT_PAGE);
	if (!event_page_construct (epage)) {
		gtk_object_unref (GTK_OBJECT (epage));
		return NULL;
	}

	return epage;
}

GtkWidget *make_date_edit (void);

GtkWidget *
make_date_edit (void)
{
	return comp_editor_new_date_edit (TRUE, TRUE, TRUE);
}

GtkWidget *make_timezone_entry (void);

GtkWidget *
make_timezone_entry (void)
{
	return e_timezone_entry_new ();
}
