/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Author:
 *   Nat Friedman (nat@ximian.com)
 *
 * Copyright 2000, Ximian, Inc.
 */

#include "config.h"  
#include "pas-backend-file.h"

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <db.h>

#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-i18n.h>

#include <e-util/e-db3-utils.h>

#if DB_VERSION_MAJOR != 3 || \
    DB_VERSION_MINOR != 1 || \
    DB_VERSION_PATCH != 17
#error Including wrong DB3.  Need libdb 3.1.17.
#endif

#include <gtk/gtksignal.h>
#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-util.h>
#include <gal/util/e-util.h>
#include <gal/widgets/e-unicode.h>

#include <ebook/e-card-simple.h>
#include <e-util/e-dbhash.h>
#include <e-util/e-db3-utils.h>
#include "pas-book.h"
#include "pas-card-cursor.h"
#include "pas-backend-card-sexp.h"

#define PAS_BACKEND_FILE_VERSION_NAME "PAS-DB-VERSION"
#define PAS_BACKEND_FILE_VERSION "0.2"

#define PAS_ID_PREFIX "pas-id-"

static PASBackendClass *pas_backend_file_parent_class;
typedef struct _PASBackendFileCursorPrivate PASBackendFileCursorPrivate;
typedef struct _PASBackendFileBookView PASBackendFileBookView;
typedef struct _PASBackendFileSearchContext PASBackendFileSearchContext;
typedef struct _PasBackendFileChangeContext PASBackendFileChangeContext;

struct _PASBackendFilePrivate {
	GList    *clients;
	gboolean  loaded;
	char     *uri;
	DB       *file_db;
	EList    *book_views;
	gboolean  writable;
};

struct _PASBackendFileCursorPrivate {
	PASBackend *backend;
	PASBook    *book;

	GList      *elements;
	guint32    num_elements;
};

struct _PASBackendFileBookView {
	PASBookView                 *book_view;
	gchar                       *search;
	PASBackendCardSExp          *card_sexp;
	gchar                       *change_id;
	PASBackendFileChangeContext *change_context;
};

struct _PasBackendFileChangeContext {
	DB *db;

	GList *add_cards;
	GList *add_ids;
	GList *mod_cards;
	GList *mod_ids;
	GList *del_ids;
};

static PASBackendFileBookView *
pas_backend_file_book_view_copy(const PASBackendFileBookView *book_view, void *closure)
{
	PASBackendFileBookView *new_book_view;
	new_book_view = g_new (PASBackendFileBookView, 1);
	new_book_view->book_view = book_view->book_view;

	new_book_view->search = g_strdup(book_view->search);
	new_book_view->card_sexp = book_view->card_sexp;
	if (new_book_view->card_sexp)
		gtk_object_ref(GTK_OBJECT(new_book_view->card_sexp));
	
	new_book_view->change_id = g_strdup(book_view->change_id);
	if (book_view->change_context) {
		new_book_view->change_context = g_new(PASBackendFileChangeContext, 1);
		new_book_view->change_context->db = book_view->change_context->db;
		new_book_view->change_context->add_cards = book_view->change_context->add_cards;
		new_book_view->change_context->add_ids = book_view->change_context->add_ids;
		new_book_view->change_context->mod_cards = book_view->change_context->mod_cards;
		new_book_view->change_context->mod_ids = book_view->change_context->mod_ids;
		new_book_view->change_context->del_ids = book_view->change_context->del_ids;
	} else
		new_book_view->change_context = NULL;
	
	return new_book_view;
}

static void
pas_backend_file_book_view_free(PASBackendFileBookView *book_view, void *closure)
{
	g_free(book_view->search);
	if (book_view->card_sexp)
		gtk_object_unref (GTK_OBJECT(book_view->card_sexp));

	g_free(book_view->change_id);
	if (book_view->change_context) {
		g_list_foreach (book_view->change_context->add_cards, (GFunc)g_free, NULL);
		g_list_foreach (book_view->change_context->add_ids, (GFunc)g_free, NULL);
		g_list_foreach (book_view->change_context->mod_cards, (GFunc)g_free, NULL);
		g_list_foreach (book_view->change_context->mod_ids, (GFunc)g_free, NULL);
		g_list_foreach (book_view->change_context->del_ids, (GFunc)g_free, NULL);
		g_list_free (book_view->change_context->add_cards);
		g_list_free (book_view->change_context->add_ids);
		g_list_free (book_view->change_context->mod_cards);
		g_list_free (book_view->change_context->mod_ids);
		g_list_free (book_view->change_context->del_ids);
	}
	g_free(book_view->change_context);

	g_free(book_view);
}

static long
get_length(PASCardCursor *cursor, gpointer data)
{
	PASBackendFileCursorPrivate *cursor_data = (PASBackendFileCursorPrivate *) data;

	return cursor_data->num_elements;
}

static char *
get_nth(PASCardCursor *cursor, long n, gpointer data)
{
	PASBackendFileCursorPrivate *cursor_data = (PASBackendFileCursorPrivate *) data;
	GList *nth_item = g_list_nth(cursor_data->elements, n);

	return g_strdup((char*)nth_item->data);
}

static void
cursor_destroy(GtkObject *object, gpointer data)
{
	CORBA_Environment ev;
	GNOME_Evolution_Addressbook_Book corba_book;
	PASBackendFileCursorPrivate *cursor_data = (PASBackendFileCursorPrivate *) data;

	corba_book = bonobo_object_corba_objref(BONOBO_OBJECT(cursor_data->book));

	CORBA_exception_init(&ev);

	GNOME_Evolution_Addressbook_Book_unref(corba_book, &ev);
	
	if (ev._major != CORBA_NO_EXCEPTION) {
		g_warning("cursor_destroy: Exception unreffing "
			  "corba book.\n");
	}

	CORBA_exception_free(&ev);

	g_list_foreach(cursor_data->elements, (GFunc)g_free, NULL);
	g_list_free (cursor_data->elements);

	g_free(cursor_data);
}

