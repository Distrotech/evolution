/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * A wrapper object which exports the GNOME_Evolution_Addressbook_Book CORBA interface
 * and which maintains a request queue.
 *
 * Author:
 *   Nat Friedman (nat@ximian.com)
 *
 * Copyright 2000, Ximian, Inc.
 */

#ifndef __PAS_BOOK_H__
#define __PAS_BOOK_H__

#include <bonobo/bonobo-object.h>
#include <libgnome/gnome-defs.h>
#include <pas/addressbook.h>
#include <pas/pas-book-view.h>
#include "e-util/e-list.h"

typedef struct _PASBook        PASBook;
typedef struct _PASBookPrivate PASBookPrivate;

#include <pas/pas-backend.h>
#include <pas/pas-card-cursor.h>

typedef enum {
	CreateCard,
	RemoveCard,
	ModifyCard,
	GetVCard,
	GetCursor,
	GetBookView,
	GetChanges,
	CheckConnection,
	AuthenticateUser,
	GetSupportedFields
} PASOperation;

typedef struct {
	PASOperation               op;
	char                      *id;
	char                      *vcard;
	char                      *search;
	char                      *change_id;
	char                      *user;
        char                      *passwd;
	GNOME_Evolution_Addressbook_BookViewListener listener;
	GNOME_Evolution_Addressbook_stringlist fields;
} PASRequest;

struct _PASBook {
	BonoboObject     parent_object;
	PASBookPrivate *priv;
};

typedef struct {
	BonoboObjectClass parent_class;

	/* Signals */
	void (*requests_queued) (void);
} PASBookClass;

typedef gboolean (*PASBookCanWriteFn)     (PASBook *book);
typedef gboolean (*PASBookCanWriteCardFn) (PASBook *book, const char *id);

PASBook                *pas_book_new                    (PASBackend                        *backend,
							 GNOME_Evolution_Addressbook_BookListener             listener);
PASBackend             *pas_book_get_backend            (PASBook                           *book);
GNOME_Evolution_Addressbook_BookListener  pas_book_get_listener           (PASBook                           *book);
int                     pas_book_check_pending          (PASBook                           *book);
PASRequest             *pas_book_pop_request            (PASBook                           *book);
void                    pas_book_respond_open           (PASBook                           *book,
							 GNOME_Evolution_Addressbook_BookListener_CallStatus  status);
void                    pas_book_respond_create         (PASBook                           *book,
							 GNOME_Evolution_Addressbook_BookListener_CallStatus  status,
							 const char                        *id);
void                    pas_book_respond_remove         (PASBook                           *book,
							 GNOME_Evolution_Addressbook_BookListener_CallStatus  status);
void                    pas_book_respond_modify         (PASBook                           *book,
							 GNOME_Evolution_Addressbook_BookListener_CallStatus  status);
void                    pas_book_respond_authenticate_user (PASBook                           *book,
							    GNOME_Evolution_Addressbook_BookListener_CallStatus  status);
void                    pas_book_respond_get_supported_fields (PASBook *book,
							       GNOME_Evolution_Addressbook_BookListener_CallStatus  status,
							       EList   *fields);

void                    pas_book_respond_get_cursor     (PASBook                           *book,
							 GNOME_Evolution_Addressbook_BookListener_CallStatus  status,
							 PASCardCursor                     *cursor);
void                    pas_book_respond_get_book_view  (PASBook                           *book,
							 GNOME_Evolution_Addressbook_BookListener_CallStatus  status,
							 PASBookView                       *book_view);
void                    pas_book_respond_get_vcard      (PASBook                           *book,
							 GNOME_Evolution_Addressbook_BookListener_CallStatus  status,
							 char                              *vcard);
void                    pas_book_respond_get_changes    (PASBook                           *book,
							 GNOME_Evolution_Addressbook_BookListener_CallStatus  status,
							 PASBookView                       *book_view);
void                    pas_book_report_connection      (PASBook                           *book,
							 gboolean                           connected);

void                    pas_book_report_writable        (PASBook                           *book,
							 gboolean                           writable);

GtkType                 pas_book_get_type               (void);

#define PAS_BOOK_TYPE        (pas_book_get_type ())
#define PAS_BOOK(o)          (GTK_CHECK_CAST ((o), PAS_BOOK_TYPE, PASBook))
#define PAS_BOOK_CLASS(k)    (GTK_CHECK_CLASS_CAST((k), PAS_BOOK_FACTORY_TYPE, PASBookClass))
#define PAS_IS_BOOK(o)       (GTK_CHECK_TYPE ((o), PAS_BOOK_TYPE))
#define PAS_IS_BOOK_CLASS(k) (GTK_CHECK_CLASS_TYPE ((k), PAS_BOOK_TYPE))

#endif /* ! __PAS_BOOK_H__ */
