/* Evolution calendar - Utilities for manipulating CalComponent objects
 *
 * Copyright (C) 2000 Ximian, Inc.
 * Copyright (C) 2000 Ximian, Inc.
 *
 * Author: Federico Mena-Quintero <federico@ximian.com>
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

#include "calendar-config.h"
#include "comp-util.h"
#include "dialogs/delete-comp.h"



/**
 * cal_comp_util_add_exdate:
 * @comp: A calendar component object.
 * @itt: Time for the exception.
 * 
 * Adds an exception date to the current list of EXDATE properties in a calendar
 * component object.
 **/
void
cal_comp_util_add_exdate (CalComponent *comp, time_t t, icaltimezone *zone)
{
	GSList *list;
	CalComponentDateTime *cdt;

	g_return_if_fail (comp != NULL);
	g_return_if_fail (IS_CAL_COMPONENT (comp));

	cal_component_get_exdate_list (comp, &list);

	cdt = g_new (CalComponentDateTime, 1);
	cdt->value = g_new (struct icaltimetype, 1);
	*cdt->value = icaltime_from_timet_with_zone (t, FALSE, zone);
	cdt->tzid = g_strdup (icaltimezone_get_tzid (zone));

	list = g_slist_append (list, cdt);
	cal_component_set_exdate_list (comp, list);
	cal_component_free_exdate_list (list);
}



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

/**
 * cal_comp_util_compare_event_timezones:
 * @comp: A calendar component object.
 * @client: A #CalClient.
 * 
 * Checks if the component uses the given timezone for both the start and
 * the end time, or if the UTC offsets of the start and end times are the same
 * as in the given zone.
 *
 * Returns: TRUE if the component's start and end time are at the same UTC
 * offset in the given timezone.
 **/
gboolean
cal_comp_util_compare_event_timezones (CalComponent *comp,
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

/**
 * cal_comp_confirm_delete_empty_comp:
 * @comp: A calendar component.
 * @client: Calendar client where the component purportedly lives.
 * @widget: Widget to be used as the basis for UTF8 conversion.
 * 
 * Assumming a calendar component with an empty SUMMARY property (as per
 * string_is_empty()), asks whether the user wants to delete it based on
 * whether the appointment is on the calendar server or not.  If the
 * component is on the server, this function will present a confirmation
 * dialog and delete the component if the user tells it to.  If the component
 * is not on the server it will just return TRUE.
 * 
 * Return value: A result code indicating whether the component
 * was not on the server and is to be deleted locally, whether it
 * was on the server and the user deleted it, or whether the
 * user cancelled the deletion.
 **/
ConfirmDeleteEmptyCompResult
cal_comp_confirm_delete_empty_comp (CalComponent *comp, CalClient *client, GtkWidget *widget)
{
	const char *uid;
	CalClientGetStatus status;
	CalComponent *server_comp;

	g_return_val_if_fail (comp != NULL, EMPTY_COMP_DO_NOT_REMOVE);
	g_return_val_if_fail (IS_CAL_COMPONENT (comp), EMPTY_COMP_DO_NOT_REMOVE);
	g_return_val_if_fail (client != NULL, EMPTY_COMP_DO_NOT_REMOVE);
	g_return_val_if_fail (IS_CAL_CLIENT (client), EMPTY_COMP_DO_NOT_REMOVE);
	g_return_val_if_fail (widget != NULL, EMPTY_COMP_DO_NOT_REMOVE);
	g_return_val_if_fail (GTK_IS_WIDGET (widget), EMPTY_COMP_DO_NOT_REMOVE);

	/* See if the component is on the server.  If it is not, then it likely
	 * means that the appointment is new, only in the day view, and we
	 * haven't added it yet to the server.  In that case, we don't need to
	 * confirm and we can just delete the event.  Otherwise, we ask
	 * the user.
	 */
	cal_component_get_uid (comp, &uid);

	status = cal_client_get_object (client, uid, &server_comp);

	switch (status) {
	case CAL_CLIENT_GET_SUCCESS:
		gtk_object_unref (GTK_OBJECT (server_comp));
		/* Will handle confirmation below */
		break;

	case CAL_CLIENT_GET_SYNTAX_ERROR:
		g_message ("confirm_delete_empty_appointment(): Syntax error when getting "
			   "object `%s'",
			   uid);
		/* However, the object *is* in the server, so confirm */
		break;

	case CAL_CLIENT_GET_NOT_FOUND:
		return EMPTY_COMP_REMOVE_LOCALLY;

	default:
		g_assert_not_reached ();
	}

	/* The event exists in the server, so confirm whether to delete it */

	if (delete_component_dialog (comp, TRUE, 1, CAL_COMPONENT_EVENT, widget)) {
		cal_client_remove_object (client, uid);
		return EMPTY_COMP_REMOVED_FROM_SERVER;
	} else
		return EMPTY_COMP_DO_NOT_REMOVE;
}

/**
 * cal_comp_event_new_with_defaults:
 * 
 * Creates a new VEVENT component and adds any default alarms to it as set in
 * the program's configuration values.
 * 
 * Return value: A newly-created calendar component.
 **/
CalComponent *
cal_comp_event_new_with_defaults (void)
{
	CalComponent *comp;
	int interval;
	CalUnits units;
	CalComponentAlarm *alarm;
	CalAlarmTrigger trigger;

	comp = cal_component_new ();

	cal_component_set_new_vtype (comp, CAL_COMPONENT_EVENT);

	if (!calendar_config_get_use_default_reminder ())
		return comp;

	interval = calendar_config_get_default_reminder_interval ();
	units = calendar_config_get_default_reminder_units ();

	alarm = cal_component_alarm_new ();

	/* We don't set the description of the alarm; we'll copy it from the
	 * summary when it gets committed to the server.
	 */

	cal_component_alarm_set_action (alarm, CAL_ALARM_DISPLAY);

	trigger.type = CAL_ALARM_TRIGGER_RELATIVE_START;

	memset (&trigger.u.rel_duration, 0, sizeof (trigger.u.rel_duration));

	trigger.u.rel_duration.is_neg = TRUE;

	switch (units) {
	case CAL_MINUTES:
		trigger.u.rel_duration.minutes = interval;
		break;

	case CAL_HOURS:	
		trigger.u.rel_duration.hours = interval;
		break;

	case CAL_DAYS:	
		trigger.u.rel_duration.days = interval;
		break;

	default:
		g_assert_not_reached ();
	}

	cal_component_alarm_set_trigger (alarm, trigger);

	cal_component_add_alarm (comp, alarm);
	cal_component_alarm_free (alarm);

	return comp;
}
