/* Evolution calendar - Alarm queueing engine
 *
 * Copyright (C) 2000 Ximian, Inc.
 * Copyright (C) 2000 Ximian, Inc.
 *
 * Authors: Federico Mena-Quintero <federico@ximian.com>
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

#include <glib.h>
#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-i18n.h>
#include <gtk/gtksignal.h>
#include <liboaf/liboaf.h>
#include <libgnome/gnome-exec.h>
#include <libgnome/gnome-sound.h>
#include <libgnomeui/gnome-dialog.h>
#include <libgnomeui/gnome-dialog-util.h>
#include <libgnomeui/gnome-uidefs.h>
#include <bonobo/bonobo-object.h>
#include <cal-util/timeutil.h>
#include "alarm.h"
#include "alarm-notify-dialog.h"
#include "alarm-queue.h"



/* Whether the queueing system has been initialized */
static gboolean alarm_queue_inited;

/* Clients we are monitoring for alarms */
static GHashTable *client_alarms_hash = NULL;

/* Structure that stores a client we are monitoring */
typedef struct {
	/* Monitored client */
	CalClient *client;

	/* Number of times this client has been registered */
	int refcount;

	/* Hash table of component UID -> CompQueuedAlarms.  If an element is
	 * present here, then it means its cqa->queued_alarms contains at least
	 * one queued alarm.  When all the alarms for a component have been
	 * dequeued, the CompQueuedAlarms structure is removed from the hash
	 * table.  Thus a CQA exists <=> it has queued alarms.
	 */
	GHashTable *uid_alarms_hash;
} ClientAlarms;

/* Pair of a CalComponentAlarms and the mapping from queued alarm IDs to the
 * actual alarm instance structures.
 */
typedef struct {
	/* The parent client alarms structure */
	ClientAlarms *parent_client;

	/* The actual component and its alarm instances */
	CalComponentAlarms *alarms;

	/* List of QueuedAlarm structures */
	GSList *queued_alarms;
} CompQueuedAlarms;

/* Pair of a queued alarm ID and the alarm trigger instance it refers to */
typedef struct {
	/* Alarm ID from alarm.h */
	gpointer alarm_id;

	/* Instance from our parent CompQueuedAlarms->alarms->alarms list */
	CalAlarmInstance *instance;

	/* Whether this is a snoozed queued alarm or a normal one */
	guint snooze : 1;
} QueuedAlarm;

/* Alarm ID for the midnight refresh function */
static gpointer midnight_refresh_id = NULL;

static void display_notification (time_t trigger, CompQueuedAlarms *cqa,
				  gpointer alarm_id, gboolean use_description);
static void audio_notification (time_t trigger, CompQueuedAlarms *cqa, gpointer alarm_id);
static void mail_notification (time_t trigger, CompQueuedAlarms *cqa, gpointer alarm_id);
static void procedure_notification (time_t trigger, CompQueuedAlarms *cqa, gpointer alarm_id);



/* Alarm queue engine */

static void load_alarms (ClientAlarms *ca);
static void midnight_refresh_cb (gpointer alarm_id, time_t trigger, gpointer data);

/* Queues an alarm trigger for midnight so that we can load the next day's worth
 * of alarms.
 */
static void
queue_midnight_refresh (void)
{
	time_t midnight;

	g_assert (midnight_refresh_id == NULL);

	midnight = time_day_end (time (NULL));

	midnight_refresh_id = alarm_add (midnight, midnight_refresh_cb, NULL, NULL);
	if (!midnight_refresh_id) {
		g_message ("queue_midnight_refresh(): Could not set up the midnight refresh alarm!");
		/* FIXME: what to do? */
	}
}

/* Loads a client's alarms; called from g_hash_table_foreach() */
static void
add_client_alarms_cb (gpointer key, gpointer value, gpointer data)
{
	ClientAlarms *ca;

	ca = value;
	load_alarms (ca);
}

/* Loads the alarms for the new day every midnight */
static void
midnight_refresh_cb (gpointer alarm_id, time_t trigger, gpointer data)
{
	/* Re-load the alarms for all clients */

	g_hash_table_foreach (client_alarms_hash, add_client_alarms_cb, NULL);

	/* Re-schedule the midnight update */

	midnight_refresh_id = NULL;
	queue_midnight_refresh ();
}

/* Looks up a client in the client alarms hash table */
static ClientAlarms *
lookup_client (CalClient *client)
{
	return g_hash_table_lookup (client_alarms_hash, client);
}

