/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Exports the Evolution:Messenger:Listener interface.  Maintains a
 * queue of async messages which come in from the Personal Messaging
 * Server.
 *
 * Author:
 *   Nat Friedman (nat@ximian.com)
 *
 * Copyright 2000, Ximian, Inc.
 */

#include <config.h>
#include <gtk/gtksignal.h>
#include "e-messenger-listener.h"

enum {
	MESSAGES_QUEUED,
	LAST_SIGNAL
};

static guint e_messenger_listener_signals [LAST_SIGNAL];

static BonoboObjectClass              *e_messenger_listener_parent_class;
POA_GNOME_Evolution_Messenger_Listener__vepv  e_messenger_listener_vepv;

struct _EMessengerListenerPrivate {
	GList   *message_queue;
	gint     timeout_id;

	guint timeout_lock : 1;
	guint stopped      : 1;
};

static void
message_free (EMessengerListenerMessage *msg)
{
	if (msg == NULL)
		return;

	e_messenger_identity_free (msg->who);
	e_messenger_identity_free (msg->id);
	g_free (msg->message);

	if (msg->backend != CORBA_OBJECT_NIL) {
		CORBA_Environment ev;
		
		CORBA_exception_init (&ev);
		
		bonobo_object_release_unref (msg->backend, &ev);
		
		if (ev._major != CORBA_NO_EXCEPTION) {
			g_warning ("e_messenger_listener_destroy: "
				   "Exception destroying backend "
				   "in message queue!\n");
		}
		
		CORBA_exception_free (&ev);
	}

	g_free (msg);
}

static gboolean
e_messenger_listener_check_queue (EMessengerListener *listener)
{
	if (listener->priv->timeout_lock)
		return TRUE;

	listener->priv->timeout_lock = TRUE;

	if (listener->priv->message_queue != NULL && !listener->priv->stopped) {
		gtk_signal_emit (GTK_OBJECT (listener), e_messenger_listener_signals [MESSAGES_QUEUED]);
	}

	if (listener->priv->message_queue == NULL || listener->priv->stopped) {
		listener->priv->timeout_id = 0;
		listener->priv->timeout_lock = FALSE;
		bonobo_object_unref (BONOBO_OBJECT (listener)); /* release the timeout's reference */
		return FALSE;
	}

	listener->priv->timeout_lock = FALSE;
	return TRUE;
}

static void
e_messenger_listener_queue_message (EMessengerListener        *listener,
				    EMessengerListenerMessage *message)
{
	if (message == NULL)
		return;

	if (listener->priv->stopped) {
		message_free (message);
		return;
	}

	listener->priv->message_queue = g_list_append (listener->priv->message_queue, message);

	if (listener->priv->timeout_id == 0) {

		/* 20 == an arbitrary small integer */
		listener->priv->timeout_id = g_timeout_add (20, (GSourceFunc) e_messenger_listener_check_queue, listener);

		/* Hold a reference on behalf of the timeout */
		bonobo_object_ref (BONOBO_OBJECT (listener));
	}
}

static void
e_messenger_listener_queue_signon_result (EMessengerListener                 *listener,
					  EMessengerIdentity                 *id,
					  GNOME_Evolution_Messenger_Backend   backend,
					  EMessengerSignonError               signon_error)
{
	EMessengerListenerMessage *msg;

	if (listener->priv->stopped)
		return;

	msg = g_new0 (EMessengerListenerMessage, 1);

	msg->type         = SignonResponse;
	msg->id           = id;
	msg->signon_error = signon_error;
	msg->backend      = backend;

	e_messenger_listener_queue_message (listener, msg);
}

