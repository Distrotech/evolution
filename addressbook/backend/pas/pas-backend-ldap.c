/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Author:
 *   Chris Toshok (toshok@ximian.com)
 *
 * Copyright 2000, Ximian, Inc.
 */

#define DEBUG

#include "config.h"  
#include <fcntl.h>
#include <time.h>
#include <lber.h>
#include <gtk/gtksignal.h>

#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-i18n.h>

#ifdef DEBUG
#define LDAP_DEBUG
#define LDAP_DEBUG_ADD
#endif
#include <ldap.h>
#ifdef DEBUG
#undef LDAP_DEBUG
#endif

#if LDAP_VENDOR_VERSION > 20000
#define OPENLDAP2
#else
#define OPENLDAP1
#endif

#ifdef OPENLDAP2
#include "ldap_schema.h"
#endif

#include <e-util/e-sexp.h>
#include <ebook/e-card-simple.h>

#include "pas-backend-ldap.h"
#include "pas-backend-card-sexp.h"
#include "pas-book.h"
#include "pas-card-cursor.h"


#define LDAP_MAX_SEARCH_RESPONSES 100

/* the objectClasses we need */
#define TOP                  "top"
#define PERSON               "person"
#define ORGANIZATIONALPERSON "organizationalPerson"
#define INETORGPERSON        "inetOrgPerson"
#define EVOLUTIONPERSON      "evolutionPerson"

static gchar *query_prop_to_ldap(gchar *query_prop);

static PASBackendClass *pas_backend_ldap_parent_class;
typedef struct _PASBackendLDAPCursorPrivate PASBackendLDAPCursorPrivate;
typedef struct _PASBackendLDAPBookView PASBackendLDAPBookView;
typedef struct LDAPOp LDAPOp;

struct _PASBackendLDAPPrivate {
	char     *uri;
	gboolean connected;
	GList    *clients;
	gchar    *ldap_host;
	gchar    *ldap_rootdn;
	int      ldap_port;
	int      ldap_scope;
	GList    *book_views;

	LDAP     *ldap;

	EList    *supported_fields;

	/* whether or not there's support for the objectclass we need
           to store all our additional fields */
	gboolean evolutionPersonSupported;
	gboolean evolutionPersonChecked;

	gboolean writable;

	/* whether or not there's a request in process on our LDAP* */
	LDAPOp *current_op;
	GList *pending_ops;
	int op_idle;
};

struct _PASBackendLDAPCursorPrivate {
	PASBackend *backend;
	PASBook    *book;

	GList      *elements;
	long       num_elements;
};

struct _PASBackendLDAPBookView {
	PASBookView           *book_view;
	PASBackendLDAPPrivate *blpriv;
	gchar                 *search;
	PASBackendCardSExp    *card_sexp;
	int                   search_idle;
	int                   search_msgid;
	LDAPOp                *search_op;
};

typedef gboolean (*LDAPOpHandler)(PASBackend *backend, LDAPOp *op);
typedef void (*LDAPOpDtor)(PASBackend *backend, LDAPOp *op);

struct LDAPOp {
	LDAPOpHandler handler;
	LDAPOpDtor    dtor;
	PASBackend    *backend;
	PASBook       *book;
	PASBookView   *view;
};

static void     ldap_op_init (LDAPOp *op, PASBackend *backend, PASBook *book, PASBookView *view, LDAPOpHandler handler, LDAPOpDtor dtor);
static void     ldap_op_process_current (PASBackend *backend);
static void     ldap_op_process (LDAPOp *op);
static void     ldap_op_restart (LDAPOp *op);
static gboolean ldap_op_process_on_idle (PASBackend *backend);
static void     ldap_op_finished (LDAPOp *op);

static ECardSimple *build_card_from_entry (LDAP *ldap, LDAPMessage *e);

static void email_populate (ECardSimple *card, char **values);
struct berval** email_ber (ECardSimple *card);
gboolean email_compare (ECardSimple *ecard1, ECardSimple *ecard2);

static void homephone_populate (ECardSimple *card, char **values);
struct berval** homephone_ber (ECardSimple *card);
gboolean homephone_compare (ECardSimple *ecard1, ECardSimple *ecard2);

static void business_populate (ECardSimple *card, char **values);
struct berval** business_ber (ECardSimple *card);
gboolean business_compare (ECardSimple *ecard1, ECardSimple *ecard2);

struct prop_info {
	ECardSimpleField field_id;
	char *query_prop;
	char *ldap_attr;
#define PROP_TYPE_STRING   0x01
#define PROP_TYPE_LIST     0x02
#define PROP_DN            0x04
#define PROP_EVOLVE        0x08
	int prop_type;

	/* the remaining items are only used for the TYPE_LIST props */

	/* used when reading from the ldap server populates ECard with the values in **values. */
	void (*populate_ecard_func)(ECardSimple *card, char **values);
	/* used when writing to an ldap server.  returns a NULL terminated array of berval*'s */
	struct berval** (*ber_func)(ECardSimple *card);
	/* used to compare list attributes */
	gboolean (*compare_func)(ECardSimple *card1, ECardSimple *card2);

} prop_info[] = {

#define LIST_PROP(fid,q,a,ctor,ber,cmp) {fid, q, a, PROP_TYPE_LIST, ctor, ber, cmp}
#define E_LIST_PROP(fid,q,a,ctor,ber,cmp) {fid, q, a, PROP_TYPE_LIST | PROP_EVOLVE, ctor, ber, cmp}
#define STRING_PROP(fid,q,a) {fid, q, a, PROP_TYPE_STRING}
#define E_STRING_PROP(fid,q,a) {fid, q, a, PROP_TYPE_STRING | PROP_EVOLVE}


	/* name fields */
	STRING_PROP (E_CARD_SIMPLE_FIELD_FULL_NAME,   "full_name", "cn" ),
	STRING_PROP (E_CARD_SIMPLE_FIELD_FAMILY_NAME, "family_name", "sn" ),

	/* email addresses */
	LIST_PROP   (E_CARD_SIMPLE_FIELD_EMAIL, "email", "mail", email_populate, email_ber, email_compare),

	/* phone numbers */
	E_STRING_PROP (E_CARD_SIMPLE_FIELD_PHONE_PRIMARY,      "primary_phone", "primaryPhone"),
	LIST_PROP     (E_CARD_SIMPLE_FIELD_PHONE_BUSINESS,     "business_phone", "telephoneNumber", business_populate, business_ber, business_compare),
	LIST_PROP     (E_CARD_SIMPLE_FIELD_PHONE_HOME,         "home_phone", "homePhone", homephone_populate, homephone_ber, homephone_compare),
	STRING_PROP   (E_CARD_SIMPLE_FIELD_PHONE_MOBILE,       "mobile_phone", "mobile"),
	E_STRING_PROP (E_CARD_SIMPLE_FIELD_PHONE_CAR,          "car_phone", "carPhone"),
	STRING_PROP   (E_CARD_SIMPLE_FIELD_PHONE_BUSINESS_FAX, "business_fax", "facsimileTelephoneNumber"), 
	E_STRING_PROP (E_CARD_SIMPLE_FIELD_PHONE_HOME_FAX,     "home_fax", "homeFacsimileTelephoneNumber"), 
	E_STRING_PROP (E_CARD_SIMPLE_FIELD_PHONE_OTHER,        "other_phone", "otherPhone"), 
	E_STRING_PROP (E_CARD_SIMPLE_FIELD_PHONE_OTHER,        "other_fax", "otherFacsimileTelephoneNumber"), 
	STRING_PROP   (E_CARD_SIMPLE_FIELD_PHONE_ISDN,         "isdn", "internationaliSDNNumber"), 
	STRING_PROP   (E_CARD_SIMPLE_FIELD_PHONE_PAGER,        "pager", "pager"),
	E_STRING_PROP (E_CARD_SIMPLE_FIELD_PHONE_RADIO,        "radio", "radio"),
	E_STRING_PROP (E_CARD_SIMPLE_FIELD_PHONE_TELEX,        "telex", "telex"),
	E_STRING_PROP (E_CARD_SIMPLE_FIELD_PHONE_ASSISTANT,    "assistant_phone", "assistantPhone"),
	E_STRING_PROP (E_CARD_SIMPLE_FIELD_PHONE_COMPANY,      "company_phone", "companyPhone"),
	E_STRING_PROP (E_CARD_SIMPLE_FIELD_PHONE_CALLBACK,     "callback_phone", "callbackPhone"),
	E_STRING_PROP (E_CARD_SIMPLE_FIELD_PHONE_TTYTTD,       "tty", "tty"),

	/* org information */
	STRING_PROP   (E_CARD_SIMPLE_FIELD_ORG,       "org",       "o"),
	STRING_PROP   (E_CARD_SIMPLE_FIELD_ORG_UNIT,  "org_unit",  "ou"),
	STRING_PROP   (E_CARD_SIMPLE_FIELD_OFFICE,    "office",    "roomNumber"),
	STRING_PROP   (E_CARD_SIMPLE_FIELD_TITLE,     "title",     "title"),
	E_STRING_PROP (E_CARD_SIMPLE_FIELD_ROLE,      "role",      "businessRole"), 
	E_STRING_PROP (E_CARD_SIMPLE_FIELD_MANAGER,   "manager",   "managerName"), 
	E_STRING_PROP (E_CARD_SIMPLE_FIELD_ASSISTANT, "assistant", "assistantName"), 

	/* addresses */
	STRING_PROP   (E_CARD_SIMPLE_FIELD_ADDRESS_BUSINESS, "business_address", "postalAddress"),
	STRING_PROP   (E_CARD_SIMPLE_FIELD_ADDRESS_HOME,     "home_address",     "homePostalAddress"),
	E_STRING_PROP (E_CARD_SIMPLE_FIELD_ADDRESS_OTHER,    "other_address",    "otherPostalAddress"),

	/* misc fields */
	STRING_PROP (E_CARD_SIMPLE_FIELD_URL,           "url", "labeledURI"),
	/* map nickname to displayName */
	STRING_PROP   (E_CARD_SIMPLE_FIELD_NICKNAME,    "nickname",  "displayName"),
	E_STRING_PROP (E_CARD_SIMPLE_FIELD_SPOUSE,      "spouse", "spouseName"), 
	E_STRING_PROP (E_CARD_SIMPLE_FIELD_NOTE,        "note", "note"), 
	E_STRING_PROP (E_CARD_SIMPLE_FIELD_ANNIVERSARY, "anniversary", "anniversary"), 
	E_STRING_PROP (E_CARD_SIMPLE_FIELD_BIRTH_DATE,  "birth_date", "birthDate"), 
	E_STRING_PROP (E_CARD_SIMPLE_FIELD_MAILER,      "mailer", "mailer"), 

	E_STRING_PROP (E_CARD_SIMPLE_FIELD_FILE_AS,     "file_as", "fileAs"),
	E_STRING_PROP (E_CARD_SIMPLE_FIELD_CATEGORIES,  "categories", "categories"),

/*      E_CARD_SIMPLE_FIELD_FBURL, */
/*  	E_CARD_SIMPLE_FIELD_NAME_OR_ORG, */


#undef E_STRING_PROP
#undef STRING_PROP
#undef E_LIST_PROP
#undef LIST_PROP
};

