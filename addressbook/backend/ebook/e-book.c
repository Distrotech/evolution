/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <config.h>

#include <pthread.h>

#include <string.h>

#include "e-book.h"
#include "e-vcard.h"

#include <bonobo-activation/bonobo-activation.h>

#include "e-book-marshal.h"
#include "e-book-listener.h"
#include "addressbook.h"
#include "e-util/e-component-listener.h"
#include "e-util/e-msgport.h"

static GObjectClass *parent_class;

#define CARDSERVER_OAF_ID "OAFIID:GNOME_Evolution_Wombat_ServerFactory"

enum {
	OPEN_PROGRESS,
	WRITABLE_STATUS,
	BACKEND_DIED,
	LAST_SIGNAL
};

static guint e_book_signals [LAST_SIGNAL];

typedef struct {
	EMutex *mutex;
	gint corba_id;
	pthread_cond_t cond;
	EBookStatus status;

	char *id;
	EList *list;
	ECard *card;

	GNOME_Evolution_Addressbook_Book corba_book;
} EBookOp;

typedef enum {
	URINotLoaded,
	URILoading,
	URILoaded
} EBookLoadState;

struct _EBookPrivate {
	GList *book_factories;
	GList *iter;

	/* cached capabilites */
	char *cap;
	gboolean cap_queried;

	EBookListener	      *listener;
	EComponentListener    *comp_listener;

	GNOME_Evolution_Addressbook_Book         corba_book;

	EBookLoadState         load_state;


	EBookOp *current_op;

	EMutex *mutex;

	gchar *uri;

	gulong listener_signal;
	gulong died_signal;
};



/* EBookOp calls */

static EBookOp*
e_book_new_op (EBook *book)
{
	EBookOp *op = g_new0 (EBookOp, 1);
	op->mutex = e_mutex_new (E_MUTEX_SIMPLE);

	book->priv->current_op = op;

	return op;
}

static EBookOp*
e_book_get_op (EBook *book,
	       int corba_id)
{
	if (!book->priv->current_op) {
		g_warning ("unexpected response");
		return NULL;
	}
		
#if 0
	if (book->priv->current_op->corba_id != corba_id) {
		g_warning ("response is not for current operation");
		return NULL;
	}
#endif
	return book->priv->current_op;
}

static void
e_book_free_op (EBookOp *op)
{
	/* XXX more stuff here */
	e_mutex_destroy (op->mutex);
	g_free (op);
}

static void
e_book_remove_op (EBook *book,
		  EBookOp *op)
{
	if (book->priv->current_op != op)
		g_warning ("cannot remove op, it's not current");

	book->priv->current_op = NULL;
}



/**
 * e_book_add_card:
 * @book: an #EBook
 * @card: an #ECard
 *
 * adds @card to @book.
 *
 * Return value: a #EBookStatus value.
 **/
EBookStatus
e_book_add_card (EBook           *book,
		 ECard           *card)
{
	EBookOp *our_op;
	EBookStatus status;
	CORBA_Environment ev;
	char *vcard_str;

	printf ("e_book_add_card\n");

	g_return_val_if_fail (book && E_IS_BOOK (book), E_BOOK_STATUS_INVALID_ARG);
	g_return_val_if_fail (card && E_IS_CARD (card), E_BOOK_STATUS_INVALID_ARG);

	e_mutex_lock (book->priv->mutex);

	if (book->priv->load_state != URILoaded) {
		e_mutex_unlock (book->priv->mutex);
		return E_BOOK_STATUS_URI_NOT_LOADED;
	}

	if (book->priv->current_op != NULL) {
		e_mutex_unlock (book->priv->mutex);
		return E_BOOK_STATUS_BUSY;
	}

	vcard_str = e_vcard_to_string (E_VCARD (card));

	our_op = e_book_new_op (book);

	e_mutex_lock (our_op->mutex);

	e_mutex_unlock (book->priv->mutex);

	CORBA_exception_init (&ev);

	/* will eventually end up calling e_book_response_add_card */
	our_op->corba_id = GNOME_Evolution_Addressbook_Book_addCard (book->priv->corba_book,
								     (const GNOME_Evolution_Addressbook_VCard) vcard_str, &ev);

	g_free (vcard_str);

	if (ev._major != CORBA_NO_EXCEPTION) {

	  e_mutex_unlock (our_op->mutex);
	  e_book_free_op (our_op);

	  CORBA_exception_free (&ev);

	  return E_BOOK_STATUS_CORBA_EXCEPTION;
	}

	CORBA_exception_free (&ev);

	/* wait for something to happen (both cancellation and a
	   successful response will notity us via our cv */
	e_mutex_cond_wait (&our_op->cond, our_op->mutex);

	status = our_op->status;
	e_card_set_id (card, our_op->id);
	g_free (our_op->id);

	e_mutex_unlock (our_op->mutex);

	e_book_free_op (our_op);

	return status;
}