static void
e_messenger_listener_queue_receive_message (EMessengerListener *listener,
					    EMessengerIdentity *id,
					    EMessengerIdentity *sender,
					    char               *message,
					    gboolean            autoresponse)
{
	EMessengerListenerMessage *msg;

	if (listener->priv->stopped)
		return;

	msg = g_new0 (EMessengerListenerMessage, 1);

	msg->type         = ReceiveMessageEvent;
	msg->id           = id;
	msg->who          = sender;
	msg->message      = message;
	msg->autoresponse = autoresponse;

	e_messenger_listener_queue_message (listener, msg);
}

static void
e_messenger_listener_queue_contact_update (EMessengerListener         *listener,
					   EMessengerIdentity         *id,
					   EMessengerIdentity         *contact,
					   const EMessengerUserStatus  user_status,
					   gboolean                    online)
{
	EMessengerListenerMessage *msg;

	if (listener->priv->stopped)
		return;

	msg = g_new0 (EMessengerListenerMessage, 1);

	msg->type         = ContactUpdateEvent;
	msg->id           = id;
	msg->who          = contact;
	msg->online       = online;
	msg->user_status  = user_status;

	e_messenger_listener_queue_message (listener, msg);
}

static EMessengerSignonError
corba_signon_error_to_signon_error (const GNOME_Evolution_Messenger_Listener_SignonError corba_signon_error)
{
	switch (corba_signon_error) {
	case GNOME_Evolution_Messenger_Listener_SignonError_NONE:
		return E_MESSENGER_SIGNON_ERROR_NONE;
	case GNOME_Evolution_Messenger_Listener_SignonError_INVALID_LOGIN:
		return E_MESSENGER_SIGNON_ERROR_INVALID_LOGIN;
	case GNOME_Evolution_Messenger_Listener_SignonError_NET_FAILURE:
		return E_MESSENGER_SIGNON_ERROR_NET_FAILURE;
	default:
		g_warning ("Unknown CORBA SignonError!");
		return 	E_MESSENGER_SIGNON_ERROR_UNKNOWN_FAILURE;
	}
}

static void
impl_Listener_signon_result (PortableServer_Servant                                servant,
			     const GNOME_Evolution_Messenger_Identity              corba_id,
			     const GNOME_Evolution_Messenger_Backend               backend,
			     const GNOME_Evolution_Messenger_Listener_SignonError  corba_signon_error,
			     CORBA_Environment                                    *ev)
{
	EMessengerListener                *listener = E_MESSENGER_LISTENER (bonobo_object_from_servant (servant));
	GNOME_Evolution_Messenger_Backend  backend_copy;
	EMessengerIdentity                *id;
	char                              *id_string;

	backend_copy = bonobo_object_dup_ref (backend, ev);

	if (ev->_major != CORBA_NO_EXCEPTION) {
		g_warning ("EMessengerListener: Exception while duplicating Backend!\n");
		return;
	}

	id_string = g_strdup (corba_id);
	id        = e_messenger_identity_create_from_string (id_string);
	
	e_messenger_listener_queue_signon_result (
		listener,
		id,
		backend,
		corba_signon_error_to_signon_error (corba_signon_error));
}

static void
impl_Listener_receive_message (PortableServer_Servant                              servant,
			       const GNOME_Evolution_Messenger_Identity            corba_id,
			       const CORBA_char                                   *corba_sender,
			       const CORBA_boolean                                 corba_autoresponse,
			       const CORBA_char                                   *corba_message,
			       CORBA_Environment                                  *ev)
{
	EMessengerListener *listener = E_MESSENGER_LISTENER (bonobo_object_from_servant (servant));
	EMessengerIdentity *id;
	EMessengerIdentity *sender;
	char               *message; 
	
	id = e_messenger_identity_create_from_string ((const char *)corba_id);
	if (id == NULL) {
		g_warning ("The PMS you are using is broken.");
		return;
	}
	
	sender = e_messenger_identity_create_from_me_and_username (id, corba_sender);

	message = g_strdup ((const char *)corba_message);
	
	e_messenger_listener_queue_receive_message (
		listener,
		id,
		sender,
		message,
		(gboolean) corba_autoresponse);
}