static int num_prop_infos = sizeof(prop_info) / sizeof(prop_info[0]);

static void
view_destroy(GtkObject *object, gpointer data)
{
	CORBA_Environment ev;
	GNOME_Evolution_Addressbook_Book    corba_book;
	PASBook           *book = (PASBook *)data;
	PASBackendLDAP    *bl;
	GList             *list;

	bl = PAS_BACKEND_LDAP(pas_book_get_backend(book));
	for (list = bl->priv->book_views; list; list = g_list_next(list)) {
		PASBackendLDAPBookView *view = list->data;
		if (view->book_view == PAS_BOOK_VIEW(object)) {
			if (view->search_idle != 0) {
				/* we have a search running on the
				   ldap connection.  remove the idle
				   handler and anbandon the msg id */
				g_source_remove(view->search_idle);
				if (view->search_msgid != -1)
					ldap_abandon (bl->priv->ldap, view->search_msgid);

				/* if the search op is the current op,
				   finish it. else, remove it from the
				   list and nuke it ourselves. */
				if (view->search_op == bl->priv->current_op)
					ldap_op_finished (view->search_op);
				else {
					bl->priv->pending_ops = g_list_remove (bl->priv->pending_ops,
									       view->search_op);
					view->search_op->dtor (view->search_op->backend,
							       view->search_op);
				}
			}
			g_free (view->search);
			gtk_object_unref (GTK_OBJECT (view->card_sexp));
			g_free (view);
			bl->priv->book_views = g_list_remove_link(bl->priv->book_views, list);
			g_list_free_1(list);
			break;
		}
	}

	corba_book = bonobo_object_corba_objref(BONOBO_OBJECT(book));

	CORBA_exception_init(&ev);

	GNOME_Evolution_Addressbook_Book_unref(corba_book, &ev);
	
	if (ev._major != CORBA_NO_EXCEPTION) {
		g_warning("view_destroy: Exception unreffing "
			  "corba book.\n");
	}

	CORBA_exception_free(&ev);
}

static void
add_to_supported_fields (PASBackendLDAP *bl, char **attrs, GHashTable *attr_hash)
{
	int i;
	for (i = 0; attrs[i]; i ++) {
		char *query_prop = g_hash_table_lookup (attr_hash, attrs[i]);

		if (query_prop) {
			e_list_append (bl->priv->supported_fields, query_prop);

			/* handle the list attributes here */
			if (!strcmp (query_prop, "email")) {
				e_list_append (bl->priv->supported_fields, "email_2");
				e_list_append (bl->priv->supported_fields, "email_3");
			}
			else if (!strcmp (query_prop, "business_phone")) {
				e_list_append (bl->priv->supported_fields, "business_phone_2");
			}
			else if (!strcmp (query_prop, "home_phone")) {
				e_list_append (bl->priv->supported_fields, "home_phone_2");
			}
		}
	}
}

static void
add_oc_attributes_to_supported_fields (PASBackendLDAP *bl, LDAPObjectClass *oc)
{
	int i;
	GHashTable *attr_hash = g_hash_table_new (g_str_hash, g_str_equal);

	for (i = 0; i < num_prop_infos; i ++)
		g_hash_table_insert (attr_hash, prop_info[i].ldap_attr, prop_info[i].query_prop);

	if (oc->oc_at_oids_must)
		add_to_supported_fields (bl, oc->oc_at_oids_must, attr_hash);

	if (oc->oc_at_oids_may)
		add_to_supported_fields (bl, oc->oc_at_oids_may, attr_hash);

	g_hash_table_destroy (attr_hash);
}

static void
check_schema_support (PASBackendLDAP *bl)
{
	char *attrs[2];
	LDAPMessage *resp;
	LDAP *ldap = bl->priv->ldap;

	bl->priv->evolutionPersonChecked = TRUE;

	attrs[0] = "objectClasses";
	attrs[1] = NULL;

	if (ldap_search_ext_s (ldap, "cn=Subschema", LDAP_SCOPE_BASE,
			       "(objectClass=subschema)", attrs, 0,
			       NULL, NULL, NULL, 0, &resp) == LDAP_SUCCESS) {
		char **values;

		values = ldap_get_values (ldap, resp, "objectClasses");

		if (values) {
			int i;
			for (i = 0; values[i]; i ++) {
				int j;
				int code;
				const char *err;
				LDAPObjectClass *oc = ldap_str2objectclass (values[i], &code, &err, 0);

				if (!oc)
					continue;

				for (j = 0; oc->oc_names[j]; j++)
					if (!g_strcasecmp (oc->oc_names[j], EVOLUTIONPERSON)) {
						g_print ("support found on ldap server for objectclass evolutionPerson\n");
						bl->priv->evolutionPersonSupported = TRUE;

						add_oc_attributes_to_supported_fields (bl, oc);
					}
					else if (!g_strcasecmp (oc->oc_names[j], INETORGPERSON)
						 || !g_strcasecmp (oc->oc_names[j], ORGANIZATIONALPERSON)
						 || !g_strcasecmp (oc->oc_names[j], PERSON)) {
						add_oc_attributes_to_supported_fields (bl, oc);
					}

				ldap_objectclass_free (oc);
			}
		}
	}
}

static void
pas_backend_ldap_connect (PASBackendLDAP *bl)
{
	PASBackendLDAPPrivate *blpriv = bl->priv;

	/* close connection first if it's open first */
	if (blpriv->ldap)
		ldap_unbind (blpriv->ldap);

	blpriv->ldap = ldap_init (blpriv->ldap_host, blpriv->ldap_port);
#ifdef DEBUG
	{
		int debug_level = ~0;
		ldap_set_option (blpriv->ldap, LDAP_OPT_DEBUG_LEVEL, &debug_level);
	}
#endif

	if (NULL != blpriv->ldap) {
		ldap_simple_bind_s(blpriv->ldap,
				   NULL /*binddn*/, NULL /*passwd*/);
		blpriv->connected = TRUE;

		/* check to see if evolutionPerson is supported, if we can (me
		   might not be able to if we can't authenticate.  if we
		   can't, try again in auth_user.) */
		check_schema_support (bl);
	}
	else {
		g_warning ("pas_backend_ldap_connect failed for "
			   "'ldap://%s:%d/%s'\n",
			   blpriv->ldap_host,
			   blpriv->ldap_port,
			   blpriv->ldap_rootdn ? blpriv->ldap_rootdn : "");
		blpriv->connected = FALSE;
	}

}

static ECardSimple *
search_for_dn (PASBackendLDAP *bl, const char *dn)
{
	LDAP *ldap = bl->priv->ldap;
	LDAPMessage    *res, *e;
	ECardSimple *result = NULL;

	if (ldap_search_s (ldap,
			   dn,
			   LDAP_SCOPE_BASE,
			   "(objectclass=*)",
			   NULL, 0, &res) != -1) {
		e = ldap_first_entry (ldap, res);
		while (NULL != e) {
			if (!strcmp (ldap_get_dn (ldap, e), dn)) {
				printf ("found it\n");
				result = build_card_from_entry (ldap, e);
				break;
			}
			e = ldap_next_entry (ldap, e);
		}

		ldap_msgfree(res);
	}

	return result;
}

static void
ldap_op_init (LDAPOp *op, PASBackend *backend,
	      PASBook *book, PASBookView *view,
	      LDAPOpHandler handler, LDAPOpDtor dtor)
{
	op->backend = backend;
	op->book = book;
	op->view = view;
	op->handler = handler;
	op->dtor = dtor;
}

static void
ldap_op_process_current (PASBackend *backend)
{
	PASBackendLDAP *bl = PAS_BACKEND_LDAP (backend);
	LDAPOp *op = bl->priv->current_op;

	if (!bl->priv->connected) {
		if (op->view)
			pas_book_view_notify_status_message (op->view, _("Connecting to LDAP server..."));
		pas_backend_ldap_connect(bl);
	}

	if (bl->priv->connected) {
		if (op->handler (backend, op))
			ldap_op_finished (op);
	}
	else {
		if (op->view) {
			pas_book_view_notify_status_message (op->view, _("Unable to connect to LDAP server."));
			pas_book_view_notify_complete (op->view);
		}

		ldap_op_finished (op);
	}
}

static void
ldap_op_process (LDAPOp *op)
{
	PASBackendLDAP *bl = PAS_BACKEND_LDAP (op->backend);

	if (bl->priv->current_op) {
		/* operation in progress.  queue this op for later and return. */
		if (op->view)
			pas_book_view_notify_status_message (op->view, _("Waiting for connection to LDAP server..."));
		bl->priv->pending_ops = g_list_append (bl->priv->pending_ops, op);
	}
	else {
		/* nothing going on, do this op now */
		bl->priv->current_op = op;
		ldap_op_process_current (op->backend);
	}
}

