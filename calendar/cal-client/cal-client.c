/* Evolution calendar client
 *
 * Copyright (C) 2001 Ximian, Inc.
 *
 * Author: Federico Mena-Quintero <federico@ximian.com>
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

#include <gtk/gtksignal.h>
#include <liboaf/liboaf.h>
#include <bonobo/bonobo-exception.h>

#include "cal-client-types.h"
#include "cal-client.h"
#include "cal-listener.h"
#include "wombat-client.h"



/* Private part of the CalClient structure */
struct _CalClientPrivate {
	/* Load state to avoid multiple loads */
	CalClientLoadState load_state;

	/* URI of the calendar that is being loaded or is already loaded, or
	 * NULL if we are not loaded.
	 */
	char *uri;

	/* The calendar factories we are contacting */
	GList *factories;

	/* Our calendar listener implementation */
	CalListener *listener;

	/* The calendar client interface object we are contacting */
	GNOME_Evolution_Calendar_Cal cal;

	/* The authentication function */
	CalClientAuthFunc auth_func;
	gpointer auth_user_data;

	/* The WombatClient */
	WombatClient *w_client;

	/* A cache of timezones retrieved from the server, to avoid getting
	   them repeatedly for each get_object() call. */
	GHashTable *timezones;
};



/* Signal IDs */
enum {
	CAL_OPENED,
	CAL_SET_MODE,
	OBJ_UPDATED,
	OBJ_REMOVED,
	CATEGORIES_CHANGED,
	FORGET_PASSWORD,
	LAST_SIGNAL
};

static void cal_client_class_init (CalClientClass *class);
static void cal_client_init (CalClient *client);
static void cal_client_destroy (GtkObject *object);

static char *client_get_password_cb (WombatClient *w_client,
				     const gchar *prompt,
				     const gchar *key,
				     gpointer user_data);
static void  client_forget_password_cb (WombatClient *w_client,
					const gchar *key,
					gpointer user_data);
static void cal_client_get_object_timezones_cb (icalparameter *param,
						void *data);

static guint cal_client_signals[LAST_SIGNAL];

static GtkObjectClass *parent_class;



