/* Evolution calendar - Main page of the task editor dialog
 *
 * Copyright (C) 2001 Ximian, Inc.
 *
 * Authors: Federico Mena-Quintero <federico@ximian.com>
 *          Miguel de Icaza <miguel@ximian.com>
 *          Seth Alves <alves@hungry.com>
 *          JP Rosevear <jpr@ximian.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
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

#include <string.h>
#include <gtk/gtksignal.h>
#include <gtk/gtktogglebutton.h>
#include <gtk/gtkspinbutton.h>
#include <gtk/gtkoptionmenu.h>
#include <glade/glade.h>
#include <gal/widgets/e-unicode.h>
#include <gal/widgets/e-categories.h>
#include <widgets/misc/e-dateedit.h>
#include "e-util/e-dialog-widgets.h"
#include "e-util/e-categories-config.h"
#include "../e-timezone-entry.h"
#include "comp-editor-util.h"
#include "task-page.h"



/* Private part of the TaskPage structure */
struct _TaskPagePrivate {
	/* Glade XML data */
	GladeXML *xml;

	/* Widgets from the Glade file */
	GtkWidget *main;

	GtkWidget *summary;

	GtkWidget *due_date;
	GtkWidget *start_date;
	GtkWidget *due_timezone;
	GtkWidget *start_timezone;

	GtkWidget *percent_complete;

	GtkWidget *status;
	GtkWidget *priority;

	GtkWidget *description;

	GtkWidget *classification_public;
	GtkWidget *classification_private;
	GtkWidget *classification_confidential;

	GtkWidget *contacts_btn;	
	GtkWidget *contacts;

	GtkWidget *categories_btn;
	GtkWidget *categories;

	gboolean updating;
};

/* Note that these two arrays must match. */
static const int status_map[] = {
	ICAL_STATUS_NEEDSACTION,
	ICAL_STATUS_INPROCESS,
	ICAL_STATUS_COMPLETED,
	ICAL_STATUS_CANCELLED,
	-1
};

typedef enum {
	PRIORITY_HIGH,
	PRIORITY_NORMAL,
	PRIORITY_LOW,
	PRIORITY_UNDEFINED,
} TaskEditorPriority;

static const int priority_map[] = {
	PRIORITY_HIGH,
	PRIORITY_NORMAL,
	PRIORITY_LOW,
	PRIORITY_UNDEFINED,
	-1
};

static const int classification_map[] = {
	CAL_COMPONENT_CLASS_PUBLIC,
	CAL_COMPONENT_CLASS_PRIVATE,
	CAL_COMPONENT_CLASS_CONFIDENTIAL,
	-1
};



static void task_page_class_init (TaskPageClass *class);
static void task_page_init (TaskPage *tpage);
static void task_page_destroy (GtkObject *object);

static GtkWidget *task_page_get_widget (CompEditorPage *page);
static void task_page_focus_main_widget (CompEditorPage *page);
static void task_page_fill_widgets (CompEditorPage *page, CalComponent *comp);
static void task_page_fill_component (CompEditorPage *page, CalComponent *comp);
static void task_page_set_summary (CompEditorPage *page, const char *summary);
static void task_page_set_dates (CompEditorPage *page, CompEditorPageDates *dates);

static CompEditorPageClass *parent_class = NULL;



/**
 * task_page_get_type:
 * 
 * Registers the #TaskPage class if necessary, and returns the type ID
 * associated to it.
 * 
 * Return value: The type ID of the #TaskPage class.
 **/
GtkType
task_page_get_type (void)
{
	static GtkType task_page_type;

	if (!task_page_type) {
		static const GtkTypeInfo task_page_info = {
			"TaskPage",
			sizeof (TaskPage),
			sizeof (TaskPageClass),
			(GtkClassInitFunc) task_page_class_init,
			(GtkObjectInitFunc) task_page_init,
			NULL, /* reserved_1 */
			NULL, /* reserved_2 */
			(GtkClassInitFunc) NULL
		};

		task_page_type = gtk_type_unique (TYPE_COMP_EDITOR_PAGE,
						  &task_page_info);
	}

	return task_page_type;
}