static gboolean
ldap_op_process_on_idle (PASBackend *backend)
{
	PASBackendLDAP *bl = PAS_BACKEND_LDAP (backend);

	bl->priv->op_idle = 0;

	ldap_op_process_current (backend);

	return FALSE;
}

static void
ldap_op_restart (LDAPOp *op)
{
	PASBackend *backend = op->backend;
	PASBackendLDAP *bl = PAS_BACKEND_LDAP (backend);

	g_return_if_fail (op == bl->priv->current_op);

	bl->priv->op_idle = g_idle_add((GSourceFunc)ldap_op_process_on_idle, backend);
}

static void
ldap_op_finished (LDAPOp *op)
{
	PASBackend *backend = op->backend;
	PASBackendLDAP *bl = PAS_BACKEND_LDAP (backend);

	g_return_if_fail (op == bl->priv->current_op);

	op->dtor (backend, op);

	if (bl->priv->pending_ops) {
		bl->priv->current_op = bl->priv->pending_ops->data;
		bl->priv->pending_ops = g_list_remove_link (bl->priv->pending_ops, bl->priv->pending_ops);

		bl->priv->op_idle = g_idle_add((GSourceFunc)ldap_op_process_on_idle, backend);
	}
	else {
		bl->priv->current_op = NULL;
	}
}

static int
ldap_error_to_response (int ldap_error)
{
	if (ldap_error == LDAP_SUCCESS)
		return GNOME_Evolution_Addressbook_BookListener_Success;
	else if (LDAP_NAME_ERROR (ldap_error))
		return GNOME_Evolution_Addressbook_BookListener_CardNotFound;
	else if (ldap_error == LDAP_INSUFFICIENT_ACCESS)
		return GNOME_Evolution_Addressbook_BookListener_PermissionDenied;
	else if (ldap_error == LDAP_SERVER_DOWN)
		return GNOME_Evolution_Addressbook_BookListener_RepositoryOffline;
	else if (ldap_error == LDAP_ALREADY_EXISTS)
		return GNOME_Evolution_Addressbook_BookListener_CardIdAlreadyExists;
	else
		return GNOME_Evolution_Addressbook_BookListener_OtherError;
}


static char *
create_dn_from_ecard (ECardSimple *card, const char *root_dn)
{
	char *cn, *cn_part = NULL;
	char *dn;
	gboolean need_comma = FALSE;

	cn = e_card_simple_get (card, E_CARD_SIMPLE_FIELD_FULL_NAME);
	if (cn) {
		cn_part = g_strdup_printf ("cn=%s%s", cn, need_comma ? "," : "");
	}
	else {
		cn_part = g_strdup ("");
	}

	dn = g_strdup_printf ("%s%s%s", cn_part,
			      (root_dn && strlen(root_dn)) ? "," : "",
			      (root_dn && strlen(root_dn)) ? root_dn: "");

	g_free (cn_part);

	g_print ("generated dn: %s\n", dn);

	return dn;
}

static void
free_mods (GPtrArray *mods)
{
	int i = 0;
	LDAPMod *mod;

	while ((mod = g_ptr_array_index (mods, i++))) {
		int j;
		g_free (mod->mod_type);

		if (mod->mod_op & LDAP_MOD_BVALUES) {
			for (j = 0; mod->mod_bvalues[j]; j++) {
				g_free (mod->mod_bvalues[j]->bv_val);
				g_free (mod->mod_bvalues[j]);
			}
		}
		else {
			for (j = 0; mod->mod_values[j]; j++)
				g_free (mod->mod_values[j]);
		}
		g_free (mod);
	}

	g_ptr_array_free (mods, TRUE /* XXX ? */);
}

static GPtrArray*
build_mods_from_ecards (PASBackendLDAP *bl, ECardSimple *current, ECardSimple *new, gboolean *new_dn_needed)
{
	gboolean adding = (current == NULL);
	GPtrArray *result = g_ptr_array_new();
	int i;

	if (new_dn_needed)
		*new_dn_needed = FALSE;

	/* we walk down the list of properties we can deal with (that
	 big table at the top of the file) */

	for (i = 0; i < num_prop_infos; i ++) {
		gboolean include;
		gboolean new_prop_present = FALSE;
		gboolean current_prop_present = FALSE;
		struct berval** new_prop_bers = NULL;
		char *new_prop = NULL;
		char *current_prop = NULL;

		/* XXX if it's an evolutionPerson prop and the ldap
                   server doesn't support that objectclass, skip it. */
		if (prop_info[i].prop_type & PROP_EVOLVE && !bl->priv->evolutionPersonSupported)
			continue;

		/* get the value for the new card, and compare it to
                   the value in the current card to see if we should
                   update it -- if adding is TRUE, short circuit the
                   check. */
		if (prop_info[i].prop_type & PROP_TYPE_STRING) {
			new_prop = e_card_simple_get (new, prop_info[i].field_id);
			new_prop_present = (new_prop != NULL);
		}
		else {
			new_prop_bers = prop_info[i].ber_func (new);
			new_prop_present = (new_prop_bers != NULL);
		}

		/* need to set INCLUDE to true if the field needs to
                   show up in the ldap modify request */
		if (adding) {
			/* if we're creating a new card, include it if the
                           field is there at all */
			if (prop_info[i].prop_type & PROP_TYPE_STRING)
				include = (new_prop_present && *new_prop); /* empty strings cause problems */
			else
				include = new_prop_present;
		}
		else {
			/* if we're modifying an existing card,
                           include it if the current field value is
                           different than the new one, if it didn't
                           exist previously, or if it's been
                           removed. */
			if (prop_info[i].prop_type & PROP_TYPE_STRING) {
				current_prop = e_card_simple_get (current, prop_info[i].field_id);
				current_prop_present = (current_prop != NULL);

				if (new_prop && current_prop)
					include = *new_prop && strcmp (new_prop, current_prop);
				else
					include = (!!new_prop != !!current_prop);
			}
			else {
				int j;
				struct berval **current_prop_bers = prop_info[i].ber_func (current);

				current_prop_present = (current_prop_bers != NULL);

				/* free up the current_prop_bers */
				if (current_prop_bers) {
					for (j = 0; current_prop_bers[j]; j++) {
						g_free (current_prop_bers[j]->bv_val);
						g_free (current_prop_bers[j]);
					}
					g_free (current_prop_bers);
				}

				include = !prop_info[i].compare_func (new, current);
			}
		}

		if (include) {
			LDAPMod *mod = g_new (LDAPMod, 1);

			/* the included attribute has changed - we
                           need to update the dn if it's one of the
                           attributes we compute the dn from. */
			if (new_dn_needed)
				*new_dn_needed |= prop_info[i].prop_type & PROP_DN;

			if (adding) {
				mod->mod_op = LDAP_MOD_ADD;
			}
			else {
				if (!new_prop_present)
					mod->mod_op = LDAP_MOD_DELETE;
				else if (!current_prop_present)
					mod->mod_op = LDAP_MOD_ADD;
				else
					mod->mod_op = LDAP_MOD_REPLACE;
			}
			
			mod->mod_type = g_strdup (prop_info[i].ldap_attr);

			if (prop_info[i].prop_type & PROP_TYPE_STRING) {
				mod->mod_values = g_new (char*, 2);
				mod->mod_values[0] = new_prop;
				mod->mod_values[1] = NULL;
			}
			else { /* PROP_TYPE_LIST */
				mod->mod_op |= LDAP_MOD_BVALUES;
				mod->mod_bvalues = new_prop_bers;
			}

			g_ptr_array_add (result, mod);
		}
		
	}

	/* NULL terminate the list of modifications */
	g_ptr_array_add (result, NULL);

	return result;
}

static void
add_objectclass_mod (PASBackendLDAP *bl, GPtrArray *mod_array)
{
	LDAPMod *objectclass_mod;

	objectclass_mod = g_new (LDAPMod, 1);
	objectclass_mod->mod_op = LDAP_MOD_ADD;
	objectclass_mod->mod_type = g_strdup ("objectClass");
	objectclass_mod->mod_values = g_new (char*, bl->priv->evolutionPersonSupported ? 6 : 5);
	objectclass_mod->mod_values[0] = g_strdup (TOP);
	objectclass_mod->mod_values[1] = g_strdup (PERSON);
	objectclass_mod->mod_values[2] = g_strdup (ORGANIZATIONALPERSON);
	objectclass_mod->mod_values[3] = g_strdup (INETORGPERSON);
	if (bl->priv->evolutionPersonSupported) {
		objectclass_mod->mod_values[4] = g_strdup (EVOLUTIONPERSON);
		objectclass_mod->mod_values[5] = NULL;
	}
	else {
		objectclass_mod->mod_values[4] = NULL;
	}
	g_ptr_array_add (mod_array, objectclass_mod);
}

typedef struct {
	LDAPOp op;
	char *vcard;
} LDAPCreateOp;

