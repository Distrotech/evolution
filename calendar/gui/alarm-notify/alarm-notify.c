/* Evolution calendar - Alarm notification service object
 *
 * Copyright (C) 2001 Ximian, Inc.
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
#include "config.h"
#endif

#include <cal-client/cal-client.h>
#include "alarm-notify.h"
#include "alarm-queue.h"
#include "save.h"
#include "e-util/e-url.h"



/* A loaded client */
typedef struct {
	/* The actual client */
	CalClient *client;

	/* The URI of the client in gnome-vfs's format.  This *is* the key that
	 * is stored in the uri_client_hash hash table below.
	 */
	EUri *uri;

	/* Number of times clients have requested this URI to be added to the
	 * alarm notification system.
	 */
	int refcount;
} LoadedClient;

/* Private part of the AlarmNotify structure */
struct _AlarmNotifyPrivate {
	/* Mapping from EUri's to LoadedClient structures */
	GHashTable *uri_client_hash;
};



static void alarm_notify_class_init (AlarmNotifyClass *class);
static void alarm_notify_init (AlarmNotify *an);
static void alarm_notify_destroy (GtkObject *object);

static void AlarmNotify_addCalendar (PortableServer_Servant servant,
				     const CORBA_char *str_uri,
				     CORBA_Environment *ev);
static void AlarmNotify_removeCalendar (PortableServer_Servant servant,
					const CORBA_char *str_uri,
					CORBA_Environment *ev);


static BonoboXObjectClass *parent_class;



BONOBO_X_TYPE_FUNC_FULL (AlarmNotify,
			 GNOME_Evolution_Calendar_AlarmNotify,
			 BONOBO_X_OBJECT_TYPE,
			 alarm_notify);

/* Class initialization function for the alarm notify service */
static void
alarm_notify_class_init (AlarmNotifyClass *class)
{
	GtkObjectClass *object_class;

	object_class = (GtkObjectClass *) class;

	parent_class = gtk_type_class (BONOBO_X_OBJECT_TYPE);

	class->epv.addCalendar = AlarmNotify_addCalendar;
	class->epv.removeCalendar = AlarmNotify_removeCalendar;

	object_class->destroy = alarm_notify_destroy;
}

/* Object initialization function for the alarm notify system */
static void
alarm_notify_init (AlarmNotify *an)
{
	AlarmNotifyPrivate *priv;

	priv = g_new0 (AlarmNotifyPrivate, 1);
	an->priv = priv;

	priv->uri_client_hash = g_hash_table_new (g_str_hash, g_str_equal);
}

/* Callback used from g_hash-table_forach(), used to destroy a loade client */
static void
destroy_loaded_client_cb (gpointer key, gpointer value, gpointer data)
{
	LoadedClient *lc;
	char *str_uri;

	str_uri = key;
	lc = value;
	
	g_free (str_uri);
	gtk_object_unref (GTK_OBJECT (lc->client));
	e_uri_free (lc->uri);
	g_free (lc);
}

/* Destroy handler for the alarm notify system */
static void
alarm_notify_destroy (GtkObject *object)
{
	AlarmNotify *an;
	AlarmNotifyPrivate *priv;

	g_return_if_fail (object != NULL);
	g_return_if_fail (IS_ALARM_NOTIFY (object));

	an = ALARM_NOTIFY (object);
	priv = an->priv;

	g_hash_table_foreach (priv->uri_client_hash, destroy_loaded_client_cb, NULL);

	g_hash_table_destroy (priv->uri_client_hash);
	priv->uri_client_hash = NULL;

	g_free (priv);
	an->priv = NULL;

	if (GTK_OBJECT_CLASS (parent_class)->destroy)
		(* GTK_OBJECT_CLASS (parent_class)->destroy) (object);
}



/* CORBA servant implementation */

/* Looks for a canonicalized URI inside an array of URIs; returns the index
 * within the array or -1 if not found.
 */
static int
find_uri_index (GPtrArray *uris, const char *str_uri)
{
	int i;

	for (i = 0; i < uris->len; i++) {
		char *uri;

		uri = uris->pdata[i];
		if (strcmp (uri, str_uri) == 0)
			break;
	}

	if (i == uris->len)
		return -1;
	else
		return i;
}

/* Frees an array of URIs and the URIs within it. */
static void
free_uris (GPtrArray *uris)
{
	int i;

	for (i = 0; i < uris->len; i++) {
		char *uri;

		uri = uris->pdata[i];
		g_free (uri);
	}

	g_ptr_array_free (uris, TRUE);
}

/* Adds an URI to the list of calendars to load on startup */
static void
add_uri_to_load (EUri *uri)
{
	char *str_uri;
	GPtrArray *loaded_uris;
	int i;

	/* Canonicalize the URI */
	str_uri = e_uri_to_string (uri, FALSE);
	g_assert (str_uri != NULL);

	loaded_uris = get_calendars_to_load ();
	if (!loaded_uris) {
		g_message ("add_uri_to_load(): Could not get the list of calendars to load; "
			   "will not add `%s'", str_uri);
		g_free (str_uri);
		return;
	}

	/* Look for the URI in the list of calendars to load */

	i = find_uri_index (loaded_uris, str_uri);

	/* We only need to add the URI if we didn't find it among the list of
	 * calendars.
	 */
	if (i != -1) {
		g_free (str_uri);
		free_uris (loaded_uris);
		return;
	}

	g_ptr_array_add (loaded_uris, str_uri);
	save_calendars_to_load (loaded_uris);

	free_uris (loaded_uris);
}

