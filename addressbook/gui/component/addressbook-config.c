/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Authors:
 * Chris Toshok <toshok@ximian.com>
 * Chris Lahey <clahey@ximian.com>
 **/

/*#define STANDALONE*/

#include <config.h>

#include <string.h>
#include <stdlib.h>
#include <sys/time.h>

#include <gtk/gtkcombo.h>
#include <gtk/gtkdialog.h>
#include <gtk/gtkentry.h>
#include <gtk/gtkmessagedialog.h>
#include <libgnome/gnome-i18n.h>
#include <libgnomeui/gnome-druid.h>
#include <libgnomeui/gnome-druid-page.h>

#include <bonobo/bonobo-generic-factory.h>

#include <glade/glade.h>

#include "addressbook.h"
#include "addressbook-component.h"
#include "addressbook-config.h"

#include "evolution-config-control.h"

#include <gal/e-table/e-table-memory-store.h>
#include <gal/e-table/e-table-scrolled.h>


#ifdef HAVE_LDAP
#include "ldap.h"
#include "ldap_schema.h"
#endif

#define LDAP_PORT_STRING "389"
#define LDAPS_PORT_STRING "636"

#define GLADE_FILE_NAME "ldap-config.glade"
#define CONFIG_CONTROL_FACTORY_ID "OAFIID:GNOME_Evolution_Addressbook_ConfigControlFactory:" BASE_VERSION
#define LDAP_CONFIG_CONTROL_ID "OAFIID:GNOME_Evolution_LDAPStorage_ConfigControl:" BASE_VERSION

GtkWidget* supported_bases_create_table (char *name, char *string1, char *string2,
					 int num1, int num2);

/* default objectclasses */
#define TOP                  "top"
#define PERSON               "person"
#define ORGANIZATIONALPERSON "organizationalPerson"
#define INETORGPERSON        "inetOrgPerson"
#define EVOLUTIONPERSON      "evolutionPerson"
#define CALENTRY             "calEntry"


typedef struct {
	GtkWidget *notebook;
	int page_num;
} FocusHelpClosure;

static void
focus_help (GtkWidget *w, GdkEventFocus *event, FocusHelpClosure *closure)
{
	gtk_notebook_set_current_page (GTK_NOTEBOOK(closure->notebook), closure->page_num);
}

static void
add_focus_handler (GtkWidget *widget, GtkWidget *notebook, int page_num)
{
	FocusHelpClosure *focus_closure = g_new0 (FocusHelpClosure, 1);
	focus_closure->notebook = notebook;
	focus_closure->page_num = page_num;

	g_signal_connect_data (G_OBJECT (widget),
			       "focus_in_event" /* XXX */,
			       G_CALLBACK (focus_help),
			       focus_closure,
			       (GClosureNotify) g_free,
			       (GConnectFlags)0);
}

typedef struct _AddressbookSourceDialog AddressbookSourceDialog;
typedef void (*ModifyFunc)(GtkWidget *item, AddressbookSourceDialog *dialog);

struct _AddressbookSourceDialog {
	GladeXML  *gui;

	GtkWidget *window;
	GtkWidget *druid; /* only used (obviously) in the druid */

	/* Source selection (druid only) */
	ESourceList *source_list;
	GtkWidget *group_optionmenu;

	/* ESource we're currently editing (editor only) */
	ESource *source;

	/* Source group we're creating/editing a source in */
	ESourceGroup *source_group;

	/* info page fields */
	ModifyFunc general_modify_func;
	GtkWidget *host;
	GtkWidget *auth_optionmenu;
	AddressbookLDAPAuthType auth; 
	GtkWidget *auth_label_notebook;
	GtkWidget *auth_entry_notebook;
	GtkWidget *email;
	GtkWidget *binddn;

	/* connecting page fields */
	ModifyFunc connecting_modify_func;
	GtkWidget *port_combo;
	GtkWidget *ssl_optionmenu;
	AddressbookLDAPSSLType ssl;

	/* searching page fields */
	ModifyFunc searching_modify_func;
	GtkWidget *rootdn;
	AddressbookLDAPScopeType scope;
	GtkWidget *scope_optionmenu;
	GtkWidget *timeout_scale;
	GtkWidget *limit_spinbutton;

	/* display name page fields */
	GtkWidget *display_name;
	gboolean display_name_changed; /* only used in the druid */

	gboolean schema_query_successful;

	/* stuff for the account editor window */
	GtkWidget *ok_button;
	GtkWidget *cancel_button;
	GtkWidget *advanced_button_notebook;
	GtkWidget *notebook; /* the toplevel notebook */

	gboolean advanced;
};



#ifdef HAVE_LDAP

static char *
ldap_unparse_auth (AddressbookLDAPAuthType auth_type)
{
	switch (auth_type) {
	case ADDRESSBOOK_LDAP_AUTH_NONE:
		return "none";
	case ADDRESSBOOK_LDAP_AUTH_SIMPLE_EMAIL:
		return "ldap/simple-email";
	case ADDRESSBOOK_LDAP_AUTH_SIMPLE_BINDDN:
		return "ldap/simple-binddn";
	default:
		g_assert(0);
		return "none";
	}
}

static AddressbookLDAPAuthType
ldap_parse_auth (const char *auth)
{
	if (!auth)
		return ADDRESSBOOK_LDAP_AUTH_NONE;

	if (!strcmp (auth, "ldap/simple-email") || !strcmp (auth, "simple"))
		return ADDRESSBOOK_LDAP_AUTH_SIMPLE_EMAIL;
	else if (!strcmp (auth, "ldap/simple-binddn"))
		return ADDRESSBOOK_LDAP_AUTH_SIMPLE_BINDDN;
	else
		return ADDRESSBOOK_LDAP_AUTH_NONE;
}

static char *
ldap_unparse_scope (AddressbookLDAPScopeType scope_type)
{
	switch (scope_type) {
	case ADDRESSBOOK_LDAP_SCOPE_BASE:
		return "base";
	case ADDRESSBOOK_LDAP_SCOPE_ONELEVEL:
		return "one";
	case ADDRESSBOOK_LDAP_SCOPE_SUBTREE:
		return "sub";
	default:
		g_assert(0);
		return "";
	}
}

static char *
ldap_unparse_ssl (AddressbookLDAPSSLType ssl_type)
{
	switch (ssl_type) {
	case ADDRESSBOOK_LDAP_SSL_NEVER:
		return "never";
	case ADDRESSBOOK_LDAP_SSL_WHENEVER_POSSIBLE:
		return "whenever_possible";
	case ADDRESSBOOK_LDAP_SSL_ALWAYS:
		return "always";
	default:
		g_assert(0);
		return "";
	}
}

