#include <bonobo.h>
#include <gtk/gtkmarshal.h>
#include <gtk/gtksignal.h>

#include "../Messenger.h"
#include "e-messenger.h"
#include "e-messenger-listener.h"

GtkObjectClass *e_messenger_parent_class;

typedef struct _EMessengerConnection EMessengerConnection;

struct _EMessengerPrivate {
	/*
	 * This is a list of EMessengerConnection structures which
	 * represents all the instant messaging services the user is
	 * signed onto.
	 */
	GList *active_connections;

	/*
	 * This is a list of EMessengerConnection structures, one
	 * for each signon operation which is currently underway.
	 */
	GList *pending_connections;

	/*
	 * The listener object we use to receive notifications and
	 * responses from the PMS (Personal Messaging Server).
	 */
	EMessengerListener *listener;

	/*
	 * The CORBA reference to the BackendDispatcher which is used to
	 * create new connections.
	 */
	GNOME_Evolution_Messenger_BackendDispatcher dispatcher;
};

enum {
	RECEIVE_MESSAGE,
	CONTACT_UPDATE,
	LAST_SIGNAL
};

static guint e_messenger_signals [LAST_SIGNAL];

struct _EMessengerConnection {
	EMessengerIdentity               *identity;

	/*
	 * Used for pending connections.
	 */
	EMessengerSignonCallback      callback;
	gpointer                      closure;

	/*
	 * Used only for pending connections, this flag is set if this
	 * connection has been closed by the user, but we have not yet
	 * received an SignonResult response from the PMS.
	 */
	gboolean                      defunct;

	/*
	 * Used only for active connections.
	 */
	GNOME_Evolution_Messenger_Backend  backend;
};

static gboolean
messenger_backend_signon (EMessenger         *messenger,
			  EMessengerIdentity *identity)
{
	CORBA_Environment  ev;
	char              *service_type;
	char              *username;
	char              *password;

	service_type = e_messenger_identity_get_service_type (identity);
	username     = e_messenger_identity_get_username (identity);
	password     = e_messenger_identity_get_password (identity);

	CORBA_exception_init (&ev);

	GNOME_Evolution_Messenger_BackendDispatcher_signon (
		messenger->priv->dispatcher,
		service_type,
		username,
		password,
		BONOBO_OBJREF (messenger->priv->listener),
		&ev);

	if (ev._major != CORBA_NO_EXCEPTION) {
		CORBA_exception_free (&ev);

		/* FIXME: Check the exception type. */
		g_warning ("Exception signing on with BackendDispatcher.");

		return FALSE;
	}

	CORBA_exception_free (&ev);

	return TRUE;
}

static gboolean
messenger_backend_signoff (EMessenger           *messenger,
			   EMessengerConnection *connection)
{
	CORBA_Environment  ev;
	char              *signon_string;

	signon_string = e_messenger_identity_to_string (connection->identity);
	g_return_val_if_fail (signon_string != NULL, FALSE);

	CORBA_exception_init (&ev);

	GNOME_Evolution_Messenger_Backend_signoff (connection->backend, signon_string, &ev);

	g_free (signon_string);

	if (ev._major != CORBA_NO_EXCEPTION) {
		CORBA_exception_free (&ev);

		/* FIXME: Check the exception type. */
		g_warning ("Exception signing off with the PMS.");


		return FALSE;
	}

	CORBA_exception_free (&ev);

	return TRUE;
}

static EMessengerConnection *
messenger_connection_create (EMessengerIdentity           *id,
				      EMessengerSignonCallback  callback,
				      gpointer                      closure)
{
	EMessengerConnection *connection;

	connection = g_new0 (EMessengerConnection, 1);
	connection->identity = id;
	connection->callback = callback;
	connection->closure  = closure;

	return connection;
}

static void
messenger_connection_free (EMessengerConnection *connection)
{

	e_messenger_identity_free (connection->identity);

	if (connection->backend != CORBA_OBJECT_NIL) {
		CORBA_Environment ev;

		CORBA_exception_init (&ev);

		bonobo_object_release_unref (connection->backend, &ev);

		if (BONOBO_EX (&ev)) {
			g_warning ("Could not release IdentityConnection CORBA backend reference.");
		}

		CORBA_exception_free (&ev);
	}

	g_free (connection);
}

