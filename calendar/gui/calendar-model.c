/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* Evolution calendar - Data model for ETable
 *
 * Copyright (C) 2000 Helix Code, Inc.
 *
 * Authors: Federico Mena-Quintero <federico@helixcode.com>
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

/* We need this for strptime. */
#define _XOPEN_SOURCE

#include <config.h>
#include <ctype.h>
#include <math.h>
#undef _XOPEN_SOURCE
#include <sys/time.h>
#define _XOPEN_SOURCE
#include <time.h>
#include <gnome.h>
#include <cal-util/timeutil.h>
#include "calendar-model.h"
#include "calendar-commands.h"



/* Private part of the ECalendarModel structure */
struct _CalendarModelPrivate {
	/* Calendar client we are using */
	CalClient *client;

	/* Types of objects we are dealing with */
	CalObjType type;

	/* Array of pointers to calendar objects */
	GArray *objects;

	/* UID -> array index hash */
	GHashTable *uid_index_hash;

	/* Whether we display dates in 24-hour format. */
	gboolean use_24_hour_format;

	/* HACK: so that ETable can do its stupid append_row() thing */
	guint appending_row : 1;
};



static void calendar_model_class_init (CalendarModelClass *class);
static void calendar_model_init (CalendarModel *model);
static void calendar_model_destroy (GtkObject *object);

static int calendar_model_column_count (ETableModel *etm);
static int calendar_model_row_count (ETableModel *etm);
static void *calendar_model_value_at (ETableModel *etm, int col, int row);
static void calendar_model_set_value_at (ETableModel *etm, int col, int row, const void *value);
static gboolean calendar_model_is_cell_editable (ETableModel *etm, int col, int row);
static void calendar_model_append_row (ETableModel *etm, ETableModel *source, gint row);
static void *calendar_model_duplicate_value (ETableModel *etm, int col, const void *value);
static void calendar_model_free_value (ETableModel *etm, int col, void *value);
static void *calendar_model_initialize_value (ETableModel *etm, int col);
static gboolean calendar_model_value_is_empty (ETableModel *etm, int col, const void *value);
#if 0
static char * calendar_model_value_to_string (ETableModel *etm, int col, const void *value);
#endif
static void load_objects (CalendarModel *model);
static int remove_object (CalendarModel *model, const char *uid);
static void ensure_task_complete (CalComponent *comp,
				  time_t completed_date);
static void ensure_task_not_complete (CalComponent *comp);

static ETableModelClass *parent_class;



/**
 * calendar_model_get_type:
 * @void:
 *
 * Registers the #CalendarModel class if necessary, and returns the type ID
 * associated to it.
 *
 * Return value: The type ID of the #CalendarModel class.
 **/
GtkType
calendar_model_get_type (void)
{
	static GtkType calendar_model_type = 0;

	if (!calendar_model_type) {
		static GtkTypeInfo calendar_model_info = {
			"CalendarModel",
			sizeof (CalendarModel),
			sizeof (CalendarModelClass),
			(GtkClassInitFunc) calendar_model_class_init,
			(GtkObjectInitFunc) calendar_model_init,
			NULL, /* reserved_1 */
			NULL, /* reserved_2 */
			(GtkClassInitFunc) NULL
		};

		calendar_model_type = gtk_type_unique (E_TABLE_MODEL_TYPE, &calendar_model_info);
	}

	return calendar_model_type;
}

/* Class initialization function for the calendar table model */
static void
calendar_model_class_init (CalendarModelClass *class)
{
	GtkObjectClass *object_class;
	ETableModelClass *etm_class;

	object_class = (GtkObjectClass *) class;
	etm_class = (ETableModelClass *) class;

	parent_class = gtk_type_class (E_TABLE_MODEL_TYPE);

	object_class->destroy = calendar_model_destroy;

	etm_class->column_count = calendar_model_column_count;
	etm_class->row_count = calendar_model_row_count;
	etm_class->value_at = calendar_model_value_at;
	etm_class->set_value_at = calendar_model_set_value_at;
	etm_class->is_cell_editable = calendar_model_is_cell_editable;
	etm_class->append_row = calendar_model_append_row;
	etm_class->duplicate_value = calendar_model_duplicate_value;
	etm_class->free_value = calendar_model_free_value;
	etm_class->initialize_value = calendar_model_initialize_value;
	etm_class->value_is_empty = calendar_model_value_is_empty;
#if 0
	etm_class->value_to_string = calendar_model_value_to_string;
#endif
}

/* Object initialization function for the calendar table model */
static void
calendar_model_init (CalendarModel *model)
{
	CalendarModelPrivate *priv;

	priv = g_new0 (CalendarModelPrivate, 1);
	model->priv = priv;

	priv->objects = g_array_new (FALSE, TRUE, sizeof (CalComponent *));
	priv->uid_index_hash = g_hash_table_new (g_str_hash, g_str_equal);
	priv->use_24_hour_format = TRUE;
}

/* Called from g_hash_table_foreach_remove(), frees a stored UID->index
 * mapping.
 */
static gboolean
free_uid_index (gpointer key, gpointer value, gpointer data)
{
	int *idx;

	idx = value;
	g_free (idx);

	return TRUE;
}

/* Frees the objects stored in the calendar model */
static void
free_objects (CalendarModel *model)
{
	CalendarModelPrivate *priv;
	int i;

	priv = model->priv;

	g_hash_table_foreach_remove (priv->uid_index_hash, free_uid_index, NULL);

	for (i = 0; i < priv->objects->len; i++) {
		CalComponent *comp;

		comp = g_array_index (priv->objects, CalComponent *, i);
		g_assert (comp != NULL);
		gtk_object_unref (GTK_OBJECT (comp));
	}

	g_array_set_size (priv->objects, 0);
}

/* Destroy handler for the calendar table model */
static void
calendar_model_destroy (GtkObject *object)
{
	CalendarModel *model;
	CalendarModelPrivate *priv;

	g_return_if_fail (object != NULL);
	g_return_if_fail (IS_CALENDAR_MODEL (object));

	model = CALENDAR_MODEL (object);
	priv = model->priv;

	/* Free the calendar client interface object */

	if (priv->client) {
		gtk_signal_disconnect_by_data (GTK_OBJECT (priv->client), model);
		gtk_object_unref (GTK_OBJECT (priv->client));
		priv->client = NULL;
	}

	/* Free the uid->index hash data and the array of UIDs */

	free_objects (model);

	g_hash_table_destroy (priv->uid_index_hash);
	priv->uid_index_hash = NULL;

	g_array_free (priv->objects, TRUE);
	priv->objects = NULL;

	/* Free the private structure */

	g_free (priv);
	model->priv = NULL;

	if (GTK_OBJECT_CLASS (parent_class)->destroy)
		(* GTK_OBJECT_CLASS (parent_class)->destroy) (object);
}



/* ETableModel methods */

/* column_count handler for the calendar table model */
static int
calendar_model_column_count (ETableModel *etm)
{
	return CAL_COMPONENT_FIELD_NUM_FIELDS;
}