/* Removes an URI from the list of calendars to load on startup */
static void
remove_uri_to_load (EUri *uri)
{
	char *str_uri;
	GPtrArray *loaded_uris;
	char *loaded_uri;
	int i;

	/* Canonicalize the URI */
	str_uri = e_uri_to_string (uri, FALSE);
	g_assert (str_uri != NULL);

	loaded_uris = get_calendars_to_load ();
	if (!loaded_uris) {
		g_message ("remove_uri_to_load(): Could not get the list of calendars to load; "
			   "will not add `%s'", str_uri);
		g_free (str_uri);
		return;
	}

	/* Look for the URI in the list of calendars to load */

	i = find_uri_index (loaded_uris, str_uri);
	g_free (str_uri);

	/* If we didn't find it, there is no need to remove it */
	if (i == -1) {
		free_uris (loaded_uris);
		return;
	}

	loaded_uri = loaded_uris->pdata[i];
	g_free (loaded_uri);

	g_ptr_array_remove_index (loaded_uris, i);
	save_calendars_to_load (loaded_uris);

	free_uris (loaded_uris);
}

/* AlarmNotify::addCalendar method */
static void
AlarmNotify_addCalendar (PortableServer_Servant servant,
			 const CORBA_char *str_uri,
			 CORBA_Environment *ev)
{
	AlarmNotify *an;

	an = ALARM_NOTIFY (bonobo_object_from_servant (servant));
	alarm_notify_add_calendar (an, str_uri, TRUE, ev);
}

/* AlarmNotify::removeCalendar method */
static void
AlarmNotify_removeCalendar (PortableServer_Servant servant,
			    const CORBA_char *str_uri,
			    CORBA_Environment *ev)
{
	AlarmNotify *an;
	AlarmNotifyPrivate *priv;
	LoadedClient *lc;
	EUri *uri;
	char *orig_str;
	gboolean found;

	an = ALARM_NOTIFY (bonobo_object_from_servant (servant));
	priv = an->priv;

	uri = e_uri_new (str_uri);
	if (!uri) {
		CORBA_exception_set (ev, CORBA_USER_EXCEPTION,
				     ex_GNOME_Evolution_Calendar_AlarmNotify_InvalidURI,
				     NULL);
		return;
	}

	remove_uri_to_load (uri);

	found = g_hash_table_lookup_extended (priv->uri_client_hash, str_uri,
					      (gpointer *) &orig_str,
					      (gpointer *) &lc);
	e_uri_free (uri);

	if (!lc) {
		CORBA_exception_set (ev, CORBA_USER_EXCEPTION,
				     ex_GNOME_Evolution_Calendar_AlarmNotify_NotFound,
				     NULL);
		return;
	}

	g_assert (lc->refcount > 0);

	lc->refcount--;
	if (lc->refcount > 0)
		return;

	g_hash_table_remove (priv->uri_client_hash, str_uri);

	g_free (orig_str);
	gtk_object_unref (GTK_OBJECT (lc->client));
	e_uri_free (lc->uri);
	g_free (lc);
}



/**
 * alarm_notify_new:
 * 
 * Creates a new #AlarmNotify object.
 * 
 * Return value: A newly-created #AlarmNotify, or NULL if its corresponding
 * CORBA object could not be created.
 **/
AlarmNotify *
alarm_notify_new (void)
{
	AlarmNotify *an;

	an = gtk_type_new (TYPE_ALARM_NOTIFY);
	return an;
}

/**
 * alarm_notify_add_calendar:
 * @an: An alarm notification service.
 * @uri: URI of the calendar to load.
 * @load_afterwards: Whether this calendar should be loaded in the future
 * when the alarm daemon starts up.
 * @ev: CORBA environment for exceptions.
 * 
 * Tells the alarm notification service to load a calendar and start monitoring
 * its alarms.  It can optionally be made to save the URI of this calendar so
 * that it can be loaded in the future when the alarm daemon starts up.
 **/
void
alarm_notify_add_calendar (AlarmNotify *an, const char *str_uri, gboolean load_afterwards,
			   CORBA_Environment *ev)
{
	AlarmNotifyPrivate *priv;
	EUri *uri;
	CalClient *client;
	LoadedClient *lc;

	g_return_if_fail (an != NULL);
	g_return_if_fail (IS_ALARM_NOTIFY (an));
	g_return_if_fail (str_uri != NULL);
	g_return_if_fail (ev != NULL);

	priv = an->priv;

	uri = e_uri_new (str_uri);
	if (!uri) {
		CORBA_exception_set (ev, CORBA_USER_EXCEPTION,
				     ex_GNOME_Evolution_Calendar_AlarmNotify_InvalidURI,
				     NULL);
		return;
	}

	if (load_afterwards)
		add_uri_to_load (uri);

	lc = g_hash_table_lookup (priv->uri_client_hash, str_uri);

	if (lc) {
		e_uri_free (uri);
		g_assert (lc->refcount > 0);
		lc->refcount++;
		return;
	}

	client = cal_client_new ();

	if (client) {
		if (cal_client_open_calendar (client, str_uri, FALSE)) {
			lc = g_new (LoadedClient, 1);
			lc->client = client;
			lc->uri = uri;
			lc->refcount = 1;
			g_hash_table_insert (priv->uri_client_hash,
					     g_strdup (str_uri), lc);

			alarm_queue_add_client (client);
		} else {
			gtk_object_unref (GTK_OBJECT (client));
			client = NULL;
		}
	}

	if (!client) {
		e_uri_free (uri);

		CORBA_exception_set (ev, CORBA_USER_EXCEPTION,
				     ex_GNOME_Evolution_Calendar_AlarmNotify_BackendContactError,
				     NULL);
		return;
	}
}
