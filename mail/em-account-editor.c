/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors:
 *    Dan Winship <danw@ximian.com>
 *    Jeffrey Stedfast <fejj@ximian.com>
 *    Michael Zucchi <notzed@ximian.com>
 *
 *  Copyright 2001 Ximian, Inc. (www.ximian.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib.h>

#include <string.h>
#include <stdarg.h>

#include <gconf/gconf-client.h>

#include <glade/glade.h>

#include <gtk/gtkentry.h>
#include <gtk/gtktogglebutton.h>
#include <gtk/gtktextbuffer.h>
#include <gtk/gtktextview.h>
#include <gtk/gtkcheckbutton.h>
#include <gtk/gtkspinbutton.h>
#include <gtk/gtkmenuitem.h>
#include <gtk/gtkoptionmenu.h>
#include <gtk/gtknotebook.h>
#include <gtk/gtkhbox.h>
#include <gtk/gtkcellrenderertext.h>
#include <gtk/gtkcelllayout.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkcombobox.h>
#include <gtk/gtktable.h>

#include <libgnomeui/gnome-druid.h>
#include <libgnomeui/gnome-druid-page-standard.h>

#include <e-util/e-account-list.h>
#include <e-util/e-signature-list.h>

#include <widgets/misc/e-error.h>

#include "em-config.h"
#include "em-folder-selection-button.h"
#include "em-account-editor.h"
#include "mail-session.h"
#include "mail-send-recv.h"
#include "mail-signature-editor.h"
#include "mail-component.h"
#include "em-utils.h"
#include "em-composer-prefs.h"
#include "mail-config.h"
#include "mail-ops.h"
#include "mail-mt.h"

#if defined (HAVE_NSS)
#include "smime/gui/e-cert-selector.h"
#endif

#define d(x)

typedef struct _EMAccountEditorService {
	EMAccountEditor *emae;	/* parent pointer, for callbacks */

	struct _GtkWidget *frame;
	struct _GtkWidget *container;

	struct _GtkLabel *description;
	struct _GtkEntry *hostname;
	struct _GtkEntry *username;
	struct _GtkEntry *path;
	struct _GtkWidget *ssl_frame;
	struct _GtkComboBox *use_ssl;
	struct _GtkWidget *ssl_selected;
	struct _GtkWidget *ssl_hbox;
	struct _GtkWidget *no_ssl;

	struct _GtkWidget *auth_frame;
	struct _GtkComboBox *authtype;
	struct _GtkWidget *authitem;
	struct _GtkToggleButton *remember;
	struct _GtkButton *check_supported;
	struct _GtkToggleButton *needs_auth;

	CamelURL *url;		/* for working with various options changing */

	GList *authtypes;	/* if "Check supported" */
	CamelProvider *provider;
	CamelProviderType type;

	int auth_changed_id;
} EMAccountEditorService;

typedef struct _EMAccountEditorPrivate {
	struct _GladeXML *xml;
	struct _GladeXML *druidxml;
	struct _EMConfig *config;
	GList *providers;

	/* signatures */
	struct _GtkOptionMenu *sig_menu;
	guint sig_added_id;
	guint sig_removed_id;
	guint sig_changed_id;
	const char *sig_uid;
	
	/* incoming mail */
	EMAccountEditorService source;
	struct _GtkToggleButton *source_auto_check;
	struct _GtkSpinButton *source_auto_check_min;
	
	/* extra incoming config */
	GHashTable *extra_config;
	GSList *extra_widgets;

	/* outgoing mail */
	EMAccountEditorService transport;
	
	/* account management */
	struct _GtkToggleButton *default_account;
	
	/* special folders */
	struct _GtkButton *drafts_folder_button;
	struct _GtkButton *sent_folder_button;
	struct _GtkButton *restore_folders_button;

	/* Security */
	struct _GtkEntry *pgp_key;
	struct _GtkToggleButton *pgp_encrypt_to_self;
	struct _GtkToggleButton *pgp_always_sign;
	struct _GtkToggleButton *pgp_no_imip_sign;
	struct _GtkToggleButton *pgp_always_trust;

	struct _GtkToggleButton *smime_sign_default;
	struct _GtkEntry *smime_sign_key;
	struct _GtkButton *smime_sign_key_select;
	struct _GtkButton *smime_sign_key_clear;
	struct _GtkButton *smime_sign_select;
	struct _GtkToggleButton *smime_encrypt_default;
	struct _GtkToggleButton *smime_encrypt_to_self;
	struct _GtkEntry *smime_encrypt_key;
	struct _GtkButton *smime_encrypt_key_select;
	struct _GtkButton *smime_encrypt_key_clear;
} EMAccountEditorPrivate;


static GtkWidget *emae_setup_authtype(EMAccountEditor *emae, EMAccountEditorService *service);

static void save_service (EMAccountEditorService *gsvc, GHashTable *extra_conf, EAccountService *service);
static void service_changed (GtkEntry *entry, gpointer user_data);


static GtkVBoxClass *emae_parent;

static void
emae_init(GObject *o)
{
	EMAccountEditor *emae = (EMAccountEditor *)o;

	emae->priv = g_malloc0(sizeof(*emae->priv));

	emae->priv->source.emae = emae;
	emae->priv->transport.emae = emae;
}

static void
emae_finalise(GObject *o)
{
	EMAccountEditor *emae = (EMAccountEditor *)o;
	EMAccountEditorPrivate *p = emae->priv;

	if (p->xml)
		g_object_unref(p->xml);
	if (p->druidxml)
		g_object_unref(p->druidxml);

	g_list_free(p->source.authtypes);
	g_list_free(p->transport.authtypes);

	g_list_free(p->providers);
	g_slist_free(p->extra_widgets);
	g_free(p);

	g_object_unref(emae->account);

	((GObjectClass *)emae_parent)->finalize(o);
}

static void
emae_class_init(GObjectClass *klass)
{
	klass->finalize = emae_finalise;

	/*((GtkObjectClass *)klass)->destroy = emfb_destroy;*/
}

GType
em_account_editor_get_type(void)
{
	static GType type = 0;

	if (type == 0) {
		static const GTypeInfo info = {
			sizeof(EMAccountEditorClass),
			NULL, NULL,
			(GClassInitFunc)emae_class_init,
			NULL, NULL,
			sizeof(EMAccountEditor), 0,
			(GInstanceInitFunc)emae_init
		};
		emae_parent = g_type_class_ref(G_TYPE_OBJECT);
		type = g_type_register_static(G_TYPE_OBJECT, "EMAccountEditor", &info, 0);
	}

	return type;
}

EMAccountEditor *em_account_editor_new(EAccount *account, em_account_editor_t type)
{
	EMAccountEditor *emae = g_object_new(em_account_editor_get_type(), 0);

	em_account_editor_construct(emae, account, type);

	return emae;
}

/* ********************************************************************** */

static struct {
	char *label;
	char *value;
} ssl_options[] = {
	{ N_("Always"), "always" },
	{ N_("Whenever Possible"), "when-possible" },
	{ N_("Never"), "never" }
};

#define num_ssl_options (sizeof (ssl_options) / sizeof (ssl_options[0]))

static gboolean
is_email (const char *address)
{
	/* This is supposed to check if the address's domain could be
           an FQDN but alas, it's not worth the pain and suffering. */
	const char *at;
	
	at = strchr (address, '@');
	/* make sure we have an '@' and that it's not the first or last char */
	if (!at || at == address || *(at + 1) == '\0')
		return FALSE;
	
	return TRUE;
}

static GtkWidget *
get_focused_widget (GtkWidget *def, ...)
{
	GtkWidget *widget, *ret = NULL;
	va_list args;
	
	va_start (args, def);
	widget = va_arg (args, GtkWidget *);
	while (widget) {
		if (GTK_WIDGET_HAS_FOCUS (widget)) {
			ret = widget;
			break;
		}
		
		widget = va_arg (args, GtkWidget *);
	}
	va_end (args);
	
	if (ret)
		return ret;
	else
		return def;
}

#if 0
gboolean
em_account_editor_identity_complete (EMAccountEditor *gui, GtkWidget **incomplete)
{
	const char *text;
	
	text = gtk_entry_get_text (gui->full_name);
	if (!text || !*text) {
		if (incomplete)
			*incomplete = get_focused_widget (GTK_WIDGET (gui->full_name),
							  GTK_WIDGET (gui->email_address),
							  GTK_WIDGET (gui->reply_to),
							  NULL);
		return FALSE;
	}
	
	text = gtk_entry_get_text (gui->email_address);
	if (!text || !is_email (text)) {
		if (incomplete)
			*incomplete = get_focused_widget (GTK_WIDGET (gui->email_address),
							  GTK_WIDGET (gui->full_name),
							  GTK_WIDGET (gui->reply_to),
							  NULL);
		return FALSE;
	}
	
	/* make sure that if the reply-to field is filled in, that it is valid */
	text = gtk_entry_get_text (gui->reply_to);
	if (text && *text && !is_email (text)) {
		if (incomplete)
			*incomplete = get_focused_widget (GTK_WIDGET (gui->reply_to),
							  GTK_WIDGET (gui->email_address),
							  GTK_WIDGET (gui->full_name),
							  NULL);
		return FALSE;
	}
	
	return TRUE;
}
#endif

static void
auto_detected_foreach (gpointer key, gpointer value, gpointer user_data)
{
	g_free (key);
	g_free (value);
}

static void
check_button_state (GtkToggleButton *button, gpointer data)
{
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON (button)))
		gtk_widget_set_sensitive (GTK_WIDGET (data), TRUE);
	else
		gtk_widget_set_sensitive (GTK_WIDGET (data), FALSE);
}


static gboolean
populate_text_entry (GtkTextView *view, const char *filename)
{
	FILE *fd;
	char *filebuf;
	GtkTextIter iter;
	GtkTextBuffer *buffer;
	int count;

	g_return_val_if_fail (filename != NULL , FALSE);

	fd = fopen (filename, "r");
	
	if (!fd) {
		/* FIXME: Should never come here */
		return FALSE;
	}
	
	filebuf = g_malloc (1024);

	buffer =  gtk_text_buffer_new (NULL);
	gtk_text_buffer_get_start_iter (buffer, &iter);

	while (!feof (fd) && !ferror (fd)) {
		count = fread (filebuf, 1, sizeof (filebuf), fd);
		gtk_text_buffer_insert (buffer, &iter, filebuf, count);
	}
	gtk_text_view_set_buffer (GTK_TEXT_VIEW (view), 
					GTK_TEXT_BUFFER (buffer));
	fclose (fd);
	g_free (filebuf);
	return TRUE;
}

static gboolean
display_license (CamelProvider *prov)
{
	GladeXML *xml;
	GtkWidget *top_widget;
	GtkTextView *text_entry;
	GtkButton *button_yes, *button_no;
	GtkCheckButton *check_button;
	GtkResponseType response = GTK_RESPONSE_NONE;
	GtkLabel *top_label;
	char *label_text, *dialog_title;
	gboolean status;
	
	xml = glade_xml_new (EVOLUTION_GLADEDIR "/mail-dialogs.glade", "lic_dialog", NULL);
	
	top_widget = glade_xml_get_widget (xml, "lic_dialog");
	text_entry = GTK_TEXT_VIEW (glade_xml_get_widget (xml, "textview1"));
	if (!(status = populate_text_entry (GTK_TEXT_VIEW (text_entry), prov->license_file)))
		goto failed;
	
	gtk_text_view_set_editable (GTK_TEXT_VIEW (text_entry), FALSE);
	
	button_yes = GTK_BUTTON (glade_xml_get_widget (xml, "lic_yes_button"));
	gtk_widget_set_sensitive (GTK_WIDGET (button_yes), FALSE);
	
	button_no = GTK_BUTTON (glade_xml_get_widget (xml, "lic_no_button"));
	
	check_button = GTK_CHECK_BUTTON (glade_xml_get_widget (xml, "lic_checkbutton"));
	
	top_label = GTK_LABEL (glade_xml_get_widget (xml, "lic_top_label"));
	
	label_text = g_strdup_printf (_("\nPlease read carefully the license agreement\n" 
					"for %s displayed below\n" 
					"and tick the check box for accepting it\n"), prov->license);
	
	gtk_label_set_label (top_label, label_text);
	
	dialog_title = g_strdup_printf (_("%s License Agreement"), prov->license);
	
	gtk_window_set_title (GTK_WINDOW (top_widget), dialog_title);
	
	g_signal_connect (check_button, "toggled", G_CALLBACK (check_button_state), button_yes);
	
	response = gtk_dialog_run (GTK_DIALOG (top_widget));
	
	g_free (label_text);
	g_free (dialog_title);
	
 failed:
	gtk_widget_destroy (top_widget);
	g_object_unref (xml);
	
	return (response == GTK_RESPONSE_ACCEPT);
}

static gboolean
service_complete (EMAccountEditorService *service, GHashTable *extra_config, GtkWidget **incomplete)
{
	const CamelProvider *prov = service->provider;
	GtkWidget *path;
	const char *text;
	
	if (!prov)
		return TRUE;
	
	/* transports don't have a path */
	if (service->path)
		path = GTK_WIDGET (service->path);
	else
		path = NULL;
	
	if (CAMEL_PROVIDER_NEEDS (prov, CAMEL_URL_PART_HOST)) {
		text = gtk_entry_get_text (service->hostname);
		if (!text || !*text) {
			if (incomplete)
				*incomplete = get_focused_widget (GTK_WIDGET (service->hostname),
								  GTK_WIDGET (service->username),
								  path,
								  NULL);
			return FALSE;
		}
	}
	
	if (CAMEL_PROVIDER_NEEDS (prov, CAMEL_URL_PART_USER)) {
		text = gtk_entry_get_text (service->username);
		if (!text || !*text) {
			if (incomplete)
				*incomplete = get_focused_widget (GTK_WIDGET (service->username),
								  GTK_WIDGET (service->hostname),
								  path,
								  NULL);
			return FALSE;
		}
	}
	
	if (CAMEL_PROVIDER_NEEDS (prov, CAMEL_URL_PART_PATH)) {
		if (!path) {
			d(printf ("aagh, transports aren't supposed to have paths.\n"));
			return TRUE;
		}
		
		text = gtk_entry_get_text (service->path);
		if (!text || !*text) {
			if (incomplete)
				*incomplete = get_focused_widget (GTK_WIDGET (service->path),
								  GTK_WIDGET (service->hostname),
								  GTK_WIDGET (service->username),
								  NULL);
			return FALSE;
		}
	}
	
	return TRUE;
}

static gboolean
em_account_editor_check_for_license (CamelProvider *prov)
{
	GConfClient *gconf;
	gboolean accepted = TRUE, status;
	GSList * providers_list, *l, *n;
	char *provider;

	if (prov->flags & CAMEL_PROVIDER_HAS_LICENSE) {
		gconf = mail_config_get_gconf_client ();
		
		providers_list = gconf_client_get_list (gconf, "/apps/evolution/mail/licenses", GCONF_VALUE_STRING, NULL);
		
		for (l = providers_list, accepted = FALSE; l && !accepted; l = g_slist_next (l))
			accepted = (strcmp ((char *)l->data, prov->protocol) == 0);
		if (!accepted) {
			/* Since the license is not yet accepted, pop-up a 
			 * dialog to display the license agreement and check 
			 * if the user accepts it
			 */

			if ((accepted = display_license (prov)) == TRUE) {
				provider = g_strdup (prov->protocol);
				providers_list = g_slist_append (providers_list,
						 provider);
				status = gconf_client_set_list (gconf, 
						"/apps/evolution/mail/licenses",
						GCONF_VALUE_STRING,
						 providers_list, NULL);
			}
		}
		l = providers_list;
		while (l) {
			n = g_slist_next (l);
			g_free (l->data);
			g_slist_free_1 (l);
			l = n;
		}
	}
	return accepted;
}

#if 0
gboolean
em_account_editor_source_complete (EMAccountEditor *gui, GtkWidget **incomplete)
{
	return service_complete (&gui->source, gui->extra_config, incomplete);
}
#endif

