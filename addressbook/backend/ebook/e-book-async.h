/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * The Evolution addressbook client object.
 *
 * Author:
 *   Nat Friedman (nat@ximian.com)
 *
 * Copyright 1999, 2000, Ximian, Inc.
 */

#ifndef __E_BOOK_ASYNC_H__
#define __E_BOOK_ASYNC_H__

#include <glib.h>
#include <glib-object.h>

#include <ebook/e-card.h>
#include <ebook/e-book.h>

G_BEGIN_DECLS

/* #defines to rename things so they don't conflict */
#define e_book_load_uri                   e_book_async_load_uri
#define e_book_unload_uri                 e_book_async_unload_uri
#define e_book_get_supported_fields       e_book_async_get_supported_fields
#define e_book_get_supported_auth_methods e_book_async_get_supported_auth_methods
#define e_book_authenticate_user          e_book_async_authenticate_user
#define e_book_get_card                   e_book_async_get_card
#define e_book_remove_card                e_book_async_remove_card
#define e_book_remove_card_by_id          e_book_async_remove_card_by_id
#define e_book_remove_cards               e_book_async_remove_cards
#define e_book_add_card                   e_book_async_add_card
#define e_book_add_vcard                  e_book_async_add_vcard
#define e_book_commit_card                e_book_async_commit_card
#define e_book_commit_vcard               e_book_async_commit_vcard
#define e_book_get_book_view              e_book_async_get_book_view

/* Callbacks for asynchronous functions. */
typedef void (*EBookCallback) (EBook *book, EBookStatus status, gpointer closure);
typedef void (*EBookOpenProgressCallback)     (EBook          *book,
					       const char     *status_message,
					       short           percent,
					       gpointer        closure);
typedef void (*EBookIdCallback)       (EBook *book, EBookStatus status, const char *id, gpointer closure);
typedef void (*EBookCardCallback)     (EBook *book, EBookStatus status, ECard *card, gpointer closure);
typedef void (*EBookBookViewCallback) (EBook *book, EBookStatus status, EBookView *book_view, gpointer closure);
typedef void (*EBookFieldsCallback)   (EBook *book, EBookStatus status, EList *fields, gpointer closure);
typedef void (*EBookAuthMethodsCallback) (EBook *book, EBookStatus status, EList *auth_methods, gpointer closure);

void      e_book_load_uri                 (EBook                 *book,
					   const char            *uri,
					   EBookCallback          open_response,
					   gpointer               closure);

void      e_book_unload_uri               (EBook                 *book);

guint     e_book_get_supported_fields     (EBook                 *book,
					   EBookFieldsCallback    cb,
					   gpointer               closure);

guint     e_book_get_supported_auth_methods (EBook                    *book,
					     EBookAuthMethodsCallback  cb,
					     gpointer                  closure);

/* User authentication. */
void      e_book_authenticate_user        (EBook                 *book,
					   const char            *user,
					   const char            *passwd,
					   const char            *auth_method,
					   EBookCallback         cb,
					   gpointer              closure);

/* Fetching cards. */
guint     e_book_get_card                 (EBook                 *book,
					   const char            *id,
					   EBookCardCallback      cb,
					   gpointer               closure);

/* Deleting cards. */
gboolean  e_book_remove_card              (EBook                 *book,
					   ECard                 *card,
					   EBookCallback          cb,
					   gpointer               closure);
gboolean  e_book_remove_card_by_id        (EBook                 *book,
					   const char            *id,
					   EBookCallback          cb,
					   gpointer               closure);

gboolean e_book_remove_cards              (EBook                 *book,
					   GList                 *id_list,
					   EBookCallback          cb,
					   gpointer               closure);

/* Adding cards. */
gboolean  e_book_add_card                 (EBook                 *book,
					   ECard                 *card,
					   EBookIdCallback        cb,
					   gpointer               closure);
gboolean  e_book_add_vcard                (EBook                 *book,
					   const char            *vcard,
					   EBookIdCallback        cb,
					   gpointer               closure);

/* Modifying cards. */
gboolean  e_book_commit_card              (EBook                 *book,
					   ECard                 *card,
					   EBookCallback          cb,
					   gpointer               closure);
gboolean  e_book_commit_vcard             (EBook                 *book,
					   const char            *vcard,
					   EBookCallback          cb,
					   gpointer               closure);

guint     e_book_get_book_view            (EBook                 *book,
					   const gchar           *query,
					   EBookBookViewCallback  cb,
					   gpointer               closure);

#ifdef BUILDING_E_BOOK_ASYNC
#undef e_book_load_uri
#undef e_book_unload_uri
#undef e_book_get_supported_fields
#undef e_book_get_supported_auth_methods
#undef e_book_authenticate_user
#undef e_book_get_card
#undef e_book_remove_card
#undef e_book_remove_card_by_id
#undef e_book_remove_cards
#undef e_book_add_card
#undef e_book_add_vcard
#undef e_book_commit_card
#undef e_book_commit_vcard
#undef e_book_get_book_view
#endif /* BUILDING_E_BOOK_ASYNC */

G_END_DECLS

#endif /* ! __E_BOOK_H__ */