/* Class initialization function for the task page */
static void
task_page_class_init (TaskPageClass *class)
{
	CompEditorPageClass *editor_page_class;
	GtkObjectClass *object_class;

	editor_page_class = (CompEditorPageClass *) class;
	object_class = (GtkObjectClass *) class;

	parent_class = gtk_type_class (TYPE_COMP_EDITOR_PAGE);

	editor_page_class->get_widget = task_page_get_widget;
	editor_page_class->focus_main_widget = task_page_focus_main_widget;
	editor_page_class->fill_widgets = task_page_fill_widgets;
	editor_page_class->fill_component = task_page_fill_component;
	editor_page_class->set_summary = task_page_set_summary;
	editor_page_class->set_dates = task_page_set_dates;

	object_class->destroy = task_page_destroy;
}

/* Object initialization function for the task page */
static void
task_page_init (TaskPage *tpage)
{
	TaskPagePrivate *priv;

	priv = g_new0 (TaskPagePrivate, 1);
	tpage->priv = priv;

	priv->xml = NULL;

	priv->main = NULL;
	priv->summary = NULL;
	priv->due_date = NULL;
	priv->start_date = NULL;
	priv->due_timezone = NULL;
	priv->start_timezone = NULL;
	priv->percent_complete = NULL;
	priv->status = NULL;
	priv->description = NULL;
	priv->classification_public = NULL;
	priv->classification_private = NULL;
	priv->classification_confidential = NULL;
	priv->contacts_btn = NULL;
	priv->contacts = NULL;
	priv->categories_btn = NULL;
	priv->categories = NULL;

	priv->updating = FALSE;
}

/* Destroy handler for the task page */
static void
task_page_destroy (GtkObject *object)
{
	TaskPage *tpage;
	TaskPagePrivate *priv;

	g_return_if_fail (object != NULL);
	g_return_if_fail (IS_TASK_PAGE (object));

	tpage = TASK_PAGE (object);
	priv = tpage->priv;

	if (priv->xml) {
		gtk_object_unref (GTK_OBJECT (priv->xml));
		priv->xml = NULL;
	}

	g_free (priv);
	tpage->priv = NULL;

	if (GTK_OBJECT_CLASS (parent_class)->destroy)
		(* GTK_OBJECT_CLASS (parent_class)->destroy) (object);
}



/* get_widget handler for the task page */
static GtkWidget *
task_page_get_widget (CompEditorPage *page)
{
	TaskPage *tpage;
	TaskPagePrivate *priv;

	tpage = TASK_PAGE (page);
	priv = tpage->priv;

	return priv->main;
}

/* focus_main_widget handler for the task page */
static void
task_page_focus_main_widget (CompEditorPage *page)
{
	TaskPage *tpage;
	TaskPagePrivate *priv;

	tpage = TASK_PAGE (page);
	priv = tpage->priv;

	gtk_widget_grab_focus (priv->summary);
}

/* Fills the widgets with default values */
static void
clear_widgets (TaskPage *tpage)
{
	TaskPagePrivate *priv;

	priv = tpage->priv;

	/* Summary, description */
	e_dialog_editable_set (priv->summary, NULL);
	e_dialog_editable_set (priv->description, NULL);

	/* Start, due times */
	e_date_edit_set_time (E_DATE_EDIT (priv->start_date), 0);
	e_date_edit_set_time (E_DATE_EDIT (priv->due_date), 0);

	/* Classification */
	e_dialog_radio_set (priv->classification_public,
			    CAL_COMPONENT_CLASS_PRIVATE, classification_map);

	/* Status, priority, complete percent */
	e_dialog_spin_set (priv->percent_complete, 0.0);
	e_dialog_option_menu_set (priv->status, 0, status_map);
	e_dialog_option_menu_set (priv->priority, 0, priority_map);
	
	/* Categories */
	e_dialog_editable_set (priv->categories, NULL);
}