static AddressbookLDAPSSLType
ldap_parse_ssl (const char *ssl)
{
	if (!ssl)
		return ADDRESSBOOK_LDAP_SSL_WHENEVER_POSSIBLE; /* XXX good default? */

	if (!strcmp (ssl, "always"))
		return ADDRESSBOOK_LDAP_SSL_ALWAYS;
	else if (!strcmp (ssl, "never"))
		return ADDRESSBOOK_LDAP_SSL_NEVER;
	else
		return ADDRESSBOOK_LDAP_SSL_WHENEVER_POSSIBLE;
}

#endif



static void
dialog_to_source (AddressbookSourceDialog *dialog, ESource *source, gboolean temporary)
{
	gchar *str;

	g_assert (source);

	e_source_set_name (source, gtk_entry_get_text (GTK_ENTRY (dialog->display_name)));

	if (!strcmp ("ldap://", e_source_group_peek_base_uri (dialog->source_group))) {
#ifdef HAVE_LDAP
		e_source_set_property (source, "email_addr", gtk_entry_get_text (GTK_ENTRY (dialog->email)));
		e_source_set_property (source, "binddn", gtk_entry_get_text (GTK_ENTRY (dialog->binddn)));
		e_source_set_property (source, "limit", gtk_entry_get_text (GTK_ENTRY (dialog->limit_spinbutton)));
		e_source_set_property (source, "ssl", ldap_unparse_ssl (dialog->ssl));
		e_source_set_property (source, "auth", ldap_unparse_auth (dialog->auth));

		str = g_strdup_printf ("%s:%s/%s?" /* trigraph prevention */ "?%s",
				       gtk_entry_get_text (GTK_ENTRY (dialog->host)),
				       gtk_entry_get_text (GTK_ENTRY (GTK_COMBO (dialog->port_combo)->entry)),
				       gtk_entry_get_text (GTK_ENTRY (dialog->rootdn)),
				       ldap_unparse_scope (dialog->scope));
		e_source_set_relative_uri (source, str);
		g_free (str);
#endif
	} else {
		const gchar *relative_uri;

		relative_uri = e_source_peek_relative_uri (source);
		if (!relative_uri || !strlen (relative_uri))
			e_source_set_relative_uri (source, e_source_peek_uid (source));
	}

	if (!temporary && !e_source_peek_group (source))
		e_source_group_add_source (dialog->source_group, source, -1);
}

static ESource *
dialog_to_temp_source (AddressbookSourceDialog *dialog)
{
	ESource *source;

	source = e_source_new ("", "");
	dialog_to_source (dialog, source, TRUE);

	return source;
}

#ifdef HAVE_LDAP
static gboolean
source_to_uri_parts (ESource *source, gchar **host, gchar **rootdn,
		     AddressbookLDAPScopeType *scope, gint *port)
{
	gchar       *uri;
	LDAPURLDesc *lud;
	gint         ldap_error;

	g_assert (source);

	uri = e_source_get_uri (source);
	ldap_error = ldap_url_parse ((gchar *) uri, &lud);
	g_free (uri);

	if (ldap_error != LDAP_SUCCESS)
		return FALSE;

	if (host)
		*host = g_strdup (lud->lud_host ? lud->lud_host : "");
	if (rootdn)
		*rootdn = g_strdup (lud->lud_dn ? lud->lud_dn : "");
	if (port)
		*port = lud->lud_port ? lud->lud_port : LDAP_PORT;
	if (scope)
		*scope = lud->lud_scope == LDAP_SCOPE_BASE     ? ADDRESSBOOK_LDAP_SCOPE_BASE :
			 lud->lud_scope == LDAP_SCOPE_ONELEVEL ? ADDRESSBOOK_LDAP_SCOPE_ONELEVEL :
			 lud->lud_scope == LDAP_SCOPE_SUBTREE  ? ADDRESSBOOK_LDAP_SCOPE_SUBTREE :
			 ADDRESSBOOK_LDAP_SCOPE_ONELEVEL;

	ldap_free_urldesc (lud);
	return TRUE;
}
#endif

#define SOURCE_PROP_STRING(source, prop) \
        (source && e_source_get_property (source, prop) ? e_source_get_property (source, prop) : "")

static void
source_to_dialog (AddressbookSourceDialog *dialog)
{
	ESource *source = dialog->source;

	gtk_entry_set_text (GTK_ENTRY (dialog->display_name), source ? e_source_peek_name (source) : "");

#ifdef HAVE_LDAP
	gtk_entry_set_text (GTK_ENTRY (dialog->email), SOURCE_PROP_STRING (source, "email_addr"));
	gtk_entry_set_text (GTK_ENTRY (dialog->binddn), SOURCE_PROP_STRING (source, "binddn"));
	gtk_entry_set_text (GTK_ENTRY (dialog->limit_spinbutton),
			    source && e_source_get_property (source, "limit") ?
			    e_source_get_property (source, "limit") : "100");

	dialog->auth = source && e_source_get_property (source, "auth") ?
		ldap_parse_auth (e_source_get_property (source, "auth")) : ADDRESSBOOK_LDAP_AUTH_NONE;
	dialog->ssl = source && e_source_get_property (source, "ssl") ?
		ldap_parse_ssl (e_source_get_property (source, "ssl")) : ADDRESSBOOK_LDAP_SSL_WHENEVER_POSSIBLE;

	if (source && !strcmp ("ldap://", e_source_group_peek_base_uri (dialog->source_group))) {
		gchar                    *host;
		gchar                    *rootdn;
		AddressbookLDAPScopeType  scope;
		gint                      port;

		if (source_to_uri_parts (source, &host, &rootdn, &scope, &port)) {
			gchar *port_str;

			gtk_entry_set_text (GTK_ENTRY (dialog->host), host);
			gtk_entry_set_text (GTK_ENTRY (dialog->rootdn), rootdn);

			dialog->scope = scope;

			port_str = g_strdup_printf ("%d", port);
			gtk_entry_set_text (GTK_ENTRY (GTK_COMBO (dialog->port_combo)->entry), port_str);
			g_free (port_str);

			g_free (host);
			g_free (rootdn);
		}
	}

	gtk_option_menu_set_history (GTK_OPTION_MENU(dialog->auth_optionmenu), dialog->auth);
	if (dialog->auth != ADDRESSBOOK_LDAP_AUTH_NONE) {
		gtk_notebook_set_current_page (GTK_NOTEBOOK(dialog->auth_label_notebook), dialog->auth - 1);
		gtk_notebook_set_current_page (GTK_NOTEBOOK(dialog->auth_entry_notebook), dialog->auth - 1);
	}
	gtk_widget_set_sensitive (dialog->auth_label_notebook, dialog->auth != ADDRESSBOOK_LDAP_AUTH_NONE);
	gtk_widget_set_sensitive (dialog->auth_entry_notebook, dialog->auth != ADDRESSBOOK_LDAP_AUTH_NONE);

	gtk_option_menu_set_history (GTK_OPTION_MENU(dialog->scope_optionmenu), dialog->scope);
	gtk_option_menu_set_history (GTK_OPTION_MENU(dialog->ssl_optionmenu), dialog->ssl);
#endif
}