static gboolean
create_card_handler (PASBackend *backend, LDAPOp *op)
{
	LDAPCreateOp *create_op = (LDAPCreateOp*)op;
	PASBackendLDAP *bl = PAS_BACKEND_LDAP (backend);
	ECard *new_ecard;
	ECardSimple *new_card;
	char *dn;
	int response;
	int            ldap_error;
	GPtrArray *mod_array;
	LDAPMod **ldap_mods;
	LDAP *ldap;

	printf ("vcard = %s\n", create_op->vcard);

	new_ecard = e_card_new (create_op->vcard);
	new_card = e_card_simple_new (new_ecard);

	dn = create_dn_from_ecard (new_card, bl->priv->ldap_rootdn);
	e_card_simple_set_id (new_card, dn); /* for the notification code below */

	ldap = bl->priv->ldap;

	/* build our mods */
	mod_array = build_mods_from_ecards (bl, NULL, new_card, NULL);

#if 0
	if (!mod_array) {
		/* there's an illegal field in there.  report
                   UnsupportedAttribute back */
		g_free (dn);

		gtk_object_unref (GTK_OBJECT(new_card));

		pas_book_respond_create (create_op->op.book,
					 GNOME_Evolution_Addressbook_BookListener_UnsupportedField,
					 dn);

		return TRUE;
	}
#endif

	/* remove the NULL at the end */
	g_ptr_array_remove (mod_array, NULL);

	/* add our objectclass(es) */
	add_objectclass_mod (bl, mod_array);

	/* then put the NULL back */
	g_ptr_array_add (mod_array, NULL);

#ifdef LDAP_DEBUG_ADD
	{
		int i;
		printf ("Sending the following to the server as ADD\n");

		for (i = 0; g_ptr_array_index(mod_array, i); i ++) {
			LDAPMod *mod = g_ptr_array_index(mod_array, i);
			if (mod->mod_op & LDAP_MOD_DELETE)
				printf ("del ");
			else if (mod->mod_op & LDAP_MOD_REPLACE)
				printf ("rep ");
			else
				printf ("add ");
			if (mod->mod_op & LDAP_MOD_BVALUES)
				printf ("ber ");
			else
				printf ("    ");

			printf (" %s:\n", mod->mod_type);

			if (mod->mod_op & LDAP_MOD_BVALUES) {
				int j;
				for (j = 0; mod->mod_bvalues[j] && mod->mod_bvalues[j]->bv_val; j++)
					printf ("\t\t'%s'\n", mod->mod_bvalues[j]->bv_val);
			}
			else {
				int j;

				for (j = 0; mod->mod_values[j]; j++)
					printf ("\t\t'%s'\n", mod->mod_values[j]);
			}
		}
	}
#endif

	ldap_mods = (LDAPMod**)mod_array->pdata;

	if (op->view)
		pas_book_view_notify_status_message (op->view, _("Adding card to LDAP server..."));

	/* actually perform the ldap add */
	ldap_error = ldap_add_s (ldap, dn, ldap_mods);

	if (ldap_error == LDAP_SUCCESS) {
		/* the card was created, let's let the views know about it */
		GList *l;
		for (l = bl->priv->book_views; l; l = l->next) {
			CORBA_Environment ev;
			gboolean match;
			PASBackendLDAPBookView *view = l->data; 
					
			CORBA_exception_init(&ev);

			bonobo_object_dup_ref(bonobo_object_corba_objref(BONOBO_OBJECT(view->book_view)), &ev);

			match = pas_backend_card_sexp_match_vcard (view->card_sexp,
								   e_card_simple_get_vcard_assume_utf8 (new_card));
			if (match)
				pas_book_view_notify_add_1 (view->book_view, e_card_simple_get_vcard_assume_utf8 (new_card));
			pas_book_view_notify_complete (view->book_view);

			bonobo_object_release_unref(bonobo_object_corba_objref(BONOBO_OBJECT(view->book_view)), &ev);
		}
	}
	else {
		ldap_perror (ldap, "ldap_add_s");
	}

	if (op->view)
		pas_book_view_notify_complete (op->view);

	/* and clean up */
	free_mods (mod_array);
	g_free (dn);

	gtk_object_unref (GTK_OBJECT(new_card));

	/* and lastly respond */
	response = ldap_error_to_response (ldap_error);
	pas_book_respond_create (create_op->op.book,
				 response,
				 dn);

	/* we're synchronous */
	return TRUE;
}

static void
create_card_dtor (PASBackend *backend, LDAPOp *op)
{
	LDAPCreateOp *create_op = (LDAPCreateOp*)op;

	g_free (create_op->vcard);
	g_free (create_op);
}

static void
pas_backend_ldap_process_create_card (PASBackend *backend,
				      PASBook    *book,
				      PASRequest *req)
{
	LDAPCreateOp *create_op = g_new (LDAPCreateOp, 1);
	PASBackendLDAP *bl = PAS_BACKEND_LDAP (backend);
	PASBookView *book_view = NULL;

	if (bl->priv->book_views) {
		PASBackendLDAPBookView *v = bl->priv->book_views->data;
		book_view = v->book_view;
	}

	ldap_op_init ((LDAPOp*)create_op, backend, book,
		      book_view,
		      create_card_handler, create_card_dtor);

	create_op->vcard = req->vcard;

	ldap_op_process ((LDAPOp*)create_op);
}


typedef struct {
	LDAPOp op;
	char *id;
} LDAPRemoveOp;

static gboolean
remove_card_handler (PASBackend *backend, LDAPOp *op)
{
	LDAPRemoveOp *remove_op = (LDAPRemoveOp*)op;
	PASBackendLDAP *bl = PAS_BACKEND_LDAP (backend);
	int response;
	int ldap_error;
	ECardSimple *simple;

	if (op->view)
		pas_book_view_notify_status_message (op->view, _("Removing card from LDAP server..."));

	simple = search_for_dn (bl, remove_op->id);

	if (simple) {
		ldap_error = ldap_delete_s (bl->priv->ldap, remove_op->id);

		if (ldap_error == LDAP_SUCCESS) {
			/* the card was removed, let's let the views know about it */
			GList *l;
			for (l = bl->priv->book_views; l; l = l->next) {
				CORBA_Environment ev;
				gboolean match;
				PASBackendLDAPBookView *view = l->data; 
					
				CORBA_exception_init(&ev);

				bonobo_object_dup_ref(bonobo_object_corba_objref(BONOBO_OBJECT(view->book_view)), &ev);

				match = pas_backend_card_sexp_match_vcard (view->card_sexp,
									   e_card_simple_get_vcard_assume_utf8 (simple));
				if (match)
					pas_book_view_notify_remove (view->book_view, remove_op->id);
				pas_book_view_notify_complete (view->book_view);

				bonobo_object_release_unref(bonobo_object_corba_objref(BONOBO_OBJECT(view->book_view)), &ev);
			}
		}
		else {
			ldap_perror (bl->priv->ldap, "ldap_delete_s");
		}

		response = ldap_error_to_response (ldap_error);
	}
	else
		response = GNOME_Evolution_Addressbook_BookListener_CardNotFound;

	pas_book_respond_remove (remove_op->op.book,
				 response);

	if (op->view)
		pas_book_view_notify_complete (op->view);

	/* we're synchronous */
	return TRUE;
}

static void
remove_card_dtor (PASBackend *backend, LDAPOp *op)
{
	LDAPRemoveOp *remove_op = (LDAPRemoveOp*)op;

	g_free (remove_op->id);
	g_free (remove_op);
}

static void
pas_backend_ldap_process_remove_card (PASBackend *backend,
				      PASBook    *book,
				      PASRequest *req)
{
	LDAPRemoveOp *remove_op = g_new (LDAPRemoveOp, 1);
	PASBackendLDAP *bl = PAS_BACKEND_LDAP (backend);
	PASBookView *book_view = NULL;

	if (bl->priv->book_views) {
		PASBackendLDAPBookView *v = bl->priv->book_views->data;
		book_view = v->book_view;
	}

	ldap_op_init ((LDAPOp*)remove_op, backend, book,
		      book_view,
		      remove_card_handler, remove_card_dtor);

	remove_op->id = req->id;

	ldap_op_process ((LDAPOp*)remove_op);
}


typedef struct {
	LDAPOp op;
	char *vcard;
} LDAPModifyOp;

static gboolean
modify_card_handler (PASBackend *backend, LDAPOp *op)
{
	LDAPModifyOp *modify_op = (LDAPModifyOp*)op;
	PASBackendLDAP *bl = PAS_BACKEND_LDAP (backend);
	ECard *new_ecard;
	const char *id;
	int response;
	int            ldap_error = LDAP_SUCCESS;
	GPtrArray *mod_array;
	LDAPMod **ldap_mods;
	LDAP *ldap;
	ECardSimple *current_card;

	new_ecard = e_card_new (modify_op->vcard);
	id = e_card_get_id(new_ecard);

	ldap = bl->priv->ldap;

	if (op->view)
		pas_book_view_notify_status_message (op->view, _("Modifying card from LDAP server..."));

	current_card = search_for_dn (bl, id);

	if (current_card) {
		ECardSimple *new_card = e_card_simple_new (new_ecard);
		gboolean need_new_dn;

		/* build our mods */
		mod_array = build_mods_from_ecards (bl, current_card, new_card, &need_new_dn);
		if (mod_array->len > 0) {
			ldap_mods = (LDAPMod**)mod_array->pdata;

			/* actually perform the ldap modify */
			ldap_error = ldap_modify_ext_s (ldap, id, ldap_mods, NULL, NULL);
			if (ldap_error == LDAP_SUCCESS) {

				/* the card was modified, let's let the views know about it */
				GList *l;
				for (l = bl->priv->book_views; l; l = l->next) {
					CORBA_Environment ev;
					gboolean old_match, new_match;
					PASBackendLDAPBookView *view = l->data; 
					
					CORBA_exception_init(&ev);

					bonobo_object_dup_ref(bonobo_object_corba_objref(BONOBO_OBJECT(view->book_view)), &ev);

					old_match = pas_backend_card_sexp_match_vcard (view->card_sexp,
										       e_card_simple_get_vcard_assume_utf8 (current_card));
					new_match = pas_backend_card_sexp_match_vcard (view->card_sexp,
										       modify_op->vcard);
					if (old_match && new_match)
						pas_book_view_notify_change_1 (view->book_view, modify_op->vcard);
					else if (new_match)
						pas_book_view_notify_add_1 (view->book_view, modify_op->vcard);
					else /* if (old_match) */
						pas_book_view_notify_remove (view->book_view, e_card_simple_get_id (new_card));
					pas_book_view_notify_complete (view->book_view);

					bonobo_object_release_unref(bonobo_object_corba_objref(BONOBO_OBJECT(view->book_view)), &ev);
				}
			}
			else {
				ldap_perror (ldap, "ldap_modify_s");
			}
		}
		else {
			g_print ("modify list empty.  no modification sent\n");
		}

		/* and clean up */
		free_mods (mod_array);
		gtk_object_unref (GTK_OBJECT(new_card));
		gtk_object_unref (GTK_OBJECT(current_card));
	}
	else {
		g_print ("didn't find original card\n");
	}

	response = ldap_error_to_response (ldap_error);
	pas_book_respond_modify (modify_op->op.book,
				 response);

	if (op->view)
		pas_book_view_notify_complete (op->view);

	/* we're synchronous */
	return TRUE;
}