/* row_count handler for the calendar table model */
static int
calendar_model_row_count (ETableModel *etm)
{
	CalendarModel *model;
	CalendarModelPrivate *priv;

	model = CALENDAR_MODEL (etm);
	priv = model->priv;

	return priv->objects->len;
}

/* Creates a nice string representation of a time value */
static char*
get_time_t (CalendarModel *model, time_t *t, gboolean skip_midnight)
{
	static char buffer[64];
	struct tm *tmp_tm;
	char *format;

	if (*t <= 0) {
		buffer[0] = '\0';
	} else {
		tmp_tm = localtime (t);

		if (skip_midnight && tmp_tm->tm_hour == 0
		    && tmp_tm->tm_min == 0 && tmp_tm->tm_sec == 0)
			/* strftime format of a weekday and a date. */
			format = _("%a %m/%d/%Y");
		else if (model->priv->use_24_hour_format)
			/* strftime format of a weekday, a date and a time,
			   in 24-hour format. */
			format = _("%a %m/%d/%Y %H:%M:%S");
		else
			/* strftime format of a weekday, a date and a time,
			   in 12-hour format. */
			format = _("%a %m/%d/%Y %I:%M:%S %p");
			
		strftime (buffer, sizeof (buffer), format, tmp_tm);
	}

	return buffer;
}

/* Builds a string based on the list of CATEGORIES properties of a calendar
 * component.
 */
static char *
get_categories (CalComponent *comp)
{
	GSList *categories;
	GString *str;
	char *s;
	GSList *l;

	cal_component_get_categories_list (comp, &categories);

	str = g_string_new (NULL);

	for (l = categories; l; l = l->next) {
		const char *category;

		category = l->data;
		g_string_append (str, category);

		if (l->next != NULL)
			g_string_append (str, ", ");
	}

	s = str->str;

	g_string_free (str, FALSE);
	cal_component_free_categories_list (categories);

	return s;
}

/* Returns a string based on the CLASSIFICATION property of a calendar component */
static char *
get_classification (CalComponent *comp)
{
	CalComponentClassification classif;

	cal_component_get_classification (comp, &classif);

	switch (classif) {
	case CAL_COMPONENT_CLASS_NONE:
		return "";

	case CAL_COMPONENT_CLASS_PUBLIC:
		return _("Public");

	case CAL_COMPONENT_CLASS_PRIVATE:
		return _("Private");

	case CAL_COMPONENT_CLASS_CONFIDENTIAL:
		return _("Confidential");

	case CAL_COMPONENT_CLASS_UNKNOWN:
		return _("Unknown");

	default:
		g_assert_not_reached ();
		return NULL;
	}
}

/* Builds a string for the COMPLETED property of a calendar component */
static char *
get_completed	(CalendarModel *model,
		 CalComponent  *comp)
{
	struct icaltimetype *completed;
	time_t t;

	cal_component_get_completed (comp, &completed);

	if (!completed)
		t = 0;
	else {
		t = icaltime_as_timet (*completed);
		cal_component_free_icaltimetype (completed);
	}

	return get_time_t (model, &t, FALSE);
}

/* Builds a string for and frees a date/time value */
static char *
get_and_free_datetime (CalendarModel *model, CalComponentDateTime dt)
{
	time_t t;

	if (!dt.value)
		t = 0;
	else
		t = icaltime_as_timet (*dt.value);

	cal_component_free_datetime (&dt);

	return get_time_t (model, &t, FALSE);
}

/* Builds a string for the DTEND property of a calendar component */
static char *
get_dtend (CalendarModel *model, CalComponent *comp)
{
	CalComponentDateTime dt;

	cal_component_get_dtend (comp, &dt);
	return get_and_free_datetime (model, dt);
}

/* Builds a string for the DTSTART property of a calendar component */
static char *
get_dtstart (CalendarModel *model, CalComponent *comp)
{
	CalComponentDateTime dt;

	cal_component_get_dtstart (comp, &dt);
	return get_and_free_datetime (model, dt);
}

/* Builds a string for the DUE property of a calendar component */
static char *
get_due (CalendarModel *model, CalComponent *comp)
{
	CalComponentDateTime dt;

	cal_component_get_due (comp, &dt);
	return get_and_free_datetime (model, dt);
}

/* Builds a string for the GEO property of a calendar component */
static char*
get_geo (CalComponent *comp)
{
	struct icalgeotype *geo;
	static gchar buf[32];

	cal_component_get_geo (comp, &geo);

	if (!geo)
		buf[0] = '\0';
	else {
		g_snprintf (buf, sizeof (buf), "%g %s, %g %s",
			    fabs (geo->lat),
			    geo->lat >= 0.0 ? _("N") : _("S"),
			    fabs (geo->lon),
			    geo->lon >= 0.0 ? _("E") : _("W"));
		cal_component_free_geo (geo);
	}

	return buf;
}

/* Builds a string for the PERCENT property of a calendar component */
static char *
get_percent (CalComponent *comp)
{
	int *percent;
	static char buf[32];

	cal_component_get_percent (comp, &percent);

	if (!percent)
		buf[0] = '\0';
	else {
		g_snprintf (buf, sizeof (buf), "%d%%", *percent);
		cal_component_free_percent (percent);
	}

	return buf;
}

/* Builds a string for the PRIORITY property of a calendar component */
static char *
get_priority (CalComponent *comp)
{
	int *priority;
	static char buf[32];

	cal_component_get_priority (comp, &priority);

	if (!priority)
		buf[0] = '\0';
	else {
		g_snprintf (buf, sizeof (buf), "%d", *priority);
		cal_component_free_priority (priority);
	}

	return buf;
}

/* Builds a string for the SUMMARY property of a calendar component */
static char *
get_summary (CalComponent *comp)
{
	CalComponentText summary;

	cal_component_get_summary (comp, &summary);

	if (summary.value)
		return (char *) summary.value;
	else
		return "";
}

/* Builds a string for the TRANSPARENCY property of a calendar component */
static char *
get_transparency (CalComponent *comp)
{
	CalComponentTransparency transp;

	cal_component_get_transparency (comp, &transp);

	switch (transp) {
	case CAL_COMPONENT_TRANSP_NONE:
		return "";

	case CAL_COMPONENT_TRANSP_TRANSPARENT:
		return _("Transparent");

	case CAL_COMPONENT_TRANSP_OPAQUE:
		return _("Opaque");

	case CAL_COMPONENT_TRANSP_UNKNOWN:
		return _("Unknown");

	default:
		g_assert_not_reached ();
		return NULL;
	}
}

/* Builds a string for the URL property of a calendar component */
static char *
get_url (CalComponent *comp)
{
	const char *url;

	cal_component_get_url (comp, &url);

	if (url)
		return (char *) url;
	else
		return "";
}

/* Returns whether the component has any alarms defined for it */
static gboolean
get_has_alarms (CalComponent *comp)
{
	CalComponentAlarm *alarm;
	gboolean retval;

	alarm = cal_component_get_first_alarm (comp);
	retval = (alarm != NULL);

	cal_component_alarm_free (alarm);
	return retval;
}