void
em_account_editor_auto_detect_extra_conf (EMAccountEditor *emae)
{
	EMAccountEditorPrivate *gui = emae->priv;
	EMAccountEditorService *service = &gui->source;
	CamelProvider *prov = service->provider;
	GHashTable *auto_detected;
	GtkWidget *path;
	CamelURL *url;
	char *text;
	const char *tmp;

	if (!prov)
		return;
	
	/* transports don't have a path */
	if (service->path)
		path = GTK_WIDGET (service->path);
	else
		path = NULL;
	
	url = g_new0 (CamelURL, 1);
	camel_url_set_protocol (url, prov->protocol);
	
	if (CAMEL_PROVIDER_ALLOWS (prov, CAMEL_URL_PART_HOST)) {
		text = g_strdup (gtk_entry_get_text (service->hostname));
		if (*text) {
			char *port;
			
			port = strchr (text, ':');
			if (port) {
				*port++ = '\0';
				camel_url_set_port (url, atoi (port));
			}
			
			camel_url_set_host (url, text);
		}
		g_free (text);
	}
	
	if (CAMEL_PROVIDER_ALLOWS (prov, CAMEL_URL_PART_USER)) {
		text = g_strdup (gtk_entry_get_text (service->username));
		g_strstrip (text);
		camel_url_set_user (url, text);
		g_free (text);
	}
	
	if (path && CAMEL_PROVIDER_ALLOWS (prov, CAMEL_URL_PART_PATH)) {
		tmp = gtk_entry_get_text (service->path);
		if (tmp && *tmp)
			camel_url_set_path (url, tmp);
	}
	
	camel_provider_auto_detect (prov, url, &auto_detected, NULL);
	camel_url_free (url);
	
	if (auto_detected) {
		CamelProviderConfEntry *entries;
		GtkToggleButton *toggle;
		GtkSpinButton *spin;
		GtkEntry *entry;
		char *value;
		int i;
		
		entries = service->provider->extra_conf;
		
		for (i = 0; entries[i].type != CAMEL_PROVIDER_CONF_END; i++) {
			GtkWidget *enable_widget = NULL;

			if (!entries[i].name)
				continue;
			
			value = g_hash_table_lookup (auto_detected, entries[i].name);
			if (!value)
				continue;
			
			switch (entries[i].type) {
			case CAMEL_PROVIDER_CONF_CHECKBOX:
				toggle = g_hash_table_lookup (gui->extra_config, entries[i].name);
				gtk_toggle_button_set_active (toggle, atoi (value));
				enable_widget = (GtkWidget *)toggle;
				break;
				
			case CAMEL_PROVIDER_CONF_ENTRY:
				entry = g_hash_table_lookup (gui->extra_config, entries[i].name);
				if (value)
					gtk_entry_set_text (entry, value);
				enable_widget = (GtkWidget *)entry;
				break;
				
			case CAMEL_PROVIDER_CONF_CHECKSPIN:
			{
				gboolean enable;
				double val;
				char *name;
				
				toggle = g_hash_table_lookup (gui->extra_config, entries[i].name);
				name = g_strdup_printf ("%s_value", entries[i].name);
				spin = g_hash_table_lookup (gui->extra_config, name);
				g_free (name);
				
				enable = *value++ == 'y';
				gtk_toggle_button_set_active (toggle, enable);
				g_assert (*value == ':');
				val = strtod (++value, NULL);
				gtk_spin_button_set_value (spin, val);
				enable_widget = (GtkWidget *)spin;
			}
			break;
			default:
				break;
			}

			if (enable_widget)
				gtk_widget_set_sensitive(enable_widget, e_account_writable_option(emae->account, prov->protocol, entries[i].name));

		}
		
		g_hash_table_foreach (auto_detected, auto_detected_foreach, NULL);
		g_hash_table_destroy (auto_detected);
	}
}

gboolean
em_account_editor_transport_complete (EMAccountEditor *emae, GtkWidget **incomplete)
{
#if 0
	EMAccountEditorPrivate *gui = emae->priv;

	if (!gui->transport.provider) {
		if (incomplete)
			*incomplete = GTK_WIDGET (gui->transport.type);
		return FALSE;
	}

	/* If it's both source and transport, there's nothing extra to
	 * configure on the transport page.
	 */
	if (CAMEL_PROVIDER_IS_STORE_AND_TRANSPORT (gui->transport.provider)) {
		if (gui->transport.provider == gui->source.provider)
			return TRUE;
		if (incomplete)
			*incomplete = GTK_WIDGET (gui->transport.type);
		return FALSE;
	}
	
	if (!service_complete (&gui->transport, NULL, incomplete))
		return FALSE;
	
	/* FIXME? */
	if (gtk_toggle_button_get_active (gui->transport_needs_auth) &&
	    CAMEL_PROVIDER_ALLOWS (gui->transport.provider, CAMEL_URL_PART_USER)) {
		const char *text = gtk_entry_get_text (gui->transport.username);
		
		if (!text || !*text) {
			if (incomplete)
				*incomplete = get_focused_widget (GTK_WIDGET (gui->transport.username),
								  GTK_WIDGET (gui->transport.hostname),
								  NULL);
			return FALSE;
		}
	}
#endif	
	return TRUE;
}

gboolean
em_account_editor_management_complete (EMAccountEditor *emae, GtkWidget **incomplete)
{
#if 0
	EMAccountEditorPrivate *gui = emae->priv;
	const char *text;
	
	text = gtk_entry_get_text (gui->account_name);
	if (text && *text)
		return TRUE;
	
	if (incomplete)
		*incomplete = GTK_WIDGET (gui->account_name);
#endif	
	return FALSE;
}


#if 0
static void
transport_provider_set_available (EMAccountEditor *emae, CamelProvider *provider,
				  gboolean available)
{
	EMAccountEditorPrivate *gui = emae->priv;
	GtkWidget *menuitem;
	
	menuitem = g_object_get_data ((GObject *) gui->transport.type, provider->protocol);
	g_return_if_fail (menuitem != NULL);
	
	gtk_widget_set_sensitive (menuitem, available);
	
	if (available) {
		gpointer number = g_object_get_data ((GObject *) menuitem, "number");
		
		g_signal_emit_by_name (menuitem, "activate");
		gtk_option_menu_set_history (gui->transport.type, GPOINTER_TO_UINT (number));
	}
}

static void
source_type_changed (GtkWidget *widget, gpointer user_data)
{
	EMAccountEditor *emae = user_data;
	EMAccountEditorPrivate *gui = emae->priv;
	GtkWidget *file_entry, *label, *frame, *dwidget = NULL;
	CamelProvider *provider;
	gboolean writeable;
	gboolean license_accepted = TRUE;
	
	provider = g_object_get_data ((GObject *) widget, "provider");
	
	/* If the previously-selected provider has a linked transport,
	 * disable it.
	 */
	if (gui->source.provider &&
	    CAMEL_PROVIDER_IS_STORE_AND_TRANSPORT (gui->source.provider))
		transport_provider_set_available (gui, gui->source.provider, FALSE);
	
	gui->source.provider = provider;
	
	if (gui->source.provider) {
		writeable = e_account_writable_option (emae->account, gui->source.provider->protocol, "auth");
		gtk_widget_set_sensitive ((GtkWidget *) gui->source.authtype, writeable);
		gtk_widget_set_sensitive ((GtkWidget *) gui->source.check_supported, writeable);
		
		writeable = e_account_writable_option (emae->account, gui->source.provider->protocol, "use_ssl");
		gtk_widget_set_sensitive ((GtkWidget *) gui->source.use_ssl, writeable);
		
		writeable = e_account_writable (emae->account, E_ACCOUNT_SOURCE_SAVE_PASSWD);
		gtk_widget_set_sensitive ((GtkWidget *) gui->source.remember, writeable);
	}
	
	if (provider)
		gtk_label_set_text (gui->source.description, provider->description);
	else
		gtk_label_set_text (gui->source.description, "");
	
	if (gui->source.provider)	
		license_accepted = em_account_editor_check_for_license (gui->source.provider);

	frame = glade_xml_get_widget (gui->xml, "source_frame");
	if (provider && license_accepted) {
		gtk_widget_show (frame);
		
		/* hostname */
		label = glade_xml_get_widget (gui->xml, "source_host_label");
		
		if (CAMEL_PROVIDER_ALLOWS (provider, CAMEL_URL_PART_HOST)) {
			dwidget = GTK_WIDGET (gui->source.hostname);
			gtk_widget_show (GTK_WIDGET (gui->source.hostname));
			gtk_widget_show (label);
		} else {
			gtk_widget_hide (GTK_WIDGET (gui->source.hostname));
			gtk_widget_hide (label);
		}
		
		/* username */
		label = glade_xml_get_widget (gui->xml, "source_user_label");
		
		if (CAMEL_PROVIDER_ALLOWS (provider, CAMEL_URL_PART_USER)) {
			if (!dwidget)
				dwidget = GTK_WIDGET (gui->source.username);
			gtk_widget_show (GTK_WIDGET (gui->source.username));
			gtk_widget_show (label);
		} else {
			gtk_widget_hide (GTK_WIDGET (gui->source.username));
			gtk_widget_hide (label);
		}
		
		/* path */
		label = glade_xml_get_widget (gui->xml, "source_path_label");
		file_entry = glade_xml_get_widget (gui->xml, "source_path_entry");
		
		if (CAMEL_PROVIDER_ALLOWS (provider, CAMEL_URL_PART_PATH)) {
			if (!dwidget)
				dwidget = GTK_WIDGET (gui->source.path);
			
			gtk_widget_show (GTK_WIDGET (file_entry));
			gtk_widget_show (label);
		} else {
			gtk_entry_set_text (gui->source.path, "");
			gtk_widget_hide (GTK_WIDGET (file_entry));
			gtk_widget_hide (label);
		}
		
		/* ssl */
#ifdef HAVE_SSL
		gtk_widget_hide (gui->source.no_ssl);
		if (provider && provider->flags & CAMEL_PROVIDER_SUPPORTS_SSL) {
			gtk_widget_show (gui->source.ssl_frame);
			gtk_widget_show (gui->source.ssl_hbox);
		} else {
			gtk_widget_hide (gui->source.ssl_frame);
			gtk_widget_hide (gui->source.ssl_hbox);
		}
#else
		gtk_widget_hide (gui->source.ssl_hbox);
		gtk_widget_show (gui->source.no_ssl);
#endif
		
		/* auth */
		frame = glade_xml_get_widget (gui->xml, "source_auth_frame");
		if (provider && CAMEL_PROVIDER_ALLOWS (provider, CAMEL_URL_PART_AUTH)) {
			build_auth_menu (&gui->source, provider->authtypes, NULL, FALSE);
			gtk_widget_show (frame);
		} else
			gtk_widget_hide (frame);
	} else {
		gtk_widget_hide (frame);
		frame = glade_xml_get_widget (gui->xml, "source_auth_frame");
		gtk_widget_hide (frame);
	}
	
	g_signal_emit_by_name (gui->source.username, "changed");
	
	if (dwidget)
		gtk_widget_grab_focus (dwidget);
	
	em_account_editor_build_extra_conf (gui, gui && emae->account && emae->account->source ? emae->account->source->url : NULL);
	
	if (provider && CAMEL_PROVIDER_IS_STORE_AND_TRANSPORT (provider))
		transport_provider_set_available (gui, provider, TRUE);
}


static void
transport_needs_auth_toggled (GtkToggleButton *toggle, gpointer data)
{
	EMAccountEditor *emae = data;
	EMAccountEditorPrivate *gui = emae->priv;
	gboolean need = gtk_toggle_button_get_active (toggle);
	GtkWidget *widget;
	
	widget = glade_xml_get_widget (gui->xml, "transport_auth_frame");
	gtk_widget_set_sensitive (widget, need);
	if (need)
		service_changed (NULL, &gui->transport);
}

static void
transport_type_changed (GtkWidget *widget, gpointer user_data)
{
	EMAccountEditor *emae = user_data;
	EMAccountEditorPrivate *gui = emae->priv;
	CamelProvider *provider;
	GtkWidget *label, *frame;
	gboolean writeable;
	
	provider = g_object_get_data ((GObject *) widget, "provider");
	gui->transport.provider = provider;
	
	if (gui->transport.provider) {
		writeable = e_account_writable_option (emae->account, gui->transport.provider->protocol, "auth");
		gtk_widget_set_sensitive ((GtkWidget *) gui->transport.authtype, writeable);
		gtk_widget_set_sensitive ((GtkWidget *) gui->transport.check_supported, writeable);
		
		writeable = e_account_writable_option (emae->account, gui->transport.provider->protocol, "use_ssl");
		gtk_widget_set_sensitive ((GtkWidget *) gui->transport.use_ssl, writeable);
		
		writeable = e_account_writable (emae->account, E_ACCOUNT_TRANSPORT_SAVE_PASSWD);
		gtk_widget_set_sensitive ((GtkWidget *) gui->transport.remember, writeable);
	}
	
	/* description */
	gtk_label_set_text (gui->transport.description, provider->description);
	
	frame = glade_xml_get_widget (gui->xml, "transport_frame");
	if (!CAMEL_PROVIDER_IS_STORE_AND_TRANSPORT (provider) &&
	    (CAMEL_PROVIDER_ALLOWS (provider, CAMEL_URL_PART_HOST) ||
	     (CAMEL_PROVIDER_ALLOWS (provider, CAMEL_URL_PART_AUTH) &&
	      !CAMEL_PROVIDER_NEEDS (provider, CAMEL_URL_PART_AUTH)))) {
		gtk_widget_show (frame);
		
		label = glade_xml_get_widget (gui->xml, "transport_host_label");
		if (CAMEL_PROVIDER_ALLOWS (provider, CAMEL_URL_PART_HOST)) {
			gtk_widget_show (GTK_WIDGET (gui->transport.hostname));
			gtk_widget_show (label);
		} else {
			gtk_widget_hide (GTK_WIDGET (gui->transport.hostname));
			gtk_widget_hide (label);
		}
		
		/* ssl */
#ifdef HAVE_SSL
		gtk_widget_hide (gui->transport.no_ssl);
		if (provider && provider->flags & CAMEL_PROVIDER_SUPPORTS_SSL) {
			gtk_widget_show (gui->transport.ssl_frame);
			gtk_widget_show (gui->transport.ssl_hbox);
		} else {
			gtk_widget_hide (gui->transport.ssl_frame);
			gtk_widget_hide (gui->transport.ssl_hbox);
		}
#else
		gtk_widget_hide (gui->transport.ssl_hbox);
		gtk_widget_show (gui->transport.no_ssl);
#endif
		
		/* auth */
		if (CAMEL_PROVIDER_ALLOWS (provider, CAMEL_URL_PART_AUTH) &&
		    !CAMEL_PROVIDER_NEEDS (provider, CAMEL_URL_PART_AUTH))
			gtk_widget_show (GTK_WIDGET (gui->transport_needs_auth));
		else
			gtk_widget_hide (GTK_WIDGET (gui->transport_needs_auth));
	} else
		gtk_widget_hide (frame);
	
	frame = glade_xml_get_widget (gui->xml, "transport_auth_frame");
	if (!CAMEL_PROVIDER_IS_STORE_AND_TRANSPORT (provider) &&
	    CAMEL_PROVIDER_ALLOWS (provider, CAMEL_URL_PART_AUTH)) {
		gtk_widget_show (frame);
		
		label = glade_xml_get_widget (gui->xml, "transport_user_label");
		if (CAMEL_PROVIDER_ALLOWS (provider, CAMEL_URL_PART_USER)) {
			gtk_widget_show (GTK_WIDGET (gui->transport.username));
			gtk_widget_show (label);
		} else {
			gtk_widget_hide (GTK_WIDGET (gui->transport.username));
			gtk_widget_hide (label);
		}
		
		build_auth_menu (&gui->transport, provider->authtypes, NULL, FALSE);
		transport_needs_auth_toggled (gui->transport_needs_auth, gui);
	} else
		gtk_widget_hide (frame);
	
	g_signal_emit_by_name (gui->transport.hostname, "changed");
}

static void
service_changed (GtkEntry *entry, gpointer user_data)
{
	EMAccountEditorService *service = user_data;
	
	gtk_widget_set_sensitive (GTK_WIDGET (service->check_supported),
				  service_complete (service, NULL, NULL));
}
#endif

static void
toggle_sensitivity (GtkToggleButton *toggle, GtkWidget *widget)
{
	gtk_widget_set_sensitive (widget, gtk_toggle_button_get_active (toggle));
}

/* Returns true if the widget is enabled */
static gboolean
setup_toggle (GtkWidget *widget, const char *depname, EMAccountEditor *emae)
{
	EMAccountEditorPrivate *gui = emae->priv;
	GtkToggleButton *toggle;
	
	if (!strcmp (depname, "UNIMPLEMENTED")) {
		gtk_widget_set_sensitive (widget, FALSE);
		return FALSE;
	}
	
	toggle = g_hash_table_lookup (gui->extra_config, depname);
	g_signal_connect (toggle, "toggled", G_CALLBACK (toggle_sensitivity), widget);
	toggle_sensitivity (toggle, widget);

	return gtk_toggle_button_get_active(toggle);
}