static void
impl_Listener_contact_update (PortableServer_Servant                              servant,
			      const GNOME_Evolution_Messenger_Identity            corba_id,
			      const CORBA_char                                   *corba_contact,
			      const CORBA_boolean                                 corba_online,
			      const GNOME_Evolution_Messenger_UserStatus          corba_user_status,
			      CORBA_Environment                                  *ev)
{
	EMessengerListener                *listener = E_MESSENGER_LISTENER (bonobo_object_from_servant (servant));
	EMessengerIdentity                *id;
	EMessengerIdentity                *contact;
	
	id = e_messenger_identity_create_from_string ((const char *)corba_id);
	if (id == NULL) {
		g_warning ("The PMS you are using is broken.");
		return;
	}

	contact = e_messenger_identity_create_from_me_and_username (id, (const char *)corba_contact);
	
	e_messenger_listener_queue_contact_update (
		listener,
		id,
		contact,
		(EMessengerUserStatus) corba_user_status,
		(gboolean) corba_online);
}

/**
 * e_messenger_listener_check_pending:
 * @listener: the #EMessengerListener 
 *
 * Returns: the number of items on the response queue,
 * or -1 if the @listener is isn't an #EMessengerListener.
 */
int
e_messenger_listener_check_pending (EMessengerListener *listener)
{
	g_return_val_if_fail (listener != NULL,              -1);
	g_return_val_if_fail (E_IS_MESSENGER_LISTENER (listener), -1);

	return g_list_length (listener->priv->message_queue);
}

/**
 * e_messenger_listener_pop_message:
 * @listener: the #EMessengerListener for which a request is to be popped
 *
 * Returns: an #EMessengerListenerMessage if there are messages on the
 * queue to be returned; %NULL if there aren't, or if the @listener
 * isn't an EMessengerListener.
 */
EMessengerListenerMessage *
e_messenger_listener_pop_message (EMessengerListener *listener)
{
	EMessengerListenerMessage *msg;
	GList                     *popped;

	g_return_val_if_fail (listener != NULL,                   NULL);
	g_return_val_if_fail (E_IS_MESSENGER_LISTENER (listener), NULL);

	if (listener->priv->message_queue == NULL)
		return NULL;

	msg = listener->priv->message_queue->data;

	popped = listener->priv->message_queue;
	listener->priv->message_queue =
		g_list_remove_link (listener->priv->message_queue,
				    listener->priv->message_queue);
	g_list_free_1 (popped);

	return msg;
}

static EMessengerListener *
e_messenger_listener_construct (EMessengerListener *listener)
{
	POA_GNOME_Evolution_Messenger_Listener *servant;
	CORBA_Environment                       ev;
	CORBA_Object                            obj;

	g_assert (listener != NULL);
	g_assert (E_IS_MESSENGER_LISTENER (listener));

	servant       = (POA_GNOME_Evolution_Messenger_Listener *) g_new0 (BonoboObjectServant, 1);
	servant->vepv = &e_messenger_listener_vepv;

	CORBA_exception_init (&ev);

	POA_GNOME_Evolution_Messenger_Listener__init ((PortableServer_Servant) servant, &ev);
	if (ev._major != CORBA_NO_EXCEPTION) {
		g_free (servant);
		CORBA_exception_free (&ev);

		return NULL;
	}

	CORBA_exception_free (&ev);

	obj = bonobo_object_activate_servant (BONOBO_OBJECT (listener), servant);
	if (obj == CORBA_OBJECT_NIL) {
		g_free (servant);

		return NULL;
	}

	bonobo_object_construct (BONOBO_OBJECT (listener), obj);

	return listener;
}

/**
 * e_messenger_listener_new:
 * @messenger: the #EMessenger for which the listener is to be bound
 *
 * Creates and returns a new #EMessengerListener for the messenger.
 *
 * Returns: a new #EMessengerListener
 */
