/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* calendar-offline-handler.c
 *
 * Copyright (C) 2001  Ximian, Inc.
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
 * Authors:
 *   Ettore Perazzoli <ettore@ximian.com>
 *   Dan Winship <danw@ximian.com>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gtk/gtkmain.h>
#include <gtk/gtksignal.h>
#include <bonobo/bonobo-exception.h>
#include <gal/util/e-util.h>
#include "e-util/e-url.h"
#include <cal-client/cal-client.h>
#include "calendar-offline-handler.h"

#define PARENT_TYPE bonobo_x_object_get_type ()
static BonoboXObjectClass *parent_class = NULL;

struct _CalendarOfflineHandlerPrivate {
	CalClient *client;
	
	GNOME_Evolution_OfflineProgressListener listener_interface;

	gboolean is_offline;	
};

static void
add_connection (gpointer data, gpointer user_data)
{
	EUri *uri = e_uri_new (data);	
	GNOME_Evolution_ConnectionList *list = user_data;

	g_return_if_fail (uri != NULL);
	
	if (uri->host != NULL)
		list->_buffer[list->_length].hostName = CORBA_string_dup (uri->host);
	else
		list->_buffer[list->_length].hostName = CORBA_string_dup ("Unknown");
	if (uri->protocol != NULL)
		list->_buffer[list->_length].type = CORBA_string_dup (uri->protocol);
	else
		list->_buffer[list->_length].type = CORBA_string_dup ("Unknown");
	list->_length++;

	e_uri_free (uri);
}

static GNOME_Evolution_ConnectionList *
create_connection_list (CalendarOfflineHandler *offline_handler)
{
	CalendarOfflineHandlerPrivate *priv;
	GNOME_Evolution_ConnectionList *list;
	GList *uris;

	priv = offline_handler->priv;

 	uris = cal_client_uri_list (priv->client, CAL_MODE_REMOTE);	

	list = GNOME_Evolution_ConnectionList__alloc ();
	list->_length = 0;
	list->_maximum = g_list_length (uris);
	list->_buffer = CORBA_sequence_GNOME_Evolution_Connection_allocbuf (list->_maximum);

	g_list_foreach (uris, add_connection, list);

	return list;
}

/* GNOME::Evolution::Offline methods.  */
static CORBA_boolean
impl__get_isOffline (PortableServer_Servant servant,
		     CORBA_Environment *ev)
{
	CalendarOfflineHandler *offline_handler;
	CalendarOfflineHandlerPrivate *priv;
	
	offline_handler = CALENDAR_OFFLINE_HANDLER (bonobo_object_from_servant (servant));
	priv = offline_handler->priv;

	return priv->is_offline;
}

static void
impl_prepareForOffline (PortableServer_Servant servant,
			GNOME_Evolution_ConnectionList **active_connection_list,
			CORBA_Environment *ev)
{
	CalendarOfflineHandler *offline_handler;
	CalendarOfflineHandlerPrivate *priv;
	
	offline_handler = CALENDAR_OFFLINE_HANDLER (bonobo_object_from_servant (servant));
	priv = offline_handler->priv;

	*active_connection_list = create_connection_list (offline_handler);
}

static void
update_offline (CalendarOfflineHandler *offline_handler)
{
	CalendarOfflineHandlerPrivate *priv;
	GNOME_Evolution_ConnectionList *connection_list;
	CORBA_Environment ev;

	priv = offline_handler->priv;

	connection_list = create_connection_list (offline_handler);

	CORBA_exception_init (&ev);

	GNOME_Evolution_OfflineProgressListener_updateProgress (priv->listener_interface, 
								connection_list, &ev);

	if (BONOBO_EX (&ev))
		g_warning ("Error updating offline progress");

	CORBA_exception_free (&ev);
}

static void
backend_cal_set_mode (CalClient *client, CalClientSetModeStatus status, CalMode mode, gpointer data)
{
	CalendarOfflineHandler *offline_handler = data;

	update_offline (offline_handler);
}

