/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * An abstract class which defines the API to a given backend.
 * There will be one PASBackend object for every URI which is loaded.
 *
 * Two people will call into the PASBackend API:
 *
 * 1. The PASBookFactory, when it has been asked to load a book.
 *    It will create a new PASBackend if one is not already running
 *    for the requested URI.  It will call pas_backend_add_client to
 *    add a new client to an existing PASBackend server.
 *
 * 2. A PASBook, when a client has requested an operation on the
 *    GNOME_Evolution_Addressbook_Book interface.
 *
 * Author:
 *   Nat Friedman (nat@ximian.com)
 *
 * Copyright 2000, Ximian, Inc.
 */

#ifndef __PAS_BACKEND_H__
#define __PAS_BACKEND_H__

#include <glib.h>
#include <glib-object.h>
#include <pas/addressbook.h>
#include <pas/pas-types.h>

#define PAS_TYPE_BACKEND         (pas_backend_get_type ())
#define PAS_BACKEND(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), PAS_TYPE_BACKEND, PASBackend))
#define PAS_BACKEND_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), PAS_TYPE_BACKEND, PASBackendClass))
#define PAS_IS_BACKEND(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), PAS_TYPE_BACKEND))
#define PAS_IS_BACKEND_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), PAS_TYPE_BACKEND))
#define PAS_BACKEND_GET_CLASS(k) (G_TYPE_INSTANCE_GET_CLASS ((k), PAS_TYPE_BACKEND, PASBackendClass))

typedef struct _PASBackendPrivate PASBackendPrivate;

#include <pas/pas-book.h>

struct _PASBackend {
	GObject parent_object;
	PASBackendPrivate *priv;
};

struct _PASBackendClass {
	GObjectClass parent_class;

	/* Virtual methods */
	GNOME_Evolution_Addressbook_CallStatus (*load_uri) (PASBackend *backend, const char *uri, gboolean only_if_exists);
	gboolean (*add_client) (PASBackend *backend, PASBook *book);
	void (*remove_client) (PASBackend *backend, PASBook *book);
        char *(*get_static_capabilities) (PASBackend *backend);

	void (*create_card)  (PASBackend *backend, PASBook *book, const char *vcard);
	void (*remove_cards) (PASBackend *backend, PASBook *book, GList *id_list);
	void (*modify_card)  (PASBackend *backend, PASBook *book, const char *vcard);
	void (*get_vcard) (PASBackend *backend, PASBook *book, const char *id);
	void (*get_card_list) (PASBackend *backend, PASBook *book, const char *query);
	void (*start_book_view) (PASBackend *backend, PASBookView *book_view);
	void (*get_changes) (PASBackend *backend, PASBook *book, const char *change_id);
	void (*authenticate_user) (PASBackend *backend, PASBook *book, const char *user, const char *passwd, const char *auth_method);
	void (*get_supported_fields) (PASBackend *backend, PASBook *book);
	void (*get_supported_auth_methods) (PASBackend *backend, PASBook *book);
	GNOME_Evolution_Addressbook_CallStatus (*cancel_operation) (PASBackend *backend, PASBook *book);

	gboolean (*is_threaded) (PASBackend *backend);
	void (*start_threaded) (PASBackend *backend);

	/* Notification signals */
	void (* last_client_gone) (PASBackend *backend);

	/* Padding for future expansion */
	void (*_pas_reserved0) (void);
	void (*_pas_reserved1) (void);
	void (*_pas_reserved2) (void);
	void (*_pas_reserved3) (void);
	void (*_pas_reserved4) (void);
};

typedef PASBackend * (*PASBackendFactoryFn) (void);

gboolean    pas_backend_construct                (PASBackend             *backend);

GNOME_Evolution_Addressbook_CallStatus
            pas_backend_load_uri                 (PASBackend             *backend,
						  const char             *uri,
						  gboolean                only_if_exists);
const char *pas_backend_get_uri                  (PASBackend             *backend);

gboolean    pas_backend_add_client               (PASBackend             *backend,
						  PASBook                *book);
void        pas_backend_remove_client            (PASBackend             *backend,
						  PASBook                *book);
char       *pas_backend_get_static_capabilities  (PASBackend             *backend);

gboolean    pas_backend_is_loaded                (PASBackend             *backend);

gboolean    pas_backend_is_writable              (PASBackend             *backend);


void        pas_backend_open                     (PASBackend             *backend,
						  PASBook                *book,
						  gboolean                only_if_exists);
void        pas_backend_create_card              (PASBackend             *backend,
						  PASBook                *book,
						  const char             *vcard);
void        pas_backend_remove_cards             (PASBackend             *backend,
						  PASBook                *book,
						  GList                  *id_list);
void        pas_backend_modify_card              (PASBackend             *backend,
						  PASBook                *book,
						  const char             *vcard);
void        pas_backend_get_vcard                (PASBackend             *backend,
						  PASBook                *book,
						  const char             *id);
void        pas_backend_get_card_list            (PASBackend             *backend,
						  PASBook                *book,
						  const char             *query);
void        pas_backend_get_changes              (PASBackend             *backend,
						  PASBook                *book,
						  const char             *change_id);
void        pas_backend_authenticate_user        (PASBackend             *backend,
						  PASBook                *book,
						  const char             *user,
						  const char             *passwd,
						  const char             *auth_method);
void        pas_backend_get_supported_fields     (PASBackend             *backend,
						  PASBook                *book);
void        pas_backend_get_supported_auth_methods (PASBackend             *backend,
						    PASBook                *book);
GNOME_Evolution_Addressbook_CallStatus pas_backend_cancel_operation (PASBackend             *backend,
								     PASBook                *book);

void        pas_backend_start_book_view            (PASBackend             *backend,
						    PASBookView            *view);

EList      *pas_backend_get_book_views             (PASBackend *backend);

GType       pas_backend_get_type                 (void);


/* protected functions for subclasses */
void        pas_backend_set_is_loaded            (PASBackend             *backend,
						  gboolean                is_loaded);
void        pas_backend_set_is_writable          (PASBackend             *backend,
						  gboolean                is_writable);

#endif /* ! __PAS_BACKEND_H__ */

