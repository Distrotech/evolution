/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-summary-calendar.c
 *
 * Copyright (C) 2001 Ximian, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
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
 * Author: Iain Holmes  <iain@ximian.com>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gnome.h>
#include <gal/widgets/e-unicode.h>

#include "e-summary-calendar.h"
#include "e-summary.h"
#include <cal-client/cal-client.h>
#include <cal-util/timeutil.h>

#include <bonobo/bonobo-exception.h>
#include <bonobo/bonobo-moniker-util.h>
#include <bonobo-conf/bonobo-config-database.h>

#include <liboaf/liboaf.h>

#include <ical.h>

struct _ESummaryCalendar {
	CalClient *client;

	char *html;
	gboolean wants24hr;
};

const char *
e_summary_calendar_get_html (ESummary *summary)
{
	if (summary->calendar == NULL) {
		return NULL;
	}

	return summary->calendar->html;
}

typedef struct {
	char *uid;
	CalComponent *comp;
	CalComponentDateTime dt;
	icaltimezone *zone;
} ESummaryCalEvent;

/* Returns TRUE if the TZIDs are equivalent, i.e. both NULL or the same. */
static gboolean
cal_component_compare_tzid (const char *tzid1, const char *tzid2)
{
        gboolean retval = TRUE;

        if (tzid1) {
                if (!tzid2 || strcmp (tzid1, tzid2))
                        retval = FALSE;
        } else {
                if (tzid2)
                        retval = FALSE;
        }

        return retval;
}

gboolean
e_cal_comp_util_compare_event_timezones (CalComponent *comp,
					 CalClient *client,
					 icaltimezone *zone)
{
        CalClientGetStatus status;
        CalComponentDateTime start_datetime, end_datetime;
        const char *tzid;
        gboolean retval = FALSE;
        icaltimezone *start_zone, *end_zone;
        int offset1, offset2;

        tzid = icaltimezone_get_tzid (zone);

        cal_component_get_dtstart (comp, &start_datetime);
        cal_component_get_dtend (comp, &end_datetime);

	/* If either the DTSTART or the DTEND is a DATE value, we return TRUE.
	   Maybe if one was a DATE-TIME we should check that, but that should
	   not happen often. */
	if (start_datetime.value->is_date || end_datetime.value->is_date) {
		retval = TRUE;
		goto out;
	}

        /* FIXME: DURATION may be used instead. */
        if (cal_component_compare_tzid (tzid, start_datetime.tzid)
            && cal_component_compare_tzid (tzid, end_datetime.tzid)) {
                /* If both TZIDs are the same as the given zone's TZID, then
                   we know the timezones are the same so we return TRUE. */
                retval = TRUE;
        } else {
                /* If the TZIDs differ, we have to compare the UTC offsets
                   of the start and end times, using their own timezones and
                   the given timezone. */
                status = cal_client_get_timezone (client,
                                                  start_datetime.tzid,
                                                  &start_zone);
                if (status != CAL_CLIENT_GET_SUCCESS)
                        goto out;

                offset1 = icaltimezone_get_utc_offset (start_zone,
                                                       start_datetime.value,
                                                       NULL);
                offset2 = icaltimezone_get_utc_offset (zone,
                                                       start_datetime.value,
                                                       NULL);
                if (offset1 == offset2) {
                        status = cal_client_get_timezone (client,
                                                          end_datetime.tzid,
                                                          &end_zone);
                        if (status != CAL_CLIENT_GET_SUCCESS)
                                goto out;

                        offset1 = icaltimezone_get_utc_offset (end_zone,
                                                               end_datetime.value,
                                                               NULL);
                        offset2 = icaltimezone_get_utc_offset (zone,
                                                               end_datetime.value,
                                                               NULL);
                        if (offset1 == offset2)
                                retval = TRUE;
                }
        }

 out:

        cal_component_free_datetime (&start_datetime);
        cal_component_free_datetime (&end_datetime);

        return retval;
}

