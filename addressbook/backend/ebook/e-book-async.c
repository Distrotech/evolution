/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#define BUILDING_E_BOOK_ASYNC
#include "ebook/e-book-async.h"

static GThread *worker_thread;
static GAsyncQueue *to_worker_queue;
static GAsyncQueue *from_worker_queue;

typedef struct _EBookMsg EBookMsg;

typedef void (*EBookMsgHandler)(EBookMsg* msg);
typedef void (*EBookMsgDtor)(EBookMsg* msg);

struct _EBookMsg {
	EBookMsgHandler handler;
	EBookMsgDtor dtor;
};

static gpointer
worker (gpointer data)
{
	while (TRUE) {
		EBookMsg *msg = g_async_queue_pop (to_worker_queue);
		msg->handler (msg);
		msg->dtor (msg);
	}

	return NULL;
}

static gboolean
main_thread_check_for_response (gpointer data)
{
	EBookMsg *msg;

	while ((msg = g_async_queue_try_pop (from_worker_queue)) != NULL) {
		msg->handler (msg);
		msg->dtor (msg);
	}
	
	return TRUE;
}

static void
e_book_msg_init (EBookMsg *msg, EBookMsgHandler handler, EBookMsgDtor dtor)
{
	msg->handler = handler;
	msg->dtor = dtor;
}

static void
init_async()
{
	static gboolean init_done = FALSE;
	if (!init_done) {
		init_done = TRUE;
		to_worker_queue = g_async_queue_new ();
		from_worker_queue = g_async_queue_new ();
		worker_thread = g_thread_create (worker, NULL, FALSE, NULL);
		g_timeout_add (300, main_thread_check_for_response, NULL);
	}
}



typedef struct {
	EBookMsg msg;

	EBook *book;
	char *uri;
	EBookCallback open_response;
	gpointer closure;
} LoadUriMsg;

typedef struct {
	EBookMsg msg;

	EBook *book;
	EBookStatus status;
	EBookCallback open_response;
	gpointer closure;
} LoadUriResponse;

static void
_load_uri_response_handler (EBookMsg *msg)
{
	LoadUriResponse *resp = (LoadUriResponse*)msg;

	resp->open_response (resp->book, resp->status, resp->closure);
}

static void
_load_uri_response_dtor (EBookMsg *msg)
{
	LoadUriResponse *resp = (LoadUriResponse*)msg;

	g_object_unref (resp->book);
	g_free (resp);
}

static void
_load_uri_handler (EBookMsg *msg)
{
	LoadUriMsg *uri_msg = (LoadUriMsg *)msg;
	LoadUriResponse *response;

	response = g_new (LoadUriResponse, 1);
	e_book_msg_init ((EBookMsg*)response, _load_uri_response_handler, _load_uri_response_dtor);

	response->status = e_book_load_uri (uri_msg->book, uri_msg->uri);
	response->book = uri_msg->book;
	response->open_response = uri_msg->open_response;
	response->closure = uri_msg->closure;

	g_async_queue_push (from_worker_queue, response);
}

static void
_load_uri_dtor (EBookMsg *msg)
{
	LoadUriMsg *uri_msg = (LoadUriMsg *)msg;
	
	g_free (uri_msg->uri);
	g_free (uri_msg);
}

void
e_book_async_load_uri (EBook                 *book,
		       const char            *uri,
		       EBookCallback          open_response,
		       gpointer               closure)
{
	LoadUriMsg *msg;

	init_async ();

	msg = g_new (LoadUriMsg, 1);
	e_book_msg_init ((EBookMsg*)msg, _load_uri_handler, _load_uri_dtor);

	msg->book = g_object_ref (book);
	msg->uri = g_strdup (uri);
	msg->open_response = open_response;
	msg->closure = closure;

	g_async_queue_push (to_worker_queue, msg);
}



typedef struct {
	EBookMsg msg;

	EBook *book;
	EBookFieldsCallback cb;
	gpointer closure;
} GetFieldsMsg;