static void
e_book_response_add_card (EBook       *book,
			  int          corba_id,
			  EBookStatus  status,
			  char        *id)
{
	EBookOp *op;

	printf ("e_book_response_add_card\n");

	op = e_book_get_op (book, corba_id);

	if (op == NULL) {
	  g_warning ("e_book_response_add_card: Cannot find operation ");
	  return;
	}

	e_mutex_lock (op->mutex);

	op->status = status;
	op->id = id;

	pthread_cond_signal (&op->cond);

	e_mutex_unlock (op->mutex);
}



/**
 * e_book_commit_card:
 * @book: an #EBook
 * @card: an #ECard
 *
 * applies the changes made to @card to the stored version in
 * @book.
 *
 * Return value: a #EBookStatus value.
 **/
EBookStatus
e_book_commit_card (EBook           *book,
		    ECard           *card)
{
	EBookOp *our_op;
	EBookStatus status;
	CORBA_Environment ev;
	char *vcard_str;

	g_return_val_if_fail (book && E_IS_BOOK (book), E_BOOK_STATUS_INVALID_ARG);
	g_return_val_if_fail (card && E_IS_CARD (card), E_BOOK_STATUS_INVALID_ARG);

	e_mutex_lock (book->priv->mutex);

	if (book->priv->load_state != URILoaded) {
		e_mutex_unlock (book->priv->mutex);
		return E_BOOK_STATUS_URI_NOT_LOADED;
	}

	if (book->priv->current_op != NULL) {
		e_mutex_unlock (book->priv->mutex);
		return E_BOOK_STATUS_BUSY;
	}

	vcard_str = e_vcard_to_string (E_VCARD (card));

	our_op = e_book_new_op (book);

	e_mutex_lock (our_op->mutex);

	e_mutex_unlock (book->priv->mutex);

	CORBA_exception_init (&ev);

	/* will eventually end up calling _e_book_response_generic */
	our_op->corba_id = GNOME_Evolution_Addressbook_Book_modifyCard (book->priv->corba_book,
									(const GNOME_Evolution_Addressbook_VCard) vcard_str, &ev);

	g_free (vcard_str);

	if (ev._major != CORBA_NO_EXCEPTION) {

	  e_mutex_unlock (our_op->mutex);
	  e_book_free_op (our_op);

	  CORBA_exception_free (&ev);

	  return E_BOOK_STATUS_CORBA_EXCEPTION;
	}

	CORBA_exception_free (&ev);

	/* wait for something to happen (both cancellation and a
	   successful response will notity us via our cv */
	e_mutex_cond_wait (&our_op->cond, our_op->mutex);

	status = our_op->status;
	e_card_set_id (card, our_op->id);
	g_free (our_op->id);

	/* remove the op from the book's hash of operations */
	e_book_remove_op (book, our_op);

	e_mutex_unlock (our_op->mutex);

	e_book_free_op (our_op);

	return status;
}


/**
 * e_book_get_supported_fields:
 * @book: an #EBook
 * @fields: an #EList
 *
 * queries @book for the list of fields it supports.  mostly for use
 * by the contact editor so it knows what fields to sensitize.
 *
 * Return value: a #EBookStatus value.
 **/