static int
e_summary_calendar_event_sort_func (const void *e1,
				    const void *e2)
{
	ESummaryCalEvent *event1, *event2;

	event1 = *(ESummaryCalEvent **) e1;
	event2 = *(ESummaryCalEvent **) e2;

	if (event1->dt.value == NULL || event2->dt.value == NULL) {
		return 0;
	}

	return icaltime_compare (*(event1->dt.value), *(event2->dt.value));
}

struct _RecurData {
	ESummary *summary;
	GPtrArray *array;
	ESummaryCalEvent *event;
};

static gboolean
add_recurrances (CalComponent *comp,
		 time_t start,
		 time_t end,
		 gpointer data)
{
	struct _RecurData *recur = data;
	struct icaltimetype v, *p;
	ESummaryCalEvent *event;

	event = g_new (ESummaryCalEvent, 1);
	v = icaltime_from_timet_with_zone (start, FALSE, recur->summary->tz);
	p = g_new (struct icaltimetype, 1);

	event->dt.value = p;

	p->year = v.year;
	p->month = v.month;
	p->day = v.day;
	p->hour = v.hour;
	p->minute = v.minute;
	p->second = v.second;
	p->is_utc = v.is_utc;
	p->is_date = v.is_date;
	p->is_daylight = v.is_daylight;
	p->zone = v.zone;
	
	event->dt.tzid = recur->summary->timezone;
	event->comp = comp;
	event->uid = g_strdup (recur->event->uid);
	event->zone = recur->summary->tz;

	gtk_object_ref (GTK_OBJECT (comp));

	g_ptr_array_add (recur->array, event);
	return TRUE;
}

static GPtrArray *
uids_to_array (ESummary *summary,
	       CalClient *client,
	       GList *uids,
	       time_t begin,
	       time_t end)
{
	GList *p;
	GPtrArray *array;

	g_return_val_if_fail (IS_E_SUMMARY (summary), NULL);
	g_return_val_if_fail (client != NULL, NULL);
	g_return_val_if_fail (uids != NULL, NULL);

	array = g_ptr_array_new ();
	for (p = uids; p; p = p->next) {
		ESummaryCalEvent *event;
		CalClientGetStatus status;

		event = g_new (ESummaryCalEvent, 1);

		event->uid = g_strdup (p->data);
		status = cal_client_get_object (client, p->data, &event->comp);
		if (status != CAL_CLIENT_GET_SUCCESS) {
			g_free (event);
			continue;
		}

		if (cal_component_has_recurrences (event->comp) == TRUE) {
			struct _RecurData *recur;

			recur = g_new (struct _RecurData, 1);
			recur->event = event;
			recur->array = array;
			recur->summary = summary;
			cal_recur_generate_instances (event->comp, begin, end,
						      add_recurrances, recur,
						      cal_client_resolve_tzid_cb, client,
						      recur->summary->tz);
			g_free (recur);
			g_free (event->uid);
			g_free (event);
		} else {
			cal_component_get_dtstart (event->comp, &event->dt);
			
			status = cal_client_get_timezone (client, event->dt.tzid, &event->zone);
			if (status != CAL_CLIENT_GET_SUCCESS) {
				gtk_object_unref (GTK_OBJECT (event->comp));
				g_free (event);
				continue;
			}
			
			icaltimezone_convert_time (event->dt.value, event->zone, summary->tz);
			g_ptr_array_add (array, event);
		}
	}

	qsort (array->pdata, array->len, sizeof (ESummaryCalEvent *), e_summary_calendar_event_sort_func);

	return array;
}

static void
free_event_array (GPtrArray *array)
{
	int i;

	for (i = 0; i < array->len; i++) {
		ESummaryCalEvent *event;
		
		event = array->pdata[i];
		g_free (event->uid);
		gtk_object_unref (GTK_OBJECT (event->comp));
	}

	g_ptr_array_free (array, TRUE);
}