typedef struct {
	EBookMsg msg;

	EBook *book;
	EBookStatus status;
	GList *fields;
	EBookFieldsCallback cb;
	gpointer closure;
} GetFieldsResponse;

static void
_get_fields_response_handler (EBookMsg *msg)
{
	GetFieldsResponse *resp = (GetFieldsResponse*)msg;
	GList *l;
	EList *fields = e_list_new ((EListCopyFunc) g_strdup, 
				    (EListFreeFunc) g_free,
				    NULL);

	for (l = resp->fields; l; l = l->next)
		e_list_append (fields, l->data);

	resp->cb (resp->book, resp->status, fields, resp->closure);

	g_object_unref (fields);
}

static void
_get_fields_response_dtor (EBookMsg *msg)
{
	GetFieldsResponse *resp = (GetFieldsResponse*)msg;

	g_list_foreach (resp->fields, (GFunc)g_free, NULL);
	g_list_free (resp->fields);
	g_object_unref (resp->book);
	g_free (resp);
}

static void
_get_fields_handler (EBookMsg *msg)
{
	GetFieldsMsg *fields_msg = (GetFieldsMsg *)msg;
	GetFieldsResponse *response;

	response = g_new (GetFieldsResponse, 1);
	e_book_msg_init ((EBookMsg*)response, _get_fields_response_handler, _get_fields_response_dtor);

	response->status = e_book_get_supported_fields (fields_msg->book, &response->fields);
	response->book = fields_msg->book;
	response->cb = fields_msg->cb;
	response->closure = fields_msg->closure;

	g_async_queue_push (from_worker_queue, response);
}

guint
e_book_async_get_supported_fields (EBook                 *book,
				   EBookFieldsCallback    cb,
				   gpointer               closure)
{
	GetFieldsMsg *msg;

	init_async ();

	msg = g_new (GetFieldsMsg, 1);
	e_book_msg_init ((EBookMsg*)msg, _get_fields_handler, (EBookMsgDtor)g_free);

	msg->book = g_object_ref (book);
	msg->cb = cb;
	msg->closure = closure;

	g_async_queue_push (to_worker_queue, msg);

	return 0;
}



typedef struct {
	EBookMsg msg;

	EBook *book;
	EBookAuthMethodsCallback cb;
	gpointer closure;
} GetMethodsMsg;

typedef struct {
	EBookMsg msg;

	EBook *book;
	EBookStatus status;
	GList *methods;
	EBookAuthMethodsCallback cb;
	gpointer closure;
} GetMethodsResponse;

static void
_get_methods_response_handler (EBookMsg *msg)
{
	GetMethodsResponse *resp = (GetMethodsResponse*)msg;
	GList *l;
	EList *methods = e_list_new ((EListCopyFunc) g_strdup, 
				    (EListFreeFunc) g_free,
				    NULL);

	for (l = resp->methods; l; l = l->next)
		e_list_append (methods, l->data);

	resp->cb (resp->book, resp->status, methods, resp->closure);

	g_object_unref (methods);
}

static void
_get_methods_response_dtor (EBookMsg *msg)
{
	GetMethodsResponse *resp = (GetMethodsResponse*)msg;

	g_list_foreach (resp->methods, (GFunc)g_free, NULL);
	g_list_free (resp->methods);
	g_object_unref (resp->book);
	g_free (resp);
}

static void
_get_methods_handler (EBookMsg *msg)
{
	GetMethodsMsg *methods_msg = (GetMethodsMsg *)msg;
	GetMethodsResponse *response;

	response = g_new (GetMethodsResponse, 1);
	e_book_msg_init ((EBookMsg*)response, _get_methods_response_handler, _get_methods_response_dtor);

	response->status = e_book_get_supported_auth_methods (methods_msg->book, &response->methods);
	response->book = methods_msg->book;
	response->cb = methods_msg->cb;
	response->closure = methods_msg->closure;

	g_async_queue_push (from_worker_queue, response);
}

