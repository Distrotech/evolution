#include <bonobo.h>
#include <gtk/gtkmarshal.h>
#include <gtk/gtksignal.h>

#include <Messenger.h>
#include <e-messenger.h>
#include <e-messenger-listener.h>

GtkObjectClass *e_messenger_parent_class;

typedef struct _EMessengerIdentitySignon EMessengerIdentitySignon;

struct _EMessengerPrivate {
	/*
	 * This is a list of EMessengerIdentitySignon structures which
	 * represents all the instant messaging services the user is
	 * signed onto.
	 */
	GList *active_signons;

	/*
	 * This is a list of EMessengerIdentitySignon structures, one
	 * for each signon operation which is currently underway.
	 */
	GList *pending_signons;

	/*
	 * The listener object we use to receive notifications and
	 * responses from the PMS (Personal Messaging Server).
	 */
	EMessengerListener *listener;

	/*
	 * The CORBA reference to the BackendDispatcher which is used to
	 * create new signons.
	 */
	GNOME_Evolution_Messenger_BackendDispatcher dispatcher;
};

enum {
	RECEIVE_MESSAGE,
	CONTACT_UPDATE,
	LAST_SIGNAL
};

static guint e_messenger_signals [LAST_SIGNAL];

struct _EMessengerIdentitySignon {
	EMessengerIdentity               *identity;

	/*
	 * Used for pending signons.
	 */
	EMessengerSignonCallback          callback;
	gpointer                          closure;

	/*
	 * Used only for active signons.
	 */
	GNOME_Evolution_Messenger_Backend  corba_backend;
};

static EMessengerIdentitySignon *
messenger_identity_signon_create (EMessengerIdentity       *id,
				  EMessengerSignonCallback  callback,
				  gpointer                  closure)
{
	EMessengerIdentitySignon *emis;

	emis = g_new0 (EMessengerIdentitySignon, 1);
	emis->identity = id;
	emis->callback = callback;
	emis->closure  = closure;

	return emis;
}

static void
messenger_identity_signon_free (EMessengerIdentitySignon *signon)
{

	e_messenger_identity_free (signon->identity);

	if (signon->corba_backend != CORBA_OBJECT_NIL) {
		CORBA_Environment ev;

		CORBA_exception_init (&ev);

		bonobo_object_release_unref (signon->corba_backend, &ev);

		if (BONOBO_EX (&ev)) {
			g_warning ("Could not release IdentitySignon CORBA backend reference.");
		}

		CORBA_exception_free (&ev);
	}

	g_free (signon);
}

static void
messenger_add_active_signon (EMessenger               *messenger,
			     EMessengerIdentitySignon *signon)
{
	messenger->priv->active_signons = g_list_prepend (
		messenger->priv->active_signons, signon);
}

static void
messenger_add_pending_signon (EMessenger               *messenger,
			      EMessengerIdentitySignon *signon)
{
	messenger->priv->pending_signons = g_list_prepend (
		messenger->priv->pending_signons, signon);
}

static gboolean
messenger_identity_is_active (EMessenger         *messenger,
			      EMessengerIdentity *identity)
{
	GList *l;

	for (l = messenger->priv->active_signons; l != NULL; l = l->next) {
		EMessengerIdentitySignon *emis = l->data;

		if (e_messenger_identity_is_equal (identity, emis->identity))
			return TRUE;
	}

	return FALSE;
}

static gboolean
messenger_identity_is_pending (EMessenger         *messenger,
			       EMessengerIdentity *identity)
{
	GList *l;

	for (l = messenger->priv->pending_signons; l != NULL; l = l->next) {
		EMessengerIdentitySignon *emis = l->data;

		if (e_messenger_identity_is_equal (identity, emis->identity))
			return TRUE;
	}

	return FALSE;
}