/* Returns whether the completion date has been set on a component */
static gboolean
get_is_complete (CalComponent *comp)
{
	struct icaltimetype *t;
	gboolean retval;

	cal_component_get_completed (comp, &t);
	retval = (t != NULL);

	if (retval)
		cal_component_free_icaltimetype (t);

	return retval;
}

/* Returns whether a calendar component is overdue.
 *
 * FIXME: This will only get called when the component is scrolled into the
 * ETable.  There should be some sort of dynamic update thingy for if a component
 * becomes overdue while it is being viewed.
 */
static gboolean
get_is_overdue (CalComponent *comp)
{
	CalComponentDateTime dt;
	gboolean retval;

	cal_component_get_due (comp, &dt);

	/* First, do we have a due date? */

	if (!dt.value)
		retval = FALSE;
	else {
		time_t t;

		/* Second, is it already completed? */

		if (get_is_complete (comp)) {
			retval = FALSE;
			goto out;
		}

		/* Third, are we overdue as of right now? */

		t = icaltime_as_timet (*dt.value);

		if (t < time (NULL))
			retval = TRUE;
		else
			retval = FALSE;
	}

 out:

	cal_component_free_datetime (&dt);

	return retval;
}

/* value_at handler for the calendar table model */
static void *
calendar_model_value_at (ETableModel *etm, int col, int row)
{
	CalendarModel *model;
	CalendarModelPrivate *priv;
	CalComponent *comp;

	model = CALENDAR_MODEL (etm);
	priv = model->priv;

	g_return_val_if_fail (col >= 0 && col < CAL_COMPONENT_FIELD_NUM_FIELDS, NULL);
	g_return_val_if_fail (row >= 0 && row < priv->objects->len, NULL);

	comp = g_array_index (priv->objects, CalComponent *, row);
	g_assert (comp != NULL);

	switch (col) {
	case CAL_COMPONENT_FIELD_CATEGORIES:
		return get_categories (comp);

	case CAL_COMPONENT_FIELD_CLASSIFICATION:
		return get_classification (comp);

	case CAL_COMPONENT_FIELD_COMPLETED:
		return get_completed (model, comp);

	case CAL_COMPONENT_FIELD_DTEND:
		return get_dtend (model, comp);

	case CAL_COMPONENT_FIELD_DTSTART:
		return get_dtstart (model, comp);

	case CAL_COMPONENT_FIELD_DUE:
		return get_due (model, comp);

	case CAL_COMPONENT_FIELD_GEO:
		return get_geo (comp);

	case CAL_COMPONENT_FIELD_PERCENT:
		return get_percent (comp);

	case CAL_COMPONENT_FIELD_PRIORITY:
		return get_priority (comp);

	case CAL_COMPONENT_FIELD_SUMMARY:
		return get_summary (comp);

	case CAL_COMPONENT_FIELD_TRANSPARENCY:
		return get_transparency (comp);

	case CAL_COMPONENT_FIELD_URL:
		return get_url (comp);

	case CAL_COMPONENT_FIELD_HAS_ALARMS:
		return GINT_TO_POINTER (get_has_alarms (comp));

	case CAL_COMPONENT_FIELD_ICON:
		/* FIXME: Also support 'Assigned to me' & 'Assigned to someone
		   else'. */
		if (cal_component_has_recurrences (comp))
			return GINT_TO_POINTER (1);
		else
			return GINT_TO_POINTER (0);

	case CAL_COMPONENT_FIELD_COMPLETE:
		return GINT_TO_POINTER (get_is_complete (comp));

	case CAL_COMPONENT_FIELD_RECURRING:
		return GINT_TO_POINTER (cal_component_has_recurrences (comp));

	case CAL_COMPONENT_FIELD_OVERDUE:
		return GINT_TO_POINTER (get_is_overdue (comp));

	case CAL_COMPONENT_FIELD_COLOR:
		if (get_is_overdue (comp))
			return "red";
		else
			return NULL;

	default:
		g_message ("calendar_model_value_at(): Requested invalid column %d", col);
		return NULL;
	}
}

/* Returns whether a string is NULL, empty, or full of whitespace */
static gboolean
string_is_empty (const char *value)
{
	const char *p;
	gboolean empty = TRUE;

	if (value) {
		p = value;
		while (*p) {
			if (!isspace (*p)) {
				empty = FALSE;
				break;
			}
			p++;
		}
	}
	return empty;
}


/* FIXME: We need to set the "transient_for" property for the dialog, but
   the model doesn't know anything about the windows. */
static void
show_date_warning (CalendarModel *model)
{
	GtkWidget *dialog;
	char buffer[64], message[256], *format;
	time_t t;
	struct tm *tmp_tm;

	t = time (NULL);
	tmp_tm = localtime (&t);

	if (model->priv->use_24_hour_format)
		/* strftime format of a weekday, a date and a time, 24-hour. */
		format = _("%a %m/%d/%Y %H:%M:%S");
	else
		/* strftime format of a weekday, a date and a time, 12-hour. */
		format = _("%a %m/%d/%Y %I:%M:%S %p");

	strftime (buffer, sizeof (buffer), format, tmp_tm);

	g_snprintf (message, 256,
		    _("The date must be entered in the format: \n\n%s"),
		    buffer);

	dialog = gnome_message_box_new (message,
					GNOME_MESSAGE_BOX_ERROR,
					GNOME_STOCK_BUTTON_OK, NULL);
	gtk_widget_show (dialog);
}

/* Builds a list of categories from a comma-delimited string */
static GSList *
categories_from_string (const char *value)
{
	GSList *list;
	const char *categ_start;
	const char *categ_end;
	const char *p;

	if (!value)
		return NULL;

	list = NULL;

	categ_start = categ_end = NULL;

	for (p = value; *p; p++) {
		if (categ_start) {
			if (*p == ',') {
				char *c;

				c = g_strndup (categ_start, categ_end - categ_start + 1);
				list = g_slist_prepend (list, c);

				categ_start = categ_end = NULL;
			} else if (!isspace (*p))
				categ_end = p;
		} else if (!isspace (*p) && *p != ',')
			categ_start = categ_end = p;
	}

	if (categ_start) {
		char *c;

		c = g_strndup (categ_start, categ_end - categ_start + 1);
		list = g_slist_prepend (list, c);
	}

	return g_slist_reverse (list);
}

/* Sets the list of categories from a comma-delimited string */
static void
set_categories (CalComponent *comp, const char *value)
{
	GSList *list;
	GSList *l;

	list = categories_from_string (value);

	cal_component_set_categories_list (comp, list);

	for (l = list; l; l = l->next) {
		char *s;

		s = l->data;
		g_free (s);
	}

	g_slist_free (list);
}

/* Parses a time value entered by the user; returns -1 if it could not be
 * parsed.  Returns 0 for an empty time.
 */
