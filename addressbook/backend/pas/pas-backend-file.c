/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Author:
 *   Nat Friedman (nat@ximian.com)
 *
 * Copyright 2000, Ximian, Inc.
 */

#include "config.h"  
#include "pas-backend-file.h"
#include "pas-backend-card-sexp.h"
#include "pas-backend-summary.h"
#include "pas-book.h"
#include "pas-book-view.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <db.h>
#include <sys/stat.h>

#include <e-util/e-db3-utils.h>

#if DB_VERSION_MAJOR != 3 || \
    DB_VERSION_MINOR != 1 || \
    DB_VERSION_PATCH != 17
#error Including wrong DB3.  Need libdb 3.1.17.
#endif

#include <gal/util/e-util.h>
#include <gal/widgets/e-unicode.h>

#include <ebook/e-contact.h>
#include <e-util/e-dbhash.h>
#include <e-util/e-db3-utils.h>
#include <libgnome/gnome-i18n.h>

#define PAS_BACKEND_FILE_VERSION_NAME "PAS-DB-VERSION"
#define PAS_BACKEND_FILE_VERSION "0.2"

#define PAS_ID_PREFIX "pas-id-"
#define SUMMARY_FLUSH_TIMEOUT 5000

static PASBackendSyncClass *pas_backend_file_parent_class;
typedef struct _PASBackendFileSearchContext PASBackendFileSearchContext;
typedef struct _PasBackendFileChangeContext PASBackendFileChangeContext;

struct _PASBackendFilePrivate {
	char     *uri;
	char     *filename;
	DB       *file_db;
	PASBackendSummary *summary;
};

struct _PasBackendFileChangeContext {
	DB *db;

	GList *add_cards;
	GList *add_ids;
	GList *mod_cards;
	GList *mod_ids;
	GList *del_ids;
};

static void
string_to_dbt(const char *str, DBT *dbt)
{
	memset (dbt, 0, sizeof (*dbt));
	dbt->data = (void*)str;
	dbt->size = strlen (str) + 1;
}

static void
build_summary (PASBackendFilePrivate *bfpriv)
{
	DB             *db = bfpriv->file_db;
	DBC            *dbc;
	int            db_error;
	DBT  id_dbt, vcard_dbt;

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

			pas_backend_summary_add_card (bfpriv->summary, vcard_dbt.data);
		}

		db_error = dbc->c_get(dbc, &id_dbt, &vcard_dbt, DB_NEXT);

	}
}

static void
do_summary_query (PASBackendFile *bf,
		  PASBookView    *view)
{
	GPtrArray *ids = pas_backend_summary_search (bf->priv->summary, pas_book_view_get_card_query (view));
	int     db_error = 0;
	DB      *db = bf->priv->file_db;
	DBT     id_dbt, vcard_dbt;
	int i;

	for (i = 0; i < ids->len; i ++) {
		char *id = g_ptr_array_index (ids, i);
		char *vcard = NULL;

#if SUMMARY_STORES_ENOUGH_INFO
		/* this is disabled for the time being because lists
		   can have more than 3 email addresses and the
		   summary only stores 3. */

		if (completion_search) {
			vcard = pas_backend_summary_get_summary_vcard (bf->priv->summary,
								       id);
		}
		else {
#endif
			string_to_dbt (id, &id_dbt);
			memset (&vcard_dbt, 0, sizeof (vcard_dbt));
				
			db_error = db->get (db, NULL, &id_dbt, &vcard_dbt, 0);

			if (db_error == 0)
				vcard = g_strdup (vcard_dbt.data);
#if SUMMARY_STORES_ENOUGH_INFO
		}
#endif

		if (vcard)
			pas_book_view_notify_add_1 (view, vcard);
	}

	g_ptr_array_free (ids, TRUE);

	pas_book_view_notify_complete (view, GNOME_Evolution_Addressbook_Success);
}

static char *
pas_backend_file_create_unique_id (void)
{
	/* use a 32 counter and the 32 bit timestamp to make an id.
	   it's doubtful 2^32 id's will be created in a second, so we
	   should be okay. */
	static guint c = 0;
	return g_strdup_printf (PAS_ID_PREFIX "%08lX%08X", time(NULL), c++);
}