void
em_account_editor_build_extra_conf (EMAccountEditor *emae, const char *url_string)
{
	EMAccountEditorPrivate *gui = emae->priv;
	CamelURL *url;
	GtkWidget *mailcheck_frame, *mailcheck_hbox;
	GtkWidget *hostname_label, *username_label, *path_label;
	GtkWidget *hostname, *username, *path;
	GtkTable *main_table, *cur_table;
	CamelProviderConfEntry *entries;
	GList *child, *next;
	char *name;
	int i, rows;
	
	if (url_string)
		url = camel_url_new (url_string, NULL);
	else
		url = NULL;
	
	hostname_label = glade_xml_get_widget (gui->xml, "source_host_label");
	gtk_label_set_text_with_mnemonic (GTK_LABEL (hostname_label), _("_Host:"));
	hostname = glade_xml_get_widget (gui->xml, "source_host");
	
	username_label = glade_xml_get_widget (gui->xml, "source_user_label");
	gtk_label_set_text_with_mnemonic (GTK_LABEL (username_label), _("User_name:"));
	username = glade_xml_get_widget (gui->xml, "source_user");
	
	path_label = glade_xml_get_widget (gui->xml, "source_path_label");
	gtk_label_set_text_with_mnemonic (GTK_LABEL (path_label), _("_Path:"));
	path = glade_xml_get_widget (gui->xml, "source_path");
	
	/* Remove the contents of the extra_table except for the
	 * mailcheck_frame.
	 */
	main_table = (GtkTable *) glade_xml_get_widget (gui->xml, "extra_table");
	gtk_container_set_border_width ((GtkContainer *) main_table, 12);
	gtk_table_set_row_spacings (main_table, 6);
	gtk_table_set_col_spacings (main_table, 8);
	mailcheck_frame = glade_xml_get_widget (gui->xml, "extra_mailcheck_frame");
	child = gtk_container_get_children (GTK_CONTAINER (main_table));
	while (child != NULL) {
		next = child->next;
		if (child->data != (gpointer) mailcheck_frame)
			gtk_container_remove (GTK_CONTAINER (main_table), child->data);
		g_list_free_1 (child);
		child = next;
	}
	
	gtk_table_resize (main_table, 1, 2);
	
	/* Remove any additional mailcheck items. */
	cur_table = (GtkTable *) glade_xml_get_widget (gui->xml, "extra_mailcheck_table");
	gtk_container_set_border_width ((GtkContainer *) cur_table, 12);
	gtk_table_set_row_spacings (cur_table, 6);
	gtk_table_set_col_spacings (cur_table, 8);
	mailcheck_hbox = glade_xml_get_widget (gui->xml, "extra_mailcheck_hbox");
	child = gtk_container_get_children (GTK_CONTAINER (cur_table));
	while (child != NULL) {
		next = child->next;
		if (child->data != (gpointer) mailcheck_hbox)
			gtk_container_remove (GTK_CONTAINER (cur_table), child->data);
		g_list_free_1 (child);
		child = next;
	}
	
	gtk_table_resize (cur_table, 1, 2);

	if (!gui->source.provider) {
		gtk_widget_set_sensitive (GTK_WIDGET (main_table), FALSE);
		if (url)
			camel_url_free (url);
		return;
	} else
		gtk_widget_set_sensitive(GTK_WIDGET(main_table), e_account_writable(emae->account, E_ACCOUNT_SOURCE_URL));
	
	/* Set up our hash table. */
	if (gui->extra_config)
		g_hash_table_destroy (gui->extra_config);
	gui->extra_config = g_hash_table_new (g_str_hash, g_str_equal);
	
	entries = gui->source.provider->extra_conf;
	if (!entries)
		goto done;
	
	cur_table = main_table;
	rows = main_table->nrows;
	for (i = 0; ; i++) {
		GtkWidget *enable_widget = NULL;
		int enabled = TRUE;
		
		switch (entries[i].type) {
		case CAMEL_PROVIDER_CONF_SECTION_START:
		{
			GtkWidget *frame, *label;
			char *markup;
			
			if (entries[i].name && !strcmp (entries[i].name, "mailcheck")) {
				cur_table = (GtkTable *) glade_xml_get_widget (gui->xml, "extra_mailcheck_table");
				rows = cur_table->nrows;
				break;
			}
			
			markup = g_strdup_printf ("<span weight=\"bold\">%s</span>", entries[i].text);
			label = gtk_label_new (NULL);
			gtk_label_set_markup ((GtkLabel *) label, markup);
			gtk_label_set_justify ((GtkLabel *) label, GTK_JUSTIFY_LEFT);
			gtk_label_set_use_markup ((GtkLabel *) label, TRUE);
			gtk_misc_set_alignment ((GtkMisc *) label, 0.0, 0.5);
			gtk_widget_show (label);
			g_free (markup);
			
			cur_table = (GtkTable *) gtk_table_new (0, 2, FALSE);
			gtk_container_set_border_width ((GtkContainer *) cur_table, 12);
			gtk_table_set_row_spacings (cur_table, 6);
			gtk_table_set_col_spacings (cur_table, 8);
			gtk_widget_show ((GtkWidget *) cur_table);
			
			frame = gtk_vbox_new (FALSE, 0);
			gtk_box_pack_start ((GtkBox *) frame, label, FALSE, FALSE, 0);
			gtk_box_pack_start ((GtkBox *) frame, (GtkWidget *) cur_table, FALSE, FALSE, 0);
			gtk_widget_show (frame);
			
			gtk_table_attach (main_table, frame, 0, 2,
					  rows, rows + 1,
					  GTK_EXPAND | GTK_FILL, 0, 0, 0);
			
			rows = 0;
			
			break;
		}
		case CAMEL_PROVIDER_CONF_SECTION_END:
			cur_table = main_table;
			rows = main_table->nrows;
			break;
			
		case CAMEL_PROVIDER_CONF_LABEL:
			if (entries[i].name && entries[i].text) {
				GtkWidget *label;
				
				if (!strcmp (entries[i].name, "username")) {
					gtk_label_set_text_with_mnemonic (GTK_LABEL (username_label), entries[i].text);
					enable_widget = username_label;
				} else if (!strcmp (entries[i].name, "hostname")) {
					gtk_label_set_text_with_mnemonic (GTK_LABEL (hostname_label), entries[i].text);
					enable_widget = hostname_label;
				} else if (!strcmp (entries[i].name, "path")) {
					gtk_label_set_text_with_mnemonic (GTK_LABEL (path_label), entries[i].text);
					enable_widget = path_label;
				} else {
					/* make a new label */
					label = gtk_label_new (entries[i].text);
					gtk_table_resize (cur_table, cur_table->nrows + 1, 2);
					gtk_table_attach (cur_table, label, 0, 2, rows, rows + 1,
							  GTK_EXPAND | GTK_FILL, 0, 0, 0);
					rows++;
					enable_widget = label;
				}
			}
			break;
			
		case CAMEL_PROVIDER_CONF_CHECKBOX:
		{
			GtkWidget *checkbox;
			gboolean active;
			
			checkbox = gtk_check_button_new_with_label (entries[i].text);
			if (url)
				active = camel_url_get_param (url, entries[i].name) != NULL;
			else
				active = atoi (entries[i].value);
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (checkbox), active);
			
			gtk_table_attach (cur_table, checkbox, 0, 2, rows, rows + 1,
					  GTK_EXPAND | GTK_FILL, 0, 0, 0);
			rows++;
			g_hash_table_insert (gui->extra_config, entries[i].name, checkbox);
			if (entries[i].depname)
				enabled = setup_toggle(checkbox, entries[i].depname, emae);

			enable_widget = checkbox;
			break;
		}
		
		case CAMEL_PROVIDER_CONF_ENTRY:
		{
			GtkWidget *label, *entry;
			const char *text;
			
			if (!strcmp (entries[i].name, "username")) {
				gtk_label_set_text_with_mnemonic (GTK_LABEL (username_label), entries[i].text);
				label = username_label;
				entry = username;
			} else if (!strcmp (entries[i].name, "hostname")) {
				gtk_label_set_text_with_mnemonic (GTK_LABEL (hostname_label), entries[i].text);
				label = hostname_label;
				entry = hostname;
			} else if (!strcmp (entries[i].name, "path")) {
				gtk_label_set_text_with_mnemonic (GTK_LABEL (path_label), entries[i].text);
				label = path_label;
				entry = path;
			} else {
				/* make a new text entry with label */
				label = gtk_label_new (entries[i].text);
				gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.0);
				entry = gtk_entry_new ();
				
				gtk_table_attach (cur_table, label, 0, 1, rows, rows + 1,
						  GTK_FILL, 0, 0, 0);
				gtk_table_attach (cur_table, entry, 1, 2, rows, rows + 1,
						  GTK_EXPAND | GTK_FILL, 0, 0, 0);
				rows++;
			}
			
			if (url)
				text = camel_url_get_param (url, entries[i].name);
			else
				text = entries[i].value;
			
			if (text)
				gtk_entry_set_text (GTK_ENTRY (entry), text);
			
			if (entries[i].depname) {
				setup_toggle (entry, entries[i].depname, emae);
				enabled = setup_toggle (label, entries[i].depname, emae);
			}
			
			g_hash_table_insert (gui->extra_config, entries[i].name, entry);
			
			enable_widget = entry;
			break;
		}
		
		case CAMEL_PROVIDER_CONF_CHECKSPIN:
		{
			GtkWidget *hbox, *checkbox, *spin, *label;
			GtkObject *adj;
			char *data, *pre, *post, *p;
			double min, def, max;
			gboolean enable;
			
			/* FIXME: this is pretty fucked... */
			data = entries[i].text;
			p = strstr (data, "%s");
			g_return_if_fail (p != NULL);
			
			pre = g_strndup (data, p - data);
			post = p + 2;
			
			data = entries[i].value;
			enable = *data++ == 'y';
			g_return_if_fail (*data == ':');
			min = strtod (data + 1, &data);
			g_return_if_fail (*data == ':');
			def = strtod (data + 1, &data);
			g_return_if_fail (*data == ':');
			max = strtod (data + 1, NULL);
			
			if (url) {
				const char *val;
				
				val = camel_url_get_param (url, entries[i].name);
				if (!val)
					enable = FALSE;
				else {
					enable = TRUE;
					def = atof (val);
				}
			}
			
			hbox = gtk_hbox_new (FALSE, 0);
			checkbox = gtk_check_button_new_with_label (pre);
			g_free (pre);
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (checkbox), enable);
			adj = gtk_adjustment_new (def, min, max, 1, 1, 1);
			spin = gtk_spin_button_new (GTK_ADJUSTMENT (adj), 1, 0);
			label = gtk_label_new (post);
			
			gtk_box_pack_start (GTK_BOX (hbox), checkbox, FALSE, TRUE, 0);
			gtk_box_pack_start (GTK_BOX (hbox), spin, FALSE, FALSE, 0);
			gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, TRUE, 4);
			
			gtk_table_attach (cur_table, hbox, 0, 2, rows, rows + 1,
					  GTK_EXPAND | GTK_FILL, 0, 0, 0);
			rows++;
			g_hash_table_insert (gui->extra_config, entries[i].name, checkbox);
			name = g_strdup_printf ("%s_value", entries[i].name);
			g_hash_table_insert (gui->extra_config, name, spin);
			if (entries[i].depname) {
				setup_toggle (checkbox, entries[i].depname, emae);
				setup_toggle (spin, entries[i].depname, emae);
				enabled = setup_toggle (label, entries[i].depname, emae);
			}
			
			enable_widget = hbox;
			break;
		}

		case CAMEL_PROVIDER_CONF_HIDDEN:
			break;

		case CAMEL_PROVIDER_CONF_END:
			goto done;
		}
		
		if (enabled && enable_widget)
			gtk_widget_set_sensitive(enable_widget, e_account_writable_option(emae->account, gui->source.provider->protocol, entries[i].name));
	}
	
 done:
	gtk_widget_show_all (GTK_WIDGET (main_table));
	if (url)
		camel_url_free (url);
}

static void
extract_values (EMAccountEditorService *source, GHashTable *extra_config, CamelURL *url)
{
	CamelProviderConfEntry *entries;
	GtkToggleButton *toggle;
	GtkEntry *entry;
	GtkSpinButton *spin;
	char *name;
	int i;
	
	if (!source->provider || !source->provider->extra_conf)
		return;
	entries = source->provider->extra_conf;
	
	for (i = 0; ; i++) {
		if (entries[i].depname) {
			toggle = g_hash_table_lookup (extra_config, entries[i].depname);
			if (!toggle || !gtk_toggle_button_get_active (toggle))
				continue;
		}
		
		switch (entries[i].type) {
		case CAMEL_PROVIDER_CONF_CHECKBOX:
			toggle = g_hash_table_lookup (extra_config, entries[i].name);
			if (gtk_toggle_button_get_active (toggle))
				camel_url_set_param (url, entries[i].name, "");
			break;
			
		case CAMEL_PROVIDER_CONF_ENTRY:
			if (strcmp (entries[i].name, "username") == 0
			    || strcmp (entries[i].name, "hostname") == 0
			    || strcmp (entries[i].name, "path") == 0) {
				break;
			}
			entry = g_hash_table_lookup (extra_config, entries[i].name);
			camel_url_set_param (url, entries[i].name, gtk_entry_get_text (entry));
			break;
			
		case CAMEL_PROVIDER_CONF_CHECKSPIN:
			toggle = g_hash_table_lookup (extra_config, entries[i].name);
			if (!gtk_toggle_button_get_active (toggle))
				break;
			name = g_strdup_printf ("%s_value", entries[i].name);
			spin = g_hash_table_lookup (extra_config, name);
			g_free (name);
			name = g_strdup_printf ("%d", gtk_spin_button_get_value_as_int (spin));
			camel_url_set_param (url, entries[i].name, name);
			g_free (name);
			break;

		case CAMEL_PROVIDER_CONF_HIDDEN:
			if (entries[i].value)
				camel_url_set_param (url, entries[i].name, entries[i].value);
			break;

		case CAMEL_PROVIDER_CONF_END:
			return;
			
		default:
			break;
		}
	}
}

static void
folder_selected (EMFolderSelectionButton *button, gpointer user_data)
{
	char **folder_name = user_data;
	
	g_free (*folder_name);
	*folder_name = g_strdup(em_folder_selection_button_get_selection(button));
}

static void
default_folders_clicked (GtkButton *button, gpointer user_data)
{
	EMAccountEditor *emae = user_data;
	const char *uri;
	
	uri = mail_component_get_folder_uri(NULL, MAIL_COMPONENT_FOLDER_DRAFTS);
	em_folder_selection_button_set_selection((EMFolderSelectionButton *)emae->priv->drafts_folder_button, uri);
	e_account_set_string(emae->account, E_ACCOUNT_DRAFTS_FOLDER_URI, uri);

	uri = mail_component_get_folder_uri(NULL, MAIL_COMPONENT_FOLDER_SENT);
	em_folder_selection_button_set_selection((EMFolderSelectionButton *)emae->priv->sent_folder_button, uri);
	e_account_set_string(emae->account, E_ACCOUNT_SENT_FOLDER_URI, uri);
}

/* custom widget factories */
GtkWidget *em_account_editor_folder_selector_button_new (char *widget_name, char *string1, char *string2, int int1, int int2);

GtkWidget *
em_account_editor_folder_selector_button_new (char *widget_name, char *string1, char *string2, int int1, int int2)
{
	return (GtkWidget *)em_folder_selection_button_new(_("Select Folder"), NULL);
}

GtkWidget *em_account_editor_provider_selector_new(char *widget_name, char *string1, char *string2, int int1, int int2);

GtkWidget *
em_account_editor_provider_selector_new(char *widget_name, char *string1, char *string2, int int1, int int2)
{
	return (GtkWidget *)gtk_combo_box_new();
}

GtkWidget *em_account_editor_dropdown_new(char *widget_name, char *string1, char *string2, int int1, int int2);

GtkWidget *
em_account_editor_dropdown_new(char *widget_name, char *string1, char *string2, int int1, int int2)
{
	return (GtkWidget *)gtk_combo_box_new();
}

GtkWidget *em_account_editor_ssl_selector_new(char *widget_name, char *string1, char *string2, int int1, int int2);

GtkWidget *
em_account_editor_ssl_selector_new(char *widget_name, char *string1, char *string2, int int1, int int2)
{
	GtkComboBox *dropdown = (GtkComboBox *)gtk_combo_box_new();
	GtkCellRenderer *cell = gtk_cell_renderer_text_new();
	GtkListStore *store;
	int i;
	GtkTreeIter iter;

	gtk_widget_show((GtkWidget *)dropdown);

	store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_POINTER);

	for (i=0;i<num_ssl_options;i++) {
		gtk_list_store_append(store, &iter);
		gtk_list_store_set(store, &iter, 0, _(ssl_options[i].label), 1, ssl_options[i].value, -1);
	}

	gtk_cell_layout_pack_start((GtkCellLayout *)dropdown, cell, TRUE);
	gtk_cell_layout_set_attributes((GtkCellLayout *)dropdown, cell, "text", 0, NULL);

	gtk_combo_box_set_model(dropdown, (GtkTreeModel *)store);

	return (GtkWidget *)dropdown;
}

static gboolean
setup_service (EMAccountEditor *emae, EMAccountEditorService *gsvc, EAccountService *service)
{
#if 0
	EMAccountEditorPrivate *gui = emae->priv;
	CamelURL *url = camel_url_new (service->url, NULL);
	gboolean has_auth = FALSE;
	
	if (url == NULL || gsvc->provider == NULL)
		return FALSE;
	
	if (url->user && CAMEL_PROVIDER_ALLOWS (gsvc->provider, CAMEL_URL_PART_USER))
		gtk_entry_set_text (gsvc->username, url->user);
	
	if (url->host && CAMEL_PROVIDER_ALLOWS (gsvc->provider, CAMEL_URL_PART_HOST)) {
		char *hostname;
		
		if (url->port)
			hostname = g_strdup_printf ("%s:%d", url->host, url->port);
		else
			hostname = g_strdup (url->host);
		
		gtk_entry_set_text (gsvc->hostname, hostname);
		g_free (hostname);
	}
	
	if (url->path && CAMEL_PROVIDER_ALLOWS (gsvc->provider, CAMEL_URL_PART_PATH))
		gtk_entry_set_text (gsvc->path, url->path);
	
	if (gsvc->provider->flags & CAMEL_PROVIDER_SUPPORTS_SSL) {
		GList *children, *item;
		const char *use_ssl;
		int i;
		
		use_ssl = camel_url_get_param (url, "use_ssl");
		if (!use_ssl)
			use_ssl = "never";
		else if (!*use_ssl)  /* old config code just used an empty string as the value */
			use_ssl = "always";
		
		children = gtk_container_get_children(GTK_CONTAINER (gtk_option_menu_get_menu (gsvc->use_ssl)));
		for (item = children, i = 0; item; item = item->next, i++) {
			if (!strcmp (use_ssl, ssl_options[i].value)) {
				gtk_option_menu_set_history (gsvc->use_ssl, i);
				g_signal_emit_by_name (item->data, "activate", gsvc);
				break;
			}
		}
	}
	
	if (url->authmech && CAMEL_PROVIDER_ALLOWS (gsvc->provider, CAMEL_URL_PART_AUTH)) {
		GList *children, *item;
		CamelServiceAuthType *authtype;
		int i;
		
		children = gtk_container_get_children(GTK_CONTAINER (gtk_option_menu_get_menu (gsvc->authtype)));
		for (item = children, i = 0; item; item = item->next, i++) {
			authtype = g_object_get_data ((GObject *) item->data, "authtype");
			if (!authtype)
				continue;
			if (!strcmp (authtype->authproto, url->authmech)) {
				gtk_option_menu_set_history (gsvc->authtype, i);
				g_signal_emit_by_name (item->data, "activate");
				break;
			}
		}
		g_list_free (children);
		
		has_auth = TRUE;
	}
	camel_url_free (url);
	
	gtk_toggle_button_set_active (gsvc->remember, service->save_passwd);

	gtk_widget_set_sensitive((GtkWidget *)gsvc->authtype, e_account_writable_option(emae->account, gsvc->provider->protocol, "auth"));
	gtk_widget_set_sensitive((GtkWidget *)gsvc->use_ssl, e_account_writable_option(emae->account, gsvc->provider->protocol, "use_ssl"));
	
	return has_auth;
#endif
	return TRUE;
}

