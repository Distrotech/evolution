#include <bonobo.h>
#include "libtoc.h"
#include "aol-toc-backend.h"

struct _AOLTOCBackendPriv {
	GHashTable *connections;

	EMessengerBackendError error;
};

static EMessengerBackendClass *parent_class = NULL;

static 
void free_hash(char *key, TOCConnection *conn)
{
	g_free(key);
	gtk_object_destroy(GTK_OBJECT(conn));
} /* free_hash */

static void
aol_toc_backend_destroy(GtkObject *obj)
{
	AOLTOCBackend *backend = AOL_TOC_BACKEND(obj);
	
	g_hash_table_foreach(
		backend->priv->connections, (GHFunc) free_hash, NULL);
	g_hash_table_destroy(backend->priv->connections);
	g_free(backend->priv);

	(* GTK_OBJECT_CLASS(parent_class)->destroy)(obj);
} /* aol_toc_backend_destroy */

static void
signed_in_cb(TOCConnection *conn, TOCConnectionStatus status, 
	     AOLTOCBackend *backend)
{
	if (status != TOC_CONNECTION_OK)
		backend->priv->error = E_MESSENGER_BACKEND_ERROR_INVALID_LOGIN;
	else
		backend->priv->error = E_MESSENGER_BACKEND_ERROR_NONE;

	gtk_main_quit();
} /* signed_in_cb */

static void
message_in_cb(TOCConnection *conn, char *name, gboolean autoresponse,
	      char *message, AOLTOCBackend *backend)
{
	char *id;

	id = gtk_object_get_user_data(GTK_OBJECT(conn));
	e_messenger_backend_event_receive_message(
		E_MESSENGER_BACKEND(backend), id, name, autoresponse, message);
} /* message_in_cb */

static void
user_info_cb(TOCConnection *conn, char *info, AOLTOCBackend *backend)
{
	char *id;

	id = gtk_object_get_user_data(GTK_OBJECT(conn));
	e_messenger_backend_event_user_info(E_MESSENGER_BACKEND(backend), id, info);
} /* user_info_cb */

static void
user_update_cb(TOCConnection *conn, char *contact,
	       TOCConnectionUserFlags flags, int evil, time_t conn_time,
	       int idle, AOLTOCBackend *backend)
{
	char *id;
	gboolean online;
	GNOME_Evolution_Messenger_UserStatus status;

	online = flags & USER_FLAGS_CONNECTED;
	
	if (flags & USER_FLAGS_UNAVAILABLE)
		status = GNOME_Evolution_Messenger_UserStatus_AWAY;
	else
		status = GNOME_Evolution_Messenger_UserStatus_ONLINE;

	id = gtk_object_get_user_data(GTK_OBJECT(conn));
	e_messenger_backend_event_user_update(
		E_MESSENGER_BACKEND(backend), id, contact, online, status);
} /* user_update_cb */

static void
build_list(char *key, gpointer value, GSList **list)
{
	*list = g_slist_append(*list, key);
} /* build_list */

static GSList *
aol_toc_get_signons(EMessengerBackend *b)
{
	AOLTOCBackend *backend = AOL_TOC_BACKEND(b);
	GSList *signons = NULL;

	g_hash_table_foreach(
		backend->priv->connections, (GHFunc) build_list, &signons);

	return signons;
} /* aol_toc_get_signons */

static EMessengerBackendError
aol_toc_signon(EMessengerBackend *b, const char *name, const char *password,
	       GNOME_Evolution_Messenger_Listener listener)
{
	AOLTOCBackend *backend = AOL_TOC_BACKEND(b);
	TOCConnection *conn;
	TOCConnectionStatus s;

	if (b->listener)
		g_warning("There is already a listener for backend %p\n", b);
			  
	b->listener = bonobo_object_dup_ref(listener, NULL);

	conn = toc_connection_new();
	gtk_object_set_user_data(GTK_OBJECT(conn), g_strdup(name));
	gtk_signal_connect(
		GTK_OBJECT(conn), "signed_in",
		GTK_SIGNAL_FUNC(signed_in_cb), backend);
	gtk_signal_connect(
		GTK_OBJECT(conn), "message_in",
		GTK_SIGNAL_FUNC(message_in_cb), backend);
	gtk_signal_connect(
		GTK_OBJECT(conn), "user_info",
		GTK_SIGNAL_FUNC(user_info_cb), backend);
	gtk_signal_connect(
		GTK_OBJECT(conn), "user_update",
		GTK_SIGNAL_FUNC(user_update_cb), backend);

	s = toc_connection_signon(conn, name, password);

	if (s != TOC_CONNECTION_OK) {
		printf("Did not connect\n");
		return E_MESSENGER_BACKEND_ERROR_NET_FAILURE;
	}

	gtk_main();

	if (backend->priv->error == E_MESSENGER_BACKEND_ERROR_NONE) {
		g_hash_table_insert(
			backend->priv->connections, g_strdup(name), conn);
	}

	return backend->priv->error;
} /* aol_toc_signon */