#ifdef HAVE_LDAP

/* ldap api foo */
static LDAP *
addressbook_ldap_init (GtkWidget *window, ESource *source)
{
	LDAP  *ldap;
	gchar *host;
	gint   port;

	if (!source_to_uri_parts (source, &host, NULL, NULL, &port))
		return NULL;

	ldap = ldap_init (host, port);
	if (!ldap) {
		GtkWidget *dialog;
		dialog = gtk_message_dialog_new (GTK_WINDOW(window), 
						 GTK_DIALOG_MODAL,
						 GTK_MESSAGE_ERROR,
						 GTK_BUTTONS_OK,
						 _("Failed to connect to LDAP server"));
		g_signal_connect (dialog, "response", G_CALLBACK(gtk_widget_destroy), NULL);
		gtk_widget_show (dialog);
	}

	/* XXX do TLS if it's configured in */

	g_free (host);
	return ldap;
}

static gint
addressbook_ldap_auth (GtkWidget *window, LDAP *ldap)
{
	gint ldap_error;

	/* XXX use auth info from source */
	ldap_error = ldap_simple_bind_s (ldap, NULL, NULL);
	if (LDAP_SUCCESS != ldap_error) {
		GtkWidget *dialog;
		dialog = gtk_message_dialog_new (GTK_WINDOW (window),
						 GTK_DIALOG_MODAL,
						 GTK_MESSAGE_ERROR,
						 GTK_BUTTONS_OK,
						 _("Failed to authenticate with LDAP server"));
		g_signal_connect (dialog, "response", G_CALLBACK(gtk_widget_destroy), NULL);
		gtk_widget_show (dialog);
	}

	return ldap_error;
}

static int
addressbook_root_dse_query (GtkWindow *window, LDAP *ldap, char **attrs, LDAPMessage **resp)
{
	int ldap_error;
	struct timeval timeout;

	/* 3 second timeout */
	timeout.tv_sec = 3;
	timeout.tv_usec = 0;

	ldap_error = ldap_search_ext_s (ldap,
					LDAP_ROOT_DSE, LDAP_SCOPE_BASE,
					"(objectclass=*)",
					attrs, 0, NULL, NULL, &timeout, LDAP_NO_LIMIT, resp);
	if (LDAP_SUCCESS != ldap_error) {
		GtkWidget *dialog;
		dialog = gtk_message_dialog_new (window,
						 GTK_DIALOG_MODAL,
						 GTK_MESSAGE_ERROR,
						 GTK_BUTTONS_OK,
						 _("Could not perform query on Root DSE"));
		g_signal_connect (dialog, "response", G_CALLBACK(gtk_widget_destroy), NULL);
		gtk_widget_show (dialog);
	}

	return ldap_error;
}

#endif

static void
addressbook_source_dialog_destroy (gpointer data, GObject *where_object_was)
{
	AddressbookSourceDialog *dialog = data;

	g_object_unref (dialog->gui);
	g_free (dialog);
}

static void
addressbook_add_server_druid_cancel (GtkWidget *widget, AddressbookSourceDialog *dialog)
{
	gtk_widget_destroy (dialog->window);
}

static void
addressbook_add_server_druid_finish (GnomeDruidPage *druid_page, GtkWidget *gnome_druid, AddressbookSourceDialog *sdialog)
{
	sdialog->source = e_source_new ("", "");
	dialog_to_source (sdialog, sdialog->source, FALSE);

	/* tear down the widgets */
	gtk_widget_destroy (sdialog->window);
}

static void
reparent_to_vbox (AddressbookSourceDialog *dialog, char *vbox_name, char *widget_name)
{
	GtkWidget *vbox, *widget;

	vbox = glade_xml_get_widget (dialog->gui, vbox_name);
	widget = glade_xml_get_widget (dialog->gui, widget_name);

	gtk_widget_reparent (widget, vbox);
	gtk_box_set_child_packing (GTK_BOX (vbox), widget, TRUE, TRUE, 0, GTK_PACK_START);
}

static void
auth_optionmenu_activated (GtkWidget *item, AddressbookSourceDialog *dialog)
{
	dialog->auth = g_list_index (gtk_container_get_children (GTK_CONTAINER (item->parent)),
				     item);

	dialog->general_modify_func (item, dialog);

	if (dialog->auth == 0) {
		gtk_widget_set_sensitive (dialog->auth_label_notebook, FALSE);
		gtk_widget_set_sensitive (dialog->auth_entry_notebook, FALSE);
	}
	else {
		gtk_widget_set_sensitive (dialog->auth_label_notebook, TRUE);
		gtk_widget_set_sensitive (dialog->auth_entry_notebook, TRUE);
		gtk_notebook_set_current_page (GTK_NOTEBOOK(dialog->auth_label_notebook), dialog->auth - 1);
		gtk_notebook_set_current_page (GTK_NOTEBOOK(dialog->auth_entry_notebook), dialog->auth - 1);
	}
}

static void
add_auth_activate_cb (GtkWidget *item, AddressbookSourceDialog *dialog)
{
	g_signal_connect (item, "activate",
			  G_CALLBACK (auth_optionmenu_activated), dialog);
}

static void
setup_general_tab (AddressbookSourceDialog *dialog, ModifyFunc modify_func)
{
	GtkWidget *general_tab_help;
	GtkWidget *menu;

	general_tab_help = glade_xml_get_widget (dialog->gui, "general-tab-help");

	dialog->general_modify_func = modify_func;
	dialog->host = glade_xml_get_widget (dialog->gui, "server-name-entry");
	g_signal_connect (dialog->host, "changed",
			  G_CALLBACK (modify_func), dialog);
	add_focus_handler (dialog->host, general_tab_help, 0);

	dialog->auth_label_notebook = glade_xml_get_widget (dialog->gui, "auth-label-notebook");
	dialog->auth_entry_notebook = glade_xml_get_widget (dialog->gui, "auth-entry-notebook");
	dialog->email = glade_xml_get_widget (dialog->gui, "email-entry");
	g_signal_connect (dialog->email, "changed",
			  G_CALLBACK (modify_func), dialog);
	add_focus_handler (dialog->email, general_tab_help, 1);
	dialog->binddn = glade_xml_get_widget (dialog->gui, "dn-entry");
	g_signal_connect (dialog->binddn, "changed",
			  G_CALLBACK (modify_func), dialog);
	add_focus_handler (dialog->binddn, general_tab_help, 2);

	dialog->auth_optionmenu = glade_xml_get_widget (dialog->gui, "auth-optionmenu");
	menu = gtk_option_menu_get_menu (GTK_OPTION_MENU(dialog->auth_optionmenu));
	gtk_container_foreach (GTK_CONTAINER (menu), (GtkCallback)add_auth_activate_cb, dialog);
	add_focus_handler (dialog->auth_optionmenu, general_tab_help, 3);
}

