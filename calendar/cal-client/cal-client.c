
/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* Evolution calendar client
 *
 * Copyright (C) 2000 Helix Code, Inc.
 *
 * Author: Federico Mena-Quintero <federico@helixcode.com>
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

#include <config.h>
#include <gtk/gtksignal.h>
#include <liboaf/liboaf.h>

#include "cal-client-types.h"
#include "cal-client.h"
#include "cal-listener.h"



/* Loading state for the calendar client */
typedef enum {
	LOAD_STATE_NOT_LOADED,
	LOAD_STATE_LOADING,
	LOAD_STATE_LOADED
} LoadState;

/* Private part of the CalClient structure */
struct _CalClientPrivate {
	/* Load state to avoid multiple loads */
	LoadState load_state;

	/* The calendar factory we are contacting */
	GNOME_Evolution_Calendar_CalFactory factory;

	/* Our calendar listener */
	CalListener *listener;

	/* The calendar client interface object we are contacting */
	GNOME_Evolution_Calendar_Cal cal;
};



/* Signal IDs */
enum {
	CAL_LOADED,
	OBJ_UPDATED,
	OBJ_REMOVED,
	LAST_SIGNAL
};

static void cal_client_class_init (CalClientClass *class);
static void cal_client_init (CalClient *client);
static void cal_client_destroy (GtkObject *object);

static guint cal_client_signals[LAST_SIGNAL];

static GtkObjectClass *parent_class;



/**
 * cal_client_get_type:
 * @void:
 *
 * Registers the #CalClient class if necessary, and returns the type ID assigned
 * to it.
 *
 * Return value: The type ID of the #CalClient class.
 **/
GtkType
cal_client_get_type (void)
{
	static GtkType cal_client_type = 0;

	if (!cal_client_type) {
		static const GtkTypeInfo cal_client_info = {
			"CalClient",
			sizeof (CalClient),
			sizeof (CalClientClass),
			(GtkClassInitFunc) cal_client_class_init,
			(GtkObjectInitFunc) cal_client_init,
			NULL, /* reserved_1 */
			NULL, /* reserved_2 */
			(GtkClassInitFunc) NULL
		};

		cal_client_type = gtk_type_unique (GTK_TYPE_OBJECT, &cal_client_info);
	}

	return cal_client_type;
}

/* Class initialization function for the calendar client */
static void
cal_client_class_init (CalClientClass *class)
{
	GtkObjectClass *object_class;

	object_class = (GtkObjectClass *) class;

	parent_class = gtk_type_class (GTK_TYPE_OBJECT);

	cal_client_signals[CAL_LOADED] =
		gtk_signal_new ("cal_loaded",
				GTK_RUN_FIRST,
				object_class->type,
				GTK_SIGNAL_OFFSET (CalClientClass, cal_loaded),
				gtk_marshal_NONE__ENUM,
				GTK_TYPE_NONE, 1,
				GTK_TYPE_ENUM);
	cal_client_signals[OBJ_UPDATED] =
		gtk_signal_new ("obj_updated",
				GTK_RUN_FIRST,
				object_class->type,
				GTK_SIGNAL_OFFSET (CalClientClass, obj_updated),
				gtk_marshal_NONE__STRING,
				GTK_TYPE_NONE, 1,
				GTK_TYPE_STRING);
	cal_client_signals[OBJ_REMOVED] =
		gtk_signal_new ("obj_removed",
				GTK_RUN_FIRST,
				object_class->type,
				GTK_SIGNAL_OFFSET (CalClientClass, obj_removed),
				gtk_marshal_NONE__STRING,
				GTK_TYPE_NONE, 1,
				GTK_TYPE_STRING);

	gtk_object_class_add_signals (object_class, cal_client_signals, LAST_SIGNAL);

	object_class->destroy = cal_client_destroy;
}

/* Object initialization function for the calendar client */
static void
cal_client_init (CalClient *client)
{
	CalClientPrivate *priv;

	priv = g_new0 (CalClientPrivate, 1);
	client->priv = priv;

	priv->factory = CORBA_OBJECT_NIL;
	priv->load_state = LOAD_STATE_NOT_LOADED;
}

/* Gets rid of the factory that a client knows about */
static void
destroy_factory (CalClient *client)
{
	CalClientPrivate *priv;
	CORBA_Environment ev;
	int result;

	priv = client->priv;

	CORBA_exception_init (&ev);
	result = CORBA_Object_is_nil (priv->factory, &ev);
	if (ev._major != CORBA_NO_EXCEPTION) {
		g_message ("destroy_factory(): could not see if the factory was nil");
		priv->factory = CORBA_OBJECT_NIL;
		CORBA_exception_free (&ev);
		return;
	}
	CORBA_exception_free (&ev);

	if (result)
		return;

	CORBA_exception_init (&ev);
	CORBA_Object_release (priv->factory, &ev);
	if (ev._major != CORBA_NO_EXCEPTION)
		g_message ("destroy_factory(): could not release the factory");

	CORBA_exception_free (&ev);
	priv->factory = CORBA_OBJECT_NIL;
}

/* Gets rid of the listener that a client knows about */
static void
destroy_listener (CalClient *client)
{
	CalClientPrivate *priv;

	priv = client->priv;

	if (!priv->listener)
		return;

	bonobo_object_unref (BONOBO_OBJECT (priv->listener));
	priv->listener = NULL;
}

