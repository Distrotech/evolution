/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Authors:
 *  JP Rosevear <jpr@ximian.com>
 *
 * Copyright 2001, Ximian, Inc.
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <bonobo/bonobo-exception.h>
#include <bonobo/bonobo-object.h>
#include <bonobo/bonobo-object-client.h>
#include <bonobo/bonobo-moniker-util.h>
#include <bonobo-conf/bonobo-config-database.h>
#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-i18n.h>
#include <gtk/gtkwidget.h>
#include <gal/widgets/e-gui-utils.h>
#include <gal/util/e-unicode-i18n.h>
#include <gal/util/e-util.h>
#include <ical.h>
#include <Evolution-Composer.h>
#include <e-util/e-time-utils.h>
#include <cal-util/timeutil.h>
#include <cal-util/cal-util.h>
#include "calendar-config.h"
#include "itip-utils.h"

#define GNOME_EVOLUTION_COMPOSER_OAFIID "OAFIID:GNOME_Evolution_Mail_Composer"

static gchar *itip_methods[] = {
	"PUBLISH",
	"REQUEST",
	"REPLY",
	"ADD",
	"CANCEL",
	"RERESH",
	"COUNTER",
	"DECLINECOUNTER"
};

static icalproperty_method itip_methods_enum[] = {
    ICAL_METHOD_PUBLISH,
    ICAL_METHOD_REQUEST,
    ICAL_METHOD_REPLY,
    ICAL_METHOD_ADD,
    ICAL_METHOD_CANCEL,
    ICAL_METHOD_REFRESH,
    ICAL_METHOD_COUNTER,
    ICAL_METHOD_DECLINECOUNTER,
};

static Bonobo_ConfigDatabase db = NULL;

static ItipAddress *
get_address (long num) 
{
	ItipAddress *a;
	gchar *path;
		
	a = g_new0 (ItipAddress, 1);

	/* get the identity info */
	path = g_strdup_printf ("/Mail/Accounts/identity_name_%ld", num);
	a->name = bonobo_config_get_string (db, path, NULL);
	g_free (path);

	path = g_strdup_printf ("/Mail/Accounts/identity_address_%ld", num);
	a->address = bonobo_config_get_string (db, path, NULL);
	g_free (path);

	a->full = g_strdup_printf ("%s <%s>", a->name, a->address);

	return a;
}

GList *
itip_addresses_get (void)
{

	CORBA_Environment ev;
	GList *addresses = NULL;
	glong len, def, i;

	if (db == NULL) {
		CORBA_exception_init (&ev);
 
		db = bonobo_get_object ("wombat:", 
					"Bonobo/ConfigDatabase", 
					&ev);
	
		if (BONOBO_EX (&ev) || db == CORBA_OBJECT_NIL) {
			CORBA_exception_free (&ev);
			return NULL;
		}
		
		CORBA_exception_free (&ev);
	}
	
	len = bonobo_config_get_long_with_default (db, "/Mail/Accounts/num", 0, NULL);
	def = bonobo_config_get_long_with_default (db, "/Mail/Accounts/default_account", 0, NULL);

	for (i = 0; i < len; i++) {
		ItipAddress *a;

		a = get_address (i);
		if (i == def)
			a->default_address = TRUE;

		addresses = g_list_append (addresses, a);
	}

	return addresses;
}

ItipAddress *
itip_addresses_get_default (void)
{
	CORBA_Environment ev;
	ItipAddress *a;
	glong def;

	if (db == NULL) {
		CORBA_exception_init (&ev);
 
		db = bonobo_get_object ("wombat:", 
					"Bonobo/ConfigDatabase", 
					&ev);
	
		if (BONOBO_EX (&ev) || db == CORBA_OBJECT_NIL) {
			CORBA_exception_free (&ev);
			return NULL;
		}
		
		CORBA_exception_free (&ev);
	}

	def = bonobo_config_get_long_with_default (db, "/Mail/Accounts/default_account", 0, NULL);
	a = get_address (def);
	a->default_address = TRUE;

	return a;
}