static TaskEditorPriority
priority_value_to_index (int priority_value)
{
	TaskEditorPriority retval;

	if (priority_value == 0)
		retval = PRIORITY_UNDEFINED;
	else if (priority_value <= 4)
		retval = PRIORITY_HIGH;
	else if (priority_value == 5)
		retval = PRIORITY_NORMAL;
	else
		retval = PRIORITY_LOW;

	return retval;
}

static int
priority_index_to_value (TaskEditorPriority priority)
{
	int retval;

	switch (priority) {
	case PRIORITY_UNDEFINED:
		retval = 0;
		break;
	case PRIORITY_HIGH:
		retval = 3;
		break;
	case PRIORITY_NORMAL:
		retval = 5;
		break;
	case PRIORITY_LOW:
		retval = 7;
		break;
	default:
		retval = -1;
		g_assert_not_reached ();
		break;
	}

	return retval;
}

/* Decode the radio button group for classifications */
static CalComponentClassification
classification_get (GtkWidget *widget)
{
	return e_dialog_radio_get (widget, classification_map);
}

/* fill_widgets handler for the task page */
static void
task_page_fill_widgets (CompEditorPage *page, CalComponent *comp)
{
	TaskPage *tpage;
	TaskPagePrivate *priv;
	CalComponentText text;
	CalComponentDateTime d;
	CalComponentClassification cl;
	CalClientGetStatus get_tz_status;
	GSList *l;
	int *priority_value, *percent;
	icalproperty_status status;
	TaskEditorPriority priority;
	const char *categories;
	icaltimezone *zone;

	tpage = TASK_PAGE (page);
	priv = tpage->priv;

	priv->updating = TRUE;
	
	/* Clean the screen */
	clear_widgets (tpage);

        /* Summary, description(s) */
	cal_component_get_summary (comp, &text);
	e_dialog_editable_set (priv->summary, text.value);

	cal_component_get_description_list (comp, &l);
	if (l) {
		text = *(CalComponentText *)l->data;
		e_dialog_editable_set (priv->description, text.value);
	} else {
		e_dialog_editable_set (priv->description, NULL);
	}
	cal_component_free_text_list (l);

	/* Due Date. */
	cal_component_get_due (comp, &d);
	if (d.value) {
		struct icaltimetype *due_tt = d.value;
		e_date_edit_set_date (E_DATE_EDIT (priv->due_date),
				      due_tt->year, due_tt->month,
				      due_tt->day);
		e_date_edit_set_time_of_day (E_DATE_EDIT (priv->due_date),
					     due_tt->hour, due_tt->minute);
	} else {
		e_date_edit_set_time (E_DATE_EDIT (priv->due_date), -1);
	}

	/* Note that if we are creating a new task, the timezones may not be
	   on the server, so we try to get the builtin timezone with the TZID
	   first. */
	zone = icaltimezone_get_builtin_timezone_from_tzid (d.tzid);
	if (!zone) {
		get_tz_status = cal_client_get_timezone (page->client, d.tzid,
							 &zone);
		/* FIXME: Handle error better. */
		if (get_tz_status != CAL_CLIENT_GET_SUCCESS)
		  g_warning ("Couldn't get timezone from server: %s",
			     d.tzid ? d.tzid : "");
	}
	e_timezone_entry_set_timezone (E_TIMEZONE_ENTRY (priv->due_timezone),
				       zone);

	cal_component_free_datetime (&d);


	/* Start Date. */
	cal_component_get_dtstart (comp, &d);
	if (d.value) {
		struct icaltimetype *start_tt = d.value;
		e_date_edit_set_date (E_DATE_EDIT (priv->start_date),
				      start_tt->year, start_tt->month,
				      start_tt->day);
		e_date_edit_set_time_of_day (E_DATE_EDIT (priv->start_date),
					     start_tt->hour, start_tt->minute);
	} else {
		e_date_edit_set_time (E_DATE_EDIT (priv->start_date), -1);
	}

	zone = icaltimezone_get_builtin_timezone_from_tzid (d.tzid);
	if (!zone) {
		get_tz_status = cal_client_get_timezone (page->client, d.tzid,
							 &zone);
		/* FIXME: Handle error better. */
		if (get_tz_status != CAL_CLIENT_GET_SUCCESS)
			g_warning ("Couldn't get timezone from server: %s",
				   d.tzid ? d.tzid : "");
	}
	e_timezone_entry_set_timezone (E_TIMEZONE_ENTRY (priv->start_timezone),
				       zone);

	cal_component_free_datetime (&d);


	/* Percent Complete. */
	cal_component_get_percent (comp, &percent);
	if (percent) {
		e_dialog_spin_set (priv->percent_complete, *percent);
		cal_component_free_percent (percent);
	} else {
		/* FIXME: Could check if task is completed and set 100%. */
		e_dialog_spin_set (priv->percent_complete, 0);
	}

	/* Status. */
	cal_component_get_status (comp, &status);
	if (status == ICAL_STATUS_NONE) {
		/* Try to user the percent value. */
		if (percent) {
			if (*percent == 0)
				status = ICAL_STATUS_NEEDSACTION;
			else if (*percent == 100)
				status = ICAL_STATUS_COMPLETED;
			else
				status = ICAL_STATUS_INPROCESS;
		} else
			status = ICAL_STATUS_NEEDSACTION;
	}
	e_dialog_option_menu_set (priv->status, status, status_map);

	/* Priority. */
	cal_component_get_priority (comp, &priority_value);
	if (priority_value) {
		priority = priority_value_to_index (*priority_value);
		cal_component_free_priority (priority_value);
	} else {
		priority = PRIORITY_UNDEFINED;
	}
	e_dialog_option_menu_set (priv->priority, priority, priority_map);


	/* Classification. */
	cal_component_get_classification (comp, &cl);

	switch (cl) {
	case CAL_COMPONENT_CLASS_PUBLIC:
	    	e_dialog_radio_set (priv->classification_public,
				    CAL_COMPONENT_CLASS_PUBLIC,
				    classification_map);
	case CAL_COMPONENT_CLASS_PRIVATE:
	    	e_dialog_radio_set (priv->classification_public,
				    CAL_COMPONENT_CLASS_PRIVATE,
				    classification_map);
	case CAL_COMPONENT_CLASS_CONFIDENTIAL:
	    	e_dialog_radio_set (priv->classification_public,
				    CAL_COMPONENT_CLASS_CONFIDENTIAL,
				    classification_map);
	default:
		/* What do do?  We can't g_assert_not_reached() since it is a
		 * value from an external file.
		 */
	}

	/* Categories */
	cal_component_get_categories (comp, &categories);
	e_dialog_editable_set (priv->categories, categories);

	priv->updating = FALSE;
}