static gboolean
general_tab_check (AddressbookSourceDialog *dialog)
{
	gboolean valid = TRUE;
	const char *string;

	if (strcmp ("ldap://", e_source_group_peek_base_uri (dialog->source_group)))
		return TRUE;

	string = gtk_entry_get_text (GTK_ENTRY (dialog->host));
	if (!string || !string[0])
		valid = FALSE;

	if (valid) {
		if (dialog->auth != ADDRESSBOOK_LDAP_AUTH_NONE) {
			if (dialog->auth == ADDRESSBOOK_LDAP_AUTH_SIMPLE_BINDDN)
				string = gtk_entry_get_text (GTK_ENTRY (dialog->binddn));
			else
				string = gtk_entry_get_text (GTK_ENTRY (dialog->email));

			if (!string || !string[0])
				valid = FALSE;
		}
	}

	return valid;
}

static void
druid_info_page_modify_cb (GtkWidget *item, AddressbookSourceDialog *dialog)
{
	gnome_druid_set_buttons_sensitive (GNOME_DRUID(dialog->druid),
					   TRUE, /* back */
					   general_tab_check (dialog), /* next */
					   TRUE, /* cancel */
					   FALSE /* help */);
}

static void
druid_info_page_prepare (GnomeDruidPage *dpage, GtkWidget *gdruid, AddressbookSourceDialog *dialog)
{
	druid_info_page_modify_cb (NULL, dialog);
	/* stick the focus in the hostname field */
	gtk_widget_grab_focus (dialog->host);
}


/* connecting page */
static void
ssl_optionmenu_activated (GtkWidget *item, AddressbookSourceDialog *dialog)
{
	dialog->ssl = g_list_index (gtk_container_get_children (GTK_CONTAINER (item->parent)),
				    item);

	dialog->connecting_modify_func (item, dialog);
}

static void
ssl_optionmenu_selected (GtkWidget *item, AddressbookSourceDialog *dialog)
{
	GtkWidget *connecting_tab_help;
	int ssl_type = g_list_index (gtk_container_get_children (GTK_CONTAINER (item->parent)),
				     item);

	connecting_tab_help = glade_xml_get_widget (dialog->gui, "connecting-tab-help");

	gtk_notebook_set_current_page (GTK_NOTEBOOK(connecting_tab_help), ssl_type + 1);
}

static void
add_ssl_activate_cb (GtkWidget *item, AddressbookSourceDialog *dialog)
{
	g_signal_connect (item, "activate",
			  G_CALLBACK (ssl_optionmenu_activated), dialog);
	g_signal_connect (item, "select",
			  G_CALLBACK (ssl_optionmenu_selected), dialog);
}

static void
port_changed_func (GtkWidget *item, AddressbookSourceDialog *dialog)
{
	/* if the port value is ldaps, set the SSL/TLS option menu to
	   Always and desensitize it */
	const char *string = gtk_entry_get_text (GTK_ENTRY (item));

	dialog->connecting_modify_func (item, dialog);

	if (!strcmp (string, LDAPS_PORT_STRING)) {
		dialog->ssl = ADDRESSBOOK_LDAP_SSL_ALWAYS;
		gtk_option_menu_set_history (GTK_OPTION_MENU(dialog->ssl_optionmenu),
					     dialog->ssl);

		gtk_widget_set_sensitive (dialog->ssl_optionmenu, FALSE);
	}
	else {
		gtk_widget_set_sensitive (dialog->ssl_optionmenu, TRUE);
	}

}

static void
setup_connecting_tab (AddressbookSourceDialog *dialog, ModifyFunc modify_func)
{
	GtkWidget *menu;
	GtkWidget *connecting_tab_help;

	dialog->connecting_modify_func = modify_func;

	connecting_tab_help = glade_xml_get_widget (dialog->gui, "connecting-tab-help");

	dialog->port_combo = glade_xml_get_widget (dialog->gui, "port-combo");
	add_focus_handler (dialog->port_combo, connecting_tab_help, 0);
	add_focus_handler (GTK_COMBO(dialog->port_combo)->entry, connecting_tab_help, 0);
	g_signal_connect (GTK_COMBO(dialog->port_combo)->entry, "changed",
			  G_CALLBACK (modify_func), dialog);
	g_signal_connect (GTK_COMBO(dialog->port_combo)->entry, "changed",
			  G_CALLBACK (port_changed_func), dialog);
	dialog->ssl_optionmenu = glade_xml_get_widget (dialog->gui, "ssl-optionmenu");
	menu = gtk_option_menu_get_menu (GTK_OPTION_MENU(dialog->ssl_optionmenu));
	gtk_container_foreach (GTK_CONTAINER (menu), (GtkCallback)add_ssl_activate_cb, dialog);
}

static gboolean
connecting_tab_check (AddressbookSourceDialog *dialog)
{
	gboolean valid = TRUE;
	const char *string;

	string = gtk_entry_get_text (GTK_ENTRY (GTK_COMBO(dialog->port_combo)->entry));
	if (!string || !string[0])
		valid = FALSE;

	return valid;
}

static void
druid_connecting_page_modify_cb (GtkWidget *item, AddressbookSourceDialog *dialog)
{
	gnome_druid_set_buttons_sensitive (GNOME_DRUID(dialog->druid),
					   TRUE, /* back */
					   connecting_tab_check (dialog), /* next */
					   TRUE, /* cancel */
					   FALSE /* help */);
}

static void
druid_connecting_page_prepare (GnomeDruidPage *dpage, GtkWidget *gdruid, AddressbookSourceDialog *dialog)
{
	druid_connecting_page_modify_cb (NULL, dialog);
	/* stick the focus in the port combo */
	gtk_widget_grab_focus (GTK_COMBO(dialog->port_combo)->entry);
}



#ifdef HAVE_LDAP

/* searching page */
static ETableMemoryStoreColumnInfo bases_table_columns[] = {
	E_TABLE_MEMORY_STORE_STRING,
	E_TABLE_MEMORY_STORE_TERMINATOR
};