void
itip_address_free (ItipAddress *address) 
{
	g_free (address->name);
	g_free (address->address);
	g_free (address->full);
	g_free (address);
}

void
itip_addresses_free (GList *addresses)
{
	GList *l;
	
	for (l = addresses; l != NULL; l = l->next) {
		ItipAddress *a = l->data;
		itip_address_free (a);
	}
	g_list_free (addresses);
}

const gchar *
itip_strip_mailto (const gchar *address) 
{
	const gchar *text;
	
	if (address == NULL)
		return NULL;
	
	text = e_strstrcase (address, "mailto:");
	if (text != NULL && strlen (address) > 7)
		address += 7;

	return address;
}

static char *
get_label (struct icaltimetype *tt)
{
	char buffer[1000];
	struct tm tmp_tm = { 0 };
	
	tmp_tm.tm_year = tt->year - 1900;
	tmp_tm.tm_mon = tt->month - 1;
	tmp_tm.tm_mday = tt->day;
	tmp_tm.tm_hour = tt->hour;
	tmp_tm.tm_min = tt->minute;
	tmp_tm.tm_sec = tt->second;
	tmp_tm.tm_isdst = -1;

	tmp_tm.tm_wday = time_day_of_week (tt->day, tt->month - 1, tt->year);

	e_time_format_date_and_time (&tmp_tm,
				     calendar_config_get_24_hour_format (), 
				     FALSE, FALSE,
				     buffer, 1000);
	
	return g_strdup (buffer);
}

typedef struct {
	GHashTable *tzids;
	icalcomponent *icomp;	
} ItipUtilTZData;

static GNOME_Evolution_Composer_RecipientList *
comp_to_list (CalComponentItipMethod method, CalComponent *comp)
{
	GNOME_Evolution_Composer_RecipientList *to_list;
	GNOME_Evolution_Composer_Recipient *recipient;
	CalComponentOrganizer organizer;
	GSList *attendees, *l;
	gint cntr, len;

	switch (method) {
	case CAL_COMPONENT_METHOD_REQUEST:
	case CAL_COMPONENT_METHOD_CANCEL:
		cal_component_get_attendee_list (comp, &attendees);
		len = g_slist_length (attendees);
		if (len <= 0) {
			e_notice (NULL, GNOME_MESSAGE_BOX_ERROR,
				  _("Atleast one attendee is necessary"));
			cal_component_free_attendee_list (attendees);
			return NULL;
		}
		
		to_list = GNOME_Evolution_Composer_RecipientList__alloc ();
		to_list->_maximum = len;
		to_list->_length = len;
		to_list->_buffer = CORBA_sequence_GNOME_Evolution_Composer_Recipient_allocbuf (len);
		
		for (cntr = 0, l = attendees; cntr < len; cntr++, l = l->next) {
			CalComponentAttendee *att = l->data;
			
			recipient = &(to_list->_buffer[cntr]);
			if (att->cn)
				recipient->name = CORBA_string_dup (att->cn);
			else
				recipient->name = CORBA_string_dup ("");
			recipient->address = CORBA_string_dup (itip_strip_mailto (att->value));
		}
		cal_component_free_attendee_list (attendees);
		break;

	case CAL_COMPONENT_METHOD_REPLY:
	case CAL_COMPONENT_METHOD_ADD:
	case CAL_COMPONENT_METHOD_REFRESH:
	case CAL_COMPONENT_METHOD_COUNTER:
	case CAL_COMPONENT_METHOD_DECLINECOUNTER:
		cal_component_get_organizer (comp, &organizer);
		if (organizer.value == NULL) {
			e_notice (NULL, GNOME_MESSAGE_BOX_ERROR,
				  _("An organizer must be set."));
			return NULL;
		}
		
		len = 1;

		to_list = GNOME_Evolution_Composer_RecipientList__alloc ();
		to_list->_maximum = len;
		to_list->_length = len;
		to_list->_buffer = CORBA_sequence_GNOME_Evolution_Composer_Recipient_allocbuf (len);
		recipient = &(to_list->_buffer[0]);

		if (organizer.cn != NULL)
			recipient->name = CORBA_string_dup (organizer.cn);
		else
			recipient->name = CORBA_string_dup ("");
		recipient->address = CORBA_string_dup (itip_strip_mailto (organizer.value));
		break;

	default:
		to_list = GNOME_Evolution_Composer_RecipientList__alloc ();
		to_list->_maximum = to_list->_length = 0;
		break;
	}
	CORBA_sequence_set_release (to_list, TRUE);

	return to_list;	
}
	
