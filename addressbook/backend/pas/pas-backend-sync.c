/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Author:
 *   Chris Toshok (toshok@ximian.com)
 *
 * Copyright (C) 2003, Ximian, Inc.
 */

#include <config.h>
#include "pas-backend-sync.h"
#include "pas-marshal.h"

struct _PASBackendSyncPrivate {
  int mumble;
};

static GObjectClass *parent_class;

gboolean
pas_backend_sync_construct (PASBackendSync *backend)
{
	return TRUE;
}

PASBackendSyncStatus
pas_backend_sync_create_card (PASBackendSync *backend,
			      PASBook    *book,
			      PASCreateCardRequest *req,
			      char **id)
{
	g_return_val_if_fail (backend && PAS_IS_BACKEND_SYNC (backend), GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (book && PAS_IS_BOOK (book), GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (req && req->vcard, GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (id, GNOME_Evolution_Addressbook_OtherError);

	g_assert (PAS_BACKEND_SYNC_GET_CLASS (backend)->create_card_sync);

	return (* PAS_BACKEND_SYNC_GET_CLASS (backend)->create_card_sync) (backend, book, req, id);
}

PASBackendSyncStatus
pas_backend_sync_remove_cards (PASBackendSync *backend,
			       PASBook *book,
			       PASRemoveCardsRequest *req)
{
	g_return_val_if_fail (backend && PAS_IS_BACKEND_SYNC (backend), GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (book && PAS_IS_BOOK (book), GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (req && req->ids, GNOME_Evolution_Addressbook_OtherError);

	g_assert (PAS_BACKEND_SYNC_GET_CLASS (backend)->remove_cards_sync);

	return (* PAS_BACKEND_SYNC_GET_CLASS (backend)->remove_cards_sync) (backend, book, req);
}

PASBackendSyncStatus
pas_backend_sync_modify_card (PASBackendSync *backend,
			      PASBook *book,
			      PASModifyCardRequest *req)
{
	g_return_val_if_fail (backend && PAS_IS_BACKEND_SYNC (backend), GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (book && PAS_IS_BOOK (book), GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (req && req->vcard, GNOME_Evolution_Addressbook_OtherError);

	g_assert (PAS_BACKEND_SYNC_GET_CLASS (backend)->modify_card_sync);

	return (* PAS_BACKEND_SYNC_GET_CLASS (backend)->modify_card_sync) (backend, book, req);
}

PASBackendSyncStatus
pas_backend_sync_get_vcard (PASBackendSync *backend,
			    PASBook *book,
			    PASGetVCardRequest *req,
			    char **vcard)
{
	g_return_val_if_fail (backend && PAS_IS_BACKEND_SYNC (backend), GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (book && PAS_IS_BOOK (book), GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (req && req->id, GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (vcard, GNOME_Evolution_Addressbook_OtherError);

	g_assert (PAS_BACKEND_SYNC_GET_CLASS (backend)->get_vcard_sync);

	return (* PAS_BACKEND_SYNC_GET_CLASS (backend)->get_vcard_sync) (backend, book, req, vcard);
}

PASBackendSyncStatus
pas_backend_sync_get_card_list (PASBackendSync *backend,
				PASBook *book,
				PASGetCardListRequest *req,
				GList **cards)
{
	g_return_val_if_fail (backend && PAS_IS_BACKEND_SYNC (backend), GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (book && PAS_IS_BOOK (book), GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (req && req->query, GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (cards, GNOME_Evolution_Addressbook_OtherError);

	g_assert (PAS_BACKEND_SYNC_GET_CLASS (backend)->get_card_list_sync);

	return (* PAS_BACKEND_SYNC_GET_CLASS (backend)->get_card_list_sync) (backend, book, req, cards);
}

PASBackendSyncStatus
pas_backend_sync_get_book_view (PASBackendSync *backend,
				PASBook *book,
				PASGetBookViewRequest *req,
				PASBookView **view)
{
	g_return_val_if_fail (backend && PAS_IS_BACKEND_SYNC (backend), GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (book && PAS_IS_BOOK (book), GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (req && req->listener != CORBA_OBJECT_NIL, GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (view, GNOME_Evolution_Addressbook_OtherError);
	
	g_assert (PAS_BACKEND_SYNC_GET_CLASS (backend)->get_book_view_sync);

	return (* PAS_BACKEND_SYNC_GET_CLASS (backend)->get_book_view_sync) (backend, book, req, view);
}

PASBackendSyncStatus
pas_backend_sync_get_changes (PASBackendSync *backend,
			      PASBook *book,
			      PASGetChangesRequest *req,
			      PASBookView **view)
{
	g_return_val_if_fail (backend && PAS_IS_BACKEND_SYNC (backend), GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (book && PAS_IS_BOOK (book), GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (req && req->change_id && req->listener != CORBA_OBJECT_NIL, GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (view, GNOME_Evolution_Addressbook_OtherError);

	g_assert (PAS_BACKEND_SYNC_GET_CLASS (backend)->get_changes_sync);

	return (* PAS_BACKEND_SYNC_GET_CLASS (backend)->get_changes_sync) (backend, book, req, view);
}

PASBackendSyncStatus
pas_backend_sync_authenticate_user (PASBackendSync *backend,
				    PASBook *book,
				    PASAuthenticateUserRequest *req)
{
	g_return_val_if_fail (backend && PAS_IS_BACKEND_SYNC (backend), GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (book && PAS_IS_BOOK (book), GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (req, GNOME_Evolution_Addressbook_OtherError);

	g_assert (PAS_BACKEND_SYNC_GET_CLASS (backend)->authenticate_user_sync);

	return (* PAS_BACKEND_SYNC_GET_CLASS (backend)->authenticate_user_sync) (backend, book, req);
}

PASBackendSyncStatus
pas_backend_sync_get_supported_fields (PASBackendSync *backend,
				       PASBook *book,
				       PASGetSupportedFieldsRequest *req,
				       GList **fields)
{
	g_return_val_if_fail (backend && PAS_IS_BACKEND_SYNC (backend), GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (book && PAS_IS_BOOK (book), GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (req, GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (fields, GNOME_Evolution_Addressbook_OtherError);
	
	g_assert (PAS_BACKEND_SYNC_GET_CLASS (backend)->get_supported_fields_sync);

	return (* PAS_BACKEND_SYNC_GET_CLASS (backend)->get_supported_fields_sync) (backend, book, req, fields);
}

PASBackendSyncStatus
pas_backend_sync_get_supported_auth_methods (PASBackendSync *backend,
					     PASBook *book,
					     PASGetSupportedAuthMethodsRequest *req,
					     GList **methods)
{
	g_return_val_if_fail (backend && PAS_IS_BACKEND_SYNC (backend), GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (book && PAS_IS_BOOK (book), GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (req, GNOME_Evolution_Addressbook_OtherError);
	g_return_val_if_fail (methods, GNOME_Evolution_Addressbook_OtherError);

	g_assert (PAS_BACKEND_SYNC_GET_CLASS (backend)->get_supported_auth_methods_sync);

	return (* PAS_BACKEND_SYNC_GET_CLASS (backend)->get_supported_auth_methods_sync) (backend, book, req, methods);
}

static gboolean
_pas_backend_is_threaded (PASBackend *backend)
{
	return TRUE;
}

static void
_pas_backend_create_card (PASBackend *backend,
			  PASBook    *book,
			  PASCreateCardRequest *req)
{
  PASBackendSyncStatus status;
  char *id;

  status = pas_backend_sync_create_card (PAS_BACKEND_SYNC (backend), book, req, &id);

  pas_book_respond_create (book, status, id);

  g_free (id);
}

static void
_pas_backend_remove_cards (PASBackend *backend,
			   PASBook    *book,
			   PASRemoveCardsRequest *req)
{
  PASBackendSyncStatus status;

  status = pas_backend_sync_remove_cards (PAS_BACKEND_SYNC (backend), book, req);

  pas_book_respond_remove (book, status);
}

static void
_pas_backend_modify_card (PASBackend *backend,
			  PASBook    *book,
			  PASModifyCardRequest *req)
{
  PASBackendSyncStatus status;

  status = pas_backend_sync_modify_card (PAS_BACKEND_SYNC (backend), book, req);

  pas_book_respond_modify (book, status);
}

static void
_pas_backend_get_vcard (PASBackend *backend,
			PASBook    *book,
			PASGetVCardRequest *req)
{
  PASBackendSyncStatus status;
  char *vcard;

  status = pas_backend_sync_get_vcard (PAS_BACKEND_SYNC (backend), book, req, &vcard);

  pas_book_respond_get_vcard (book, status, vcard);

  g_free (vcard);
}

static void
_pas_backend_get_card_list (PASBackend *backend,
			    PASBook    *book,
			    PASGetCardListRequest *req)
{
  PASBackendSyncStatus status;
  GList *cards = NULL;

  status = pas_backend_sync_get_card_list (PAS_BACKEND_SYNC (backend), book, req, &cards);

  pas_book_respond_get_card_list (book, status, cards);

  g_list_foreach (cards, (GFunc)g_free, NULL);
  g_list_free (cards);
}

static void
_pas_backend_get_book_view (PASBackend *backend,
			    PASBook    *book,
			    PASGetBookViewRequest *req)
{
  PASBackendSyncStatus status;
  PASBookView *view;

  status = pas_backend_sync_get_book_view (PAS_BACKEND_SYNC (backend), book, req, &view);

  pas_book_respond_get_book_view (book, status, view);

  /* XXX free view? */
}

static void
_pas_backend_get_changes (PASBackend *backend,
			  PASBook    *book,
			  PASGetChangesRequest *req)
{
  PASBackendSyncStatus status;
  PASBookView *view;

  status = pas_backend_sync_get_changes (PAS_BACKEND_SYNC (backend), book, req, &view);

  pas_book_respond_get_changes (book, status, view);

  /* XXX free view? */
}

static void
_pas_backend_authenticate_user (PASBackend *backend,
				PASBook    *book,
				PASAuthenticateUserRequest *req)
{
  PASBackendSyncStatus status;

  status = pas_backend_sync_authenticate_user (PAS_BACKEND_SYNC (backend), book, req);

  pas_book_respond_authenticate_user (book, status);
}

static void
_pas_backend_get_supported_fields (PASBackend *backend,
				   PASBook    *book,
				   PASGetSupportedFieldsRequest *req)
{
  PASBackendSyncStatus status;
  GList *fields = NULL;

  status = pas_backend_sync_get_supported_fields (PAS_BACKEND_SYNC (backend), book, req, &fields);

  pas_book_respond_get_supported_fields (book, status, fields);

  g_list_foreach (fields, (GFunc)g_free, NULL);
  g_list_free (fields);
}

static void
_pas_backend_get_supported_auth_methods (PASBackend *backend,
					 PASBook    *book,
					 PASGetSupportedAuthMethodsRequest *req)
{
  PASBackendSyncStatus status;
  GList *methods = NULL;

  status = pas_backend_sync_get_supported_auth_methods (PAS_BACKEND_SYNC (backend), book, req, &methods);

  pas_book_respond_get_supported_auth_methods (book, status, methods);

  g_list_foreach (methods, (GFunc)g_free, NULL);
  g_list_free (methods);
}

static void
pas_backend_sync_init (PASBackendSync *backend)
{
	PASBackendSyncPrivate *priv;

	priv          = g_new0 (PASBackendSyncPrivate, 1);

	backend->priv = priv;
}

static void
pas_backend_sync_dispose (GObject *object)
{
	PASBackendSync *backend;

	backend = PAS_BACKEND_SYNC (object);

	if (backend->priv) {
		g_free (backend->priv);

		backend->priv = NULL;
	}

	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
pas_backend_sync_class_init (PASBackendSyncClass *klass)
{
	GObjectClass *object_class;
	PASBackendClass *backend_class = PAS_BACKEND_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class = (GObjectClass *) klass;

	backend_class->is_threaded = _pas_backend_is_threaded;
	backend_class->create_card = _pas_backend_create_card;
	backend_class->remove_cards = _pas_backend_remove_cards;
	backend_class->modify_card = _pas_backend_modify_card;
	backend_class->get_vcard = _pas_backend_get_vcard;
	backend_class->get_card_list = _pas_backend_get_card_list;
	backend_class->get_book_view = _pas_backend_get_book_view;
	backend_class->get_changes = _pas_backend_get_changes;
	backend_class->authenticate_user = _pas_backend_authenticate_user;
	backend_class->get_supported_fields = _pas_backend_get_supported_fields;
	backend_class->get_supported_auth_methods = _pas_backend_get_supported_auth_methods;

	object_class->dispose = pas_backend_sync_dispose;
}

/**
 * pas_backend_get_type:
 */
GType
pas_backend_sync_get_type (void)
{
	static GType type = 0;

	if (! type) {
		GTypeInfo info = {
			sizeof (PASBackendSyncClass),
			NULL, /* base_class_init */
			NULL, /* base_class_finalize */
			(GClassInitFunc)  pas_backend_sync_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (PASBackendSync),
			0,    /* n_preallocs */
			(GInstanceInitFunc) pas_backend_sync_init
		};

		type = g_type_register_static (PAS_TYPE_BACKEND, "PASBackendSync", &info, 0);
	}

	return type;
}