#define BASES_TABLE_SPEC \
"<ETableSpecification cursor-mode=\"line\" no-headers=\"true\"> \
  <ETableColumn model_col= \"0\" _title=\"Base\" expansion=\"1.0\" minimum_width=\"20\" resizable=\"true\" cell=\"string\" compare=\"string\"/> \
  <ETableState> \
    <column source=\"0\"/> \
    <grouping></grouping> \
  </ETableState> \
</ETableSpecification>"

GtkWidget*
supported_bases_create_table (char *name, char *string1, char *string2, int num1, int num2)
{
	GtkWidget *table;
	ETableModel *model;

	model = e_table_memory_store_new (bases_table_columns);

	table = e_table_scrolled_new (model, NULL, BASES_TABLE_SPEC, NULL);

	g_object_set_data (G_OBJECT (table), "model", model);

	return table;
}

static gboolean
do_ldap_root_dse_query (GtkWidget *dialog, ETableModel *model, ESource *source, char ***rvalues)
{
	LDAP *ldap;
	char *attrs[2];
	int ldap_error;
	char **values;
	LDAPMessage *resp;
	int i;

	ldap = addressbook_ldap_init (dialog, source);
	if (!ldap)
		return FALSE;

	if (LDAP_SUCCESS != addressbook_ldap_auth (dialog, ldap))
		goto fail;

	attrs[0] = "namingContexts";
	attrs[1] = NULL;

	ldap_error = addressbook_root_dse_query (GTK_WINDOW (dialog), ldap, attrs, &resp);

	if (ldap_error != LDAP_SUCCESS)
		goto fail;

	values = ldap_get_values (ldap, resp, "namingContexts");
	if (!values || values[0] == NULL) {
		GtkWidget *error_dialog;
		error_dialog = gtk_message_dialog_new (GTK_WINDOW (dialog),
						       GTK_DIALOG_MODAL,
						       GTK_MESSAGE_ERROR,
						       GTK_BUTTONS_OK,
						       _("The server responded with no supported search bases"));
		g_signal_connect (error_dialog, "response", G_CALLBACK(gtk_widget_destroy), NULL);
		gtk_widget_show (error_dialog);
		goto fail;
	}

	for (i = 0; values[i]; i++)
		e_table_memory_store_insert (E_TABLE_MEMORY_STORE (model),
					     -1, GINT_TO_POINTER(i), values[i]);

	*rvalues = values;

	ldap_unbind_s (ldap);
	return TRUE;

 fail:
	ldap_unbind_s (ldap);
	return FALSE;
}

static void
search_base_selection_model_changed (ESelectionModel *selection_model, GtkWidget *dialog)
{
	gtk_dialog_set_response_sensitive (GTK_DIALOG (dialog),
					   GTK_RESPONSE_OK,
					   e_selection_model_selected_count (selection_model) == 1);
}

static void
query_for_supported_bases (GtkWidget *button, AddressbookSourceDialog *sdialog)
{
	ESelectionModel *selection_model;
	ESource *source;
	GtkWidget *dialog;
	GtkWidget *supported_bases_table;
	ETableModel *model;
	int id;
	char **values;

	source = dialog_to_temp_source (sdialog);

	dialog = glade_xml_get_widget (sdialog->gui, "supported-bases-dialog");

	supported_bases_table = glade_xml_get_widget (sdialog->gui, "supported-bases-table");
	gtk_widget_show (supported_bases_table);
	selection_model = e_table_get_selection_model (e_table_scrolled_get_table (E_TABLE_SCROLLED(supported_bases_table)));
	model = g_object_get_data (G_OBJECT (supported_bases_table), "model");

	g_signal_connect (selection_model, "selection_changed",
			  G_CALLBACK (search_base_selection_model_changed), dialog);

	search_base_selection_model_changed (selection_model, dialog);

	if (do_ldap_root_dse_query (dialog, model, source, &values)) {
		id = gtk_dialog_run (GTK_DIALOG (dialog));

		gtk_widget_hide (dialog);

		if (id == GTK_RESPONSE_OK) {
			int i;
			/* OK was clicked */

			/* ugh. */
			for (i = 0; values[i]; i ++) {
				if (e_selection_model_is_row_selected (selection_model, i)) {
					gtk_entry_set_text (GTK_ENTRY (sdialog->rootdn), values[i]);
					break; /* single selection, so we can quit when we've found it. */
				}
			}
		}

		ldap_value_free (values);

		e_table_memory_store_clear (E_TABLE_MEMORY_STORE (model));
	}

	g_object_unref (source);
}

static void
scope_optionmenu_activated (GtkWidget *item, AddressbookSourceDialog *dialog)
{
	dialog->scope = g_list_index (gtk_container_get_children (GTK_CONTAINER (item->parent)),
				      item);

	if (dialog->searching_modify_func)
		dialog->searching_modify_func (item, dialog);
}

static void
add_scope_activate_cb (GtkWidget *item, AddressbookSourceDialog *dialog)
{
	g_signal_connect (item, "activate",
			  G_CALLBACK (scope_optionmenu_activated), dialog);
}

static void
setup_searching_tab (AddressbookSourceDialog *dialog, ModifyFunc modify_func)
{
	GtkWidget *menu;
	GtkWidget *rootdn_button;
	GtkWidget *searching_tab_help;

	dialog->searching_modify_func = modify_func;

	searching_tab_help = glade_xml_get_widget (dialog->gui, "searching-tab-help");

	dialog->rootdn = glade_xml_get_widget (dialog->gui, "rootdn-entry");
	add_focus_handler (dialog->rootdn, searching_tab_help, 0);
	if (modify_func)
		g_signal_connect (dialog->rootdn, "changed",
				  G_CALLBACK (modify_func), dialog);

	dialog->scope_optionmenu = glade_xml_get_widget (dialog->gui, "scope-optionmenu");
	add_focus_handler (dialog->scope_optionmenu, searching_tab_help, 1);
	menu = gtk_option_menu_get_menu (GTK_OPTION_MENU(dialog->scope_optionmenu));
	gtk_container_foreach (GTK_CONTAINER (menu), (GtkCallback)add_scope_activate_cb, dialog);

	dialog->timeout_scale = glade_xml_get_widget (dialog->gui, "timeout-scale");
	add_focus_handler (dialog->timeout_scale, searching_tab_help, 2);
	if (modify_func)
		g_signal_connect (GTK_RANGE(dialog->timeout_scale)->adjustment,
				  "value_changed",
				  G_CALLBACK (modify_func), dialog);

	dialog->limit_spinbutton = glade_xml_get_widget (dialog->gui, "download-limit-spinbutton");
	if (modify_func)
		g_signal_connect (dialog->limit_spinbutton, "changed",
				  G_CALLBACK (modify_func), dialog);

	/* special handling for the "Show Supported Bases button" */
	rootdn_button = glade_xml_get_widget (dialog->gui, "rootdn-button");
	g_signal_connect (rootdn_button, "clicked",
			  G_CALLBACK(query_for_supported_bases), dialog);
}