EBookStatus
e_book_get_supported_fields  (EBook            *book,
			      EList           **fields)
{
	EBookOp *our_op;
	EBookStatus status;
	CORBA_Environment ev;

	g_return_val_if_fail (book && E_IS_BOOK (book), E_BOOK_STATUS_INVALID_ARG);
	g_return_val_if_fail (fields,                   E_BOOK_STATUS_INVALID_ARG);

	e_mutex_lock (book->priv->mutex);

	if (book->priv->load_state != URILoaded) {
		e_mutex_unlock (book->priv->mutex);
		return E_BOOK_STATUS_URI_NOT_LOADED;
	}

	if (book->priv->current_op != NULL) {
		e_mutex_unlock (book->priv->mutex);
		return E_BOOK_STATUS_BUSY;
	}

	our_op = e_book_new_op (book);

	e_mutex_lock (our_op->mutex);

	e_mutex_unlock (book->priv->mutex);

	CORBA_exception_init (&ev);

	/* will eventually end up calling
	   _e_book_response_get_supported_fields */
	our_op->corba_id = GNOME_Evolution_Addressbook_Book_getSupportedFields(book->priv->corba_book, &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {

		e_mutex_unlock (our_op->mutex);
		e_book_free_op (our_op);

		CORBA_exception_free (&ev);

		return E_BOOK_STATUS_CORBA_EXCEPTION;
	}


	CORBA_exception_free (&ev);

	/* wait for something to happen (both cancellation and a
	   successful response will notity us via our cv */
	e_mutex_cond_wait (&our_op->cond, our_op->mutex);

	status = our_op->status;
	*fields = our_op->list;

	e_mutex_unlock (our_op->mutex);

	e_book_free_op (our_op);

	return status;
}

static void
e_book_response_get_supported_fields (EBook       *book,
				       int          corba_id,
				       EBookStatus  status,
				       EList       *fields)
{
	EBookOp *op;

	op = e_book_get_op (book, corba_id);

	if (op == NULL) {
	  g_warning ("e_book_response_get_supported_fields: Cannot find operation ");
	  return;
	}

	e_mutex_lock (op->mutex);

	op->status = status;
	op->list = fields;

	pthread_cond_signal (&op->cond);

	e_mutex_unlock (op->mutex);
}


/**
 * e_book_get_supported_auth_methods:
 * @book: an #EBook
 * @auth_methods: an #EList
 *
 * queries @book for the list of authentication methods it supports.
 *
 * Return value: a #EBookStatus value.
 **/
EBookStatus
e_book_get_supported_auth_methods (EBook            *book,
				   EList           **auth_methods)
{
	EBookOp *our_op;
	EBookStatus status;
	CORBA_Environment ev;

	g_return_val_if_fail (book && E_IS_BOOK (book), E_BOOK_STATUS_INVALID_ARG);
	g_return_val_if_fail (auth_methods,             E_BOOK_STATUS_INVALID_ARG);

	e_mutex_lock (book->priv->mutex);

	if (book->priv->load_state != URILoaded) {
		e_mutex_unlock (book->priv->mutex);
		return E_BOOK_STATUS_URI_NOT_LOADED;
	}

	if (book->priv->current_op != NULL) {
		e_mutex_unlock (book->priv->mutex);
		return E_BOOK_STATUS_BUSY;
	}

	our_op = e_book_new_op (book);

	e_mutex_lock (our_op->mutex);

	e_mutex_unlock (book->priv->mutex);

	CORBA_exception_init (&ev);

	/* will eventually end up calling
	   e_book_response_get_supported_fields */
	our_op->corba_id = GNOME_Evolution_Addressbook_Book_getSupportedAuthMethods(book->priv->corba_book, &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {

		e_mutex_unlock (our_op->mutex);
		e_book_free_op (our_op);

		CORBA_exception_free (&ev);

		return E_BOOK_STATUS_CORBA_EXCEPTION;
	}


	CORBA_exception_free (&ev);

	/* wait for something to happen (both cancellation and a
	   successful response will notity us via our cv */
	e_mutex_cond_wait (&our_op->cond, our_op->mutex);

	status = our_op->status;
	*auth_methods = our_op->list;

	e_mutex_unlock (our_op->mutex);

	e_book_free_op (our_op);

	return status;
}

static void
e_book_response_get_supported_auth_methods (EBook                 *book,
					    int                    corba_id,
					    EBookStatus            status,
					    EList                 *auth_methods)
{
	EBookOp *op;

	op = e_book_get_op (book, corba_id);

	if (op == NULL) {
	  g_warning ("e_book_response_get_supported_auth_methods: Cannot find operation ");
	  return;
	}

	e_mutex_lock (op->mutex);

	op->status = status;
	op->list = auth_methods;

	pthread_cond_signal (&op->cond);

	e_mutex_unlock (op->mutex);
}



/**
 * e_book_authenticate_user:
 * @book: an #EBook
 * @user: a string
 * @passwd: a string
 * @auth_method: a string
 *
 * authenticates @user with @passwd, using the auth method
 * @auth_method.  @auth_method must be one of the authentication
 * methods returned using e_book_get_supported_auth_methods.
 *
 * Return value: a #EBookStatus value.
 **/
EBookStatus
e_book_authenticate_user (EBook         *book,
			  const char    *user,
			  const char    *passwd,
			  const char    *auth_method)
{
	EBookOp *our_op;
	EBookStatus status;
	CORBA_Environment ev;

	g_return_val_if_fail (book && E_IS_BOOK (book), E_BOOK_STATUS_INVALID_ARG);
	g_return_val_if_fail (user,                     E_BOOK_STATUS_INVALID_ARG);
	g_return_val_if_fail (passwd,                   E_BOOK_STATUS_INVALID_ARG);
	g_return_val_if_fail (auth_method,              E_BOOK_STATUS_INVALID_ARG);

	e_mutex_lock (book->priv->mutex);

	if (book->priv->load_state != URILoaded) {
		e_mutex_unlock (book->priv->mutex);
		return E_BOOK_STATUS_URI_NOT_LOADED;
	}

	if (book->priv->current_op != NULL) {
		e_mutex_unlock (book->priv->mutex);
		return E_BOOK_STATUS_BUSY;
	}

	our_op = e_book_new_op (book);

	e_mutex_lock (our_op->mutex);

	e_mutex_unlock (book->priv->mutex);

	CORBA_exception_init (&ev);

	/* will eventually end up calling
	   e_book_response_generic */
	our_op->corba_id = GNOME_Evolution_Addressbook_Book_authenticateUser (book->priv->corba_book,
									      user, passwd,
									      auth_method,
									      &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {

		e_mutex_unlock (our_op->mutex);
		e_book_free_op (our_op);

		CORBA_exception_free (&ev);

		return E_BOOK_STATUS_CORBA_EXCEPTION;
	}

	CORBA_exception_free (&ev);

	/* wait for something to happen (both cancellation and a
	   successful response will notity us via our cv */
	e_mutex_cond_wait (&our_op->cond, our_op->mutex);

	status = our_op->status;

	e_mutex_unlock (our_op->mutex);

	e_book_free_op (our_op);

	return status;
}


/**
 * e_book_get_card:
 * @book: an #EBook
 * @id: a string
 * @card: an #ECard
 *
 * Fills in @card with the contents of the vcard in @book
 * corresponding to @id.
 *
 * Return value: a #EBookStatus value.
 **/
EBookStatus 
e_book_get_card (EBook       *book,
		 const char  *id,
		 ECard      **card)
{
	EBookOp *our_op;
	EBookStatus status;
	CORBA_Environment ev;

	g_return_val_if_fail (book && E_IS_BOOK (book), E_BOOK_STATUS_INVALID_ARG);
	g_return_val_if_fail (id,                       E_BOOK_STATUS_INVALID_ARG);
	g_return_val_if_fail (card,                     E_BOOK_STATUS_INVALID_ARG);

	e_mutex_lock (book->priv->mutex);

	if (book->priv->load_state != URILoaded) {
		e_mutex_unlock (book->priv->mutex);
		return E_BOOK_STATUS_URI_NOT_LOADED;
	}

	if (book->priv->current_op != NULL) {
		e_mutex_unlock (book->priv->mutex);
		return E_BOOK_STATUS_BUSY;
	}

	our_op = e_book_new_op (book);

	e_mutex_lock (our_op->mutex);

	e_mutex_unlock (book->priv->mutex);

	CORBA_exception_init (&ev);

	/* will eventually end up calling e_book_response_generic */
	our_op->corba_id = GNOME_Evolution_Addressbook_Book_getVCard (book->priv->corba_book,
								      (const GNOME_Evolution_Addressbook_VCard) id, &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {

		e_mutex_unlock (our_op->mutex);
		e_book_free_op (our_op);

		CORBA_exception_free (&ev);

		return E_BOOK_STATUS_CORBA_EXCEPTION;
	}

	CORBA_exception_free (&ev);

	/* wait for something to happen (both cancellation and a
	   successful response will notity us via our cv */
	e_mutex_cond_wait (&our_op->cond, our_op->mutex);

	status = our_op->status;
	*card = our_op->card;

	e_mutex_unlock (our_op->mutex);

	e_book_free_op (our_op);

	return status;
}

static void
e_book_response_get_card (EBook       *book,
			  int          corba_id,
			  EBookStatus  status,
			  char        *vcard)
{

	EBookOp *op;

	op = e_book_get_op (book, corba_id);

	if (op == NULL) {
	  g_warning ("e_book_response_get_card: Cannot find operation ");
	  return;
	}

	e_mutex_lock (op->mutex);

	op->status = status;
	op->card = e_card_new (vcard);

	if (op->card)
		e_card_set_book (op->card, book);

	pthread_cond_signal (&op->cond);

	e_mutex_unlock (op->mutex);
}


/**
 * e_book_remove_card_by_id:
 * @book: an #EBook
 * @id: a string
 *
 * Removes the card with id @id from @book.
 *
 * Return value: a #EBookStatus value.
 **/
EBookStatus
e_book_remove_card (EBook      *book,
		    const char *id)
{
	EList *list;
	gboolean rv;

	g_return_val_if_fail (book && E_IS_BOOK (book), E_BOOK_STATUS_INVALID_ARG);
	g_return_val_if_fail (id,                       E_BOOK_STATUS_INVALID_ARG);

	e_mutex_lock (book->priv->mutex);

	if (book->priv->load_state != URILoaded) {
		e_mutex_unlock (book->priv->mutex);
		return E_BOOK_STATUS_URI_NOT_LOADED;
	}

	if (book->priv->current_op != NULL) {
		e_mutex_unlock (book->priv->mutex);
		return E_BOOK_STATUS_BUSY;
	}

	e_mutex_lock (book->priv->mutex);

	list = e_list_new ((EListCopyFunc)g_strdup, (EListFreeFunc)g_free, NULL);

	e_list_append (list, id);
	
	rv = e_book_remove_cards (book, list);

	g_object_unref (list);

	return rv;
}

/**
 * e_book_remove_cards:
 * @book: an #EBook
 * @ids: an #EList of const char *id's
 *
 * Removes the cards with ids from the list @ids from @book.  This is
 * always more efficient than calling e_book_remove_card_by_id if you
 * have more than one id to remove, as some backends can implement it
 * as a batch request.
 *
 * Return value: a #EBookStatus value.
 **/
EBookStatus
e_book_remove_cards (EBook    *book,
		     EList    *ids)
{
	GNOME_Evolution_Addressbook_CardIdList idlist;
	CORBA_Environment ev;
	EIterator *iter;
	int num_ids, i;
	EBookOp *our_op;
	EBookStatus status;

	g_return_val_if_fail (book && E_IS_BOOK (book),       E_BOOK_STATUS_INVALID_ARG);
	g_return_val_if_fail (ids && e_list_length (ids) > 0, E_BOOK_STATUS_INVALID_ARG);

	e_mutex_lock (book->priv->mutex);

	if (book->priv->load_state != URILoaded) {
		e_mutex_unlock (book->priv->mutex);
		return E_BOOK_STATUS_URI_NOT_LOADED;
	}

	if (book->priv->current_op != NULL) {
		e_mutex_unlock (book->priv->mutex);
		return E_BOOK_STATUS_BUSY;
	}

	our_op = e_book_new_op (book);

	e_mutex_lock (our_op->mutex);

	e_mutex_unlock (book->priv->mutex);

	CORBA_exception_init (&ev);

	num_ids = e_list_length (ids);
	idlist._buffer = CORBA_sequence_GNOME_Evolution_Addressbook_CardId_allocbuf (num_ids);
	idlist._maximum = num_ids;
	idlist._length = num_ids;

	iter = e_list_get_iterator (ids); i = 0;
	while (e_iterator_is_valid (iter)) {
		idlist._buffer[i++] = CORBA_string_dup (e_iterator_get (iter));
		e_iterator_next (iter);
	}

	/* will eventually end up calling e_book_response_generic */
	our_op->corba_id = GNOME_Evolution_Addressbook_Book_removeCards (book->priv->corba_book, &idlist, &ev);

	CORBA_free(idlist._buffer);

	if (ev._major != CORBA_NO_EXCEPTION) {
		e_mutex_unlock (our_op->mutex);
		e_book_free_op (our_op);

		CORBA_exception_free (&ev);

		return E_BOOK_STATUS_CORBA_EXCEPTION;
	}
	
	CORBA_exception_free (&ev);

	/* wait for something to happen (both cancellation and a
	   successful response will notity us via our cv */
	e_mutex_cond_wait (&our_op->cond, our_op->mutex);

	status = our_op->status;

	e_mutex_unlock (our_op->mutex);

	e_book_free_op (our_op);

	return status;
}



/**
 * e_book_get_card_list:
 * @book: an #EBook
 * @query: an #EBookQuery
 *
 * need docs here..
 *
 * Return value: a #EBookStatus value.
 **/
EBookStatus
e_book_get_card_list (EBook       *book,
		      EBookQuery  *query,
		      EList       **cards)
{
	CORBA_Environment ev;
	EBookOp *our_op;
	EBookStatus status;
	char *query_string;

	g_return_val_if_fail (book && E_IS_BOOK (book),       E_BOOK_STATUS_INVALID_ARG);
	g_return_val_if_fail (query,                          E_BOOK_STATUS_INVALID_ARG);
	g_return_val_if_fail (cards,                          E_BOOK_STATUS_INVALID_ARG);

	e_mutex_lock (book->priv->mutex);

	if (book->priv->load_state != URILoaded) {
		e_mutex_unlock (book->priv->mutex);
		return E_BOOK_STATUS_URI_NOT_LOADED;
	}

	if (book->priv->current_op != NULL) {
		e_mutex_unlock (book->priv->mutex);
		return E_BOOK_STATUS_BUSY;
	}

	our_op = e_book_new_op (book);

	e_mutex_lock (our_op->mutex);

	e_mutex_unlock (book->priv->mutex);

	CORBA_exception_init (&ev);

	query_string = e_book_query_to_string (query);

	/* will eventually end up calling e_book_response_get_card_list */
	our_op->corba_id = GNOME_Evolution_Addressbook_Book_getCardList (book->priv->corba_book, query_string, &ev);

	g_free (query_string);

	if (ev._major != CORBA_NO_EXCEPTION) {
		e_mutex_unlock (our_op->mutex);
		e_book_free_op (our_op);

		CORBA_exception_free (&ev);

		g_warning ("corba exception._major = %d\n", ev._major);

		return E_BOOK_STATUS_CORBA_EXCEPTION;
	}
	
	CORBA_exception_free (&ev);

	/* wait for something to happen (both cancellation and a
	   successful response will notity us via our cv */
	e_mutex_cond_wait (&our_op->cond, our_op->mutex);

	status = our_op->status;
	*cards = our_op->list;

	e_mutex_unlock (our_op->mutex);

	e_book_free_op (our_op);

	return status;
}

static void
e_book_response_get_card_list (EBook       *book,
			       int          corba_id,
			       EBookStatus  status,
			       EList       *card_list)
{

	EBookOp *op;

	op = e_book_get_op (book, corba_id);

	if (op == NULL) {
	  g_warning ("e_book_response_get_card_list: Cannot find operation ");
	  return;
	}

	e_mutex_lock (op->mutex);

	op->status = status;
	op->list = card_list;

	pthread_cond_signal (&op->cond);

	e_mutex_unlock (op->mutex);
}



static void
e_book_response_generic (EBook       *book,
			 int          corba_id,
			 EBookStatus  status)
{
	EBookOp *op;

	op = e_book_get_op (book, corba_id);

	if (op == NULL) {
	  g_warning ("e_book_response_generic: Cannot find operation ");
	  return;
	}

	e_mutex_lock (op->mutex);

	op->status = status;

	pthread_cond_signal (&op->cond);

	e_mutex_unlock (op->mutex);
}

/**
 * e_book_cancel:
 * @book: an #EBook
 *
 * Used to cancel an already running operation on @book.  This
 * function makes a synchronous CORBA to the backend telling it to
 * cancel the operation.  If the operation wasn't cancellable (either
 * transiently or permanently) or had already comopleted on the wombat
 * side, this function will return E_BOOK_STATUS_COULD_NOT_CANCEL, and
 * the operation will continue uncancelled.  If the operation could be
 * cancelled, this function will return E_BOOK_STATUS_OK, and the
 * blocked e_book function corresponding to current operation will
 * return with a status of E_BOOK_STATUS_CANCELLED.
 *
 * Return value: a #EBookStatus value.
 **/
EBookStatus
e_book_cancel (EBook *book)
{
	EBookOp *op;
	EBookStatus status, rv;
	CORBA_Environment ev;

	e_mutex_lock (book->priv->mutex);

	if (book->priv->current_op == NULL) {
		e_mutex_unlock (book->priv->mutex);
		return E_BOOK_STATUS_COULD_NOT_CANCEL;
	}

	op = book->priv->current_op;

	e_mutex_lock (op->mutex);

	e_mutex_unlock (book->priv->mutex);

	status = GNOME_Evolution_Addressbook_Book_cancelOperation(book->priv->corba_book, op->corba_id, &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {

		e_mutex_unlock (op->mutex);

		CORBA_exception_free (&ev);

		return E_BOOK_STATUS_CORBA_EXCEPTION;
	}

	CORBA_exception_free (&ev);

	if (status == E_BOOK_STATUS_OK) {
		op->status = E_BOOK_STATUS_CANCELLED;

		pthread_cond_signal (&op->cond);

		rv = E_BOOK_STATUS_OK;
	}
	else
		rv = E_BOOK_STATUS_COULD_NOT_CANCEL;

	e_mutex_unlock (op->mutex);

	return rv;
}


static void
e_book_response_open (EBook       *book,
		      EBookStatus  status,
		      GNOME_Evolution_Addressbook_Book corba_book)
{

	EBookOp *op;

	op = e_book_get_op (book, 0); /* XXX */

	if (op == NULL) {
	  g_warning ("e_book_response_open: Cannot find operation ");
	  return;
	}

	e_mutex_lock (op->mutex);

	op->status = status;
	op->corba_book = corba_book;

	pthread_cond_signal (&op->cond);

	e_mutex_unlock (op->mutex);
}

static void
e_book_handle_response (EBookListener *listener, EBookListenerResponse *resp, EBook *book)
{
	switch (resp->op) {
	case CreateCardResponse:
		e_book_response_add_card (book, resp->corba_id, resp->status, resp->id);
		break;
	case RemoveCardResponse:
	case ModifyCardResponse:
	case AuthenticationResponse:
		e_book_response_generic (book, resp->corba_id, resp->status);
		break;
	case GetCardResponse:
		e_book_response_get_card (book, resp->corba_id, resp->status, resp->vcard);
		break;
	case GetCardListResponse:
		e_book_response_get_card_list (book, resp->corba_id, resp->status, resp->list);
		break;
#if notyet
	case GetBookViewResponse:
		e_book_do_response_get_view(book, resp);
		break;
	case GetChangesResponse:
		e_book_do_response_get_changes(book, resp);
		break;
#endif
	case OpenBookResponse:
		e_book_response_open (book, resp->status, resp->book);
		break;
	case GetSupportedFieldsResponse:
		e_book_response_get_supported_fields (book, resp->corba_id, resp->status, resp->list);
		break;
	case GetSupportedAuthMethodsResponse:
		e_book_response_get_supported_auth_methods (book, resp->corba_id, resp->status, resp->list);
		break;
#if notyet
	case WritableStatusEvent:
		e_book_do_writable_event (book, resp);
		break;
#endif
	default:
		g_error ("EBook: Unknown response code %d!\n",
			 resp->op);
	}
}



EBookStatus
e_book_unload_uri (EBook *book)
{
	CORBA_Environment ev;

	g_return_val_if_fail (book && E_IS_BOOK (book), E_BOOK_STATUS_INVALID_ARG);
	g_return_val_if_fail (book->priv->current_op != NULL, E_BOOK_STATUS_URI_NOT_LOADED);

	/* Release the remote GNOME_Evolution_Addressbook_Book in the PAS. */
	CORBA_exception_init (&ev);

	bonobo_object_release_unref  (book->priv->corba_book, &ev);
	if (ev._major != CORBA_NO_EXCEPTION) {
		g_warning ("e_book_unload_uri: Exception releasing "
			   "remote book interface!\n");
	}

	CORBA_exception_free (&ev);

	e_book_listener_stop (book->priv->listener);
	bonobo_object_unref (BONOBO_OBJECT (book->priv->listener));

	book->priv->listener   = NULL;
	book->priv->load_state = URINotLoaded;

	return E_BOOK_STATUS_OK;
}



/**
 * e_book_load_uri:
 */

static GList *
activate_factories_for_uri (EBook *book, const char *uri)
{
	CORBA_Environment ev;
	Bonobo_ServerInfoList *info_list = NULL;
	int i;
	char *protocol, *query, *colon;
	GList *factories = NULL;

	colon = strchr (uri, ':');
	if (!colon) {
		g_warning ("e_book_load_uri: Unable to determine protocol in the URI\n");
		return FALSE;
	}

	protocol = g_strndup (uri, colon-uri);
	query = g_strdup_printf ("repo_ids.has ('IDL:GNOME/Evolution/BookFactory:1.0')"
				 " AND addressbook:supported_protocols.has ('%s')", protocol
				 );

	CORBA_exception_init (&ev);
	
	info_list = bonobo_activation_query (query, NULL, &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {
		g_warning ("Eeek!  Cannot perform bonobo-activation query for book factories.");
		CORBA_exception_free (&ev);
		goto done;
		return NULL;
	}

	if (info_list->_length == 0) {
		g_warning ("Can't find installed BookFactory that handles protocol '%s'.", protocol);
		CORBA_exception_free (&ev);
		goto done;
	}

	CORBA_exception_free (&ev);

	for (i = 0; i < info_list->_length; i ++) {
		const Bonobo_ServerInfo *info;
		GNOME_Evolution_Addressbook_BookFactory factory;

		info = info_list->_buffer + i;

		factory = bonobo_activation_activate_from_id (info->iid, 0, NULL, NULL);

		if (factory == CORBA_OBJECT_NIL)
			g_warning ("e_book_construct: Could not obtain a handle "
				   "to the Personal Addressbook Server with IID `%s'\n", info->iid);
		else
			factories = g_list_append (factories, factory);
	}

 done:
	if (info_list)
		CORBA_free (info_list);
	g_free (query);
	g_free (protocol);

	return factories;
}

EBookStatus
e_book_load_uri (EBook                     *book,
		 const char                *uri)
{
	GList *factories;
	GList *l;
	EBookStatus rv = E_BOOK_STATUS_PROTOCOL_NOT_SUPPORTED;
	GNOME_Evolution_Addressbook_Book corba_book = CORBA_OBJECT_NIL;

	g_return_val_if_fail (book && E_IS_BOOK (book), E_BOOK_STATUS_INVALID_ARG);
	g_return_val_if_fail (uri,                      E_BOOK_STATUS_INVALID_ARG);

	/* XXX this needs to happen while holding the book's lock i would think... */
	g_return_val_if_fail (book->priv->load_state == URINotLoaded, E_BOOK_STATUS_URI_ALREADY_LOADED);

	/* try to find a list of factories that can handle the protocol */
	if (! (factories = activate_factories_for_uri (book, uri)))
		return E_BOOK_STATUS_PROTOCOL_NOT_SUPPORTED;


	book->priv->load_state = URILoading;

	/*
	 * Create our local BookListener interface.
	 */
	book->priv->listener = e_book_listener_new ();
	if (book->priv->listener == NULL) {
		g_warning ("e_book_load_uri: Could not create EBookListener!\n");
		return E_BOOK_STATUS_OTHER_ERROR; /* XXX need a new status code here */
	}

	book->priv->listener_signal = g_signal_connect (book->priv->listener, "response",
							G_CALLBACK (e_book_handle_response), book);

	g_free (book->priv->uri);
	book->priv->uri = g_strdup (uri);

	for (l = factories; l; l = l->next) {
		GNOME_Evolution_Addressbook_BookFactory factory = l->data;
		EBookOp *our_op;
		CORBA_Environment ev;
		EBookStatus status;

		our_op = e_book_new_op (book);

		e_mutex_lock (our_op->mutex);

		CORBA_exception_init (&ev);

		GNOME_Evolution_Addressbook_BookFactory_openBook (factory, book->priv->uri,
								  bonobo_object_corba_objref (BONOBO_OBJECT (book->priv->listener)),
								  &ev);

		if (ev._major != CORBA_NO_EXCEPTION) {
			e_book_remove_op (book, our_op);
			e_mutex_unlock (our_op->mutex);
			e_book_free_op (our_op);

			CORBA_exception_free (&ev);
			continue;
		}

		CORBA_exception_free (&ev);

		/* wait for something to happen (both cancellation and a
		   successful response will notity us via our cv */
		e_mutex_cond_wait (&our_op->cond, our_op->mutex);

		status = our_op->status;
		corba_book = our_op->corba_book;

		/* remove the op from the book's hash of operations */
		e_book_remove_op (book, our_op);
		e_mutex_unlock (our_op->mutex);
		e_book_free_op (our_op);

		if (status == E_BOOK_STATUS_CANCELLED
		    || status == E_BOOK_STATUS_OK) {
			rv = status;
			break;
		}
	}

	/* free up the factories */
	for (l = factories; l; l = l->next)
		CORBA_Object_release ((CORBA_Object)l->data, NULL);

	if (rv == E_BOOK_STATUS_OK) {
		book->priv->corba_book = corba_book;
		book->priv->load_state = URILoaded;
	}
		
	return rv;
}

EBookStatus
e_book_load_local_addressbook (EBook *book)
{
	char *filename;
	char *uri;
	EBookStatus status;

	filename = g_build_filename (g_get_home_dir(),
				     "evolution/local/Contacts/addressbook.db",
				     NULL);
	uri = g_strdup_printf ("file://%s", filename);

	g_free (filename);
	
	status = e_book_load_uri (book, uri);

	g_free (uri);

	return status;
}



EBook*
e_book_new (void)
{
	return g_object_new (E_TYPE_BOOK, NULL);
}


static void
e_book_init (EBook *book)
{
	book->priv             = g_new0 (EBookPrivate, 1);
	book->priv->load_state = URINotLoaded;
	book->priv->uri        = NULL;
	book->priv->mutex      = e_mutex_new (E_MUTEX_REC);
}

static void
e_book_dispose (GObject *object)
{
	EBook             *book = E_BOOK (object);

	if (book->priv) {
		CORBA_Environment  ev;
		GList *l;

		if (book->priv->load_state == URILoaded)
			e_book_unload_uri (book);

		CORBA_exception_init (&ev);

		for (l = book->priv->book_factories; l; l = l->next) {
			CORBA_Object_release ((CORBA_Object)l->data, &ev);
			if (ev._major != CORBA_NO_EXCEPTION) {
				g_warning ("EBook: Exception while releasing BookFactory\n");

				CORBA_exception_free (&ev);
				CORBA_exception_init (&ev);
			}
		}
		
		CORBA_exception_free (&ev);

		if (book->priv->listener) {
			g_signal_handler_disconnect (book->priv->comp_listener, book->priv->listener_signal);
			bonobo_object_unref (book->priv->listener);
			book->priv->listener = NULL;
		}
		
		if (book->priv->comp_listener) {
			g_signal_handler_disconnect (book->priv->comp_listener, book->priv->died_signal);
			g_object_unref (book->priv->comp_listener);
			book->priv->comp_listener = NULL;
		}

		g_free (book->priv->cap);

		g_free (book->priv->uri);

		g_free (book->priv);
		book->priv = NULL;
	}

	if (G_OBJECT_CLASS (parent_class)->dispose)
		G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
e_book_class_init (EBookClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_ref (G_TYPE_OBJECT);

	e_book_signals [WRITABLE_STATUS] =
		g_signal_new ("writable_status",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (EBookClass, writable_status),
			      NULL, NULL,
			      e_book_marshal_NONE__BOOL,
			      G_TYPE_NONE, 1,
			      G_TYPE_BOOLEAN);

	e_book_signals [BACKEND_DIED] =
		g_signal_new ("backend_died",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (EBookClass, backend_died),
			      NULL, NULL,
			      e_book_marshal_NONE__NONE,
			      G_TYPE_NONE, 0);

	object_class->dispose = e_book_dispose;
}

/**
 * e_book_get_type:
 */
GType
e_book_get_type (void)
{
	static GType type = 0;

	if (! type) {
		GTypeInfo info = {
			sizeof (EBookClass),
			NULL, /* base_class_init */
			NULL, /* base_class_finalize */
			(GClassInitFunc)  e_book_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (EBook),
			0,    /* n_preallocs */
			(GInstanceInitFunc) e_book_init
		};

		type = g_type_register_static (G_TYPE_OBJECT, "EBook", &info, 0);
	}

	return type;
}