/* fill_component handler for the task page */
static void
task_page_fill_component (CompEditorPage *page, CalComponent *comp)
{
	TaskPage *tpage;
	TaskPagePrivate *priv;
	CalComponentDateTime date;
	struct icaltimetype icaltime;
	icalproperty_status status;
	TaskEditorPriority priority;
	int priority_value, percent;
	char *cat;
	char *str;
	gboolean date_set;
	icaltimezone *zone;

	tpage = TASK_PAGE (page);
	priv = tpage->priv;

	/* Summary. */

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

	if (!str)
		g_free (str);

	/* Dates */

	icaltime = icaltime_null_time ();

	date.value = &icaltime;
	date.tzid = NULL;

	/* FIXME: We should use is_date at some point. */

	/* Due Date. */
	date_set = e_date_edit_get_date (E_DATE_EDIT (priv->due_date),
					 &icaltime.year,
					 &icaltime.month,
					 &icaltime.day);
	e_date_edit_get_time_of_day (E_DATE_EDIT (priv->due_date),
				     &icaltime.hour,
				     &icaltime.minute);
	if (date_set) {
		zone = e_timezone_entry_get_timezone (E_TIMEZONE_ENTRY (priv->due_timezone));
		if (zone)
			date.tzid = icaltimezone_get_tzid (zone);
		cal_component_set_due (comp, &date);
	} else {
		cal_component_set_due (comp, NULL);
	}

	/* Start Date. */
	date_set = e_date_edit_get_date (E_DATE_EDIT (priv->start_date),
					 &icaltime.year,
					 &icaltime.month,
					 &icaltime.day);
	e_date_edit_get_time_of_day (E_DATE_EDIT (priv->start_date),
				     &icaltime.hour,
				     &icaltime.minute);
	if (date_set) {
		zone = e_timezone_entry_get_timezone (E_TIMEZONE_ENTRY (priv->start_timezone));
		if (zone)
			date.tzid = icaltimezone_get_tzid (zone);
		cal_component_set_dtstart (comp, &date);
	} else {
		cal_component_set_dtstart (comp, NULL);
	}

	/* Percent Complete. */
	percent = e_dialog_spin_get_int (priv->percent_complete);
	cal_component_set_percent (comp, &percent);

	/* Status. */
	status = e_dialog_option_menu_get (priv->status, status_map);
	cal_component_set_status (comp, status);

	/* Priority. */
	priority = e_dialog_option_menu_get (priv->priority, priority_map);
	priority_value = priority_index_to_value (priority);
	cal_component_set_priority (comp, &priority_value);

	/* Classification. */
	cal_component_set_classification (comp, classification_get (priv->classification_public));

	/* Categories */
	cat = e_dialog_editable_get (priv->categories);
	cal_component_set_categories (comp, cat);

	if (cat)
		g_free (cat);
}