static void
druid_searching_page_prepare (GnomeDruidPage *dpage, GtkWidget *gdruid, AddressbookSourceDialog *dialog)
{
	gnome_druid_set_buttons_sensitive (GNOME_DRUID(dialog->druid),
					   TRUE, /* back */
					   TRUE, /* next */
					   TRUE, /* cancel */
					   FALSE /* help */);
}

#endif


/* display name page */
static gboolean
display_name_check (AddressbookSourceDialog *dialog)
{
	gboolean valid = TRUE;
	const char *string;

	string = gtk_entry_get_text (GTK_ENTRY (dialog->display_name));
	if (!string || !string[0])
		valid = FALSE;

	return valid;
}

static void
folder_page_prepare (GtkWidget *page, GtkWidget *gnome_druid, AddressbookSourceDialog *dialog)
{
	if (!dialog->display_name_changed) {
		const char *server_name = gtk_entry_get_text (GTK_ENTRY (dialog->host));
		gtk_entry_set_text (GTK_ENTRY (dialog->display_name), server_name);
	}

	gnome_druid_set_buttons_sensitive (GNOME_DRUID(dialog->druid),
					   TRUE, /* back */
					   display_name_check (dialog), /* next */
					   TRUE, /* cancel */
					   FALSE /* help */);
}

static void
druid_folder_page_modify_cb (GtkWidget *item, AddressbookSourceDialog *dialog)
{
	dialog->display_name_changed = TRUE;
	folder_page_prepare (NULL, NULL, dialog);
}



static void
source_group_changed_cb (GtkWidget *widget, AddressbookSourceDialog *sdialog)
{
	sdialog->source_group = g_slist_nth (e_source_list_peek_groups (sdialog->source_list),
		gtk_option_menu_get_history (GTK_OPTION_MENU (sdialog->group_optionmenu)))->data;
}

static void
source_group_menu_add_groups (GtkMenuShell *menu_shell, ESourceList *source_list)
{
	GSList *groups, *sl;

	groups = e_source_list_peek_groups (source_list);
	for (sl = groups; sl; sl = g_slist_next (sl)) {
		GtkWidget *menu_item;
		ESourceGroup *group = sl->data;

#ifndef HAVE_LDAP
		/* If LDAP isn't configured, skip LDAP groups */
		if (!strcmp ("ldap://", e_source_group_peek_base_uri (group)))
			continue;
#endif

		menu_item = gtk_menu_item_new_with_label (e_source_group_peek_name (group));
		gtk_widget_show (menu_item);
		gtk_menu_shell_append (menu_shell, menu_item);
	}
}

static gboolean
folder_page_forward (GtkWidget *page, GtkWidget *widget, AddressbookSourceDialog *sdialog)
{
	GtkWidget *finish_page = glade_xml_get_widget (sdialog->gui, "add-server-druid-finish-page");
	
	if (strcmp ("ldap://", e_source_group_peek_base_uri (sdialog->source_group))) {
		gnome_druid_set_page (GNOME_DRUID (sdialog->druid), GNOME_DRUID_PAGE (finish_page));
		return TRUE;
	}

	return FALSE;
}

static gboolean
finish_page_back (GtkWidget *page, GtkWidget *widget, AddressbookSourceDialog *sdialog)
{
	GtkWidget *folder_page = glade_xml_get_widget (sdialog->gui, "add-server-druid-folder-page");
	
	if (strcmp ("ldap://", e_source_group_peek_base_uri (sdialog->source_group))) {
		gnome_druid_set_page (GNOME_DRUID (sdialog->druid), GNOME_DRUID_PAGE (folder_page));
		return TRUE;
	}

	return FALSE;
}

static AddressbookSourceDialog *
addressbook_add_server_druid (void)
{
	AddressbookSourceDialog *sdialog = g_new0 (AddressbookSourceDialog, 1);
	GtkWidget *page;
	GConfClient *gconf_client;

	sdialog->gui = glade_xml_new (EVOLUTION_GLADEDIR "/" GLADE_FILE_NAME, NULL, NULL);

	sdialog->window = glade_xml_get_widget (sdialog->gui, "account-druid-window");
	sdialog->druid = glade_xml_get_widget (sdialog->gui, "account-druid");

	/* general page */
	page = glade_xml_get_widget (sdialog->gui, "add-server-druid-folder-page");
	sdialog->display_name = glade_xml_get_widget (sdialog->gui, "druid-display-name-entry");
	g_signal_connect (sdialog->display_name, "changed",
			  G_CALLBACK (druid_folder_page_modify_cb), sdialog);
	g_signal_connect_after (page, "prepare",
				G_CALLBACK (folder_page_prepare), sdialog);
	g_signal_connect_after (page, "next",
				G_CALLBACK (folder_page_forward), sdialog);

	gconf_client = gconf_client_get_default ();
	sdialog->source_list = e_source_list_new_for_gconf (gconf_client, "/apps/evolution/addressbook/sources");
	sdialog->group_optionmenu = glade_xml_get_widget (sdialog->gui, "druid-group-option-menu");
	if (!GTK_IS_MENU (gtk_option_menu_get_menu (GTK_OPTION_MENU (sdialog->group_optionmenu)))) {
		GtkWidget *menu = gtk_menu_new ();
		gtk_option_menu_set_menu (GTK_OPTION_MENU (sdialog->group_optionmenu), menu);
		gtk_widget_show (menu);
	}

	/* NOTE: This assumes that we have sources. If they don't exist, they're set up
	 * on startup of the Addressbook component. */
	source_group_menu_add_groups (GTK_MENU_SHELL (gtk_option_menu_get_menu (
		GTK_OPTION_MENU (sdialog->group_optionmenu))), sdialog->source_list);
	gtk_option_menu_set_history (GTK_OPTION_MENU (sdialog->group_optionmenu), 0);
	sdialog->source_group = e_source_list_peek_groups (sdialog->source_list)->data;
	g_signal_connect (sdialog->group_optionmenu, "changed",
			  G_CALLBACK (source_group_changed_cb), sdialog);

#ifdef HAVE_LDAP

	/* info page */
	page = glade_xml_get_widget (sdialog->gui, "add-server-druid-info-page");
	reparent_to_vbox (sdialog, "account-druid-general-vbox", "general-tab");
	setup_general_tab (sdialog, druid_info_page_modify_cb);
	g_signal_connect_after (page, "prepare",
				G_CALLBACK(druid_info_page_prepare), sdialog);

	/* connecting page */
	page = glade_xml_get_widget (sdialog->gui, "add-server-druid-connecting-page");
	reparent_to_vbox (sdialog, "account-druid-connecting-vbox", "connecting-tab");
	setup_connecting_tab (sdialog, druid_connecting_page_modify_cb);
	g_signal_connect_after (page, "prepare",
				G_CALLBACK(druid_connecting_page_prepare), sdialog);

	/* searching page */
	page = glade_xml_get_widget (sdialog->gui, "add-server-druid-searching-page");
	reparent_to_vbox (sdialog, "account-druid-searching-vbox", "searching-tab");
	setup_searching_tab (sdialog, NULL);
	g_signal_connect_after (page, "prepare",
				G_CALLBACK(druid_searching_page_prepare), sdialog);

#endif

	/* finish page */
	page = glade_xml_get_widget (sdialog->gui, "add-server-druid-finish-page");
	g_signal_connect (page, "finish",
			  G_CALLBACK(addressbook_add_server_druid_finish), sdialog);
	g_signal_connect_after (page, "back",
				G_CALLBACK (finish_page_back), sdialog);
	g_signal_connect (sdialog->druid, "cancel",
			  G_CALLBACK(addressbook_add_server_druid_cancel), sdialog);
	g_object_weak_ref (G_OBJECT (sdialog->window),
			   addressbook_source_dialog_destroy, sdialog);

	/* make sure we fill in the default values */
	source_to_dialog (sdialog);

	gtk_window_set_type_hint (GTK_WINDOW (sdialog->window), GDK_WINDOW_TYPE_HINT_DIALOG);
	gtk_window_set_modal (GTK_WINDOW (sdialog->window), TRUE);

	gtk_widget_show_all (sdialog->window);

	return sdialog;
}