/* Looks up a queued alarm based on its alarm ID */
static QueuedAlarm *
lookup_queued_alarm (CompQueuedAlarms *cqa, gpointer alarm_id)
{
	GSList *l;
	QueuedAlarm *qa;

	qa = NULL;

	for (l = cqa->queued_alarms; l; l = l->next) {
		qa = l->data;
		if (qa->alarm_id == alarm_id)
			break;
	}

	g_assert (l != NULL);
	return qa;
}

/* Removes an alarm from the list of alarms of a component.  If the alarm was
 * the last one listed for the component, it removes the component itself.
 */
static void
remove_queued_alarm (CompQueuedAlarms *cqa, gpointer alarm_id)
{
	QueuedAlarm *qa;
	const char *uid;
	GSList *l;

	qa = NULL;

	for (l = cqa->queued_alarms; l; l = l->next) {
		qa = l->data;
		if (qa->alarm_id == alarm_id)
			break;
	}

	g_assert (l != NULL);

	cqa->queued_alarms = g_slist_remove_link (cqa->queued_alarms, l);
	g_slist_free_1 (l);

	g_free (qa);

	/* If this was the last queued alarm for this component, remove the
	 * component itself.
	 */

	if (cqa->queued_alarms != NULL)
		return;

	cal_component_get_uid (cqa->alarms->comp, &uid);
	g_hash_table_remove (cqa->parent_client->uid_alarms_hash, uid);
	cqa->parent_client = NULL;

	cal_component_alarms_free (cqa->alarms);
	cqa->alarms = NULL;

	g_free (cqa);
}

/* Callback used when an alarm triggers */
static void
alarm_trigger_cb (gpointer alarm_id, time_t trigger, gpointer data)
{
	CompQueuedAlarms *cqa;
	CalComponent *comp;
	QueuedAlarm *qa;
	CalComponentAlarm *alarm;
	CalAlarmAction action;

	cqa = data;
	comp = cqa->alarms->comp;

	qa = lookup_queued_alarm (cqa, alarm_id);

	/* Decide what to do based on the alarm action.  We use the trigger that
	 * is passed to us instead of the one from the instance structure
	 * because this may be a snoozed alarm instead of an original
	 * occurrence.
	 */

	alarm = cal_component_get_alarm (comp, qa->instance->auid);
	g_assert (alarm != NULL);

	cal_component_alarm_get_action (alarm, &action);
	cal_component_alarm_free (alarm);

	switch (action) {
	case CAL_ALARM_AUDIO:
		audio_notification (trigger, cqa, alarm_id);
		break;

	case CAL_ALARM_DISPLAY:
		display_notification (trigger, cqa, alarm_id, TRUE);
		break;

	case CAL_ALARM_EMAIL:
		mail_notification (trigger, cqa, alarm_id);
		break;

	case CAL_ALARM_PROCEDURE:
		procedure_notification (trigger, cqa, alarm_id);
		break;

	default:
		g_assert_not_reached ();
		break;
	}
}

/* Adds the alarms in a CalComponentAlarms structure to the alarms queued for a
 * particular client.  Also puts the triggers in the alarm timer queue.
 */
static void
add_component_alarms (ClientAlarms *ca, CalComponentAlarms *alarms)
{
	const char *uid;
	CompQueuedAlarms *cqa;
	GSList *l;

	/* No alarms? */
	if (alarms->alarms == NULL) {
		cal_component_alarms_free (alarms);
		return;
	}

	cqa = g_new (CompQueuedAlarms, 1);
	cqa->parent_client = ca;
	cqa->alarms = alarms;

	cqa->queued_alarms = NULL;

	for (l = alarms->alarms; l; l = l->next) {
		CalAlarmInstance *instance;
		gpointer alarm_id;
		QueuedAlarm *qa;

		instance = l->data;

		alarm_id = alarm_add (instance->trigger, alarm_trigger_cb, cqa, NULL);
		if (!alarm_id) {
			g_message ("add_component_alarms(): Could not schedule a trigger for "
				   "%ld, discarding...", (long) instance->trigger);
			continue;
		}

		qa = g_new (QueuedAlarm, 1);
		qa->alarm_id = alarm_id;
		qa->instance = instance;
		qa->snooze = FALSE;

		cqa->queued_alarms = g_slist_prepend (cqa->queued_alarms, qa);
	}

	cal_component_get_uid (alarms->comp, &uid);

	/* If we failed to add all the alarms, then we should get rid of the cqa */
	if (cqa->queued_alarms == NULL) {
		g_message ("add_component_alarms(): Could not add any of the alarms "
			   "for the component `%s'; discarding it...", uid);

		cal_component_alarms_free (cqa->alarms);
		cqa->alarms = NULL;

		g_free (cqa);
		return;
	}

	cqa->queued_alarms = g_slist_reverse (cqa->queued_alarms);
	g_hash_table_insert (ca->uid_alarms_hash, (char *) uid, cqa);
}