/* Gets rid of the calendar client interface object that a client knows about */
static void
destroy_cal (CalClient *client)
{
	CalClientPrivate *priv;
	CORBA_Environment ev;
	int result;

	priv = client->priv;

	CORBA_exception_init (&ev);
	result = CORBA_Object_is_nil (priv->cal, &ev);
	if (ev._major != CORBA_NO_EXCEPTION) {
		g_message ("destroy_cal(): could not see if the "
			   "calendar client interface object was nil");
		priv->cal = CORBA_OBJECT_NIL;
		CORBA_exception_free (&ev);
		return;
	}
	CORBA_exception_free (&ev);

	if (result)
		return;

	CORBA_exception_init (&ev);
	GNOME_Evolution_Calendar_Cal_unref (priv->cal, &ev);
	if (ev._major != CORBA_NO_EXCEPTION)
		g_message ("destroy_cal(): could not unref the calendar client interface object");

	CORBA_exception_free (&ev);

	CORBA_exception_init (&ev);
	CORBA_Object_release (priv->cal, &ev);
	if (ev._major != CORBA_NO_EXCEPTION)
		g_message ("destroy_cal(): could not release the calendar client interface object");

	CORBA_exception_free (&ev);
	priv->cal = CORBA_OBJECT_NIL;

}

/* Destroy handler for the calendar client */
static void
cal_client_destroy (GtkObject *object)
{
	CalClient *client;
	CalClientPrivate *priv;

	g_return_if_fail (object != NULL);
	g_return_if_fail (IS_CAL_CLIENT (object));

	client = CAL_CLIENT (object);
	priv = client->priv;

	destroy_factory (client);
	destroy_listener (client);
	destroy_cal (client);

	priv->load_state = LOAD_STATE_NOT_LOADED;

	g_free (priv);
	client->priv = NULL;

	if (GTK_OBJECT_CLASS (parent_class)->destroy)
		(* GTK_OBJECT_CLASS (parent_class)->destroy) (object);
}



/* Signal handlers for the listener's signals */

/* Handle the cal_loaded signal from the listener */
static void
cal_loaded_cb (CalListener *listener,
	       GNOME_Evolution_Calendar_Listener_LoadStatus status,
	       GNOME_Evolution_Calendar_Cal cal,
	       gpointer data)
{
	CalClient *client;
	CalClientPrivate *priv;
	CORBA_Environment ev;
	GNOME_Evolution_Calendar_Cal cal_copy;
	CalClientLoadStatus client_status;

	client = CAL_CLIENT (data);
	priv = client->priv;

	g_assert (priv->load_state == LOAD_STATE_LOADING);

	client_status = CAL_CLIENT_LOAD_ERROR;

	switch (status) {
	case GNOME_Evolution_Calendar_Listener_SUCCESS:
		CORBA_exception_init (&ev);
		cal_copy = CORBA_Object_duplicate (cal, &ev);
		if (ev._major != CORBA_NO_EXCEPTION) {
			g_message ("cal_loaded(): could not duplicate the calendar client interface");
			CORBA_exception_free (&ev);
			goto error;
		}
		CORBA_exception_free (&ev);

		priv->cal = cal_copy;
		priv->load_state = LOAD_STATE_LOADED;

		client_status = CAL_CLIENT_LOAD_SUCCESS;
		goto out;

	case GNOME_Evolution_Calendar_Listener_ERROR:
		client_status = CAL_CLIENT_LOAD_ERROR;
		goto error;

	case GNOME_Evolution_Calendar_Listener_IN_USE:
		client_status = CAL_CLIENT_LOAD_IN_USE;
		goto error;

	case GNOME_Evolution_Calendar_Listener_METHOD_NOT_SUPPORTED:
		client_status = CAL_CLIENT_LOAD_METHOD_NOT_SUPPORTED;
		goto error;

	default:
		g_assert_not_reached ();
	}

 error:

	bonobo_object_unref (BONOBO_OBJECT (priv->listener));
	priv->listener = NULL;
	priv->load_state = LOAD_STATE_NOT_LOADED;

 out:

	g_assert (priv->load_state != LOAD_STATE_LOADING);

	gtk_signal_emit (GTK_OBJECT (client), cal_client_signals[CAL_LOADED],
			 client_status);
}

/* Handle the obj_updated signal from the listener */
static void
obj_updated_cb (CalListener *listener, const GNOME_Evolution_Calendar_CalObjUID uid, gpointer data)
{
	CalClient *client;

	client = CAL_CLIENT (data);
	gtk_signal_emit (GTK_OBJECT (client), cal_client_signals[OBJ_UPDATED], uid);
}

/* Handle the obj_removed signal from the listener */
static void
obj_removed_cb (CalListener *listener, const GNOME_Evolution_Calendar_CalObjUID uid, gpointer data)
{
	CalClient *client;

	client = CAL_CLIENT (data);
	gtk_signal_emit (GTK_OBJECT (client), cal_client_signals[OBJ_REMOVED], uid);
}



/**
 * cal_client_construct:
 * @client: A calendar client.
 *
 * Constructs a calendar client object by contacting the calendar factory of the
 * calendar server.
 *
 * Return value: The same object as the @client argument, or NULL if the
 * calendar factory could not be contacted.
 **/