typedef struct {
	PASBackendFile *bf;
	PASBook *book;
	PASBookView *view;
	DBC    *dbc;

	gboolean done_first;
	gboolean search_needed;
} FileBackendSearchClosure;

static void
free_search_closure (FileBackendSearchClosure *closure)
{
	g_free (closure);
}

static gboolean
pas_backend_file_search_timeout (gpointer data)
{
	FileBackendSearchClosure *closure = data;
	int     db_error = 0;
	DBT     id_dbt, vcard_dbt;
	int     file_version_name_len;
	DBC     *dbc = closure->dbc;

	file_version_name_len = strlen (PAS_BACKEND_FILE_VERSION_NAME);

	memset (&id_dbt, 0, sizeof (id_dbt));
	memset (&vcard_dbt, 0, sizeof (vcard_dbt));

	if (closure->done_first) {
		db_error = dbc->c_get(dbc, &id_dbt, &vcard_dbt, DB_NEXT);
	}
	else {
		db_error = dbc->c_get(dbc, &id_dbt, &vcard_dbt, DB_FIRST);
		closure->done_first = TRUE;
	}

	while (db_error == 0) {

		/* don't include the version in the list of cards */
		if (strcmp (id_dbt.data, PAS_BACKEND_FILE_VERSION_NAME)) {
			char *vcard_string = vcard_dbt.data;

			/* check if the vcard matches the search sexp */
			if ((!closure->search_needed) || pas_book_view_vcard_matches (closure->view, vcard_string))
				pas_book_view_notify_add_1 (closure->view, g_strdup (vcard_string));
		}

		db_error = dbc->c_get(dbc, &id_dbt, &vcard_dbt, DB_NEXT);
	}

	dbc->c_close (dbc);

	if (db_error != DB_NOTFOUND) {
		g_warning ("pas_backend_file_search: error building list\n");
		free_search_closure (closure);
	}

	pas_book_view_notify_complete (closure->view, GNOME_Evolution_Addressbook_Success);

	free_search_closure (closure);

	return FALSE;
}