static time_t
parse_time (const char *value)
{
	struct tm discard_tm, date_tm, time_tm;
	struct tm *today_tm;
	time_t t;
	const char *pos, *parse_end;
	char *format[4];
	gboolean parsed_date = FALSE, parsed_time = FALSE;
	gint i;

	if (string_is_empty (value))
		return 0;

	pos = value;

	/* Skip any whitespace. */
	while (isspace (*pos))
		pos++;

	/* Skip any weekday name, full or abbreviated. */
	parse_end = strptime (pos, "%a ", &discard_tm);
	if (parse_end)
		pos = parse_end;

	memset (&date_tm, 0, sizeof (date_tm));
	/* strptime format for a date. */
	parse_end = strptime (pos, _("%m/%d/%Y"), &date_tm);
	if (parse_end) {
		pos = parse_end;
		parsed_date = TRUE;
	}

	/* Skip any whitespace. */
	while (isspace (*pos))
		pos++;

	/* Skip any weekday name, full or abbreviated, again. */
	parse_end = strptime (pos, "%a ", &discard_tm);
	if (parse_end)
		pos = parse_end;


	/* strptime format for a time of day, in 12-hour format.
	   If it is is not appropriate in the locale set to an empty string. */
	format[0] = _("%I:%M:%S %p%n");

	/* strptime format for a time of day, in 24-hour format. */
	format[1] = _("%H:%M:%S%n");

	/* strptime format for time of day, without seconds, 12-hour format.
	   If it is is not appropriate in the locale set to an empty string. */
	format[2] = _("%I:%M %p%n");

	/* strptime format for time of day, without seconds 24-hour format. */
	format[3] = _("%H:%M%n");

	for (i = 0; i < sizeof (format) / sizeof (format[0]); i++) {
		memset (&time_tm, 0, sizeof (time_tm));
		parse_end = strptime (pos, format[i], &time_tm);
		if (parse_end) {
			pos = parse_end;
			parsed_time = TRUE;
			break;
		}
	}

	/* Skip any whitespace. */
	while (isspace (*pos))
		pos++;

	/* If we haven't already parsed a date, try again. */
	if (!parsed_date) {
		memset (&date_tm, 0, sizeof (date_tm));
		/* strptime format for a date. */
		parse_end = strptime (pos, _("%m/%d/%Y"), &date_tm);
		if (parse_end) {
			pos = parse_end;
			parsed_date = TRUE;
		}
	}

	/* If we don't have a date or a time it must be invalid. */
	if (!parsed_date && !parsed_time)
		return -1;


	if (parsed_date) {
		/* If a 2-digit year was used we use the current century. */
		if (date_tm.tm_year < 0) {
			t = time (NULL);
			today_tm = localtime (&t);

			/* This should convert it into a value from 0 to 99. */
			date_tm.tm_year += 1900;

			/* Now add on the century. */
			date_tm.tm_year += today_tm->tm_year
				- (today_tm->tm_year % 100);
		}
	} else {
		/* If we didn't get a date we use the current day. */
		t = time (NULL);
		today_tm = localtime (&t);
		date_tm.tm_mday = today_tm->tm_mday;
		date_tm.tm_mon  = today_tm->tm_mon;
		date_tm.tm_year = today_tm->tm_year;
	}

	if (parsed_time) {
		date_tm.tm_hour = time_tm.tm_hour;
		date_tm.tm_min = time_tm.tm_min;
		date_tm.tm_sec = time_tm.tm_sec;
	} else {
		date_tm.tm_hour = 0;
		date_tm.tm_min = 0;
		date_tm.tm_sec = 0;
	}


	date_tm.tm_isdst = -1;
	return mktime (&date_tm);
}

/* Called to set the "Date Completed" field. We also need to update the
   Status and Percent fields to make sure they match. */
static void
set_completed (CalendarModel *model, CalComponent *comp, const char *value)
{
	time_t t;

	t = parse_time (value);
	if (t == -1) {
		show_date_warning (model);
	} else if (t == 0) {
		ensure_task_not_complete (comp);
	} else {
		ensure_task_complete (comp, t);
	}
}

/* Sets a CalComponentDateTime value */
static void
set_datetime (CalendarModel *model, CalComponent *comp, const char *value,
	      void (* set_func) (CalComponent *comp, CalComponentDateTime *dt))
{
	time_t t;

	t = parse_time (value);
	if (t == -1) {
		show_date_warning (model);
		return;
	} else if (t == 0) {
		(* set_func) (comp, NULL);
		return;
	} else {
		CalComponentDateTime dt;
		struct icaltimetype itt;

		itt = icaltime_from_timet (t, FALSE, TRUE);
		dt.value = &itt;
		dt.tzid = NULL;

		(* set_func) (comp, &dt);
	}
}

/* FIXME: We need to set the "transient_for" property for the dialog, but the
 * model doesn't know anything about the windows.
 */
static void
show_geo_warning (void)
{
	GtkWidget *dialog;

	dialog = gnome_message_box_new (_("The geographical position must be entered "
					  "in the format: \n\n45.436845,125.862501"),
					GNOME_MESSAGE_BOX_ERROR,
					GNOME_STOCK_BUTTON_OK, NULL);
	gtk_widget_show (dialog);
}

/* Sets the geographical position value of a component */
static void
set_geo (CalComponent *comp, const char *value)
{
	double latitude, longitude;
	int matched;
	struct icalgeotype geo;

	if (string_is_empty (value)) {
		cal_component_set_geo (comp, NULL);
		return;
	}

	matched = sscanf (value, "%lg , %lg", &latitude, &longitude);

	if (matched != 2) {
		show_geo_warning ();
		return;
	}

	geo.lat = latitude;
	geo.lon = longitude;
	cal_component_set_geo (comp, &geo);
}

/* FIXME: We need to set the "transient_for" property for the dialog, but the
 * model doesn't know anything about the windows.
 */
static void
show_percent_warning (void)
{
	GtkWidget *dialog;

	dialog = gnome_message_box_new (_("The percent value must be between 0 and 100, inclusive"),
					GNOME_MESSAGE_BOX_ERROR,
					GNOME_STOCK_BUTTON_OK, NULL);
	gtk_widget_show (dialog);
}

/* Sets the percent value of a calendar component */
static void
set_percent (CalComponent *comp, const char *value)
{
	int matched, percent;

	if (string_is_empty (value)) {
		cal_component_set_percent (comp, NULL);
		ensure_task_not_complete (comp);
		return;
	}

	matched = sscanf (value, "%i", &percent);

	if (matched != 1 || percent < 0 || percent > 100) {
		show_percent_warning ();
		return;
	}

	cal_component_set_percent (comp, &percent);

	if (percent == 100)
		ensure_task_complete (comp, -1);
	else
		ensure_task_not_complete (comp);
}

/* FIXME: We need to set the "transient_for" property for the dialog, but the
 * model doesn't know anything about the windows.  */
static void
show_priority_warning (void)
{
	GtkWidget *dialog;

	dialog = gnome_message_box_new (_("The priority must be between 1 and 9, inclusive"),
					GNOME_MESSAGE_BOX_ERROR,
					GNOME_STOCK_BUTTON_OK, NULL);
	gtk_widget_show (dialog);
}

/* Sets the priority of a calendar component */
static void
set_priority (CalComponent *comp, const char *value)
{
	int matched, priority;

	if (string_is_empty (value)) {
		cal_component_set_priority (comp, NULL);
		return;
	}

	matched = sscanf (value, "%i", &priority);

	if (matched != 1 || priority < 1 || priority > 9) {
		show_priority_warning ();
		return;
	}

	cal_component_set_priority (comp, &priority);
}