static void
view_destroy(GtkObject *object, gpointer data)
{
	PASBook           *book = (PASBook *)data;
	PASBackendFile    *bf;
	EIterator         *iterator;
	gboolean success = FALSE;

	bf = PAS_BACKEND_FILE(pas_book_get_backend(book));
	for (iterator = e_list_get_iterator(bf->priv->book_views); e_iterator_is_valid(iterator); e_iterator_next(iterator)) {
		const PASBackendFileBookView *view = e_iterator_get(iterator);
		if (view->book_view == PAS_BOOK_VIEW(object)) {
			e_iterator_delete(iterator);
			success = TRUE;
			break;
		}
	}
	if (!success)
		g_warning ("Failed to remove from book_views list");
	gtk_object_unref(GTK_OBJECT(iterator));

	bonobo_object_unref(BONOBO_OBJECT(book));
}

static void
string_to_dbt(const char *str, DBT *dbt)
{
	memset (dbt, 0, sizeof (*dbt));
	dbt->data = (void*)str;
	dbt->size = strlen (str) + 1;
}

static char *
pas_backend_file_create_unique_id (char *vcard)
{
	/* use a 32 counter and the 32 bit timestamp to make an id.
	   it's doubtful 2^32 id's will be created in a second, so we
	   should be okay. */
	static guint c = 0;
	return g_strdup_printf (PAS_ID_PREFIX "%08lX%08X", time(NULL), c++);
}

static gboolean
vcard_matches_search (const PASBackendFileBookView *view, char *vcard_string)
{
	/* If this is not a search context view, it doesn't match be default */
	if (view->card_sexp == NULL)
		return FALSE;

	return pas_backend_card_sexp_match_vcard (view->card_sexp, vcard_string);
}

static void
pas_backend_file_search (PASBackendFile  	      *bf,
			 PASBook         	      *book,
			 const PASBackendFileBookView *cnstview)
{
	int     db_error = 0;
	GList   *cards = NULL;
	gint    card_count = 0, card_threshold = 20, card_threshold_max = 1000;
	DB      *db = bf->priv->file_db;
	DBC     *dbc;
	DBT     id_dbt, vcard_dbt;
	int     file_version_name_len;
	PASBackendFileBookView *view = (PASBackendFileBookView *)cnstview;
	gboolean search_needed;

	if (!bf->priv->loaded)
		return;

	search_needed = TRUE;

	if ( ! strcmp (view->search, "(contains \"x-evolution-any-field\" \"\")"))
		search_needed = FALSE;

	if (search_needed)
		pas_book_view_notify_status_message (view->book_view, _("Searching..."));
	else
		pas_book_view_notify_status_message (view->book_view, _("Loading..."));

	if (view->card_sexp)
		gtk_object_unref (GTK_OBJECT(view->card_sexp));

	view->card_sexp = pas_backend_card_sexp_new (view->search);
	
	if (!view->card_sexp) {
		/* need a different error message here. */
		pas_book_view_notify_status_message (view->book_view, _("Error in search expression."));
		pas_book_view_notify_complete (view->book_view);
		return;
	}

	file_version_name_len = strlen (PAS_BACKEND_FILE_VERSION_NAME);

	db_error = db->cursor (db, NULL, &dbc, 0);

	memset (&id_dbt, 0, sizeof (id_dbt));
	memset (&vcard_dbt, 0, sizeof (vcard_dbt));

	if (db_error != 0) {
		g_warning ("pas_backend_file_search: error building list\n");
	} else {
		db_error = dbc->c_get(dbc, &id_dbt, &vcard_dbt, DB_FIRST);

		while (db_error == 0) {

			/* don't include the version in the list of cards */
			if (id_dbt.size != file_version_name_len+1
			    || strcmp (id_dbt.data, PAS_BACKEND_FILE_VERSION_NAME)) {
				char *vcard_string = vcard_dbt.data;

				/* check if the vcard matches the search sexp */
				if ((!search_needed) || vcard_matches_search (view, vcard_string)) {
					cards = g_list_prepend (cards, g_strdup (vcard_string));
				}

				card_count ++;

				/* If we've accumulated a number of checks, pass them off to the client. */
				if (card_count >= card_threshold) {
					cards = g_list_reverse (cards);
					pas_book_view_notify_add (view->book_view, cards);
					/* Clean up the handed-off data. */
					g_list_foreach (cards, (GFunc)g_free, NULL);
					g_list_free (cards);
					cards = NULL;
					card_count = 0;

					/* Yeah, this scheme is overly complicated.  But I like it. */
					if (card_threshold < card_threshold_max) {
						card_threshold = MIN (2*card_threshold, card_threshold_max);
					}
				}
			}

			db_error = dbc->c_get(dbc, &id_dbt, &vcard_dbt, DB_NEXT);
		}
		dbc->c_close (dbc);

		if (db_error != DB_NOTFOUND) {
			g_warning ("pas_backend_file_search: error building list\n");
		}
	}

	if (card_count)
		pas_book_view_notify_add (view->book_view, cards);

	pas_book_view_notify_complete (view->book_view);

	/*
	** It's fine to do this now since the data has been handed off.
	*/
	g_list_foreach (cards, (GFunc)g_free, NULL);
	g_list_free (cards);
}

static void
pas_backend_file_changes_foreach_key (const char *key, gpointer user_data)
{
	PASBackendFileChangeContext *ctx = user_data;
	DB      *db = ctx->db;
	DBT     id_dbt, vcard_dbt;
	int     db_error = 0;
	
	string_to_dbt (key, &id_dbt);
	memset (&vcard_dbt, 0, sizeof (vcard_dbt));
	db_error = db->get (db, NULL, &id_dbt, &vcard_dbt, 0);
	
	if (db_error != 0) {
		char *id = id_dbt.data;
		
		ctx->del_ids = g_list_append (ctx->del_ids, g_strdup (id));
	}
}