static void
modify_card_dtor (PASBackend *backend, LDAPOp *op)
{
	LDAPModifyOp *modify_op = (LDAPModifyOp*)op;

	g_free (modify_op->vcard);
	g_free (modify_op);
}

static void
pas_backend_ldap_process_modify_card (PASBackend *backend,
				      PASBook    *book,
				      PASRequest *req)
{
	LDAPModifyOp *modify_op = g_new (LDAPModifyOp, 1);
	PASBackendLDAP *bl = PAS_BACKEND_LDAP (backend);
	PASBookView *book_view = NULL;

	if (bl->priv->book_views) {
		PASBackendLDAPBookView *v = bl->priv->book_views->data;
		book_view = v->book_view;
	}

	ldap_op_init ((LDAPOp*)modify_op, backend, book,
		      book_view,
		      modify_card_handler, modify_card_dtor);

	modify_op->vcard = req->vcard;

	ldap_op_process ((LDAPOp*)modify_op);
}


static void
pas_backend_ldap_process_get_vcard (PASBackend *backend,
				    PASBook *book,
				    PASRequest *req)
{
	PASBackendLDAP *bl;
	ECardSimple *simple;

	bl = PAS_BACKEND_LDAP (pas_book_get_backend (book));

	simple = search_for_dn (bl, req->id);

	if (simple) {
		pas_book_respond_get_vcard (book,
					    GNOME_Evolution_Addressbook_BookListener_Success,
					    e_card_simple_get_vcard_assume_utf8 (simple));
		gtk_object_unref (GTK_OBJECT (simple));
	}
	else {
		pas_book_respond_get_vcard (book,
					    GNOME_Evolution_Addressbook_BookListener_CardNotFound,
					    "");
	}
}


typedef struct {
	LDAPOp op;
	PASBook *book;
} LDAPGetCursorOp;

static long
get_length(PASCardCursor *cursor, gpointer data)
{
	PASBackendLDAPCursorPrivate *cursor_data = (PASBackendLDAPCursorPrivate *) data;

	return cursor_data->num_elements;
}

static char *
get_nth(PASCardCursor *cursor, long n, gpointer data)
{
	PASBackendLDAPCursorPrivate *cursor_data = (PASBackendLDAPCursorPrivate *) data;

	g_return_val_if_fail (n < cursor_data->num_elements, NULL);

	return (char*)g_list_nth (cursor_data->elements, n);
}

static void
cursor_destroy(GtkObject *object, gpointer data)
{
	CORBA_Environment ev;
	GNOME_Evolution_Addressbook_Book corba_book;
	PASBackendLDAPCursorPrivate *cursor_data = (PASBackendLDAPCursorPrivate *) data;

	corba_book = bonobo_object_corba_objref(BONOBO_OBJECT(cursor_data->book));

	CORBA_exception_init(&ev);

	GNOME_Evolution_Addressbook_Book_unref(corba_book, &ev);
	
	if (ev._major != CORBA_NO_EXCEPTION) {
		g_warning("cursor_destroy: Exception unreffing "
			  "corba book.\n");
	}

	CORBA_exception_free(&ev);

	/* free the ldap specific cursor information */
	g_list_foreach (cursor_data->elements, (GFunc)g_free, NULL);
	g_list_free (cursor_data->elements);

	g_free(cursor_data);
}

static void
pas_backend_ldap_build_all_cards_list(PASBackend *backend,
				      PASBackendLDAPCursorPrivate *cursor_data)
{
	PASBackendLDAP *bl = PAS_BACKEND_LDAP (backend);
	LDAP           *ldap = bl->priv->ldap;
	int            ldap_error;
	LDAPMessage    *res, *e;

	if ((ldap_error = ldap_search_s (ldap,
					 bl->priv->ldap_rootdn,
					 bl->priv->ldap_scope,
					 "(objectclass=*)",
					 NULL, 0, &res)) == -1) {
		g_warning ("ldap error '%s' in "
			   "pas_backend_ldap_build_all_cards_list\n",
			   ldap_err2string(ldap_error));
	}

	cursor_data->elements = NULL;

	cursor_data->num_elements = ldap_count_entries (ldap, res);

	e = ldap_first_entry(ldap, res);

	while (NULL != e) {

		/* for now just make a list of the dn's */
#if 0
		for ( a = ldap_first_attribute( ldap, e, &ber ); a != NULL;
		      a = ldap_next_attribute( ldap, e, ber ) ) {
		}
#else
		cursor_data->elements = g_list_prepend(cursor_data->elements,
						       g_strdup(ldap_get_dn(ldap, e)));
#endif

		e = ldap_next_entry(ldap, e);
	}

	ldap_msgfree(res);
}


static gboolean
get_cursor_handler (PASBackend *backend, LDAPOp *op)
{
	LDAPGetCursorOp *cursor_op = (LDAPGetCursorOp*)op;
	CORBA_Environment ev;
	int            ldap_error = 0;
	PASCardCursor *cursor;
	GNOME_Evolution_Addressbook_Book corba_book;
	PASBackendLDAPCursorPrivate *cursor_data;
	PASBook *book = cursor_op->book;

	cursor_data = g_new(PASBackendLDAPCursorPrivate, 1);
	cursor_data->backend = backend;
	cursor_data->book = book;

	pas_backend_ldap_build_all_cards_list(backend, cursor_data);

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
	
	pas_book_respond_get_cursor (book,
				     ldap_error_to_response (ldap_error),
				     cursor);

	/* we're synchronous */
	return TRUE;
}

static void
get_cursor_dtor (PASBackend *backend, LDAPOp *op)
{
	g_free (op);
}

static void
pas_backend_ldap_process_get_cursor (PASBackend *backend,
				     PASBook    *book,
				     PASRequest *req)
{
	LDAPGetCursorOp *op = g_new (LDAPGetCursorOp, 1);

	ldap_op_init ((LDAPOp*)op, backend, book, NULL, get_cursor_handler, get_cursor_dtor);

	ldap_op_process ((LDAPOp*)op);
}


/* List property functions */
static void
email_populate(ECardSimple *card, char **values)
{
	int i;

	for (i = 0; values[i] && i < 3; i ++) {
		e_card_simple_set_email (card, i, values[i]);
	}
}

struct berval**
email_ber(ECardSimple *card)
{
	struct berval** result;
	const char *emails[3];
	int i, j, num;

	num = 0;
	for (i = 0; i < 3; i ++) {
		emails[i] = e_card_simple_get_email (card, E_CARD_SIMPLE_EMAIL_ID_EMAIL + i);
		if (emails[i])
			num++;
	}

	if (num == 0)
		return NULL;

	result = g_new (struct berval*, num + 1);

	for (i = 0; i < num; i ++)
		result[i] = g_new (struct berval, 1);

	j = 0;
	for (i = 0; i < 3; i ++) {
		if (emails[i]) {
			result[j]->bv_val = g_strdup (emails[i]);
			result[j++]->bv_len = strlen (emails[i]);
		}
	}

	result[num] = NULL;

	return result;
}

gboolean
email_compare (ECardSimple *ecard1, ECardSimple *ecard2)
{
	const char *email1, *email2;
	int i;

	for (i = 0; i < 3; i ++) {
		gboolean equal;
		email1 = e_card_simple_get_email (ecard1, E_CARD_SIMPLE_EMAIL_ID_EMAIL + i);
		email2 = e_card_simple_get_email (ecard2, E_CARD_SIMPLE_EMAIL_ID_EMAIL + i);

		if (email1 && email2)
			equal = !strcmp (email1, email2);
		else
			equal = (!!email1 == !!email2);

		if (!equal)
			return equal;
	}

	return TRUE;
}

static void
homephone_populate(ECardSimple *card, char **values)
{
	if (values[0])
		e_card_simple_set (card, E_CARD_SIMPLE_FIELD_PHONE_HOME, values[0]);
	if (values[1])
		e_card_simple_set (card, E_CARD_SIMPLE_FIELD_PHONE_HOME_2, values[1]);
}

struct berval**
homephone_ber(ECardSimple *card)
{
	struct berval** result;
	const char *homephones[3];
	int i, j, num;

	num = 0;
	if ((homephones[0] = e_card_simple_get (card, E_CARD_SIMPLE_FIELD_PHONE_HOME)))
		num++;
	if ((homephones[1] = e_card_simple_get (card, E_CARD_SIMPLE_FIELD_PHONE_HOME_2)))
		num++;

	if (num == 0)
		return NULL;

	result = g_new (struct berval*, num + 1);

	for (i = 0; i < num; i ++)
		result[i] = g_new (struct berval, 1);

	j = 0;
	for (i = 0; i < 2; i ++) {
		if (homephones[i]) {
			result[j]->bv_val = g_strdup (homephones[i]);
			result[j++]->bv_len = strlen (homephones[i]);
		}
	}

	result[num] = NULL;

	return result;
}

gboolean
homephone_compare (ECardSimple *ecard1, ECardSimple *ecard2)
{
	int phone_ids[2] = { E_CARD_SIMPLE_FIELD_PHONE_HOME, E_CARD_SIMPLE_FIELD_PHONE_HOME_2 };
	const char *phone1, *phone2;
	int i;

	for (i = 0; i < 2; i ++) {
		gboolean equal;
		phone1 = e_card_simple_get (ecard1, phone_ids[i]);
		phone2 = e_card_simple_get (ecard2, phone_ids[i]);

		if (phone1 && phone2)
			equal = !strcmp (phone1, phone2);
		else
			equal = (!!phone1 == !!phone2);

		if (!equal)
			return equal;
	}

	return TRUE;
}

static void
business_populate(ECardSimple *card, char **values)
{
	if (values[0])
		e_card_simple_set (card, E_CARD_SIMPLE_FIELD_PHONE_BUSINESS, values[0]);
	if (values[1])
		e_card_simple_set (card, E_CARD_SIMPLE_FIELD_PHONE_BUSINESS_2, values[1]);
}