static gboolean
generate_html (gpointer data)
{
	ESummary *summary = data;
	ESummaryCalendar *calendar = summary->calendar;
	GList *uids;
	GString *string;
	char *tmp;
	time_t t, begin, end, f;

	/* Set the default timezone on the server. */
	if (summary->tz) {
		cal_client_set_default_timezone (calendar->client,
						 summary->tz);
	}

	t = time (NULL);
	begin = time_day_begin_with_zone (t, summary->tz);
	switch (summary->preferences->days) {
	case E_SUMMARY_CALENDAR_ONE_DAY:
		end = time_day_end_with_zone (t, summary->tz);
		break;

	case E_SUMMARY_CALENDAR_FIVE_DAYS:
		f = time_add_day_with_zone (t, 5, summary->tz);
		end = time_day_end_with_zone (f, summary->tz);
		break;
	
	case E_SUMMARY_CALENDAR_ONE_WEEK:
		f = time_add_week_with_zone (t, 1, summary->tz);
		end = time_day_end_with_zone (f, summary->tz);
		break;

	case E_SUMMARY_CALENDAR_ONE_MONTH:
	default:
		f = time_add_month_with_zone (t, 1, summary->tz);
		end = time_day_end_with_zone (f, summary->tz);
		break;
	}

	uids = cal_client_get_objects_in_range (calendar->client, 
						CALOBJ_TYPE_EVENT, begin, end);
	if (uids == NULL) {
		char *s1, *s2;

		s1 = e_utf8_from_locale_string (_("Appointments"));
		s2 = e_utf8_from_locale_string (_("No appointments"));
		g_free (calendar->html);
		calendar->html = g_strconcat ("<dl><dt><img src=\"myevo-appointments.png\" align=\"middle\" "
		                              "alt=\"\" width=\"48\" height=\"48\"> <b><a href=\"evolution:/local/Calendar\">",
		                              s1, "</a></b></dt><dd><b>", s2, "</b></dd></dl>", NULL);
		g_free (s1);
		g_free (s2);

		e_summary_draw (summary);
		return FALSE;
	} else {
		GPtrArray *uidarray;
		int i;
		char *s;

		string = g_string_new ("<dl><dt><img src=\"myevo-appointments.png\" align=\"middle\" "
		                       "alt=\"\" width=\"48\" height=\"48\"> <b><a href=\"evolution:/local/Calendar\">");
		s = e_utf8_from_locale_string (_("Appointments"));
		g_string_append (string, s);
		g_free (s);
		g_string_append (string, "</a></b></dt><dd>");

		uidarray = uids_to_array (summary, calendar->client, uids, begin, end);
		for (i = 0; i < uidarray->len; i++) {
			ESummaryCalEvent *event;
			CalComponentText text;
			struct tm start_tm;
			char start_str[64], *start_str_utf, *img;

			event = uidarray->pdata[i];
			cal_component_get_summary (event->comp, &text);

			start_tm = icaltimetype_to_tm (event->dt.value);
			if (calendar->wants24hr == TRUE) {
				strftime (start_str, sizeof start_str, _("%k:%M %d %B"), &start_tm);
			} else {
				strftime (start_str, sizeof start_str, _("%l:%M %d %B"), &start_tm);
			}

			if (cal_component_has_alarms (event->comp)) {
				img = "es-appointments.png";
			} else if (e_cal_comp_util_compare_event_timezones (event->comp,
									    calendar->client,
									    summary->tz) == FALSE) {
				img = "timezone-16.xpm";
			} else {
				img = "new_appointment.xpm";
			}

			start_str_utf = e_utf8_from_locale_string (start_str);
			tmp = g_strdup_printf ("<img align=\"middle\" src=\"%s\" "
					       "alt=\"\" width=\"16\" height=\"16\">  &#160; "
					       "<font size=\"-1\"><a href=\"calendar:/%s\">%s, %s</a></font><br>", 
					       img,
					       event->uid, start_str_utf, text.value ? text.value : _("No description"));
			g_free (start_str_utf);
			
			g_string_append (string, tmp);
			g_free (tmp);
		}

		free_event_array (uidarray);
		g_string_append (string, "</dd></dl>");
	}

	if (calendar->html) {
		g_free (calendar->html);
	}
	calendar->html = string->str;
	g_string_free (string, FALSE);

	e_summary_draw (summary);
	return FALSE;
}