/**
 * cal_client_get_type:
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

#define marshal_NONE__ENUM_ENUM gtk_marshal_NONE__INT_INT

/* Class initialization function for the calendar client */
static void
cal_client_class_init (CalClientClass *class)
{
	GtkObjectClass *object_class;

	object_class = (GtkObjectClass *) class;

	parent_class = gtk_type_class (GTK_TYPE_OBJECT);

	cal_client_signals[CAL_OPENED] =
		gtk_signal_new ("cal_opened",
				GTK_RUN_FIRST,
				object_class->type,
				GTK_SIGNAL_OFFSET (CalClientClass, cal_opened),
				gtk_marshal_NONE__ENUM,
				GTK_TYPE_NONE, 1,
				GTK_TYPE_ENUM);
	cal_client_signals[CAL_SET_MODE] =
		gtk_signal_new ("cal_set_mode",
				GTK_RUN_FIRST,
				object_class->type,
				GTK_SIGNAL_OFFSET (CalClientClass, cal_set_mode),
				marshal_NONE__ENUM_ENUM,
				GTK_TYPE_NONE, 2,
				GTK_TYPE_ENUM,
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
	cal_client_signals[CATEGORIES_CHANGED] =
		gtk_signal_new ("categories_changed",
				GTK_RUN_FIRST,
				object_class->type,
				GTK_SIGNAL_OFFSET (CalClientClass, categories_changed),
				gtk_marshal_NONE__POINTER,
				GTK_TYPE_NONE, 1,
				GTK_TYPE_POINTER);
	cal_client_signals[FORGET_PASSWORD] =
		gtk_signal_new ("forget_password",
				GTK_RUN_FIRST,
                                object_class->type,
                                GTK_SIGNAL_OFFSET (CalClientClass, forget_password),
                                gtk_marshal_NONE__STRING,
                                GTK_TYPE_NONE, 1,
                                GTK_TYPE_STRING);

	gtk_object_class_add_signals (object_class, cal_client_signals, LAST_SIGNAL);

	class->cal_opened = NULL;
	class->obj_updated = NULL;
	class->obj_removed = NULL;
	class->categories_changed = NULL;
	class->forget_password = NULL;

	object_class->destroy = cal_client_destroy;
}

/* Object initialization function for the calendar client */
static void
cal_client_init (CalClient *client)
{
	CalClientPrivate *priv;

	priv = g_new0 (CalClientPrivate, 1);
	client->priv = priv;

	priv->load_state = CAL_CLIENT_LOAD_NOT_LOADED;
	priv->uri = NULL;
	priv->factories = NULL;
	priv->timezones = g_hash_table_new (g_str_hash, g_str_equal);

	/* create the WombatClient */
	priv->w_client = wombat_client_new (
		(WombatClientGetPasswordFn) client_get_password_cb,
                (WombatClientForgetPasswordFn) client_forget_password_cb,
                (gpointer) client);
}

/* Gets rid of the WombatClient that a client knows about */
static void
destroy_wombat_client (CalClient *client)
{
        CalClientPrivate *priv;

	g_return_if_fail (client != NULL);
	g_return_if_fail (IS_CAL_CLIENT (client));

        priv = client->priv;

        if (!priv->w_client)
                return;

        bonobo_object_unref (BONOBO_OBJECT (priv->w_client));
        priv->w_client = NULL;
}

/* Gets rid of the factories that a client knows about */
static void
destroy_factories (CalClient *client)
{
	CalClientPrivate *priv;
	CORBA_Object factory;
	CORBA_Environment ev;
	int result;
	GList *f;

	priv = client->priv;

	CORBA_exception_init (&ev);

	for (f = priv->factories; f; f = f->next) {
		factory = f->data;

		result = CORBA_Object_is_nil (factory, &ev);
		if (BONOBO_EX (&ev)) {
			g_message ("destroy_factories(): could not see if a factory was nil");
			CORBA_exception_free (&ev);

			continue;
		}

		if (result)
			continue;

		CORBA_Object_release (factory, &ev);
		if (BONOBO_EX (&ev)) {
			g_message ("destroy_factories(): could not release a factory");
			CORBA_exception_free (&ev);
		}
	}

	g_list_free (priv->factories);
	priv->factories = NULL;
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
	if (BONOBO_EX (&ev)) {
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
	if (BONOBO_EX (&ev))
		g_message ("destroy_cal(): could not unref the calendar client interface object");

	CORBA_exception_free (&ev);

	CORBA_exception_init (&ev);
	CORBA_Object_release (priv->cal, &ev);
	if (BONOBO_EX (&ev))
		g_message ("destroy_cal(): could not release the calendar client interface object");

	CORBA_exception_free (&ev);
	priv->cal = CORBA_OBJECT_NIL;

}

static void
free_timezone (gpointer key, gpointer value, gpointer data)
{
	/* Note that the key comes from within the icaltimezone value, so we
	   don't free that. */
	icaltimezone_free (value, TRUE);
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

	/* The server unrefs the query listener, so we just NULL it out here */
	if (priv->listener) {
		cal_listener_stop_notification (priv->listener);
		priv->listener = NULL;
	}

	destroy_wombat_client (client);
	destroy_factories (client);
	destroy_cal (client);

	priv->load_state = CAL_CLIENT_LOAD_NOT_LOADED;

	if (priv->uri) {
		g_free (priv->uri);
		priv->uri = NULL;
	}

	g_hash_table_foreach (priv->timezones, free_timezone, NULL);
	g_hash_table_destroy (priv->timezones);
	priv->timezones = NULL;

	g_free (priv);
	client->priv = NULL;

	if (GTK_OBJECT_CLASS (parent_class)->destroy)
		(* GTK_OBJECT_CLASS (parent_class)->destroy) (object);
}



/* Signal handlers for the listener's signals */
/* Handle the cal_opened notification from the listener */
static void
cal_opened_cb (CalListener *listener,
	       GNOME_Evolution_Calendar_Listener_OpenStatus status,
	       GNOME_Evolution_Calendar_Cal cal,
	       gpointer data)
{
	CalClient *client;
	CalClientPrivate *priv;
	CORBA_Environment ev;
	GNOME_Evolution_Calendar_Cal cal_copy;
	CalClientOpenStatus client_status;

	client = CAL_CLIENT (data);
	priv = client->priv;

	g_assert (priv->load_state == CAL_CLIENT_LOAD_LOADING);
	g_assert (priv->uri != NULL);

	client_status = CAL_CLIENT_OPEN_ERROR;

	switch (status) {
	case GNOME_Evolution_Calendar_Listener_SUCCESS:
		CORBA_exception_init (&ev);
		cal_copy = CORBA_Object_duplicate (cal, &ev);
		if (BONOBO_EX (&ev)) {
			g_message ("cal_opened_cb(): could not duplicate the "
				   "calendar client interface");
			CORBA_exception_free (&ev);
			goto error;
		}
		CORBA_exception_free (&ev);

		priv->cal = cal_copy;
		priv->load_state = CAL_CLIENT_LOAD_LOADED;

		client_status = CAL_CLIENT_OPEN_SUCCESS;
		goto out;

	case GNOME_Evolution_Calendar_Listener_ERROR:
		client_status = CAL_CLIENT_OPEN_ERROR;
		goto error;

	case GNOME_Evolution_Calendar_Listener_NOT_FOUND:
		client_status = CAL_CLIENT_OPEN_NOT_FOUND;
		goto error;

	case GNOME_Evolution_Calendar_Listener_METHOD_NOT_SUPPORTED:
		client_status = CAL_CLIENT_OPEN_METHOD_NOT_SUPPORTED;
		goto error;

	default:
		g_assert_not_reached ();
	}

 error:

	bonobo_object_unref (BONOBO_OBJECT (priv->listener));
	priv->listener = NULL;

	/* We free the priv->uri and set the priv->load_state until after the
	 * "cal_opened" signal has been emitted so that handlers will be able to
	 * access this information.
	 */

 out:

	/* We are *not* inside a signal handler (this is just a simple callback
	 * called from the listener), so there is not a temporary reference to
	 * the client object.  We ref() so that we can safely emit our own
	 * signal and clean up.
	 */

	gtk_object_ref (GTK_OBJECT (client));

	gtk_signal_emit (GTK_OBJECT (client), cal_client_signals[CAL_OPENED],
			 client_status);

	if (client_status != CAL_CLIENT_OPEN_SUCCESS) {
		priv->load_state = CAL_CLIENT_LOAD_NOT_LOADED;
		g_free (priv->uri);
		priv->uri = NULL;
	}

	g_assert (priv->load_state != CAL_CLIENT_LOAD_LOADING);

	gtk_object_unref (GTK_OBJECT (client));
}

/* Handle the cal_set_mode notification from the listener */
static void
cal_set_mode_cb (CalListener *listener,
		 GNOME_Evolution_Calendar_Listener_SetModeStatus status,
		 GNOME_Evolution_Calendar_CalMode mode,
		 gpointer data)
{
	CalClient *client;
	CalClientPrivate *priv;
	CalClientSetModeStatus client_status;

	client = CAL_CLIENT (data);
	priv = client->priv;

	g_assert (priv->load_state == CAL_CLIENT_LOAD_LOADING);
	g_assert (priv->uri != NULL);

	client_status = CAL_CLIENT_OPEN_ERROR;

	switch (status) {
	case GNOME_Evolution_Calendar_Listener_MODE_SET:
		client_status = CAL_CLIENT_SET_MODE_SUCCESS;
		break;		
	case GNOME_Evolution_Calendar_Listener_MODE_NOT_SET:
		client_status = CAL_CLIENT_SET_MODE_ERROR;
		break;
	case GNOME_Evolution_Calendar_Listener_MODE_NOT_SUPPORTED:
		client_status = CAL_CLIENT_SET_MODE_NOT_SUPPORTED;
		break;		
	default:
		g_assert_not_reached ();
	}

	/* We are *not* inside a signal handler (this is just a simple callback
	 * called from the listener), so there is not a temporary reference to
	 * the client object.  We ref() so that we can safely emit our own
	 * signal and clean up.
	 */

	gtk_object_ref (GTK_OBJECT (client));

	gtk_signal_emit (GTK_OBJECT (client), cal_client_signals[CAL_SET_MODE],
			 client_status, mode);

	gtk_object_unref (GTK_OBJECT (client));
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

/* Handle the categories_changed signal from the listener */
static void
categories_changed_cb (CalListener *listener, const GNOME_Evolution_Calendar_StringSeq *categories,
		       gpointer data)
{
	CalClient *client;
	GPtrArray *cats;
	int i;

	client = CAL_CLIENT (data);

	cats = g_ptr_array_new ();
	g_ptr_array_set_size (cats, categories->_length);

	for (i = 0; i < categories->_length; i++)
		cats->pdata[i] = categories->_buffer[i];

	gtk_signal_emit (GTK_OBJECT (client), cal_client_signals[CATEGORIES_CHANGED], cats);

	g_ptr_array_free (cats, TRUE);
}


/* Handle the get_password signal from the Wombatclient */
static gchar *
client_get_password_cb (WombatClient *w_client,
                        const gchar *prompt,
                        const gchar *key,
                        gpointer user_data)
{
        CalClient *client;

        client = CAL_CLIENT (user_data);
        g_return_val_if_fail (IS_CAL_CLIENT (client), NULL);

        if (client->priv->auth_func)
                return client->priv->auth_func (client, prompt, key, client->priv->auth_user_data);

        return NULL;
}

/* Handle the forget_password signal from the WombatClient */
static void
client_forget_password_cb (WombatClient *w_client,
                           const gchar *key,
                           gpointer user_data)
{
        CalClient *client;

        client = CAL_CLIENT (user_data);
        g_return_if_fail (IS_CAL_CLIENT (client));

        gtk_signal_emit (GTK_OBJECT (client),
                         cal_client_signals [FORGET_PASSWORD],
                         key);
}



/**
 * cal_client_construct:
 * @client: A calendar client.
 *
 * Constructs a calendar client object by contacting all available
 * calendar factories.
 *
 * Return value: The same object as the @client argument, or NULL if the
 * calendar factory could not be contacted.
 **/
CalClient *
cal_client_construct (CalClient *client)
{
	CalClientPrivate *priv;
	GNOME_Evolution_Calendar_CalFactory factory;
	OAF_ServerInfoList *servers;
	CORBA_Environment ev;
	int i;

	CORBA_exception_init (&ev);
	g_return_val_if_fail (client != NULL, NULL);
	g_return_val_if_fail (IS_CAL_CLIENT (client), NULL);

	priv = client->priv;

	CORBA_exception_init (&ev);

	servers = oaf_query ("repo_ids.has ('IDL:GNOME/Evolution/Calendar/CalFactory:1.0')", NULL, &ev);
	if (ev._major != CORBA_NO_EXCEPTION) {
		g_message ("Cannot perform OAF query for Calendar servers.");
		CORBA_exception_free (&ev);
		return NULL;
	}

	if (servers->_length == 0)
		g_warning ("No Calendar servers installed.");

	for (i = 0; i < servers->_length; i++) {
		const OAF_ServerInfo *info;

		info = servers->_buffer + i;

		factory = (GNOME_Evolution_Calendar_CalFactory)
			oaf_activate_from_id (info->iid, 0, NULL, &ev);
		if (BONOBO_EX (&ev)) {
			g_warning ("cal_client_construct: Could not activate calendar server %s", info->iid);
			CORBA_free (servers);
			CORBA_exception_free (&ev);
			return NULL;
		}

		priv->factories = g_list_prepend (priv->factories, factory);
	}

	CORBA_free (servers);

	CORBA_exception_free (&ev);
	return client;
}

/**
 * cal_client_new:
 *
 * Creates a new calendar client.  It should be initialized by calling
 * cal_client_open_calendar().
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

/**
 * cal_client_set_auth_func
 * @client: A calendar client.
 * @func: The authentication function
 * @data: User data to be used when calling the authentication function
 *
 * Associates the given authentication function with a calendar client. This
 * function will be called any time the calendar server needs a password
 * from the client. So, calendar clients should provide such authentication
 * function, which, when called, should act accordingly (by showing a dialog
 * box, for example, to ask the user for the password).
 *
 * The authentication function must have the following form:
 *	char * auth_func (CalClient *client,
 *			  const gchar *prompt,
 *			  const gchar *key,
 *			  gpointer user_data)
 */
void
cal_client_set_auth_func (CalClient *client, CalClientAuthFunc func, gpointer data)
{
	g_return_if_fail (client != NULL);
	g_return_if_fail (IS_CAL_CLIENT (client));

	client->priv->auth_func = func;
	client->priv->auth_user_data = data;
}

/**
 * cal_client_open_calendar:
 * @client: A calendar client.
 * @str_uri: URI of calendar to open.
 * @only_if_exists: FALSE if the calendar should be opened even if there
 * was no storage for it, i.e. to create a new calendar or load an existing
 * one if it already exists.  TRUE if it should only try to load calendars
 * that already exist.
 *
 * Makes a calendar client initiate a request to open a calendar.  The calendar
 * client will emit the "cal_opened" signal when the response from the server is
 * received.
 *
 * Return value: TRUE on success, FALSE on failure to issue the open request.
 **/
gboolean
cal_client_open_calendar (CalClient *client, const char *str_uri, gboolean only_if_exists)
{
	CalClientPrivate *priv;
	GNOME_Evolution_Calendar_Listener corba_listener;
	CORBA_Environment ev;
	GList *f;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (IS_CAL_CLIENT (client), FALSE);

	priv = client->priv;
	g_return_val_if_fail (priv->load_state == CAL_CLIENT_LOAD_NOT_LOADED, FALSE);
	g_assert (priv->uri == NULL);

	g_return_val_if_fail (str_uri != NULL, FALSE);

	priv->listener = cal_listener_new (cal_opened_cb,
					   cal_set_mode_cb,
					   obj_updated_cb,
					   obj_removed_cb,
					   categories_changed_cb,
					   client);
	if (!priv->listener) {
		g_message ("cal_client_open_calendar(): could not create the listener");
		return FALSE;
	}

	bonobo_object_add_interface (BONOBO_OBJECT (priv->listener),
				     BONOBO_OBJECT (priv->w_client));

	corba_listener = (GNOME_Evolution_Calendar_Listener) (BONOBO_OBJREF (priv->listener));

	priv->load_state = CAL_CLIENT_LOAD_LOADING;
	priv->uri = g_strdup (str_uri);

	for (f = priv->factories; f; f = f->next) {
		CORBA_exception_init (&ev);
		GNOME_Evolution_Calendar_CalFactory_open (f->data, str_uri,
							  only_if_exists,
							  corba_listener, &ev);
		if (!BONOBO_EX (&ev))
			break;

		CORBA_exception_free (&ev);
	}

	if (BONOBO_EX (&ev)) {
		CORBA_exception_free (&ev);

		if (BONOBO_USER_EX (&ev, ex_GNOME_Evolution_Calendar_CalFactory_InvalidURI))
			g_message ("cal_client_open_calendar: invalid URI");
		else if (BONOBO_USER_EX (&ev, ex_GNOME_Evolution_Calendar_CalFactory_UnsupportedMethod))
			 g_message ("cal_client_open_calendar: unsupported method");
		else
			g_message ("cal_client_open_calendar(): open request failed");
		bonobo_object_unref (BONOBO_OBJECT (priv->listener));
		priv->listener = NULL;
		priv->load_state = CAL_CLIENT_LOAD_NOT_LOADED;
		g_free (priv->uri);
		priv->uri = NULL;

		return FALSE;
	}
	CORBA_exception_free (&ev);

	return TRUE;
}

/* Builds an URI list out of a CORBA string sequence */
static GList *
build_uri_list (GNOME_Evolution_Calendar_StringSeq *seq)
{
	GList *uris = NULL;
	int i;

	for (i = 0; i < seq->_length; i++)
		uris = g_list_prepend (uris, g_strdup (seq->_buffer[i]));

	return uris;
}

/**
 * cal_client_uri_list:
 * @client: A calendar client
 * @type: type of uri's to get
 * 
 * 
 * Return value: A list of URI's open on the wombat
 **/
GList *
cal_client_uri_list (CalClient *client, CalMode mode)
{
	CalClientPrivate *priv;
	GNOME_Evolution_Calendar_StringSeq *uri_seq;
	GList *uris = NULL;	
	CORBA_Environment ev;
	GList *f;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (IS_CAL_CLIENT (client), FALSE);

	priv = client->priv;

	for (f = priv->factories; f; f = f->next) {
		CORBA_exception_init (&ev);
		uri_seq = GNOME_Evolution_Calendar_CalFactory_uriList (f->data, mode, &ev);

		if (BONOBO_EX (&ev)) {
			g_message ("cal_client_uri_list(): request failed");

			/* free memory and return */
			g_list_foreach (uris, (GFunc) g_free, NULL);
			g_list_free (uris);
			uris = NULL;
			break;
		}
		else
			uris = g_list_concat (uris, build_uri_list (uri_seq));
	
		CORBA_exception_free (&ev);
	}
	
	return uris;	
}

/**
 * cal_client_get_load_state:
 * @client: A calendar client.
 * 
 * Queries the state of loading of a calendar client.
 * 
 * Return value: A #CalClientLoadState value indicating whether the client has
 * not been loaded with cal_client_open_calendar() yet, whether it is being
 * loaded, or whether it is already loaded.
 **/
CalClientLoadState
cal_client_get_load_state (CalClient *client)
{
	CalClientPrivate *priv;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (IS_CAL_CLIENT (client), FALSE);

	priv = client->priv;
	return priv->load_state;
}

/**
 * cal_client_get_uri:
 * @client: A calendar client.
 * 
 * Queries the URI that is open in a calendar client.
 * 
 * Return value: The URI of the calendar that is already loaded or is being
 * loaded, or NULL if the client has not started a load request yet.
 **/
const char *
cal_client_get_uri (CalClient *client)
{
	CalClientPrivate *priv;

	g_return_val_if_fail (client != NULL, NULL);
	g_return_val_if_fail (IS_CAL_CLIENT (client), NULL);

	priv = client->priv;
	return priv->uri;
}

/* Converts our representation of a calendar component type into its CORBA representation */
static GNOME_Evolution_Calendar_CalObjType
corba_obj_type (CalObjType type)
{
	return (((type & CALOBJ_TYPE_EVENT) ? GNOME_Evolution_Calendar_TYPE_EVENT : 0)
		| ((type & CALOBJ_TYPE_TODO) ? GNOME_Evolution_Calendar_TYPE_TODO : 0)
		| ((type & CALOBJ_TYPE_JOURNAL) ? GNOME_Evolution_Calendar_TYPE_JOURNAL : 0));
}

gboolean
cal_client_set_mode (CalClient *client, CalMode mode)
{
	CalClientPrivate *priv;
	gboolean retval = TRUE;	
	CORBA_Environment ev;

	g_return_val_if_fail (client != NULL, -1);
	g_return_val_if_fail (IS_CAL_CLIENT (client), -1);

	priv = client->priv;
	g_return_val_if_fail (priv->load_state == CAL_CLIENT_LOAD_LOADED, -1);

	CORBA_exception_init (&ev);
	GNOME_Evolution_Calendar_Cal_setMode (priv->cal, mode, &ev);

	if (BONOBO_EX (&ev))
		retval = FALSE;
		
	CORBA_exception_free (&ev);

	return retval;
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
	g_return_val_if_fail (priv->load_state == CAL_CLIENT_LOAD_LOADED, -1);

	t = corba_obj_type (type);

	CORBA_exception_init (&ev);
	n = GNOME_Evolution_Calendar_Cal_countObjects (priv->cal, t, &ev);

	if (BONOBO_EX (&ev)) {
		g_message ("cal_client_get_n_objects(): could not get the number of objects");
		CORBA_exception_free (&ev);
		return -1;
	}

	CORBA_exception_free (&ev);
	return n;
}


/* This is used in the callback which fetches all the timezones needed for an
   object. */
typedef struct _CalClientGetTimezonesData CalClientGetTimezonesData;
struct _CalClientGetTimezonesData {
	CalClient *client;

	/* This starts out at CAL_CLIENT_GET_SUCCESS. If an error occurs this
	   contains the last error. */
	CalClientGetStatus status;
};


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
	GNOME_Evolution_Calendar_CalObj comp_str;
	CalClientGetStatus retval;
	icalcomponent *icalcomp;
	CalClientGetTimezonesData cb_data;

	g_return_val_if_fail (client != NULL, CAL_CLIENT_GET_NOT_FOUND);
	g_return_val_if_fail (IS_CAL_CLIENT (client), CAL_CLIENT_GET_NOT_FOUND);

	priv = client->priv;
	g_return_val_if_fail (priv->load_state == CAL_CLIENT_LOAD_LOADED, CAL_CLIENT_GET_NOT_FOUND);

	g_return_val_if_fail (uid != NULL, CAL_CLIENT_GET_NOT_FOUND);
	g_return_val_if_fail (comp != NULL, CAL_CLIENT_GET_NOT_FOUND);

	retval = CAL_CLIENT_GET_NOT_FOUND;
	*comp = NULL;

	CORBA_exception_init (&ev);
	comp_str = GNOME_Evolution_Calendar_Cal_getObject (priv->cal, (char *) uid, &ev);

	if (BONOBO_USER_EX (&ev, ex_GNOME_Evolution_Calendar_Cal_NotFound))
		goto out;
	else if (BONOBO_EX (&ev)) {		
		g_message ("cal_client_get_object(): could not get the object");
		goto out;
	}

	icalcomp = icalparser_parse_string (comp_str);
	CORBA_free (comp_str);

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

	/* Now make sure we have all timezones needed for this object.
	   We do this to try to avoid any problems caused by getting a timezone
	   in the middle of other code. Any calls to ORBit result in a 
	   recursive call of the GTK+ main loop, which can cause problems for
	   code that doesn't expect it. Currently GnomeCanvas has problems if
	   we try to get a timezone in the middle of a redraw, and there is a
	   resize pending, which leads to an assert failure and an abort. */
	cb_data.client = client;
	cb_data.status = CAL_CLIENT_GET_SUCCESS;
	icalcomponent_foreach_tzid (icalcomp,
				    cal_client_get_object_timezones_cb,
				    &cb_data);

	retval = cb_data.status;

 out:

	CORBA_exception_free (&ev);
	return retval;
}


static void
cal_client_get_object_timezones_cb (icalparameter *param,
				    void *data)
{
	CalClientGetTimezonesData *cb_data = data;
	const char *tzid;
	icaltimezone *zone;
	CalClientGetStatus status;

	tzid = icalparameter_get_tzid (param);
	if (!tzid) {
		cb_data->status = CAL_CLIENT_GET_SYNTAX_ERROR;
		return;
	}

	status = cal_client_get_timezone (cb_data->client, tzid, &zone);
	if (status != CAL_CLIENT_GET_SUCCESS)
		cb_data->status = status;
}


CalClientGetStatus
cal_client_get_timezone (CalClient *client,
			 const char *tzid,
			 icaltimezone **zone)
{
	CalClientPrivate *priv;
	CORBA_Environment ev;
	GNOME_Evolution_Calendar_CalObj comp_str;
	CalClientGetStatus retval;
	icalcomponent *icalcomp;
	icaltimezone *tmp_zone;

	g_return_val_if_fail (client != NULL, CAL_CLIENT_GET_NOT_FOUND);
	g_return_val_if_fail (IS_CAL_CLIENT (client), CAL_CLIENT_GET_NOT_FOUND);

	priv = client->priv;
	g_return_val_if_fail (priv->load_state == CAL_CLIENT_LOAD_LOADED,
			      CAL_CLIENT_GET_NOT_FOUND);

	g_return_val_if_fail (zone != NULL, CAL_CLIENT_GET_NOT_FOUND);

	/* If tzid is NULL or "" we return NULL, since it is a 'local time'. */
	if (!tzid || !tzid[0]) {
		*zone = NULL;
		return CAL_CLIENT_GET_SUCCESS;
	}

	/* If it is UTC, we return the special UTC timezone. */
	if (!strcmp (tzid, "UTC")) {
		*zone = icaltimezone_get_utc_timezone ();
		return CAL_CLIENT_GET_SUCCESS;
	}

	/* See if we already have it in the cache. */
	tmp_zone = g_hash_table_lookup (priv->timezones, tzid);
	if (tmp_zone) {
		*zone = tmp_zone;
		return CAL_CLIENT_GET_SUCCESS;
	}

	retval = CAL_CLIENT_GET_NOT_FOUND;
	*zone = NULL;

	/* We don't already have it, so we try to get it from the server. */
	CORBA_exception_init (&ev);
	comp_str = GNOME_Evolution_Calendar_Cal_getTimezoneObject (priv->cal, (char *) tzid, &ev);

	if (BONOBO_USER_EX (&ev, ex_GNOME_Evolution_Calendar_Cal_NotFound))
		goto out;
	else if (BONOBO_EX (&ev)) {
		g_message ("cal_client_get_timezone(): could not get the object");
		goto out;
	}

	icalcomp = icalparser_parse_string (comp_str);
	CORBA_free (comp_str);

	if (!icalcomp) {
		retval = CAL_CLIENT_GET_SYNTAX_ERROR;
		goto out;
	}

	tmp_zone = icaltimezone_new ();
	if (!tmp_zone) {
		/* FIXME: Needs better error code - out of memory. Or just
		   abort like GTK+ does? */
		retval = CAL_CLIENT_GET_NOT_FOUND;
		goto out;
	}

	if (!icaltimezone_set_component (tmp_zone, icalcomp)) {
		retval = CAL_CLIENT_GET_SYNTAX_ERROR;
		goto out;
	}

	/* Now add it to the cache, to avoid the server call in future. */
	g_hash_table_insert (priv->timezones, icaltimezone_get_tzid (tmp_zone),
			     tmp_zone);

	*zone = tmp_zone;
	retval = CAL_CLIENT_GET_SUCCESS;

 out:

	CORBA_exception_free (&ev);
	return retval;
}

/* Resolves TZIDs for the recurrence generator. */
icaltimezone*
cal_client_resolve_tzid_cb (const char *tzid, gpointer data)
{
	CalClient *client;
	icaltimezone *zone = NULL;
	CalClientGetStatus status;

	g_return_val_if_fail (data != NULL, NULL);
	g_return_val_if_fail (IS_CAL_CLIENT (data), NULL);
	
	client = CAL_CLIENT (data);

	/* FIXME: Handle errors. */
	status = cal_client_get_timezone (client, tzid, &zone);

	return zone;
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
 * Return value: A list of strings that are the sought UIDs.  This should be
 * freed using the cal_obj_uid_list_free() function.
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
	g_return_val_if_fail (priv->load_state == CAL_CLIENT_LOAD_LOADED, NULL);

	t = corba_obj_type (type);

	CORBA_exception_init (&ev);

	seq = GNOME_Evolution_Calendar_Cal_getUIDs (priv->cal, t, &ev);
	if (BONOBO_EX (&ev)) {
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
	g_return_val_if_fail (priv->load_state == CAL_CLIENT_LOAD_LOADED, NULL);

	t = corba_obj_type (type);
	CORBA_exception_init (&ev);

	seq = GNOME_Evolution_Calendar_Cal_getChanges (priv->cal, t, change_id, &ev);
	if (BONOBO_EX (&ev)) {
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
	g_return_val_if_fail (priv->load_state == CAL_CLIENT_LOAD_LOADED, NULL);

	g_return_val_if_fail (start != -1 && end != -1, NULL);
	g_return_val_if_fail (start <= end, NULL);

	CORBA_exception_init (&ev);

	t = corba_obj_type (type);

	seq = GNOME_Evolution_Calendar_Cal_getObjectsInRange (priv->cal, t, start, end, &ev);
	if (BONOBO_EX (&ev)) {
		g_message ("cal_client_get_objects_in_range(): could not get the objects");
		CORBA_exception_free (&ev);
		return NULL;
	}
	CORBA_exception_free (&ev);

	uids = build_uid_list (seq);
	CORBA_free (seq);

	return uids;
}

/**
 * cal_client_get_free_busy
 * @client:: A calendar client.
 * @users: List of users to retrieve free/busy information for.
 * @start: Start time for query.
 * @end: End time for query.
 *
 * Gets free/busy information from the calendar server.
 *
 * Returns: a GList of VFREEBUSY CalComponents
 */
GList *
cal_client_get_free_busy (CalClient *client, GList *users,
			  time_t start, time_t end)
{
	CalClientPrivate *priv;
	CORBA_Environment ev;
	GNOME_Evolution_Calendar_UserList *corba_list;
	GNOME_Evolution_Calendar_CalObjSeq *calobj_list;
	GList *l;
	GList *comp_list = NULL;
	int len, i;

	g_return_val_if_fail (client != NULL, NULL);
	g_return_val_if_fail (IS_CAL_CLIENT (client), NULL);

	priv = client->priv;
	g_return_val_if_fail (priv->load_state == CAL_CLIENT_LOAD_LOADED, NULL);

	g_return_val_if_fail (start != -1 && end != -1, NULL);
	g_return_val_if_fail (start <= end, NULL);

	/* create the CORBA user list to be passed to the backend */
	len = g_list_length (users);

	corba_list = GNOME_Evolution_Calendar_UserList__alloc ();
	CORBA_sequence_set_release (corba_list, TRUE);
	corba_list->_length = len;
	corba_list->_buffer = CORBA_sequence_GNOME_Evolution_Calendar_User_allocbuf (len);

	for (l = g_list_first (users), i = 0; l; l = l->next, i++)
		corba_list->_buffer[i] = CORBA_string_dup ((CORBA_char *) l->data);

	/* call the method on the backend */
	CORBA_exception_init (&ev);

	calobj_list = GNOME_Evolution_Calendar_Cal_getFreeBusy (priv->cal, corba_list,
								start, end, &ev);
	CORBA_free (corba_list);
	if (BONOBO_EX (&ev) || !calobj_list) {
		g_message ("cal_client_get_free_busy(): could not get the objects");
		CORBA_exception_free (&ev);
		return NULL;
	}

	for (i = 0; i < calobj_list->_length; i++) {
		CalComponent *comp;
		icalcomponent *icalcomp;
		icalcomponent_kind kind;

		icalcomp = icalparser_parse_string (calobj_list->_buffer[i]);
		if (!icalcomp)
			continue;

		kind = icalcomponent_isa (icalcomp);
		if (kind == ICAL_VFREEBUSY_COMPONENT) {
			comp = cal_component_new ();
			if (!cal_component_set_icalcomponent (comp, icalcomp)) {
				icalcomponent_free (icalcomp);
				gtk_object_unref (GTK_OBJECT (comp));
				continue;
			}

			comp_list = g_list_append (comp_list, comp);
		}
		else
			icalcomponent_free (icalcomp);
	}

	CORBA_exception_free (&ev);
	CORBA_free (calobj_list);

	return comp_list;
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
	g_return_if_fail (priv->load_state == CAL_CLIENT_LOAD_LOADED);

	g_return_if_fail (start != -1 && end != -1);
	g_return_if_fail (start <= end);
	g_return_if_fail (cb != NULL);

	/* Generate objects */

	objects = get_objects_atomically (client, type, start, end);
	instances = NULL;

	for (l = objects; l; l = l->next) {
		CalComponent *comp;

		comp = l->data;
		cal_recur_generate_instances (comp, start, end, add_instance, &instances,
					      cal_client_resolve_tzid_cb, client);
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

/* Builds a list of CalAlarmInstance structures */
static GSList *
build_alarm_instance_list (CalComponent *comp, GNOME_Evolution_Calendar_CalAlarmInstanceSeq *seq)
{
	GSList *alarms;
	int i;

	alarms = NULL;

	for (i = 0; i < seq->_length; i++) {
		GNOME_Evolution_Calendar_CalAlarmInstance *corba_instance;
		CalComponentAlarm *alarm;
		const char *auid;
		CalAlarmInstance *instance;

		corba_instance = seq->_buffer + i;

		/* Since we want the in-commponent auid, we look for the alarm
		 * in the component and fetch its "real" auid.
		 */

		alarm = cal_component_get_alarm (comp, corba_instance->auid);
		if (!alarm)
			continue;

		auid = cal_component_alarm_get_uid (alarm);
		cal_component_alarm_free (alarm);

		instance = g_new (CalAlarmInstance, 1);
		instance->auid = auid;
		instance->trigger = corba_instance->trigger;
		instance->occur_start = corba_instance->occur_start;
		instance->occur_end = corba_instance->occur_end;

		alarms = g_slist_prepend (alarms, instance);
	}

	return g_slist_reverse (alarms);
}

/* Builds a list of CalComponentAlarms structures */
static GSList *
build_component_alarms_list (GNOME_Evolution_Calendar_CalComponentAlarmsSeq *seq)
{
	GSList *comp_alarms;
	int i;

	comp_alarms = NULL;

	for (i = 0; i < seq->_length; i++) {
		GNOME_Evolution_Calendar_CalComponentAlarms *corba_alarms;
		CalComponent *comp;
		CalComponentAlarms *alarms;
		icalcomponent *icalcomp;

		corba_alarms = seq->_buffer + i;

		icalcomp = icalparser_parse_string (corba_alarms->calobj);
		if (!icalcomp)
			continue;

		comp = cal_component_new ();
		if (!cal_component_set_icalcomponent (comp, icalcomp)) {
			icalcomponent_free (icalcomp);
			gtk_object_unref (GTK_OBJECT (comp));
			continue;
		}

		alarms = g_new (CalComponentAlarms, 1);
		alarms->comp = comp;
		alarms->alarms = build_alarm_instance_list (comp, &corba_alarms->alarms);

		comp_alarms = g_slist_prepend (comp_alarms, alarms);
	}

	return comp_alarms;
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
 * Return value: A list of #CalComponentAlarms structures.  This should be freed
 * using the cal_client_free_alarms() function, or by freeing each element
 * separately with cal_component_alarms_free() and then freeing the list with
 * g_slist_free().
 **/
GSList *
cal_client_get_alarms_in_range (CalClient *client, time_t start, time_t end)
{
	CalClientPrivate *priv;
	CORBA_Environment ev;
	GNOME_Evolution_Calendar_CalComponentAlarmsSeq *seq;
	GSList *alarms;

	g_return_val_if_fail (client != NULL, NULL);
	g_return_val_if_fail (IS_CAL_CLIENT (client), NULL);

	priv = client->priv;
	g_return_val_if_fail (priv->load_state == CAL_CLIENT_LOAD_LOADED, NULL);

	g_return_val_if_fail (start != -1 && end != -1, NULL);
	g_return_val_if_fail (start <= end, NULL);

	CORBA_exception_init (&ev);

	seq = GNOME_Evolution_Calendar_Cal_getAlarmsInRange (priv->cal, start, end, &ev);
	if (BONOBO_EX (&ev)) {
		g_message ("cal_client_get_alarms_in_range(): could not get the alarm range");
		CORBA_exception_free (&ev);
		return NULL;
	}
	CORBA_exception_free (&ev);

	alarms = build_component_alarms_list (seq);
	CORBA_free (seq);

	return alarms;
}

/**
 * cal_client_free_alarms:
 * @comp_alarms: A list of #CalComponentAlarms structures.
 * 
 * Frees a list of #CalComponentAlarms structures as returned by
 * cal_client_get_alarms_in_range().
 **/
void
cal_client_free_alarms (GSList *comp_alarms)
{
	GSList *l;

	for (l = comp_alarms; l; l = l->next) {
		CalComponentAlarms *alarms;

		alarms = l->data;
		g_assert (alarms != NULL);

		cal_component_alarms_free (alarms);
	}

	g_slist_free (comp_alarms);
}

/**
 * cal_client_get_alarms_for_object:
 * @client: A calendar client.
 * @uid: Unique identifier for a calendar component.
 * @start: Start time for query.
 * @end: End time for query.
 * @alarms: Return value for the component's alarm instances.  Will return NULL
 * if no instances occur within the specified time range.  This should be freed
 * using the cal_component_alarms_free() function.
 *
 * Queries a calendar for the alarms of a particular object that trigger in the
 * specified range of time.
 *
 * Return value: TRUE on success, FALSE if the object was not found.
 **/
gboolean
cal_client_get_alarms_for_object (CalClient *client, const char *uid,
				  time_t start, time_t end,
				  CalComponentAlarms **alarms)
{
	CalClientPrivate *priv;
	CORBA_Environment ev;
	GNOME_Evolution_Calendar_CalComponentAlarms *corba_alarms;
	gboolean retval;
	icalcomponent *icalcomp;
	CalComponent *comp;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (IS_CAL_CLIENT (client), FALSE);

	priv = client->priv;
	g_return_val_if_fail (priv->load_state == CAL_CLIENT_LOAD_LOADED, FALSE);

	g_return_val_if_fail (uid != NULL, FALSE);
	g_return_val_if_fail (start != -1 && end != -1, FALSE);
	g_return_val_if_fail (start <= end, FALSE);
	g_return_val_if_fail (alarms != NULL, FALSE);

	*alarms = NULL;
	retval = FALSE;

	CORBA_exception_init (&ev);

	corba_alarms = GNOME_Evolution_Calendar_Cal_getAlarmsForObject (priv->cal, (char *) uid,
									start, end, &ev);
	if (BONOBO_USER_EX (&ev, ex_GNOME_Evolution_Calendar_Cal_NotFound))
		goto out;
	else if (BONOBO_EX (&ev)) {
		g_message ("cal_client_get_alarms_for_object(): could not get the alarm range");
		goto out;
	}

	icalcomp = icalparser_parse_string (corba_alarms->calobj);
	if (!icalcomp)
		goto out;

	comp = cal_component_new ();
	if (!cal_component_set_icalcomponent (comp, icalcomp)) {
		icalcomponent_free (icalcomp);
		gtk_object_unref (GTK_OBJECT (comp));
		goto out;
	}

	retval = TRUE;

	*alarms = g_new (CalComponentAlarms, 1);
	(*alarms)->comp = comp;
	(*alarms)->alarms = build_alarm_instance_list (comp, &corba_alarms->alarms);
	CORBA_free (corba_alarms);

 out:
	CORBA_exception_free (&ev);
	return retval;
}

typedef struct _ForeachTZIDCallbackData ForeachTZIDCallbackData;
struct _ForeachTZIDCallbackData {
	CalClient *client;
	GHashTable *timezone_hash;
};

/* This adds the VTIMEZONE given by the TZID parameter to the GHashTable in
   data. */
static void
foreach_tzid_callback (icalparameter *param, void *cbdata)
{
	ForeachTZIDCallbackData *data = cbdata;
	CalClientPrivate *priv;
	const char *tzid;
	icaltimezone *zone;
	icalcomponent *vtimezone_comp;
	char *vtimezone_as_string;

	priv = data->client->priv;

	/* Get the TZID string from the parameter. */
	tzid = icalparameter_get_tzid (param);
	if (!tzid)
		return;

	/* Check if it is in our cache. If it is, it must already be on the
	   server so return. */
	if (g_hash_table_lookup (priv->timezones, tzid))
		return;

	/* Check if we've already added it to the GHashTable. */
	if (g_hash_table_lookup (data->timezone_hash, tzid))
		return;

	/* Check if it is a builtin timezone. If it isn't, return. */
	zone = icaltimezone_get_builtin_timezone_from_tzid (tzid);
	if (!zone)
		return;

	/* Convert it to a string and add it to the hash. */
	vtimezone_comp = icaltimezone_get_component (zone);
	if (!vtimezone_comp)
		return;

	vtimezone_as_string = icalcomponent_as_ical_string (vtimezone_comp);

	g_hash_table_insert (data->timezone_hash, (char*) tzid,
			     g_strdup (vtimezone_as_string));
}

/* This appends the value string to the GString given in data. */
static void
append_timezone_string (gpointer key, gpointer value, gpointer data)
{
	GString *vcal_string = data;

	g_string_append (vcal_string, value);
	g_free (value);
}


/* This converts the VEVENT/VTODO to a string. It checks if we need to send
   any builtin timezones to the server along with the object.
   To do that we check every TZID in the component to see if it is a builtin
   timezone. If it is, we see if it it in our cache. If it is in our cache,
   then we know the server already has it and we don't need to send it.
   If it isn't in our cache, then we need to send it to the server.
   If we need to send any timezones to the server, then we have to create a
   complete VCALENDAR object, otherwise we can just send a single VEVENT/VTODO
   as before. */
static char*
cal_client_get_component_as_string (CalClient *client,
				    CalComponent *comp)
{
	GHashTable *timezone_hash;
	GString *vcal_string;
	int initial_vcal_string_len;
	ForeachTZIDCallbackData cbdata;
	char *obj_string;

	CalClientPrivate *priv;

	priv = client->priv;

	timezone_hash = g_hash_table_new (g_str_hash, g_str_equal);

	/* Add any builtin timezones needed to the hash. We use a hash since
	   we only want to add each timezone once at most. */
	cbdata.client = client;
	cbdata.timezone_hash = timezone_hash;
	icalcomponent_foreach_tzid (cal_component_get_icalcomponent (comp),
				    foreach_tzid_callback, &cbdata);

	/* Create the start of a VCALENDAR, to add the VTIMEZONES to,
	   and remember its length so we know if any VTIMEZONEs get added. */
	vcal_string = g_string_new (NULL);
	g_string_append (vcal_string,
			 "BEGIN:VCALENDAR\n"
			 "PRODID:-//Ximian//NONSGML Evolution Calendar//EN\n"
			 "VERSION:2.0\n");
	initial_vcal_string_len = vcal_string->len;

	/* Now concatenate all the timezone strings. This also frees the
	   timezone strings as it goes. */
	g_hash_table_foreach (timezone_hash, append_timezone_string,
			      vcal_string);

	/* Get the string for the VEVENT/VTODO. */
	obj_string = cal_component_get_as_string (comp);

	/* If there were any timezones to send, create a complete VCALENDAR,
	   else just send the VEVENT/VTODO string. */
	if (vcal_string->len == initial_vcal_string_len) {
		g_string_free (vcal_string, TRUE);
	} else {
		g_string_append (vcal_string, obj_string);
		g_string_append (vcal_string, "END:VCALENDAR\n");
		g_free (obj_string);
		obj_string = vcal_string->str;
		g_string_free (vcal_string, FALSE);
	}

	g_hash_table_destroy (timezone_hash);

	return obj_string;
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

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (IS_CAL_CLIENT (client), FALSE);

	priv = client->priv;
	g_return_val_if_fail (priv->load_state == CAL_CLIENT_LOAD_LOADED, FALSE);

	g_return_val_if_fail (comp != NULL, FALSE);

	retval = FALSE;

	cal_component_commit_sequence (comp);

	obj_string = cal_client_get_component_as_string (client, comp);
	if (obj_string == NULL)
		return FALSE;

	CORBA_exception_init (&ev);
	GNOME_Evolution_Calendar_Cal_updateObjects (priv->cal, obj_string, &ev);
	g_free (obj_string);
	
	if (BONOBO_USER_EX (&ev, ex_GNOME_Evolution_Calendar_Cal_InvalidObject))
	    goto out;
	else if (BONOBO_EX (&ev)) {
		g_message ("cal_client_update_object(): could not update the object");
		goto out;
	}

	retval = TRUE;

 out:
	CORBA_exception_free (&ev);
	return retval;
}

/**
 * cal_client_update_objects:
 * @client: A calendar client.
 * @icalcomp: A toplevel VCALENDAR libical component.
 *
 * Asks a calendar to add or update one or more components, possibly including
 * VTIMEZONE data.  Any existing components with the same UIDs will be
 * replaced. The VTIMEZONE data will be compared to existing VTIMEZONEs in
 * the calendar, and the VTIMEZONEs may possibly be renamed, as well as all
 * references to them throughout the VCALENDAR.
 *
 * The client program should not assume that the objects are actually in the
 * server's storage until it has received the "obj_updated" notification
 * signal.
 *
 * Return value: TRUE on success, FALSE on specifying an invalid component.
 **/
gboolean
cal_client_update_objects (CalClient *client, icalcomponent *icalcomp)
{
	CalClientPrivate *priv;
	CORBA_Environment ev;
	gboolean retval;
	char *obj_string;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (IS_CAL_CLIENT (client), FALSE);

	priv = client->priv;
	g_return_val_if_fail (priv->load_state == CAL_CLIENT_LOAD_LOADED,
			      FALSE);

	g_return_val_if_fail (icalcomp != NULL, FALSE);

	retval = FALSE;

	/* Libical owns this memory, using one of its temporary buffers. */
	obj_string = icalcomponent_as_ical_string (icalcomp);

	CORBA_exception_init (&ev);
	GNOME_Evolution_Calendar_Cal_updateObjects (priv->cal, obj_string, &ev);

	if (BONOBO_USER_EX (&ev, ex_GNOME_Evolution_Calendar_Cal_InvalidObject))
		goto out;
	else if (BONOBO_EX (&ev)) {
		g_message ("cal_client_update_objects(): could not update the objects");
		goto out;
	}

	retval = TRUE;

 out:
	CORBA_exception_free (&ev);
	return retval;

}


/**
 * cal_client_remove_object:
 * @client: A calendar client.
 * @uid: Unique identifier of the calendar component to remove.
 * 
 * Asks a calendar to remove a component.  If the server is able to remove the
 * component, all clients will be notified and they will emit the "obj_removed"
 * signal.
 * 
 * Return value: TRUE on success, FALSE on specifying a UID for a component that
 * is not in the server.  Returning FALSE is normal; the object may have
 * disappeared from the server before the client has had a chance to receive the
 * corresponding notification.
 **/
gboolean
cal_client_remove_object (CalClient *client, const char *uid)
{
	CalClientPrivate *priv;
	CORBA_Environment ev;
	gboolean retval;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (IS_CAL_CLIENT (client), FALSE);

	priv = client->priv;
	g_return_val_if_fail (priv->load_state == CAL_CLIENT_LOAD_LOADED, FALSE);

	g_return_val_if_fail (uid != NULL, FALSE);

	retval = FALSE;

	CORBA_exception_init (&ev);
	GNOME_Evolution_Calendar_Cal_removeObject (priv->cal, (char *) uid, &ev);

	if (BONOBO_USER_EX (&ev,  ex_GNOME_Evolution_Calendar_Cal_NotFound))
		goto out;
	else if (BONOBO_EX (&ev)) {
		g_message ("cal_client_remove_object(): could not remove the object");
		goto out;
	}

	retval = TRUE;

 out:
	CORBA_exception_free (&ev);
	return retval;
}

/**
 * cal_client_get_query:
 * @client: A calendar client.
 * @sexp: S-expression representing the query.
 * 
 * Creates a live query object from a loaded calendar.
 * 
 * Return value: A query object that will emit notification signals as calendar
 * components are added and removed from the query in the server.
 **/
CalQuery *
cal_client_get_query (CalClient *client, const char *sexp)
{
	CalClientPrivate *priv;

	g_return_val_if_fail (client != NULL, NULL);
	g_return_val_if_fail (IS_CAL_CLIENT (client), NULL);

	priv = client->priv;
	g_return_val_if_fail (priv->load_state == CAL_CLIENT_LOAD_LOADED, FALSE);

	g_return_val_if_fail (sexp != NULL, NULL);

	return cal_query_new (priv->cal, sexp);
}