static void
aol_toc_signoff(EMessengerBackend *b, const char *id)
{
	AOLTOCBackend *backend = AOL_TOC_BACKEND(b);
	TOCConnection *conn;

	conn = g_hash_table_lookup(backend->priv->connections, id);

	toc_connection_signoff(conn);
	g_free(gtk_object_get_user_data(GTK_OBJECT(conn)));

	g_hash_table_remove(backend->priv->connections, id);
} /* aol_toc_signoff */

static void
aol_toc_change_status(EMessengerBackend *b, const char *id,
		      const GNOME_Evolution_Messenger_UserStatus status,
		      const CORBA_char *data)
{
	AOLTOCBackend *backend = AOL_TOC_BACKEND(b);
	TOCConnection *conn;
	int idle;

	conn = g_hash_table_lookup(backend->priv->connections, id);

	switch (status) {
	case GNOME_Evolution_Messenger_UserStatus_ONLINE:
		toc_connection_set_away(conn, NULL);
		break;
	case GNOME_Evolution_Messenger_UserStatus_IDLE:
		idle = atoi(data);
		toc_connection_set_idle(conn, idle);
		break;
	case GNOME_Evolution_Messenger_UserStatus_AWAY:
		toc_connection_set_away(conn, data);
		break;
	default:
		g_warning("Unsupported status");
	}
} /* aol_toc_change_status */

static void
aol_toc_send_message(EMessengerBackend *b, const char *id,
		     const char *contact, const char *message)
{
	AOLTOCBackend *backend = AOL_TOC_BACKEND(b);
	TOCConnection *conn;

	conn = g_hash_table_lookup(backend->priv->connections, id);

	toc_connection_send_message(conn, contact, message);
} /* aol_toc_send_message */

static void
aol_toc_contact_info(EMessengerBackend *b, const char *id, 
		     const char *contact)
{
	AOLTOCBackend *backend = AOL_TOC_BACKEND(b);
	TOCConnection *conn;

	conn = g_hash_table_lookup(backend->priv->connections, id);

	toc_connection_get_info(conn, contact);
} /* aol_toc_contact_info */

static void
aol_toc_add_contact(EMessengerBackend *b, const char *id, 
		    const char *contact)
{
	AOLTOCBackend *backend = AOL_TOC_BACKEND(b);
	TOCConnection *conn;

	conn = g_hash_table_lookup(backend->priv->connections, id);

	toc_connection_add_buddy(conn, contact);
} /* aol_toc_add_contact */

static void
aol_toc_remove_contact(EMessengerBackend *b, const char *id, 
		       const char *contact)
{
	AOLTOCBackend *backend = AOL_TOC_BACKEND(b);
	TOCConnection *conn;

	conn = g_hash_table_lookup(backend->priv->connections, id);

	toc_connection_remove_buddy(conn, contact);
} /* aol_toc_remove_contact */

static void
aol_toc_keepalive(EMessengerBackend *b, const char *id)
{
	AOLTOCBackend *backend = AOL_TOC_BACKEND(b);
	TOCConnection *conn;

	conn = g_hash_table_lookup(backend->priv->connections, id);

	toc_connection_keepalive(conn);
} /* aol_toc_keepalive */

static void
aol_toc_backend_class_init(AOLTOCBackendClass *klass)
{
	GtkObjectClass *object_class = GTK_OBJECT_CLASS(klass);
	EMessengerBackendClass *backend_class = 
		E_MESSENGER_BACKEND_CLASS(klass);

	object_class->destroy = aol_toc_backend_destroy;

	backend_class->get_signon_list = aol_toc_get_signons;
	backend_class->signon = aol_toc_signon;
	backend_class->signoff = aol_toc_signoff;
	backend_class->change_status = aol_toc_change_status;
	backend_class->send_message = aol_toc_send_message;
	backend_class->contact_info = aol_toc_contact_info;
	backend_class->add_contact = aol_toc_add_contact;
	backend_class->remove_contact = aol_toc_remove_contact;
	backend_class->keepalive = aol_toc_keepalive;

	parent_class = gtk_type_class(e_messenger_backend_get_type());
} /* aol_toc_backend_class_init */

static void
aol_toc_backend_init(AOLTOCBackend *backend)
{
	backend->priv = g_new0(AOLTOCBackendPriv, 1);
	backend->priv->connections = g_hash_table_new(
		g_str_hash, g_str_equal);

	E_MESSENGER_BACKEND(backend)->service_name = "AIM";
} /* aol_toc_backend_init */

GtkType
aol_toc_backend_get_type(void)
{
	static GtkType type = 0;

	if (!type) {
		GtkTypeInfo info = {
			"AOLTOCBackend",
			sizeof(AOLTOCBackend),
			sizeof(AOLTOCBackendClass),
			(GtkClassInitFunc) 
			        aol_toc_backend_class_init,
			(GtkObjectInitFunc) aol_toc_backend_init,
			NULL, /* reserved 1 */
			NULL, /* reserved 2 */
			(GtkClassInitFunc) NULL
		};
		type = gtk_type_unique(
			e_messenger_backend_get_type(), &info);
	}

	return type;
} /* aol_toc_backend_get_type */			

EMessengerBackend *
aol_toc_backend_new(void)
{
	AOLTOCBackend *backend;

	backend = gtk_type_new(aol_toc_backend_get_type());

	return E_MESSENGER_BACKEND(backend);
} /* aol_toc_backend_new */