/* Sets the summary of a calendar component */
static void
set_summary (CalComponent *comp, const char *value)
{
	CalComponentText text;

	if (string_is_empty (value)) {
		cal_component_set_summary (comp, NULL);
		return;
	}

	text.value = value;
	text.altrep = NULL; /* FIXME: should we preserve the old ALTREP? */

	cal_component_set_summary (comp, &text);
}

/* Sets the URI of a calendar component */
static void
set_url (CalComponent *comp, const char *value)
{
	if (string_is_empty (value)) {
		cal_component_set_url (comp, NULL);
		return;
	}

	cal_component_set_url (comp, value);
}

/* Called to set the checkbutton field which indicates whether a task is
   complete. */
static void
set_complete (CalComponent *comp, const void *value)
{
	gint state = GPOINTER_TO_INT (value);

	if (state) {
		ensure_task_complete (comp, -1);
	} else {
		ensure_task_not_complete (comp);
	}
}

/* set_value_at handler for the calendar table model */
static void
calendar_model_set_value_at (ETableModel *etm, int col, int row, const void *value)
{
	CalendarModel *model;
	CalendarModelPrivate *priv;
	CalComponent *comp;

	model = CALENDAR_MODEL (etm);
	priv = model->priv;

	g_return_if_fail (col >= 0 && col < CAL_COMPONENT_FIELD_NUM_FIELDS);
	g_return_if_fail (row >= 0 && row < priv->objects->len);

	comp = g_array_index (priv->objects, CalComponent *, row);
	g_assert (comp != NULL);

	switch (col) {
	case CAL_COMPONENT_FIELD_CATEGORIES:
		set_categories (comp, value);
		break;

	/* FIXME: CLASSIFICATION requires an option menu cell renderer */

	case CAL_COMPONENT_FIELD_COMPLETED:
		set_completed (model, comp, value);
		break;

	case CAL_COMPONENT_FIELD_DTEND:
		/* FIXME: Need to reset dtstart if dtend happens before it */
		set_datetime (model, comp, value, cal_component_set_dtend);
		break;

	case CAL_COMPONENT_FIELD_DTSTART:
		/* FIXME: Need to reset dtend if dtstart happens after it */
		set_datetime (model, comp, value, cal_component_set_dtstart);
		break;

	case CAL_COMPONENT_FIELD_DUE:
		set_datetime (model, comp, value, cal_component_set_due);
		break;

	case CAL_COMPONENT_FIELD_GEO:
		set_geo (comp, value);
		break;

	case CAL_COMPONENT_FIELD_PERCENT:
		set_percent (comp, value);
		break;

	case CAL_COMPONENT_FIELD_PRIORITY:
		set_priority (comp, value);
		break;

	case CAL_COMPONENT_FIELD_SUMMARY:
		set_summary (comp, value);
		break;

	/* FIXME: TRANSPARENCY requires an option menu cell renderer */

	case CAL_COMPONENT_FIELD_URL:
		set_url (comp, value);
		break;

	case CAL_COMPONENT_FIELD_COMPLETE:
		set_complete (comp, value);
		break;

	default:
		g_message ("calendar_model_set_value_at(): Requested invalid column %d", col);
		break;
	}

	/* FIXME: this is an ugly HACK.  ETable needs a better API for the
	 * "click here to add an element" thingy.
	 */
	if (priv->appending_row)
		return;

	if (!cal_client_update_object (priv->client, comp))
		g_message ("calendar_model_set_value_at(): Could not update the object!");
}

/* is_cell_editable handler for the calendar table model */
static gboolean
calendar_model_is_cell_editable (ETableModel *etm, int col, int row)
{
	CalendarModel *model;
	CalendarModelPrivate *priv;

	model = CALENDAR_MODEL (etm);
	priv = model->priv;

	g_return_val_if_fail (col >= 0 && col < CAL_COMPONENT_FIELD_NUM_FIELDS, FALSE);

	/* FIXME: We can't check this as 'click-to-add' passes row 0. */
	/*g_return_val_if_fail (row >= 0 && row < priv->objects->len, FALSE);*/

	switch (col) {
	case CAL_COMPONENT_FIELD_CATEGORIES:
	case CAL_COMPONENT_FIELD_COMPLETED:
	case CAL_COMPONENT_FIELD_DTEND:
	case CAL_COMPONENT_FIELD_DTSTART:
	case CAL_COMPONENT_FIELD_DUE:
	case CAL_COMPONENT_FIELD_GEO:
	case CAL_COMPONENT_FIELD_PERCENT:
	case CAL_COMPONENT_FIELD_PRIORITY:
	case CAL_COMPONENT_FIELD_SUMMARY:
	case CAL_COMPONENT_FIELD_URL:
	case CAL_COMPONENT_FIELD_ICON:
	case CAL_COMPONENT_FIELD_COMPLETE:
		return TRUE;

	default:
		return FALSE;
	}
}

/* append_row handler for the calendar model */
static void
calendar_model_append_row (ETableModel *etm, ETableModel *source, gint row)
{
	CalendarModel *model;
	CalendarModelPrivate *priv;
	CalComponent *comp;
	int *new_idx, col;
	const char *uid;

	model = CALENDAR_MODEL (etm);
	priv = model->priv;

	/* This is a HACK */
	priv->appending_row = TRUE;

	/* FIXME: This should support other types of components, but for now it
	 * is only used for the task list.
	 */
	comp = cal_component_new ();
	cal_component_set_new_vtype (comp, CAL_COMPONENT_TODO);

	cal_component_get_uid (comp, &uid);

	g_array_append_val (priv->objects, comp);
	new_idx = g_new (int, 1);
	*new_idx = priv->objects->len - 1;
	g_hash_table_insert (priv->uid_index_hash, (char *) uid, new_idx);

	/* Notify the views about the new row. I think we have to do that here,
	   or the views may become confused when they start getting
	   "row_changed" or "cell_changed" signals for this new row. */
	e_table_model_row_inserted (etm, *new_idx);

	for (col = 0; col < CAL_COMPONENT_FIELD_NUM_FIELDS; col++) {
		const void *val;

		if (!e_table_model_is_cell_editable (etm, col, *new_idx))
			continue;

		val = e_table_model_value_at(source, col, row);
		e_table_model_set_value_at (etm, col, *new_idx, val);
	}

	/* This is the end of the HACK */
	priv->appending_row = FALSE;

	if (!cal_client_update_object (priv->client, comp)) {
		/* FIXME: Show error dialog. */
		g_message ("calendar_model_append_row(): Could not add new object!");
		remove_object (model, uid);
		e_table_model_row_deleted (etm, *new_idx);
	}
}

/* Duplicates a string value */
static char *
dup_string (const char *value)
{
	return g_strdup (value);
}