static void
backend_cal_opened (CalClient *client, CalClientOpenStatus status, gpointer data)
{
	CalendarOfflineHandler *offline_handler = data;

	if (status != CAL_CLIENT_OPEN_SUCCESS) {
		update_offline (offline_handler);
		gtk_object_unref (GTK_OBJECT (client));
		return;
	}

	gtk_signal_connect (GTK_OBJECT (client), "cal_set_mode", 
			    backend_cal_set_mode, offline_handler);
	cal_client_set_mode (client, CAL_MODE_LOCAL);
}

static void
backend_go_offline (gpointer data, gpointer user_data)
{
	CalendarOfflineHandler *offline_handler = user_data;
	char *uri = data;
	CalClient *client;
	gboolean success;
	
	client = cal_client_new ();
	gtk_signal_connect (GTK_OBJECT (client), "cal_opened", 
			    backend_cal_opened, offline_handler);
	success = cal_client_open_calendar (client, uri, TRUE);
	if (!success) {
		update_offline (offline_handler);
		gtk_object_unref (GTK_OBJECT (client));
		return;		
	}	
}

static void
impl_goOffline (PortableServer_Servant servant,
		const GNOME_Evolution_OfflineProgressListener progress_listener,
		CORBA_Environment *ev)
{
	CalendarOfflineHandler *offline_handler;
	CalendarOfflineHandlerPrivate *priv;
	GList *uris;
	
	offline_handler = CALENDAR_OFFLINE_HANDLER (bonobo_object_from_servant (servant));
	priv = offline_handler->priv;

	/* To update the status */
	priv->listener_interface = CORBA_Object_duplicate (progress_listener, ev);

	uris = cal_client_uri_list (priv->client, CAL_MODE_REMOTE);

	g_list_foreach (uris, backend_go_offline, offline_handler);	
}

static void
impl_goOnline (PortableServer_Servant servant,
	       CORBA_Environment *ev)
{
	CalendarOfflineHandler *offline_handler;
	CalendarOfflineHandlerPrivate *priv;

	offline_handler = CALENDAR_OFFLINE_HANDLER (bonobo_object_from_servant (servant));
	priv = offline_handler->priv;
}

/* GtkObject methods.  */

static void
impl_destroy (GtkObject *object)
{
	CalendarOfflineHandler *offline_handler;
	CalendarOfflineHandlerPrivate *priv;

	offline_handler = CALENDAR_OFFLINE_HANDLER (object);
	priv = offline_handler->priv;

	if (priv->listener_interface != CORBA_OBJECT_NIL) {
		CORBA_Environment ev;

		CORBA_exception_init (&ev);
		CORBA_Object_release (priv->listener_interface, &ev);
		CORBA_exception_free (&ev);
	}

	g_free (priv);

	if (GTK_OBJECT_CLASS (parent_class)->destroy != NULL)
		(* GTK_OBJECT_CLASS (parent_class)->destroy) (object);
}

/* GTK+ type initialization.  */

static void
calendar_offline_handler_class_init (CalendarOfflineHandlerClass *klass)
{
	GtkObjectClass *object_class;
	POA_GNOME_Evolution_Offline__epv *epv;

	object_class = GTK_OBJECT_CLASS (klass);
	object_class->destroy = impl_destroy;

	epv = & klass->epv;
	epv->_get_isOffline    = impl__get_isOffline;
	epv->prepareForOffline = impl_prepareForOffline;
	epv->goOffline         = impl_goOffline;
	epv->goOnline          = impl_goOnline;

	parent_class = gtk_type_class (PARENT_TYPE);
}

static void
calendar_offline_handler_init (CalendarOfflineHandler *offline_handler)
{
	CalendarOfflineHandlerPrivate *priv;

	priv = g_new (CalendarOfflineHandlerPrivate, 1);
	offline_handler->priv = priv;

	priv->client = cal_client_new ();
	priv->listener_interface = CORBA_OBJECT_NIL;
	priv->is_offline = FALSE;
}

CalendarOfflineHandler *
calendar_offline_handler_new (void)
{
	CalendarOfflineHandler *new;

	new = gtk_type_new (calendar_offline_handler_get_type ());
	
	return new;
}

BONOBO_X_TYPE_FUNC_FULL (CalendarOfflineHandler, GNOME_Evolution_Offline, PARENT_TYPE, calendar_offline_handler);