static gint
provider_compare (const CamelProvider *p1, const CamelProvider *p2)
{
	/* sort providers based on "location" (ie. local or remote) */
	if (p1->flags & CAMEL_PROVIDER_IS_REMOTE) {
		if (p2->flags & CAMEL_PROVIDER_IS_REMOTE)
			return 0;
		return -1;
	} else {
		if (p2->flags & CAMEL_PROVIDER_IS_REMOTE)
			return 1;
		return 0;
	}
}

static void
sig_activate (GtkWidget *item, EMAccountEditor *emae)
{
	EMAccountEditorPrivate *gui = emae->priv;
	ESignature *sig;
	
	sig = g_object_get_data ((GObject *) item, "sig");
	
	gui->sig_uid = sig ? sig->uid : NULL;
}

static void
signature_added (ESignatureList *signatures, ESignature *sig, EMAccountEditor *emae)
{
	EMAccountEditorPrivate *gui = emae->priv;
	GtkWidget *menu, *item;
	
	menu = gtk_option_menu_get_menu (gui->sig_menu);
	if (sig->autogen)
		item = gtk_menu_item_new_with_label (_("Autogenerated"));
	else
		item = gtk_menu_item_new_with_label (sig->name);
	g_object_set_data ((GObject *) item, "sig", sig);
	g_signal_connect (item, "activate", G_CALLBACK (sig_activate), emae);
	gtk_widget_show (item);
	
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	
	gtk_option_menu_set_history (gui->sig_menu, g_list_length (GTK_MENU_SHELL (menu)->children));
}

static void
signature_removed (ESignatureList *signatures, ESignature *sig, EMAccountEditor *emae)
{
	EMAccountEditorPrivate *gui = emae->priv;
	GtkWidget *menu;
	ESignature *cur;
	GList *items;
	
	if (gui->sig_uid == sig->uid)
		gui->sig_uid = NULL;
	
	menu = gtk_option_menu_get_menu (gui->sig_menu);
	items = GTK_MENU_SHELL (menu)->children;
	while (items != NULL) {
		cur = g_object_get_data (items->data, "sig");
		if (cur == sig) {
			gtk_widget_destroy (items->data);
			break;
		}
		items = items->next;
	}
}

static void
menu_item_set_label (GtkMenuItem *item, const char *label)
{
	GtkWidget *widget;
	
	widget = gtk_bin_get_child ((GtkBin *) item);
	if (GTK_IS_LABEL (widget))
		gtk_label_set_text ((GtkLabel *) widget, label);
}

static void
signature_changed (ESignatureList *signatures, ESignature *sig, EMAccountEditor *emae)
{
	EMAccountEditorPrivate *gui = emae->priv;
	GtkWidget *menu;
	ESignature *cur;
	GList *items;
	
	menu = gtk_option_menu_get_menu (gui->sig_menu);
	items = GTK_MENU_SHELL (menu)->children;
	while (items != NULL) {
		cur = g_object_get_data (items->data, "sig");
		if (cur == sig) {
			menu_item_set_label (items->data, sig->name);
			break;
		}
		items = items->next;
	}
}

static void
clear_menu (GtkWidget *menu)
{
	while (GTK_MENU_SHELL (menu)->children)
		gtk_container_remove (GTK_CONTAINER (menu), GTK_MENU_SHELL (menu)->children->data);
}

static void
sig_fill_menu (EMAccountEditor *emae)
{
	EMAccountEditorPrivate *gui = emae->priv;
	ESignatureList *signatures;
	GtkWidget *menu, *item;
	EIterator *it;
	
	menu = gtk_option_menu_get_menu (gui->sig_menu);
	clear_menu (menu);
	
	item = gtk_menu_item_new_with_label (_("None"));
	gtk_widget_show (item);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	
	signatures = mail_config_get_signatures ();
	it = e_list_get_iterator ((EList *) signatures);
	
	while (e_iterator_is_valid (it)) {
		ESignature *sig;
		
		sig = (ESignature *) e_iterator_get (it);
		signature_added (signatures, sig, emae);
		e_iterator_next (it);
	}
	
	g_object_unref (it);
	
	gui->sig_added_id = g_signal_connect (signatures, "signature-added", G_CALLBACK (signature_added), emae);
	gui->sig_removed_id = g_signal_connect (signatures, "signature-removed", G_CALLBACK (signature_removed), emae);
	gui->sig_changed_id = g_signal_connect (signatures, "signature-changed", G_CALLBACK (signature_changed), emae);
	
	gtk_option_menu_set_history (gui->sig_menu, 0);
}

static void
sig_switch_to_list (GtkWidget *w, EMAccountEditor *emae)
{
#if 0
	EMAccountEditorPrivate *gui = emae->priv;

	/* what the fuck is this for?  no changelog comments */
	gtk_window_set_transient_for (GTK_WINDOW (gtk_widget_get_toplevel (w)), NULL);
	gdk_window_raise (GTK_WIDGET (gui->dialog)->window);
	gtk_notebook_set_current_page (GTK_NOTEBOOK (glade_xml_get_widget (gui->dialog->gui, "notebook")), 3);
#endif
}

static void
sig_add_new_signature (GtkWidget *w, EMAccountEditor *emae)
{
	GConfClient *gconf;
	gboolean send_html;
	GtkWidget *parent;

	/* wtf??  is this how it knows if it has to set it up or wtf?
	if (!gui->dialog)
		return;
	*/
	
	sig_switch_to_list (w, emae);
	
	gconf = mail_config_get_gconf_client ();
	send_html = gconf_client_get_bool (gconf, "/apps/evolution/mail/composer/send_html", NULL);
	
	parent = gtk_widget_get_toplevel (w);
	parent = GTK_WIDGET_TOPLEVEL (parent) ? parent : NULL;
	
	em_composer_prefs_new_signature ((GtkWindow *) parent, send_html);
}

static void
select_account_signature (EMAccountEditor *emae)
{
	EMAccountEditorPrivate *gui = emae->priv;
	ESignature *sig, *cur;
	GtkWidget *menu;
	GList *items;
	int i = 0;
	
	if (!emae->account->id->sig_uid || !(sig = mail_config_get_signature_by_uid (emae->account->id->sig_uid)))
		return;
	
	menu = gtk_option_menu_get_menu (gui->sig_menu);
	items = GTK_MENU_SHELL (menu)->children;
	while (items != NULL) {
		cur = g_object_get_data (items->data, "sig");
		if (cur == sig) {
			gtk_option_menu_set_history (gui->sig_menu, i);
			gtk_menu_item_activate (items->data);
			break;
		}
		items = items->next;
		i++;
	}
}

static void
prepare_signatures (EMAccountEditor *emae)
{
	EMAccountEditorPrivate *gui = emae->priv;
	GtkWidget *button;
	
	gui->sig_menu = (GtkOptionMenu *) glade_xml_get_widget (gui->xml, "sigOption");
	sig_fill_menu (emae);
	
	button = glade_xml_get_widget (gui->xml, "sigAddNew");
	g_signal_connect (button, "clicked", G_CALLBACK (sig_add_new_signature), emae);

	if (!emae->do_signature) {
		gtk_widget_hide (glade_xml_get_widget (gui->xml, "sigLabel"));
		gtk_widget_hide (glade_xml_get_widget (gui->xml, "sigOption"));
		gtk_widget_hide (glade_xml_get_widget (gui->xml, "sigAddNew"));
	}
	
	select_account_signature (emae);
}

static void
emae_build_extra_conf(EMAccountEditor *emae, EMAccountEditorService *service)
{
	EMAccountEditorPrivate *gui = emae->priv;
	CamelURL *url;
	GtkWidget *mailcheck_frame, *mailcheck_hbox;
	GtkWidget *hostname_label, *username_label, *path_label;
	GtkWidget *hostname, *username, *path;
	GtkTable *main_table, *cur_table;
	CamelProviderConfEntry *entries;
	GList *child, *next;
	char *name;
	int i, rows;

	hostname_label = glade_xml_get_widget (gui->xml, "source_host_label");
	gtk_label_set_text_with_mnemonic (GTK_LABEL (hostname_label), _("_Host:"));
	hostname = glade_xml_get_widget (gui->xml, "source_host");
	
	username_label = glade_xml_get_widget (gui->xml, "source_user_label");
	gtk_label_set_text_with_mnemonic (GTK_LABEL (username_label), _("User_name:"));
	username = glade_xml_get_widget (gui->xml, "source_user");
	
	path_label = glade_xml_get_widget (gui->xml, "source_path_label");
	gtk_label_set_text_with_mnemonic (GTK_LABEL (path_label), _("_Path:"));
	path = glade_xml_get_widget (gui->xml, "source_path");
	
	/* Remove the contents of the extra_table except for the
	 * mailcheck_frame.
	 */
	main_table = (GtkTable *) glade_xml_get_widget (gui->xml, "extra_table");
	gtk_container_set_border_width ((GtkContainer *) main_table, 12);
	gtk_table_set_row_spacings (main_table, 6);
	gtk_table_set_col_spacings (main_table, 8);
	mailcheck_frame = glade_xml_get_widget (gui->xml, "extra_mailcheck_frame");
	child = gtk_container_get_children (GTK_CONTAINER (main_table));
	while (child != NULL) {
		next = child->next;
		if (child->data != (gpointer) mailcheck_frame)
			gtk_container_remove (GTK_CONTAINER (main_table), child->data);
		g_list_free_1 (child);
		child = next;
	}
	
	gtk_table_resize (main_table, 1, 2);
	
	/* Remove any additional mailcheck items. */
	cur_table = (GtkTable *) glade_xml_get_widget (gui->xml, "extra_mailcheck_table");
	gtk_container_set_border_width ((GtkContainer *) cur_table, 12);
	gtk_table_set_row_spacings (cur_table, 6);
	gtk_table_set_col_spacings (cur_table, 8);
	mailcheck_hbox = glade_xml_get_widget (gui->xml, "extra_mailcheck_hbox");
	child = gtk_container_get_children (GTK_CONTAINER (cur_table));
	while (child != NULL) {
		next = child->next;
		if (child->data != (gpointer) mailcheck_hbox)
			gtk_container_remove (GTK_CONTAINER (cur_table), child->data);
		g_list_free_1 (child);
		child = next;
	}
	
	gtk_table_resize (cur_table, 1, 2);

	if (!gui->source.provider) {
		gtk_widget_set_sensitive (GTK_WIDGET (main_table), FALSE);
		if (url)
			camel_url_free (url);
		return;
	} else
		gtk_widget_set_sensitive(GTK_WIDGET(main_table), e_account_writable(emae->account, E_ACCOUNT_SOURCE_URL));
	
	/* Set up our hash table. */
	if (gui->extra_config)
		g_hash_table_destroy (gui->extra_config);
	gui->extra_config = g_hash_table_new (g_str_hash, g_str_equal);
	
	entries = gui->source.provider->extra_conf;
	if (!entries)
		goto done;
	
	cur_table = main_table;
	rows = main_table->nrows;
	for (i = 0; ; i++) {
		GtkWidget *enable_widget = NULL;
		int enabled = TRUE;
		
		switch (entries[i].type) {
		case CAMEL_PROVIDER_CONF_SECTION_START:
		{
			GtkWidget *frame, *label;
			char *markup;
			
			if (entries[i].name && !strcmp (entries[i].name, "mailcheck")) {
				cur_table = (GtkTable *) glade_xml_get_widget (gui->xml, "extra_mailcheck_table");
				rows = cur_table->nrows;
				break;
			}
			
			markup = g_strdup_printf ("<span weight=\"bold\">%s</span>", entries[i].text);
			label = gtk_label_new (NULL);
			gtk_label_set_markup ((GtkLabel *) label, markup);
			gtk_label_set_justify ((GtkLabel *) label, GTK_JUSTIFY_LEFT);
			gtk_label_set_use_markup ((GtkLabel *) label, TRUE);
			gtk_misc_set_alignment ((GtkMisc *) label, 0.0, 0.5);
			gtk_widget_show (label);
			g_free (markup);
			
			cur_table = (GtkTable *) gtk_table_new (0, 2, FALSE);
			gtk_container_set_border_width ((GtkContainer *) cur_table, 12);
			gtk_table_set_row_spacings (cur_table, 6);
			gtk_table_set_col_spacings (cur_table, 8);
			gtk_widget_show ((GtkWidget *) cur_table);
			
			frame = gtk_vbox_new (FALSE, 0);
			gtk_box_pack_start ((GtkBox *) frame, label, FALSE, FALSE, 0);
			gtk_box_pack_start ((GtkBox *) frame, (GtkWidget *) cur_table, FALSE, FALSE, 0);
			gtk_widget_show (frame);
			
			gtk_table_attach (main_table, frame, 0, 2,
					  rows, rows + 1,
					  GTK_EXPAND | GTK_FILL, 0, 0, 0);
			
			rows = 0;
			
			break;
		}
		case CAMEL_PROVIDER_CONF_SECTION_END:
			cur_table = main_table;
			rows = main_table->nrows;
			break;
			
		case CAMEL_PROVIDER_CONF_LABEL:
			if (entries[i].name && entries[i].text) {
				GtkWidget *label;
				
				if (!strcmp (entries[i].name, "username")) {
					gtk_label_set_text_with_mnemonic (GTK_LABEL (username_label), entries[i].text);
					enable_widget = username_label;
				} else if (!strcmp (entries[i].name, "hostname")) {
					gtk_label_set_text_with_mnemonic (GTK_LABEL (hostname_label), entries[i].text);
					enable_widget = hostname_label;
				} else if (!strcmp (entries[i].name, "path")) {
					gtk_label_set_text_with_mnemonic (GTK_LABEL (path_label), entries[i].text);
					enable_widget = path_label;
				} else {
					/* make a new label */
					label = gtk_label_new (entries[i].text);
					gtk_table_resize (cur_table, cur_table->nrows + 1, 2);
					gtk_table_attach (cur_table, label, 0, 2, rows, rows + 1,
							  GTK_EXPAND | GTK_FILL, 0, 0, 0);
					rows++;
					enable_widget = label;
				}
			}
			break;
			
		case CAMEL_PROVIDER_CONF_CHECKBOX:
		{
			GtkWidget *checkbox;
			gboolean active;
			
			checkbox = gtk_check_button_new_with_label (entries[i].text);
			if (url)
				active = camel_url_get_param (url, entries[i].name) != NULL;
			else
				active = atoi (entries[i].value);
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (checkbox), active);
			
			gtk_table_attach (cur_table, checkbox, 0, 2, rows, rows + 1,
					  GTK_EXPAND | GTK_FILL, 0, 0, 0);
			rows++;
			g_hash_table_insert (gui->extra_config, entries[i].name, checkbox);
			if (entries[i].depname)
				enabled = setup_toggle(checkbox, entries[i].depname, emae);

			enable_widget = checkbox;
			break;
		}
		
		case CAMEL_PROVIDER_CONF_ENTRY:
		{
			GtkWidget *label, *entry;
			const char *text;
			
			if (!strcmp (entries[i].name, "username")) {
				gtk_label_set_text_with_mnemonic (GTK_LABEL (username_label), entries[i].text);
				label = username_label;
				entry = username;
			} else if (!strcmp (entries[i].name, "hostname")) {
				gtk_label_set_text_with_mnemonic (GTK_LABEL (hostname_label), entries[i].text);
				label = hostname_label;
				entry = hostname;
			} else if (!strcmp (entries[i].name, "path")) {
				gtk_label_set_text_with_mnemonic (GTK_LABEL (path_label), entries[i].text);
				label = path_label;
				entry = path;
			} else {
				/* make a new text entry with label */
				label = gtk_label_new (entries[i].text);
				gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.0);
				entry = gtk_entry_new ();
				
				gtk_table_attach (cur_table, label, 0, 1, rows, rows + 1,
						  GTK_FILL, 0, 0, 0);
				gtk_table_attach (cur_table, entry, 1, 2, rows, rows + 1,
						  GTK_EXPAND | GTK_FILL, 0, 0, 0);
				rows++;
			}
			
			if (url)
				text = camel_url_get_param (url, entries[i].name);
			else
				text = entries[i].value;
			
			if (text)
				gtk_entry_set_text (GTK_ENTRY (entry), text);
			
			if (entries[i].depname) {
				setup_toggle (entry, entries[i].depname, emae);
				enabled = setup_toggle (label, entries[i].depname, emae);
			}
			
			g_hash_table_insert (gui->extra_config, entries[i].name, entry);
			
			enable_widget = entry;
			break;
		}
		
		case CAMEL_PROVIDER_CONF_CHECKSPIN:
		{
			GtkWidget *hbox, *checkbox, *spin, *label;
			GtkObject *adj;
			char *data, *pre, *post, *p;
			double min, def, max;
			gboolean enable;
			
			/* FIXME: this is pretty fucked... */
			data = entries[i].text;
			p = strstr (data, "%s");
			g_return_if_fail (p != NULL);
			
			pre = g_strndup (data, p - data);
			post = p + 2;
			
			data = entries[i].value;
			enable = *data++ == 'y';
			g_return_if_fail (*data == ':');
			min = strtod (data + 1, &data);
			g_return_if_fail (*data == ':');
			def = strtod (data + 1, &data);
			g_return_if_fail (*data == ':');
			max = strtod (data + 1, NULL);
			
			if (url) {
				const char *val;
				
				val = camel_url_get_param (url, entries[i].name);
				if (!val)
					enable = FALSE;
				else {
					enable = TRUE;
					def = atof (val);
				}
			}
			
			hbox = gtk_hbox_new (FALSE, 0);
			checkbox = gtk_check_button_new_with_label (pre);
			g_free (pre);
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (checkbox), enable);
			adj = gtk_adjustment_new (def, min, max, 1, 1, 1);
			spin = gtk_spin_button_new (GTK_ADJUSTMENT (adj), 1, 0);
			label = gtk_label_new (post);
			
			gtk_box_pack_start (GTK_BOX (hbox), checkbox, FALSE, TRUE, 0);
			gtk_box_pack_start (GTK_BOX (hbox), spin, FALSE, FALSE, 0);
			gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, TRUE, 4);
			
			gtk_table_attach (cur_table, hbox, 0, 2, rows, rows + 1,
					  GTK_EXPAND | GTK_FILL, 0, 0, 0);
			rows++;
			g_hash_table_insert (gui->extra_config, entries[i].name, checkbox);
			name = g_strdup_printf ("%s_value", entries[i].name);
			g_hash_table_insert (gui->extra_config, name, spin);
			if (entries[i].depname) {
				setup_toggle (checkbox, entries[i].depname, emae);
				setup_toggle (spin, entries[i].depname, emae);
				enabled = setup_toggle (label, entries[i].depname, emae);
			}
			
			enable_widget = hbox;
			break;
		}

		case CAMEL_PROVIDER_CONF_HIDDEN:
			break;

		case CAMEL_PROVIDER_CONF_END:
			goto done;
		}
		
		if (enabled && enable_widget)
			gtk_widget_set_sensitive(enable_widget, e_account_writable_option(emae->account, gui->source.provider->protocol, entries[i].name));
	}
	
 done:
	gtk_widget_show_all (GTK_WIDGET (main_table));
	if (url)
		camel_url_free (url);
}

