#include <bonobo.h>
#include "Messenger.h"
#include "e-messenger-backend.h"

#define PARENT_TYPE BONOBO_X_OBJECT_TYPE

static BonoboXObjectClass *parent_class = NULL;

static void
e_messenger_backend_destroy(GtkObject *obj)
{
	(* GTK_OBJECT_CLASS(parent_class)->destroy)(obj);
} /* e_messenger_backend_destroy */

static void
impl_MessengerBackend_signoff(PortableServer_Servant servant, const char *id,
			      CORBA_Environment *ev)
{
	EMessengerBackend *backend;
	EMessengerBackendClass *klass;

	backend = E_MESSENGER_BACKEND(bonobo_object_from_servant(servant));
	klass = E_MESSENGER_BACKEND_GET_CLASS(backend);

	(klass->signoff)(backend, id);
} /* impl_MessengerBackend_signoff */

static void
impl_MessengerBackend_signoff_all(PortableServer_Servant servant,
				  CORBA_Environment *ev)
{
	EMessengerBackend *backend;
	EMessengerBackendClass *klass;
	GSList *signons;

	backend = E_MESSENGER_BACKEND(bonobo_object_from_servant(servant));
	klass = E_MESSENGER_BACKEND_GET_CLASS(backend);

	signons = (klass->get_signon_list)(backend);
	while (signons) {
		(klass->signoff)(backend, (char *) signons->data);

		signons = signons->next;
	}
} /* impl_MessengerBackend_signoff_all */

static void
impl_MessengerBackend_change_status(
	PortableServer_Servant servant,
	const char *id,
	const GNOME_Evolution_Messenger_UserStatus status,
	const CORBA_char *data,
	CORBA_Environment *ev)
{
	EMessengerBackend *backend;
	EMessengerBackendClass *klass;

	backend = E_MESSENGER_BACKEND(bonobo_object_from_servant(servant));
	klass = E_MESSENGER_BACKEND_GET_CLASS(backend);

	(klass->change_status)(backend, id, status, data);
} /* impl_MessengerBackend_change_status */

static void
impl_MessengerBackend_send_message(PortableServer_Servant servant,
				    const char *id, const CORBA_char *contact, 
				    const CORBA_char *message,
				    CORBA_Environment *ev)
{
	EMessengerBackend *backend;
	EMessengerBackendClass *klass;

	backend = E_MESSENGER_BACKEND(bonobo_object_from_servant(servant));
	klass = E_MESSENGER_BACKEND_GET_CLASS(backend);

	(klass->send_message)(backend, id, contact, message);
} /* impl_MessengerBackend_send_message */

static void
impl_MessengerBackend_contact_info(PortableServer_Servant servant,
				   const char *id, const char *contact,
				   CORBA_Environment *ev)
{
	EMessengerBackend *backend;
	EMessengerBackendClass *klass;

	backend = E_MESSENGER_BACKEND(bonobo_object_from_servant(servant));
	klass = E_MESSENGER_BACKEND_GET_CLASS(backend);

	(klass->contact_info)(backend, id, contact);
} /* impl_MessengerBackend_contact_info */

static void
impl_MessengerBackend_add_contact(PortableServer_Servant servant,
				  const char *id, const char *contact,
				  CORBA_Environment *ev)
{
	EMessengerBackend *backend;
	EMessengerBackendClass *klass;

	backend = E_MESSENGER_BACKEND(bonobo_object_from_servant(servant));
	klass = E_MESSENGER_BACKEND_GET_CLASS(backend);

	(klass->add_contact)(backend, id, contact);
} /* impl_MessengerBackend_add_contact */

static void
impl_MessengerBackend_remove_contact(PortableServer_Servant servant,
				     const char *id, const char *contact,
				     CORBA_Environment *ev)
{
	EMessengerBackend *backend;
	EMessengerBackendClass *klass;

	backend = E_MESSENGER_BACKEND(bonobo_object_from_servant(servant));
	klass = E_MESSENGER_BACKEND_GET_CLASS(backend);

	(klass->remove_contact)(backend, id, contact);
} /* impl_MessengerBackend_remove_contact */