static CORBA_char *
comp_subject (CalComponent *comp) 
{
	CalComponentText caltext;
	cal_component_get_summary (comp, &caltext);
	if (caltext.value != NULL)	
		return CORBA_string_dup (caltext.value);

	switch (cal_component_get_vtype (comp)) {
	case CAL_COMPONENT_EVENT:
		return CORBA_string_dup ("Event information");
	case CAL_COMPONENT_TODO:
		return CORBA_string_dup ("Task information");
	case CAL_COMPONENT_JOURNAL:
		return CORBA_string_dup ("Journal information");
	case CAL_COMPONENT_FREEBUSY:
		return CORBA_string_dup ("Free/Busy information");
	default:
		return CORBA_string_dup ("Calendar information");
	}		
}

static CORBA_char *
comp_content_type (CalComponentItipMethod method)
{
	char tmp[256];	

	sprintf (tmp, "text/calendar; charset=utf-8; METHOD=%s", itip_methods[method]);
	return CORBA_string_dup (tmp);

}

static CORBA_char *
comp_filename (CalComponent *comp)
{
	switch (cal_component_get_vtype (comp)) {
	case CAL_COMPONENT_FREEBUSY:
		return CORBA_string_dup ("freebusy.ifb");
	default:
		return CORBA_string_dup ("calendar.ics");
	}	
}

static CORBA_char *
comp_description (CalComponent *comp)
{
	CORBA_char *description;	
	CalComponentDateTime dt;
	char *start = NULL, *end = NULL;

	switch (cal_component_get_vtype (comp)) {
	case CAL_COMPONENT_EVENT:
		return CORBA_string_dup ("Event information");
	case CAL_COMPONENT_TODO:
		return CORBA_string_dup ("Task information");
	case CAL_COMPONENT_JOURNAL:
		return CORBA_string_dup ("Journal information");
	case CAL_COMPONENT_FREEBUSY:
		cal_component_get_dtstart (comp, &dt);
		if (dt.value) {
			start = get_label (dt.value);
			cal_component_get_dtend (comp, &dt);
			if (dt.value)
				end = get_label (dt.value);
		}
		if (start != NULL && end != NULL) {
			char *tmp = g_strdup_printf ("Free/Busy information (%s to %s)", start, end);
			description = CORBA_string_dup (tmp);
			g_free (tmp);			
		} else {
			description = CORBA_string_dup ("Free/Busy information");
		}
		g_free (start);
		g_free (end);
		return description;		
	default:
		return CORBA_string_dup ("iCalendar information");
	}
}

static void
foreach_tzid_callback (icalparameter *param, gpointer data)
{
	ItipUtilTZData *tz_data = data;	
	const char *tzid;
	icaltimezone *zone;
	icalcomponent *vtimezone_comp;

	/* Get the TZID string from the parameter. */
	tzid = icalparameter_get_tzid (param);
	if (!tzid || g_hash_table_lookup (tz_data->tzids, tzid))
		return;

	/* Check if it is a builtin timezone. If it isn't, return. */
	zone = icaltimezone_get_builtin_timezone_from_tzid (tzid);
	if (!zone)
		return;

	/* Convert it to a string and add it to the hash. */
	vtimezone_comp = icaltimezone_get_component (zone);
	if (!vtimezone_comp)
		return;

	icalcomponent_add_component (tz_data->icomp, icalcomponent_new_clone (vtimezone_comp));
	g_hash_table_insert (tz_data->tzids, (char *)tzid, (char *)tzid);	
}

