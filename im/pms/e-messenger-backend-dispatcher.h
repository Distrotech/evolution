#ifndef E_MESSENGER_BACKEND_DISPATCHER_H
#define E_MESSENGER_BACKEND_DISPATCHER_H

#include <bonobo/bonobo-xobject.h>
#include "Messenger.h"

#define E_MESSENGER_BACKEND_DISPATCHER_TYPE             (e_messenger_backend_dispatcher_get_type())
#define E_MESSENGER_BACKEND_DISPATCHER(obj)             (GTK_CHECK_CAST((obj), E_MESSENGER_BACKEND_DISPATCHER_TYPE, EMessengerBackendDispatcher))
#define E_MESSENGER_BACKEND_DISPATCHER_CLASS(klass)     (GTK_CHECK_CLASS_CAST((klass), E_MESSENGER_BACKEND_DISPATCHER_TYPE, EMessengerBackendDispatcherClass))
#define E_IS_MESSENGER_BACKEND(obj)                     (GTK_CHECK_TYPE((obj), E_MESSENGER_BACKEND_DISPATCHER_TYPE))
#define E_IS_MESSENGER_BACKEND_CLASS(klass)             (GTK_CHECK_CLASS_TYPE((klass), E_MESSENGER_BACKEND_DISPATCHER_TYPE))
#define E_MESSENGER_BACKEND_DISPATCHER_GET_CLASS(obj)   (E_MESSENGER_BACKEND_DISPATCHER_CLASS(GTK_OBJECT(obj)->klass))

typedef struct _EMessengerBackendDispatcher      EMessengerBackendDispatcher;
typedef struct _EMessengerBackendDispatcherClass EMessengerBackendDispatcherClass;

struct _EMessengerBackendDispatcher {
	BonoboXObject parent;

	/* List of activated backends */
	GSList *activated_backends; /* EMessengerBackendDispatcher */
};

struct _EMessengerBackendDispatcherClass {
	BonoboXObjectClass parent_class;

	POA_GNOME_Evolution_Messenger_BackendDispatcher__epv epv;
};

GtkType e_messenger_backend_dispatcher_get_type(void);
EMessengerBackendDispatcher *e_messenger_backend_dispatcher_new(void);

#endif /* E_MESSENGER_BACKEND_DISPATCHER_H */
