/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * The Evolution addressbook client object.
 *
 * Author:
 *   Chris Toshok (toshok@ximian.com)
 *
 * Copyright (C) 1999-2003, Ximian, Inc.
 */

#ifndef __E_BOOK_H__
#define __E_BOOK_H__

#include <glib.h>
#include <glib-object.h>

#include <ebook/e-contact.h>
#include <ebook/e-book-query.h>
#include <ebook/e-book-view.h>
#include <ebook/e-book-types.h>

#define E_TYPE_BOOK        (e_book_get_type ())
#define E_BOOK(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), E_TYPE_BOOK, EBook))
#define E_BOOK_CLASS(k)    (G_TYPE_CHECK_CLASS_CAST ((k), E_TYPE_BOOK, EBookClass))
#define E_IS_BOOK(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), E_TYPE_BOOK))
#define E_IS_BOOK_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), E_TYPE_BOOK))
#define E_BOOK_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), E_TYPE_BOOK, EBookClass))

G_BEGIN_DECLS

typedef struct _EBook        EBook;
typedef struct _EBookClass   EBookClass;
typedef struct _EBookPrivate EBookPrivate;

struct _EBook {
	GObject       parent;
	EBookPrivate *priv;
};

struct _EBookClass {
	GObjectClass parent;

	/*
	 * Signals.
	 */
	void (* writable_status) (EBook *book, gboolean writable);
	void (* backend_died)    (EBook *book);
};

/* Creating a new addressbook. */
EBook      *e_book_new                   (void);

/* loading arbitrary addressbooks */
EBookStatus e_book_load_uri              (EBook       *book,
					  const char  *uri);

EBookStatus e_book_unload_uri            (EBook       *book);

/* convenience function for loading the "local" contact folder */
EBookStatus e_book_load_local_addressbook (EBook *book);

EBookStatus e_book_get_supported_fields  (EBook       *book,
					  GList      **fields);

EBookStatus e_book_get_supported_auth_methods (EBook       *book,
					       GList      **auth_methods);

/* User authentication. */
EBookStatus e_book_authenticate_user     (EBook       *book,
					  const char  *user,
					  const char  *passwd,
					  const char  *auth_method);

/* Fetching contacts. */
EBookStatus e_book_get_contact           (EBook       *book,
					  const char  *id,
					  EContact   **contact);

/* Deleting contacts. */
EBookStatus e_book_remove_contact        (EBook       *book,
					  const char  *id);

EBookStatus e_book_remove_contacts       (EBook       *book,
					  GList       *id_list);

/* Adding contacts. */
EBookStatus e_book_add_contact           (EBook       *book,
					  EContact    *contact);

/* Modifying contacts. */
EBookStatus e_book_commit_contact        (EBook       *book,
					  EContact       *contact);

EBookStatus e_book_get_book_view         (EBook       *book,
					  EBookQuery  *query,
					  GList       *requested_fields,
					  int          max_results,
					  EBookView  **book_view);

EBookStatus e_book_get_contacts          (EBook       *book,
					  EBookQuery  *query,
					  GList       **contacts);

EBookStatus e_book_get_changes           (EBook       *book,
					  char        *changeid,
					  EBookView  **book_view);

const char *e_book_get_uri               (EBook       *book);

const char *e_book_get_static_capabilities (EBook       *book);
gboolean    e_book_check_static_capability (EBook       *book,
					    const char  *cap);

/* Cancel a pending operation. */
EBookStatus e_book_cancel                  (EBook *book);

GType     e_book_get_type                (void);

G_END_DECLS

#endif /* ! __E_BOOK_H__ */