CalClient *
cal_client_construct (CalClient *client)
{
	CalClientPrivate *priv;
	GNOME_Evolution_Calendar_CalFactory factory, factory_copy;
	CORBA_Environment ev;
	int result;

	CORBA_exception_init (&ev);
	g_return_val_if_fail (client != NULL, NULL);
	g_return_val_if_fail (IS_CAL_CLIENT (client), NULL);

	priv = client->priv;

	factory = (GNOME_Evolution_Calendar_CalFactory) oaf_activate_from_id (
		"OAFIID:GNOME_Evolution_Wombat_CalendarFactory",
		OAF_FLAG_NO_LOCAL, NULL, &ev);

	result = CORBA_Object_is_nil (factory, &ev);
	if (ev._major != CORBA_NO_EXCEPTION) {
		g_message ("cal_client_construct(): could not see if the factory is NIL");
		CORBA_exception_free (&ev);
		return NULL;
	}
	CORBA_exception_free (&ev);

	if (result) {
		g_message ("cal_client_construct(): could not contact Wombat, "
			   "the personal calendar server");
		return NULL;
	}

	CORBA_exception_init (&ev);
	factory_copy = CORBA_Object_duplicate (factory, &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {
		g_message ("cal_client_construct(): could not duplicate the calendar factory");
		CORBA_exception_free (&ev);
		return NULL;
	}
	CORBA_exception_free (&ev);

	priv->factory = factory_copy;
	return client;
}

/**
 * cal_client_new:
 *
 * Creates a new calendar client.  It should be initialized by calling
 * cal_client_load_calendar() or cal_client_create_calendar().
 *
 * Return value: A newly-created calendar client, or NULL if the client could
 * not be constructed because it could not contact the calendar server.
 **/
CalClient *
cal_client_new (void)
{
	CalClient *client;

	client = gtk_type_new (CAL_CLIENT_TYPE);

	if (!cal_client_construct (client)) {
		g_message ("cal_client_new(): could not construct the calendar client");
		gtk_object_unref (GTK_OBJECT (client));
		return NULL;
	}

	return client;
}

/* Issues a load or create request */
static gboolean
load_or_create (CalClient *client, const char *str_uri, gboolean load)
{
	CalClientPrivate *priv;
	GNOME_Evolution_Calendar_Listener corba_listener;
	CORBA_Environment ev;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (IS_CAL_CLIENT (client), FALSE);

	priv = client->priv;
	g_return_val_if_fail (priv->load_state == LOAD_STATE_NOT_LOADED, FALSE);

	g_return_val_if_fail (str_uri != NULL, FALSE);

	priv->listener = cal_listener_new ();
	if (!priv->listener) {
		g_message ("load_or_create(): could not create the listener");
		return FALSE;
	}

	gtk_signal_connect (GTK_OBJECT (priv->listener), "cal_loaded",
			    GTK_SIGNAL_FUNC (cal_loaded_cb),
			    client);
	gtk_signal_connect (GTK_OBJECT (priv->listener), "obj_updated",
			    GTK_SIGNAL_FUNC (obj_updated_cb),
			    client);
	gtk_signal_connect (GTK_OBJECT (priv->listener), "obj_removed",
			    GTK_SIGNAL_FUNC (obj_removed_cb),
			    client);

	corba_listener = (GNOME_Evolution_Calendar_Listener) bonobo_object_corba_objref (
		BONOBO_OBJECT (priv->listener));

	CORBA_exception_init (&ev);

	priv->load_state = LOAD_STATE_LOADING;

	if (load)
		GNOME_Evolution_Calendar_CalFactory_load (priv->factory, str_uri, corba_listener, &ev);
	else
		GNOME_Evolution_Calendar_CalFactory_create (priv->factory, str_uri, corba_listener, &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {
		g_message ("load_or_create(): load/create request failed");
		bonobo_object_unref (BONOBO_OBJECT (priv->listener));
		priv->listener = NULL;
		priv->load_state = LOAD_STATE_NOT_LOADED;
		CORBA_exception_free (&ev);
		return FALSE;
	}
	CORBA_exception_free (&ev);

	return TRUE;
}

/**
 * cal_client_load_calendar:
 * @client: A calendar client.
 * @str_uri: URI of calendar to load.
 *
 * Makes a calendar client initiate a request to load a calendar.  The calendar
 * client will emit the "cal_loaded" signal when the response from the server is
 * received.
 *
 * Return value: TRUE on success, FALSE on failure to issue the load request.
 **/
gboolean
cal_client_load_calendar (CalClient *client, const char *str_uri)
{
	return load_or_create (client, str_uri, TRUE);
}

/**
 * cal_client_create_calendar:
 * @client: A calendar client.
 * @str_uri: URI that will contain the calendar data.
 *
 * Makes a calendar client initiate a request to create a new calendar.  The
 * calendar client will emit the "cal_loaded" signal when the response from the
 * server is received.
 *
 * Return value: TRUE on success, FALSE on failure to issue the create request.
 **/
gboolean
cal_client_create_calendar (CalClient *client, const char *str_uri)
{
	return load_or_create (client, str_uri, FALSE);
}

/**
 * cal_client_is_loaded:
 * @client: A calendar client.
 * 
 * Queries whether a calendar client has been loaded successfully.
 * 
 * Return value: TRUE if the client has been loaded, FALSE if it has not or if
 * the loading process is not finished yet.
 **/
gboolean
cal_client_is_loaded (CalClient *client)
{
	CalClientPrivate *priv;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (IS_CAL_CLIENT (client), FALSE);

	priv = client->priv;
	return (priv->load_state == LOAD_STATE_LOADED);
}

/* Converts our representation of a calendar component type into its CORBA representation */
static GNOME_Evolution_Calendar_CalObjType
corba_obj_type (CalObjType type)
{
	return (((type & CALOBJ_TYPE_EVENT) ? GNOME_Evolution_Calendar_TYPE_EVENT : 0)
		| ((type & CALOBJ_TYPE_TODO) ? GNOME_Evolution_Calendar_TYPE_TODO : 0)
		| ((type & CALOBJ_TYPE_JOURNAL) ? GNOME_Evolution_Calendar_TYPE_JOURNAL : 0));
}

/**
 * cal_client_get_n_objects:
 * @client: A calendar client.
 * @type: Type of objects that will be counted.
 * 
 * Counts the number of calendar components of the specified @type.  This can be
 * used to count how many events, to-dos, or journals there are, for example.
 * 
 * Return value: Number of components.
 **/
int
cal_client_get_n_objects (CalClient *client, CalObjType type)
{
	CalClientPrivate *priv;
	CORBA_Environment ev;
	int n;
	int t;

	g_return_val_if_fail (client != NULL, -1);
	g_return_val_if_fail (IS_CAL_CLIENT (client), -1);

	priv = client->priv;
	g_return_val_if_fail (priv->load_state == LOAD_STATE_LOADED, -1);

	t = corba_obj_type (type);

	CORBA_exception_init (&ev);
	n = GNOME_Evolution_Calendar_Cal_countObjects (priv->cal, t, &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {
		g_message ("cal_client_get_n_objects(): could not get the number of objects");
		CORBA_exception_free (&ev);
		return -1;
	}

	CORBA_exception_free (&ev);
	return n;
}

/**
 * cal_client_get_object:
 * @client: A calendar client.
 * @uid: Unique identifier for a calendar component.
 * @comp: Return value for the calendar component object.
 *
 * Queries a calendar for a calendar component object based on its unique
 * identifier.
 *
 * Return value: Result code based on the status of the operation.
 **/
CalClientGetStatus
cal_client_get_object (CalClient *client, const char *uid, CalComponent **comp)
{
	CalClientPrivate *priv;
	CORBA_Environment ev;
	GNOME_Evolution_Calendar_CalObj calobj_str;
	CalClientGetStatus retval;
	icalcomponent *icalcomp;

	g_return_val_if_fail (client != NULL, CAL_CLIENT_GET_NOT_FOUND);
	g_return_val_if_fail (IS_CAL_CLIENT (client), CAL_CLIENT_GET_NOT_FOUND);

	priv = client->priv;
	g_return_val_if_fail (priv->load_state == LOAD_STATE_LOADED, CAL_CLIENT_GET_NOT_FOUND);

	g_return_val_if_fail (uid != NULL, CAL_CLIENT_GET_NOT_FOUND);
	g_return_val_if_fail (comp != NULL, CAL_CLIENT_GET_NOT_FOUND);

	retval = CAL_CLIENT_GET_NOT_FOUND;
	*comp = NULL;

	CORBA_exception_init (&ev);
	calobj_str = GNOME_Evolution_Calendar_Cal_getObject (priv->cal, (char *) uid, &ev);

	if (ev._major == CORBA_USER_EXCEPTION
	    && strcmp (CORBA_exception_id (&ev), ex_GNOME_Evolution_Calendar_Cal_NotFound) == 0)
		goto out;
	else if (ev._major != CORBA_NO_EXCEPTION) {
		g_message ("cal_client_get_object(): could not get the object");
		goto out;
	}

	icalcomp = icalparser_parse_string (calobj_str);
	CORBA_free (calobj_str);

	if (!icalcomp) {
		retval = CAL_CLIENT_GET_SYNTAX_ERROR;
		goto out;
	}

	*comp = cal_component_new ();
	if (!cal_component_set_icalcomponent (*comp, icalcomp)) {
		icalcomponent_free (icalcomp);
		gtk_object_unref (GTK_OBJECT (*comp));
		*comp = NULL;

		retval = CAL_CLIENT_GET_SYNTAX_ERROR;
		goto out;
	}

	retval = CAL_CLIENT_GET_SUCCESS;

 out:

	CORBA_exception_free (&ev);
	return retval;
}

/* Builds an UID list out of a CORBA UID sequence */
static GList *
build_uid_list (GNOME_Evolution_Calendar_CalObjUIDSeq *seq)
{
	GList *uids;
	int i;

	uids = NULL;

	for (i = 0; i < seq->_length; i++)
		uids = g_list_prepend (uids, g_strdup (seq->_buffer[i]));

	return uids;
}

/**
 * cal_client_get_uids:
 * @client: A calendar client.
 * @type: Bitmask with types of objects to return.
 *
 * Queries a calendar for a list of unique identifiers corresponding to calendar
 * objects whose type matches one of the types specified in the @type flags.
 *
 * Return value: A list of strings that are the sought UIDs.
 **/
GList *
cal_client_get_uids (CalClient *client, CalObjType type)
{
	CalClientPrivate *priv;
	CORBA_Environment ev;
	GNOME_Evolution_Calendar_CalObjUIDSeq *seq;
	int t;
	GList *uids;

	g_return_val_if_fail (client != NULL, NULL);
	g_return_val_if_fail (IS_CAL_CLIENT (client), NULL);

	priv = client->priv;
	g_return_val_if_fail (priv->load_state == LOAD_STATE_LOADED, NULL);

	t = corba_obj_type (type);

	CORBA_exception_init (&ev);

	seq = GNOME_Evolution_Calendar_Cal_getUIds (priv->cal, t, &ev);
	if (ev._major != CORBA_NO_EXCEPTION) {
		g_message ("cal_client_get_uids(): could not get the list of UIDs");
		CORBA_exception_free (&ev);
		return NULL;
	}

	CORBA_exception_free (&ev);

	uids = build_uid_list (seq);
	CORBA_free (seq);

	return uids;
}

/* Builds a GList of CalClientChange structures from the CORBA sequence */
static GList *
build_change_list (GNOME_Evolution_Calendar_CalObjChangeSeq *seq)
{
	GList *list = NULL;
	icalcomponent *icalcomp;
	int i;

	/* Create the list in reverse order */
	for (i = 0; i < seq->_length; i++) {
		GNOME_Evolution_Calendar_CalObjChange *corba_coc;
		CalClientChange *ccc;

		corba_coc = &seq->_buffer[i];
		ccc = g_new (CalClientChange, 1);

		icalcomp = icalparser_parse_string (corba_coc->calobj);
		if (!icalcomp)
			continue;

		ccc->comp = cal_component_new ();
		if (!cal_component_set_icalcomponent (ccc->comp, icalcomp)) {
			icalcomponent_free (icalcomp);
			gtk_object_unref (GTK_OBJECT (ccc->comp));
			continue;
		}
		ccc->type = corba_coc->type;

		list = g_list_prepend (list, ccc);
	}

	list = g_list_reverse (list);

	return list;
}

GList *
cal_client_get_changes (CalClient *client, CalObjType type, const char *change_id)
{
	CalClientPrivate *priv;
	CORBA_Environment ev;
	GNOME_Evolution_Calendar_CalObjChangeSeq *seq;
	int t;
	GList *changes;

	g_return_val_if_fail (client != NULL, NULL);
	g_return_val_if_fail (IS_CAL_CLIENT (client), NULL);

	priv = client->priv;
	g_return_val_if_fail (priv->load_state == LOAD_STATE_LOADED, NULL);

	t = corba_obj_type (type);
	CORBA_exception_init (&ev);

	seq = GNOME_Evolution_Calendar_Cal_getChanges (priv->cal, t, change_id, &ev);
	if (ev._major != CORBA_NO_EXCEPTION) {
		g_message ("cal_client_get_changes(): could not get the list of changes");
		CORBA_exception_free (&ev);
		return NULL;
	}

	CORBA_exception_free (&ev);

	changes = build_change_list (seq);
	CORBA_free (seq);

	return changes;
}

/* FIXME: Not used? */
#if 0
/* Builds a GList of CalObjInstance structures from the CORBA sequence */
static GList *
build_object_instance_list (GNOME_Evolution_Calendar_CalObjInstanceSeq *seq)
{
	GList *list;
	int i;

	/* Create the list in reverse order */

	list = NULL;
	for (i = 0; i < seq->_length; i++) {
		GNOME_Evolution_Calendar_CalObjInstance *corba_icoi;
		CalObjInstance *icoi;

		corba_icoi = &seq->_buffer[i];
		icoi = g_new (CalObjInstance, 1);

		icoi->uid = g_strdup (corba_icoi->uid);
		icoi->start = corba_icoi->start;
		icoi->end = corba_icoi->end;

		list = g_list_prepend (list, icoi);
	}

	list = g_list_reverse (list);
	return list;
}
#endif

/**
 * cal_client_get_objects_in_range:
 * @client: A calendar client.
 * @type: Bitmask with types of objects to return.
 * @start: Start time for query.
 * @end: End time for query.
 *
 * Queries a calendar for the objects that occur or recur in the specified range
 * of time.
 *
 * Return value: A list of UID strings.  This should be freed using the
 * cal_obj_uid_list_free() function.
 **/
GList *
cal_client_get_objects_in_range (CalClient *client, CalObjType type, time_t start, time_t end)
{
	CalClientPrivate *priv;
	CORBA_Environment ev;
	GNOME_Evolution_Calendar_CalObjUIDSeq *seq;
	GList *uids;
	int t;

	g_return_val_if_fail (client != NULL, NULL);
	g_return_val_if_fail (IS_CAL_CLIENT (client), NULL);

	priv = client->priv;
	g_return_val_if_fail (priv->load_state == LOAD_STATE_LOADED, NULL);

	g_return_val_if_fail (start != -1 && end != -1, NULL);
	g_return_val_if_fail (start <= end, NULL);

	CORBA_exception_init (&ev);

	t = corba_obj_type (type);

	seq = GNOME_Evolution_Calendar_Cal_getObjectsInRange (priv->cal, t, start, end, &ev);
	if (ev._major != CORBA_NO_EXCEPTION) {
		g_message ("cal_client_get_objects_in_range(): could not get the objects");
		CORBA_exception_free (&ev);
		return NULL;
	}
	CORBA_exception_free (&ev);

	uids = build_uid_list (seq);
	CORBA_free (seq);

	return uids;
}

/* Callback used when an object is updated and we must update the copy we have */
static void
generate_instances_obj_updated_cb (CalClient *client, const char *uid, gpointer data)
{
	GHashTable *uid_comp_hash;
	CalComponent *comp;
	CalClientGetStatus status;
	const char *comp_uid;

	uid_comp_hash = data;

	comp = g_hash_table_lookup (uid_comp_hash, uid);
	if (!comp)
		/* OK, so we don't care about new objects that may indeed be in
		 * the requested time range.  We only care about the ones that
		 * were returned by the first query to
		 * cal_client_get_objects_in_range().
		 */
		return;

	g_hash_table_remove (uid_comp_hash, uid);
	gtk_object_unref (GTK_OBJECT (comp));

	status = cal_client_get_object (client, uid, &comp);

	switch (status) {
	case CAL_CLIENT_GET_SUCCESS:
		/* The hash key comes from the component's internal data */
		cal_component_get_uid (comp, &comp_uid);
		g_hash_table_insert (uid_comp_hash, (char *) comp_uid, comp);
		break;

	case CAL_CLIENT_GET_NOT_FOUND:
		/* No longer in the server, too bad */
		break;

	case CAL_CLIENT_GET_SYNTAX_ERROR:
		g_message ("obj_updated_cb(): Syntax error when getting "
			   "object `%s'; ignoring...", uid);
		break;
		
	}
}

/* Callback used when an object is removed and we must delete the copy we have */
static void
generate_instances_obj_removed_cb (CalClient *client, const char *uid, gpointer data)
{
	GHashTable *uid_comp_hash;
	CalComponent *comp;

	uid_comp_hash = data;

	comp = g_hash_table_lookup (uid_comp_hash, uid);
	if (!comp)
		return;

	g_hash_table_remove (uid_comp_hash, uid);
	gtk_object_unref (GTK_OBJECT (comp));
}

/* Adds a component to the list; called from g_hash_table_foreach() */
static void
add_component (gpointer key, gpointer value, gpointer data)
{
	CalComponent *comp;
	GList **list;

	comp = CAL_COMPONENT (value);
	list = data;

	*list = g_list_prepend (*list, comp);
}

/* Gets a list of components that recur within the specified range of time.  It
 * ensures that the resulting list of CalComponent objects contains only objects
 * that are actually in the server at the time the initial
 * cal_client_get_objects_in_range() query ends.
 */
static GList *
get_objects_atomically (CalClient *client, CalObjType type, time_t start, time_t end)
{
	GList *uids;
	GHashTable *uid_comp_hash;
	GList *objects;
	guint obj_updated_id;
	guint obj_removed_id;
	GList *l;

	uids = cal_client_get_objects_in_range (client, type, start, end);

	uid_comp_hash = g_hash_table_new (g_str_hash, g_str_equal);

	/* While we are getting the actual object data, keep track of changes */

	obj_updated_id = gtk_signal_connect (GTK_OBJECT (client), "obj_updated",
					     GTK_SIGNAL_FUNC (generate_instances_obj_updated_cb),
					     uid_comp_hash);

	obj_removed_id = gtk_signal_connect (GTK_OBJECT (client), "obj_removed",
					     GTK_SIGNAL_FUNC (generate_instances_obj_removed_cb),
					     uid_comp_hash);

	/* Get the objects */

	for (l = uids; l; l = l->next) {
		CalComponent *comp;
		CalClientGetStatus status;
		char *uid;
		const char *comp_uid;

		uid = l->data;

		status = cal_client_get_object (client, uid, &comp);

		switch (status) {
		case CAL_CLIENT_GET_SUCCESS:
			/* The hash key comes from the component's internal data
			 * instead of the duped UID from the list of UIDS.
			 */
			cal_component_get_uid (comp, &comp_uid);
			g_hash_table_insert (uid_comp_hash, (char *) comp_uid, comp);
			break;

		case CAL_CLIENT_GET_NOT_FOUND:
			/* Object disappeared from the server, so don't log it */
			break;

		case CAL_CLIENT_GET_SYNTAX_ERROR:
			g_message ("get_objects_atomically(): Syntax error when getting "
				   "object `%s'; ignoring...", uid);
			break;

		default:
			g_assert_not_reached ();
		}
	}

	cal_obj_uid_list_free (uids);

	/* Now our state is consistent with the server, so disconnect from the
	 * notification signals and generate the final list of components.
	 */

	gtk_signal_disconnect (GTK_OBJECT (client), obj_updated_id);
	gtk_signal_disconnect (GTK_OBJECT (client), obj_removed_id);

	objects = NULL;
	g_hash_table_foreach (uid_comp_hash, add_component, &objects);
	g_hash_table_destroy (uid_comp_hash);

	return objects;
}

struct comp_instance {
	CalComponent *comp;
	time_t start;
	time_t end;
};

/* Called from cal_recur_generate_instances(); adds an instance to the list */
static gboolean
add_instance (CalComponent *comp, time_t start, time_t end, gpointer data)
{
	GList **list;
	struct comp_instance *ci;

	list = data;

	ci = g_new (struct comp_instance, 1);

	ci->comp = comp;
	gtk_object_ref (GTK_OBJECT (ci->comp));
	
	ci->start = start;
	ci->end = end;

	*list = g_list_prepend (*list, ci);

	return TRUE;
}

/* Used from g_list_sort(); compares two struct comp_instance structures */
static gint
compare_comp_instance (gconstpointer a, gconstpointer b)
{
	const struct comp_instance *cia, *cib;
	time_t diff;

	cia = a;
	cib = b;

	diff = cia->start - cib->start;
	return (diff < 0) ? -1 : (diff > 0) ? 1 : 0;
}

/**
 * cal_client_generate_instances:
 * @client: A calendar client.
 * @type: Bitmask with types of objects to return.
 * @start: Start time for query.
 * @end: End time for query.
 * @cb: Callback for each generated instance.
 * @cb_data: Closure data for the callback.
 * 
 * Does a combination of cal_client_get_objects_in_range() and
 * cal_recur_generate_instances().  It fetches the list of objects in an atomic
 * way so that the generated instances are actually in the server at the time
 * the initial cal_client_get_objects_in_range() query ends.
 *
 * The callback function should do a gtk_object_ref() of the calendar component
 * it gets passed if it intends to keep it around.
 **/
void
cal_client_generate_instances (CalClient *client, CalObjType type,
			       time_t start, time_t end,
			       CalRecurInstanceFn cb, gpointer cb_data)
{
	CalClientPrivate *priv;
	GList *objects;
	GList *instances;
	GList *l;

	g_return_if_fail (client != NULL);
	g_return_if_fail (IS_CAL_CLIENT (client));

	priv = client->priv;
	g_return_if_fail (priv->load_state == LOAD_STATE_LOADED);

	g_return_if_fail (start != -1 && end != -1);
	g_return_if_fail (start <= end);
	g_return_if_fail (cb != NULL);

	/* Generate objects */

	objects = get_objects_atomically (client, type, start, end);
	instances = NULL;

	for (l = objects; l; l = l->next) {
		CalComponent *comp;

		comp = l->data;
		cal_recur_generate_instances (comp, start, end, add_instance, &instances);
		gtk_object_unref (GTK_OBJECT (comp));
	}

	g_list_free (objects);

	/* Generate instances and spew them out */

	instances = g_list_sort (instances, compare_comp_instance);

	for (l = instances; l; l = l->next) {
		struct comp_instance *ci;
		gboolean result;
		
		ci = l->data;
		
		result = (* cb) (ci->comp, ci->start, ci->end, cb_data);

		if (!result)
			break;
	}

	/* Clean up */

	for (l = instances; l; l = l->next) {
		struct comp_instance *ci;

		ci = l->data;
		gtk_object_unref (GTK_OBJECT (ci->comp));
		g_free (ci);
	}

	g_list_free (instances);
}


#if 0
/* Translates the CORBA representation of an AlarmType */
static enum AlarmType
uncorba_alarm_type (Evolution_Calendar_AlarmType corba_type)
{
	switch (corba_type) {
	case Evolution_Calendar_MAIL:
		return ALARM_MAIL;

	case Evolution_Calendar_PROGRAM:
		return ALARM_PROGRAM;

	case Evolution_Calendar_DISPLAY:
		return ALARM_DISPLAY;

	case Evolution_Calendar_AUDIO:
		return ALARM_AUDIO;

	default:
		g_assert_not_reached ();
		return ALARM_DISPLAY;
	}
}
#endif

/* Builds a GList of CalAlarmInstance structures from the CORBA sequence */
static GList *
build_alarm_instance_list (GNOME_Evolution_Calendar_CalAlarmInstanceSeq *seq)
{
	GList *list;
	int i;

	/* Create the list in reverse order */

	list = NULL;
	for (i = 0; i < seq->_length; i++) {
		GNOME_Evolution_Calendar_CalAlarmInstance *corba_ai;
		CalAlarmInstance *ai;

		corba_ai = &seq->_buffer[i];
		ai = g_new (CalAlarmInstance, 1);

		ai->uid = g_strdup (corba_ai->uid);
#if 0
		ai->type = uncorba_alarm_type (corba_ai->type);
#endif
		ai->trigger = corba_ai->trigger;
		ai->occur = corba_ai->occur;

		list = g_list_prepend (list, ai);
	}

	list = g_list_reverse (list);
	return list;
}

/**
 * cal_client_get_alarms_in_range:
 * @client: A calendar client.
 * @start: Start time for query.
 * @end: End time for query.
 *
 * Queries a calendar for the alarms that trigger in the specified range of
 * time.
 *
 * Return value: A list of #CalAlarmInstance structures.
 **/
GList *
cal_client_get_alarms_in_range (CalClient *client, time_t start, time_t end)
{
	CalClientPrivate *priv;
	CORBA_Environment ev;
	GNOME_Evolution_Calendar_CalAlarmInstanceSeq *seq;
	GList *alarms;

	g_return_val_if_fail (client != NULL, NULL);
	g_return_val_if_fail (IS_CAL_CLIENT (client), NULL);

	priv = client->priv;
	g_return_val_if_fail (priv->load_state == LOAD_STATE_LOADED, NULL);

	g_return_val_if_fail (start != -1 && end != -1, NULL);
	g_return_val_if_fail (start <= end, NULL);

	CORBA_exception_init (&ev);

	seq = GNOME_Evolution_Calendar_Cal_getAlarmsInRange (priv->cal, start, end, &ev);
	if (ev._major != CORBA_NO_EXCEPTION) {
		g_message ("cal_client_get_alarms_in_range(): could not get the alarm range");
		CORBA_exception_free (&ev);
		return NULL;
	}
	CORBA_exception_free (&ev);

	alarms = build_alarm_instance_list (seq);
	CORBA_free (seq);

	return alarms;
}

/**
 * cal_client_get_alarms_for_object:
 * @client: A calendar client.
 * @uid: Unique identifier for a calendar object.
 * @start: Start time for query.
 * @end: End time for query.
 * @alarms: Return value for the list of alarm instances.
 *
 * Queries a calendar for the alarms of a particular object that trigger in the
 * specified range of time.
 *
 * Return value: TRUE on success, FALSE if the object was not found.
 **/
gboolean
cal_client_get_alarms_for_object (CalClient *client, const char *uid,
				  time_t start, time_t end,
				  GList **alarms)
{
	CalClientPrivate *priv;
	CORBA_Environment ev;
	GNOME_Evolution_Calendar_CalAlarmInstanceSeq *seq;
	gboolean retval;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (IS_CAL_CLIENT (client), FALSE);

	priv = client->priv;
	g_return_val_if_fail (priv->load_state == LOAD_STATE_LOADED, FALSE);

	g_return_val_if_fail (uid != NULL, FALSE);
	g_return_val_if_fail (start != -1 && end != -1, FALSE);
	g_return_val_if_fail (start <= end, FALSE);
	g_return_val_if_fail (alarms != NULL, FALSE);

	*alarms = NULL;
	retval = FALSE;

	CORBA_exception_init (&ev);

	seq = GNOME_Evolution_Calendar_Cal_getAlarmsForObject (priv->cal, (char *) uid, start, end, &ev);
	if (ev._major == CORBA_USER_EXCEPTION
	    && strcmp (CORBA_exception_id (&ev), ex_GNOME_Evolution_Calendar_Cal_NotFound) == 0)
		goto out;
	else if (ev._major != CORBA_NO_EXCEPTION) {
		g_message ("cal_client_get_alarms_for_object(): could not get the alarm range");
		goto out;
	}

	retval = TRUE;
	*alarms = build_alarm_instance_list (seq);
	CORBA_free (seq);

 out:
	CORBA_exception_free (&ev);
	return retval;

}

/**
 * cal_client_update_object:
 * @client: A calendar client.
 * @comp: A calendar component object.
 *
 * Asks a calendar to update a component.  Any existing component with the
 * specified component's UID will be replaced.  The client program should not
 * assume that the object is actually in the server's storage until it has
 * received the "obj_updated" notification signal.
 *
 * Return value: TRUE on success, FALSE on specifying an invalid component.
 **/
gboolean
cal_client_update_object (CalClient *client, CalComponent *comp)
{
	CalClientPrivate *priv;
	CORBA_Environment ev;
	gboolean retval;
	char *obj_string;
	const char *uid;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (IS_CAL_CLIENT (client), FALSE);

	priv = client->priv;
	g_return_val_if_fail (priv->load_state == LOAD_STATE_LOADED, FALSE);

	g_return_val_if_fail (comp != NULL, FALSE);

	retval = FALSE;

	cal_component_commit_sequence (comp);
	obj_string = cal_component_get_as_string (comp);

	cal_component_get_uid (comp, &uid);

	CORBA_exception_init (&ev);
	GNOME_Evolution_Calendar_Cal_updateObject (priv->cal, (char *) uid, obj_string, &ev);
	g_free (obj_string);

	if (ev._major == CORBA_USER_EXCEPTION &&
	    strcmp (CORBA_exception_id (&ev), ex_GNOME_Evolution_Calendar_Cal_InvalidObject) == 0)
		goto out;
	else if (ev._major != CORBA_NO_EXCEPTION) {
		g_message ("cal_client_update_object(): could not update the object");
		goto out;
	}

	retval = TRUE;

 out:
	CORBA_exception_free (&ev);
	return retval;
}

gboolean
cal_client_remove_object (CalClient *client, const char *uid)
{
	CalClientPrivate *priv;
	CORBA_Environment ev;
	gboolean retval;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (IS_CAL_CLIENT (client), FALSE);

	priv = client->priv;
	g_return_val_if_fail (priv->load_state == LOAD_STATE_LOADED, FALSE);

	g_return_val_if_fail (uid != NULL, FALSE);

	retval = FALSE;

	CORBA_exception_init (&ev);
	GNOME_Evolution_Calendar_Cal_removeObject (priv->cal, (char *) uid, &ev);

	if (ev._major == CORBA_USER_EXCEPTION &&
	    strcmp (CORBA_exception_id (&ev), ex_GNOME_Evolution_Calendar_Cal_NotFound) == 0)
		goto out;
	else if (ev._major != CORBA_NO_EXCEPTION) {
		g_message ("cal_client_remove_object(): could not remove the object");
		goto out;
	}

	retval = TRUE;

 out:
	CORBA_exception_free (&ev);
	return retval;
}