static void
emae_account_entry_changed(GtkEntry *entry, EMAccountEditor *emae)
{
	int item = GPOINTER_TO_INT(g_object_get_data((GObject *)entry, "account-item"));

	e_account_set_string(emae->account, item, gtk_entry_get_text(entry));
}

static GtkEntry *
emae_account_entry(EMAccountEditor *emae, const char *name, int item)
{
	GtkEntry *entry;
	const char *text;

	entry = (GtkEntry *)glade_xml_get_widget(emae->priv->xml, name);
	text = e_account_get_string(emae->account, item);
	if (text)
		gtk_entry_set_text(entry, text);
	g_object_set_data((GObject *)entry, "account-item", GINT_TO_POINTER(item));
	g_signal_connect(entry, "changed", G_CALLBACK(emae_account_entry_changed), emae);
	gtk_widget_set_sensitive((GtkWidget *)entry, e_account_writable(emae->account, item));

	return entry;
}

static void
emae_account_toggle_changed(GtkToggleButton *toggle, EMAccountEditor *emae)
{
	int item = GPOINTER_TO_INT(g_object_get_data((GObject *)toggle, "account-item"));

	e_account_set_bool(emae->account, item, gtk_toggle_button_get_active(toggle));
}

static GtkToggleButton *
emae_account_toggle(EMAccountEditor *emae, const char *name, int item)
{
	GtkToggleButton *toggle;

	toggle = (GtkToggleButton *)glade_xml_get_widget(emae->priv->xml, name);
	gtk_toggle_button_set_active(toggle, e_account_get_bool(emae->account, item));
	g_object_set_data((GObject *)toggle, "account-item", GINT_TO_POINTER(item));
	g_signal_connect(toggle, "toggled", G_CALLBACK(emae_account_toggle_changed), emae);
	gtk_widget_set_sensitive((GtkWidget *)toggle, e_account_writable(emae->account, item));

	return toggle;
}

static void
emae_account_spinint_changed(GtkSpinButton *spin, EMAccountEditor *emae)
{
	int item = GPOINTER_TO_INT(g_object_get_data((GObject *)spin, "account-item"));

	e_account_set_int(emae->account, item, gtk_spin_button_get_value(spin));
}

static GtkSpinButton *
emae_account_spinint(EMAccountEditor *emae, const char *name, int item)
{
	GtkSpinButton *spin;

	spin = (GtkSpinButton *)glade_xml_get_widget(emae->priv->xml, name);
	gtk_spin_button_set_value(spin, e_account_get_int(emae->account, item));
	g_object_set_data((GObject *)spin, "account-item", GINT_TO_POINTER(item));
	g_signal_connect(spin, "value_changed", G_CALLBACK(emae_account_spinint_changed), emae);
	gtk_widget_set_sensitive((GtkWidget *)spin, e_account_writable(emae->account, item));

	return spin;
}

static void
emae_account_folder_changed(EMFolderSelectionButton *folder, EMAccountEditor *emae)
{
	int item = GPOINTER_TO_INT(g_object_get_data((GObject *)folder, "account-item"));

	e_account_set_string(emae->account, item, em_folder_selection_button_get_selection(folder));
}

static EMFolderSelectionButton *
emae_account_folder(EMAccountEditor *emae, const char *name, int item, int deffolder)
{
	EMFolderSelectionButton *folder;
	const char *uri;

	folder = (EMFolderSelectionButton *)glade_xml_get_widget(emae->priv->xml, name);
	uri = e_account_get_string(emae->account, item);
	if (uri) {
		char *tmp = em_uri_to_camel(uri);

		em_folder_selection_button_set_selection(folder, tmp);
		g_free(tmp);
	} else {
		em_folder_selection_button_set_selection(folder, mail_component_get_folder_uri(NULL, deffolder));
	}

	g_object_set_data((GObject *)folder, "account-item", GINT_TO_POINTER(item));
	g_object_set_data((GObject *)folder, "folder-default", GINT_TO_POINTER(deffolder));
	g_signal_connect(folder, "selected", G_CALLBACK(emae_account_folder_changed), emae);
	gtk_widget_show((GtkWidget *)folder);

	gtk_widget_set_sensitive((GtkWidget *)folder, e_account_writable(emae->account, item));

	return folder;
}

#if defined (HAVE_NSS)
static void
smime_changed(EMAccountEditor *emae)
{
	EMAccountEditorPrivate *gui = emae->priv;
	int act;
	const char *tmp;

	tmp = gtk_entry_get_text(gui->smime_sign_key);
	act = tmp && tmp[0];
	gtk_widget_set_sensitive((GtkWidget *)gui->smime_sign_key_clear, act);
	gtk_widget_set_sensitive((GtkWidget *)gui->smime_sign_default, act);
	if (!act)
		gtk_toggle_button_set_active(gui->smime_sign_default, FALSE);

	tmp = gtk_entry_get_text(gui->smime_encrypt_key);
	act = tmp && tmp[0];
	gtk_widget_set_sensitive((GtkWidget *)gui->smime_encrypt_key_clear, act);
	gtk_widget_set_sensitive((GtkWidget *)gui->smime_encrypt_default, act);
	gtk_widget_set_sensitive((GtkWidget *)gui->smime_encrypt_to_self, act);
	if (!act) {
		gtk_toggle_button_set_active(gui->smime_encrypt_default, FALSE);
		gtk_toggle_button_set_active(gui->smime_encrypt_to_self, FALSE);
	}
}

static void
smime_sign_key_selected(GtkWidget *dialog, const char *key, EMAccountEditor *emae)
{
	EMAccountEditorPrivate *gui = emae->priv;

	if (key != NULL) {
		gtk_entry_set_text(gui->smime_sign_key, key);
		smime_changed(emae);
	}

	gtk_widget_destroy(dialog);
}

static void
smime_sign_key_select(GtkWidget *button, EMAccountEditor *emae)
{
	EMAccountEditorPrivate *gui = emae->priv;
	GtkWidget *w;

	w = e_cert_selector_new(E_CERT_SELECTOR_SIGNER, gtk_entry_get_text(gui->smime_sign_key));
	gtk_window_set_modal((GtkWindow *)w, TRUE);
	gtk_window_set_transient_for((GtkWindow *)w, (GtkWindow *)gtk_widget_get_toplevel((GtkWidget *)emae));
	g_signal_connect(w, "selected", G_CALLBACK(smime_sign_key_selected), emae);
	gtk_widget_show(w);
}

static void
smime_sign_key_clear(GtkWidget *w, EMAccountEditor *emae)
{
	EMAccountEditorPrivate *gui = emae->priv;

	gtk_entry_set_text(gui->smime_sign_key, "");
	smime_changed(emae);
}

static void
smime_encrypt_key_selected(GtkWidget *dialog, const char *key, EMAccountEditor *emae)
{
	EMAccountEditorPrivate *gui = emae->priv;

	if (key != NULL) {
		gtk_entry_set_text(gui->smime_encrypt_key, key);
		smime_changed(emae);
	}

	gtk_widget_destroy(dialog);
}

static void
smime_encrypt_key_select(GtkWidget *button, EMAccountEditor *emae)
{
	EMAccountEditorPrivate *gui = emae->priv;
	GtkWidget *w;

	w = e_cert_selector_new(E_CERT_SELECTOR_SIGNER, gtk_entry_get_text(gui->smime_encrypt_key));
	gtk_window_set_modal((GtkWindow *)w, TRUE);
	gtk_window_set_transient_for((GtkWindow *)w, (GtkWindow *)gtk_widget_get_toplevel((GtkWidget *)emae));
	g_signal_connect(w, "selected", G_CALLBACK(smime_encrypt_key_selected), emae);
	gtk_widget_show(w);
}

static void
smime_encrypt_key_clear(GtkWidget *w, EMAccountEditor *emae)
{
	EMAccountEditorPrivate *gui = emae->priv;

	gtk_entry_set_text(gui->smime_encrypt_key, "");
	smime_changed(emae);
}
#endif

struct _provider_host_info {
	guint32 flag;
	void (*setval)(CamelURL *, const char *);
	const char *widgets[3];
};

static struct _provider_host_info emae_source_host_info[] = {
	{ CAMEL_URL_PART_HOST, camel_url_set_host, { "source_host", "source_host_label" } },
	{ CAMEL_URL_PART_USER, camel_url_set_user, { "source_user", "source_user_label", } },
	{ CAMEL_URL_PART_PATH, camel_url_set_path, { "source_path", "source_path_label", "source_path_entry" } },
	{ CAMEL_URL_PART_AUTH, NULL, { NULL, "source_auth_frame" } },
	{ 0 },
};

static struct _provider_host_info emae_transport_host_info[] = {
	{ CAMEL_URL_PART_HOST, camel_url_set_host, { "transport_host", "transport_host_label" } },
	{ CAMEL_URL_PART_USER, camel_url_set_user, { "transport_user", "transport_user_label", } },
	{ CAMEL_URL_PART_AUTH, NULL, { NULL, "transport_auth_frame" } },
	{ 0 },
};

static struct _service_info {
	int account_uri_key;
	int save_passwd_key;

	char *frame;
	char *type_dropdown;

	char *container;
	char *description;
	char *hostname;
	char *username;
	char *path;

	char *security_frame;
	char *ssl_hbox;
	char *use_ssl;
	char *ssl_disabled;

	char *needs_auth;
	char *auth_frame;

	char *authtype;
	char *authtype_check;

	char *remember_password;

	struct _provider_host_info *host_info;
} emae_service_info[CAMEL_NUM_PROVIDER_TYPES] = {
	{ E_ACCOUNT_SOURCE_URL, E_ACCOUNT_SOURCE_SAVE_PASSWD,
	  "source_frame", "source_type_dropdown",
	  "source_vbox", "source_description", "source_host", "source_user", "source_path",
	  "source_security_frame", "source_ssl_hbox", "source_use_ssl", "source_ssl_disabled",
	  NULL, "source_auth_frame",
	  "source_auth_dropdown", "source_check_supported",
	  "source_remember_password",
	  emae_source_host_info,
	},
	{ E_ACCOUNT_TRANSPORT_URL, E_ACCOUNT_TRANSPORT_SAVE_PASSWD,
	  "transport_frame", "transport_type_dropdown",
	  "transport_vbox", "transport_description", "transport_host", "transport_user", NULL,
	  "transport_security_frame", "transport_ssl_hbox", "transport_use_ssl", "transport_ssl_disabled",
	  "transport_needs_auth", "transport_auth_frame",
	  "transport_auth_dropdown", "transport_check_supported",
	  "transport_remember_password",
	  emae_transport_host_info,
	},
};

static void
emae_uri_changed(EMAccountEditorService *service)
{
	char *uri;

	uri = camel_url_to_string(service->url, 0);
	e_account_set_string(service->emae->account, emae_service_info[service->type].account_uri_key, uri);
	g_free(uri);
}

static void
emae_service_url_changed(EMAccountEditorService *service, void (*setval)(CamelURL *, const char *), GtkEntry *entry)
{
	setval(service->url, gtk_entry_get_text(entry));
	emae_uri_changed(service);
}

static void
emae_hostname_changed(GtkEntry *entry, EMAccountEditorService *service)
{
	emae_service_url_changed(service, camel_url_set_host, entry);
}

static void
emae_username_changed(GtkEntry *entry, EMAccountEditorService *service)
{
	emae_service_url_changed(service, camel_url_set_user, entry);
}

static void
emae_path_changed(GtkEntry *entry, EMAccountEditorService *service)
{
	emae_service_url_changed(service, camel_url_set_path, entry);
}

static void
emae_needs_auth(GtkToggleButton *toggle, EMAccountEditorService *service)
{
	GtkWidget *w;
	int need = gtk_toggle_button_get_active(toggle);

	w = glade_xml_get_widget(service->emae->priv->xml, emae_service_info[service->type].auth_frame);
	gtk_widget_set_sensitive(w, need);
	/* if need ; service_changed? */
}

static void
emae_ssl_update(EMAccountEditorService *service)
{
	int id = gtk_combo_box_get_active(service->use_ssl);
	GtkTreeModel *model;
	GtkTreeIter iter;
	char *ssl;

	if (id == -1)
		return;

	model = gtk_combo_box_get_model(service->use_ssl);
	if (!gtk_tree_model_iter_nth_child(model, &iter, NULL, id))
		return;

	gtk_tree_model_get(model, &iter, 1, &ssl, -1);
	if (!strcmp(ssl, "none"))
		ssl = NULL;
	camel_url_set_param(service->url, "use_ssl", ssl);
}

static void
emae_ssl_changed(GtkComboBox *dropdown, EMAccountEditorService *service)
{
	emae_ssl_update(service);
	emae_uri_changed(service);
}

static void
emae_service_provider_changed(EMAccountEditorService *service)
{
	char *uri;
	int i, j;
	void (*show)(GtkWidget *);

	if (service->provider) {
		int enable;
		GtkWidget *dwidget = NULL;

		camel_url_set_protocol(service->url, service->provider->protocol);

		enable = e_account_writable_option(service->emae->account, service->provider->protocol, "auth");
			
		gtk_widget_set_sensitive((GtkWidget *)service->authtype, enable);
		gtk_widget_set_sensitive((GtkWidget *)service->check_supported, enable);

		enable = e_account_writable_option(service->emae->account, service->provider->protocol, "use_ssl");
		gtk_widget_set_sensitive((GtkWidget *)service->use_ssl, enable);
			
		enable = e_account_writable(service->emae->account, emae_service_info[service->type].save_passwd_key);
		gtk_widget_set_sensitive((GtkWidget *)service->remember, enable);

		/* FIXME: handle license */
		gtk_label_set_text(service->description, service->provider->description);
		gtk_widget_show(service->frame);

		for (i=0;emae_service_info[service->type].host_info[i].flag;i++) {
			const char *name;
			GtkWidget *w;
			struct _provider_host_info *info = &emae_service_info[service->type].host_info[i];

			enable = CAMEL_PROVIDER_ALLOWS(service->provider, info->flag);
			show = enable?gtk_widget_show:gtk_widget_hide;

			for (j=0; j < sizeof(info->widgets)/sizeof(info->widgets[0]); j++) {
				name = info->widgets[j];
				if (name) {
					w = glade_xml_get_widget(service->emae->priv->xml, name);
					show(w);
					if (j == 0) {
						if (dwidget == NULL && enable)
							dwidget = w;

						if (info->setval)
							info->setval(service->url, enable?gtk_entry_get_text((GtkEntry *)w):NULL);
					}
				}
			}
		}

		if (dwidget)
			gtk_widget_grab_focus(dwidget);

		if (CAMEL_PROVIDER_ALLOWS(service->provider, CAMEL_URL_PART_AUTH)) {
			camel_url_set_authmech(service->url, NULL);
			emae_setup_authtype(service->emae, service);
			if (service->needs_auth && !CAMEL_PROVIDER_NEEDS(service->provider, CAMEL_URL_PART_AUTH))
				gtk_widget_show((GtkWidget *)service->needs_auth);
		} else {
			if (service->needs_auth)
				gtk_widget_hide((GtkWidget *)service->needs_auth);
		}
#ifdef HAVE_SSL
		gtk_widget_hide(service->no_ssl);
		if (service->provider->flags & CAMEL_PROVIDER_SUPPORTS_SSL) {
			emae_ssl_update(service);
			show = gtk_widget_show;
		} else {
			camel_url_set_param(service->url, "use_ssl", NULL);
			show = gtk_widget_hide;
		}
		show(service->ssl_frame);
		show(service->ssl_hbox);
#else
		gtk_widget_hide(service->ssl_hbox);
		gtk_widget_show(service->no_ssl);
		camel_url_set_param(service->url, "use_ssl", NULL);
#endif
		uri = camel_url_to_string(service->url, 0);
	} else {
		camel_url_set_protocol(service->url, NULL);
		gtk_label_set_text(service->description, "");
		gtk_widget_hide(service->frame);
		gtk_widget_hide(service->auth_frame);
		gtk_widget_hide(service->ssl_frame);
		uri = NULL;
	}

	/* FIXME: linked services? */
	/* FIXME: permissions setup */

	printf("providertype changed, newuri = '%s'\n", uri);
	e_account_set_string(service->emae->account, emae_service_info[service->type].account_uri_key, uri);
	g_free(uri);
}