static void
messenger_add_active_connection (EMessenger           *messenger,
				 EMessengerConnection *connection)
{
	messenger->priv->active_connections = g_list_prepend (
		messenger->priv->active_connections, connection);
}

/**
 * messenger_remove_active_connection (INTERNAL):
 * @messenger: The #EMessenger object which currently has this
 * connection listed among its active connections.
 * @connection: The signon to be removed.
 *
 * This function is called after the PMS has been notified to signoff
 * this connection.
 */
static void
messenger_remove_active_connection (EMessenger           *messenger,
				    EMessengerConnection *connection)
{
	messenger->priv->active_connections =
		g_list_remove (messenger->priv->active_connections, connection);
}

static void
messenger_remove_pending_connection (EMessenger           *messenger,
				     EMessengerConnection *connection)
{
	messenger->priv->pending_connections =
		g_list_remove (messenger->priv->pending_connections, connection);
}

static void
messenger_add_pending_connection (EMessenger           *messenger,
				  EMessengerConnection *connection)
{
	messenger->priv->pending_connections = g_list_prepend (
		messenger->priv->pending_connections, connection);
}

static EMessengerConnection *
messenger_find_pending_connection (EMessenger         *messenger,
				   EMessengerIdentity *identity)
{
	GList *l;

	for (l = messenger->priv->pending_connections; l != NULL; l = l->next) {
		EMessengerConnection *connection = l->data;

		if (e_messenger_identity_is_equal (identity, connection->identity))
			return connection;
	}

	return NULL;
}

static EMessengerConnection *
messenger_find_active_connection (EMessenger         *messenger,
				  EMessengerIdentity *identity)
{
	GList *l;

	for (l = messenger->priv->active_connections; l != NULL; l = l->next) {
		EMessengerConnection *connection = l->data;

		if (e_messenger_identity_is_equal (identity, connection->identity))
			return connection;
	}

	return NULL;
}

static gboolean
messenger_identity_is_active (EMessenger         *messenger,
			      EMessengerIdentity *identity)
{
	EMessengerConnection *conn;

	conn = messenger_find_active_connection (messenger, identity);

	if (conn == NULL)
		return FALSE;

	return TRUE;
}

static gboolean
messenger_identity_is_pending (EMessenger         *messenger,
			       EMessengerIdentity *identity)
{
	EMessengerConnection *conn;

	conn = messenger_find_pending_connection (messenger, identity);

	if (conn == NULL)
		return FALSE;

	return TRUE;
}

static void
messenger_signoff_defunct_connection (EMessenger           *messenger,
				      EMessengerConnection *connection)
{
	messenger_backend_signoff (messenger, connection);

	messenger_remove_pending_connection (messenger, connection);
	messenger_connection_free (connection);
}

static void
messenger_handle_message_signon_response (EMessenger                *messenger,
					  EMessengerListenerMessage *message)
{
	GList *l, *match;

	match = NULL;

	for (l = messenger->priv->pending_connections; l != NULL; l = l->next) {
		EMessengerConnection *connection = l->data;

		g_assert (connection != NULL);

		if (e_messenger_identity_is_equal (connection->identity, message->id)) {
			match = l;

			if (connection->defunct) {
				messenger_signoff_defunct_connection (messenger, connection);
				break;
			}
		
			connection->callback (
				messenger,
				connection->identity,
				message->signon_error,
				connection->closure);

			messenger_remove_pending_connection (messenger, connection);

			if (message->signon_error == E_MESSENGER_SIGNON_ERROR_NONE) {
				messenger_add_active_connection (messenger, connection);
			} else {
				messenger_connection_free (connection);
			}

			break;
		}
	}

	e_messenger_identity_free (message->id);

	if (match == NULL)
		g_error ("Connection response for unknown connection received from PMS!");
}


static void
messenger_handle_receive_message (EMessenger                *messenger,
				  EMessengerListenerMessage *message)
{

	gtk_signal_emit (GTK_OBJECT (messenger),
			 e_messenger_signals [RECEIVE_MESSAGE],
			 message->id,
			 message->who,
			 message->message,
			 message->autoresponse);
	
	e_messenger_identity_free (message->id);
	g_free (message->who);
	g_free (message->message);
}