guint
e_book_async_get_supported_auth_methods (EBook                    *book,
					 EBookAuthMethodsCallback  cb,
					 gpointer                  closure)
{
	GetMethodsMsg *msg;

	init_async ();

	msg = g_new (GetMethodsMsg, 1);
	e_book_msg_init ((EBookMsg*)msg, _get_methods_handler, (EBookMsgDtor)g_free);

	msg->book = g_object_ref (book);
	msg->cb = cb;
	msg->closure = closure;

	g_async_queue_push (to_worker_queue, msg);

	return 0;
}



typedef struct {
	EBookMsg msg;

	EBook *book;
	char *user;
	char *passwd;
	char *auth_method;
	EBookCallback cb;
	gpointer closure;
} AuthUserMsg;

typedef struct {
	EBookMsg msg;

	EBook *book;
	EBookStatus status;
	EBookCallback cb;
	gpointer closure;
} AuthUserResponse;

static void
_auth_user_response_handler (EBookMsg *msg)
{
	AuthUserResponse *resp = (AuthUserResponse*)msg;

	resp->cb (resp->book, resp->status, resp->closure);
}

static void
_auth_user_response_dtor (EBookMsg *msg)
{
	AuthUserResponse *resp = (AuthUserResponse*)msg;

	g_object_unref (resp->book);
	g_free (resp);
}

static void
_auth_user_handler (EBookMsg *msg)
{
	AuthUserMsg *auth_msg = (AuthUserMsg *)msg;
	AuthUserResponse *response;

	response = g_new (AuthUserResponse, 1);
	e_book_msg_init ((EBookMsg*)response, _auth_user_response_handler, _auth_user_response_dtor);

	response->status = e_book_authenticate_user (auth_msg->book, auth_msg->user, auth_msg->passwd, auth_msg->auth_method);
	response->book = auth_msg->book;
	response->cb = auth_msg->cb;
	response->closure = auth_msg->closure;

	g_async_queue_push (from_worker_queue, response);
}

static void
_auth_user_dtor (EBookMsg *msg)
{
	AuthUserMsg *auth_msg = (AuthUserMsg *)msg;

	g_free (auth_msg->user);
	g_free (auth_msg->passwd);
	g_free (auth_msg->auth_method);

	g_free (auth_msg);
}

/* User authentication. */
void
e_book_async_authenticate_user (EBook                 *book,
				const char            *user,
				const char            *passwd,
				const char            *auth_method,
				EBookCallback         cb,
				gpointer              closure)
{
	AuthUserMsg *msg;

	init_async ();

	msg = g_new (AuthUserMsg, 1);
	e_book_msg_init ((EBookMsg*)msg, _auth_user_handler, _auth_user_dtor);

	msg->book = g_object_ref (book);
	msg->user = g_strdup (user);
	msg->passwd = g_strdup (passwd);
	msg->auth_method = g_strdup (auth_method);
	msg->cb = cb;
	msg->closure = closure;

	g_async_queue_push (to_worker_queue, msg);
}



typedef struct {
	EBookMsg msg;

	EBook *book;
	char *id;
	EBookCardCallback cb;
	gpointer closure;
} GetCardMsg;

typedef struct {
	EBookMsg msg;

	EBook *book;
	EContact *contact;
	EBookStatus status;
	EBookCardCallback cb;
	gpointer closure;
} GetCardResponse;

static void
_get_card_response_handler (EBookMsg *msg)
{
	GetCardResponse *resp = (GetCardResponse*)msg;

	resp->cb (resp->book, resp->status, resp->contact, resp->closure);
}

static void
_get_card_response_dtor (EBookMsg *msg)
{
	GetCardResponse *resp = (GetCardResponse*)msg;

	g_object_unref (resp->contact);
	g_object_unref (resp->book);
	g_free (resp);
}