/* set_summary handler for the task page */
static void
task_page_set_summary (CompEditorPage *page, const char *summary)
{
	TaskPage *tpage;
	TaskPagePrivate *priv;

	tpage = TASK_PAGE (page);
	priv = tpage->priv;

	priv->updating = TRUE;
	e_utf8_gtk_entry_set_text (GTK_ENTRY (priv->summary), summary);
	priv->updating = FALSE;
}

static void
task_page_set_dates (CompEditorPage *page, CompEditorPageDates *dates)
{
	TaskPage *tpage;
	TaskPagePrivate *priv;

	tpage = TASK_PAGE (page);
	priv = tpage->priv;

	if (priv->updating)
	        return;

	priv->updating = TRUE;
	
	if (dates->complete) {
		if (icaltime_is_null_time (*dates->complete)) {
			/* If the 'Completed Date' is set to 'None',
			   we set the status to 'Not Started' and the
			   percent-complete to 0.  The task may
			   actually be partially-complete, but we
			   leave it to the user to set those
			   fields. */
			e_dialog_option_menu_set (priv->status,
						  ICAL_STATUS_NEEDSACTION,
						  status_map);
			e_dialog_spin_set (priv->percent_complete, 0);
		} else {
			e_dialog_option_menu_set (priv->status,
						  ICAL_STATUS_COMPLETED,
						  status_map);
			e_dialog_spin_set (priv->percent_complete, 100);
		}
	}

	priv->updating = FALSE;
}



/* Gets the widgets from the XML file and returns if they are all available. */
static gboolean
get_widgets (TaskPage *tpage)
{
	TaskPagePrivate *priv;

	priv = tpage->priv;

#define GW(name) glade_xml_get_widget (priv->xml, name)

	priv->main = GW ("task-page");
	if (!priv->main)
		return FALSE;

	gtk_widget_ref (priv->main);
	gtk_widget_unparent (priv->main);

	priv->summary = GW ("summary");

	priv->due_date = GW ("due-date");
	priv->start_date = GW ("start-date");
	priv->due_timezone = GW ("due-timezone");
	priv->start_timezone = GW ("start-timezone");

	priv->percent_complete = GW ("percent-complete");

	priv->status = GW ("status");
	priv->priority = GW ("priority");

	priv->description = GW ("description");

	priv->classification_public = GW ("classification-public");
	priv->classification_private = GW ("classification-private");
	priv->classification_confidential = GW ("classification-confidential");

	priv->contacts_btn = GW ("contacts-button");
	priv->contacts = GW ("contacts");

	priv->categories_btn = GW ("categories-button");
	priv->categories = GW ("categories");

#undef GW

	return (priv->summary
		&& priv->due_date
		&& priv->start_date
		&& priv->due_timezone
		&& priv->start_timezone
		&& priv->percent_complete
		&& priv->status
		&& priv->priority
		&& priv->classification_public
		&& priv->classification_private
		&& priv->classification_confidential
		&& priv->description
		&& priv->contacts_btn
		&& priv->contacts
		&& priv->categories_btn
		&& priv->categories);
}