static void
messenger_handle_contact_update (EMessenger                *messenger,
				 EMessengerListenerMessage *message)
{

	gtk_signal_emit (GTK_OBJECT (messenger),
			 e_messenger_signals [CONTACT_UPDATE],
			 message->id,
			 message->who,
			 message->online,
			 message->user_status);
	
	e_messenger_identity_free (message->id);
	g_free (message->who);
}

static void
messenger_check_listener_queue (EMessengerListener *listener,
				EMessenger         *messenger)
{
	EMessengerListenerMessage *msg;

	msg = e_messenger_listener_pop_message (listener);

	if (msg == NULL)
		return;

	switch (msg->type) {
	case SignonResponse:
		messenger_handle_message_signon_response (messenger, msg);
		break;
	case ReceiveMessageEvent:
		messenger_handle_receive_message (messenger, msg);
	case ContactUpdateEvent:
		messenger_handle_contact_update (messenger, msg);
	default:
		g_warning ("Unknown message type from MessengerListener.");
		return;
	}

	g_free (msg);
}

static gboolean
messenger_activate_dispatcher (EMessenger *messenger)
{
	Bonobo_Unknown                               corba_object;
	GNOME_Evolution_Messenger_BackendDispatcher  dispatcher;
	CORBA_Environment        		     ev;

	/*
	 * Check to see if the dispatcher has already been activated.
	 */
	if (messenger->priv->dispatcher != CORBA_OBJECT_NIL)
		return TRUE;

	CORBA_exception_init (&ev);

	corba_object = oaf_activate_from_id ("OAFIID:GNOME_Evolution_Messenger_BackendDispatcher", 0, NULL, &ev);

	if (BONOBO_EX (&ev)) {
		g_warning ("Exception performing OAF query for Evolution Personal Messaging Server.");
		CORBA_exception_free (&ev);
		return FALSE;
	}

	dispatcher = Bonobo_Unknown_queryInterface (
		corba_object, 
		"IDL:GNOME/Evolution/Messenger/BackendDispatcher:1.0", &ev);

	if (BONOBO_EX (&ev)) {
		g_warning ("Exception querying for the BackendDispatcher interface.");
		CORBA_exception_free (&ev);
		return FALSE;
	}
	

	CORBA_exception_free (&ev);

	if (dispatcher == CORBA_OBJECT_NIL) {
		g_warning ("Got NIL CORBA object querying for BackendDispatcher interface.");
		return FALSE;
	}
	
	messenger->priv->dispatcher = dispatcher;

	return TRUE;
}

/**
 * e_messenger_signon:
 *
 * FIXME
 *
 * Note about how a copy is created.
 * Note about how signon callback may be passed a copy.
 */
gboolean
e_messenger_signon (EMessenger               *messenger,
		    EMessengerIdentity       *identity,
		    EMessengerSignonCallback  signon_callback,
		    gpointer                  closure)
{
	EMessengerConnection *connection;
	EMessengerIdentity   *id;

	g_return_val_if_fail (messenger != NULL,                                FALSE);
	g_return_val_if_fail (E_IS_MESSENGER (messenger),                       FALSE);
	g_return_val_if_fail (identity != NULL,                                 FALSE);
	g_return_val_if_fail (e_messenger_identity_check_invariants (identity), FALSE);
	g_return_val_if_fail (signon_callback != NULL,                          FALSE);
	
	if (messenger_identity_is_active (messenger, identity)) {
		g_warning ("Attempt to signon with already active identity.");
		return FALSE;
	}

	if (messenger_identity_is_pending (messenger, identity)) {
		EMessengerConnection *connection;

		connection = messenger_find_pending_connection (messenger, identity);

		if (connection->defunct) {
			g_warning ("Attempt to signon with defunct pending identity.");
			signon_callback (messenger, identity, E_MESSENGER_SIGNON_ERROR_DEFUNCT_CONNECTION, closure);
		} else
			g_warning ("Attempt to signon with pending identity.");

		return FALSE;
	}

	/*
	 * Activate the dispatcher, if it is not already active.
	 */
	if (! messenger_activate_dispatcher (messenger)) {
		signon_callback (messenger, identity, E_MESSENGER_SIGNON_ERROR_PMS_FAILURE, closure);
		return FALSE;
	}

	/*
	 * Create our listener interface if it doesn't already exist.
	 * It will already exist in the case where this EMessenger has
	 * been used to sign on to an IM service already.
	 */
	if (messenger->priv->listener == NULL) {
		messenger->priv->listener = e_messenger_listener_new ();

		if (messenger->priv->listener == NULL) {
			g_warning ("Could not create EMessengerListener!\n");
			return FALSE;
		}

		if (! gtk_signal_connect (GTK_OBJECT (messenger->priv->listener),
					  "messages_queued",
					  messenger_check_listener_queue, messenger)) {
			g_warning ("Could not connect to EMessengerListener messages_queued signal.");
			return FALSE;
		}
	}

	id = e_messenger_identity_copy (identity);

	/*
	 * Ask the BackendDispatcher to perform the signon.
	 */
	if (! messenger_backend_signon (messenger, id))
		return FALSE;

	/*
	 * Add this connection to the list of pending connections.
	 */
	connection = messenger_connection_create (id, signon_callback, closure);
	messenger_add_pending_connection (messenger, connection);

	return TRUE;
}