static void
emae_provider_changed(GtkComboBox *dropdown, EMAccountEditorService *service)
{
	int id = gtk_combo_box_get_active(dropdown);
	GtkTreeModel *model;
	GtkTreeIter iter;

	if (id == -1)
		return;

	model = gtk_combo_box_get_model(dropdown);
	if (!gtk_tree_model_iter_nth_child(model, &iter, NULL, id))
		return;

	gtk_tree_model_get(model, &iter, 1, &service->provider, -1);

	g_list_free(service->authtypes);
	service->authtypes = NULL;

	emae_service_provider_changed(service);

	e_config_target_changed((EConfig *)service->emae->priv->config);
}

static GtkWidget *
emae_setup_providers(EMAccountEditor *emae, EMAccountEditorService *service)
{
	EMAccountEditorPrivate *gui = emae->priv;
	EAccount *account = emae->account;
	GtkListStore *store;
	GtkTreeIter iter;
	GList *l;
	GtkCellRenderer *cell = gtk_cell_renderer_text_new();
	GtkComboBox *dropdown;
	int active = 0, i;
	struct _service_info *info = &emae_service_info[service->type];
	const char *uri = e_account_get_string(account, info->account_uri_key);
	char *current = NULL;

	dropdown = (GtkComboBox *)glade_xml_get_widget(gui->xml, info->type_dropdown);
	gtk_widget_show((GtkWidget *)dropdown);

	if (uri) {
		const char *colon = strchr(uri, ':');
		int len;

		if (colon) {
			len = colon-uri;
			current = g_alloca(len+1);
			memcpy(current, uri, len);
			current[len] = 0;
		}
	}
		
	store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_POINTER);

	i = 0;

	/* We just special case each type here, its just easier */
	if (service->type == CAMEL_PROVIDER_STORE) {
		gtk_list_store_append(store, &iter);
		gtk_list_store_set(store, &iter, 0, _("None"), 1, NULL, -1);
		i++;
	}

	for (l=gui->providers; l; l=l->next) {
		CamelProvider *provider = l->data;
		
		if (!((strcmp(provider->domain, "mail") == 0
		       || strcmp (provider->domain, "news") == 0)
		      && provider->object_types[service->type]
		      && (service->type != CAMEL_PROVIDER_STORE || (provider->flags & CAMEL_PROVIDER_IS_SOURCE) != 0)))
			continue;

		gtk_list_store_append(store, &iter);
		gtk_list_store_set(store, &iter, 0, provider->name, 1, provider, -1);

		/* FIXME: GtkCombo doesn't support sensitiviy, we can hopefully kill this crap anyway */
#if 0
		if (type == CAMEL_PROVIDER_TRANSPORT
		    && CAMEL_PROVIDER_IS_STORE_AND_TRANSPORT (provider))
			gtk_widget_set_sensitive (item, FALSE);
#endif
		printf("adding provider '%s' is the same as our uri? '%s'\n", provider->protocol, current);

		if (current && strcmp(provider->protocol, current) == 0)
			active = i;
		i++;
	}

	gtk_combo_box_set_model(dropdown, (GtkTreeModel *)store);
	gtk_cell_layout_pack_start((GtkCellLayout *)dropdown, cell, TRUE);
	gtk_cell_layout_set_attributes((GtkCellLayout *)dropdown, cell, "text", 0, NULL);

	gtk_combo_box_set_active(dropdown, -1);
	printf("setting active provider to '%d'\n", active);
	gtk_combo_box_set_active(dropdown, active);
	g_signal_connect(dropdown, "changed", G_CALLBACK(emae_provider_changed), service);

	return (GtkWidget *)dropdown;
}

static void
emae_authtype_changed(GtkComboBox *dropdown, EMAccountEditorService *service)
{
	int id = gtk_combo_box_get_active(dropdown);
	GtkTreeModel *model;
	GtkTreeIter iter;
	char *uri;
	CamelServiceAuthType *authtype;

	if (id == -1)
		return;

	model = gtk_combo_box_get_model(dropdown);
	if (!gtk_tree_model_iter_nth_child(model, &iter, NULL, id))
		return;

	gtk_tree_model_get(model, &iter, 1, &authtype, -1);
	if (authtype)
		camel_url_set_authmech(service->url, authtype->authproto);
	else
		camel_url_set_authmech(service->url, NULL);

	uri = camel_url_to_string(service->url, 0);
	printf("authtype changed, newuri = '%s'\n", uri);
	e_account_set_string(service->emae->account, emae_service_info[service->type].account_uri_key, uri);
	g_free(uri);

	/* invoke service_changed thing? */
	gtk_widget_set_sensitive((GtkWidget *)service->remember, authtype?authtype->need_password:FALSE);
}

static void emae_check_authtype(GtkWidget *w, EMAccountEditorService *service);

static GtkWidget *
emae_setup_authtype(EMAccountEditor *emae, EMAccountEditorService *service)
{
	EMAccountEditorPrivate *gui = emae->priv;
	EAccount *account = emae->account;
	GtkListStore *store;
	GtkTreeIter iter;
	GtkComboBox *dropdown;
	GtkWidget *w;
	int active = 0;
	int i;
	struct _service_info *info = &emae_service_info[service->type];
	const char *uri = e_account_get_string(account, info->account_uri_key);
	GList *l, *ll;
	CamelURL *url = NULL;

	dropdown = (GtkComboBox *)glade_xml_get_widget(gui->xml, info->authtype);
	gtk_widget_show((GtkWidget *)dropdown);

	store = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_POINTER, G_TYPE_BOOLEAN);

	if (uri)
		url = camel_url_new(uri, NULL);

	if (service->provider) {
		for (i=0, l=service->provider->authtypes; l; l=l->next, i++) {
			CamelServiceAuthType *authtype = l->data;
			int avail;

			/* if we have some already shown */
			if (service->authtypes) {
				for (ll = service->authtypes;ll;ll = g_list_next(ll))
					if (!strcmp(authtype->authproto, ((CamelServiceAuthType *)ll->data)->authproto))
						break;
				avail = ll != NULL;
			} else {
				avail = TRUE;
			}
			
			gtk_list_store_append(store, &iter);
			gtk_list_store_set(store, &iter, 0, authtype->name, 1, authtype, 2, !avail, -1);

			if (url && url->authmech && !strcmp(url->authmech, authtype->authproto))
				active = i;
		}
	}

	gtk_combo_box_set_model(dropdown, (GtkTreeModel *)store);
	gtk_combo_box_set_active(dropdown, -1);

	if (service->auth_changed_id == 0) {
		GtkCellRenderer *cell = gtk_cell_renderer_text_new();

		gtk_cell_layout_pack_start((GtkCellLayout *)dropdown, cell, TRUE);
		gtk_cell_layout_set_attributes((GtkCellLayout *)dropdown, cell, "text", 0, "strikethrough", 2, NULL);

		service->auth_changed_id = g_signal_connect(dropdown, "changed", G_CALLBACK(emae_authtype_changed), service);
		w = glade_xml_get_widget(gui->xml, info->authtype_check);
		g_signal_connect(w, "clicked", G_CALLBACK(emae_check_authtype), service);
	}

	gtk_combo_box_set_active(dropdown, active);

	if (url)
		camel_url_free(url);

	return (GtkWidget *)dropdown;
}

static void emae_check_authtype(GtkWidget *w, EMAccountEditorService *service)
{
	EMAccountEditor *emae = service->emae;
	const char *uri;

	if (service->authtypes) {
		g_list_free(service->authtypes);
		service->authtypes = NULL;
	}

	uri = e_account_get_string(emae->account, emae_service_info[service->type].account_uri_key);
	if (mail_config_check_service(uri, service->type, &service->authtypes, (GtkWindow *)gtk_widget_get_toplevel((GtkWidget *)emae)))
		emae_setup_authtype(emae, service);
}

static void
emae_setup_service(EMAccountEditor *emae, EMAccountEditorService *service)
{
	EMAccountEditorPrivate *gui = emae->priv;
	struct _service_info *info = &emae_service_info[service->type];
	const char *uri = e_account_get_string(emae->account, info->account_uri_key);

	if (uri) {
		service->provider = camel_provider_get(uri, NULL);
		service->url = camel_url_new(uri, NULL);
	} else {
		service->provider = NULL;
		service->url = camel_url_new("dummy:", NULL);
		camel_url_set_protocol(service->url, NULL);
	}

	service->frame = glade_xml_get_widget(gui->xml, info->frame);
	service->container = glade_xml_get_widget(gui->xml, info->container);
	service->description = GTK_LABEL (glade_xml_get_widget (gui->xml, info->description));
	service->hostname = GTK_ENTRY (glade_xml_get_widget (gui->xml, info->hostname));
	service->username = GTK_ENTRY (glade_xml_get_widget (gui->xml, info->username));
	if (info->path)
		service->path = GTK_ENTRY (glade_xml_get_widget (gui->xml, info->path));

	service->ssl_frame = glade_xml_get_widget (gui->xml, info->security_frame);
	gtk_widget_hide (service->ssl_frame);
	service->ssl_hbox = glade_xml_get_widget (gui->xml, info->ssl_hbox);
	service->use_ssl = (GtkComboBox *)glade_xml_get_widget (gui->xml, info->use_ssl);
	service->no_ssl = glade_xml_get_widget (gui->xml, info->ssl_disabled);

	if (service->url) {
		const char *tmp;
		int i;

		if (service->url->host)
			gtk_entry_set_text(service->hostname, service->url->host);
		if (service->url->user)
			gtk_entry_set_text(service->username, service->url->user);
		if (service->path && service->url->path)
			gtk_entry_set_text(service->path, service->url->path);

		tmp = camel_url_get_param(service->url, "use_ssl");
		if (tmp == NULL)
			tmp = "never";

		for (i=0;i<num_ssl_options;i++) {
			if (!strcmp(ssl_options[i].value, tmp)) {
				gtk_combo_box_set_active(service->use_ssl, i);
				break;
			}
		}
	}

	g_signal_connect (service->hostname, "changed", G_CALLBACK (emae_hostname_changed), service);
	g_signal_connect (service->username, "changed", G_CALLBACK (emae_username_changed), service);
	if (service->path)
		g_signal_connect (service->path, "changed", G_CALLBACK (emae_path_changed), service);

	g_signal_connect(service->use_ssl, "changed", G_CALLBACK(emae_ssl_changed), service);

	service->auth_frame = glade_xml_get_widget(gui->xml, info->auth_frame);
	service->remember = emae_account_toggle(emae, info->remember_password, info->save_passwd_key);
	service->check_supported = (GtkButton *)glade_xml_get_widget(gui->xml, info->authtype_check);
	if (info->needs_auth) {
		service->needs_auth = (GtkToggleButton *)glade_xml_get_widget (gui->xml, info->needs_auth);
		g_signal_connect(service->needs_auth, "toggled", G_CALLBACK(emae_needs_auth), service);
	}

	emae_setup_providers(emae, service);
	service->authtype = (GtkComboBox *)emae_setup_authtype(emae, service);

	if (!e_account_writable (emae->account, info->account_uri_key))
		gtk_widget_set_sensitive(service->container, FALSE);
	else
		gtk_widget_set_sensitive(service->container, TRUE);

	emae_service_provider_changed(service);
}

static struct {
	char *name;
	int item;
} emae_identity_entries[] = {
	{ "management_name", E_ACCOUNT_NAME },
	{ "identity_full_name", E_ACCOUNT_ID_NAME },
	{ "identity_address", E_ACCOUNT_ID_ADDRESS },
	{ "identity_reply_to", E_ACCOUNT_ID_REPLY_TO },
	{ "identity_organization", E_ACCOUNT_ID_ORGANIZATION },
};

static GtkWidget *
emae_identity_page(EConfig *ec, EConfigItem *item, struct _GtkWidget *parent, struct _GtkWidget *old, void *data)
{
	EMAccountEditor *emae = data;
	EMAccountEditorPrivate *gui = emae->priv;
	EAccount *account = emae->account;
	int i;
	GtkWidget *w;

	if (old)
		return old;

	/* Management & Identity fields*/
	for (i=0;i<sizeof(emae_identity_entries)/sizeof(emae_identity_entries[0]);i++)
		emae_account_entry(emae, emae_identity_entries[i].name, emae_identity_entries[i].item);

	/* FIXME: listen to toggle */
	gui->default_account = GTK_TOGGLE_BUTTON (glade_xml_get_widget (gui->xml, "management_default"));
	if (!mail_config_get_default_account ()
	    || (account == mail_config_get_default_account ()))
		gtk_toggle_button_set_active (gui->default_account, TRUE);
	
	prepare_signatures (emae);

	gtk_widget_set_sensitive((GtkWidget *)gui->sig_menu, e_account_writable(emae->account, E_ACCOUNT_ID_SIGNATURE));
	gtk_widget_set_sensitive(glade_xml_get_widget(gui->xml, "sigAddNew"),
				 gconf_client_key_is_writable(mail_config_get_gconf_client(),
							      "/apps/evolution/mail/signatures", NULL));

	w = glade_xml_get_widget(gui->xml, item->label);
	if (((EConfig *)gui->config)->type == E_CONFIG_DRUID) {
		GtkWidget *page = glade_xml_get_widget(gui->druidxml, "identity_page");

		/* need to set packing? */
		gtk_widget_reparent(w, ((GnomeDruidPageStandard *)page)->vbox);
	}

	return w;
}

static GtkWidget *
emae_receive_page(EConfig *ec, EConfigItem *item, struct _GtkWidget *parent, struct _GtkWidget *old, void *data)
{
	EMAccountEditor *emae = data;
	EMAccountEditorPrivate *gui = emae->priv;
	GtkWidget *w;

	if (old)
		return old;

	/* FIXME: how to setup the initial account details so this doesn't just crash a lot */

	/* Source */
	gui->source.type = CAMEL_PROVIDER_STORE;
	emae_setup_service(emae, &gui->source);

	emae_account_toggle(emae, "extra_auto_check", E_ACCOUNT_SOURCE_AUTO_CHECK);
	emae_account_spinint(emae, "extra_auto_check_min", E_ACCOUNT_SOURCE_AUTO_CHECK_TIME);

	w = glade_xml_get_widget(gui->xml, item->label);
	if (((EConfig *)gui->config)->type == E_CONFIG_DRUID) {
		GtkWidget *page = glade_xml_get_widget(gui->druidxml, "source_page");

		/* need to set packing? */
		gtk_widget_reparent(w, ((GnomeDruidPageStandard *)page)->vbox);
	}

	return w;
}

static void
emae_remove_childen(EMAccountEditor *emae, const char *container, const char *keep)
{
	GtkWidget *k;
	GList *l;

	k = keep?glade_xml_get_widget(emae->priv->xml, keep):NULL;
	l = gtk_container_get_children((GtkContainer *)glade_xml_get_widget(emae->priv->xml, container));
	while (l) {
		GList *n = g_list_next(l);

		if (l->data != (void *)k)
			gtk_widget_destroy((GtkWidget *)l->data);

		g_list_free_1(l);
		l = n;
	}
}

static GtkWidget *
emae_receive_options_page(EConfig *ec, EConfigItem *item, struct _GtkWidget *parent, struct _GtkWidget *old, void *data)
{
	EMAccountEditor *emae = data;
	GtkWidget *w;

	/* this is really crap-o */
	emae_remove_childen(emae, "extra_table", "extra_mailcheck_frame");
	emae_remove_childen(emae, "extra_mailcheck_table", "extra_mailcheck_hbox");

	/* FIXME: need to hide it in druid if it isn't needed, e.g. recieve type none */
	/*uri = e_account_get_string(emae->account, E_ACCOUNT_SOURCE_URL); */

	{
		GtkWidget *w, *l;
		char *tmp;
		const char *uri;

		uri = e_account_get_string(emae->account, E_ACCOUNT_SOURCE_URL);
		tmp = g_strdup_printf("account url is: %s", uri?uri:"unset");

		l = gtk_label_new(tmp);
		g_free(tmp);
		gtk_widget_show(l);
		w = glade_xml_get_widget(emae->priv->xml, "extra_table");
		gtk_table_attach((GtkTable *)w, l, 0, 2, 1, 2, GTK_EXPAND|GTK_FILL, 0, 0, 0);
	}

	if (old)
		return old;

	w = glade_xml_get_widget(emae->priv->xml, item->label);
	if (((EConfig *)emae->priv->config)->type == E_CONFIG_DRUID) {
		GtkWidget *page = glade_xml_get_widget(emae->priv->druidxml, "extra_page");

		/* need to set packing? */
		gtk_widget_reparent(w, ((GnomeDruidPageStandard *)page)->vbox);
	}

	return w;
}

static void
emae_option_toggle_changed(GtkToggleButton *toggle, EMAccountEditorService *service)
{
	const char *name = g_object_get_data((GObject *)toggle, "option-name");
	GSList *depl = g_object_get_data((GObject *)toggle, "dependent-list");
	int active = gtk_toggle_button_get_active(toggle);

	for (;depl;depl = g_slist_next(depl))
		gtk_widget_set_sensitive((GtkWidget *)depl->data, active);

	camel_url_set_param(service->url, name, active?"":NULL);
	emae_uri_changed(service);
}

static GtkWidget *
emae_option_toggle(EMAccountEditorService *service, const char *text, const char *name, int def)
{
	GtkWidget *w;

	w = g_object_new(gtk_check_button_get_type(),
			 "label", text,
			 "active", service->url?camel_url_get_param(service->url, name) != NULL:def,
			 NULL);
	g_object_set_data((GObject *)w, "option-name", (void *)name);
	g_signal_connect(w, "toggled", G_CALLBACK(emae_option_toggle_changed), service);

	printf("adding option toggle '%s'\n", text);

	return w;
}