static void
_get_card_handler (EBookMsg *msg)
{
	GetCardMsg *get_card_msg = (GetCardMsg *)msg;
	GetCardResponse *response;

	response = g_new (GetCardResponse, 1);
	e_book_msg_init ((EBookMsg*)response, _get_card_response_handler, _get_card_response_dtor);

	response->status = e_book_get_contact (get_card_msg->book, get_card_msg->id, &response->contact);
	response->book = get_card_msg->book;
	response->cb = get_card_msg->cb;
	response->closure = get_card_msg->closure;

	g_async_queue_push (from_worker_queue, response);
}

static void
_get_card_dtor (EBookMsg *msg)
{
	GetCardMsg *get_card_msg = (GetCardMsg *)msg;

	g_free (get_card_msg->id);
	g_free (get_card_msg);
}

/* Fetching cards. */
guint
e_book_async_get_card (EBook                 *book,
		       const char            *id,
		       EBookCardCallback      cb,
		       gpointer               closure)
{
	GetCardMsg *msg;

	init_async ();

	msg = g_new (GetCardMsg, 1);
	e_book_msg_init ((EBookMsg*)msg, _get_card_handler, _get_card_dtor);

	msg->book = g_object_ref (book);
	msg->id = g_strdup (id);
	msg->cb = cb;
	msg->closure = closure;

	g_async_queue_push (to_worker_queue, msg);

	return 0;
}



/* Deleting cards. */
gboolean
e_book_async_remove_card (EBook                 *book,
			  ECard                 *card,
			  EBookCallback          cb,
			  gpointer               closure)
{
	const char *id = e_card_get_id (card);

	return e_book_async_remove_card_by_id (book, id, cb, closure);
}

gboolean
e_book_async_remove_card_by_id (EBook                 *book,
				const char            *id,
				EBookCallback          cb,
				gpointer               closure)
{
	GList *list = g_list_append (NULL, g_strdup (id));

	return e_book_async_remove_cards (book, list, cb, closure);
}


typedef struct {
	EBookMsg msg;

	EBook *book;
	GList *id_list;
	EBookCallback cb;
	gpointer closure;
} RemoveCardsMsg;

typedef struct {
	EBookMsg msg;

	EBook *book;
	EBookStatus status;
	EBookCallback cb;
	gpointer closure;
} RemoveCardsResponse;

static void
_remove_cards_response_handler (EBookMsg *msg)
{
	RemoveCardsResponse *resp = (RemoveCardsResponse*)msg;

	resp->cb (resp->book, resp->status, resp->closure);
}

static void
_remove_cards_response_dtor (EBookMsg *msg)
{
	RemoveCardsResponse *resp = (RemoveCardsResponse*)msg;

	g_object_unref (resp->book);
	g_free (resp);
}

static void
_remove_cards_handler (EBookMsg *msg)
{
	RemoveCardsMsg *remove_cards_msg = (RemoveCardsMsg *)msg;
	RemoveCardsResponse *response;

	response = g_new (RemoveCardsResponse, 1);
	e_book_msg_init ((EBookMsg*)response, _remove_cards_response_handler, _remove_cards_response_dtor);

	response->status = e_book_remove_contacts (remove_cards_msg->book, remove_cards_msg->id_list);
	response->book = remove_cards_msg->book;
	response->cb = remove_cards_msg->cb;
	response->closure = remove_cards_msg->closure;

	g_async_queue_push (from_worker_queue, response);
}

static void
_remove_cards_dtor (EBookMsg *msg)
{
	RemoveCardsMsg *remove_cards_msg = (RemoveCardsMsg *)msg;

	/* XXX ugh, free the list? */

	g_free (remove_cards_msg);
}

gboolean
e_book_async_remove_cards (EBook                 *book,
			   GList                 *id_list,
			   EBookCallback          cb,
			   gpointer               closure)
{
	RemoveCardsMsg *msg;

	init_async ();

	msg = g_new (RemoveCardsMsg, 1);
	e_book_msg_init ((EBookMsg*)msg, _remove_cards_handler, _remove_cards_dtor);

	msg->book = g_object_ref (book);
	msg->id_list = id_list;
	msg->cb = cb;
	msg->closure = closure;

	g_async_queue_push (to_worker_queue, msg);

	return 0;
}