static gboolean
messenger_signoff_active_connection (EMessenger         *messenger,
				     EMessengerIdentity *identity)
{
	
	EMessengerConnection *connection;

	connection = messenger_find_active_connection (messenger, identity);
	g_assert (connection != NULL);

	/*
	 * Ask the Backend to perform the signoff.
	 */
	if (! messenger_backend_signoff (messenger, connection))
		return FALSE;

	/*
	 * FIXME: Remove any ancillary data associated with
	 * this connection (e.g. buddy lists, etc).
	 */

	/*
	 * Remove the connection from the active connection list.
	 */
	messenger_remove_active_connection (messenger, connection);
	messenger_connection_free (connection);

	return TRUE;
}

/**
 * messenger_signoff_pending_connnection (INTERNAL)
 * @messenger: The #EMessenger object.
 * @identity: The identity for the connection which is to be closed.
 *
 * This function is called when the programmer asks for a pending
 * connection to be closed.  Because we have not yet received a signon
 * response for the connection from the PMS, we cannot destroy the
 * connection yet.  Instead, we mark the connection as 'defunct' so
 * that it can be removed later, once the PMS has responded.
 *
 * The application-specified signon callback will not get invoked
 * if the pending connection is closed before it's established.
 *
 * If the user requests to create a new connection for a pending,
 * defunct identity, e_messenger_signon will return FALSE and invoke
 * the user's signon callback with an error of
 * E_MESSENGER_SIGNON_ERROR_DEFUNCT_CONNECTION.  The application must
 * wait until the connection is no longer defunct to attempt to sign
 * on again.
 */
static gboolean
messenger_signoff_pending_connection (EMessenger         *messenger,
				      EMessengerIdentity *identity)
{
	
	EMessengerConnection *connection;

	connection = messenger_find_pending_connection (messenger, identity);
	g_assert (connection != NULL);

	connection->defunct = TRUE;

	return TRUE;
}

/**
 * e_messenger_signoff:
 *
 * FIXME
 */
gboolean
e_messenger_signoff (EMessenger               *messenger,
		     EMessengerIdentity       *identity)
{
	gboolean identity_is_active;

	g_return_val_if_fail (messenger != NULL,                                FALSE);
	g_return_val_if_fail (E_IS_MESSENGER (messenger),                       FALSE);
	g_return_val_if_fail (identity != NULL,                                 FALSE);
	g_return_val_if_fail (e_messenger_identity_check_invariants (identity), FALSE);

	identity_is_active = messenger_identity_is_active (messenger, identity);

	if (! identity_is_active || ! messenger_identity_is_pending (messenger, identity)) {
		g_warning ("Attempt to signoff an ID which isn't signed on or being signed in.");
		return FALSE;
	}

	if (identity_is_active)
		return messenger_signoff_active_connection (messenger, identity);
	else
		return messenger_signoff_pending_connection (messenger, identity);
}

static gboolean
e_messenger_construct (EMessenger *messenger)
{
	g_return_val_if_fail (messenger != NULL,          FALSE);
	g_return_val_if_fail (E_IS_MESSENGER (messenger), FALSE);

	return TRUE;
}

EMessenger *
e_messenger_new (void)
{
	EMessenger *messenger;

	messenger = gtk_type_new (E_MESSENGER_TYPE);

	if (! e_messenger_construct (messenger)) {
		gtk_object_unref (GTK_OBJECT (messenger));
		return NULL;
	}

	return messenger;
}