static void
pas_backend_file_changes (PASBackendFile  	      *bf,
			  PASBook         	      *book,
			  const PASBackendFileBookView *cnstview)
{
	int     db_error = 0;
	DBT     id_dbt, vcard_dbt;
	char    *filename;
	EDbHash *ehash;
	GList *i, *v;
	DB      *db = bf->priv->file_db;
	DBC *dbc;
	PASBackendFileBookView *view = (PASBackendFileBookView *)cnstview;
	PASBackendFileChangeContext *ctx = cnstview->change_context;

	memset (&id_dbt, 0, sizeof (id_dbt));
	memset (&vcard_dbt, 0, sizeof (vcard_dbt));

	if (!bf->priv->loaded)
		return;

	/* Find the changed ids - FIX ME, path should not be hard coded */
	filename = g_strdup_printf ("%s/evolution/local/Contacts/%s.db", g_get_home_dir (), view->change_id);
	ehash = e_dbhash_new (filename);
	g_free (filename);
	
	db_error = db->cursor (db, NULL, &dbc, 0);

	if (db_error != 0) {
		g_warning ("pas_backend_file_changes: error building list\n");
	} else {
		db_error = dbc->c_get(dbc, &id_dbt, &vcard_dbt, DB_FIRST);

		while (db_error == 0) {

			/* don't include the version in the list of cards */
			if (id_dbt.size != strlen(PAS_BACKEND_FILE_VERSION_NAME) + 1
			    || strcmp (id_dbt.data, PAS_BACKEND_FILE_VERSION_NAME)) {
				ECard *card;
				char *id = id_dbt.data;
				char *vcard_string;
				
				/* Remove fields the user can't change
				 * and can change without the rest of the
				 * card changing 
				 */
				card = e_card_new (vcard_dbt.data);
				gtk_object_set (GTK_OBJECT (card), "last_use", NULL, "use_score", 0.0, NULL);
				vcard_string = e_card_get_vcard_assume_utf8 (card);
				gtk_object_unref (GTK_OBJECT (card));
				
				/* check what type of change has occurred, if any */
				switch (e_dbhash_compare (ehash, id, vcard_string)) {
				case E_DBHASH_STATUS_SAME:
					break;
				case E_DBHASH_STATUS_NOT_FOUND:
					ctx->add_cards = g_list_append (ctx->add_cards, 
									g_strdup(vcard_string));
					ctx->add_ids = g_list_append (ctx->add_ids, g_strdup(id));
					break;
				case E_DBHASH_STATUS_DIFFERENT:
					ctx->mod_cards = g_list_append (ctx->mod_cards, 
									g_strdup(vcard_string));
					ctx->mod_ids = g_list_append (ctx->mod_ids, g_strdup(id));
					break;
				}
			}

			db_error = dbc->c_get(dbc, &id_dbt, &vcard_dbt, DB_NEXT);
		}
		dbc->c_close (dbc);
	}

   	e_dbhash_foreach_key (ehash, (EDbHashFunc)pas_backend_file_changes_foreach_key, view->change_context);

	/* Send the changes */
	if (db_error != DB_NOTFOUND) {
		g_warning ("pas_backend_file_changes: error building list\n");
	} else {
  		if (ctx->add_cards != NULL)
  			pas_book_view_notify_add (view->book_view, ctx->add_cards);
		
		if (ctx->mod_cards != NULL)
			pas_book_view_notify_change (view->book_view, ctx->mod_cards);

		for (v = ctx->del_ids; v != NULL; v = v->next){
			char *id = v->data;
			pas_book_view_notify_remove (view->book_view, id);
		}
		
		pas_book_view_notify_complete (view->book_view);
	}

	/* Update the hash */
	for (i = ctx->add_ids, v = ctx->add_cards; i != NULL; i = i->next, v = v->next){
		char *id = i->data;
		char *vcard = v->data;

		e_dbhash_add (ehash, id, vcard);
		g_free (i->data);
		g_free (v->data);		
	}	
	for (i = ctx->mod_ids, v = ctx->mod_cards; i != NULL; i = i->next, v = v->next){
		char *id = i->data;
		char *vcard = v->data;

		e_dbhash_add (ehash, id, vcard);
		g_free (i->data);
		g_free (v->data);		
	}	
	for (i = ctx->del_ids; i != NULL; i = i->next){
		char *id = i->data;

		e_dbhash_remove (ehash, id);
		g_free (i->data);
	}

	e_dbhash_write (ehash);
  	e_dbhash_destroy (ehash);
}

static char *
do_create(PASBackend *backend,
	  char       *vcard_req,
	  char      **vcard_ptr)
{
	PASBackendFile *bf = PAS_BACKEND_FILE (backend);
	DB             *db = bf->priv->file_db;
	DBT            id_dbt, vcard_dbt;
	int            db_error;
	char           *id;
	ECard          *card;
	char           *vcard;
	char           *ret_val;

	id = pas_backend_file_create_unique_id (vcard_req);

	string_to_dbt (id, &id_dbt);
	
	card = e_card_new(vcard_req);
	e_card_set_id(card, id);
	vcard = e_card_get_vcard_assume_utf8(card);

	string_to_dbt (vcard, &vcard_dbt);

	db_error = db->put (db, NULL, &id_dbt, &vcard_dbt, 0);

	if (0 == db_error) {
		db_error = db->sync (db, 0);
		if (db_error != 0)
			g_warning ("db->sync failed.\n");
		ret_val = id;

	}
	else {
		g_free (id);
		ret_val = NULL;
	}

	gtk_object_unref(GTK_OBJECT(card));
	card = NULL;

	if (vcard_ptr && ret_val)
		*vcard_ptr = vcard;
	else
		g_free (vcard);

	return ret_val;
}

static void
pas_backend_file_process_create_card (PASBackend *backend,
				      PASBook    *book,
				      PASRequest *req)
{
	char *id;
	char *vcard;
	EIterator *iterator;
	PASBackendFile *bf = PAS_BACKEND_FILE (backend);

	id = do_create(backend, req->vcard, &vcard);
	if (id) {
		for (iterator = e_list_get_iterator(bf->priv->book_views); e_iterator_is_valid(iterator); e_iterator_next(iterator)) {
			const PASBackendFileBookView *view = e_iterator_get(iterator);
			if (vcard_matches_search (view, vcard)) {
				bonobo_object_ref (BONOBO_OBJECT (view->book_view));
				pas_book_view_notify_add_1 (view->book_view, vcard);
				pas_book_view_notify_complete (view->book_view);
				bonobo_object_unref (BONOBO_OBJECT (view->book_view));
			}
		}
		gtk_object_unref(GTK_OBJECT(iterator));
		
		pas_book_respond_create (
			book,
			GNOME_Evolution_Addressbook_BookListener_Success,
			id);
		g_free(vcard);
		g_free(id);
	}
	else {
		/* XXX need a different call status for this case, i
                   think */
		pas_book_respond_create (
				 book,
				 GNOME_Evolution_Addressbook_BookListener_CardNotFound,
				 "");
	}

	g_free(req->vcard);
}

