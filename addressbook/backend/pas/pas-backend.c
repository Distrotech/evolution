/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Author:
 *   Nat Friedman (nat@ximian.com)
 *
 * Copyright 2000, Ximian, Inc.
 */

#include <config.h>
#include <pthread.h>
#include "pas-book-view.h"
#include "pas-backend.h"
#include "pas-marshal.h"

struct _PASBackendPrivate {
	GList *clients;
	char *uri;
	gboolean loaded, writable;
	EList *views;
	GMutex *open_mutex;
};

/* Signal IDs */
enum {
	LAST_CLIENT_GONE,
	LAST_SIGNAL
};

static guint pas_backend_signals[LAST_SIGNAL];

static GObjectClass *parent_class;

gboolean
pas_backend_construct (PASBackend *backend)
{
	return TRUE;
}

GNOME_Evolution_Addressbook_CallStatus
pas_backend_load_uri (PASBackend             *backend,
		      const char             *uri,
		      gboolean                only_if_exists)
{
	GNOME_Evolution_Addressbook_CallStatus status;

	g_return_val_if_fail (backend && PAS_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (uri, FALSE);
	g_return_val_if_fail (backend->priv->loaded == FALSE, FALSE);

	g_assert (PAS_BACKEND_GET_CLASS (backend)->load_uri);

	status = (* PAS_BACKEND_GET_CLASS (backend)->load_uri) (backend, uri, only_if_exists);

	if (status == GNOME_Evolution_Addressbook_Success)
		backend->priv->uri = g_strdup (uri);

	return status;
}

/**
 * pas_backend_get_uri:
 * @backend: An addressbook backend.
 * 
 * Queries the URI that an addressbook backend is serving.
 * 
 * Return value: URI for the backend.
 **/
const char *
pas_backend_get_uri (PASBackend *backend)
{
	g_return_val_if_fail (backend && PAS_IS_BACKEND (backend), NULL);

	return backend->priv->uri;
}

void
pas_backend_open (PASBackend *backend,
		  PASBook    *book,
		  gboolean    only_if_exists)
{
	g_return_if_fail (backend && PAS_IS_BACKEND (backend));
	g_return_if_fail (book && PAS_IS_BOOK (book));

	g_mutex_lock (backend->priv->open_mutex);

	if (backend->priv->loaded) {
		pas_book_respond_open (
			book, GNOME_Evolution_Addressbook_Success);

		pas_book_report_writable (book, backend->priv->writable);
	} else {
		pas_book_respond_open (
		       book, pas_backend_load_uri (backend, pas_book_get_uri (book), only_if_exists));
	}

	g_mutex_unlock (backend->priv->open_mutex);
}

void
pas_backend_create_card (PASBackend *backend,
			 PASBook    *book,
			 const char *vcard)
{
	g_return_if_fail (backend && PAS_IS_BACKEND (backend));
	g_return_if_fail (book && PAS_IS_BOOK (book));
	g_return_if_fail (vcard);

	g_assert (PAS_BACKEND_GET_CLASS (backend)->create_card);

	(* PAS_BACKEND_GET_CLASS (backend)->create_card) (backend, book, vcard);
}

void
pas_backend_remove_cards (PASBackend *backend,
			  PASBook *book,
			  GList *id_list)
{
	g_return_if_fail (backend && PAS_IS_BACKEND (backend));
	g_return_if_fail (book && PAS_IS_BOOK (book));
	g_return_if_fail (id_list);

	g_assert (PAS_BACKEND_GET_CLASS (backend)->remove_cards);

	(* PAS_BACKEND_GET_CLASS (backend)->remove_cards) (backend, book, id_list);
}

void
pas_backend_modify_card (PASBackend *backend,
			 PASBook *book,
			 const char *vcard)
{
	g_return_if_fail (backend && PAS_IS_BACKEND (backend));
	g_return_if_fail (book && PAS_IS_BOOK (book));
	g_return_if_fail (vcard);

	g_assert (PAS_BACKEND_GET_CLASS (backend)->modify_card);

	(* PAS_BACKEND_GET_CLASS (backend)->modify_card) (backend, book, vcard);
}

void
pas_backend_get_vcard (PASBackend *backend,
		       PASBook *book,
		       const char *id)
{
	g_return_if_fail (backend && PAS_IS_BACKEND (backend));
	g_return_if_fail (book && PAS_IS_BOOK (book));
	g_return_if_fail (id);

	g_assert (PAS_BACKEND_GET_CLASS (backend)->get_vcard);

	(* PAS_BACKEND_GET_CLASS (backend)->get_vcard) (backend, book, id);
}

void
pas_backend_get_card_list (PASBackend *backend,
			   PASBook *book,
			   const char *query)
{
	g_return_if_fail (backend && PAS_IS_BACKEND (backend));
	g_return_if_fail (book && PAS_IS_BOOK (book));
	g_return_if_fail (query);

	g_assert (PAS_BACKEND_GET_CLASS (backend)->get_card_list);

	(* PAS_BACKEND_GET_CLASS (backend)->get_card_list) (backend, book, query);
}

void
pas_backend_start_book_view (PASBackend *backend,
			     PASBookView *book_view)
{
	g_return_if_fail (backend && PAS_IS_BACKEND (backend));
	g_return_if_fail (book_view && PAS_IS_BOOK_VIEW (book_view));

	g_assert (PAS_BACKEND_GET_CLASS (backend)->start_book_view);

	(* PAS_BACKEND_GET_CLASS (backend)->start_book_view) (backend, book_view);
}

void
pas_backend_get_changes (PASBackend *backend,
			 PASBook *book,
			 const char *change_id)
{
	g_return_if_fail (backend && PAS_IS_BACKEND (backend));
	g_return_if_fail (book && PAS_IS_BOOK (book));
	g_return_if_fail (change_id);

	g_assert (PAS_BACKEND_GET_CLASS (backend)->get_changes);

	(* PAS_BACKEND_GET_CLASS (backend)->get_changes) (backend, book, change_id);
}

void
pas_backend_authenticate_user (PASBackend *backend,
			       PASBook *book,
			       const char *user,
			       const char *passwd,
			       const char *auth_method)
{
	g_return_if_fail (backend && PAS_IS_BACKEND (backend));
	g_return_if_fail (book && PAS_IS_BOOK (book));
	g_return_if_fail (user && passwd && auth_method);

	g_assert (PAS_BACKEND_GET_CLASS (backend)->authenticate_user);

	(* PAS_BACKEND_GET_CLASS (backend)->authenticate_user) (backend, book, user, passwd, auth_method);
}

void
pas_backend_get_supported_fields (PASBackend *backend,
				  PASBook *book)

{
	g_return_if_fail (backend && PAS_IS_BACKEND (backend));
	g_return_if_fail (book && PAS_IS_BOOK (book));

	g_assert (PAS_BACKEND_GET_CLASS (backend)->get_supported_fields);

	(* PAS_BACKEND_GET_CLASS (backend)->get_supported_fields) (backend, book);
}

void
pas_backend_get_supported_auth_methods (PASBackend *backend,
					PASBook *book)
{
	g_return_if_fail (backend && PAS_IS_BACKEND (backend));
	g_return_if_fail (book && PAS_IS_BOOK (book));

	g_assert (PAS_BACKEND_GET_CLASS (backend)->get_supported_auth_methods);

	(* PAS_BACKEND_GET_CLASS (backend)->get_supported_auth_methods) (backend, book);
}

GNOME_Evolution_Addressbook_CallStatus
pas_backend_cancel_operation (PASBackend *backend,
			      PASBook *book)
{
	g_return_val_if_fail (backend && PAS_IS_BACKEND (backend), GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (book && PAS_IS_BOOK (book), GNOME_Evolution_Addressbook_OtherError);

	g_assert (PAS_BACKEND_GET_CLASS (backend)->cancel_operation);

	return (* PAS_BACKEND_GET_CLASS (backend)->cancel_operation) (backend, book);
}

static void
book_destroy_cb (gpointer data, GObject *where_book_was)
{
	PASBackend *backend = PAS_BACKEND (data);

	pas_backend_remove_client (backend, (PASBook *)where_book_was);
}

static void
listener_died_cb (gpointer cnx, gpointer user_data)
{
	PASBook *book = PAS_BOOK (user_data);

	pas_backend_remove_client (pas_book_get_backend (book), book);
}

static void
last_client_gone (PASBackend *backend)
{
	g_signal_emit (backend, pas_backend_signals[LAST_CLIENT_GONE], 0);
}

static gboolean
real_add_client (PASBackend      *backend,
		 PASBook         *book)
{
	bonobo_object_set_immortal (BONOBO_OBJECT (book), TRUE);

	g_object_weak_ref (G_OBJECT (book), book_destroy_cb, backend);

	ORBit_small_listen_for_broken (pas_book_get_listener (book), G_CALLBACK (listener_died_cb), book);

	backend->priv->clients = g_list_prepend (backend->priv->clients, book);

	return TRUE;
}

static void
real_remove_client (PASBackend *backend,
		    PASBook    *book)
{
	/* Disconnect */
	backend->priv->clients = g_list_remove (backend->priv->clients, book);

	/* When all clients go away, notify the parent factory about it so that
	 * it may decide whether to kill the backend or not.
	 */
	if (!backend->priv->clients)
		last_client_gone (backend);
}

EList*
pas_backend_get_book_views (PASBackend *backend)
{
	g_return_val_if_fail (backend && PAS_IS_BACKEND (backend), FALSE);

	return g_object_ref (backend->priv->views);
}

/**
 * pas_backend_add_client:
 * @backend: An addressbook backend.
 * @book: the corba object representing the client connection.
 *
 * Adds a client to an addressbook backend.
 *
 * Return value: TRUE on success, FALSE on failure to add the client.
 */
gboolean
pas_backend_add_client (PASBackend      *backend,
			PASBook         *book)
{
	g_return_val_if_fail (backend && PAS_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (book && PAS_IS_BOOK (book), FALSE);

	g_assert (PAS_BACKEND_GET_CLASS (backend)->add_client);

	return PAS_BACKEND_GET_CLASS (backend)->add_client (backend, book);
}

void
pas_backend_remove_client (PASBackend *backend,
			   PASBook    *book)
{
	g_return_if_fail (backend && PAS_IS_BACKEND (backend));
	g_return_if_fail (book && PAS_IS_BOOK (book));
	
	g_assert (PAS_BACKEND_GET_CLASS (backend)->remove_client);

	PAS_BACKEND_GET_CLASS (backend)->remove_client (backend, book);
}

char *
pas_backend_get_static_capabilities (PASBackend *backend)
{
	g_return_val_if_fail (backend && PAS_IS_BACKEND (backend), NULL);
	
	g_assert (PAS_BACKEND_GET_CLASS (backend)->get_static_capabilities);

	return PAS_BACKEND_GET_CLASS (backend)->get_static_capabilities (backend);
}

gboolean
pas_backend_is_loaded (PASBackend *backend)
{
	g_return_val_if_fail (backend && PAS_IS_BACKEND (backend), FALSE);

	return backend->priv->loaded;
}

void
pas_backend_set_is_loaded (PASBackend *backend, gboolean is_loaded)
{
	g_return_if_fail (backend && PAS_IS_BACKEND (backend));

	backend->priv->loaded = is_loaded;
}

gboolean
pas_backend_is_writable (PASBackend *backend)
{
	g_return_val_if_fail (backend && PAS_IS_BACKEND (backend), FALSE);
	
	return backend->priv->writable;
}

void
pas_backend_set_is_writable (PASBackend *backend, gboolean is_writable)
{
	g_return_if_fail (backend && PAS_IS_BACKEND (backend));
	
	backend->priv->writable = is_writable;
}

static void
pas_backend_init (PASBackend *backend)
{
	PASBackendPrivate *priv;

	priv          = g_new0 (PASBackendPrivate, 1);
	priv->uri     = NULL;
	priv->clients = NULL;
	priv->views   = e_list_new((EListCopyFunc) g_object_ref, (EListFreeFunc) g_object_unref, NULL);
	priv->open_mutex = g_mutex_new ();

	backend->priv = priv;
}

static void
pas_backend_dispose (GObject *object)
{
	PASBackend *backend;

	backend = PAS_BACKEND (object);

	if (backend->priv) {
		g_list_free (backend->priv->clients);

		if (backend->priv->uri)
			g_free (backend->priv->uri);

		if (backend->priv->views) {
			g_object_unref (backend->priv->views);
			backend->priv->views = NULL;
		}

		g_mutex_free (backend->priv->open_mutex);

		g_free (backend->priv);
		backend->priv = NULL;
	}

	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
pas_backend_class_init (PASBackendClass *klass)
{
	GObjectClass *object_class;

	parent_class = g_type_class_peek_parent (klass);

	object_class = (GObjectClass *) klass;

	klass->add_client = real_add_client;
	klass->remove_client = real_remove_client;

	object_class->dispose = pas_backend_dispose;

	pas_backend_signals[LAST_CLIENT_GONE] =
		g_signal_new ("last_client_gone",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (PASBackendClass, last_client_gone),
			      NULL, NULL,
			      pas_marshal_NONE__NONE,
			      G_TYPE_NONE, 0);
}

/**
 * pas_backend_get_type:
 */
GType
pas_backend_get_type (void)
{
	static GType type = 0;

	if (! type) {
		GTypeInfo info = {
			sizeof (PASBackendClass),
			NULL, /* base_class_init */
			NULL, /* base_class_finalize */
			(GClassInitFunc)  pas_backend_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (PASBackend),
			0,    /* n_preallocs */
			(GInstanceInitFunc) pas_backend_init
		};

		type = g_type_register_static (G_TYPE_OBJECT, "PASBackend", &info, 0);
	}

	return type;
}
