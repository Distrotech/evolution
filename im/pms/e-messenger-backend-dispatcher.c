#include <bonobo.h>
#include <Messenger.h>
#include "e-messenger-backend.h"
#include "e-messenger-backend-dispatcher.h"

#define PARENT_TYPE BONOBO_X_OBJECT_TYPE

static BonoboXObjectClass *parent_class = NULL;

static void
e_messenger_backend_dispatcher_destroy(GtkObject *obj)
{
	(* GTK_OBJECT_CLASS(parent_class)->destroy)(obj);
} /* e_messenger_backend_dispatcher_destroy */

static void
impl_MessengerBackendDispatcher_signon(
	PortableServer_Servant servant,
	const CORBA_char *service_name,
	const CORBA_char *signon,
	const CORBA_char *password,
	GNOME_Evolution_Messenger_Listener listener,
	CORBA_Environment *ev)
{
	EMessengerBackendDispatcher *dispatcher;
	GSList *i;
	char *identity;

	dispatcher = E_MESSENGER_BACKEND_DISPATCHER(
		bonobo_object_from_servant(servant));

	for (i = dispatcher->activated_backends; i; i = i->next) {
		EMessengerBackend *backend = i->data;
		EMessengerBackendClass *klass;
		EMessengerBackendError err;

		klass = E_MESSENGER_BACKEND_GET_CLASS(backend);

		if (g_strcasecmp(backend->service_name, service_name) == 0) {
			identity = g_strdup_printf(
				"%s@%s", signon, backend->service_name);
	
			err = (klass->signon)(
				backend, signon, password, listener);

			switch (err) {
			case E_MESSENGER_BACKEND_ERROR_NONE:
				GNOME_Evolution_Messenger_Listener_signonResult(
					listener,
					CORBA_string_dup(identity), 
					BONOBO_OBJREF(backend),
					GNOME_Evolution_Messenger_Listener_SignonError_NONE,
					ev);
				break;
			case E_MESSENGER_BACKEND_ERROR_INVALID_LOGIN:
				GNOME_Evolution_Messenger_Listener_signonResult(
					listener,
					CORBA_string_dup(identity),
					BONOBO_OBJREF(backend),
					GNOME_Evolution_Messenger_Listener_SignonError_INVALID_LOGIN,
					ev);
				break;
			case E_MESSENGER_BACKEND_ERROR_NET_FAILURE:
				GNOME_Evolution_Messenger_Listener_signonResult(
					listener,
					CORBA_string_dup(identity),
					BONOBO_OBJREF(backend),
					GNOME_Evolution_Messenger_Listener_SignonError_NET_FAILURE,
					ev);
				break;
			default:
				g_assert_not_reached();
				break;
			}

			g_free(identity);

			return;
		}
	}

	CORBA_exception_set(
		ev, CORBA_USER_EXCEPTION,
		ex_GNOME_Evolution_Messenger_BackendDispatcher_ServiceNotSupported,
		NULL);
} /* impl_MessengerBackendDispatcher_signon */

static void
e_messenger_backend_dispatcher_class_init(EMessengerBackendDispatcherClass *klass)
{
	GtkObjectClass *object_class = GTK_OBJECT_CLASS(klass);
	POA_GNOME_Evolution_Messenger_BackendDispatcher__epv *epv;

	object_class->destroy = e_messenger_backend_dispatcher_destroy;

	parent_class = gtk_type_class(bonobo_object_get_type());

	/* Initialize CORBA class functions */
	epv = &klass->epv;
	epv->signon             = impl_MessengerBackendDispatcher_signon;
} /* e_messenger_backend_dispatcher_class_init */

static void
e_messenger_backend_dispatcher_init(EMessengerBackendDispatcher *backend)
{
	backend->activated_backends = NULL;
} /* e_messenger_backend_dispatcher_init */

BONOBO_X_TYPE_FUNC_FULL(EMessengerBackendDispatcher,
			GNOME_Evolution_Messenger_BackendDispatcher,
			PARENT_TYPE, e_messenger_backend_dispatcher);

/* Temporary.  See below. */
#include "aol-toc-backend.h"

EMessengerBackendDispatcher *
e_messenger_backend_dispatcher_new(void)
{
	EMessengerBackendDispatcher *dispatcher;
	EMessengerBackend *backend;

	dispatcher = gtk_type_new(E_MESSENGER_BACKEND_DISPATCHER_TYPE);

	/* This would be a fine time to dynamically load the various
	   EMessengerBackends.  But we'll just hard code them for now */
	backend = aol_toc_backend_new();
	dispatcher->activated_backends = g_slist_prepend(
		dispatcher->activated_backends, backend);

	return dispatcher;
} /* e_messenger_backend_dispatcher_new */