static void
pas_backend_file_process_remove_card (PASBackend *backend,
				      PASBook    *book,
				      PASRequest *req)
{
	PASBackendFile *bf = PAS_BACKEND_FILE (backend);
	DB             *db = bf->priv->file_db;
	DBT            id_dbt, vcard_dbt;
	int            db_error;
	EIterator     *iterator;
	char          *vcard_string;
	const char    *id;

	id = req->id;
	string_to_dbt (id, &id_dbt);
	memset (&vcard_dbt, 0, sizeof (vcard_dbt));

	db_error = db->get (db, NULL, &id_dbt, &vcard_dbt, 0);
	if (0 != db_error) {
		pas_book_respond_remove (
				 book,
				 GNOME_Evolution_Addressbook_BookListener_CardNotFound);
		g_free (req->id);
		return;
	}
	
	db_error = db->del (db, NULL, &id_dbt, 0);
	if (0 != db_error) {
		pas_book_respond_remove (
				 book,
				 GNOME_Evolution_Addressbook_BookListener_CardNotFound);
		g_free (req->id);
		return;
	}

	db_error = db->sync (db, 0);
	if (db_error != 0)
		g_warning ("db->sync failed.\n");


	vcard_string = vcard_dbt.data;
	for (iterator = e_list_get_iterator (bf->priv->book_views); e_iterator_is_valid(iterator); e_iterator_next(iterator)) {
		const PASBackendFileBookView *view = e_iterator_get(iterator);
		if (vcard_matches_search (view, vcard_string)) {
			bonobo_object_ref (BONOBO_OBJECT (view->book_view));
			pas_book_view_notify_remove (view->book_view, req->id);
			pas_book_view_notify_complete (view->book_view);
			bonobo_object_unref (BONOBO_OBJECT (view->book_view));
		}
	}
	gtk_object_unref(GTK_OBJECT(iterator));
	
	pas_book_respond_remove (
				 book,
				 GNOME_Evolution_Addressbook_BookListener_Success);
	
	g_free (req->id);
}

static void
pas_backend_file_process_modify_card (PASBackend *backend,
				      PASBook    *book,
				      PASRequest *req)
{
	PASBackendFile *bf = PAS_BACKEND_FILE (backend);
	DB             *db = bf->priv->file_db;
	DBT            id_dbt, vcard_dbt;
	int            db_error;
	EIterator     *iterator;
	ECard         *card;
	const char    *id, *lookup_id;
	char          *old_vcard_string;

	/* create a new ecard from the request data */
	card = e_card_new(req->vcard);
	id = e_card_get_id(card);

	/* This is disgusting, but for a time cards were added with
           ID's that are no longer used (they contained both the uri
           and the id.) If we recognize it as a uri (file:///...) trim
           off everything before the last '/', and use that as the
           id.*/
	if (!strncmp (id, "file:///", strlen ("file:///"))) {
		lookup_id = strrchr (id, '/') + 1;
	}
	else
		lookup_id = id;

	string_to_dbt (lookup_id, &id_dbt);	
	memset (&vcard_dbt, 0, sizeof (vcard_dbt));

	/* get the old ecard - the one that's presently in the db */
	db_error = db->get (db, NULL, &id_dbt, &vcard_dbt, 0);
	if (0 != db_error) {
		pas_book_respond_modify (
				 book,
				 GNOME_Evolution_Addressbook_BookListener_CardNotFound);
		g_free (req->id);
		return;
	}
	old_vcard_string = g_strdup(vcard_dbt.data);

	string_to_dbt (req->vcard, &vcard_dbt);	

	db_error = db->put (db, NULL, &id_dbt, &vcard_dbt, 0);

	if (0 == db_error) {
		db_error = db->sync (db, 0);
		if (db_error != 0)
			g_warning ("db->sync failed.\n");

		for (iterator = e_list_get_iterator(bf->priv->book_views); e_iterator_is_valid(iterator); e_iterator_next(iterator)) {
			CORBA_Environment ev;
			const PASBackendFileBookView *view = e_iterator_get(iterator);
			gboolean old_match, new_match;

			CORBA_exception_init(&ev);

			bonobo_object_dup_ref(bonobo_object_corba_objref(BONOBO_OBJECT(view->book_view)), &ev);

			old_match = vcard_matches_search (view, old_vcard_string);
			new_match = vcard_matches_search (view, req->vcard);
			if (old_match && new_match)
				pas_book_view_notify_change_1 (view->book_view, req->vcard);
			else if (new_match)
				pas_book_view_notify_add_1 (view->book_view, req->vcard);
			else /* if (old_match) */
				pas_book_view_notify_remove (view->book_view, id);
			pas_book_view_notify_complete (view->book_view);

			bonobo_object_release_unref(bonobo_object_corba_objref(BONOBO_OBJECT(view->book_view)), &ev);
		}
		gtk_object_unref(GTK_OBJECT(iterator));

		pas_book_respond_modify (
				 book,
				 GNOME_Evolution_Addressbook_BookListener_Success);
	}
	else {
		pas_book_respond_modify (
				 book,
				 GNOME_Evolution_Addressbook_BookListener_CardNotFound);
	}

	g_free(old_vcard_string);

	gtk_object_unref(GTK_OBJECT(card));
	g_free (req->vcard);
}