struct berval**
business_ber(ECardSimple *card)
{
	struct berval** result;
	const char *business_phones[3];
	int i, j, num;

	num = 0;
	if ((business_phones[0] = e_card_simple_get (card, E_CARD_SIMPLE_FIELD_PHONE_BUSINESS)))
		num++;
	if ((business_phones[1] = e_card_simple_get (card, E_CARD_SIMPLE_FIELD_PHONE_BUSINESS_2)))
		num++;

	if (num == 0)
		return NULL;

	result = g_new (struct berval*, num + 1);

	for (i = 0; i < num; i ++)
		result[i] = g_new (struct berval, 1);

	j = 0;
	for (i = 0; i < 2; i ++) {
		if (business_phones[i]) {
			result[j]->bv_val = g_strdup (business_phones[i]);
			result[j++]->bv_len = strlen (business_phones[i]);
		}
	}

	result[num] = NULL;

	return result;
}

gboolean
business_compare (ECardSimple *ecard1, ECardSimple *ecard2)
{
	int phone_ids[2] = { E_CARD_SIMPLE_FIELD_PHONE_BUSINESS, E_CARD_SIMPLE_FIELD_PHONE_BUSINESS_2 };
	const char *phone1, *phone2;
	int i;

	for (i = 0; i < 2; i ++) {
		gboolean equal;
		phone1 = e_card_simple_get (ecard1, phone_ids[i]);
		phone2 = e_card_simple_get (ecard2, phone_ids[i]);

		if (phone1 && phone2)
			equal = !strcmp (phone1, phone2);
		else
			equal = (!!phone1 == !!phone2);

		if (!equal)
			return equal;
	}

	return TRUE;
}

#define IS_RFC2254_CHAR(c) ((c) == '*' || (c) =='\\' || (c) == '(' || (c) == ')' || (c) == '\0')
static char hex[] = "0123456789abcdef";
static char *
rfc2254_escape(char *str)
{
	int i;
	int len = strlen(str);
	int newlen = 0;

	for (i = 0; i < len; i ++) {
		if (IS_RFC2254_CHAR(str[i]))
			newlen += 3;
		else
			newlen ++;
	}

	if (len == newlen) {
		return g_strdup (str);
	}
	else {
		char *newstr = g_malloc0 (newlen + 1);
		int j = 0;
		for (i = 0; i < len; i ++) {
			if (IS_RFC2254_CHAR(str[i])) {
				newstr[j++] = '\\';
				newstr[j++] = hex[(str[i]&0xf0) >> 4];
				newstr[j++] = hex[str[i]&0x0f];
			}
			else {
				newstr[j++] = str[i];
			}
		}
		return newstr;
	}
		
}

static ESExpResult *
func_and(struct _ESExp *f, int argc, struct _ESExpResult **argv, void *data)
{
	GList **list = data;
	ESExpResult *r;
	char ** strings;

	if (argc > 0) {
		int i;

		strings = g_new(char*, argc+3);
		strings[0] = g_strdup ("(&");
		strings[argc+3 - 2] = g_strdup (")");
		strings[argc+3 - 1] = NULL;
		
		for (i = 0; i < argc; i ++) {
			GList *list_head = *list;
			strings[argc - i] = (*list)->data;
			*list = g_list_remove_link(*list, *list);
			g_list_free_1(list_head);
		}

		*list = g_list_prepend(*list, g_strjoinv(" ", strings));

		for (i = 0 ; i < argc + 2; i ++)
			g_free (strings[i]);

		g_free (strings);
	}

	r = e_sexp_result_new(f, ESEXP_RES_BOOL);
	r->value.bool = FALSE;

	return r;
}

static ESExpResult *
func_or(struct _ESExp *f, int argc, struct _ESExpResult **argv, void *data)
{
	GList **list = data;
	ESExpResult *r;
	char ** strings;

	if (argc > 0) {
		int i;

		strings = g_new(char*, argc+3);
		strings[0] = g_strdup ("(|");
		strings[argc+3 - 2] = g_strdup (")");
		strings[argc+3 - 1] = NULL;
		for (i = 0; i < argc; i ++) {
			GList *list_head = *list;
			strings[argc - i] = (*list)->data;
			*list = g_list_remove_link(*list, *list);
			g_list_free_1(list_head);
		}

		*list = g_list_prepend(*list, g_strjoinv(" ", strings));

		for (i = 0 ; i < argc + 2; i ++)
			g_free (strings[i]);

		g_free (strings);
	}

	r = e_sexp_result_new(f, ESEXP_RES_BOOL);
	r->value.bool = FALSE;

	return r;
}

static ESExpResult *
func_not(struct _ESExp *f, int argc, struct _ESExpResult **argv, void *data)
{
	GList **list = data;
	ESExpResult *r;

	/* just replace the head of the list with the NOT of it. */
	if (argc > 0) {
		char *term = (*list)->data;
		(*list)->data = g_strdup_printf("(!%s)", term);
		g_free (term);
	}

	r = e_sexp_result_new(f, ESEXP_RES_BOOL);
	r->value.bool = FALSE;

	return r;
}

static ESExpResult *
func_contains(struct _ESExp *f, int argc, struct _ESExpResult **argv, void *data)
{
	GList **list = data;
	ESExpResult *r;

	if (argc == 2
	    && argv[0]->type == ESEXP_RES_STRING
	    && argv[1]->type == ESEXP_RES_STRING) {
		char *propname = argv[0]->value.string;
		char *str = rfc2254_escape(argv[1]->value.string);
		gboolean one_star = FALSE;

		if (strlen(str) == 0)
			one_star = TRUE;

		if (!strcmp (propname, "x-evolution-any-field")) {
			int i;
			int query_length;
			char *big_query;
			char *match_str;

			match_str = g_strdup_printf("=*%s%s)",
						    str, one_star ? "" : "*");

			query_length = 3; /* strlen ("(|") + strlen (")") */

			for (i = 0; i < num_prop_infos; i ++) {
				query_length += 1 /* strlen ("(") */ + strlen(prop_info[i].ldap_attr) + strlen (match_str);
			}

			big_query = g_malloc0(query_length + 1);
			strcat (big_query, "(|");
			for (i = 0; i < num_prop_infos; i ++) {
				strcat (big_query, "(");
				strcat (big_query, prop_info[i].ldap_attr);
				strcat (big_query, match_str);
			}
			strcat (big_query, ")");

			*list = g_list_prepend(*list, big_query);

			g_free (match_str);
		}
		else {
			char *ldap_attr = query_prop_to_ldap(propname);

			if (ldap_attr)
				*list = g_list_prepend(*list,
						       g_strdup_printf("(%s=*%s%s)",
								       ldap_attr,
								       str,
								       one_star ? "" : "*"));
		}

		g_free (str);
	}

	r = e_sexp_result_new(f, ESEXP_RES_BOOL);
	r->value.bool = FALSE;

	return r;
}

static ESExpResult *
func_is(struct _ESExp *f, int argc, struct _ESExpResult **argv, void *data)
{
	GList **list = data;
	ESExpResult *r;

	if (argc == 2
	    && argv[0]->type == ESEXP_RES_STRING
	    && argv[1]->type == ESEXP_RES_STRING) {
		char *propname = argv[0]->value.string;
		char *str = rfc2254_escape(argv[1]->value.string);
		char *ldap_attr = query_prop_to_ldap(propname);

		if (ldap_attr)
			*list = g_list_prepend(*list,
					       g_strdup_printf("(%s=%s)",
							       ldap_attr, str));
		else {
			g_warning ("unknown query property\n");
			/* we want something that'll always be false */
			*list = g_list_prepend(*list,
					       g_strdup("objectClass=MyBarnIsBiggerThanYourBarn"));
		}

		g_free (str);
	}

	r = e_sexp_result_new(f, ESEXP_RES_BOOL);
	r->value.bool = FALSE;

	return r;
}

static ESExpResult *
func_beginswith(struct _ESExp *f, int argc, struct _ESExpResult **argv, void *data)
{
	GList **list = data;
	ESExpResult *r;

	if (argc == 2
	    && argv[0]->type == ESEXP_RES_STRING
	    && argv[1]->type == ESEXP_RES_STRING) {
		char *propname = argv[0]->value.string;
		char *str = rfc2254_escape(argv[1]->value.string);
		char *ldap_attr = query_prop_to_ldap(propname);
		gboolean one_star = FALSE;

		if (strlen(str) == 0)
			one_star = TRUE;

		if (ldap_attr)
			*list = g_list_prepend(*list,
					       g_strdup_printf("(%s=%s*)",
							       ldap_attr,
							       str));
		g_free (str);
	}

	r = e_sexp_result_new(f, ESEXP_RES_BOOL);
	r->value.bool = FALSE;

	return r;
}

static ESExpResult *
func_endswith(struct _ESExp *f, int argc, struct _ESExpResult **argv, void *data)
{
	GList **list = data;
	ESExpResult *r;

	if (argc == 2
	    && argv[0]->type == ESEXP_RES_STRING
	    && argv[1]->type == ESEXP_RES_STRING) {
		char *propname = argv[0]->value.string;
		char *str = rfc2254_escape(argv[1]->value.string);
		char *ldap_attr = query_prop_to_ldap(propname);
		gboolean one_star = FALSE;

		if (strlen(str) == 0)
			one_star = TRUE;

		if (ldap_attr)
			*list = g_list_prepend(*list,
					       g_strdup_printf("(%s=*%s)",
							       ldap_attr,
							       str));
		g_free (str);
	}

	r = e_sexp_result_new(f, ESEXP_RES_BOOL);
	r->value.bool = FALSE;

	return r;
}

/* 'builtin' functions */
static struct {
	char *name;
	ESExpFunc *func;
	int type;		/* set to 1 if a function can perform shortcut evaluation, or
				   doesn't execute everything, 0 otherwise */
} symbols[] = {
	{ "and", func_and, 0 },
	{ "or", func_or, 0 },
	{ "not", func_not, 0 },
	{ "contains", func_contains, 0 },
	{ "is", func_is, 0 },
	{ "beginswith", func_beginswith, 0 },
	{ "endswith", func_endswith, 0 },
};