/* Loads today's remaining alarms for a client */
static void
load_alarms (ClientAlarms *ca)
{
	time_t now, day_end;
	GSList *comp_alarms;
	GSList *l;

	now = time (NULL);
	day_end = time_day_end (now);

	comp_alarms = cal_client_get_alarms_in_range (ca->client, now, day_end);

	/* All of the last day's alarms should have already triggered and should
	 * have been removed, so we should have no pending components.
	 */
	g_assert (g_hash_table_size (ca->uid_alarms_hash) == 0);

	for (l = comp_alarms; l; l = l->next) {
		CalComponentAlarms *alarms;

		alarms = l->data;
		add_component_alarms (ca, alarms);
	}

	g_slist_free (comp_alarms);
}

/* Called when a calendar client finished loading; we load its alarms */
static void
cal_opened_cb (CalClient *client, CalClientOpenStatus status, gpointer data)
{
	ClientAlarms *ca;

	ca = data;

	if (status != CAL_CLIENT_OPEN_SUCCESS)
		return;

	load_alarms (ca);
}

/* Looks up a component's queued alarm structure in a client alarms structure */
static CompQueuedAlarms *
lookup_comp_queued_alarms (ClientAlarms *ca, const char *uid)
{
	return g_hash_table_lookup (ca->uid_alarms_hash, uid);
}

/* Removes a component an its alarms */
static void
remove_comp (ClientAlarms *ca, const char *uid)
{
	CompQueuedAlarms *cqa;
	GSList *l;

	cqa = lookup_comp_queued_alarms (ca, uid);
	if (!cqa)
		return;

	/* If a component is present, then it means we must have alarms queued
	 * for it.
	 */
	g_assert (cqa->queued_alarms != NULL);

	for (l = cqa->queued_alarms; l;) {
		QueuedAlarm *qa;

		qa = l->data;

		/* Get the next element here because the list element will go
		 * away in remove_queued_alarm().  The qa will be freed there as
		 * well.
		 */
		l = l->next;

		alarm_remove (qa->alarm_id);
		remove_queued_alarm (cqa, qa->alarm_id);
	}

	/* The list should be empty now, and thus the queued component alarms
	 * structure should have been freed and removed from the hash table.
	 */
	g_assert (lookup_comp_queued_alarms (ca, uid) == NULL);
}

/* Called when a calendar component changes; we must reload its corresponding
 * alarms.
 */
static void
obj_updated_cb (CalClient *client, const char *uid, gpointer data)
{
	ClientAlarms *ca;
	time_t now, day_end;
	CalComponentAlarms *alarms;
	gboolean found;

	ca = data;

	remove_comp (ca, uid);

	now = time (NULL);
	day_end = time_day_end (now);

	found = cal_client_get_alarms_for_object (ca->client, uid, now, day_end, &alarms);

	if (!found)
		return;

	add_component_alarms (ca, alarms);
}

/* Called when a calendar component is removed; we must delete its corresponding
 * alarms.
 */
static void
obj_removed_cb (CalClient *client, const char *uid, gpointer data)
{
	ClientAlarms *ca;

	ca = data;

	remove_comp (ca, uid);
}



/* Notification functions */

/* Creates a snooze alarm based on an existing one.  The snooze offset is
 * compued with respect to the current time.
 */