static char *
comp_string (CalComponentItipMethod method, CalComponent *comp)
{
	icalcomponent *top_level, *icomp;
	icalproperty *prop;
	icalvalue *value;
	gchar *ical_string;
	ItipUtilTZData tz_data;
		
	top_level = cal_util_new_top_level ();

	prop = icalproperty_new (ICAL_METHOD_PROPERTY);
	value = icalvalue_new_method (itip_methods_enum[method]);
	icalproperty_set_value (prop, value);
	icalcomponent_add_property (top_level, prop);

	icomp = cal_component_get_icalcomponent (comp);
		
	/* Add the timezones */
	tz_data.tzids = g_hash_table_new (g_str_hash, g_str_equal);
	tz_data.icomp = top_level;		
	icalcomponent_foreach_tzid (icomp, foreach_tzid_callback, &tz_data);
	g_hash_table_destroy (tz_data.tzids);

	icalcomponent_add_component (top_level, icomp);
	ical_string = icalcomponent_as_ical_string (top_level);
	icalcomponent_remove_component (top_level, icomp);
	
	icalcomponent_free (top_level);
	
	return ical_string;	
}

static gboolean
comp_limit_attendees (CalComponent *comp) 
{
	icalcomponent *icomp;
	GList *addresses;
	icalproperty *prop;
	gboolean found = FALSE, match = FALSE;
	
	icomp = cal_component_get_icalcomponent (comp);
	addresses = itip_addresses_get ();	

	for (prop = icalcomponent_get_first_property (icomp, ICAL_ATTENDEE_PROPERTY);
	     prop != NULL;
	     prop = icalcomponent_get_next_property (icomp, ICAL_ATTENDEE_PROPERTY))
	{
		icalvalue *value;
		const char *attendee, *text;
		GList *l;

		/* If we've already found something, just erase the rest */
		if (found) {
			icalcomponent_remove_property (icomp, prop);
			icalproperty_free (prop);
			continue;
		}
		
		value = icalproperty_get_value (prop);
		if (!value)
			continue;

		attendee = icalvalue_get_string (value);

		text = itip_strip_mailto (attendee);
		for (l = addresses; l != NULL; l = l->next) {
			ItipAddress *a = l->data;

			if (strstr (text, a->address))
				found = match = TRUE;
		}
		if (!match) {
			icalcomponent_remove_property (icomp, prop);
			icalproperty_free (prop);
		}
		match = FALSE;
	}
	itip_addresses_free (addresses);

	return found;
}