static gchar *
pas_backend_ldap_build_query (gchar *query)
{
	ESExp *sexp;
	ESExpResult *r;
	gchar *retval;
	GList *list = NULL;
	int i;

	sexp = e_sexp_new();

	for(i=0;i<sizeof(symbols)/sizeof(symbols[0]);i++) {
		if (symbols[i].type == 1) {
			e_sexp_add_ifunction(sexp, 0, symbols[i].name,
					     (ESExpIFunc *)symbols[i].func, &list);
		} else {
			e_sexp_add_function(sexp, 0, symbols[i].name,
					    symbols[i].func, &list);
		}
	}

	e_sexp_input_text(sexp, query, strlen(query));
	e_sexp_parse(sexp);

	r = e_sexp_eval(sexp);

	e_sexp_result_free(sexp, r);
	e_sexp_unref (sexp);

	if (list) {
		if (list->next) {
			g_warning ("conversion to ldap query string failed");
			retval = NULL;
			g_list_foreach (list, (GFunc)g_free, NULL);
		}
		else {
			retval = list->data;
		}
	}
	else {
		g_warning ("conversion to ldap query string failed");
		retval = NULL;
	}

	g_list_free (list);
	return retval;
}

static gchar *
query_prop_to_ldap(gchar *query_prop)
{
	int i;

	for (i = 0; i < num_prop_infos; i ++)
		if (!strcmp (query_prop, prop_info[i].query_prop))
			return prop_info[i].ldap_attr;

	return NULL;
}


typedef struct {
	LDAPOp op;
	char *ldap_query;
	PASBackendLDAP *bl;
	PASBackendLDAPBookView *view;
} LDAPSearchOp;

static ECardSimple *
build_card_from_entry (LDAP *ldap, LDAPMessage *e)
{
	ECard *ecard = E_CARD(gtk_type_new(e_card_get_type()));
	ECardSimple *card = e_card_simple_new (ecard);
	char *dn = ldap_get_dn(ldap, e);
	char *attr;
	BerElement *ber = NULL;

	g_print ("build_card_from_entry, dn = %s\n", dn);
	e_card_simple_set_id (card, dn);

	for (attr = ldap_first_attribute (ldap, e, &ber); attr;
	     attr = ldap_next_attribute (ldap, e, ber)) {
		int i;
		struct prop_info *info = NULL;

		for (i = 0; i < num_prop_infos; i ++)
			if (!g_strcasecmp (attr, prop_info[i].ldap_attr))
				info = &prop_info[i];

		if (info) {
			char **values;
			values = ldap_get_values (ldap, e, attr);

			if (values) {
				if (info->prop_type & PROP_TYPE_STRING) {
				/* if it's a normal property just set the string */
					e_card_simple_set (card, info->field_id, values[0]);

				}
				else if (info->prop_type & PROP_TYPE_LIST) {
				/* if it's a list call the ecard-populate function,
				   which calls gtk_object_set to set the property */
					info->populate_ecard_func(card,
								  values);
				}

				ldap_value_free (values);
			}
		}
	}

#ifndef OPENLDAP2
	/* if ldap->ld_errno == LDAP_DECODING_ERROR there was an
	   error decoding an attribute, and we shouldn't free ber,
	   since the ldap library already did it. */
	if (ldap->ld_errno != LDAP_DECODING_ERROR && ber)
		ber_free (ber, 0);
#endif

	e_card_simple_sync_card (card);

	return card;
}

static gboolean
poll_ldap (LDAPSearchOp *op)
{
	PASBackendLDAPBookView *view = op->view;
	PASBackendLDAP *bl = op->bl;
	LDAP           *ldap = bl->priv->ldap;
	int            rc;
	LDAPMessage    *res, *e;
	GList   *cards = NULL;
	static int received = 0;

	pas_book_view_notify_status_message (view->book_view, _("Receiving LDAP search results..."));

	rc = ldap_result (ldap, view->search_msgid, 0, NULL, &res);
	
	if (rc == -1 && received == 0) {
		pas_book_view_notify_status_message (view->book_view, _("Restarting search."));
		/* connection went down and we never got any. */
		bl->priv->connected = FALSE;

		/* this will reopen the connection */
		ldap_op_restart ((LDAPOp*)op);
		return FALSE;
	}

	if (rc != LDAP_RES_SEARCH_ENTRY) {
		view->search_idle = 0;
		pas_book_view_notify_complete (view->book_view);
		ldap_op_finished ((LDAPOp*)op);
		received = 0;
		return FALSE;
	}

	received = 1;
		
	e = ldap_first_entry(ldap, res);

	while (NULL != e) {
		ECardSimple *card = build_card_from_entry (ldap, e);

		cards = g_list_append (cards, e_card_simple_get_vcard_assume_utf8 (card));

		gtk_object_unref (GTK_OBJECT(card));

		e = ldap_next_entry(ldap, e);
	}

	if (cards) {
		pas_book_view_notify_add (view->book_view, cards);
			
		g_list_foreach (cards, (GFunc)g_free, NULL);
		g_list_free (cards);
		cards = NULL;
	}

	ldap_msgfree(res);

	return TRUE;
}

static gboolean
ldap_search_handler (PASBackend *backend, LDAPOp *op)
{
	LDAPSearchOp *search_op = (LDAPSearchOp*) op;

	if (op->view)
		pas_book_view_notify_status_message (op->view, _("Searching..."));

	/* it might not be NULL if we've been restarted */
	if (search_op->ldap_query == NULL)
		search_op->ldap_query = pas_backend_ldap_build_query(search_op->view->search);

	if (search_op->ldap_query != NULL) {
		PASBackendLDAP *bl = PAS_BACKEND_LDAP (backend);
		PASBackendLDAPBookView *view = search_op->view;
		LDAP *ldap = bl->priv->ldap;
		int ldap_err;

		ldap_err = ldap_search_ext (ldap, bl->priv->ldap_rootdn,
					    bl->priv->ldap_scope,
					    search_op->ldap_query,
					    NULL, 0,
					    NULL, /* XXX */
					    NULL, /* XXX */
					    NULL,
					    LDAP_MAX_SEARCH_RESPONSES, &view->search_msgid);

		if (ldap_err != LDAP_SUCCESS) {
			pas_book_view_notify_status_message (view->book_view, ldap_err2string(ldap_err));
			return TRUE; /* act synchronous in this case */
		}

		if (view->search_msgid == -1) {
			pas_book_view_notify_status_message (view->book_view, ldap_err2string(ldap_err));
			return TRUE; /* act synchronous in this case */
		}
		else {
			view->search_idle = g_idle_add((GSourceFunc)poll_ldap, search_op);
		}

		/* we're async */
		return FALSE;
	}
	else {
		/* error doing the conversion to an ldap query, let's
                   end this now by acting like we're synchronous. */
		g_warning ("LDAP problem converting search query %s\n", search_op->view->search);
		return TRUE;
	}
}

static void
ldap_search_dtor (PASBackend *backend, LDAPOp *op)
{
	LDAPSearchOp *search_op = (LDAPSearchOp*) op;

	g_free (search_op->ldap_query);
	g_free (search_op);
}

static void
pas_backend_ldap_search (PASBackendLDAP  	*bl,
			 PASBook         	*book,
			 PASBackendLDAPBookView *view)
{
	LDAPSearchOp *op = g_new (LDAPSearchOp, 1);

	ldap_op_init ((LDAPOp*)op, PAS_BACKEND(bl), book, view->book_view, ldap_search_handler, ldap_search_dtor);

	op->ldap_query = NULL;
	op->view = view;
	op->bl = bl;

	/* keep track of the search op so we can delete it from the
           list if the view is destroyed */
	view->search_op = (LDAPOp*)op;

	ldap_op_process ((LDAPOp*)op);
}

static void
pas_backend_ldap_process_get_book_view (PASBackend *backend,
					PASBook    *book,
					PASRequest *req)
{
	PASBackendLDAP *bl = PAS_BACKEND_LDAP (backend);
	CORBA_Environment ev;
	PASBookView       *book_view;
	PASBackendLDAPBookView *view;

	g_return_if_fail (req->listener != NULL);

	book_view = pas_book_view_new (req->listener);

	bonobo_object_ref(BONOBO_OBJECT(book));
	gtk_signal_connect(GTK_OBJECT(book_view), "destroy",
			   GTK_SIGNAL_FUNC(view_destroy), book);

	view = g_new0(PASBackendLDAPBookView, 1);
	view->book_view = book_view;
	view->search = g_strdup(req->search);
	view->card_sexp = pas_backend_card_sexp_new (view->search);
	view->blpriv = bl->priv;

	bl->priv->book_views = g_list_prepend(bl->priv->book_views, view);

	pas_book_respond_get_book_view (book,
		(book_view != NULL
		 ? GNOME_Evolution_Addressbook_BookListener_Success 
		 : GNOME_Evolution_Addressbook_BookListener_CardNotFound /* XXX */),
		book_view);

	pas_backend_ldap_search (bl, book, view);

	g_free (req->search);
	CORBA_exception_init(&ev);
	bonobo_object_release_unref(req->listener, &ev);
	if (ev._major != CORBA_NO_EXCEPTION) {
		g_warning("pas_backend_file_process_get_book_view: Exception reffing "
			  "corba book.\n");
	}
	CORBA_exception_free(&ev);

}

static void
pas_backend_ldap_process_check_connection (PASBackend *backend,
					   PASBook    *book,
					   PASRequest *req)
{
	PASBackendLDAP *bl = PAS_BACKEND_LDAP (backend);

	pas_book_report_connection (book, bl->priv->connected);
}