static void
pas_backend_file_build_cards_list(PASBackend *backend,
				      PASBackendFileCursorPrivate *cursor_data,
				      char *search)
{
	PASBackendFile *bf = PAS_BACKEND_FILE (backend);
	DB             *db = bf->priv->file_db;
	DBC            *dbc;
	int            db_error;
	DBT  id_dbt, vcard_dbt;
	PASBackendCardSExp *card_sexp = NULL;
	gboolean search_needed;
	
	cursor_data->elements = NULL;

	search_needed = TRUE;

	if (!strcmp (search, "(contains \"x-evolution-any-field\" \"\")"))
		search_needed = FALSE;

	card_sexp = pas_backend_card_sexp_new (search);
	
	if (!card_sexp)
		g_warning ("pas_backend_file_build_all_cards_list: error building list\n");

	db_error = db->cursor (db, NULL, &dbc, 0);

	if (db_error != 0) {
		g_warning ("pas_backend_file_build_all_cards_list: error building list\n");
	}

	memset (&vcard_dbt, 0, sizeof (vcard_dbt));
	memset (&id_dbt, 0, sizeof (id_dbt));
	db_error = dbc->c_get(dbc, &id_dbt, &vcard_dbt, DB_FIRST);

	while (db_error == 0) {

		/* don't include the version in the list of cards */
		if (id_dbt.size != strlen(PAS_BACKEND_FILE_VERSION_NAME) + 1
		    || strcmp (id_dbt.data, PAS_BACKEND_FILE_VERSION_NAME)) {

			if ((!search_needed) || (card_sexp != NULL && pas_backend_card_sexp_match_vcard  (card_sexp, vcard_dbt.data))) {
				cursor_data->elements = g_list_prepend (cursor_data->elements, g_strdup (vcard_dbt.data));
			}
		}

		db_error = dbc->c_get(dbc, &id_dbt, &vcard_dbt, DB_NEXT);

	}

	if (db_error != DB_NOTFOUND) {
		g_warning ("pas_backend_file_build_all_cards_list: error building list\n");
	}
	else {
		cursor_data->num_elements = g_list_length (cursor_data->elements);
		cursor_data->elements = g_list_reverse (cursor_data->elements);
	}
}

static void
pas_backend_file_process_get_vcard (PASBackend *backend,
				    PASBook    *book,
				    PASRequest *req)
{
	PASBackendFile *bf;
	DB             *db;
	DBT             id_dbt, vcard_dbt;
	int             db_error = 0;
	char           *card;
	GNOME_Evolution_Addressbook_BookListener_CallStatus status;	

	bf = PAS_BACKEND_FILE (pas_book_get_backend (book));
	db = bf->priv->file_db;

	string_to_dbt (req->id, &id_dbt);
	memset (&vcard_dbt, 0, sizeof (vcard_dbt));

	db_error = db->get (db, NULL, &id_dbt, &vcard_dbt, 0);

	if (db_error == 0) {
		card = vcard_dbt.data;
		status = GNOME_Evolution_Addressbook_BookListener_Success;
	} else {
		card = NULL;
		status = GNOME_Evolution_Addressbook_BookListener_CardNotFound;
	}

	pas_book_respond_get_vcard (book,
				    status,
				    card);
}

static void
pas_backend_file_process_get_cursor (PASBackend *backend,
				     PASBook    *book,
				     PASRequest *req)
{
	/*
	  PASBackendFile *bf = PAS_BACKEND_FILE (backend);
	  DB             *db = bf->priv->file_db;
	  DBT            id_dbt, vcard_dbt;
	*/
	CORBA_Environment ev;
	int            db_error = 0;
	PASBackendFileCursorPrivate *cursor_data;
	PASCardCursor *cursor;
	GNOME_Evolution_Addressbook_Book corba_book;

	cursor_data = g_new(PASBackendFileCursorPrivate, 1);
	cursor_data->backend = backend;
	cursor_data->book = book;

	pas_backend_file_build_cards_list(backend, cursor_data, req->search);

	corba_book = bonobo_object_corba_objref(BONOBO_OBJECT(book));

	CORBA_exception_init(&ev);

	GNOME_Evolution_Addressbook_Book_ref(corba_book, &ev);
	
	if (ev._major != CORBA_NO_EXCEPTION) {
		g_warning("pas_backend_file_process_get_cursor: Exception reffing "
			  "corba book.\n");
	}

	CORBA_exception_free(&ev);
	
	cursor = pas_card_cursor_new(get_length,
				     get_nth,
				     cursor_data);

	gtk_signal_connect(GTK_OBJECT(cursor), "destroy",
			   GTK_SIGNAL_FUNC(cursor_destroy), cursor_data);
	
	pas_book_respond_get_cursor (
		book,
		(db_error == 0 
		 ? GNOME_Evolution_Addressbook_BookListener_Success 
		 : GNOME_Evolution_Addressbook_BookListener_CardNotFound),
		cursor);
}

static void
pas_backend_file_process_get_book_view (PASBackend *backend,
					PASBook    *book,
					PASRequest *req)
{
	PASBackendFile *bf = PAS_BACKEND_FILE (backend);
	CORBA_Environment ev;
	PASBookView       *book_view;
	PASBackendFileBookView view;
	EIterator *iterator;

	g_return_if_fail (req->listener != NULL);
	
	bonobo_object_ref(BONOBO_OBJECT(book));

	book_view = pas_book_view_new (req->listener);

	gtk_signal_connect(GTK_OBJECT(book_view), "destroy",
			   GTK_SIGNAL_FUNC(view_destroy), book);

	view.book_view = book_view;
	view.search = req->search;
	view.card_sexp = NULL;
	view.change_id = NULL;
	view.change_context = NULL;	

	e_list_append(bf->priv->book_views, &view);

	pas_book_respond_get_book_view (book,
		   (book_view != NULL
		    ? GNOME_Evolution_Addressbook_BookListener_Success 
		    : GNOME_Evolution_Addressbook_BookListener_CardNotFound /* XXX */),
		   book_view);

	iterator = e_list_get_iterator(bf->priv->book_views);
	e_iterator_last(iterator);
	pas_backend_file_search (bf, book, e_iterator_get(iterator));
	gtk_object_unref(GTK_OBJECT(iterator));

	g_free(req->search);
	CORBA_exception_init(&ev);

	bonobo_object_unref (BONOBO_OBJECT (book_view));
	bonobo_object_release_unref (req->listener, &ev);
	
	if (ev._major != CORBA_NO_EXCEPTION) {
		g_warning("pas_backend_file_process_get_book_view: Exception unreffing "
			  "listener.\n");
	}

	CORBA_exception_free(&ev);
}