static void
comp_sentby (CalComponent *comp)
{
	CalComponentOrganizer organizer;
	GList *addresses, *l;
	const char *strip;
	gboolean is_user = FALSE;
	
	cal_component_get_organizer (comp, &organizer);
	if (!organizer.value) {
		ItipAddress *a = itip_addresses_get_default ();

		organizer.value = g_strdup_printf ("MAILTO:%s", a->address);
		organizer.sentby = NULL;
		organizer.cn = a->name;
		organizer.language = NULL;
		
		cal_component_set_organizer (comp, &organizer);
		g_free ((char *) organizer.value);
		itip_address_free (a);
		
		return;
	}
	
	strip = itip_strip_mailto (organizer.value);

	addresses = itip_addresses_get ();
	for (l = addresses; l != NULL; l = l->next) {
		ItipAddress *a = l->data;
		
		if (!strcmp (a->address, strip)) {
			is_user = TRUE;
			break;
		}
	}
	if (!is_user) {
		ItipAddress *a = itip_addresses_get_default ();
		
		organizer.value = g_strdup (organizer.value);
		organizer.sentby = g_strdup_printf ("MAILTO:%s", a->address);
		organizer.cn = g_strdup (organizer.cn);
		organizer.language = g_strdup (organizer.language);
		
		cal_component_set_organizer (comp, &organizer);

		g_free ((char *)organizer.value);
		g_free ((char *)organizer.sentby);
		g_free ((char *)organizer.cn);
		g_free ((char *)organizer.language);
		itip_address_free (a);
	}
	
	itip_addresses_free (addresses);
}
static CalComponent *
comp_minimal (CalComponent *comp, gboolean attendee)
{
	CalComponent *clone;
	icalcomponent *icomp;
	icalproperty *prop;
	CalComponentOrganizer organizer;
	const char *uid;
	GSList *comments;
	struct icaltimetype itt;
	CalComponentRange *recur_id;
	
	clone = cal_component_new ();
	cal_component_set_new_vtype (clone, cal_component_get_vtype (comp));

	if (attendee) {
		GSList *attendees;
		
		cal_component_get_attendee_list (comp, &attendees);
		cal_component_set_attendee_list (clone, attendees);

		if (!comp_limit_attendees (clone)) {
			e_notice (NULL, GNOME_MESSAGE_BOX_ERROR,
				  _("You must be an attendee of the event."));
			goto error;
		}
	}
	
	itt = icaltime_from_timet_with_zone (time (NULL), FALSE,
					     icaltimezone_get_utc_timezone ());
	cal_component_set_dtstamp (clone, &itt);

	cal_component_get_organizer (comp, &organizer);
	if (organizer.value == NULL)
		goto error;
	cal_component_set_organizer (clone, &organizer);

	cal_component_get_uid (comp, &uid);
	cal_component_set_uid (clone, uid);

	cal_component_get_comment_list (comp, &comments);
	if (g_slist_length (comments) <= 1) {
		cal_component_set_comment_list (clone, comments);
	} else {
		GSList *l = comments;
		
		comments = g_slist_remove_link (comments, l);
		cal_component_set_comment_list (clone, l);
		cal_component_free_text_list (l);
	}
	cal_component_free_text_list (comments);
	
	cal_component_get_recurid (comp, &recur_id);
	cal_component_set_recurid (clone, recur_id);
	
	icomp = cal_component_get_icalcomponent (comp);
	for (prop = icalcomponent_get_first_property (icomp, ICAL_X_PROPERTY);
	     prop != NULL;
	     prop = icalcomponent_get_next_property (icomp, ICAL_X_PROPERTY))
	{
		icalproperty *p;
		
		p = icalproperty_new_clone (prop);
		icalcomponent_add_property (icomp, p);
	}

	cal_component_rescan (clone);
	
	return clone;

 error:
	gtk_object_unref (GTK_OBJECT (clone));
	return NULL;
}

static CalComponent *
comp_compliant (CalComponentItipMethod method, CalComponent *comp)
{
	CalComponent *clone, *temp_clone;
	
	clone = cal_component_clone (comp);

	/* We delete incoming alarms anyhow, and this helps with outlook */
	cal_component_remove_all_alarms (clone);

	/* Comply with itip spec */
	switch (method) {
	case CAL_COMPONENT_METHOD_PUBLISH:
		comp_sentby (clone);
		cal_component_set_attendee_list (clone, NULL);
		break;
	case CAL_COMPONENT_METHOD_REQUEST:
		comp_sentby (clone);
		break;
	case CAL_COMPONENT_METHOD_CANCEL:
		comp_sentby (clone);
		break;	
	case CAL_COMPONENT_METHOD_REPLY:
		break;
	case CAL_COMPONENT_METHOD_ADD:
		break;
	case CAL_COMPONENT_METHOD_REFRESH:
		/* Need to remove almost everything */
		temp_clone = comp_minimal (clone, TRUE);
		gtk_object_unref (GTK_OBJECT (clone));
		clone = temp_clone;
		break;
	case CAL_COMPONENT_METHOD_COUNTER:
		break;
	case CAL_COMPONENT_METHOD_DECLINECOUNTER:
		/* Need to remove almost everything */
		temp_clone = comp_minimal (clone, FALSE);
		clone = temp_clone;
		break;
	default:
	}

	return clone;
}

