/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * pas-book-view.c
 *
 * Copyright 2000, Ximian, Inc.
 */

#include <config.h>
#include <glib.h>
#include "pas-book-view.h"

static BonoboObjectClass *pas_book_view_parent_class;
POA_GNOME_Evolution_Addressbook_BookView__vepv pas_book_view_vepv;

struct _PASBookViewPrivate {
	GNOME_Evolution_Addressbook_BookViewListener  listener;
};

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

	length = g_list_length((GList *) cards);

	card_sequence._buffer = CORBA_sequence_GNOME_Evolution_Addressbook_VCard_allocbuf(length);
	card_sequence._maximum = length;
	card_sequence._length = length;

	for ( i = 0; cards; cards = g_list_next(cards), i++ ) {
		card_sequence._buffer[i] = CORBA_string_dup((char *) cards->data);
	}

	CORBA_exception_init (&ev);

	GNOME_Evolution_Addressbook_BookViewListener_notifyCardChanged (
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
pas_book_view_notify_remove (PASBookView                *book_view,
			     const char                 *id)
{
	CORBA_Environment ev;

	CORBA_exception_init (&ev);

	GNOME_Evolution_Addressbook_BookViewListener_notifyCardRemoved (
		book_view->priv->listener, (GNOME_Evolution_Addressbook_CardId) id, &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {
		g_warning ("pas_book_view_notify_remove: Exception signaling BookViewListener!\n");
	}

	CORBA_exception_free (&ev);
}

/**
 * pas_book_view_notify_add:
 */
void
pas_book_view_notify_add (PASBookView                *book_view,
			  const GList                *cards)
{
	CORBA_Environment ev;
	gint i, length;
	CORBA_sequence_GNOME_Evolution_Addressbook_VCard card_sequence;

	length = g_list_length((GList *)cards);

	card_sequence._buffer = CORBA_sequence_GNOME_Evolution_Addressbook_VCard_allocbuf(length);
	card_sequence._maximum = length;
	card_sequence._length = length;

	for ( i = 0; cards; cards = g_list_next(cards), i++ ) {
		card_sequence._buffer[i] = CORBA_string_dup((char *) cards->data);
	}

	CORBA_exception_init (&ev);

	GNOME_Evolution_Addressbook_BookViewListener_notifyCardAdded (
		book_view->priv->listener, &card_sequence, &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {
		g_warning ("pas_book_view_notify_add: Exception signaling BookViewListener!\n");
	}

	CORBA_exception_free (&ev);

	CORBA_free(card_sequence._buffer);
}

void
pas_book_view_notify_add_1 (PASBookView *book_view,
			    const char  *card)
{
	GList *list = g_list_append(NULL, (char *) card);
	pas_book_view_notify_add(book_view, list);
	g_list_free(list);
}

void
pas_book_view_notify_complete (PASBookView *book_view)
{
	CORBA_Environment ev;

	CORBA_exception_init (&ev);

	GNOME_Evolution_Addressbook_BookViewListener_notifySequenceComplete (
		book_view->priv->listener, &ev);

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

	GNOME_Evolution_Addressbook_BookViewListener_notifyStatusMessage (
		book_view->priv->listener, message, &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {
		g_warning ("pas_book_view_notify_status_message: Exception signaling BookViewListener!\n");
	}

	CORBA_exception_free (&ev);
}

static gboolean
pas_book_view_construct (PASBookView                *book_view,
			 GNOME_Evolution_Addressbook_BookViewListener  listener)
{
	POA_GNOME_Evolution_Addressbook_BookView *servant;
	CORBA_Environment   ev;
	CORBA_Object        obj;

	g_assert (book_view      != NULL);
	g_assert (PAS_IS_BOOK_VIEW (book_view));
	g_assert (listener  != CORBA_OBJECT_NIL);

	servant = (POA_GNOME_Evolution_Addressbook_BookView *) g_new0 (BonoboObjectServant, 1);
	servant->vepv = &pas_book_view_vepv;

	CORBA_exception_init (&ev);

	POA_GNOME_Evolution_Addressbook_BookView__init ((PortableServer_Servant) servant, &ev);
	if (ev._major != CORBA_NO_EXCEPTION) {
		g_free (servant);
		CORBA_exception_free (&ev);

		return FALSE;
	}

	bonobo_object_dup_ref (listener, &ev);
	if (ev._major != CORBA_NO_EXCEPTION) {
		g_warning("Unable to duplicate & ref listener object in pas-book-view.c\n");
		CORBA_exception_free (&ev);

		return FALSE;
	}

	CORBA_exception_free (&ev);

	obj = bonobo_object_activate_servant (BONOBO_OBJECT (book_view), servant);
	if (obj == CORBA_OBJECT_NIL) {
		g_free (servant);

		return FALSE;
	}

	bonobo_object_construct (BONOBO_OBJECT (book_view), obj);

	book_view->priv->listener  = listener;

	return TRUE;
}

/**
 * pas_book_view_new:
 */
PASBookView *
pas_book_view_new (GNOME_Evolution_Addressbook_BookViewListener  listener)
{
	PASBookView *book_view;

	g_return_val_if_fail (listener  != CORBA_OBJECT_NIL, NULL);

	book_view = gtk_type_new (pas_book_view_get_type ());

	if (! pas_book_view_construct (book_view, listener)) {
		gtk_object_unref (GTK_OBJECT (book_view));

		return NULL;
	}

	return book_view;
}

static void
pas_book_view_destroy (GtkObject *object)
{
	PASBookView *book_view = PAS_BOOK_VIEW (object);
	CORBA_Environment   ev;

	CORBA_exception_init (&ev);
	bonobo_object_release_unref (book_view->priv->listener, &ev);
	if (ev._major != CORBA_NO_EXCEPTION) {
		CORBA_exception_free (&ev);

		return;
	}
	CORBA_exception_free (&ev);

	g_free (book_view->priv);

	GTK_OBJECT_CLASS (pas_book_view_parent_class)->destroy (object);	
}

static POA_GNOME_Evolution_Addressbook_BookView__epv *
pas_book_view_get_epv (void)
{
	POA_GNOME_Evolution_Addressbook_BookView__epv *epv;

	epv = g_new0 (POA_GNOME_Evolution_Addressbook_BookView__epv, 1);

	return epv;
	
}

static void
pas_book_view_corba_class_init (void)
{
	pas_book_view_vepv.Bonobo_Unknown_epv     = bonobo_object_get_epv ();
	pas_book_view_vepv.GNOME_Evolution_Addressbook_BookView_epv = pas_book_view_get_epv ();
}

static void
pas_book_view_class_init (PASBookViewClass *klass)
{
	GtkObjectClass *object_class = (GtkObjectClass *) klass;

	pas_book_view_parent_class = gtk_type_class (bonobo_object_get_type ());

	object_class->destroy = pas_book_view_destroy;

	pas_book_view_corba_class_init ();
}

static void
pas_book_view_init (PASBookView *book_view)
{
	book_view->priv           = g_new0 (PASBookViewPrivate, 1);
	book_view->priv->listener = CORBA_OBJECT_NIL;
}

/**
 * pas_book_view_get_type:
 */
GtkType
pas_book_view_get_type (void)
{
	static GtkType type = 0;

	if (! type) {
		GtkTypeInfo info = {
			"PASBookView",
			sizeof (PASBookView),
			sizeof (PASBookViewClass),
			(GtkClassInitFunc)  pas_book_view_class_init,
			(GtkObjectInitFunc) pas_book_view_init,
			NULL, /* reserved 1 */
			NULL, /* reserved 2 */
			(GtkClassInitFunc) NULL
		};

		type = gtk_type_unique (bonobo_object_get_type (), &info);
	}

	return type;
}