static void
emae_option_entry_changed(GtkEntry *entry, EMAccountEditorService *service)
{
	const char *name = g_object_get_data((GObject *)entry, "option-name");
	const char *text = gtk_entry_get_text(entry);

	camel_url_set_param(service->url, name, text && text[0]?text:NULL);
	emae_uri_changed(service);
}

static GtkWidget *
emae_option_entry(EMAccountEditorService *service, const char *name, const char *def)
{
	GtkWidget *w;

	if (service->url
	    && (def = camel_url_get_param(service->url, name)) == NULL)
		def = "";

	w = g_object_new(gtk_entry_get_type(),
			 "label", def,
			 NULL);
	g_object_set_data((GObject *)w, "option-name", (void *)name);
	g_signal_connect(w, "changed", G_CALLBACK(emae_option_entry_changed), service);

	return w;
}

static void
emae_option_checkspin_changed(GtkSpinButton *spin, EMAccountEditorService *service)
{
	const char *name = g_object_get_data((GObject *)spin, "option-name");
	char value[16];

	sprintf(value, "%d", gtk_spin_button_get_value_as_int(spin));
	camel_url_set_param(service->url, name, value);
	emae_uri_changed(service);
}

static void
emae_option_checkspin_check_changed(GtkToggleButton *toggle, EMAccountEditorService *service)
{
	const char *name = g_object_get_data((GObject *)toggle, "option-name");
	GtkSpinButton *spin = g_object_get_data((GObject *)toggle, "option-target");

	if (gtk_toggle_button_get_active(toggle)) {
		gtk_widget_set_sensitive((GtkWidget *)spin, TRUE);
		emae_option_checkspin_changed(spin, service);
	} else {
		camel_url_set_param(service->url, name, NULL);
		gtk_widget_set_sensitive((GtkWidget *)spin, FALSE);
		emae_uri_changed(service);
	}
}

/* this is a fugly api */
static GtkWidget *
emae_option_checkspin(EMAccountEditorService *service, const char *name, const char *fmt, const char *info)
{
	GtkWidget *hbox, *check, *spin, *label;
	double min, def, max;
	char *pre, *post;
	const char *val;
	char on;
	int enable;

	pre = g_alloca(strlen(fmt)+1);
	strcpy(pre, fmt);
	post = strstr(pre, "%s");
	if (post) {
		*post = 0;
		post+=2;
	}

	if (sscanf(info, "%c:%lf:%lf:%lf", &on, &min, &def, &max) != 4) {
		min = 0.0;
		def = 0.0;
		max = 1.0;
	}

	if (service->url
	    && (enable = (val = camel_url_get_param(service->url, name)) != NULL) )
		def = strtod(val, NULL);
	else
		enable = (on == 'y');

	hbox = gtk_hbox_new(FALSE, 0);
	check = g_object_new(gtk_check_button_get_type(), "label", pre, "active", enable, NULL);
	spin = gtk_spin_button_new((GtkAdjustment *)gtk_adjustment_new(def, min, max, 1, 1, 1), 1, 0);
	if (post)
		label = gtk_label_new(post);
	gtk_box_pack_start((GtkBox *)hbox, check, FALSE, TRUE, 0);
	gtk_box_pack_start((GtkBox *)hbox, spin, FALSE, TRUE, 0);
	if (label)
		gtk_box_pack_start((GtkBox *)hbox, label, FALSE, TRUE, 4);

	g_object_set_data((GObject *)spin, "option-name", (void *)name);
	g_object_set_data((GObject *)check, "option-name", (void *)name);
	g_object_set_data((GObject *)check, "option-target", (void *)spin);

	g_signal_connect(spin, "value_changed", G_CALLBACK(emae_option_checkspin_changed), service);
	g_signal_connect(check, "toggled", G_CALLBACK(emae_option_checkspin_check_changed), service);

	return hbox;
}

static GtkWidget *
emae_receive_options_extra_item(EConfig *ec, EConfigItem *item, struct _GtkWidget *parent, struct _GtkWidget *old, void *data)
{
	EMAccountEditor *emae = data;
	GtkWidget *w, *l;
	CamelProviderConfEntry *entries;
	GtkWidget *table, *depw;
	GSList *depl = NULL;
	EMAccountEditorService *service = &emae->priv->source;
	int row, i;
	GHashTable *extra;

	if (emae->priv->extra_widgets) {
		g_slist_foreach(emae->priv->extra_widgets, (GFunc)gtk_widget_destroy, NULL);
		g_slist_free(emae->priv->extra_widgets);
		emae->priv->extra_widgets = NULL;
	}

	if (emae->priv->source.provider == NULL)
		return NULL;

	entries = emae->priv->source.provider->extra_conf;
	for (i=0;entries && entries[i].type != CAMEL_PROVIDER_CONF_END;i++)
		if (entries[i].type == CAMEL_PROVIDER_CONF_SECTION_START
		    && entries[i].name
		    && strcmp(entries[i].name, item->user_data) == 0)
			goto section;

	return NULL;
section:
	printf("Building extra section '%s'\n", item->path);

	table = gtk_table_new(1, 1, FALSE);
	extra = g_hash_table_new(g_str_hash, g_str_equal);
	row = ((GtkTable *)parent)->nrows;

	for (;entries[i].type != CAMEL_PROVIDER_CONF_END && entries[i].type != CAMEL_PROVIDER_CONF_SECTION_END;i++) {
		if (entries[i].depname) {
			depw = g_hash_table_lookup(extra, entries[i].depname);
			if (depw)
				depl = g_object_steal_data((GObject *)depw, "dependent-list");
		} else
			depw = NULL;

		switch (entries[i].type) {
		case CAMEL_PROVIDER_CONF_SECTION_START:
		case CAMEL_PROVIDER_CONF_SECTION_END:
			break;
		case CAMEL_PROVIDER_CONF_LABEL:
			break;
		case CAMEL_PROVIDER_CONF_CHECKBOX:
			w = emae_option_toggle(service, entries[i].text, entries[i].name, atoi(entries[i].value));
			gtk_table_attach((GtkTable *)table, w, 0, 2, row, row+1, GTK_EXPAND|GTK_FILL, 0, 0, 0);
			g_hash_table_insert(extra, entries[i].name, w);
			if (depw)
				depl = g_slist_prepend(depl, w);
			row++;
			break;
		case CAMEL_PROVIDER_CONF_ENTRY:
			l = g_object_new(gtk_label_get_type(), "label", entries[i].text, "xalign", 0.0, NULL);
			w = emae_option_entry(service, entries[i].name, entries[i].value);
			gtk_table_attach((GtkTable *)table, l, 0, 1, row, row+1, GTK_FILL, 0, 0, 0);
			gtk_table_attach((GtkTable *)table, w, 1, 2, row, row+1, GTK_EXPAND|GTK_FILL, 0, 0, 0);
			if (depw) {
				depl = g_slist_prepend(depl, w);
				depl = g_slist_prepend(depl, l);
			}
			row++;
			break;
		case CAMEL_PROVIDER_CONF_CHECKSPIN:
			w = emae_option_checkspin(service, entries[i].name, entries[i].text, entries[i].value);
			gtk_table_attach((GtkTable *)table, w, 0, 2, row, row+1, GTK_EXPAND|GTK_FILL, 0, 0, 0);
			if (depw)
				depl = g_slist_prepend(depl, w);
			row++;
			break;
		default:
			break;
		}

		if (depw && depl) {
			GSList *n = depl;
			int act = gtk_toggle_button_get_active((GtkToggleButton *)depw);

			g_object_set_data_full((GObject *)depw, "dependent-list", depl, (GDestroyNotify)g_slist_free);
			for (n=depl;n;n=g_slist_next(n))
				gtk_widget_set_sensitive((GtkWidget *)n->data, act);
		}
	}

	g_hash_table_destroy(extra);
	gtk_widget_show_all(table);

	gtk_box_pack_start((GtkBox *)parent, table, FALSE, TRUE, 0);

	return table;
}

static GtkWidget *
emae_send_page(EConfig *ec, EConfigItem *item, struct _GtkWidget *parent, struct _GtkWidget *old, void *data)
{
	EMAccountEditor *emae = data;
	EMAccountEditorPrivate *gui = emae->priv;
	GtkWidget *w;

	if (old)
		return old;

	/* Transport */
	gui->transport.type = CAMEL_PROVIDER_TRANSPORT;
	emae_setup_service(emae, &gui->transport);

	w = glade_xml_get_widget(gui->xml, item->label);
	if (((EConfig *)gui->config)->type == E_CONFIG_DRUID) {
		GtkWidget *page = glade_xml_get_widget(gui->druidxml, "transport_page");

		/* need to set packing? */
		gtk_widget_reparent(w, ((GnomeDruidPageStandard *)page)->vbox);
	}

	return w;
}

static GtkWidget *
emae_defaults_page(EConfig *ec, EConfigItem *item, struct _GtkWidget *parent, struct _GtkWidget *old, void *data)
{
	EMAccountEditor *emae = data;
	EMAccountEditorPrivate *gui = emae->priv;

	if (old)
		return old;

	/* Special folders */
	gui->drafts_folder_button = (GtkButton *)emae_account_folder(emae, "drafts_button", E_ACCOUNT_DRAFTS_FOLDER_URI, MAIL_COMPONENT_FOLDER_DRAFTS);
	gui->sent_folder_button = (GtkButton *)emae_account_folder(emae, "sent_button", E_ACCOUNT_SENT_FOLDER_URI, MAIL_COMPONENT_FOLDER_SENT);

	/* Special Folders "Reset Defaults" button */
	gui->restore_folders_button = (GtkButton *)glade_xml_get_widget (gui->xml, "default_folders_button");
	g_signal_connect (gui->restore_folders_button, "clicked", G_CALLBACK (default_folders_clicked), emae);
	
	/* Always Cc/Bcc */
	emae_account_toggle(emae, "always_cc", E_ACCOUNT_CC_ALWAYS);
	emae_account_entry(emae, "cc_addrs", E_ACCOUNT_CC_ADDRS);
	emae_account_toggle(emae, "always_bcc", E_ACCOUNT_BCC_ALWAYS);
	emae_account_entry(emae, "bcc_addrs", E_ACCOUNT_BCC_ADDRS);

	gtk_widget_set_sensitive((GtkWidget *)gui->drafts_folder_button, e_account_writable(emae->account, E_ACCOUNT_DRAFTS_FOLDER_URI));
	gtk_widget_set_sensitive((GtkWidget *)gui->sent_folder_button, e_account_writable(emae->account, E_ACCOUNT_SENT_FOLDER_URI));
	gtk_widget_set_sensitive((GtkWidget *)gui->restore_folders_button,
				 e_account_writable(emae->account, E_ACCOUNT_SENT_FOLDER_URI)
				 || e_account_writable(emae->account, E_ACCOUNT_DRAFTS_FOLDER_URI));
	
	return glade_xml_get_widget(gui->xml, item->label);
}

static GtkWidget *
emae_security_page(EConfig *ec, EConfigItem *item, struct _GtkWidget *parent, struct _GtkWidget *old, void *data)
{
	EMAccountEditor *emae = data;
	EMAccountEditorPrivate *gui = emae->priv;

	if (old)
		return old;

	/* Security */
	emae_account_entry(emae, "pgp_key", E_ACCOUNT_PGP_KEY);
	emae_account_toggle(emae, "pgp_encrypt_to_self", E_ACCOUNT_PGP_ENCRYPT_TO_SELF);
	emae_account_toggle(emae, "pgp_always_sign", E_ACCOUNT_PGP_ALWAYS_SIGN);
	emae_account_toggle(emae, "pgp_no_imip_sign", E_ACCOUNT_PGP_NO_IMIP_SIGN);
	emae_account_toggle(emae, "pgp_always_trust", E_ACCOUNT_PGP_ALWAYS_TRUST);
	
#if defined (HAVE_NSS)
	/* TODO: this should handle its entry separately? */
	gui->smime_sign_key = emae_account_entry(emae, "smime_sign_key", E_ACCOUNT_SMIME_SIGN_KEY);
	gui->smime_sign_key_select = (GtkButton *)glade_xml_get_widget (gui->xml, "smime_sign_key_select");
	gui->smime_sign_key_clear = (GtkButton *)glade_xml_get_widget (gui->xml, "smime_sign_key_clear");
	g_signal_connect(gui->smime_sign_key_select, "clicked", G_CALLBACK(smime_sign_key_select), emae);
	g_signal_connect(gui->smime_sign_key_clear, "clicked", G_CALLBACK(smime_sign_key_clear), emae);

	gui->smime_sign_default = emae_account_toggle(emae, "smime_sign_default", E_ACCOUNT_SMIME_SIGN_DEFAULT);

	gui->smime_encrypt_key = emae_account_entry(emae, "smime_encrypt_key", E_ACCOUNT_SMIME_ENCRYPT_KEY);
	gui->smime_encrypt_key_select = (GtkButton *)glade_xml_get_widget (gui->xml, "smime_encrypt_key_select");
	gui->smime_encrypt_key_clear = (GtkButton *)glade_xml_get_widget (gui->xml, "smime_encrypt_key_clear");
	g_signal_connect(gui->smime_encrypt_key_select, "clicked", G_CALLBACK(smime_encrypt_key_select), emae);
	g_signal_connect(gui->smime_encrypt_key_clear, "clicked", G_CALLBACK(smime_encrypt_key_clear), emae);

	gui->smime_encrypt_default = emae_account_toggle(emae, "smime_encrypt_default", E_ACCOUNT_SMIME_ENCRYPT_DEFAULT);
	gui->smime_encrypt_to_self = emae_account_toggle(emae, "smime_encrypt_to_self", E_ACCOUNT_SMIME_ENCRYPT_TO_SELF);
	smime_changed(emae);
#else
	{
		/* Since we don't have NSS, hide the S/MIME config options */
		GtkWidget *frame;
		
		frame = glade_xml_get_widget (gui->xml, "smime_vbox");
		gtk_widget_destroy (frame);
	}
#endif /* HAVE_NSS */

	return glade_xml_get_widget(gui->xml, item->label);
}

static GtkWidget *
emae_widget_glade(EConfig *ec, EConfigItem *item, struct _GtkWidget *parent, struct _GtkWidget *old, void *data)
{
	EMAccountEditor *emae = data;

	if (old)
		return old;

	printf("getting widget '%s' = %p\n", item->label, glade_xml_get_widget(emae->priv->xml, item->label));

	return glade_xml_get_widget(emae->priv->xml, item->label);
}

/* plugin meta-data for "com.novell.evolution.mail.config.accountEditor" */
static EMConfigItem emae_editor_items[] = {
	{ E_CONFIG_BOOK, "", "account_editor_notebook", emae_widget_glade },
	{ E_CONFIG_PAGE, "00.identity", "vboxIdentityBorder", emae_identity_page },
	{ E_CONFIG_SECTION, "00.identity/00.name", "account_vbox", emae_widget_glade },
	/* table not vbox: { E_CONFIG_SECTION, "00.identity/10.required", "identity_required_table", emae_widget_glade }, */
	/* table not vbox: { E_CONFIG_SECTION, "00.identity/20.info", "identity_optional_table", emae_widget_glade }, */

	{ E_CONFIG_PAGE, "10.receive", "vboxSourceBorder", emae_receive_page },
	/* table not vbox: { E_CONFIG_SECTION, "10.receive/00.type", "source_type_table", emcp_widget_glade }, */
	/* table not vbox: { E_CONFIG_SECTION, "10.receive/10.config", "table13", emcp_widget_glade }, */
	{ E_CONFIG_SECTION, "10.receive/20.security", "vbox181", emae_widget_glade },
	{ E_CONFIG_SECTION, "10.receive/30.auth", "vbox179", emae_widget_glade },

	{ E_CONFIG_PAGE, "20.receive_options", "vboxExtraTableBorder", emae_receive_options_page },
	/* the structure of this is fucked, needs fixing */
	/* table not vbox: { E_CONFIG_SECTION, "20.receive_options/00.mailcheck", "extra_mailcheck_table", emcp_widget_glade }, */
	/* table not vbox: { E_CONFIG_SECTION, "20.receive_options/10.extra", "extra_table", emcp_widget_glade }, */

	{ E_CONFIG_PAGE, "30.send", "vboxTransportBorder", emae_send_page },
	/* table not vbox: { E_CONFIG_SECTION, "30.send/00.type", "transport_type_table", emcp_widget_glade }, */
	{ E_CONFIG_SECTION, "30.send/10.config", "vbox12", emae_widget_glade },
	{ E_CONFIG_SECTION, "30.send/20.security", "vbox183", emae_widget_glade },
	{ E_CONFIG_SECTION, "30.send/30.auth", "vbox61", emae_widget_glade },

	{ E_CONFIG_PAGE, "40.defaults", "vboxFoldersBorder", emae_defaults_page },
	{ E_CONFIG_SECTION, "40.defaults/00.folders", "vbox184", emae_widget_glade },
	/* table not vbox: { E_CONFIG_SECTION, "40.defaults/10.composing", "table8", emae_widget_glade }, */

	{ E_CONFIG_PAGE, "50.security", "vboxSecurityBorder", emae_security_page },
	/* 1x1 table(!) not vbox: { E_CONFIG_SECTION, "50.security/00.gpg", "table19", emae_widget_glade }, */
	/* table not vbox: { E_CONFIG_SECTION, "50.security/10.smime", "smime_table", emae_widget_glade }, */
	{ 0 },
};