static void
impl_MessengerBackend_keepalive(PortableServer_Servant servant, const char *id,
				CORBA_Environment *ev)
{
	EMessengerBackend *backend;
	EMessengerBackendClass *klass;

	backend = E_MESSENGER_BACKEND(bonobo_object_from_servant(servant));
	klass = E_MESSENGER_BACKEND_GET_CLASS(backend);
	
	(klass->keepalive)(backend, id);
} /* impl_MessengerBackend_keepalive */

static void
e_messenger_backend_class_init(EMessengerBackendClass *klass)
{
	GtkObjectClass *object_class = GTK_OBJECT_CLASS(klass);
	POA_GNOME_Evolution_Messenger_Backend__epv *epv;

	object_class->destroy = e_messenger_backend_destroy;

	parent_class = gtk_type_class(bonobo_object_get_type());

	/* Do signals here */

	/* Initialize CORBA class functions */
	epv = &klass->epv;
	epv->signoff            = impl_MessengerBackend_signoff;
	epv->signoffAll         = impl_MessengerBackend_signoff_all;
	epv->changeStatus       = impl_MessengerBackend_change_status;
	epv->sendMessage        = impl_MessengerBackend_send_message;
	epv->contactInfo        = impl_MessengerBackend_contact_info;
	epv->addContact         = impl_MessengerBackend_add_contact;
	epv->removeContact      = impl_MessengerBackend_remove_contact;
	epv->keepalive          = impl_MessengerBackend_keepalive;
} /* e_messenger_backend_class_init */

static void
e_messenger_backend_init(EMessengerBackend *backend)
{
	backend->listener = CORBA_OBJECT_NIL;
} /* e_messenger_backend_init */

BONOBO_X_TYPE_FUNC_FULL(EMessengerBackend, GNOME_Evolution_Messenger_Backend,
			PARENT_TYPE, e_messenger_backend);

EMessengerBackend *
e_messenger_backend_new(void)
{
	EMessengerBackend *backend;

	backend = gtk_type_new(E_MESSENGER_BACKEND_TYPE);

	return backend;
} /* e_messenger_backend_new */
	
void
e_messenger_backend_event_receive_message(EMessengerBackend *backend, 
					  char *signon, char *contact, 
					  gboolean autoresponse, char *message)
{
	CORBA_Environment ev;
	char *identity;

	CORBA_exception_init(&ev);

	identity = g_strdup_printf("%s@%s", signon, backend->service_name);

	GNOME_Evolution_Messenger_Listener_receiveMessage(
		backend->listener, CORBA_string_dup(identity), contact,
		autoresponse, message, &ev);

	g_free(identity);

	if (BONOBO_EX(&ev)) {
		g_warning("Unable to notify listeners of received message");
	}

	CORBA_exception_free(&ev);

	printf("Message to %s@%s from %s: %s\n", signon,
	       backend->service_name, contact, message);
} /* e_messenger_backend_event_receive_message */

void
e_messenger_backend_event_user_info(EMessengerBackend *backend, char *signon, 
				    char *info)
{
	CORBA_Environment ev;
	char *identity;

	CORBA_exception_init(&ev);

	identity = g_strdup_printf("%s@%s", signon, backend->service_name);

	GNOME_Evolution_Messenger_Listener_contactInfo(
		backend->listener, CORBA_string_dup(identity), info, &ev);
	
	g_free(identity);

	if (BONOBO_EX(&ev)) {
		g_warning("Unable to notify listeners of event");
	}

	printf("User info for %s@%s requested: %s\n", signon,
	       backend->service_name, info);
} /* e_messenger_backend_event_user_info */

void
e_messenger_backend_event_user_update(
	EMessengerBackend *backend,
	char *signon, char *contact,
	gboolean online,
	GNOME_Evolution_Messenger_UserStatus status)
{
	CORBA_Environment ev;
	char *identity;

	CORBA_exception_init(&ev);

	identity = g_strdup_printf("%s@%s", signon, backend->service_name);

	GNOME_Evolution_Messenger_Listener_contactUpdate(
		backend->listener, CORBA_string_dup(identity), contact,
		online, status, &ev);
	
	g_free(identity);

	if (BONOBO_EX(&ev)) {
		g_warning("Unable to notify listeners of user update");
	}

	printf("User update for %s@%s: %s is %s\n", signon,
	       backend->service_name, contact, 
	       online ? "online" : "offline");
} /* e_messenger_backend_event_user_update */	