static void
editor_modify_cb (GtkWidget *item, AddressbookSourceDialog *dialog)
{
	gboolean valid = TRUE;

	valid = display_name_check (dialog);
#ifdef HAVE_LDAP
	if (valid)
		valid = general_tab_check (dialog);
	if (valid)
		valid = connecting_tab_check (dialog);
#if 0
	if (valid)
		valid = searching_tab_check (dialog);
#endif
#endif

	gtk_widget_set_sensitive (dialog->ok_button, valid);
}

static void
set_advanced_button_state (AddressbookSourceDialog *dialog)
{
	if (dialog->advanced) {
		gtk_notebook_set_current_page (GTK_NOTEBOOK(dialog->advanced_button_notebook), 0);
#ifdef NEW_ADVANCED_UI
		gtk_notebook_append_page (GTK_NOTEBOOK(dialog->notebook), dialog->objectclasses_tab, dialog->objectclasses_label);
		gtk_notebook_append_page (GTK_NOTEBOOK(dialog->notebook), dialog->mappings_tab, dialog->mappings_label);
		gtk_notebook_append_page (GTK_NOTEBOOK(dialog->notebook), dialog->dn_customization_tab, dialog->dn_customization_label);
#endif
	}
	else {
#ifdef NEW_ADVANCED_UI
		gtk_notebook_set_current_page (GTK_NOTEBOOK(dialog->advanced_button_notebook), 1);
		
		/* hide the advanced tabs of the main notebook */
		gtk_notebook_remove_page (GTK_NOTEBOOK(dialog->notebook), 5);
		gtk_notebook_remove_page (GTK_NOTEBOOK(dialog->notebook), 4);
		gtk_notebook_remove_page (GTK_NOTEBOOK(dialog->notebook), 3);
#endif
	}
}

#ifdef NEW_ADVANCED_UI
static void
advanced_button_clicked (GtkWidget *button, AddressbookSourceDialog *dialog)
{
	dialog->advanced = !dialog->advanced;
	set_advanced_button_state (dialog);
}

static gboolean
do_schema_query (AddressbookSourceDialog *sdialog)
{
	LDAP *ldap;
	int ldap_error;
	char *schema_dn;
	char *attrs[3];
	char **values;
	int i;
	AddressbookSource *source = addressbook_dialog_get_source (sdialog);
	LDAPMessage *resp;
	struct timeval timeout;

	ldap = addressbook_ldap_init (sdialog->window, source);
	if (!ldap)
		goto fail;

	if (LDAP_SUCCESS != addressbook_ldap_auth (sdialog->window, source, ldap))
		goto fail;

	attrs[0] = "subschemaSubentry";
	attrs[1] = NULL;

	ldap_error = addressbook_root_dse_query (sdialog->window, source, ldap, attrs, &resp);

	if (ldap_error != LDAP_SUCCESS)
		goto fail;

	values = ldap_get_values (ldap, resp, "subschemaSubentry");
	if (!values || values[0] == NULL) {
		GtkWidget *dialog;
		dialog = gnome_ok_dialog_parented (_("This server does not support LDAPv3 schema information"), GTK_WINDOW (sdialog->window));
		gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
		goto fail;
	}

	schema_dn = g_strdup (values[0]);

	ldap_value_free (values);
	ldap_msgfree (resp);

	attrs[0] = "objectClasses";
	attrs[1] = NULL;

	/* 3 second timeout */
	timeout.tv_sec = 3;
	timeout.tv_usec = 0;

	ldap_error = ldap_search_ext_s (ldap, schema_dn, LDAP_SCOPE_BASE,
					"(objectClass=subschema)", attrs, 0,
					NULL, NULL, &timeout, LDAP_NO_LIMIT, &resp);
	if (LDAP_SUCCESS != ldap_error) {
		GtkWidget *dialog;
		dialog = gnome_error_dialog_parented (_("Error retrieving schema information"), GTK_WINDOW (sdialog->window));
		gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
		goto fail;
	}

	values = ldap_get_values (ldap, resp, "objectClasses");
	if (!values) {
		GtkWidget *dialog;
		dialog = gnome_error_dialog_parented (_("Server did not respond with valid schema information"), GTK_WINDOW (sdialog->window));
		gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
		goto fail;
	}

	for (i = 0; values[i]; i ++) { 
		int j;
		int code;
		const char *err;
		LDAPObjectClass *oc = ldap_str2objectclass (values[i], &code, &err, 0);

		if (!oc)
			continue;

		/* we fill in the default list of classes here */
		for (j = 0; oc->oc_names[j]; j ++) {
			if (!g_strcasecmp (oc->oc_names[j], EVOLUTIONPERSON) ||
			    !g_strcasecmp (oc->oc_names[j], INETORGPERSON) ||
			    !g_strcasecmp (oc->oc_names[j], ORGANIZATIONALPERSON) ||
			    !g_strcasecmp (oc->oc_names[j], PERSON) ||
			    !g_strcasecmp (oc->oc_names[j], CALENTRY) ||
			    !g_strcasecmp (oc->oc_names[j], TOP))
				g_ptr_array_add (sdialog->default_objectclasses, oc);
		}

		g_ptr_array_add (sdialog->server_objectclasses, oc);
	}

	addressbook_source_free (source);
	ldap_unbind_s (ldap);
	return TRUE;

 fail:
	addressbook_source_free (source);
	if (ldap)
		ldap_unbind_s (ldap);
	return FALSE;
}