static void
pas_backend_file_process_get_changes (PASBackend *backend,
				      PASBook    *book,
				      PASRequest *req)
{
	PASBackendFile *bf = PAS_BACKEND_FILE (backend);
	CORBA_Environment ev;
	PASBookView       *book_view;
	PASBackendFileBookView view;
	PASBackendFileChangeContext ctx;
	EIterator *iterator;

	g_return_if_fail (req->listener != NULL);

	bonobo_object_ref(BONOBO_OBJECT(book));

	book_view = pas_book_view_new (req->listener);

	gtk_signal_connect(GTK_OBJECT(book_view), "destroy",
			   GTK_SIGNAL_FUNC(view_destroy), book);

	pas_book_respond_get_changes (book,
		   (book_view != NULL
		    ? GNOME_Evolution_Addressbook_BookListener_Success 
		    : GNOME_Evolution_Addressbook_BookListener_CardNotFound /* XXX */),
		   book_view);

	view.book_view = book_view;
	view.change_id = req->change_id;
	view.change_context = &ctx;
	ctx.db = bf->priv->file_db;
	ctx.add_cards = NULL;
	ctx.add_ids = NULL;
	ctx.mod_cards = NULL;
	ctx.mod_ids = NULL;
	ctx.del_ids = NULL;
	view.search = NULL;
	view.card_sexp = NULL;
	
	e_list_append(bf->priv->book_views, &view);

	iterator = e_list_get_iterator(bf->priv->book_views);
	e_iterator_last(iterator);
	pas_backend_file_changes (bf, book, e_iterator_get(iterator));
	gtk_object_unref(GTK_OBJECT(iterator));

	g_free(req->search);
	CORBA_exception_init(&ev);
	bonobo_object_release_unref (req->listener, &ev);
	
	if (ev._major != CORBA_NO_EXCEPTION) {
		g_warning("pas_backend_file_process_get_changed: Exception unreffing "
			  "listener.\n");
	}

	CORBA_exception_free(&ev);
}

static void
pas_backend_file_process_check_connection (PASBackend *backend,
					   PASBook    *book,
					   PASRequest *req)
{
	PASBackendFile *bf = PAS_BACKEND_FILE (backend);

	pas_book_report_connection (book, bf->priv->file_db != NULL);
}

static char *
pas_backend_file_extract_path_from_uri (const char *uri)
{
	g_assert (strncasecmp (uri, "file:", 5) == 0);

	return g_strdup (uri + 5);
}

static void
pas_backend_file_process_authenticate_user (PASBackend *backend,
					    PASBook    *book,
					    PASRequest *req)
{
	pas_book_respond_authenticate_user (book,
					    GNOME_Evolution_Addressbook_BookListener_Success);
}

static void
pas_backend_file_process_get_supported_fields (PASBackend *backend,
					       PASBook    *book,
					       PASRequest *req)
{
	EList *fields = e_list_new ((EListCopyFunc)g_strdup, (EListFreeFunc)g_free, NULL);
	ECardSimple *simple;
	ECard *card;
	int i;

	/* we support everything, so instantiate an e-card, and loop
           through all fields, adding their ecard_fields. */

	card = e_card_new ("");
	simple = e_card_simple_new (card);

	for (i = 0; i < E_CARD_SIMPLE_FIELD_LAST; i ++)
		e_list_append (fields, e_card_simple_get_ecard_field (simple, i));

	gtk_object_unref (GTK_OBJECT (card));
	gtk_object_unref (GTK_OBJECT (simple));

	pas_book_respond_get_supported_fields (book,
					       GNOME_Evolution_Addressbook_BookListener_Success,
					       fields);
}

static void
pas_backend_file_process_client_requests (PASBook *book)
{
	PASBackend *backend;
	PASRequest *req;

	backend = pas_book_get_backend (book);

	req = pas_book_pop_request (book);
	if (req == NULL)
		return;

	switch (req->op) {
	case CreateCard:
		pas_backend_file_process_create_card (backend, book, req);
		break;

	case RemoveCard:
		pas_backend_file_process_remove_card (backend, book, req);
		break;

	case ModifyCard:
		pas_backend_file_process_modify_card (backend, book, req);
		break;

	case CheckConnection:
		pas_backend_file_process_check_connection (backend, book, req);
		break;

	case GetVCard:
		pas_backend_file_process_get_vcard (backend, book, req);
		break;
		
	case GetCursor:
		pas_backend_file_process_get_cursor (backend, book, req);
		break;
		
	case GetBookView:
		pas_backend_file_process_get_book_view (backend, book, req);
		break;

	case GetChanges:
		pas_backend_file_process_get_changes (backend, book, req);
		break;

	case AuthenticateUser:
		pas_backend_file_process_authenticate_user (backend, book, req);
		break;

	case GetSupportedFields:
		pas_backend_file_process_get_supported_fields (backend, book, req);
		break;
	}

	g_free (req);
}

static void
pas_backend_file_book_destroy_cb (PASBook *book, gpointer data)
{
	PASBackendFile *backend;

	backend = PAS_BACKEND_FILE (data);

	pas_backend_remove_client (PAS_BACKEND (backend), book);
}