static void
e_messenger_init (EMessenger *messenger)
{
	messenger->priv = g_new0 (EMessengerPrivate, 1);

	/*
	 * FIXME: Init priv.
	 */
}

static void
e_messenger_destroy (GtkObject *object)
{
	EMessenger *messenger = E_MESSENGER (object);

	g_free (messenger->priv);

	GTK_OBJECT_CLASS (e_messenger_parent_class)->destroy (object);
}

/*
 * Custom marshalers.
 */
typedef void (*GtkSignal_NONE__POINTER_POINTER_POINTER_BOOL) (GtkObject *object, 
gpointer arg1,
gpointer arg2,
gpointer arg3,
gboolean arg4,
gpointer user_data);
void gtk_marshal_NONE__POINTER_POINTER_POINTER_BOOL (GtkObject    *object,
						     GtkSignalFunc func,
						     gpointer      func_data,
						     GtkArg       *args)
{
	GtkSignal_NONE__POINTER_POINTER_POINTER_BOOL rfunc;
	rfunc = (GtkSignal_NONE__POINTER_POINTER_POINTER_BOOL) func;

	(* rfunc) (object,
		   GTK_VALUE_POINTER(args[0]),
		   GTK_VALUE_POINTER(args[1]),
		   GTK_VALUE_POINTER(args[2]),
		   GTK_VALUE_BOOL(args[3]),
		   func_data);
}

typedef void (*GtkSignal_NONE__POINTER_POINTER_BOOL_INT) (GtkObject *object,
							  gpointer arg1,
							  gpointer arg2,
							  gboolean arg3,
							  gint     arg4,
							  gpointer user_data);

void gtk_marshal_NONE__POINTER_POINTER_BOOL_INT (GtkObject    *object,
						 GtkSignalFunc func,
						 gpointer      func_data,
						 GtkArg       *args)
{
	GtkSignal_NONE__POINTER_POINTER_BOOL_INT rfunc;
	rfunc = (GtkSignal_NONE__POINTER_POINTER_BOOL_INT) func;

	(* rfunc) (object,
		   GTK_VALUE_POINTER(args[0]),
		   GTK_VALUE_POINTER(args[1]),
		   GTK_VALUE_BOOL(args[2]),
		   GTK_VALUE_INT(args[3]),
		   func_data);
}

static void
e_messenger_class_init (EMessengerClass *klass)
{
	GtkObjectClass *object_class = (GtkObjectClass *) klass;

	e_messenger_parent_class = gtk_type_class (gtk_object_get_type ());

	e_messenger_signals [RECEIVE_MESSAGE] =
		gtk_signal_new ("receive_message",
				GTK_RUN_LAST,
				object_class->type,
				GTK_SIGNAL_OFFSET (EMessengerClass, receive_message),
				gtk_marshal_NONE__POINTER_POINTER_POINTER_BOOL,
				GTK_TYPE_NONE, 4,
				GTK_TYPE_POINTER,
				GTK_TYPE_POINTER,
				GTK_TYPE_POINTER,
				GTK_TYPE_BOOL);

	e_messenger_signals [CONTACT_UPDATE] =
		gtk_signal_new ("contact_update",
				GTK_RUN_LAST,
				object_class->type,
				GTK_SIGNAL_OFFSET (EMessengerClass, receive_message),
				gtk_marshal_NONE__POINTER_POINTER_BOOL_INT,
				GTK_TYPE_NONE, 4,
				GTK_TYPE_POINTER,
				GTK_TYPE_POINTER,
				GTK_TYPE_BOOL,
				GTK_TYPE_INT);

	gtk_object_class_add_signals (object_class, e_messenger_signals, LAST_SIGNAL);

	object_class->destroy = e_messenger_destroy;
}

/**
 * e_messenger_get_type: */
GtkType
e_messenger_get_type (void)
{
	static GtkType type = 0;

	if (! type) {
		GtkTypeInfo info = {
			"EMessenger",
			sizeof (EMessenger),
			sizeof (EMessengerClass),
			(GtkClassInitFunc)  e_messenger_class_init,
			(GtkObjectInitFunc) e_messenger_init,
			NULL, /* reserved 1 */
			NULL, /* reserved 2 */
			(GtkClassInitFunc) NULL
		};

		type = gtk_type_unique (gtk_object_get_type (), &info);
	}

	return type;
}
