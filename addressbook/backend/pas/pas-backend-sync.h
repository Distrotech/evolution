/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 */

#ifndef __PAS_BACKEND_SYNC_H__
#define __PAS_BACKEND_SYNC_H__

#include <glib.h>
#include <pas/pas-types.h>
#include <pas/pas-backend.h>
#include <pas/addressbook.h>

#define PAS_TYPE_BACKEND_SYNC         (pas_backend_sync_get_type ())
#define PAS_BACKEND_SYNC(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), PAS_TYPE_BACKEND_SYNC, PASBackendSync))
#define PAS_BACKEND_SYNC_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), PAS_TYPE_BACKEND_SYNC, PASBackendSyncClass))
#define PAS_IS_BACKEND_SYNC(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), PAS_TYPE_BACKEND_SYNC))
#define PAS_IS_BACKEND_SYNC_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), PAS_TYPE_BACKEND_SYNC))
#define PAS_BACKEND_SYNC_GET_CLASS(k) (G_TYPE_INSTANCE_GET_CLASS ((k), PAS_TYPE_BACKEND_SYNC, PASBackendSyncClass))

typedef struct _PASBackendSyncPrivate PASBackendSyncPrivate;

typedef GNOME_Evolution_Addressbook_CallStatus PASBackendSyncStatus;

struct _PASBackendSync {
	PASBackend parent_object;
	PASBackendSyncPrivate *priv;
};

struct _PASBackendSyncClass {
	PASBackendClass parent_class;

	/* Virtual methods */
	PASBackendSyncStatus (*create_card_sync)  (PASBackendSync *backend, PASBook *book,
						   PASCreateCardRequest *req, char **id);
	PASBackendSyncStatus (*remove_cards_sync) (PASBackendSync *backend, PASBook *book,
						   PASRemoveCardsRequest *req, GList **ids);
	PASBackendSyncStatus (*modify_card_sync)  (PASBackendSync *backend, PASBook *book,
						   PASModifyCardRequest *req, char **old_vcard);
	PASBackendSyncStatus (*get_vcard_sync) (PASBackendSync *backend, PASBook *book,
						PASGetVCardRequest *req, char **vcard);
	PASBackendSyncStatus (*get_card_list_sync) (PASBackendSync *backend, PASBook *book,
						    PASGetCardListRequest *req, GList **cards);
	PASBackendSyncStatus (*get_changes_sync) (PASBackendSync *backend, PASBook *book,
						  PASGetChangesRequest *req, GList **changes);
	PASBackendSyncStatus (*authenticate_user_sync) (PASBackendSync *backend, PASBook *book,
							PASAuthenticateUserRequest *req);
	PASBackendSyncStatus (*get_supported_fields_sync) (PASBackendSync *backend, PASBook *book,
							   PASGetSupportedFieldsRequest *req,
							   GList **fields);
	PASBackendSyncStatus (*get_supported_auth_methods_sync) (PASBackendSync *backend, PASBook *book,
								 PASGetSupportedAuthMethodsRequest *req,
								 GList **methods);

	/* Padding for future expansion */
	void (*_pas_reserved0) (void);
	void (*_pas_reserved1) (void);
	void (*_pas_reserved2) (void);
	void (*_pas_reserved3) (void);
	void (*_pas_reserved4) (void);

};

typedef PASBackendSync * (*PASBackendSyncFactoryFn) (void);

gboolean    pas_backend_sync_construct                (PASBackendSync             *backend);

GType       pas_backend_sync_get_type                 (void);

PASBackendSyncStatus pas_backend_sync_create_card  (PASBackendSync *backend, PASBook *book, PASCreateCardRequest *req, char **id);
PASBackendSyncStatus pas_backend_sync_remove_cards (PASBackendSync *backend, PASBook *book, PASRemoveCardsRequest *req, GList **ids);
PASBackendSyncStatus pas_backend_sync_modify_card  (PASBackendSync *backend, PASBook *book, PASModifyCardRequest *req, char **old_vcard);
PASBackendSyncStatus pas_backend_sync_get_vcard (PASBackendSync *backend, PASBook *book, PASGetVCardRequest *req, char **vcard);
PASBackendSyncStatus pas_backend_sync_get_card_list (PASBackendSync *backend, PASBook *book, PASGetCardListRequest *req, GList **cards);
PASBackendSyncStatus pas_backend_sync_get_changes (PASBackendSync *backend, PASBook *book, PASGetChangesRequest *req, GList **changes);
PASBackendSyncStatus pas_backend_sync_authenticate_user (PASBackendSync *backend, PASBook *book, PASAuthenticateUserRequest *req);
PASBackendSyncStatus pas_backend_sync_get_supported_fields (PASBackendSync *backend, PASBook *book, PASGetSupportedFieldsRequest *req, GList **fields);
PASBackendSyncStatus pas_backend_sync_get_supported_auth_methods (PASBackendSync *backend, PASBook *book, PASGetSupportedAuthMethodsRequest *req, GList **methods);

#endif /* ! __PAS_BACKEND_SYNC_H__ */