/* Callback used when the summary changes; we emit the notification signal. */
static void
summary_changed_cb (GtkEditable *editable, gpointer data)
{
	TaskPage *tpage;
	TaskPagePrivate *priv;
	gchar *summary;
	
	tpage = TASK_PAGE (data);
	priv = tpage->priv;
	
	if (priv->updating)
		return;
	
	summary = gtk_editable_get_chars (editable, 0, -1);
	comp_editor_page_notify_summary_changed (COMP_EDITOR_PAGE (tpage), 
						 summary);
	g_free (summary);
}

/* Callback used when the start or end date widgets change.  We check that the
 * start date < end date and we set the "all day task" button as appropriate.
 */
static void
date_changed_cb (EDateEdit *dedit, gpointer data)
{
	TaskPage *tpage;
	TaskPagePrivate *priv;
	CompEditorPageDates dates;
	gboolean date_set;
	struct icaltimetype start_tt = icaltime_null_time();
	struct icaltimetype due_tt = icaltime_null_time();

	tpage = TASK_PAGE (data);
	priv = tpage->priv;

	if (priv->updating)
		return;

	date_set = e_date_edit_get_date (E_DATE_EDIT (priv->start_date),
					 &start_tt.year,
					 &start_tt.month,
					 &start_tt.day);
	e_date_edit_get_time_of_day (E_DATE_EDIT (priv->start_date),
				     &start_tt.hour,
				     &start_tt.minute);
	if (!date_set)
		start_tt = icaltime_null_time ();

	date_set = e_date_edit_get_date (E_DATE_EDIT (priv->due_date),
					 &due_tt.year,
					 &due_tt.month,
					 &due_tt.day);
	e_date_edit_get_time_of_day (E_DATE_EDIT (priv->due_date),
				     &due_tt.hour,
				     &due_tt.minute);
	if (!date_set)
		due_tt = icaltime_null_time ();

	dates.start = &start_tt;
	dates.end = NULL;
	dates.due = &due_tt;
	dates.complete = NULL;
	
	/* Notify upstream */
	comp_editor_page_notify_dates_changed (COMP_EDITOR_PAGE (tpage),
					       &dates);
}

/* Callback used when the categories button is clicked; we must bring up the
 * category list dialog.
 */
static void
categories_clicked_cb (GtkWidget *button, gpointer data)
{
	TaskPage *tpage;
	TaskPagePrivate *priv;
	GtkWidget *entry;

	tpage = TASK_PAGE (data);
	priv = tpage->priv;

	entry = priv->categories;
	e_categories_config_open_dialog_for_entry (GTK_ENTRY (entry));
}

/* This is called when any field is changed; it notifies upstream. */
static void
field_changed_cb (GtkWidget *widget, gpointer data)
{
	TaskPage *tpage;
	TaskPagePrivate *priv;
	
	tpage = TASK_PAGE (data);
	priv = tpage->priv;
	
	if (!priv->updating)
		comp_editor_page_notify_changed (COMP_EDITOR_PAGE (tpage));
}