/* Adding cards. */
gboolean
e_book_async_add_card (EBook                 *book,
		       ECard                 *card,
		       EBookIdCallback        cb,
		       gpointer               closure)
{
	gboolean rv;

	char *vcard = e_card_get_vcard_assume_utf8 (card);

	rv = e_book_async_add_vcard (book, vcard, cb, closure);

	g_free (vcard);

	return rv;
}

typedef struct {
	EBookMsg msg;

	EBook *book;
	char *vcard;
	EBookIdCallback cb;
	gpointer closure;
} AddVCardMsg;

typedef struct {
	EBookMsg msg;

	EBook *book;
	char *id;
	EBookStatus status;
	EBookIdCallback cb;
	gpointer closure;
} AddVCardResponse;

static void
_add_vcard_response_handler (EBookMsg *msg)
{
	AddVCardResponse *resp = (AddVCardResponse*)msg;

	resp->cb (resp->book, resp->status, resp->id, resp->closure);
}

static void
_add_vcard_response_dtor (EBookMsg *msg)
{
	AddVCardResponse *resp = (AddVCardResponse*)msg;

	g_object_unref (resp->book);
	g_free (resp->id);
	g_free (resp);
}

static void
_add_vcard_handler (EBookMsg *msg)
{
	AddVCardMsg *add_vcard_msg = (AddVCardMsg *)msg;
	AddVCardResponse *response;
	EContact *contact;

	response = g_new (AddVCardResponse, 1);
	e_book_msg_init ((EBookMsg*)response, _add_vcard_response_handler, _add_vcard_response_dtor);

	contact = e_contact_new_from_vcard (add_vcard_msg->vcard);
	response->status = e_book_add_contact (add_vcard_msg->book, contact);
	response->book = add_vcard_msg->book;
	response->cb = add_vcard_msg->cb;
	response->closure = add_vcard_msg->closure;

	g_object_unref (contact);
	g_async_queue_push (from_worker_queue, response);
}

static void
_add_vcard_dtor (EBookMsg *msg)
{
	AddVCardMsg *add_vcard_msg = (AddVCardMsg *)msg;

	g_free (add_vcard_msg->vcard);
	g_free (add_vcard_msg);
}

gboolean
e_book_async_add_vcard (EBook                 *book,
			const char            *vcard,
			EBookIdCallback        cb,
			gpointer               closure)
{
	AddVCardMsg *msg;

	init_async ();

	msg = g_new (AddVCardMsg, 1);
	e_book_msg_init ((EBookMsg*)msg, _add_vcard_handler, _add_vcard_dtor);

	msg->book = g_object_ref (book);
	msg->vcard = g_strdup (vcard);
	msg->cb = cb;
	msg->closure = closure;

	g_async_queue_push (to_worker_queue, msg);

	return TRUE;
}



/* Modifying cards. */
gboolean
e_book_async_commit_card (EBook                 *book,
			  ECard                 *card,
			  EBookCallback          cb,
			  gpointer               closure)
{
	gboolean rv;

	char *vcard = e_card_get_vcard_assume_utf8 (card);

	rv = e_book_async_commit_vcard (book, vcard, cb, closure);

	g_free (vcard);

	return rv;
}

typedef struct {
	EBookMsg msg;

	EBook *book;
	char *vcard;
	EBookCallback cb;
	gpointer closure;
} CommitVCardMsg;

typedef struct {
	EBookMsg msg;

	EBook *book;
	EBookStatus status;
	EBookCallback cb;
	gpointer closure;
} CommitVCardResponse;

static void
_commit_vcard_response_handler (EBookMsg *msg)
{
	CommitVCardResponse *resp = (CommitVCardResponse*)msg;

	resp->cb (resp->book, resp->status, resp->closure);
}

static void
_commit_vcard_response_dtor (EBookMsg *msg)
{
	CommitVCardResponse *resp = (CommitVCardResponse*)msg;

	g_object_unref (resp->book);
	g_free (resp);
}