static void
edit_dialog_switch_page (GtkNotebook *notebook,
			 GtkNotebookPage *page, guint page_num,
			 AddressbookSourceDialog *sdialog)
{
	if (page_num >= 3 && !sdialog->schema_query_successful) {
		int i;

		gtk_widget_set_sensitive (GTK_WIDGET (notebook), FALSE);

		sdialog->schema_query_successful = do_schema_query (sdialog);

		if (sdialog->schema_query_successful) {
			/* fill in the objectclasses model */
			for (i = 0; i < sdialog->server_objectclasses->len; i ++) {
				LDAPObjectClass *oc = g_ptr_array_index (sdialog->server_objectclasses, i);
				e_table_memory_store_insert (E_TABLE_MEMORY_STORE (sdialog->objectclasses_server_model),
							     -1, oc, oc->oc_names[0]);
			}
			gtk_widget_set_sensitive (page->child, TRUE);
		}
		else {
			gtk_widget_set_sensitive (page->child, FALSE);
		}

		gtk_widget_set_sensitive (GTK_WIDGET (notebook), TRUE);
	}
}
#endif

static gboolean
edit_dialog_store_change (AddressbookSourceDialog *sdialog)
{
	dialog_to_source (sdialog, sdialog->source, FALSE);

	/* check the display name for uniqueness */
	if (FALSE /* XXX */) {
		return FALSE;
	}

	return TRUE;
}

static void
edit_dialog_cancel_clicked (GtkWidget *item, AddressbookSourceDialog *sdialog)
{
	gtk_widget_destroy (sdialog->window);
}

static void
edit_dialog_ok_clicked (GtkWidget *item, AddressbookSourceDialog *sdialog)
{
	if (edit_dialog_store_change (sdialog)) {
		gtk_widget_destroy (sdialog->window);
	}
}

void
addressbook_config_edit_source (GtkWidget *parent, ESource *source)
{
	AddressbookSourceDialog *sdialog = g_new0 (AddressbookSourceDialog, 1);
	GtkWidget *general_tab_help;

	sdialog->gui = glade_xml_new (EVOLUTION_GLADEDIR "/" GLADE_FILE_NAME, NULL, NULL);
	sdialog->window = glade_xml_get_widget (sdialog->gui, "account-editor-window");

	sdialog->source = source;
	sdialog->source_group = e_source_peek_group (source);

	sdialog->display_name = glade_xml_get_widget (sdialog->gui, "account-editor-display-name-entry");
	g_signal_connect (sdialog->display_name, "changed",
			  G_CALLBACK (editor_modify_cb), sdialog);

#ifdef HAVE_LDAP

	/* general tab */
	general_tab_help = glade_xml_get_widget (sdialog->gui, "general-tab-help");
	reparent_to_vbox (sdialog, "account-editor-general-ldap-vbox", "general-tab");
	setup_general_tab (sdialog, editor_modify_cb);

	/* connecting tab */
	reparent_to_vbox (sdialog, "account-editor-connecting-vbox", "connecting-tab");
	setup_connecting_tab (sdialog, editor_modify_cb);

	/* searching tab */
	reparent_to_vbox (sdialog, "account-editor-searching-vbox", "searching-tab");
	setup_searching_tab (sdialog, editor_modify_cb);

#endif

	sdialog->notebook = glade_xml_get_widget (sdialog->gui, "account-editor-notebook");

	sdialog->ok_button = glade_xml_get_widget (sdialog->gui, "account-editor-ok-button");
	sdialog->cancel_button = glade_xml_get_widget (sdialog->gui, "account-editor-cancel-button");

#ifdef HAVE_LDAP
	if (strcmp ("ldap://", e_source_group_peek_base_uri (sdialog->source_group))) {
		gtk_widget_hide (glade_xml_get_widget (sdialog->gui, "account-editor-general-ldap-vbox"));
		gtk_widget_hide (glade_xml_get_widget (sdialog->gui, "account-editor-connecting-vbox"));
		gtk_widget_hide (glade_xml_get_widget (sdialog->gui, "account-editor-searching-vbox"));
	} else {
		add_focus_handler (sdialog->display_name, general_tab_help, 4);
	}
#else
	gtk_widget_hide (glade_xml_get_widget (sdialog->gui, "account-editor-general-ldap-vbox"));
	gtk_widget_hide (glade_xml_get_widget (sdialog->gui, "account-editor-connecting-vbox"));
	gtk_widget_hide (glade_xml_get_widget (sdialog->gui, "account-editor-searching-vbox"));
#endif

	source_to_dialog (sdialog);

	set_advanced_button_state (sdialog);

	g_signal_connect (sdialog->ok_button,
			  "clicked", G_CALLBACK(edit_dialog_ok_clicked), sdialog);
	g_signal_connect (sdialog->cancel_button,
			  "clicked", G_CALLBACK(edit_dialog_cancel_clicked), sdialog);
	g_object_weak_ref (G_OBJECT (sdialog->window),
			   addressbook_source_dialog_destroy, sdialog);

	gtk_widget_set_sensitive (sdialog->ok_button, FALSE);

	gtk_window_set_type_hint (GTK_WINDOW (sdialog->window), GDK_WINDOW_TYPE_HINT_DIALOG);
	gtk_window_set_modal (GTK_WINDOW (sdialog->window), TRUE);

	gtk_widget_show (sdialog->window);
}

void
addressbook_config_create_new_source (GtkWidget *parent)
{
	AddressbookSourceDialog *dialog;

	dialog = addressbook_add_server_druid ();
}

#if 0
#ifdef STANDALONE
int
main(int argc, char **argv)
{
	AddressbookDialog *dialog;

	gnome_init_with_popt_table ("evolution-addressbook", "0.0",
				    argc, argv, oaf_popt_options, 0, NULL);

	glade_init ();

	bindtextdomain (GETTEXT_PACKAGE, EVOLUTION_LOCALEDIR);
	textdomain (GETTEXT_PACKAGE);

#if 0
	g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING);
#endif

	gtk_widget_push_colormap (gdk_rgb_get_cmap ());

	dialog = ldap_dialog_new (NULL);

	gtk_widget_show (glade_xml_get_widget (dialog->gui, "addressbook-sources-window"));

	gtk_main();

	return 0;
}
#endif

#endif