static void
complete_date_changed (TaskPage *tpage, gboolean complete)
{
	TaskPagePrivate *priv;
	CompEditorPageDates dates;
	icaltimezone *zone;
	struct icaltimetype completed_tt = icaltime_null_time();

	priv = tpage->priv;

	/* Get the current time in UTC. */
	zone = icaltimezone_get_utc_timezone ();
	completed_tt = icaltime_from_timet_with_zone (time (NULL), FALSE, zone);
	completed_tt.is_utc = TRUE;

	dates.start = NULL;
	dates.end = NULL;
	dates.due = NULL;	
	dates.complete = &completed_tt;
	
	/* Notify upstream */
	comp_editor_page_notify_dates_changed (COMP_EDITOR_PAGE (tpage),
					       &dates);
}

static void
status_changed (GtkMenu	*menu, TaskPage *tpage)
{
	TaskPagePrivate *priv;
	icalproperty_status status;

	priv = tpage->priv;

	if (priv->updating)
		return;

	priv->updating = TRUE;

	status = e_dialog_option_menu_get (priv->status, status_map);
	if (status == ICAL_STATUS_NEEDSACTION) {
		e_dialog_spin_set (priv->percent_complete, 0);
		complete_date_changed (tpage, FALSE);
	} else if (status == ICAL_STATUS_INPROCESS) {
		e_dialog_spin_set (priv->percent_complete, 50);
		complete_date_changed (tpage, FALSE);
	} else if (status == ICAL_STATUS_COMPLETED) {
		e_dialog_spin_set (priv->percent_complete, 100);
		complete_date_changed (tpage, TRUE);
	}

	priv->updating = FALSE;

	comp_editor_page_notify_changed (COMP_EDITOR_PAGE (tpage));
}

static void
percent_complete_changed (GtkAdjustment	*adj, TaskPage *tpage)
{
	TaskPagePrivate *priv;
	gint percent;
	icalproperty_status status;
	gboolean complete;

	priv = tpage->priv;

	if (priv->updating)
		return;
	
	priv->updating = TRUE;

	percent = e_dialog_spin_get_int (priv->percent_complete);
	if (percent == 100) {
		complete = TRUE;
		status = ICAL_STATUS_COMPLETED;
	} else {
		complete = FALSE;

		if (percent == 0)
			status = ICAL_STATUS_NEEDSACTION;
		else
			status = ICAL_STATUS_INPROCESS;
	}

	e_dialog_option_menu_set (priv->status, status, status_map);
	complete_date_changed (tpage, complete);

	priv->updating = FALSE;

	comp_editor_page_notify_changed (COMP_EDITOR_PAGE (tpage));
}