static void
messenger_handle_message_signon_response (EMessenger                *messenger,
					  EMessengerListenerMessage *message)
{
	GList *l, *match;

	match = NULL;

	for (l = messenger->priv->pending_signons; l != NULL; l = l->next) {
		EMessengerIdentitySignon *emis = l->data;

		g_assert (emis != NULL);
		
		if (e_messenger_identity_is_equal (emis->identity, message->id)) {
			match = l;

			emis->callback (messenger, (const EMessengerIdentity *) emis->identity, message->signon_error, emis->closure);

			if (message->signon_error == E_MESSENGER_SIGNON_ERROR_NONE) {
				messenger_add_active_signon (messenger, emis);
			} else {
				messenger_identity_signon_free (emis);
			}
			
			break;
		}
	}

	e_messenger_identity_free (message->id);

	if (match != NULL) {
		l = g_list_remove (l, match->data);
	} else {
		g_error ("Signon response for unknown signon received from PMS!");
	}
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
	GNOME_Evolution_Messenger_BackendDispatcher  dispatcher;
	OAF_ServerInfoList       		    *info_list;
	const OAF_ServerInfo     		    *info;
	char                     		    *query;
	CORBA_Environment        		     ev;

	/*
	 * Check to see if the dispatcher has already been activated.  */
	if (messenger->priv->dispatcher != CORBA_OBJECT_NIL)
		return TRUE;

	CORBA_exception_init (&ev);

	/*
	 * Query for the PMS.
	 */
	query = g_strdup_printf ("repo_ids.has ('IDL:GNOME/Evolution/IM/BackendDispatcher:1.0')");

	info_list = oaf_query (query, NULL, &ev);

	g_free (query);

	if (ev._major != CORBA_NO_EXCEPTION) {
		g_warning ("Failed performing OAF query for Evolution Personal Messaging Server.");
		return FALSE;
	}

	if (info_list->_length == 0) {
		g_warning ("Could not find Evolution Personal Messaging Server installed.");
		CORBA_exception_free (&ev);
		return FALSE;
	}

	CORBA_exception_free (&ev);

	if (info_list->_length > 1) {
		g_warning ("Found multiple Evolution Personal Messaging Servers in OAF query; "
			   "using the first one in the list.");
	}

	/*
	 * Now activate it.
	 */
	info = info_list->_buffer;

	dispatcher = oaf_activate_from_id (info->iid, 0, NULL, NULL);
	
	if (dispatcher == CORBA_OBJECT_NIL) {
		g_warning ("Could not activate Evolution Personal Messaging Server with "
			   "OAF IID: %s\n", info->iid);
	} else {
		messenger->priv->dispatcher = dispatcher;
	}

	CORBA_free (info_list);

	if (dispatcher == CORBA_OBJECT_NIL)
		return FALSE;

	return TRUE;
}

static gboolean
messenger_signon (EMessenger         *messenger,
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

gboolean
e_messenger_signon (EMessenger               *messenger,
		    EMessengerIdentity       *identity,
		    EMessengerSignonCallback  signon_callback,
		    gpointer                  closure)
{
	EMessengerIdentitySignon *emis;

	g_return_val_if_fail (messenger != NULL,          FALSE);
	g_return_val_if_fail (E_IS_MESSENGER (messenger), FALSE);
	g_return_val_if_fail (identity != NULL,           FALSE);
	g_return_val_if_fail (signon_callback != NULL,    FALSE);
	
	if (messenger_identity_is_active (messenger, identity)) {
		g_warning ("Attempt to signon with already active identity.");
		return FALSE;
	}

	if (messenger_identity_is_pending (messenger, identity)) {
		g_warning ("Attempt to signon with pending identity.");
		return FALSE;
	}

	/*
	 * Activate the dispatcher, if it is not already active.
	 */
	if (! messenger_activate_dispatcher (messenger)) {
		signon_callback (messenger, identity, E_MESSENGER_SIGNON_ERROR_UNKNOWN_FAILURE, closure);
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

		gtk_signal_connect (GTK_OBJECT (messenger->priv->listener),
				    "message_queued",
				    messenger_check_listener_queue, messenger);
	}

	/*
	 * Ask the BackendDispatcher to perform the signon.
	 */
	if (! messenger_signon (messenger, identity))
		return FALSE;

	/*
	 * Add this signon to the list of pending signons.
	 */
	emis = messenger_identity_signon_create (
		identity, signon_callback, closure);
	messenger_add_pending_signon (messenger, emis);

	return TRUE;
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