/* duplicate_value handler for the calendar table model */
static void *
calendar_model_duplicate_value (ETableModel *etm, int col, const void *value)
{
	g_return_val_if_fail (col >= 0 && col < CAL_COMPONENT_FIELD_NUM_FIELDS, NULL);

	/* They are almost all dup_string()s for now, but we'll have real fields
	 * later.
	 */

	switch (col) {
	case CAL_COMPONENT_FIELD_CATEGORIES:
		return dup_string (value);

	case CAL_COMPONENT_FIELD_CLASSIFICATION:
		return (void *) value;

	case CAL_COMPONENT_FIELD_COMPLETED:
	case CAL_COMPONENT_FIELD_DTEND:
	case CAL_COMPONENT_FIELD_DTSTART:
	case CAL_COMPONENT_FIELD_DUE:
	case CAL_COMPONENT_FIELD_GEO:
	case CAL_COMPONENT_FIELD_PERCENT:
	case CAL_COMPONENT_FIELD_PRIORITY:
	case CAL_COMPONENT_FIELD_SUMMARY:
		return dup_string (value);

	case CAL_COMPONENT_FIELD_TRANSPARENCY:
		return (void *) value;

	case CAL_COMPONENT_FIELD_URL:
		return dup_string (value);

	case CAL_COMPONENT_FIELD_HAS_ALARMS:
	case CAL_COMPONENT_FIELD_ICON:
	case CAL_COMPONENT_FIELD_COMPLETE:
	case CAL_COMPONENT_FIELD_RECURRING:
	case CAL_COMPONENT_FIELD_OVERDUE:
	case CAL_COMPONENT_FIELD_COLOR:
		return (void *) value;

	default:
		g_message ("calendar_model_duplicate_value(): Requested invalid column %d", col);
		return NULL;
	}
}

/* free_value handler for the calendar table model */
static void
calendar_model_free_value (ETableModel *etm, int col, void *value)
{
	g_return_if_fail (col >= 0 && col < CAL_COMPONENT_FIELD_NUM_FIELDS);

	switch (col) {
	case CAL_COMPONENT_FIELD_CATEGORIES:
		g_free (value);

	case CAL_COMPONENT_FIELD_CLASSIFICATION:
		return;

	case CAL_COMPONENT_FIELD_COMPLETED:
	case CAL_COMPONENT_FIELD_DTEND:
	case CAL_COMPONENT_FIELD_DTSTART:
	case CAL_COMPONENT_FIELD_DUE:
	case CAL_COMPONENT_FIELD_GEO:
	case CAL_COMPONENT_FIELD_PERCENT:
	case CAL_COMPONENT_FIELD_PRIORITY:
	case CAL_COMPONENT_FIELD_SUMMARY:
		g_free (value);

	case CAL_COMPONENT_FIELD_TRANSPARENCY:
		return;

	case CAL_COMPONENT_FIELD_URL:
		g_free (value);

	case CAL_COMPONENT_FIELD_HAS_ALARMS:
	case CAL_COMPONENT_FIELD_ICON:
	case CAL_COMPONENT_FIELD_COMPLETE:
	case CAL_COMPONENT_FIELD_RECURRING:
	case CAL_COMPONENT_FIELD_OVERDUE:
	case CAL_COMPONENT_FIELD_COLOR:
		return;

	default:
		g_message ("calendar_model_free_value(): Requested invalid column %d", col);
		return;
	}
}

/* Initializes a string value */
static char *
init_string (void)
{
	return g_strdup ("");
}

/* initialize_value handler for the calendar table model */
static void *
calendar_model_initialize_value (ETableModel *etm, int col)
{
	g_return_val_if_fail (col >= 0 && col < CAL_COMPONENT_FIELD_NUM_FIELDS, NULL);

	switch (col) {
	case CAL_COMPONENT_FIELD_CATEGORIES:
		return init_string ();

	case CAL_COMPONENT_FIELD_CLASSIFICATION:
		return NULL;

	case CAL_COMPONENT_FIELD_COMPLETED:
	case CAL_COMPONENT_FIELD_DTEND:
	case CAL_COMPONENT_FIELD_DTSTART:
	case CAL_COMPONENT_FIELD_DUE:
	case CAL_COMPONENT_FIELD_GEO:
	case CAL_COMPONENT_FIELD_PERCENT:
	case CAL_COMPONENT_FIELD_PRIORITY:
	case CAL_COMPONENT_FIELD_SUMMARY:
		return init_string ();

	case CAL_COMPONENT_FIELD_TRANSPARENCY:
		return NULL;

	case CAL_COMPONENT_FIELD_URL:
		return init_string ();

	case CAL_COMPONENT_FIELD_HAS_ALARMS:
	case CAL_COMPONENT_FIELD_ICON:
	case CAL_COMPONENT_FIELD_COMPLETE:
	case CAL_COMPONENT_FIELD_RECURRING:
	case CAL_COMPONENT_FIELD_OVERDUE:
	case CAL_COMPONENT_FIELD_COLOR:
		return NULL;

	default:
		g_message ("calendar_model_initialize_value(): Requested invalid column %d", col);
		return NULL;
	}
}

/* value_is_empty handler for the calendar model. This should return TRUE
   unless a significant value has been set. The 'click-to-add' feature
   checks all fields to see if any are not empty and if so it adds a new
   row, so we only want to return FALSE if we have a useful object. */
static gboolean
calendar_model_value_is_empty (ETableModel *etm, int col, const void *value)
{
	g_return_val_if_fail (col >= 0 && col < CAL_COMPONENT_FIELD_NUM_FIELDS, TRUE);

	switch (col) {
	case CAL_COMPONENT_FIELD_CATEGORIES:
	case CAL_COMPONENT_FIELD_CLASSIFICATION: /* actually goes here, not by itself */
	case CAL_COMPONENT_FIELD_COMPLETED:
	case CAL_COMPONENT_FIELD_DTEND:
	case CAL_COMPONENT_FIELD_DTSTART:
	case CAL_COMPONENT_FIELD_DUE:
	case CAL_COMPONENT_FIELD_GEO:
	case CAL_COMPONENT_FIELD_PERCENT:
	case CAL_COMPONENT_FIELD_PRIORITY:
	case CAL_COMPONENT_FIELD_SUMMARY:
	case CAL_COMPONENT_FIELD_TRANSPARENCY:
	case CAL_COMPONENT_FIELD_URL:
		return string_is_empty (value);

	case CAL_COMPONENT_FIELD_HAS_ALARMS:
	case CAL_COMPONENT_FIELD_ICON:
	case CAL_COMPONENT_FIELD_COMPLETE:
	case CAL_COMPONENT_FIELD_RECURRING:
	case CAL_COMPONENT_FIELD_OVERDUE:
	case CAL_COMPONENT_FIELD_COLOR:
		return TRUE;

	default:
		g_message ("calendar_model_value_is_empty(): Requested invalid column %d", col);
		return TRUE;
	}
}



/**
 * calendar_model_new:
 *
 * Creates a new calendar model.  It must be told about the calendar client
 * interface object it will monitor with calendar_model_set_cal_client().
 *
 * Return value: A newly-created calendar model.
 **/
CalendarModel *
calendar_model_new (void)
{
	return CALENDAR_MODEL (gtk_type_new (TYPE_CALENDAR_MODEL));
}