static void
create_snooze (CompQueuedAlarms *cqa, gpointer alarm_id, int snooze_mins)
{
	QueuedAlarm *orig_qa, *qa;
	CalAlarmInstance *instance;
	time_t t;
	gpointer new_id;

	orig_qa = lookup_queued_alarm (cqa, alarm_id);

	t = time (NULL);
	t += snooze_mins * 60;

	new_id = alarm_add (t, alarm_trigger_cb, cqa, NULL);
	if (!new_id) {
		g_message ("create_snooze(): Could not schedule a trigger for "
			   "%ld, discarding...", (long) t);
		return;
	}

	instance = g_new (CalAlarmInstance, 1);
	instance->auid = orig_qa->instance->auid;
	instance->trigger = t;
	instance->occur_start = orig_qa->instance->occur_start;
	instance->occur_end = orig_qa->instance->occur_end;

	cqa->alarms->alarms = g_slist_prepend (cqa->alarms->alarms, instance);

	qa = g_new (QueuedAlarm, 1);
	qa->alarm_id = new_id;
	qa->instance = instance;
	qa->snooze = TRUE;

	cqa->queued_alarms = g_slist_prepend (cqa->queued_alarms, qa);
}

/* Launches a component editor for a component */
static void
edit_component (CompQueuedAlarms *cqa)
{
	CalComponent *comp;
	const char *uid;
	const char *uri;
	CORBA_Environment ev;
	GNOME_Evolution_Calendar_CompEditorFactory factory;

	comp = cqa->alarms->comp;
	cal_component_get_uid (comp, &uid);

	uri = cal_client_get_uri (cqa->parent_client->client);

	/* Get the factory */

	CORBA_exception_init (&ev);
	factory = oaf_activate_from_id ("OAFIID:GNOME_Evolution_Calendar_CompEditorFactory",
					0, NULL, &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {
		g_message ("edit_component(): Could not activate the component editor factory");
		CORBA_exception_free (&ev);
		return;
	}
	CORBA_exception_free (&ev);

	/* Edit the component */

	CORBA_exception_init (&ev);
	GNOME_Evolution_Calendar_CompEditorFactory_editExisting (factory, uri, (char *) uid, &ev);

	if (ev._major != CORBA_NO_EXCEPTION)
		g_message ("edit_component(): Exception while editing the component");

	CORBA_exception_free (&ev);

	/* Get rid of the factory */

	CORBA_exception_init (&ev);
	bonobo_object_release_unref (factory, &ev);
	if (ev._major != CORBA_NO_EXCEPTION)
		g_message ("edit_component(): Could not unref the calendar component factory");

	CORBA_exception_free (&ev);
}

struct notify_dialog_closure {
	CompQueuedAlarms *cqa;
	gpointer alarm_id;
};

/* Callback used from the alarm notify dialog */
static void
notify_dialog_cb (AlarmNotifyResult result, int snooze_mins, gpointer data)
{
	struct notify_dialog_closure *c;

	c = data;

	switch (result) {
	case ALARM_NOTIFY_SNOOZE:
		create_snooze (c->cqa, c->alarm_id, snooze_mins);
		break;

	case ALARM_NOTIFY_EDIT:
		edit_component (c->cqa);
		break;

	case ALARM_NOTIFY_CLOSE:
		/* Do nothing */
		break;

	default:
		g_assert_not_reached ();
	}

	remove_queued_alarm (c->cqa, c->alarm_id);
	g_free (c);
}

/* Performs notification of a display alarm */
static void
display_notification (time_t trigger, CompQueuedAlarms *cqa,
		      gpointer alarm_id, gboolean use_description)
{
	CalComponent *comp;
	CalComponentVType vtype;
	CalComponentText text;
	QueuedAlarm *qa;
	const char *message;
	struct notify_dialog_closure *c;
	gboolean use_summary;

	comp = cqa->alarms->comp;
	qa = lookup_queued_alarm (cqa, alarm_id);
	g_assert (qa != NULL);

	vtype = cal_component_get_vtype (comp);

	/* Pick a sensible notification message.  First we try the DESCRIPTION
	 * from the alarm, then the SUMMARY of the component.
	 */

	use_summary = TRUE;
	message = NULL;

	if (use_description) {
		CalComponentAlarm *alarm;

		alarm = cal_component_get_alarm (comp, qa->instance->auid);
		g_assert (alarm != NULL);

		cal_component_alarm_get_description (alarm, &text);
		cal_component_alarm_free (alarm);

		if (text.value) {
			message = text.value;
			use_summary = FALSE;
		}
	}

	if (use_summary) {
		cal_component_get_summary (comp, &text);
		if (text.value)
			message = text.value;
		else
			message = _("No description available.");
	}

	c = g_new (struct notify_dialog_closure, 1);
	c->cqa = cqa;
	c->alarm_id = alarm_id;

	if (!alarm_notify_dialog (trigger,
				  qa->instance->occur_start, qa->instance->occur_end,
				  vtype, message,
				  notify_dialog_cb, c))
		g_message ("display_notification(): Could not create the alarm notify dialog");
}

/* Performs notification of an audio alarm */
static void
audio_notification (time_t trigger, CompQueuedAlarms *cqa,
		    gpointer alarm_id)
{
	QueuedAlarm *qa;
	CalComponent *comp;
	CalComponentAlarm *alarm;
	icalattach *attach;

	comp = cqa->alarms->comp;
	qa = lookup_queued_alarm (cqa, alarm_id);
	g_assert (qa != NULL);

	alarm = cal_component_get_alarm (comp, qa->instance->auid);
	g_assert (alarm != NULL);

	cal_component_alarm_get_attach (alarm, &attach);
	cal_component_alarm_free (alarm);

	if (attach && icalattach_get_is_url (attach)) {
		const char *url;

		url = icalattach_get_url (attach);
		g_assert (url != NULL);

		gnome_sound_play (url); /* this sucks */
	}

	if (attach)
		icalattach_unref (attach);

	/* We present a notification message in addition to playing the sound */
	display_notification (trigger, cqa, alarm_id, FALSE);
}

/* Performs notification of a mail alarm */
static void
mail_notification (time_t trigger, CompQueuedAlarms *cqa, gpointer alarm_id)
{
	GtkWidget *dialog;

	/* FIXME */

	display_notification (trigger, cqa, alarm_id, FALSE);

	dialog = gnome_warning_dialog (_("Evolution does not support calendar reminders with\n"
					 "email notifications yet, but this reminder was\n"
					 "configured to send an email.  Evolution will display\n"
					 "a normal reminder dialog box instead."));
	gnome_dialog_run (GNOME_DIALOG (dialog));
}

/* Performs notification of a procedure alarm */
static void
procedure_notification (time_t trigger, CompQueuedAlarms *cqa, gpointer alarm_id)
{
	QueuedAlarm *qa;
	CalComponent *comp;
	CalComponentAlarm *alarm;
	CalComponentText description;
	icalattach *attach;
	const char *url;
	char *cmd, *str;
	GtkWidget *dialog;
	int result;

	comp = cqa->alarms->comp;
	qa = lookup_queued_alarm (cqa, alarm_id);
	g_assert (qa != NULL);

	alarm = cal_component_get_alarm (comp, qa->instance->auid);
	g_assert (alarm != NULL);

	cal_component_alarm_get_attach (alarm, &attach);
	cal_component_alarm_get_description (alarm, &description);
	cal_component_alarm_free (alarm);

	/* If the alarm has no attachment, simply display a notification dialog. */
	if (!attach)
		goto fallback;

	if (!icalattach_get_is_url (attach)) {
		icalattach_unref (attach);
		goto fallback;
	}

	url = icalattach_get_url (attach);
	g_assert (url != NULL);

	/* Ask for confirmation before executing the stuff */

	if (description.value)
		cmd = g_strconcat (url, " ", description.value, NULL);
	else
		cmd = (char *) url;

	str = g_strdup_printf (_("An Evolution Calendar reminder is about to trigger.\n"
				 "This reminder is configured to run the following program:\n\n"
				 "        %s\n\n"
				 "Are you sure you want to run this program?"),
			       cmd);

	dialog = gnome_question_dialog_modal (str, NULL, NULL);
	g_free (str);

	result = 0;
	if (gnome_dialog_run (GNOME_DIALOG (dialog)) == GNOME_YES)
		result = gnome_execute_shell (NULL, cmd);

	if (cmd != (char *) url)
		g_free (cmd);

	icalattach_unref (attach);

	/* Fall back to display notification if we got an error */
	if (result < 0)
		goto fallback;

	remove_queued_alarm (cqa, alarm_id);
	return;

 fallback:

	display_notification (trigger, cqa, alarm_id, FALSE);
}



/**
 * alarm_queue_init:
 *
 * Initializes the alarm queueing system.  This should be called near the
 * beginning of the program, after calling alarm_init().
 **/
void
alarm_queue_init (void)
{
	g_return_if_fail (alarm_queue_inited == FALSE);

	client_alarms_hash = g_hash_table_new (g_direct_hash, g_direct_equal);
	queue_midnight_refresh ();

	alarm_queue_inited = TRUE;
}

/**
 * alarm_queue_done:
 *
 * Shuts down the alarm queueing system.  This should be called near the end
 * of the program.  All the monitored calendar clients should already have been
 * unregistered with alarm_queue_remove_client().
 **/
void
alarm_queue_done (void)
{
	g_return_if_fail (alarm_queue_inited);

	/* All clients must be unregistered by now */
	g_return_if_fail (g_hash_table_size (client_alarms_hash) == 0);

	g_hash_table_destroy (client_alarms_hash);
	client_alarms_hash = NULL;

	g_assert (midnight_refresh_id != NULL);
	alarm_remove (midnight_refresh_id);
	midnight_refresh_id = NULL;

	alarm_queue_inited = FALSE;
}

/**
 * alarm_queue_add_client:
 * @client: A calendar client.
 *
 * Adds a calendar client to the alarm queueing system.  Alarm trigger
 * notifications will be presented at the appropriate times.  The client should
 * be removed with alarm_queue_remove_client() when receiving notifications
 * from it is no longer desired.
 *
 * A client can be added any number of times to the alarm queueing system,
 * but any single alarm trigger will only be presented once for a particular
 * client.  The client must still be removed the same number of times from the
 * queueing system when it is no longer wanted.
 **/
void
alarm_queue_add_client (CalClient *client)
{
	ClientAlarms *ca;

	g_return_if_fail (alarm_queue_inited);
	g_return_if_fail (client != NULL);
	g_return_if_fail (IS_CAL_CLIENT (client));

	ca = lookup_client (client);
	if (ca) {
		ca->refcount++;
		return;
	}

	ca = g_new (ClientAlarms, 1);

	ca->client = client;
	gtk_object_ref (GTK_OBJECT (ca->client));

	ca->refcount = 1;
	g_hash_table_insert (client_alarms_hash, client, ca);

	ca->uid_alarms_hash = g_hash_table_new (g_str_hash, g_str_equal);

	if (cal_client_get_load_state (client) != CAL_CLIENT_LOAD_LOADED)
		gtk_signal_connect (GTK_OBJECT (client), "cal_opened",
				    GTK_SIGNAL_FUNC (cal_opened_cb), ca);

	gtk_signal_connect (GTK_OBJECT (client), "obj_updated",
			    GTK_SIGNAL_FUNC (obj_updated_cb), ca);
	gtk_signal_connect (GTK_OBJECT (client), "obj_removed",
			    GTK_SIGNAL_FUNC (obj_removed_cb), ca);

	if (cal_client_get_load_state (client) == CAL_CLIENT_LOAD_LOADED)
		load_alarms (ca);
}

/* Called from g_hash_table_foreach(); adds a component UID to a list */
static void
add_uid_cb (gpointer key, gpointer value, gpointer data)
{
	GSList **uids;
	const char *uid;

	uids = data;
	uid = key;

	*uids = g_slist_prepend (*uids, (char *) uid);
}

/* Removes all the alarms queued for a particular calendar client */
static void
remove_client_alarms (ClientAlarms *ca)
{
	GSList *uids;
	GSList *l;

	/* First we build a list of UIDs so that we can remove them one by one */

	uids = NULL;
	g_hash_table_foreach (ca->uid_alarms_hash, add_uid_cb, &uids);

	for (l = uids; l; l = l->next) {
		const char *uid;

		uid = l->data;

		remove_comp (ca, uid);
	}

	g_slist_free (uids);

	/* The hash table should be empty now */

	g_assert (g_hash_table_size (ca->uid_alarms_hash) == 0);
}

/**
 * alarm_queue_remove_client:
 * @client: A calendar client.
 *
 * Removes a calendar client from the alarm queueing system.
 **/
void
alarm_queue_remove_client (CalClient *client)
{
	ClientAlarms *ca;

	g_return_if_fail (alarm_queue_inited);
	g_return_if_fail (client != NULL);
	g_return_if_fail (IS_CAL_CLIENT (client));

	ca = lookup_client (client);
	g_return_if_fail (ca != NULL);

	g_assert (ca->refcount > 0);
	ca->refcount--;

	if (ca->refcount > 0)
		return;

	remove_client_alarms (ca);

	/* Clean up */

	gtk_signal_disconnect_by_data (GTK_OBJECT (ca->client), ca);

	gtk_object_unref (GTK_OBJECT (ca->client));
	ca->client = NULL;

	g_hash_table_destroy (ca->uid_alarms_hash);
	ca->uid_alarms_hash = NULL;

	g_free (ca);

	g_hash_table_remove (client_alarms_hash, client);
}