/*
** versions:
**
** 0.0 just a list of cards
**
** 0.1 same as 0.0, but with the version tag
**
** 0.2 not a real format upgrade, just a hack to fix broken ids caused
**     by a bug in early betas, but we only need to convert them if
**     the previous version is 0.1, since the bug existed after 0.1
**     came about.
*/
static gboolean
pas_backend_file_upgrade_db (PASBackendFile *bf, char *old_version)
{
	DB  *db = bf->priv->file_db;
	int db_error;
	DBT version_name_dbt, version_dbt;
	
	if (strcmp (old_version, "0.0")
	    && strcmp (old_version, "0.1")) {
		g_warning ("unsupported version '%s' found in PAS backend file\n",
			   old_version);
		return FALSE;
	}

	if (!strcmp (old_version, "0.1")) {
		/* we just loop through all the cards in the db,
                   giving them valid ids if they don't have them */
		DBT  id_dbt, vcard_dbt;
		DBC *dbc;
		int  card_failed = 0;

		db_error = db->cursor (db, NULL, &dbc, 0);
		if (db_error != 0) {
			g_warning ("unable to get cursor");
			return FALSE;
		}

		memset (&id_dbt, 0, sizeof (id_dbt));
		memset (&vcard_dbt, 0, sizeof (vcard_dbt));

		db_error = dbc->c_get(dbc, &id_dbt, &vcard_dbt, DB_FIRST);

		while (db_error == 0) {
			if (id_dbt.size != strlen(PAS_BACKEND_FILE_VERSION_NAME) + 1
			    || strcmp (id_dbt.data, PAS_BACKEND_FILE_VERSION_NAME)) {
				ECard *card;

				card = e_card_new (vcard_dbt.data);

				/* the cards we're looking for are
				   created with a normal id dbt, but
				   with the id field in the vcard set
				   to something that doesn't match.
				   so, we need to modify the card to
				   have the same id as the the dbt. */
				if (strcmp (id_dbt.data, e_card_get_id (card))) {
					char *vcard;

					e_card_set_id (card, id_dbt.data);

					vcard = e_card_get_vcard (card);
					string_to_dbt (vcard, &vcard_dbt);

					db_error = db->put (db, NULL,
							    &id_dbt, &vcard_dbt, 0);

					g_free (vcard);

					if (db_error != 0)
						card_failed++;
				}

				gtk_object_unref (GTK_OBJECT(card));
			}

			db_error = dbc->c_get(dbc, &id_dbt, &vcard_dbt, DB_NEXT);
		}

		if (card_failed) {
			g_warning ("failed to update %d cards\n", card_failed);
			return FALSE;
		}
	}

	string_to_dbt (PAS_BACKEND_FILE_VERSION_NAME, &version_name_dbt);
	string_to_dbt (PAS_BACKEND_FILE_VERSION, &version_dbt);

	db_error = db->put (db, NULL, &version_name_dbt, &version_dbt, 0);
	if (db_error == 0)
		return TRUE;
	else
		return FALSE;
}

static gboolean
pas_backend_file_maybe_upgrade_db (PASBackendFile *bf)
{
	DB   *db = bf->priv->file_db;
	DBT  version_name_dbt, version_dbt;
	int  db_error;
	char *version;
	gboolean ret_val = TRUE;

	string_to_dbt (PAS_BACKEND_FILE_VERSION_NAME, &version_name_dbt);
	memset (&version_dbt, 0, sizeof (version_dbt));

	db_error = db->get (db, NULL, &version_name_dbt, &version_dbt, 0);
	if (db_error == 0) {
		/* success */
		version = g_strdup (version_dbt.data);
	}
	else {
		/* key was not in file */
		version = g_strdup ("0.0");
	}

	if (strcmp (version, PAS_BACKEND_FILE_VERSION))
		ret_val = pas_backend_file_upgrade_db (bf, version);

	g_free (version);

	return ret_val;
}


#define INITIAL_VCARD "BEGIN:VCARD\n\
X-EVOLUTION-FILE-AS:Ximian, Inc.\n\
LABEL;WORK;QUOTED-PRINTABLE:401 Park Drive  3 West=0ABoston, MA 02215=0AUSA\n\
TEL;WORK;VOICE:(617) 236-0442\n\
TEL;WORK;FAX:(617) 236-8630\n\
EMAIL;INTERNET:hello@ximian.com\n\
URL:www.ximian.com/\n\
ORG:Ximian, Inc.;\n\
NOTE:Welcome to the Ximian Addressbook.\n\
END:VCARD"

static gboolean
pas_backend_file_load_uri (PASBackend             *backend,
			   const char             *uri)
{
	PASBackendFile *bf = PAS_BACKEND_FILE (backend);
	char           *filename;
	gboolean        writable = FALSE;
	GList          *l;
	int             db_error;
	DB *db;
	int major, minor, patch;

	g_assert (bf->priv->loaded == FALSE);

	db_version (&major, &minor, &patch);

	if (major != 3 ||
	    minor != 1 ||
	    patch != 17) {
		g_warning ("Wrong version of libdb.");
		return FALSE;
	}

	filename = pas_backend_file_extract_path_from_uri (uri);

	db_error = e_db3_utils_maybe_recover (filename);
	if (db_error != 0)
		return FALSE;

	db_error = db_create (&db, NULL, 0);
	if (db_error != 0)
		return FALSE;

	db_error = db->open (db, filename, NULL, DB_HASH, 0, 0666);

	if (db_error == DB_OLD_VERSION) {
		db_error = e_db3_utils_upgrade_format (filename);

		if (db_error != 0)
			return FALSE;

		db_error = db->open (db, filename, NULL, DB_HASH, 0, 0666);
	}

	bf->priv->file_db = db;

	if (db_error == 0) {
		writable = TRUE;
	} else {
		db_error = db->open (db, filename, NULL, DB_HASH, DB_RDONLY, 0666);

		if (db_error != 0) {
			db_error = db->open (db, filename, NULL, DB_HASH, DB_CREATE, 0666);

			if (db_error == 0) {
				char *create_initial_file;
				char *dir;

				dir = g_dirname(filename);
				create_initial_file = g_concat_dir_and_file(dir, "create-initial");

				if (g_file_exists(create_initial_file)) {
					char *id;
					id = do_create(backend, INITIAL_VCARD, NULL);
					g_free (id);
				}

				g_free(create_initial_file);
				g_free(dir);

				writable = TRUE;
			}
		}
	}

	g_free (filename);


	if (db_error != 0) {
		bf->priv->file_db = NULL;
		return FALSE;
	}

	bf->priv->writable = writable;

	if (pas_backend_file_maybe_upgrade_db (bf))
		bf->priv->loaded = TRUE;
	else {
		db->close (db, 0);
		bf->priv->file_db = NULL;
		bf->priv->writable = FALSE;
		return FALSE;
	}

	g_free(bf->priv->uri);
	bf->priv->uri = g_strdup (uri);

	/* report the writable status of the book to all its clients */
	for (l = bf->priv->clients; l; l = g_list_next (l)) {
		PASBook *book = l->data;

		pas_book_report_writable (book, writable);
	}

	return TRUE;
}