/* Callback used when a calendar is loaded into the server */
static void
cal_loaded_cb (CalClient *client,
	       CalClientLoadStatus status,
	       CalendarModel *model)
{
	g_return_if_fail (IS_CALENDAR_MODEL (model));

	e_table_model_pre_change (E_TABLE_MODEL (model));

	if (status == CAL_CLIENT_LOAD_SUCCESS)
		load_objects (model);

	e_table_model_changed (E_TABLE_MODEL (model));
}


/* Removes an object from the model and updates all the indices that follow.
 * Returns the index of the object that was removed, or -1 if no object with
 * such UID was found.
 */
static int
remove_object (CalendarModel *model, const char *uid)
{
	CalendarModelPrivate *priv;
	int *idx;
	CalComponent *orig_comp;
	int i;
	int n;

	priv = model->priv;

	/* Find the index of the object to be removed */

	idx = g_hash_table_lookup (priv->uid_index_hash, uid);
	if (!idx)
		return -1;

	orig_comp = g_array_index (priv->objects, CalComponent *, *idx);
	g_assert (orig_comp != NULL);

	/* Decrease the indices of all the objects that follow in the array */

	for (i = *idx + 1; i < priv->objects->len; i++) {
		CalComponent *comp;
		int *comp_idx;
		const char *comp_uid;

		comp = g_array_index (priv->objects, CalComponent *, i);
		g_assert (comp != NULL);

		cal_component_get_uid (comp, &comp_uid);

		comp_idx = g_hash_table_lookup (priv->uid_index_hash, comp_uid);
		g_assert (comp_idx != NULL);

		(*comp_idx)--;
		g_assert (*comp_idx >= 0);
	}

	/* Remove this object from the array and hash */

	g_hash_table_remove (priv->uid_index_hash, uid);
	g_array_remove_index (priv->objects, *idx);

	gtk_object_unref (GTK_OBJECT (orig_comp));

	n = *idx;
	g_free (idx);

	return n;
}

/* Returns whether a component's type matches the types we support */
static gboolean
matches_type (CalObjType type, CalComponentVType vtype)
{
	return ((vtype == CAL_COMPONENT_EVENT && (type & CALOBJ_TYPE_EVENT))
		|| (vtype == CAL_COMPONENT_TODO && (type & CALOBJ_TYPE_TODO))
		|| (vtype == CAL_COMPONENT_JOURNAL && (type & CALOBJ_TYPE_JOURNAL)));
}

/* Callback used when an object is updated in the server */
static void
obj_updated_cb (CalClient *client, const char *uid, gpointer data)
{
	CalendarModel *model;
	CalendarModelPrivate *priv;
	int orig_idx;
	CalComponent *new_comp;
	CalComponentVType new_comp_vtype;
	const char *new_comp_uid;
	int *new_idx;
	CalClientGetStatus status;

	model = CALENDAR_MODEL (data);
	priv = model->priv;

	orig_idx = remove_object (model, uid);

	status = cal_client_get_object (priv->client, uid, &new_comp);

	switch (status) {
	case CAL_CLIENT_GET_SUCCESS:
		/* Check if we are interested in this type of object */

		new_comp_vtype = cal_component_get_vtype (new_comp);
		if (!matches_type (priv->type, new_comp_vtype)) {
			gtk_object_unref (GTK_OBJECT (new_comp));
			break;
		}

		/* Insert the object into the model */

		cal_component_get_uid (new_comp, &new_comp_uid);

		if (orig_idx == -1) {
			/* The object not in the model originally, so we just append it */

			g_array_append_val (priv->objects, new_comp);

			new_idx = g_new (int, 1);
			*new_idx = priv->objects->len - 1;

			g_hash_table_insert (priv->uid_index_hash, (char *) new_comp_uid, new_idx);
		} else {
			int i;

			/* Insert the new version of the object in its old position */

			g_array_insert_val (priv->objects, orig_idx, new_comp);

			new_idx = g_new (int, 1);
			*new_idx = orig_idx;
			g_hash_table_insert (priv->uid_index_hash, (char *) new_comp_uid, new_idx);

			/* Increase the indices of all subsequent objects */

			for (i = orig_idx + 1; i < priv->objects->len; i++) {
				CalComponent *comp;
				int *comp_idx;
				const char *comp_uid;

				comp = g_array_index (priv->objects, CalComponent *, i);
				g_assert (comp != NULL);

				cal_component_get_uid (comp, &comp_uid);

				comp_idx = g_hash_table_lookup (priv->uid_index_hash, comp_uid);
				g_assert (comp_idx != NULL);

				(*comp_idx)++;
			}
		}

		e_table_model_row_changed (E_TABLE_MODEL (model), *new_idx);
		break;

	case CAL_CLIENT_GET_NOT_FOUND:
		/* Nothing; the object may have been removed from the server.  We just
		 * notify that the old object was deleted.
		 */
		if (orig_idx != -1)
			e_table_model_row_deleted (E_TABLE_MODEL (model), orig_idx);

		break;

	case CAL_CLIENT_GET_SYNTAX_ERROR:
		g_message ("obj_updated_cb(): Syntax error when getting object `%s'", uid);

		/* Same notification as above */
		if (orig_idx != -1)
			e_table_model_row_deleted (E_TABLE_MODEL (model), orig_idx);

		break;

	default:
		g_assert_not_reached ();
	}
}

/* Callback used when an object is removed in the server */
static void
obj_removed_cb (CalClient *client, const char *uid, gpointer data)
{
	CalendarModel *model;
	int idx;

	model = CALENDAR_MODEL (data);

	idx = remove_object (model, uid);

	if (idx != -1)
		e_table_model_row_deleted (E_TABLE_MODEL (model), idx);
}

/* Loads the required objects from the calendar client */
static void
load_objects (CalendarModel *model)
{
	CalendarModelPrivate *priv;
	GList *uids;
	GList *l;

	priv = model->priv;

	if (!cal_client_is_loaded (priv->client))
		return;

	uids = cal_client_get_uids (priv->client, priv->type);

	for (l = uids; l; l = l->next) {
		char *uid;
		CalComponent *comp;
		const char *comp_uid;
		CalClientGetStatus status;
		CalComponentVType comp_vtype;
		int *idx;

		uid = l->data;
		status = cal_client_get_object (priv->client, uid, &comp);

		switch (status) {
		case CAL_CLIENT_GET_SUCCESS:
			break;

		case CAL_CLIENT_GET_NOT_FOUND:
			/* Nothing; the object may have been removed from the server */
			continue;

		case CAL_CLIENT_GET_SYNTAX_ERROR:
			g_message ("load_objects(): Syntax error when getting object `%s'", uid);
			continue;

		default:
			g_assert_not_reached ();
		}

		/* Check if we are interested in this type of object */

		comp_vtype = cal_component_get_vtype (comp);
		if (!matches_type (priv->type, comp_vtype)) {
			gtk_object_unref (GTK_OBJECT (comp));
			continue;
		}

		/* Insert the object into the model */

		idx = g_new (int, 1);

		g_array_append_val (priv->objects, comp);
		*idx = priv->objects->len - 1;

		cal_component_get_uid (comp, &comp_uid);
		g_hash_table_insert (priv->uid_index_hash, (char *) comp_uid, idx);
	}

	cal_obj_uid_list_free (uids);
}

