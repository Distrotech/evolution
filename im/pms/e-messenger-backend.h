#ifndef E_MESSENGER_BACKEND_H
#define E_MESSENGER_BACKEND_H

#include <bonobo/bonobo-xobject.h>
#include "Messenger.h"

#define E_MESSENGER_BACKEND_TYPE             (e_messenger_backend_get_type())
#define E_MESSENGER_BACKEND(obj)             (GTK_CHECK_CAST((obj), E_MESSENGER_BACKEND_TYPE, EMessengerBackend))
#define E_MESSENGER_BACKEND_CLASS(klass)     (GTK_CHECK_CLASS_CAST((klass), E_MESSENGER_BACKEND_TYPE, EMessengerBackendClass))
#define IM_IS_BACKEND(obj)          (GTK_CHECK_TYPE((obj), E_MESSENGER_BACKEND_TYPE))
#define IM_IS_BACKEND_CLASS(klass)  (GTK_CHECK_CLASS_TYPE((klass), E_MESSENGER_BACKEND_TYPE))
#define E_MESSENGER_BACKEND_GET_CLASS(obj)   (E_MESSENGER_BACKEND_CLASS(GTK_OBJECT(obj)->klass))

typedef   enum _EMessengerBackendError EMessengerBackendError;
typedef struct _EMessengerBackend      EMessengerBackend;
typedef struct _EMessengerBackendClass EMessengerBackendClass;

enum _EMessengerBackendError {
	E_MESSENGER_BACKEND_ERROR_NONE,
	E_MESSENGER_BACKEND_ERROR_INVALID_LOGIN,
	E_MESSENGER_BACKEND_ERROR_NET_FAILURE,
	E_MESSENGER_BACKEND_ERROR_UNKNOWN_CONTACT
};

struct _EMessengerBackend {
	BonoboXObject parent;

	char *service_name;

	GNOME_Evolution_Messenger_Listener listener;
};

struct _EMessengerBackendClass {
	BonoboXObjectClass parent_class;

	POA_GNOME_Evolution_Messenger_Backend__epv epv;

	/* Virtual functions */
	GSList *(* get_signon_list)(EMessengerBackend *backend);
	EMessengerBackendError (*signon)(
		EMessengerBackend *backend, 
		const char *signon, 
		const char *password,
		GNOME_Evolution_Messenger_Listener listener);
	void (* signoff)(
		EMessengerBackend *backend, 
		const char *signon);
	void (* change_status)(
		EMessengerBackend *backend,
		const char *signon,
		const GNOME_Evolution_Messenger_UserStatus status,
		const CORBA_char *data);
	void (* send_message)(
		EMessengerBackend *backend,
		const char *signon,
		const CORBA_char *contact,
		const CORBA_char *message);
	void (* contact_info)(
		EMessengerBackend *backend, 
		const char *signon, 
		const char *contact);
	void (* add_contact)(
		EMessengerBackend *backend, 
		const char *signon,
		const char *contact);
	void (* remove_contact)(
		EMessengerBackend *backend, 
		const char *signon, 
		const char *contact);
	void (* keepalive)(
		EMessengerBackend *backend, 
		const char *signon);
};

GtkType e_messenger_backend_get_type(void);
EMessengerBackend *e_messenger_backend_new(void);

/* Notifying listeners of events */
void e_messenger_backend_event_receive_message(
	EMessengerBackend *backend, char *id, char *name,
	gboolean autoresponse, char *message);
void e_messenger_backend_event_user_info(
	EMessengerBackend *backend, char *id, char *info);
void e_messenger_backend_event_user_update(
	EMessengerBackend *backend, char *id, char *contact, gboolean online,
	GNOME_Evolution_Messenger_UserStatus status);

#endif /* E_MESSENGER_BACKEND_H */