/* Hooks the widget signals */
static void
init_widgets (TaskPage *tpage)
{
	TaskPagePrivate *priv;

	priv = tpage->priv;

	/* Make sure the EDateEdit widgets use our timezones to get the
	   current time. */
	e_date_edit_set_get_time_callback (E_DATE_EDIT (priv->start_date),
					   (EDateEditGetTimeCallback) comp_editor_get_current_time,
					   tpage, NULL);
	e_date_edit_set_get_time_callback (E_DATE_EDIT (priv->due_date),
					   (EDateEditGetTimeCallback) comp_editor_get_current_time,
					   tpage, NULL);
	
	/* Summary */
	gtk_signal_connect (GTK_OBJECT (priv->summary), "changed",
			    GTK_SIGNAL_FUNC (summary_changed_cb), tpage);

	/* Dates */
	gtk_signal_connect (GTK_OBJECT (priv->start_date), "changed",
			    GTK_SIGNAL_FUNC (date_changed_cb), tpage);
	gtk_signal_connect (GTK_OBJECT (priv->due_date), "changed",
			    GTK_SIGNAL_FUNC (date_changed_cb), tpage);

	gtk_signal_connect (GTK_OBJECT (priv->due_timezone), "changed",
			    GTK_SIGNAL_FUNC (field_changed_cb), tpage);
	gtk_signal_connect (GTK_OBJECT (priv->start_timezone), "changed",
			    GTK_SIGNAL_FUNC (field_changed_cb), tpage);

	/* Connect signals. The Status, Percent Complete & Date Completed
	   properties are closely related so whenever one changes we may need
	   to update the other 2. */
	gtk_signal_connect (GTK_OBJECT (GTK_OPTION_MENU (priv->status)->menu),
			    "deactivate",
			    GTK_SIGNAL_FUNC (status_changed), tpage);

	gtk_signal_connect (GTK_OBJECT (GTK_SPIN_BUTTON (priv->percent_complete)->adjustment),
			    "value_changed",
			    GTK_SIGNAL_FUNC (percent_complete_changed), tpage);

	/* Classification */
	gtk_signal_connect (GTK_OBJECT (priv->description), "changed",
			    GTK_SIGNAL_FUNC (field_changed_cb), tpage);
	gtk_signal_connect (GTK_OBJECT (priv->classification_public),
			    "toggled",
			    GTK_SIGNAL_FUNC (field_changed_cb), tpage);
	gtk_signal_connect (GTK_OBJECT (priv->classification_private),
			    "toggled",
			    GTK_SIGNAL_FUNC (field_changed_cb), tpage);
	gtk_signal_connect (GTK_OBJECT (priv->classification_confidential),
			    "toggled",
			    GTK_SIGNAL_FUNC (field_changed_cb), tpage);

	/* Connect the default signal handler to use to make sure the "changed"
	   field gets set whenever a field is changed. */
	gtk_signal_connect (GTK_OBJECT (GTK_OPTION_MENU (priv->priority)->menu),
			    "deactivate",
			    GTK_SIGNAL_FUNC (field_changed_cb), tpage);
	gtk_signal_connect (GTK_OBJECT (priv->description), "changed",
			    GTK_SIGNAL_FUNC (field_changed_cb), tpage);
	gtk_signal_connect (GTK_OBJECT (priv->contacts), "changed",
			    GTK_SIGNAL_FUNC (field_changed_cb), tpage);
	gtk_signal_connect (GTK_OBJECT (priv->categories), "changed",
			    GTK_SIGNAL_FUNC (field_changed_cb), tpage);

	/* Button clicks */
	gtk_signal_connect (GTK_OBJECT (priv->categories_btn), "clicked",
			    GTK_SIGNAL_FUNC (categories_clicked_cb), tpage);

	/* FIXME: we do not support these fields yet, so we disable them */

	gtk_widget_set_sensitive (priv->contacts_btn, FALSE);
	gtk_widget_set_sensitive (priv->contacts, FALSE);
}



/**
 * task_page_construct:
 * @tpage: An task page.
 * 
 * Constructs an task page by loading its Glade data.
 * 
 * Return value: The same object as @tpage, or NULL if the widgets could not be
 * created.
 **/
TaskPage *
task_page_construct (TaskPage *tpage)
{
	TaskPagePrivate *priv;

	priv = tpage->priv;

	priv->xml = glade_xml_new (EVOLUTION_GLADEDIR "/task-page.glade",
				   NULL);
	if (!priv->xml) {
		g_message ("task_page_construct(): "
			   "Could not load the Glade XML file!");
		return NULL;
	}

	if (!get_widgets (tpage)) {
		g_message ("task_page_construct(): "
			   "Could not find all widgets in the XML file!");
		return NULL;
	}

	init_widgets (tpage);

	return tpage;
}

/**
 * task_page_new:
 * 
 * Creates a new task page.
 * 
 * Return value: A newly-created task page, or NULL if the page could
 * not be created.
 **/
TaskPage *
task_page_new (void)
{
	TaskPage *tpage;

	tpage = gtk_type_new (TYPE_TASK_PAGE);
	if (!task_page_construct (tpage)) {
		gtk_object_unref (GTK_OBJECT (tpage));
		return NULL;
	}

	return tpage;
}

GtkWidget *task_page_create_date_edit (void);

GtkWidget *
task_page_create_date_edit (void)
{
	GtkWidget *dedit;

	dedit = comp_editor_new_date_edit (TRUE, TRUE);
	e_date_edit_set_allow_no_date_set (E_DATE_EDIT (dedit), TRUE);

	return dedit;
}