static void
pas_backend_ldap_process_authenticate_user (PASBackend *backend,
					    PASBook    *book,
					    PASRequest *req)
{
	PASBackendLDAP *bl = PAS_BACKEND_LDAP (backend);
	int ldap_error;
	char *query;
	LDAPMessage    *res, *e;

	query = g_strdup_printf ("(mail=%s)", req->user);

	if (ldap_search_s (bl->priv->ldap,
			   bl->priv->ldap_rootdn,
			   bl->priv->ldap_scope,
			   query,
			   NULL, 0, &res) != -1) {
		char *dn;

		e = ldap_first_entry (bl->priv->ldap, res);
		dn = ldap_get_dn (bl->priv->ldap, e);

		printf ("authenticating as %s\n", dn);

		ldap_error = ldap_simple_bind_s(bl->priv->ldap,
						dn,
						req->passwd);

		pas_book_respond_authenticate_user (book,
						    ldap_error_to_response (ldap_error));

		bl->priv->writable = (ldap_error == LDAP_SUCCESS);

		if (!bl->priv->evolutionPersonChecked)
			check_schema_support (bl);

		ldap_msgfree (res);
	}
	else {
		pas_book_respond_authenticate_user (book, GNOME_Evolution_Addressbook_BookListener_PermissionDenied);
	}

	pas_book_report_writable (book, bl->priv->writable);

	g_free (query);
}

static void
pas_backend_ldap_process_get_supported_fields (PASBackend *backend,
					       PASBook    *book,
					       PASRequest *req)

{
	PASBackendLDAP *bl = PAS_BACKEND_LDAP (backend);

	pas_book_respond_get_supported_fields (book,
					       GNOME_Evolution_Addressbook_BookListener_Success,
					       bl->priv->supported_fields);
}

static void
pas_backend_ldap_process_client_requests (PASBook *book)
{
	PASBackend *backend;
	PASRequest *req;

	backend = pas_book_get_backend (book);

	req = pas_book_pop_request (book);
	if (req == NULL)
		return;

	switch (req->op) {
	case CreateCard:
		pas_backend_ldap_process_create_card (backend, book, req);
		break;

	case RemoveCard:
		pas_backend_ldap_process_remove_card (backend, book, req);
		break;

	case ModifyCard:
		pas_backend_ldap_process_modify_card (backend, book, req);
		break;

	case CheckConnection:
		pas_backend_ldap_process_check_connection (backend, book, req);
		break;

	case GetVCard:
		pas_backend_ldap_process_get_vcard (backend, book, req);
		break;

	case GetCursor:
		pas_backend_ldap_process_get_cursor (backend, book, req);
		break;

	case GetBookView:
		pas_backend_ldap_process_get_book_view (backend, book, req);
		break;

	case GetChanges:
		/* FIXME: Code this. */
		break;

	case AuthenticateUser:
		pas_backend_ldap_process_authenticate_user (backend, book, req);
		break;

	case GetSupportedFields:
		pas_backend_ldap_process_get_supported_fields (backend, book, req);
		break;
	}

	g_free (req);
}

static void
pas_backend_ldap_book_destroy_cb (PASBook *book, gpointer data)
{
	PASBackendLDAP *backend;

	backend = PAS_BACKEND_LDAP (data);

	pas_backend_remove_client (PAS_BACKEND (backend), book);
}

static gboolean
pas_backend_ldap_load_uri (PASBackend             *backend,
			   const char             *uri)
{
	PASBackendLDAP *bl = PAS_BACKEND_LDAP (backend);
	LDAPURLDesc    *lud;
	int ldap_error;

	g_assert (bl->priv->connected == FALSE);

	ldap_error = ldap_url_parse ((char*)uri, &lud);
	if (ldap_error == LDAP_SUCCESS) {
		g_free(bl->priv->uri);
		bl->priv->uri = g_strdup (uri);
		bl->priv->ldap_host = g_strdup(lud->lud_host);
		bl->priv->ldap_port = lud->lud_port;
		/* if a port wasn't specified, default to LDAP_PORT */
		if (bl->priv->ldap_port == 0)
			bl->priv->ldap_port = LDAP_PORT;
		bl->priv->ldap_rootdn = g_strdup(lud->lud_dn);
		bl->priv->ldap_scope = lud->lud_scope;

		ldap_free_urldesc(lud);

		pas_backend_ldap_connect (bl);
		if (bl->priv->ldap == NULL)
			return FALSE;
		else
			return TRUE;
	} else
		return FALSE;
}

/* Get_uri handler for the addressbook LDAP backend */
static const char *
pas_backend_ldap_get_uri (PASBackend *backend)
{
	PASBackendLDAP *bl;

	bl = PAS_BACKEND_LDAP (backend);
	return bl->priv->uri;
}

static gboolean
pas_backend_ldap_add_client (PASBackend             *backend,
			     GNOME_Evolution_Addressbook_BookListener  listener)
{
	PASBackendLDAP *bl;
	PASBook        *book;

	g_assert (backend != NULL);
	g_assert (PAS_IS_BACKEND_LDAP (backend));

	bl = PAS_BACKEND_LDAP (backend);

	book = pas_book_new (backend, listener);

	if (!book) {
		if (!bl->priv->clients)
			pas_backend_last_client_gone (backend);

		return FALSE;
	}

	gtk_signal_connect (GTK_OBJECT (book), "destroy",
		    pas_backend_ldap_book_destroy_cb, backend);

	gtk_signal_connect (GTK_OBJECT (book), "requests_queued",
		    pas_backend_ldap_process_client_requests, NULL);

	bl->priv->clients = g_list_prepend (
		bl->priv->clients, book);

	if (bl->priv->connected) {
		pas_book_respond_open (
			book, GNOME_Evolution_Addressbook_BookListener_Success);
	} else {
		/* Open the book. */
		pas_book_respond_open (
			book, GNOME_Evolution_Addressbook_BookListener_Success);
	}

	pas_book_report_writable (book, bl->priv->writable);

	return TRUE;
}

static void
pas_backend_ldap_remove_client (PASBackend             *backend,
				PASBook                *book)
{
	PASBackendLDAP *bl;
	GList *l;
	PASBook *lbook;

	g_return_if_fail (backend != NULL);
	g_return_if_fail (PAS_IS_BACKEND_LDAP (backend));
	g_return_if_fail (book != NULL);
	g_return_if_fail (PAS_IS_BOOK (book));

	bl = PAS_BACKEND_LDAP (backend);

	/* Find the book in the list of clients */

	for (l = bl->priv->clients; l; l = l->next) {
		lbook = PAS_BOOK (l->data);

		if (lbook == book)
			break;
	}

	g_assert (l != NULL);

	/* Disconnect */

	bl->priv->clients = g_list_remove_link (bl->priv->clients, l);
	g_list_free_1 (l);

	/* When all clients go away, notify the parent factory about it so that
	 * it may decide whether to kill the backend or not.
	 */
	if (!bl->priv->clients)
		pas_backend_last_client_gone (backend);
}

static char *
pas_backend_ldap_get_static_capabilites (PASBackend *backend)
{
	return g_strdup("net");
}

static gboolean
pas_backend_ldap_construct (PASBackendLDAP *backend)
{
	g_assert (backend != NULL);
	g_assert (PAS_IS_BACKEND_LDAP (backend));

	if (! pas_backend_construct (PAS_BACKEND (backend)))
		return FALSE;

	return TRUE;
}

/**
 * pas_backend_ldap_new:
 */
PASBackend *
pas_backend_ldap_new (void)
{
	PASBackendLDAP *backend;

	backend = gtk_type_new (pas_backend_ldap_get_type ());

	if (! pas_backend_ldap_construct (backend)) {
		gtk_object_unref (GTK_OBJECT (backend));

		return NULL;
	}

	return PAS_BACKEND (backend);
}

static void
call_dtor (LDAPOp *op, gpointer data)
{
	op->dtor (op->backend, op);
}

static void
pas_backend_ldap_destroy (GtkObject *object)
{
	PASBackendLDAP *bl;

	bl = PAS_BACKEND_LDAP (object);

	g_list_foreach (bl->priv->pending_ops, (GFunc)call_dtor, NULL);
	g_list_free (bl->priv->pending_ops);

	if (bl->priv->supported_fields)
		gtk_object_unref (GTK_OBJECT (bl->priv->supported_fields));

	g_free (bl->priv->uri);

	GTK_OBJECT_CLASS (pas_backend_ldap_parent_class)->destroy (object);	
}

static void
pas_backend_ldap_class_init (PASBackendLDAPClass *klass)
{
	GtkObjectClass  *object_class = (GtkObjectClass *) klass;
	PASBackendClass *parent_class;

	pas_backend_ldap_parent_class = gtk_type_class (pas_backend_get_type ());

	parent_class = PAS_BACKEND_CLASS (klass);

	/* Set the virtual methods. */
	parent_class->load_uri                = pas_backend_ldap_load_uri;
	parent_class->get_uri                 = pas_backend_ldap_get_uri;
	parent_class->add_client              = pas_backend_ldap_add_client;
	parent_class->remove_client           = pas_backend_ldap_remove_client;
	parent_class->get_static_capabilities = pas_backend_ldap_get_static_capabilites;

	object_class->destroy = pas_backend_ldap_destroy;
}

static void
pas_backend_ldap_init (PASBackendLDAP *backend)
{
	PASBackendLDAPPrivate *priv;

	priv                   = g_new0 (PASBackendLDAPPrivate, 1);

	priv->supported_fields = e_list_new ((EListCopyFunc)g_strdup, (EListFreeFunc)g_free, NULL);

	backend->priv = priv;
}

/**
 * pas_backend_ldap_get_type:
 */
GtkType
pas_backend_ldap_get_type (void)
{
	static GtkType type = 0;

	if (! type) {
		GtkTypeInfo info = {
			"PASBackendLDAP",
			sizeof (PASBackendLDAP),
			sizeof (PASBackendLDAPClass),
			(GtkClassInitFunc)  pas_backend_ldap_class_init,
			(GtkObjectInitFunc) pas_backend_ldap_init,
			NULL, /* reserved 1 */
			NULL, /* reserved 2 */
			(GtkClassInitFunc) NULL
		};

		type = gtk_type_unique (pas_backend_get_type (), &info);
	}

	return type;
}