static void
_commit_vcard_handler (EBookMsg *msg)
{
	CommitVCardMsg *commit_vcard_msg = (CommitVCardMsg *)msg;
	CommitVCardResponse *response;
	EContact *contact;

	response = g_new (CommitVCardResponse, 1);
	e_book_msg_init ((EBookMsg*)response, _commit_vcard_response_handler, _commit_vcard_response_dtor);

	contact = e_contact_new_from_vcard (commit_vcard_msg->vcard);
	response->status = e_book_commit_contact (commit_vcard_msg->book, contact);
	response->book = commit_vcard_msg->book;
	response->cb = commit_vcard_msg->cb;
	response->closure = commit_vcard_msg->closure;

	g_object_unref (contact);
	g_async_queue_push (from_worker_queue, response);
}

static void
_commit_vcard_dtor (EBookMsg *msg)
{
	CommitVCardMsg *commit_vcard_msg = (CommitVCardMsg *)msg;

	g_free (commit_vcard_msg->vcard);
	g_free (commit_vcard_msg);
}


gboolean
e_book_async_commit_vcard (EBook                 *book,
			   const char            *vcard,
			   EBookCallback          cb,
			   gpointer               closure)
{
	CommitVCardMsg *msg;

	init_async ();

	msg = g_new (CommitVCardMsg, 1);
	e_book_msg_init ((EBookMsg*)msg, _commit_vcard_handler, _commit_vcard_dtor);

	msg->book = g_object_ref (book);
	msg->vcard = g_strdup (vcard);
	msg->cb = cb;
	msg->closure = closure;

	g_async_queue_push (to_worker_queue, msg);

	return TRUE;
}

typedef struct {
	EBookMsg msg;

	EBook *book;
	char *query;
	EBookBookViewCallback cb;
	gpointer closure;
} GetBookViewMsg;

typedef struct {
	EBookMsg msg;

	EBook *book;
	EBookStatus status;
	EBookView *book_view;
	EBookBookViewCallback cb;
	gpointer closure;
} GetBookViewResponse;

static void
_get_book_view_response_handler (EBookMsg *msg)
{
	GetBookViewResponse *resp = (GetBookViewResponse*)msg;

	resp->cb (resp->book, resp->status, resp->book_view, resp->closure);
}

static void
_get_book_view_response_dtor (EBookMsg *msg)
{
	GetBookViewResponse *resp = (GetBookViewResponse*)msg;

	g_object_unref (resp->book);
	g_free (resp);
}

static void
_get_book_view_handler (EBookMsg *msg)
{
	GetBookViewMsg *view_msg = (GetBookViewMsg *)msg;
	GetBookViewResponse *response;

	response = g_new (GetBookViewResponse, 1);
	e_book_msg_init ((EBookMsg*)response, _get_book_view_response_handler, _get_book_view_response_dtor);

	response->status = e_book_get_book_view (view_msg->book, view_msg->query, NULL, -1, &response->book_view);
	response->book = view_msg->book;
	response->cb = view_msg->cb;
	response->closure = view_msg->closure;

	g_async_queue_push (from_worker_queue, response);
}

static void
_get_book_view_dtor (EBookMsg *msg)
{
	GetBookViewMsg *view_msg = (GetBookViewMsg *)msg;
	
	g_free (view_msg->query);
	g_free (view_msg);
}

guint
e_book_async_get_book_view (EBook                 *book,
			    const gchar           *query,
			    EBookBookViewCallback  cb,
			    gpointer               closure)
{
	GetBookViewMsg *msg;

	init_async ();

	msg = g_new (GetBookViewMsg, 1);
	e_book_msg_init ((EBookMsg*)msg, _get_book_view_handler, _get_book_view_dtor);

	msg->book = g_object_ref (book);
	msg->query = g_strdup (query);
	msg->cb = cb;
	msg->closure = closure;

	g_async_queue_push (to_worker_queue, msg);

	return 0;
}