static GtkWidget *
emae_widget_druid_glade(EConfig *ec, EConfigItem *item, struct _GtkWidget *parent, struct _GtkWidget *old, void *data)
{
	EMAccountEditor *emae = data;

	if (old)
		return old;

	printf("getting widget '%s' = %p\n", item->label, glade_xml_get_widget(emae->priv->druidxml, item->label));

	return glade_xml_get_widget(emae->priv->druidxml, item->label);
}

/* plugin meta-data for "com.novell.evolution.mail.config.accountDruid" */
static EMConfigItem emae_druid_items[] = {
	{ E_CONFIG_DRUID, "Evolution Account Wizard", "account_druid", emae_widget_druid_glade },
	{ E_CONFIG_PAGE, "00.identity", "vboxIdentityBorder", emae_identity_page },
	{ E_CONFIG_SECTION, "00.identity/00.name", "account_vbox", emae_widget_glade },
	/* table not vbox: { E_CONFIG_SECTION, "00.identity/10.required", "identity_required_table", emae_widget_glade }, */
	/* table not vbox: { E_CONFIG_SECTION, "00.identity/20.info", "identity_optional_table", emae_widget_glade }, */

	{ E_CONFIG_PAGE, "10.receive", "vboxSourceBorder", emae_receive_page },
	/* table not vbox: { E_CONFIG_SECTION, "10.receive/00.type", "source_type_table", emcp_widget_glade }, */
	/* table not vbox: { E_CONFIG_SECTION, "10.receive/10.config", "table13", emcp_widget_glade }, */
	{ E_CONFIG_SECTION, "10.receive/20.security", "vbox181", emae_widget_glade },
	{ E_CONFIG_SECTION, "10.receive/30.auth", "vbox179", emae_widget_glade },

	{ E_CONFIG_PAGE, "20.receive_options", "vboxExtraTableBorder", emae_receive_options_page },
	/* the structure of this is fucked, needs fixing */
	/* table not vbox: { E_CONFIG_SECTION, "20.receive_options/00.mailcheck", "extra_mailcheck_table", emcp_widget_glade }, */
	/* table not vbox: { E_CONFIG_SECTION, "20.receive_options/10.extra", "extra_table", emcp_widget_glade }, */

	{ E_CONFIG_PAGE, "30.send", "vboxTransportBorder", emae_send_page },
	/* table not vbox: { E_CONFIG_SECTION, "30.send/00.type", "transport_type_table", emcp_widget_glade }, */
	{ E_CONFIG_SECTION, "30.send/10.config", "vbox12", emae_widget_glade },
	{ E_CONFIG_SECTION, "30.send/20.security", "vbox183", emae_widget_glade },
	{ E_CONFIG_SECTION, "30.send/30.auth", "vbox61", emae_widget_glade },

	{ 0 },
};

static void
emae_free(EConfig *ec, GSList *items, void *data)
{
	g_slist_free(items);
}

static void
emae_free_auto(EConfig *ec, GSList *items, void *data)
{
	GSList *l, *n;

	for (l=items;l;) {
		EMConfigItem *item = l->data;

		n = g_slist_next(l);
		g_free(item->path);
		g_free(item);
		g_slist_free_1(l);
		l = n;
	}
}

static gboolean
emae_service_complete(EMAccountEditor *emae, EMAccountEditorService *service)
{
	CamelURL *url;
	int ok = TRUE;
	const char *uri;

	uri = e_account_get_string(emae->account, emae_service_info[service->type].account_uri_key);
	if (uri == NULL || (url = camel_url_new(uri, NULL)) == NULL)
		return FALSE;

	if (CAMEL_PROVIDER_NEEDS(service->provider, CAMEL_URL_PART_HOST)
	    && (url->host == NULL || url->host[0] == 0))
		ok = FALSE;

	if (ok
	    && CAMEL_PROVIDER_NEEDS(service->provider, CAMEL_URL_PART_HOST)
	    && (url->user == NULL || url->user[0] == 0))
		ok = FALSE;

	if (ok
	    && CAMEL_PROVIDER_NEEDS(service->provider, CAMEL_URL_PART_PATH)
	    && (url->path == NULL || url->path[0] == 0))
		ok = FALSE;

	camel_url_free(url);

	return ok;
}

static gboolean
emae_check_complete(EConfig *ec, const char *pageid, void *data)
{
	EMAccountEditor *emae = data;
	int ok = TRUE;
	const char *tmp;

	if (pageid == NULL || !strcmp(pageid, "00.identity")) {
		/* TODO: check the account name is set, and unique in the account list */
		ok = (tmp = e_account_get_string(emae->account, E_ACCOUNT_ID_NAME))
			&& tmp[0]
			&& (tmp = e_account_get_string(emae->account, E_ACCOUNT_ID_ADDRESS))
			&& tmp[0]
			&& ((tmp = e_account_get_string(emae->account, E_ACCOUNT_ID_REPLY_TO)) == NULL
			    || is_email(tmp));
	}

	if (ok && (pageid == NULL || !strcmp(pageid, "10.receive")))
		ok = emae_service_complete(emae, &emae->priv->source);

	if (ok && (pageid == NULL || !strcmp(pageid, "30.send")))
		ok = emae_service_complete(emae, &emae->priv->transport);

	return ok;
}

void
em_account_editor_construct(EMAccountEditor *emae, EAccount *account, em_account_editor_t type)
{
	EMAccountEditorPrivate *gui = emae->priv;
	int i, index;
	GSList *l;
	GList *prov;
	EMConfig *ec;
	EMConfigTargetAccount *target;
	GHashTable *have;
	EConfigItem *items;

	emae->type = type;
	emae->account = account;
	g_object_ref(account);
	gui->xml = glade_xml_new(EVOLUTION_GLADEDIR "/mail-config.glade", "account_editor_notebook", NULL);
	if (type == EMAE_DRUID)
		gui->druidxml = glade_xml_new(EVOLUTION_GLADEDIR "/mail-config.glade", "account_druid", NULL);

	/* sort the providers, remote first */
	gui->providers = g_list_sort(camel_provider_list(TRUE), (GCompareFunc)provider_compare);

	if (type == EMAE_NOTEBOOK) {
		ec = em_config_new(E_CONFIG_BOOK, "com.novell.evolution.mail.config.accountEditor");
		items = emae_editor_items;
	} else {
		ec = em_config_new(E_CONFIG_DRUID, "com.novell.evolution.mail.config.accountDruid");
		items = emae_druid_items;
	}

	emae->config = gui->config = ec;
	l = NULL;
	for (i=0;items[i].path;i++)
		l = g_slist_prepend(l, &items[i]);
	e_config_add_items((EConfig *)ec, l, NULL, NULL, emae_free, emae);

	/* This is kinda yuck, we're dynamically mapping from the 'old style' extensibility api to the new one */
	l = NULL;
	have = g_hash_table_new(g_str_hash, g_str_equal);
	index = 10;
	for (prov=gui->providers;prov;prov=g_list_next(prov)) {
		CamelProviderConfEntry *entries = ((CamelProvider *)prov->data)->extra_conf;

		for (i=0;entries && entries[i].type != CAMEL_PROVIDER_CONF_END;i++) {
			EMConfigItem *item;
			char *name = entries[i].name;

			if (entries[i].type != CAMEL_PROVIDER_CONF_SECTION_START
			    || name == NULL
			    || g_hash_table_lookup(have, name))
				continue;

			item = g_malloc0(sizeof(*item));
			item->type = E_CONFIG_SECTION;
			item->path = g_strdup_printf("20.receive_options/%02d.%s", index, name?name:"unnamed");
			item->label = entries[i].text;

			l = g_slist_prepend(l, item);

			item = g_malloc0(sizeof(*item));
			item->type = E_CONFIG_ITEM;
			item->path = g_strdup_printf("20.receive_options/%02d.%s/00.auto", index, name?name:"unnamed");
			item->factory = emae_receive_options_extra_item;
			item->user_data = entries[i].name;

			l = g_slist_prepend(l, item);

			index += 10;
			g_hash_table_insert(have, entries[i].name, have);
		}
	}
	g_hash_table_destroy(have);
	e_config_add_items((EConfig *)ec, l, NULL, NULL, emae_free_auto, emae);

	e_config_add_page_check((EConfig *)ec, NULL, emae_check_complete, emae);

	target = em_config_target_new_account(ec, account);
	emae->editor = e_config_create_widget((EConfig *)ec, (EConfigTarget *)target);
	gtk_widget_show((GtkWidget *)emae->editor);

	/* FIXME: need to hook onto destroy as required */
}

static void
save_service (EMAccountEditorService *gsvc, GHashTable *extra_config, EAccountService *service)
{
	CamelURL *url;
	const char *str;
	
	if (!gsvc->provider) {
		g_free (service->url);
		service->url = NULL;
		return;
	}
	
	url = g_new0 (CamelURL, 1);
	url->protocol = g_strdup (gsvc->provider->protocol);
	
	if (CAMEL_PROVIDER_ALLOWS (gsvc->provider, CAMEL_URL_PART_USER)) {
		str = gtk_entry_get_text (gsvc->username);
		if (str && *str)
			url->user = g_strstrip (g_strdup (str));
	}
	
	if (CAMEL_PROVIDER_ALLOWS (gsvc->provider, CAMEL_URL_PART_AUTH) &&
	    GTK_WIDGET_IS_SENSITIVE (gsvc->authtype) && gsvc->authitem && url->user) {
		CamelServiceAuthType *authtype;
		
		authtype = g_object_get_data(G_OBJECT(gsvc->authitem), "authtype");
		if (authtype && authtype->authproto && *authtype->authproto)
			url->authmech = g_strdup (authtype->authproto);
		
		service->save_passwd = gtk_toggle_button_get_active (gsvc->remember);
	}
	
	if (CAMEL_PROVIDER_ALLOWS (gsvc->provider, CAMEL_URL_PART_HOST)) {
		char *pport;
		
		str = gtk_entry_get_text (gsvc->hostname);
		if (str && *str) {
			pport = strchr (str, ':');
			if (pport) {
				url->host = g_strndup (str, pport - str);
				url->port = atoi (pport + 1);
			} else
				url->host = g_strdup (str);
		}
	}
	
	if (CAMEL_PROVIDER_ALLOWS (gsvc->provider, CAMEL_URL_PART_PATH)) {
		str = gtk_entry_get_text (gsvc->path);
		if (str && *str)
			url->path = g_strdup (str);
	}
	
	if (gsvc->provider->flags & CAMEL_PROVIDER_SUPPORTS_SSL) {
		const char *use_ssl;
		
		use_ssl = g_object_get_data(G_OBJECT(gsvc->ssl_selected), "use_ssl");
		
		/* set the value to either "always" or "when-possible"
		   but don't bother setting it for "never" */
		if (strcmp (use_ssl, "never"))
			camel_url_set_param (url, "use_ssl", use_ssl);
	}
	
	if (extra_config)
		extract_values (gsvc, extra_config, url);
	
	g_free (service->url);
	service->url = camel_url_to_string (url, 0);
	
	/* Temporary until keep_on_server moves into the POP provider */
	if (camel_url_get_param (url, "keep_on_server"))
		service->keep_on_server = TRUE;
	
	camel_url_free (url);
}

static void
add_new_store (char *uri, CamelStore *store, void *user_data)
{
	MailComponent *component = mail_component_peek ();
	EAccount *account = user_data;
	
	if (store == NULL)
		return;
	
	mail_component_add_store (component, store, account->name);
}

gboolean
em_account_editor_save (EMAccountEditor *emae)
{
#if 0
	EMAccountEditorPrivate *gui = emae->priv;
	EAccount *account, *new;
	CamelProvider *provider = NULL;
	gboolean is_new = FALSE;
	const char *new_name;
	gboolean is_storage;
	
	if (!em_account_editor_identity_complete (gui, NULL) ||
	    !em_account_editor_source_complete (gui, NULL) ||
	    !em_account_editor_transport_complete (gui, NULL) ||
	    !em_account_editor_management_complete (gui, NULL))
		return FALSE;
	
	new = emae->account;
	
	/* this would happen at an inconvenient time in the druid,
	 * but the druid performs its own check so this can't happen
	 * here. */
	
	new_name = gtk_entry_get_text (emae->account_name);
	account = mail_config_get_account_by_name (new_name);
	
	if (account && account != new) {
		e_error_run((GtkWindow *)gtk_widget_get_toplevel((GtkWidget *)emae->account_name),
			    "mail:account-notunique", NULL);
		return FALSE;
	}
	
	account = new;
	
	new = e_account_new ();
	new->name = g_strdup (new_name);
	new->enabled = account->enabled;
	
	/* construct the identity */
	new->id->name = g_strdup (gtk_entry_get_text (gui->full_name));
	new->id->address = g_strdup (gtk_entry_get_text (gui->email_address));
	new->id->reply_to = g_strdup (gtk_entry_get_text (gui->reply_to));
	new->id->organization = g_strdup (gtk_entry_get_text (gui->organization));
	
	/* signatures */
	new->id->sig_uid = g_strdup (gui->sig_uid);
	
	/* source */
	save_service (&gui->source, gui->extra_config, new->source);
	if (new->source->url)
		provider = camel_provider_get(new->source->url, NULL);
	
	new->source->auto_check = gtk_toggle_button_get_active (gui->source_auto_check);
	if (new->source->auto_check)
		new->source->auto_check_time = gtk_spin_button_get_value_as_int (gui->source_auto_check_min);
	
	/* transport */
	if (CAMEL_PROVIDER_IS_STORE_AND_TRANSPORT (gui->transport.provider)) {
		/* The transport URI is the same as the source URI. */
		save_service (&gui->source, gui->extra_config, new->transport);
	} else
		save_service (&gui->transport, NULL, new->transport);
	
	/* Check to make sure that the Drafts folder uri is "valid" before assigning it */
	if (mail_config_get_account_by_source_url (gui->drafts_folder_uri) ||
		!strncmp (gui->drafts_folder_uri, "mbox:", 5)) {
		new->drafts_folder_uri = em_uri_from_camel (gui->drafts_folder_uri);
	} else {
		/* assign defaults - the uri is unknown to us (probably pointed to an old source url) */
		new->drafts_folder_uri = em_uri_from_camel(mail_component_get_folder_uri(NULL, MAIL_COMPONENT_FOLDER_DRAFTS));
	}
	
	/* Check to make sure that the Sent folder uri is "valid" before assigning it */
	if (mail_config_get_account_by_source_url (gui->sent_folder_uri) ||
		!strncmp (gui->sent_folder_uri, "mbox:", 5)) {
		new->sent_folder_uri = em_uri_from_camel (gui->sent_folder_uri);
	} else {
		/* assign defaults - the uri is unknown to us (probably pointed to an old source url) */
		new->sent_folder_uri = em_uri_from_camel(mail_component_get_folder_uri(NULL, MAIL_COMPONENT_FOLDER_SENT));
	}
	
	new->always_cc = gtk_toggle_button_get_active (gui->always_cc);
	new->cc_addrs = g_strdup (gtk_entry_get_text (gui->cc_addrs));
	new->always_bcc = gtk_toggle_button_get_active (gui->always_bcc);
	new->bcc_addrs = g_strdup (gtk_entry_get_text (gui->bcc_addrs));
	
	new->pgp_key = g_strdup (gtk_entry_get_text (gui->pgp_key));
	new->pgp_encrypt_to_self = gtk_toggle_button_get_active (gui->pgp_encrypt_to_self);
	new->pgp_always_sign = gtk_toggle_button_get_active (gui->pgp_always_sign);
	new->pgp_no_imip_sign = gtk_toggle_button_get_active (gui->pgp_no_imip_sign);
	new->pgp_always_trust = gtk_toggle_button_get_active (gui->pgp_always_trust);
	
#if defined (HAVE_NSS)
	new->smime_sign_default = gtk_toggle_button_get_active (gui->smime_sign_default);
	new->smime_sign_key = g_strdup (gtk_entry_get_text (gui->smime_sign_key));
	
	new->smime_encrypt_default = gtk_toggle_button_get_active (gui->smime_encrypt_default);
	new->smime_encrypt_key = g_strdup (gtk_entry_get_text (gui->smime_encrypt_key));
	new->smime_encrypt_to_self = gtk_toggle_button_get_active (gui->smime_encrypt_to_self);
#endif /* HAVE_NSS */
	
	is_storage = provider && (provider->flags & CAMEL_PROVIDER_IS_STORAGE);
	
	if (!mail_config_find_account (account)) {
		/* this is a new account so add it to our account-list */
		is_new = TRUE;
	}
	
	/* update the old account with the new settings */
	e_account_import (account, new);
	g_object_unref (new);
	
	if (is_new) {
		mail_config_add_account (account);
		
		/* if the account provider is something we can stick
		   in the folder-tree and not added by some other
		   component, then get the CamelStore and add it to
		   the folder-tree */
		if (is_storage && account->enabled)
			mail_get_store (account->source->url, NULL, add_new_store, account);
	} else {
		e_account_list_change (mail_config_get_accounts (), account);
	}
	
	if (gtk_toggle_button_get_active (gui->default_account))
		mail_config_set_default_account (account);
	
	mail_config_save_accounts ();
	
	mail_autoreceive_setup ();
#endif	
	return TRUE;
}

void
em_account_editor_destroy (EMAccountEditor *emae)
{
#if 0
	EMAccountEditorPrivate *gui = emae->priv;
	ESignatureList *signatures;
	
	g_object_unref (gui->xml);
	g_object_unref (emae->account);
	
	signatures = mail_config_get_signatures ();
	g_signal_handler_disconnect (signatures, gui->sig_added_id);
	g_signal_handler_disconnect (signatures, gui->sig_removed_id);
	g_signal_handler_disconnect (signatures, gui->sig_changed_id);
	
	if (gui->extra_config)
		g_hash_table_destroy (gui->extra_config);
	
	g_free (gui->drafts_folder_uri);
	g_free (gui->sent_folder_uri);
	g_free (gui);
#endif
}