/* Get_uri handler for the addressbook file backend */
static const char *
pas_backend_file_get_uri (PASBackend *backend)
{
	PASBackendFile *bf;

	bf = PAS_BACKEND_FILE (backend);

	g_return_val_if_fail (bf->priv->loaded, NULL);
	g_assert (bf->priv->uri != NULL);

	return bf->priv->uri;
}

static gboolean
pas_backend_file_add_client (PASBackend             *backend,
			     GNOME_Evolution_Addressbook_BookListener  listener)
{
	PASBackendFile *bf;
	PASBook        *book;

	g_assert (backend != NULL);
	g_assert (PAS_IS_BACKEND_FILE (backend));

	bf = PAS_BACKEND_FILE (backend);

	book = pas_book_new (backend, listener);

	if (!book) {
		if (!bf->priv->clients)
			pas_backend_last_client_gone (backend);

		return FALSE;
	}

	gtk_signal_connect (GTK_OBJECT (book), "destroy",
			    pas_backend_file_book_destroy_cb, backend);

	gtk_signal_connect (GTK_OBJECT (book), "requests_queued",
		    pas_backend_file_process_client_requests, NULL);

	bf->priv->clients = g_list_prepend (
		bf->priv->clients, book);

	if (bf->priv->loaded) {
		pas_book_respond_open (
			book, GNOME_Evolution_Addressbook_BookListener_Success);
		if (bf->priv->writable)
			pas_book_report_writable (book, bf->priv->writable);
	} else {
		/* Open the book. */
		pas_book_respond_open (
			book, GNOME_Evolution_Addressbook_BookListener_Success);
		if (bf->priv->writable)
			pas_book_report_writable (book, bf->priv->writable);
	}

	bonobo_object_unref (BONOBO_OBJECT (book));
	
	return TRUE;
}

static void
pas_backend_file_remove_client (PASBackend             *backend,
				PASBook                *book)
{
	PASBackendFile *bf;
	GList *l;
	PASBook *lbook;

	g_return_if_fail (backend != NULL);
	g_return_if_fail (PAS_IS_BACKEND_FILE (backend));
	g_return_if_fail (book != NULL);
	g_return_if_fail (PAS_IS_BOOK (book));

	bf = PAS_BACKEND_FILE (backend);

	/* Find the book in the list of clients */

	for (l = bf->priv->clients; l; l = l->next) {
		lbook = PAS_BOOK (l->data);

		if (lbook == book)
			break;
	}

	g_assert (l != NULL);

	/* Disconnect */

	bf->priv->clients = g_list_remove_link (bf->priv->clients, l);
	g_list_free_1 (l);

	/* When all clients go away, notify the parent factory about it so that
	 * it may decide whether to kill the backend or not.
	 */
	if (!bf->priv->clients)
		pas_backend_last_client_gone (backend);
}

static char *
pas_backend_file_get_static_capabilities (PASBackend             *backend)
{
	return g_strdup("local");
}

static gboolean
pas_backend_file_construct (PASBackendFile *backend)
{
	g_assert (backend != NULL);
	g_assert (PAS_IS_BACKEND_FILE (backend));

	if (! pas_backend_construct (PAS_BACKEND (backend)))
		return FALSE;

	return TRUE;
}

/**
 * pas_backend_file_new:
 */
PASBackend *
pas_backend_file_new (void)
{
	PASBackendFile *backend;

	backend = gtk_type_new (pas_backend_file_get_type ());

	if (! pas_backend_file_construct (backend)) {
		gtk_object_unref (GTK_OBJECT (backend));

		return NULL;
	}

	return PAS_BACKEND (backend);
}

static void
pas_backend_file_destroy (GtkObject *object)
{
	PASBackendFile *bf;

	bf = PAS_BACKEND_FILE (object);

	gtk_object_unref(GTK_OBJECT(bf->priv->book_views));
	g_free (bf->priv->uri);

	GTK_OBJECT_CLASS (pas_backend_file_parent_class)->destroy (object);	
}

static void
pas_backend_file_class_init (PASBackendFileClass *klass)
{
	GtkObjectClass  *object_class = (GtkObjectClass *) klass;
	PASBackendClass *parent_class;

	pas_backend_file_parent_class = gtk_type_class (pas_backend_get_type ());

	parent_class = PAS_BACKEND_CLASS (klass);

	/* Set the virtual methods. */
	parent_class->load_uri                = pas_backend_file_load_uri;
	parent_class->get_uri                 = pas_backend_file_get_uri;
	parent_class->add_client              = pas_backend_file_add_client;
	parent_class->remove_client           = pas_backend_file_remove_client;
	parent_class->get_static_capabilities = pas_backend_file_get_static_capabilities;

	object_class->destroy = pas_backend_file_destroy;
}

static void
pas_backend_file_init (PASBackendFile *backend)
{
	PASBackendFilePrivate *priv;

	priv             = g_new0 (PASBackendFilePrivate, 1);
	priv->loaded     = FALSE;
	priv->clients    = NULL;
	priv->book_views = e_list_new((EListCopyFunc) pas_backend_file_book_view_copy, (EListFreeFunc) pas_backend_file_book_view_free, NULL);
	priv->uri        = NULL;
	priv->writable   = FALSE;

	backend->priv = priv;
}

/**
 * pas_backend_file_get_type:
 */
GtkType
pas_backend_file_get_type (void)
{
	static GtkType type = 0;

	if (! type) {
		GtkTypeInfo info = {
			"PASBackendFile",
			sizeof (PASBackendFile),
			sizeof (PASBackendFileClass),
			(GtkClassInitFunc)  pas_backend_file_class_init,
			(GtkObjectInitFunc) pas_backend_file_init,
			NULL, /* reserved 1 */
			NULL, /* reserved 2 */
			(GtkClassInitFunc) NULL
		};

		type = gtk_type_unique (pas_backend_get_type (), &info);
	}

	return type;
}