void
itip_send_comp (CalComponentItipMethod method, CalComponent *send_comp)
{
	BonoboObjectClient *bonobo_server;
	GNOME_Evolution_Composer composer_server;
	CalComponent *comp = NULL;
	GNOME_Evolution_Composer_RecipientList *to_list = NULL;
	GNOME_Evolution_Composer_RecipientList *cc_list = NULL;
	GNOME_Evolution_Composer_RecipientList *bcc_list = NULL;
	CORBA_char *subject = NULL, *body = NULL, *content_type = NULL;
	CORBA_char *filename = NULL, *description = NULL;
	GNOME_Evolution_Composer_AttachmentData *attach_data = NULL;
	CORBA_boolean show_inline;
	char *ical_string;
	CORBA_Environment ev;
	
	CORBA_exception_init (&ev);

	/* Obtain an object reference for the Composer. */
	bonobo_server = bonobo_object_activate (GNOME_EVOLUTION_COMPOSER_OAFIID, 0);
	g_return_if_fail (bonobo_server != NULL);
	composer_server = BONOBO_OBJREF (bonobo_server);

	GNOME_Evolution_Composer_setMultipartType (composer_server, GNOME_Evolution_Composer_MIXED, &ev);
	if (BONOBO_EX (&ev)) {		
		g_warning ("Unable to set multipart type while sending iTip message");
		goto cleanup;
	}

	comp = comp_compliant (method, send_comp);
	if (comp == NULL)
		goto cleanup;
	
	to_list = comp_to_list (method, comp);
	if (to_list == NULL)
		goto cleanup;
	
	cc_list = GNOME_Evolution_Composer_RecipientList__alloc ();
	cc_list->_maximum = cc_list->_length = 0;
	bcc_list = GNOME_Evolution_Composer_RecipientList__alloc ();
	bcc_list->_maximum = bcc_list->_length = 0;
	
	/* Subject information */
	subject = comp_subject (comp);
	
	/* Set recipients, subject */
	GNOME_Evolution_Composer_setHeaders (composer_server, to_list, cc_list, bcc_list, subject, &ev);
	if (BONOBO_EX (&ev)) {		
		g_warning ("Unable to set composer headers while sending iTip message");
		goto cleanup;
	}

	/* Plain text body */
	body = comp_description (comp);
	GNOME_Evolution_Composer_setBodyText (composer_server, body, &ev);

	/* Content type, suggested file name, description */
	content_type = comp_content_type (method);
	filename = comp_filename (comp);	
	description = comp_description (comp);	
	show_inline = TRUE;

	ical_string = comp_string (method, comp);	
	attach_data = GNOME_Evolution_Composer_AttachmentData__alloc ();
	attach_data->_length = strlen (ical_string) + 1;
	attach_data->_maximum = attach_data->_length;	
	attach_data->_buffer = CORBA_sequence_CORBA_char_allocbuf (attach_data->_length);
	strcpy (attach_data->_buffer, ical_string);

	GNOME_Evolution_Composer_attachData (composer_server, 
					content_type, filename, description,
					show_inline, attach_data,
					&ev);
	
	if (BONOBO_EX (&ev)) {
		g_warning ("Unable to attach data to the composer while sending iTip message");
		goto cleanup;
	}
	
	if (method == CAL_COMPONENT_METHOD_PUBLISH) {
		GNOME_Evolution_Composer_show (composer_server, &ev);
		if (BONOBO_EX (&ev))
			g_warning ("Unable to show the composer while sending iTip message");
	} else {		
		GNOME_Evolution_Composer_send (composer_server, &ev);
		if (BONOBO_EX (&ev))
			g_warning ("Unable to send iTip message");
	}
	
 cleanup:
	CORBA_exception_free (&ev);

	if (comp != NULL)
		gtk_object_unref (GTK_OBJECT (comp));
		
	if (to_list != NULL)
		CORBA_free (to_list);
	if (cc_list != NULL)
		CORBA_free (cc_list);
	if (bcc_list != NULL)
		CORBA_free (bcc_list);

	if (subject != NULL)
		CORBA_free (subject);
	if (body != NULL)
		CORBA_free (body);
	if (content_type != NULL)
		CORBA_free (content_type);
	if (filename != NULL)
		CORBA_free (filename);
	if (description != NULL)
		CORBA_free (description);
	if (attach_data != NULL)
		CORBA_free (attach_data);
}