static void
cal_opened_cb (CalClient *client,
	       CalClientOpenStatus status,
	       ESummary *summary)
{
	if (status == CAL_CLIENT_OPEN_SUCCESS) {
		g_idle_add (generate_html, summary);
	} else {
		/* Need to work out what to do if there's an error */
	}
}
static void
obj_changed_cb (CalClient *client,
		const char *uid,
		gpointer data)
{
	g_idle_add (generate_html, data);
}

static void
e_summary_calendar_protocol (ESummary *summary,
			     const char *uri,
			     void *closure)
{
	ESummaryCalendar *calendar;
	CORBA_Environment ev;
	const char *comp_uri;
	GNOME_Evolution_Calendar_CompEditorFactory factory;

	calendar = (ESummaryCalendar *) closure;

	comp_uri = cal_client_get_uri (calendar->client);

	/* Get the factory */
	CORBA_exception_init (&ev);
	factory = oaf_activate_from_id ("OAFIID:GNOME_Evolution_Calendar_CompEditorFactory", 0, NULL, &ev);
	if (BONOBO_EX (&ev)) {
		g_message ("%s: Could not activate the component editor factory (%s)", __FUNCTION__,
			   CORBA_exception_id (&ev));
		CORBA_exception_free (&ev);
		return;
	}

	GNOME_Evolution_Calendar_CompEditorFactory_editExisting (factory, comp_uri, (char *)uri + 10, &ev);

	if (BONOBO_EX (&ev)) {
		g_message ("%s: Execption while editing the component (%s)", __FUNCTION__, 
			   CORBA_exception_id (&ev));
	}

	CORBA_exception_free (&ev);
	bonobo_object_release_unref (factory, NULL);
}

static gboolean
locale_uses_24h_time_format (void)
{
	char s[16];
	time_t t = 0;

	strftime (s, sizeof s, "%p", gmtime (&t));
	return s[0] == '\0';
}

void
e_summary_calendar_init (ESummary *summary)
{
	Bonobo_ConfigDatabase db;
	CORBA_Environment ev;
	ESummaryCalendar *calendar;
	gboolean result;

	g_return_if_fail (summary != NULL);

	calendar = g_new (ESummaryCalendar, 1);
	summary->calendar = calendar;
	calendar->html = NULL;

	CORBA_exception_init (&ev);
	db = bonobo_get_object ("wombat:", "Bonobo/ConfigDatabase", &ev);
	if (BONOBO_EX (&ev) || db == CORBA_OBJECT_NIL) {
		CORBA_exception_free (&ev);
		g_warning ("Error getting Wombat. Using defaults");
		return;
	}

	CORBA_exception_free (&ev);

	calendar->client = cal_client_new ();
	if (calendar->client == NULL) {
		bonobo_object_release_unref (db, NULL);
		g_warning ("Error making the client");
		return;
	}

	gtk_signal_connect (GTK_OBJECT (calendar->client), "cal-opened",
			    GTK_SIGNAL_FUNC (cal_opened_cb), summary);
	gtk_signal_connect (GTK_OBJECT (calendar->client), "obj-updated",
			    GTK_SIGNAL_FUNC (obj_changed_cb), summary);
	gtk_signal_connect (GTK_OBJECT (calendar->client), "obj-removed",
			    GTK_SIGNAL_FUNC (obj_changed_cb), summary);

	result = cal_client_open_default_calendar (calendar->client, FALSE);
	if (result == FALSE) {
		g_message ("Open calendar failed");
	}
	
	e_summary_add_protocol_listener (summary, "calendar", e_summary_calendar_protocol, calendar);

	calendar->wants24hr = bonobo_config_get_boolean_with_default (db, "/Calendar/Display/Use24HourFormat", locale_uses_24h_time_format (), NULL);
	bonobo_object_release_unref (db, NULL);
}

void
e_summary_calendar_reconfigure (ESummary *summary)
{
	generate_html (summary);
}

void
e_summary_calendar_free (ESummary *summary)
{
	ESummaryCalendar *calendar;

	g_return_if_fail (summary != NULL);
	g_return_if_fail (IS_E_SUMMARY (summary));

	calendar = summary->calendar;
	gtk_object_unref (GTK_OBJECT (calendar->client));
	g_free (calendar->html);

	g_free (calendar);
	summary->calendar = NULL;
}