static void
pas_backend_file_search (PASBackendFile  	      *bf,
			 PASBookView         	      *book_view)
{
	gboolean search_needed;
	const char *query = pas_book_view_get_card_query (book_view);
	search_needed = TRUE;

	if ( ! strcmp (query, "(contains \"x-evolution-any-field\" \"\")"))
		search_needed = FALSE;

	if (search_needed)
		pas_book_view_notify_status_message (book_view, _("Searching..."));
	else
		pas_book_view_notify_status_message (book_view, _("Loading..."));

	if (pas_backend_summary_is_summary_query (bf->priv->summary, query)) {
		do_summary_query (bf, book_view);
	}
	else {
		FileBackendSearchClosure *closure = g_new0 (FileBackendSearchClosure, 1);
		DB  *db = bf->priv->file_db;
		int db_error;

		closure->search_needed = search_needed;
		closure->view = book_view;
		closure->bf = bf;

		db_error = db->cursor (db, NULL, &closure->dbc, 0);

		if (db_error != 0) {
			g_warning ("pas_backend_file_search: error building list\n");
		} else {
			g_idle_add (pas_backend_file_search_timeout, closure);
		}
	}
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

#if notyet
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
	char *dirname, *slash;

	memset (&id_dbt, 0, sizeof (id_dbt));
	memset (&vcard_dbt, 0, sizeof (vcard_dbt));

	/* Find the changed ids */
	dirname = g_strdup (bf->priv->filename);
	slash = strrchr (dirname, '/');
	*slash = '\0';

	filename = g_strdup_printf ("%s/%s.db", dirname, view->change_id);
	ehash = e_dbhash_new (filename);
	g_free (filename);
	g_free (dirname);

	db_error = db->cursor (db, NULL, &dbc, 0);

	if (db_error != 0) {
		g_warning ("pas_backend_file_changes: error building list\n");
	} else {
		db_error = dbc->c_get(dbc, &id_dbt, &vcard_dbt, DB_FIRST);

		while (db_error == 0) {

			/* don't include the version in the list of cards */
			if (id_dbt.size != strlen(PAS_BACKEND_FILE_VERSION_NAME) + 1
			    || strcmp (id_dbt.data, PAS_BACKEND_FILE_VERSION_NAME)) {
				EContact *contact;
				char *id = id_dbt.data;
				char *vcard_string;
				
				/* Remove fields the user can't change
				 * and can change without the rest of the
				 * card changing 
				 */
				contact = e_contact_new_from_string (vcard_dbt.data);
#if notyet
				g_object_set (card, "last_use", NULL, "use_score", 0.0, NULL);
#endif
				vcard_string = e_vcard_to_string (E_VCARD (contact));
				g_object_unref (contact);
				
				/* check what type of change has occurred, if any */
				switch (e_dbhash_compare (ehash, id, vcard_string)) {
				case E_DBHASH_STATUS_SAME:
					break;
				case E_DBHASH_STATUS_NOT_FOUND:
					ctx->add_cards = g_list_append (ctx->add_cards, 
									vcard_string);
					ctx->add_ids = g_list_append (ctx->add_ids, g_strdup(id));
					break;
				case E_DBHASH_STATUS_DIFFERENT:
					ctx->mod_cards = g_list_append (ctx->mod_cards, 
									vcard_string);
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
			pas_book_view_notify_remove_1 (view->book_view, id);
		}
		
		pas_book_view_notify_complete (view->book_view, GNOME_Evolution_Addressbook_Success);
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
#endif

static char *
do_create(PASBackendFile  *bf,
	  const char      *vcard_req)
{
	DB             *db = bf->priv->file_db;
	DBT            id_dbt, vcard_dbt;
	int            db_error;
	char           *id;
	EContact       *contact;
	char           *vcard;
	char           *ret_val;

	id = pas_backend_file_create_unique_id ();

	string_to_dbt (id, &id_dbt);

	contact = e_contact_new_from_vcard (vcard_req);
	e_contact_set(contact, E_CONTACT_UID, id);
	vcard = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);

	string_to_dbt (vcard, &vcard_dbt);

	g_free (vcard);

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

	g_object_unref(contact);

	return ret_val;
}

static PASBackendSyncStatus
pas_backend_file_process_create_card (PASBackendSync *backend,
				      PASBook    *book,
				      PASCreateCardRequest *req,
				      char **id_out)
{
	char *id;
	PASBackendFile *bf = PAS_BACKEND_FILE (backend);

	id = do_create(bf, req->vcard);
	if (id) {
		pas_backend_summary_add_card (bf->priv->summary, req->vcard);

		*id_out = id;
		return GNOME_Evolution_Addressbook_Success;
	}
	else {
		/* XXX need a different call status for this case, i
                   think */
		*id_out = g_strdup ("");
		return GNOME_Evolution_Addressbook_CardNotFound;
	}
}

static PASBackendSyncStatus
pas_backend_file_process_remove_cards (PASBackendSync *backend,
				       PASBook    *book,
				       PASRemoveCardsRequest *req,
				       GList **ids)
{
	PASBackendFile *bf = PAS_BACKEND_FILE (backend);
	DB             *db = bf->priv->file_db;
	DBT            id_dbt, vcard_dbt;
	int            db_error;
	char          *id;
	GList         *l;
	GList         *removed_cards = NULL;
	GNOME_Evolution_Addressbook_CallStatus rv = GNOME_Evolution_Addressbook_Success;

	for (l = req->ids; l; l = l->next) {
		id = l->data;

		string_to_dbt (id, &id_dbt);
		memset (&vcard_dbt, 0, sizeof (vcard_dbt));

		db_error = db->get (db, NULL, &id_dbt, &vcard_dbt, 0);
		if (0 != db_error) {
			rv = GNOME_Evolution_Addressbook_CardNotFound;
			continue;
		}
	
		db_error = db->del (db, NULL, &id_dbt, 0);
		if (0 != db_error) {
			rv = GNOME_Evolution_Addressbook_CardNotFound;
			continue;
		}

		removed_cards = g_list_prepend (removed_cards, id);
	}

	/* if we actually removed some, try to sync */
	if (removed_cards) {
		db_error = db->sync (db, 0);
		if (db_error != 0)
			g_warning ("db->sync failed.\n");
	}

	*ids = removed_cards;

	for (l = removed_cards; l; l = l->next) {
		char *id = l->data;
		pas_backend_summary_remove_card (bf->priv->summary, id);
	}

	g_list_free (removed_cards);

	return rv;
}

static PASBackendSyncStatus
pas_backend_file_process_modify_card (PASBackendSync *backend,
				      PASBook    *book,
				      PASModifyCardRequest *req,
				      char **old_vcard)
{
	PASBackendFile *bf = PAS_BACKEND_FILE (backend);
	DB             *db = bf->priv->file_db;
	DBT            id_dbt, vcard_dbt;
	int            db_error;
	EContact      *contact;
	char          *id, *lookup_id;

	/* create a new ecard from the request data */
	contact = e_contact_new_from_vcard (req->vcard);
	id = e_contact_get(contact, E_CONTACT_UID);

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
	g_free (id);
	memset (&vcard_dbt, 0, sizeof (vcard_dbt));

	/* get the old ecard - the one that's presently in the db */
	db_error = db->get (db, NULL, &id_dbt, &vcard_dbt, 0);
	if (0 != db_error)
		return GNOME_Evolution_Addressbook_CardNotFound;

	*old_vcard = g_strdup(vcard_dbt.data);

	string_to_dbt (req->vcard, &vcard_dbt);	

	db_error = db->put (db, NULL, &id_dbt, &vcard_dbt, 0);

	if (0 == db_error) {
		db_error = db->sync (db, 0);
		if (db_error != 0)
			g_warning ("db->sync failed.\n");

		pas_backend_summary_remove_card (bf->priv->summary, id);
		pas_backend_summary_add_card (bf->priv->summary, req->vcard);
	}

	g_object_unref(contact);

	if (0 == db_error)
		return GNOME_Evolution_Addressbook_Success;
	else
		return GNOME_Evolution_Addressbook_CardNotFound;
}

static PASBackendSyncStatus
pas_backend_file_process_get_vcard (PASBackendSync *backend,
				    PASBook    *book,
				    PASGetVCardRequest *req,
				    char **vcard)
{
	PASBackendFile *bf;
	DB             *db;
	DBT             id_dbt, vcard_dbt;
	int             db_error = 0;

	bf = PAS_BACKEND_FILE (pas_book_get_backend (book));
	db = bf->priv->file_db;

	string_to_dbt (req->id, &id_dbt);
	memset (&vcard_dbt, 0, sizeof (vcard_dbt));

	db_error = db->get (db, NULL, &id_dbt, &vcard_dbt, 0);

	if (db_error == 0) {
		*vcard = g_strdup (vcard_dbt.data);
		return GNOME_Evolution_Addressbook_Success;
	} else {
		*vcard = g_strdup ("");
		return GNOME_Evolution_Addressbook_CardNotFound;
	}
}

static PASBackendSyncStatus
pas_backend_file_process_get_card_list (PASBackendSync *backend,
					PASBook    *book,
					PASGetCardListRequest *req,
					GList **cards)
{
	PASBackendFile *bf = PAS_BACKEND_FILE (backend);
	DB             *db = bf->priv->file_db;
	DBC            *dbc;
	int            db_error;
	DBT  id_dbt, vcard_dbt;
	PASBackendCardSExp *card_sexp = NULL;
	gboolean search_needed;
	const char *search = req->query;
	GList *card_list = NULL;

	printf ("pas_backend_file_process_get_card_list (%s)\n", search);

	search_needed = TRUE;

	if (!strcmp (search, "(contains \"x-evolution-any-field\" \"\")"))
		search_needed = FALSE;

	card_sexp = pas_backend_card_sexp_new (search);
	if (!card_sexp) {
		/* XXX this needs to be an invalid query error of some sort*/
		return GNOME_Evolution_Addressbook_CardNotFound;
	}

	db_error = db->cursor (db, NULL, &dbc, 0);

	if (db_error != 0) {
		/* XXX this needs to be some CouldNotOpen error */
		return GNOME_Evolution_Addressbook_CardNotFound;
	}

	memset (&vcard_dbt, 0, sizeof (vcard_dbt));
	memset (&id_dbt, 0, sizeof (id_dbt));
	db_error = dbc->c_get(dbc, &id_dbt, &vcard_dbt, DB_FIRST);

	while (db_error == 0) {

		/* don't include the version in the list of cards */
		if (id_dbt.size != strlen(PAS_BACKEND_FILE_VERSION_NAME) + 1
		    || strcmp (id_dbt.data, PAS_BACKEND_FILE_VERSION_NAME)) {

			if ((!search_needed) || (card_sexp != NULL && pas_backend_card_sexp_match_vcard  (card_sexp, vcard_dbt.data))) {
				card_list = g_list_append (card_list, g_strdup (vcard_dbt.data));
			}
		}

		db_error = dbc->c_get(dbc, &id_dbt, &vcard_dbt, DB_NEXT);

	}

	*cards = card_list;
	return db_error != DB_NOTFOUND
		? GNOME_Evolution_Addressbook_OtherError
		: GNOME_Evolution_Addressbook_Success;
}

static void
pas_backend_file_start_book_view (PASBackend  *backend,
				  PASBookView *book_view)
{
	pas_backend_file_search (PAS_BACKEND_FILE (backend), book_view);
}

#if 0
static void
pas_backend_file_process_get_completion_view (PASBackendSync *backend,
					      PASBook    *book,
					      PASGetCompletionViewRequest *req)
{
	PASBackendFile *bf = PAS_BACKEND_FILE (backend);
	PASBookView       *book_view;
	PASBackendFileBookView view;
	EIterator *iterator;

	bonobo_object_ref(BONOBO_OBJECT(book));

	book_view = pas_book_view_new (req->listener);

	g_object_weak_ref (G_OBJECT (book_view), view_destroy, book);

	view.book_view = book_view;
	view.search = g_strdup (req->search);
	view.card_sexp = NULL;
	view.change_id = NULL;
	view.change_context = NULL;	

	e_list_append(bf->priv->book_views, &view);

	pas_book_respond_get_completion_view (book,
		   (book_view != NULL
		    ? GNOME_Evolution_Addressbook_BookListener_Success 
		    : GNOME_Evolution_Addressbook_BookListener_CardNotFound /* XXX */),
		   book_view);

	if (!pas_backend_is_loaded (backend))
		return;

	iterator = e_list_get_iterator(bf->priv->book_views);
	e_iterator_last(iterator);
	pas_backend_file_search (bf, book, e_iterator_get(iterator), TRUE);
	g_object_unref(iterator);
}
#endif

static PASBackendSyncStatus
pas_backend_file_process_get_changes (PASBackendSync *backend,
				      PASBook    *book,
				      PASGetChangesRequest *req,
				      GList **changes_out)
{
#if notyet
	PASBackendFile *bf = PAS_BACKEND_FILE (backend);
	PASBookView       *book_view;
	PASBackendFileBookView view;
	PASBackendFileChangeContext ctx;
	EIterator *iterator;

	bonobo_object_ref(BONOBO_OBJECT(book));

	book_view = pas_book_view_new (req->listener);

	g_object_weak_ref (G_OBJECT (book_view), view_destroy, book);

	pas_book_respond_get_changes (book,
		   (book_view != NULL
		    ? GNOME_Evolution_Addressbook_Success 
		    : GNOME_Evolution_Addressbook_CardNotFound /* XXX */),
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
	
	e_list_append(bf->priv->book_views, &view);

	if (!pas_backend_is_loaded (backend))
		return;

	iterator = e_list_get_iterator(bf->priv->book_views);
	e_iterator_last(iterator);
	pas_backend_file_changes (bf, book, e_iterator_get(iterator));
	g_object_unref(iterator);
#endif
}

static char *
pas_backend_file_extract_path_from_uri (const char *uri)
{
	g_assert (strncasecmp (uri, "file:", 5) == 0);

	return g_strdup (uri + 5);
}

static PASBackendSyncStatus
pas_backend_file_process_authenticate_user (PASBackendSync *backend,
					    PASBook    *book,
					    PASAuthenticateUserRequest *req)
{
	return GNOME_Evolution_Addressbook_Success;
}

static PASBackendSyncStatus
pas_backend_file_process_get_supported_fields (PASBackendSync *backend,
					       PASBook    *book,
					       PASGetSupportedFieldsRequest *req,
					       GList **fields_out)
{
	GList *fields = NULL;
	int i;

	/* XXX we need a way to say "we support everything", since the
	   file backend does */
	for (i = 0; i < E_CONTACT_FIELD_LAST; i ++)
		fields = g_list_append (fields, (char*)e_contact_field_name (i));		

	*fields_out = fields;
	return GNOME_Evolution_Addressbook_Success;
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
				EContact *contact;

				contact = e_contact_new_from_vcard (vcard_dbt.data);

				/* the cards we're looking for are
				   created with a normal id dbt, but
				   with the id field in the vcard set
				   to something that doesn't match.
				   so, we need to modify the card to
				   have the same id as the the dbt. */
				if (strcmp (id_dbt.data, e_contact_get_const (contact, E_CONTACT_UID))) {
					char *vcard;

					e_contact_set (contact, E_CONTACT_UID, id_dbt.data);

					vcard = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);
					string_to_dbt (vcard, &vcard_dbt);

					db_error = db->put (db, NULL,
							    &id_dbt, &vcard_dbt, 0);

					g_free (vcard);

					if (db_error != 0)
						card_failed++;
				}

				g_object_unref (contact);
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
TEL;WORK;VOICE:(617) 375-3800\n\
TEL;WORK;FAX:(617) 236-8630\n\
EMAIL;INTERNET:hello@ximian.com\n\
URL:www.ximian.com/\n\
ORG:Ximian, Inc.;\n\
NOTE:Welcome to the Ximian Addressbook.\n\
END:VCARD"

static GNOME_Evolution_Addressbook_CallStatus
pas_backend_file_load_uri (PASBackend             *backend,
			   const char             *uri,
			   gboolean                only_if_exists)
{
	PASBackendFile *bf = PAS_BACKEND_FILE (backend);
	char           *filename;
	gboolean        writable = FALSE;
	int             db_error;
	DB *db;
	int major, minor, patch;
	time_t db_mtime;
	struct stat sb;
	char *summary_filename;

	g_free(bf->priv->uri);
	bf->priv->uri = g_strdup (uri);

	db_version (&major, &minor, &patch);

	if (major != 3 ||
	    minor != 1 ||
	    patch != 17) {
		g_warning ("Wrong version of libdb.");
		return GNOME_Evolution_Addressbook_OtherError;
	}

	filename = pas_backend_file_extract_path_from_uri (uri);

	db_error = e_db3_utils_maybe_recover (filename);
	if (db_error != 0)
		return GNOME_Evolution_Addressbook_OtherError;

	db_error = db_create (&db, NULL, 0);
	if (db_error != 0)
		return GNOME_Evolution_Addressbook_OtherError;

	db_error = db->open (db, filename, NULL, DB_HASH, 0, 0666);

	if (db_error == DB_OLD_VERSION) {
		db_error = e_db3_utils_upgrade_format (filename);

		if (db_error != 0)
			return GNOME_Evolution_Addressbook_OtherError;

		db_error = db->open (db, filename, NULL, DB_HASH, 0, 0666);
	}

	bf->priv->file_db = db;

	if (db_error == 0) {
		writable = TRUE;
	} else {
		db_error = db->open (db, filename, NULL, DB_HASH, DB_RDONLY, 0666);

		if (db_error != 0) {
			db_error = db->open (db, filename, NULL, DB_HASH, DB_CREATE, 0666);

			if (db_error == 0 && !only_if_exists) {
				char *id;
				id = do_create(bf, INITIAL_VCARD);
				g_free (id);
				/* XXX check errors here */

				writable = TRUE;
			}
		}
	}

	if (db_error != 0) {
		bf->priv->file_db = NULL;
		return GNOME_Evolution_Addressbook_OtherError;
	}

	if (!pas_backend_file_maybe_upgrade_db (bf)) {
		db->close (db, 0);
		bf->priv->file_db = NULL;
		return GNOME_Evolution_Addressbook_OtherError;
	}

	g_free (bf->priv->filename);
	bf->priv->filename = filename;

	if (stat (bf->priv->filename, &sb) == -1) {
		db->close (db, 0);
		bf->priv->file_db = NULL;
		return GNOME_Evolution_Addressbook_OtherError;
	}
	db_mtime = sb.st_mtime;

	summary_filename = g_strconcat (bf->priv->filename, ".summary", NULL);
	bf->priv->summary = pas_backend_summary_new (summary_filename, SUMMARY_FLUSH_TIMEOUT);
	g_free (summary_filename);

	if (pas_backend_summary_is_up_to_date (bf->priv->summary, db_mtime) == FALSE
	    || pas_backend_summary_load (bf->priv->summary) == FALSE ) {
		build_summary (bf->priv);
	}

	pas_backend_set_is_loaded (backend, TRUE);
	pas_backend_set_is_writable (backend, writable);
	return GNOME_Evolution_Addressbook_Success;
}

static char *
pas_backend_file_get_static_capabilities (PASBackend *backend)
{
	return g_strdup("local,do-initial-query,bulk-removes");
}

static GNOME_Evolution_Addressbook_CallStatus
pas_backend_file_cancel_operation (PASBackend *backend, PASBook *book, PASCancelOperationRequest *req)
{
	return GNOME_Evolution_Addressbook_CouldNotCancel;
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

	backend = g_object_new (PAS_TYPE_BACKEND_FILE, NULL);

	if (! pas_backend_file_construct (backend)) {
		g_object_unref (backend);

		return NULL;
	}

	return PAS_BACKEND (backend);
}

static void
pas_backend_file_dispose (GObject *object)
{
	PASBackendFile *bf;

	bf = PAS_BACKEND_FILE (object);

	if (bf->priv) {
		if (bf->priv->summary)
			g_object_unref(bf->priv->summary);
		g_free (bf->priv->uri);
		g_free (bf->priv->filename);

		g_free (bf->priv);
		bf->priv = NULL;
	}

	G_OBJECT_CLASS (pas_backend_file_parent_class)->dispose (object);	
}

static void
pas_backend_file_class_init (PASBackendFileClass *klass)
{
	GObjectClass    *object_class = G_OBJECT_CLASS (klass);
	PASBackendSyncClass *sync_class;
	PASBackendClass *backend_class;

	pas_backend_file_parent_class = g_type_class_peek_parent (klass);

	sync_class = PAS_BACKEND_SYNC_CLASS (klass);
	backend_class = PAS_BACKEND_CLASS (klass);

	/* Set the virtual methods. */
	backend_class->load_uri                = pas_backend_file_load_uri;
	backend_class->get_static_capabilities = pas_backend_file_get_static_capabilities;
	backend_class->start_book_view         = pas_backend_file_start_book_view;
	backend_class->cancel_operation        = pas_backend_file_cancel_operation;

	sync_class->create_card_sync           = pas_backend_file_process_create_card;
	sync_class->remove_cards_sync          = pas_backend_file_process_remove_cards;
	sync_class->modify_card_sync           = pas_backend_file_process_modify_card;
	sync_class->get_vcard_sync             = pas_backend_file_process_get_vcard;
	sync_class->get_card_list_sync         = pas_backend_file_process_get_card_list;
	sync_class->get_changes_sync           = pas_backend_file_process_get_changes;
	sync_class->authenticate_user_sync     = pas_backend_file_process_authenticate_user;
	sync_class->get_supported_fields_sync  = pas_backend_file_process_get_supported_fields;

	object_class->dispose = pas_backend_file_dispose;
}

static void
pas_backend_file_init (PASBackendFile *backend)
{
	PASBackendFilePrivate *priv;

	priv             = g_new0 (PASBackendFilePrivate, 1);
	priv->uri        = NULL;

	backend->priv = priv;
}

/**
 * pas_backend_file_get_type:
 */
GType
pas_backend_file_get_type (void)
{
	static GType type = 0;

	if (! type) {
		GTypeInfo info = {
			sizeof (PASBackendFileClass),
			NULL, /* base_class_init */
			NULL, /* base_class_finalize */
			(GClassInitFunc)  pas_backend_file_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (PASBackendFile),
			0,    /* n_preallocs */
			(GInstanceInitFunc) pas_backend_file_init
		};

		type = g_type_register_static (PAS_TYPE_BACKEND_SYNC, "PASBackendFile", &info, 0);
	}

	return type;
}