EMessengerListener *
e_messenger_listener_new ()
{
	EMessengerListener *listener;
	EMessengerListener *retval;

	listener = gtk_type_new (E_MESSENGER_LISTENER_TYPE);

	retval = e_messenger_listener_construct (listener);

	if (retval == NULL) {
		g_warning ("e_messenger_listener_new: Error constructing "
			   "EMessengerListener!\n");
		bonobo_object_unref (BONOBO_OBJECT (listener));
		return NULL;
	}

	return retval;
}

static void
e_messenger_listener_init (EMessengerListener *listener)
{
	listener->priv = g_new0 (EMessengerListenerPrivate, 1);
}

void
e_messenger_listener_stop (EMessengerListener *listener)
{
	g_return_if_fail (E_IS_MESSENGER_LISTENER (listener));

	listener->priv->stopped = TRUE;
}

static void
e_messenger_listener_destroy (GtkObject *object)
{
	EMessengerListener *listener = E_MESSENGER_LISTENER (object);
	GList *l;

	/*
	 * Remove our message queue handler: In theory, this can
	 * never happen since we always hold a reference to the
	 * listener while the timeout is running.
	*/
	if (listener->priv->timeout_id) {
		g_source_remove (listener->priv->timeout_id);
	}

	/* Clean up anything still sitting in message_queue */
	for (l = listener->priv->message_queue; l != NULL; l = l->next) {
		EMessengerListenerMessage *msg = l->data;

		message_free (msg);
	}
	g_list_free (listener->priv->message_queue);

	g_free (listener->priv);
	
	GTK_OBJECT_CLASS (e_messenger_listener_parent_class)->destroy (object);
}

POA_GNOME_Evolution_Messenger_Listener__epv *
e_messenger_listener_get_epv (void)
{
	POA_GNOME_Evolution_Messenger_Listener__epv *epv;

	epv                 = g_new0 (POA_GNOME_Evolution_Messenger_Listener__epv, 1);

	epv->signonResult   = impl_Listener_signon_result;
	epv->receiveMessage = impl_Listener_receive_message;
	epv->contactUpdate  = impl_Listener_contact_update;

	return epv;
}

static void
e_messenger_listener_corba_class_init (void)
{
	e_messenger_listener_vepv.Bonobo_Unknown_epv              = bonobo_object_get_epv ();
	e_messenger_listener_vepv.GNOME_Evolution_Messenger_Listener_epv = e_messenger_listener_get_epv ();
}

static void
e_messenger_listener_class_init (EMessengerListenerClass *klass)
{
	GtkObjectClass *object_class = (GtkObjectClass *) klass;

	e_messenger_listener_parent_class = gtk_type_class (bonobo_object_get_type ());

	e_messenger_listener_signals [MESSAGES_QUEUED] =
		gtk_signal_new ("messages_queued",
				GTK_RUN_LAST,
				object_class->type,
				GTK_SIGNAL_OFFSET (EMessengerListenerClass, messages_queued),
				gtk_marshal_NONE__NONE,
				GTK_TYPE_NONE, 0);

	gtk_object_class_add_signals (object_class, e_messenger_listener_signals, LAST_SIGNAL);

	object_class->destroy = e_messenger_listener_destroy;

	e_messenger_listener_corba_class_init ();
}

/**
 * e_messenger_listener_get_type:
 */
GtkType
e_messenger_listener_get_type (void)
{
	static GtkType type = 0;

	if (! type) {
		GtkTypeInfo info = {
			"EMessengerListener",
			sizeof (EMessengerListener),
			sizeof (EMessengerListenerClass),
			(GtkClassInitFunc)  e_messenger_listener_class_init,
			(GtkObjectInitFunc) e_messenger_listener_init,
			NULL, /* reserved 1 */
			NULL, /* reserved 2 */
			(GtkClassInitFunc) NULL
		};

		type = gtk_type_unique (bonobo_object_get_type (), &info);
	}

	return type;
}