/**
 * calendar_model_get_cal_client:
 * @model: A calendar model.
 * 
 * Queries the calendar client interface object that a calendar model is using.
 * 
 * Return value: A calendar client interface object.
 **/
CalClient *
calendar_model_get_cal_client (CalendarModel *model)
{
	CalendarModelPrivate *priv;

	g_return_val_if_fail (model != NULL, NULL);
	g_return_val_if_fail (IS_CALENDAR_MODEL (model), NULL);

	priv = model->priv;

	return priv->client;
}


/**
 * calendar_model_set_cal_client:
 * @model: A calendar model.
 * @client: A calendar client interface object.
 * @type: Type of objects to present.
 *
 * Sets the calendar client interface object that a calendar model will monitor.
 * It also sets the types of objects this model will present to an #ETable.
 **/
void
calendar_model_set_cal_client (CalendarModel *model, CalClient *client, CalObjType type)
{
	CalendarModelPrivate *priv;

	g_return_if_fail (model != NULL);
	g_return_if_fail (IS_CALENDAR_MODEL (model));

	if (client)
		g_return_if_fail (IS_CAL_CLIENT (client));

	priv = model->priv;

	if (priv->client == client && priv->type == type)
		return;

	e_table_model_pre_change (E_TABLE_MODEL(model));

	if (client)
		gtk_object_ref (GTK_OBJECT (client));

	if (priv->client) {
		gtk_signal_disconnect_by_data (GTK_OBJECT (priv->client), model);
		gtk_object_unref (GTK_OBJECT (priv->client));
	}

	free_objects (model);

	priv->client = client;
	priv->type = type;

	if (priv->client) {
		gtk_signal_connect (GTK_OBJECT (priv->client), "cal_loaded",
				    GTK_SIGNAL_FUNC (cal_loaded_cb), model);
		gtk_signal_connect (GTK_OBJECT (priv->client), "obj_updated",
				    GTK_SIGNAL_FUNC (obj_updated_cb), model);
		gtk_signal_connect (GTK_OBJECT (priv->client), "obj_removed",
				    GTK_SIGNAL_FUNC (obj_removed_cb), model);

		load_objects (model);
	}

	e_table_model_changed (E_TABLE_MODEL (model));
}


void
calendar_model_delete_task (CalendarModel *model,
			    gint row)
{
	CalendarModelPrivate *priv;
	CalComponent *comp;
	const char *uid;

	g_return_if_fail (model != NULL);
	g_return_if_fail (IS_CALENDAR_MODEL (model));

	priv = model->priv;

	g_return_if_fail (row >= 0 && row < priv->objects->len);

	comp = g_array_index (priv->objects, CalComponent *, row);
	g_assert (comp != NULL);

	cal_component_get_uid (comp, &uid);

	/* We don't check the return value; FALSE can mean the object was not in
	 * the server anyways.
	 */
	cal_client_remove_object (priv->client, uid);
}


void
calendar_model_mark_task_complete (CalendarModel *model,
				   gint row)
{
	CalendarModelPrivate *priv;
	CalComponent *comp;

	g_return_if_fail (model != NULL);
	g_return_if_fail (IS_CALENDAR_MODEL (model));

	priv = model->priv;

	g_return_if_fail (row >= 0 && row < priv->objects->len);

	comp = g_array_index (priv->objects, CalComponent *, row);
	g_assert (comp != NULL);

	ensure_task_complete (comp, -1);

	if (!cal_client_update_object (priv->client, comp))
		g_message ("calendar_model_mark_task_complete(): Could not update the object!");
}


/* Frees the objects stored in the calendar model */
CalComponent *
calendar_model_get_cal_object (CalendarModel *model,
			       gint	      row)
{
	CalendarModelPrivate *priv;

	g_return_val_if_fail (model != NULL, NULL);
	g_return_val_if_fail (IS_CALENDAR_MODEL (model), NULL);

	priv = model->priv;

	g_return_val_if_fail (row >= 0 && row < priv->objects->len, NULL);

	return g_array_index (priv->objects, CalComponent *, row);
}


/* This makes sure a task is marked as complete.
   It makes sure the "Date Completed" property is set. If the completed_date
   is not -1, then that is used, otherwise if the "Date Completed" property
   is not already set it is set to the current time.
   It makes sure the percent is set to 100, and that the status is "Completed".
   Note that this doesn't update the component on the client. */
static void
ensure_task_complete (CalComponent *comp,
		      time_t completed_date)
{
	struct icaltimetype *old_completed = NULL;
	struct icaltimetype new_completed;
	const char *old_status;
	int *old_percent, new_percent;
	gboolean set_completed = TRUE;

	/* Date Completed. */
	if (completed_date == -1) {
		cal_component_get_completed (comp, &old_completed);

		if (old_completed) {
			cal_component_free_icaltimetype (old_completed);
			set_completed = FALSE;
		} else {
			completed_date = time (NULL);
		}
	}

	if (set_completed) {
		new_completed = icaltime_from_timet (completed_date, FALSE,
						     TRUE);
		cal_component_set_completed (comp, &new_completed);
	}

	/* Percent. */
	cal_component_get_percent (comp, &old_percent);
	if (!old_percent || *old_percent != 100) {
		new_percent = 100;
		cal_component_set_percent (comp, &new_percent);
	}
	if (old_percent)
		cal_component_free_percent (old_percent);

	/* Status. */
	cal_component_get_status (comp, &old_status);
}


/* This makes sure a task is marked as incomplete. It clears the
   "Date Completed" property. If the percent is set to 100 it removes it,
   and if the status is "Completed" it sets it to "Needs Action".
   Note that this doesn't update the component on the client. */
static void
ensure_task_not_complete (CalComponent *comp)
{
	const char *old_status;
	int *old_percent;

	/* Date Completed. */
	cal_component_set_completed (comp, NULL);

	/* Percent. */
	cal_component_get_percent (comp, &old_percent);
	if (old_percent && *old_percent == 100)
		cal_component_set_percent (comp, NULL);
	if (old_percent)
		cal_component_free_percent (old_percent);

	/* Status. */
	cal_component_get_status (comp, &old_status);
	if (old_status && !strcmp (old_status, "COMPLETED"))
		cal_component_set_status (comp, "NEEDS-ACTION");
}


/* Whether we use 24 hour format to display the times. */
gboolean
calendar_model_get_use_24_hour_format (CalendarModel *model)
{
	g_return_val_if_fail (IS_CALENDAR_MODEL (model), TRUE);

	return model->priv->use_24_hour_format;
}


void
calendar_model_set_use_24_hour_format (CalendarModel *model,
				       gboolean	      use_24_hour_format)
{
	g_return_if_fail (IS_CALENDAR_MODEL (model));

	if (model->priv->use_24_hour_format != use_24_hour_format) {
		model->priv->use_24_hour_format = use_24_hour_format;
		/* Get the views to redraw themselves. */
		e_table_model_changed (E_TABLE_MODEL (model));
	}
}

