/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * pas-book-view.c
 *
 * Copyright 2000, Ximian, Inc.
 */

#include <config.h>
#include <glib.h>
#include <bonobo/bonobo-main.h>
#include "pas-backend.h"
#include "pas-backend-card-sexp.h"
#include "pas-book-view.h"

static BonoboObjectClass *pas_book_view_parent_class;

struct _PASBookViewPrivate {
	GNOME_Evolution_Addressbook_BookViewListener  listener;

#define INITIAL_THRESHOLD 20
	GMutex *pending_mutex;
	int     card_count;
	int     card_threshold;
	int     card_threshold_max;
	GList  *cards;

	PASBackend *backend;
	char *card_query;
	PASBackendCardSExp *card_sexp;
};

static void
send_pending_adds (PASBookView *book_view)
{
	CORBA_Environment ev;
	gint i;
	CORBA_sequence_GNOME_Evolution_Addressbook_VCard card_sequence;
	GList *cards = book_view->priv->cards;
	int card_count = book_view->priv->card_count;

	printf ("send_pending_adds (%d cards)\n", card_count);

	book_view->priv->cards = NULL;
	book_view->priv->card_count = 0;
	book_view->priv->card_threshold = INITIAL_THRESHOLD;

	card_sequence._buffer = CORBA_sequence_GNOME_Evolution_Addressbook_VCard_allocbuf(card_count);
	card_sequence._maximum = card_count;
	card_sequence._length = card_count;

	for ( i = 0; cards; cards = g_list_next(cards), i++ )
		card_sequence._buffer[i] = CORBA_string_dup((char *) cards->data);

	CORBA_exception_init (&ev);

	GNOME_Evolution_Addressbook_BookViewListener_notifyContactsAdded (
		book_view->priv->listener, &card_sequence, &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {
		g_warning ("send_pending_adds: Exception signaling BookViewListener!\n");
	}

	CORBA_exception_free (&ev);

	CORBA_free(card_sequence._buffer);
	g_list_foreach (cards, (GFunc)g_free, NULL);
	g_list_free (cards);
}

/**
 * pas_book_view_notify_change:
 */
void
pas_book_view_notify_change (PASBookView                *book_view,
			     const GList                *cards)
{
	CORBA_Environment ev;
	gint i, length;
	CORBA_sequence_GNOME_Evolution_Addressbook_VCard card_sequence;

	g_mutex_lock (book_view->priv->pending_mutex);

	if (book_view->priv->cards)
		send_pending_adds (book_view);

	g_mutex_unlock (book_view->priv->pending_mutex);

	length = g_list_length((GList *) cards);

	card_sequence._buffer = CORBA_sequence_GNOME_Evolution_Addressbook_VCard_allocbuf(length);
	card_sequence._maximum = length;
	card_sequence._length = length;

	for ( i = 0; cards; cards = g_list_next(cards), i++ )
		card_sequence._buffer[i] = CORBA_string_dup((char *) cards->data);

	CORBA_exception_init (&ev);

	GNOME_Evolution_Addressbook_BookViewListener_notifyContactsChanged (
		book_view->priv->listener, &card_sequence, &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {
		g_warning ("pas_book_view_notify_change: Exception signaling BookViewListener!\n");
	}

	CORBA_exception_free (&ev);

	CORBA_free(card_sequence._buffer);
}

void
pas_book_view_notify_change_1 (PASBookView *book_view,
			       const char  *card)
{
	GList *list = g_list_append(NULL, (char *) card);
	pas_book_view_notify_change(book_view, list);
	g_list_free(list);
}

/**
 * pas_book_view_notify_remove:
 */
void
pas_book_view_notify_remove (PASBookView  *book_view,
			     const GList  *ids)
{
	GNOME_Evolution_Addressbook_ContactIdList idlist;
	CORBA_Environment ev;
	const GList *l;
	int num_ids, i;

	g_mutex_lock (book_view->priv->pending_mutex);

	if (book_view->priv->cards)
		send_pending_adds (book_view);

	g_mutex_unlock (book_view->priv->pending_mutex);

	num_ids = g_list_length ((GList*)ids);
	idlist._buffer = CORBA_sequence_GNOME_Evolution_Addressbook_ContactId_allocbuf (num_ids);
	idlist._maximum = num_ids;
	idlist._length = num_ids;

	for (l = ids, i = 0; l; l=l->next, i ++)
		idlist._buffer[i] = CORBA_string_dup (l->data);

	CORBA_exception_init (&ev);

	GNOME_Evolution_Addressbook_BookViewListener_notifyContactsRemoved (
		book_view->priv->listener, &idlist, &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {
		g_warning ("pas_book_view_notify_remove: Exception signaling BookViewListener!\n");
	}

	CORBA_free(idlist._buffer);

	CORBA_exception_free (&ev);
}

void
pas_book_view_notify_remove_1 (PASBookView                *book_view,
			       const char                 *id)
{
	GList *ids = NULL;

	ids = g_list_prepend (ids, (char*)id);

	pas_book_view_notify_remove (book_view, ids);

	g_list_free (ids);
}

/**
 * pas_book_view_notify_add:
 */
void
pas_book_view_notify_add (PASBookView          *book_view,
			  GList                *cards)
{
	PASBookViewPrivate *priv = book_view->priv;

	g_mutex_lock (book_view->priv->pending_mutex);

	priv->card_count += g_list_length (cards);
	priv->cards = g_list_concat (cards, priv->cards);

	/* If we've accumulated a number of cards, actually send them to the client */
	if (priv->card_count >= priv->card_threshold) {
		send_pending_adds (book_view);

		/* Yeah, this scheme is overly complicated.  But I like it. */
		if (priv->card_threshold < priv->card_threshold_max)
			priv->card_threshold = MIN (2*priv->card_threshold, priv->card_threshold_max);
	}

	g_mutex_unlock (book_view->priv->pending_mutex);
}

void
pas_book_view_notify_add_1 (PASBookView *book_view,
			    const char  *card)
{
	GList *list = g_list_append(NULL, (char *) card);
	pas_book_view_notify_add(book_view, list);
}

void
pas_book_view_notify_complete (PASBookView *book_view,
			       GNOME_Evolution_Addressbook_CallStatus status)
{
	CORBA_Environment ev;

	g_mutex_lock (book_view->priv->pending_mutex);

	if (book_view->priv->cards)
		send_pending_adds (book_view);

	g_mutex_unlock (book_view->priv->pending_mutex);

	CORBA_exception_init (&ev);

	GNOME_Evolution_Addressbook_BookViewListener_notifySequenceComplete (
		book_view->priv->listener, status, &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {
		g_warning ("pas_book_view_notify_complete: Exception signaling BookViewListener!\n");
	}

	CORBA_exception_free (&ev);
}

void
pas_book_view_notify_status_message (PASBookView *book_view,
				     const char  *message)
{
	CORBA_Environment ev;

	CORBA_exception_init (&ev);

	GNOME_Evolution_Addressbook_BookViewListener_notifyProgress (
		book_view->priv->listener, message, 0, &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {
		g_warning ("pas_book_view_notify_status_message: Exception signaling BookViewListener!\n");
	}

	CORBA_exception_free (&ev);
}

gboolean
pas_book_view_vcard_matches (PASBookView *book_view,
			     const char  *vcard)
{
	if (book_view->priv->card_sexp == NULL)
		return FALSE;

	return pas_backend_card_sexp_match_vcard (book_view->priv->card_sexp, vcard);
}

static void
pas_book_view_construct (PASBookView                *book_view,
			 PASBackend                 *backend,
			 GNOME_Evolution_Addressbook_BookViewListener  listener,
			 const char                 *card_query,
			 PASBackendCardSExp         *card_sexp)
{
	PASBookViewPrivate *priv;
	CORBA_Environment ev;

	g_return_if_fail (book_view != NULL);
	g_return_if_fail (listener != CORBA_OBJECT_NIL);

	priv = book_view->priv;

	CORBA_exception_init (&ev);

	priv->listener = CORBA_Object_duplicate (listener, &ev);
	if (ev._major != CORBA_NO_EXCEPTION) {
		g_warning("Unable to duplicate listener object in pas-book-view.c\n");
		CORBA_exception_free (&ev);
		return;
	}

	CORBA_exception_free (&ev);

	priv->backend = backend;
	priv->cards = NULL;
	priv->card_threshold = INITIAL_THRESHOLD;
	priv->card_threshold_max = 3000;
	priv->card_query = g_strdup (card_query);
	priv->card_sexp = card_sexp;
	priv->pending_mutex = g_mutex_new();
}

/**
 * pas_book_view_new:
 */
static void
impl_GNOME_Evolution_Addressbook_BookView_start (PortableServer_Servant servant,
						 CORBA_Environment *ev)
{
	PASBookView *view = PAS_BOOK_VIEW (bonobo_object (servant));

	pas_backend_start_book_view (pas_book_view_get_backend (view), view);
}

/**
 * pas_book_view_get_card_query
 */
const char*
pas_book_view_get_card_query (PASBookView *book_view)
{
	return book_view->priv->card_query;
}

/**
 * pas_book_view_get_card_sexp
 */
PASBackendCardSExp*
pas_book_view_get_card_sexp (PASBookView *book_view)
{
	return book_view->priv->card_sexp;
}

PASBackend*
pas_book_view_get_backend (PASBookView *book_view)
{
	return book_view->priv->backend;
}

/**
 * pas_book_view_new:
 */
PASBookView *
pas_book_view_new (PASBackend *backend,
		   GNOME_Evolution_Addressbook_BookViewListener  listener,
		   const char *card_query,
		   PASBackendCardSExp *card_sexp)
{
	PASBookView *book_view;

	book_view = g_object_new (PAS_TYPE_BOOK_VIEW, NULL);
	
	pas_book_view_construct (book_view, backend, listener, card_query, card_sexp);

	return book_view;
}

static void
pas_book_view_dispose (GObject *object)
{
	PASBookView *book_view = PAS_BOOK_VIEW (object);

	if (book_view->priv) {
		bonobo_object_release_unref (book_view->priv->listener, NULL);

		g_list_foreach (book_view->priv->cards, (GFunc)g_free, NULL);
		g_list_free (book_view->priv->cards);

		g_free (book_view->priv->card_query);
		g_object_unref (book_view->priv->card_sexp);

		g_mutex_free (book_view->priv->pending_mutex);
		book_view->priv->pending_mutex = NULL;

		g_free (book_view->priv);
		book_view->priv = NULL;
	}

	if (G_OBJECT_CLASS (pas_book_view_parent_class)->dispose)
		G_OBJECT_CLASS (pas_book_view_parent_class)->dispose (object);	
}

static void
pas_book_view_class_init (PASBookViewClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	POA_GNOME_Evolution_Addressbook_BookView__epv *epv;

	pas_book_view_parent_class = g_type_class_peek_parent (klass);

	object_class->dispose = pas_book_view_dispose;

	epv = &klass->epv;

	epv->start                = impl_GNOME_Evolution_Addressbook_BookView_start;

}

static void
pas_book_view_init (PASBookView *book_view)
{
	book_view->priv           = g_new0 (PASBookViewPrivate, 1);
	book_view->priv->listener = CORBA_OBJECT_NIL;
}

BONOBO_TYPE_FUNC_FULL (
		       PASBookView,
		       GNOME_Evolution_Addressbook_BookView,
		       BONOBO_TYPE_OBJECT,
		       pas_book_view);
