/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors:
 *    Dan Winship <danw@ximian.com>
 *    Jeffrey Stedfast <fejj@ximian.com>
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

#include <string.h>
#include <stdarg.h>

#include <gconf/gconf.h>
#include <gconf/gconf-client.h>

#include <e-util/e-account-list.h>
#include <e-util/e-dialog-utils.h>

#include "evolution-folder-selector-button.h"
#include "mail-account-gui.h"
#include "mail-session.h"
#include "mail-send-recv.h"
#include "mail-signature-editor.h"
#include "mail-composer-prefs.h"
#include "mail-config.h"
#include "mail-ops.h"
#include "mail-mt.h"
#include "mail.h"

#define d(x)

extern char *default_drafts_folder_uri, *default_sent_folder_uri;
extern EvolutionShellClient *global_shell_client;

static void save_service (MailAccountGuiService *gsvc, GHashTable *extra_conf, EAccountService *service);
static void service_changed (GtkEntry *entry, gpointer user_data);

struct {
	char *label;
	char *value;
} ssl_options[] = {
	{ N_("Always"), "always" },
	{ N_("Whenever Possible"), "when-possible" },
	{ N_("Never"), "never" }
};

static int num_ssl_options = sizeof (ssl_options) / sizeof (ssl_options[0]);

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

gboolean
mail_account_gui_identity_complete (MailAccountGui *gui, GtkWidget **incomplete)
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

static void
auto_detected_foreach (gpointer key, gpointer value, gpointer user_data)
{
	g_free (key);
	g_free (value);
}

static gboolean
service_complete (MailAccountGuiService *service, GHashTable *extra_config, GtkWidget **incomplete)
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

gboolean
mail_account_gui_source_complete (MailAccountGui *gui, GtkWidget **incomplete)
{
	return service_complete (&gui->source, gui->extra_config, incomplete);
}

void
mail_account_gui_auto_detect_extra_conf (MailAccountGui *gui)
{
	MailAccountGuiService *service = &gui->source;
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
			if (!entries[i].name)
				continue;
			
			value = g_hash_table_lookup (auto_detected, entries[i].name);
			if (!value)
				continue;
			
			switch (entries[i].type) {
			case CAMEL_PROVIDER_CONF_CHECKBOX:
				toggle = g_hash_table_lookup (gui->extra_config, entries[i].name);
				gtk_toggle_button_set_active (toggle, atoi (value));
				break;
				
			case CAMEL_PROVIDER_CONF_ENTRY:
				entry = g_hash_table_lookup (gui->extra_config, entries[i].name);
				if (value)
					gtk_entry_set_text (entry, value);
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
			}
			break;
			default:
				break;
			}
		}
		
		g_hash_table_foreach (auto_detected, auto_detected_foreach, NULL);
		g_hash_table_destroy (auto_detected);
	}
}

gboolean
mail_account_gui_transport_complete (MailAccountGui *gui, GtkWidget **incomplete)
{
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
	
	return TRUE;
}

gboolean
mail_account_gui_management_complete (MailAccountGui *gui, GtkWidget **incomplete)
{
	const char *text;
	
	text = gtk_entry_get_text (gui->account_name);
	if (text && *text)
		return TRUE;
	
	if (incomplete)
		*incomplete = GTK_WIDGET (gui->account_name);
	
	return FALSE;
}


static void
service_authtype_changed (GtkWidget *widget, gpointer user_data)
{
	MailAccountGuiService *service = user_data;
	CamelServiceAuthType *authtype;
	
	service->authitem = widget;
	authtype = g_object_get_data ((GObject *) widget, "authtype");
	
	gtk_widget_set_sensitive (GTK_WIDGET (service->remember), authtype->need_password);
}

static void
build_auth_menu (MailAccountGuiService *service, GList *all_authtypes,
		 GList *supported_authtypes, gboolean check_supported)
{
	GtkWidget *menu, *item, *first = NULL;
	CamelServiceAuthType *current, *authtype, *sauthtype;
	int history = 0, i;
	GList *l, *s;
	
	if (service->authitem)
		current = g_object_get_data ((GObject *) service->authitem, "authtype");
	else
		current = NULL;
	
	service->authitem = NULL;
	
	menu = gtk_menu_new ();
	
	for (l = all_authtypes, i = 0; l; l = l->next, i++) {
		authtype = l->data;
		
		item = gtk_menu_item_new_with_label (authtype->name);
		for (s = supported_authtypes; s; s = s->next) {
			sauthtype = s->data;
			if (!strcmp (authtype->name, sauthtype->name))
				break;
		}
		
		if (check_supported && !s) {
			gtk_widget_set_sensitive (item, FALSE);
		} else if (current && !strcmp (authtype->name, current->name)) {
			first = item;
			history = i;
		} else if (!first) {
			first = item;
			history = i;
		}
		
		g_object_set_data ((GObject *) item, "authtype", authtype);
		g_signal_connect (item, "activate", G_CALLBACK (service_authtype_changed), service);
		
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
		
		gtk_widget_show (item);
	}
	
	gtk_option_menu_remove_menu (service->authtype);
	gtk_option_menu_set_menu (service->authtype, menu);
	
	if (first) {
		gtk_option_menu_set_history (service->authtype, history);
		g_signal_emit_by_name (first, "activate");
	}
}

static void
transport_provider_set_available (MailAccountGui *gui, CamelProvider *provider,
				  gboolean available)
{
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
	MailAccountGui *gui = user_data;
	GtkWidget *file_entry, *label, *frame, *dwidget = NULL;
	CamelProvider *provider;
	
	provider = g_object_get_data ((GObject *) widget, "provider");
	
	/* If the previously-selected provider has a linked transport,
	 * disable it.
	 */
	if (gui->source.provider &&
	    CAMEL_PROVIDER_IS_STORE_AND_TRANSPORT (gui->source.provider))
		transport_provider_set_available (gui, gui->source.provider, FALSE);
	
	gui->source.provider = provider;
	
	if (provider)
		gtk_label_set_text (gui->source.description, provider->description);
	else
		gtk_label_set_text (gui->source.description, "");
	
	frame = glade_xml_get_widget (gui->xml, "source_frame");
	if (provider) {
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
		if (provider && provider->flags & CAMEL_PROVIDER_SUPPORTS_SSL)
			gtk_widget_show (gui->source.ssl_hbox);
		else
			gtk_widget_hide (gui->source.ssl_hbox);
		gtk_widget_hide (gui->source.no_ssl);
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
	
	mail_account_gui_build_extra_conf (gui, gui && gui->account && gui->account->source ? gui->account->source->url : NULL);
	
	if (provider && CAMEL_PROVIDER_IS_STORE_AND_TRANSPORT (provider))
		transport_provider_set_available (gui, provider, TRUE);
}


static void
transport_needs_auth_toggled (GtkToggleButton *toggle, gpointer data)
{
	MailAccountGui *gui = data;
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
	MailAccountGui *gui = user_data;
	CamelProvider *provider;
	GtkWidget *label, *frame;
	
	provider = g_object_get_data ((GObject *) widget, "provider");
	gui->transport.provider = provider;
	
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
			gtk_widget_grab_focus (GTK_WIDGET (gui->transport.hostname));
			gtk_widget_show (GTK_WIDGET (gui->transport.hostname));
			gtk_widget_show (label);
		} else {
			gtk_widget_hide (GTK_WIDGET (gui->transport.hostname));
			gtk_widget_hide (label);
		}
		
		/* ssl */
#ifdef HAVE_SSL
		if (provider && provider->flags & CAMEL_PROVIDER_SUPPORTS_SSL)
			gtk_widget_show (gui->transport.ssl_hbox);
		else
			gtk_widget_hide (gui->transport.ssl_hbox);
		gtk_widget_hide (gui->transport.no_ssl);
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
	MailAccountGuiService *service = user_data;
	
	gtk_widget_set_sensitive (GTK_WIDGET (service->check_supported),
				  service_complete (service, NULL, NULL));
}

static void
service_check_supported (GtkButton *button, gpointer user_data)
{
	MailAccountGuiService *gsvc = user_data;
	EAccountService *service;
	GList *authtypes = NULL;
	GtkWidget *authitem;
	GtkWidget *window;
	
	service = g_new0 (EAccountService, 1);
	
	/* This is sort of a hack, when checking for supported AUTH
           types we don't want to use whatever authtype is selected
           because it may not be available. */
	authitem = gsvc->authitem;
	gsvc->authitem = NULL;
	
	save_service (gsvc, NULL, service);
	
	gsvc->authitem = authitem;
	
	window = gtk_widget_get_ancestor (GTK_WIDGET (button), GTK_TYPE_WINDOW);
	
	if (mail_config_check_service (service->url, gsvc->provider_type, &authtypes, GTK_WINDOW (window))) {
		build_auth_menu (gsvc, gsvc->provider->authtypes, authtypes, TRUE);
		if (!authtypes) {
			/* provider doesn't support any authtypes */
			gtk_widget_set_sensitive (GTK_WIDGET (gsvc->check_supported), FALSE);
		}
		g_list_free (authtypes);
	}
	
	g_free (service->url);
	g_free (service);
}


static void
toggle_sensitivity (GtkToggleButton *toggle, GtkWidget *widget)
{
	gtk_widget_set_sensitive (widget, gtk_toggle_button_get_active (toggle));
}

static void
setup_toggle (GtkWidget *widget, const char *depname, MailAccountGui *gui)
{
	GtkToggleButton *toggle;
	
	if (!strcmp (depname, "UNIMPLEMENTED")) {
		gtk_widget_set_sensitive (widget, FALSE);
		return;
	}
	
	toggle = g_hash_table_lookup (gui->extra_config, depname);
	g_signal_connect (toggle, "toggled", G_CALLBACK (toggle_sensitivity), widget);
	toggle_sensitivity (toggle, widget);
}

void
mail_account_gui_build_extra_conf (MailAccountGui *gui, const char *url_string)
{
	CamelURL *url;
	GtkWidget *mailcheck_frame, *mailcheck_hbox;
	GtkWidget *hostname_label, *username_label, *path_label;
	GtkWidget *hostname, *username, *path;
	GtkTable *main_table, *cur_table;
	CamelProviderConfEntry *entries;
	GList *children, *child;
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
	main_table = (GtkTable *)glade_xml_get_widget (gui->xml, "extra_table");
	mailcheck_frame = glade_xml_get_widget (gui->xml, "extra_mailcheck_frame");
	children = gtk_container_get_children (GTK_CONTAINER (main_table));
	for (child = children; child; child = child->next) {
		if (child->data != (gpointer)mailcheck_frame) {
			gtk_container_remove (GTK_CONTAINER (main_table),
					      child->data);
		}
	}
	g_list_free (children);
	gtk_table_resize (main_table, 1, 2);
	
	/* Remove any additional mailcheck items. */
	cur_table = (GtkTable *)glade_xml_get_widget (gui->xml, "extra_mailcheck_table");
	mailcheck_hbox = glade_xml_get_widget (gui->xml, "extra_mailcheck_hbox");
	children = gtk_container_get_children (GTK_CONTAINER (cur_table));
	for (child = children; child; child = child->next) {
		if (child->data != (gpointer)mailcheck_hbox) {
			gtk_container_remove (GTK_CONTAINER (cur_table),
					      child->data);
		}
	}
	g_list_free (children);
	gtk_table_resize (cur_table, 1, 2);

	if (!gui->source.provider) {
		gtk_widget_set_sensitive (GTK_WIDGET (main_table), FALSE);
		if (url)
			camel_url_free (url);
		return;
	} else
		gtk_widget_set_sensitive (GTK_WIDGET (main_table), TRUE);
	
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
		switch (entries[i].type) {
		case CAMEL_PROVIDER_CONF_SECTION_START:
		{
			GtkWidget *frame;
			
			if (entries[i].name && !strcmp (entries[i].name, "mailcheck"))
				cur_table = (GtkTable *)glade_xml_get_widget (gui->xml, "extra_mailcheck_table");
			else {
				frame = gtk_frame_new (entries[i].text);
				gtk_container_set_border_width (GTK_CONTAINER (frame), 3);
				gtk_table_attach (main_table, frame, 0, 2,
						  rows, rows + 1,
						  GTK_EXPAND | GTK_FILL, 0, 0, 0);

				cur_table = (GtkTable *)gtk_table_new (0, 2, FALSE);
				rows = 0;
				gtk_table_set_row_spacings (cur_table, 4);
				gtk_table_set_col_spacings (cur_table, 8);
				gtk_container_set_border_width (GTK_CONTAINER (cur_table), 3);

				gtk_container_add (GTK_CONTAINER (frame), GTK_WIDGET (cur_table));
			}
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
				} else if (!strcmp (entries[i].name, "hostname")) {
					gtk_label_set_text_with_mnemonic (GTK_LABEL (hostname_label), entries[i].text);
				} else if (!strcmp (entries[i].name, "path")) {
					gtk_label_set_text_with_mnemonic (GTK_LABEL (path_label), entries[i].text);
				} else {
					/* make a new label */
					label = gtk_label_new (entries[i].text);
					gtk_table_resize (cur_table, cur_table->nrows + 1, 2);
					gtk_table_attach (cur_table, label, 0, 2, rows, rows + 1,
							  GTK_EXPAND | GTK_FILL, 0, 0, 0);
					rows++;
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
				setup_toggle (checkbox, entries[i].depname, gui);
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
				setup_toggle (entry, entries[i].depname, gui);
				setup_toggle (label, entries[i].depname, gui);
			}

			g_hash_table_insert (gui->extra_config, entries[i].name, entry);
						
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
			min = strtod (++data, &data);
			g_return_if_fail (*data == ':');
			def = strtod (++data, &data);
			g_return_if_fail (*data == ':');
			max = strtod (++data, NULL);
			
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
				setup_toggle (checkbox, entries[i].depname, gui);
				setup_toggle (spin, entries[i].depname, gui);
				setup_toggle (label, entries[i].depname, gui);
			}
			break;
		}
		
		case CAMEL_PROVIDER_CONF_END:
			goto done;
		}
	}
	
 done:
	gtk_widget_show_all (GTK_WIDGET (main_table));
	if (url)
		camel_url_free (url);
}

static void
extract_values (MailAccountGuiService *source, GHashTable *extra_config, CamelURL *url)
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
			
		case CAMEL_PROVIDER_CONF_END:
			return;
			
		default:
			break;
		}
	}
}


static void
folder_selected (EvolutionFolderSelectorButton *button,
		 GNOME_Evolution_Folder *corba_folder,
		 gpointer user_data)
{
	char **folder_name = user_data;
	
	g_free (*folder_name);
	*folder_name = g_strdup (corba_folder->physicalUri);
}

static void
default_folders_clicked (GtkButton *button, gpointer user_data)
{
	MailAccountGui *gui = user_data;
	
	/* Drafts folder */
	g_free (gui->drafts_folder_uri);
	gui->drafts_folder_uri = g_strdup (default_drafts_folder_uri);
	evolution_folder_selector_button_set_uri (EVOLUTION_FOLDER_SELECTOR_BUTTON (gui->drafts_folder_button),
						  gui->drafts_folder_uri);
	
	/* Sent folder */
	g_free (gui->sent_folder_uri);
	gui->sent_folder_uri = g_strdup (default_sent_folder_uri);
	evolution_folder_selector_button_set_uri (EVOLUTION_FOLDER_SELECTOR_BUTTON (gui->sent_folder_button),
						  gui->sent_folder_uri);
}

GtkWidget *mail_account_gui_folder_selector_button_new (char *widget_name, char *string1, char *string2, int int1, int int2);

GtkWidget *
mail_account_gui_folder_selector_button_new (char *widget_name,
					     char *string1, char *string2,
					     int int1, int int2)
{
	return (GtkWidget *)g_object_new (EVOLUTION_TYPE_FOLDER_SELECTOR_BUTTON, NULL);
}

static gboolean
setup_service (MailAccountGuiService *gsvc, EAccountService *service)
{
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
	
	return has_auth;
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
ssl_option_activate (GtkWidget *widget, gpointer user_data)
{
	MailAccountGuiService *service = user_data;
	
	service->ssl_selected = widget;
}

static void
construct_ssl_menu (MailAccountGuiService *service)
{
	GtkWidget *menu, *item = NULL;
	int i;
	
	menu = gtk_menu_new ();
	
	for (i = 0; i < num_ssl_options; i++) {
		item = gtk_menu_item_new_with_label (_(ssl_options[i].label));
		g_object_set_data ((GObject *) item, "use_ssl", ssl_options[i].value);
		g_signal_connect (item, "activate", G_CALLBACK (ssl_option_activate), service);
		gtk_widget_show (item);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
	}
	
	gtk_option_menu_remove_menu (service->use_ssl);
	gtk_option_menu_set_menu (service->use_ssl, menu);
	
	gtk_option_menu_set_history (service->use_ssl, i - 1);
	g_signal_emit_by_name (item, "activate", service);
}

static void
clear_menu (GtkWidget *menu)
{
	while (GTK_MENU_SHELL (menu)->children)
		gtk_container_remove (GTK_CONTAINER (menu), GTK_MENU_SHELL (menu)->children->data);
}

static inline int
sig_get_index (MailConfigSignature *sig)
{
	return sig ? sig->id + 2 : 0;
}

static inline int
sig_gui_get_index (MailAccountGui *gui)
{
	if (gui->auto_signature)
		return 1;
	return sig_get_index (gui->def_signature);
}

static void
sig_fill_options (MailAccountGui *gui)
{
	GtkWidget *menu;
	GtkWidget *mi;
	GSList *l;
	MailConfigSignature *sig;
	
	menu = gtk_option_menu_get_menu (GTK_OPTION_MENU (gui->sig_option_menu));
	
	if (menu)
		clear_menu (menu);
	else
		menu = gtk_menu_new ();
	
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_menu_item_new_with_label (_("None")));
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_menu_item_new_with_label (_("Autogenerated")));
	/* gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_menu_item_new_with_label (_("Random"))); */
	
	for (l = mail_config_get_signature_list (); l; l = l->next) {
		sig = l->data;
		mi = gtk_menu_item_new_with_label (sig->name);
		g_object_set_data ((GObject *) mi, "sig", sig);
		gtk_widget_show (mi);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
	}
}

static void
sig_changed (GtkWidget *w, MailAccountGui *gui)
{
	GtkWidget *active;
	int index;
	
	active = gtk_menu_get_active (GTK_MENU (w));
	index = g_list_index (GTK_MENU_SHELL (w)->children, active);
	
	gui->def_signature = (MailConfigSignature *) g_object_get_data(G_OBJECT(active), "sig");
	gui->auto_signature = index == 1 ? TRUE : FALSE;
}

static void
sig_switch_to_list (GtkWidget *w, MailAccountGui *gui)
{
	gtk_window_set_transient_for (GTK_WINDOW (gtk_widget_get_toplevel (w)), NULL);
	gdk_window_raise (GTK_WIDGET (gui->dialog)->window);
	gtk_notebook_set_current_page (GTK_NOTEBOOK (glade_xml_get_widget (gui->dialog->gui, "notebook")), 3);
}

static void
sig_add_new_signature (GtkWidget *w, MailAccountGui *gui)
{
	GConfClient *gconf;
	gboolean send_html;
	GtkWidget *parent;
	
	if (!gui->dialog)
		return;
	
	sig_switch_to_list (w, gui);
	
	gconf = mail_config_get_gconf_client ();
	send_html = gconf_client_get_bool (gconf, "/apps/evolution/mail/composer/send_html", NULL);
	
	parent = gtk_widget_get_toplevel (w);
	parent = GTK_WIDGET_TOPLEVEL (parent) ? parent : NULL;
	
	gui->def_signature = mail_composer_prefs_new_signature ((GtkWindow *) parent, send_html, NULL);
	gui->auto_signature = FALSE;
	
	gtk_option_menu_set_history (GTK_OPTION_MENU (gui->sig_option_menu), sig_gui_get_index (gui));
}

static void
setup_signatures (MailAccountGui *gui)
{
	MailConfigSignature *sig;
	GSList *signatures;
	
	signatures = mail_config_get_signature_list ();
	sig = g_slist_nth_data (signatures, gui->account->id->def_signature);
	
	gui->def_signature = sig;
	gui->auto_signature = gui->account->id->auto_signature;
	gtk_option_menu_set_history (GTK_OPTION_MENU (gui->sig_option_menu), sig_gui_get_index (gui));
}

static void
sig_event_client (MailConfigSigEvent event, MailConfigSignature *sig, MailAccountGui *gui)
{
	switch (event) {
	case MAIL_CONFIG_SIG_EVENT_ADDED: {
		GtkWidget *menu;
		GtkWidget *mi;
		
		d(printf ("accounts ADDED\n"));
		mi = gtk_menu_item_new_with_label (sig->name);
		g_object_set_data ((GObject *) mi, "sig", sig);
		gtk_widget_show (mi);
		menu = gtk_option_menu_get_menu (GTK_OPTION_MENU (gui->sig_option_menu));
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
		
		break;
	}
	case MAIL_CONFIG_SIG_EVENT_NAME_CHANGED: {
		GtkWidget *menu;
		GtkWidget *mi;
		
		d(printf ("gui NAME CHANGED\n"));
		menu = gtk_option_menu_get_menu (GTK_OPTION_MENU (gui->sig_option_menu));
		gtk_widget_ref (menu);
		gtk_option_menu_remove_menu (GTK_OPTION_MENU (gui->sig_option_menu));
		mi = g_list_nth_data (GTK_MENU_SHELL (menu)->children, sig_get_index (sig));
		gtk_label_set_text (GTK_LABEL (GTK_BIN (mi)->child), sig->name);
		gtk_option_menu_set_menu (GTK_OPTION_MENU (gui->sig_option_menu), menu);
		gtk_widget_unref (menu);
		gtk_option_menu_set_history (GTK_OPTION_MENU (gui->sig_option_menu),
					     sig_gui_get_index (gui));
		
		break;
	}
	case MAIL_CONFIG_SIG_EVENT_DELETED: {
		GtkWidget *menu;
		GtkWidget *mi;
		
		d(printf ("gui DELETED\n"));
		
		if (sig == gui->def_signature) {
			gui->def_signature = NULL;
			gui->auto_signature = TRUE;
			gtk_option_menu_set_history (GTK_OPTION_MENU (gui->sig_option_menu),
						     sig_gui_get_index (gui));
		}
		
		menu = gtk_option_menu_get_menu (GTK_OPTION_MENU (gui->sig_option_menu));
		mi = g_list_nth_data (GTK_MENU_SHELL (menu)->children, sig_get_index (sig));
		gtk_container_remove (GTK_CONTAINER (menu), mi);
		
		break;
	}
	default:
		;
	}
}

static void
prepare_signatures (MailAccountGui *gui)
{
	gui->sig_option_menu = glade_xml_get_widget (gui->xml, "sigOption");
	sig_fill_options (gui);
	g_signal_connect (gtk_option_menu_get_menu (GTK_OPTION_MENU (gui->sig_option_menu)),
			  "selection-done", G_CALLBACK(sig_changed), gui);
	
	glade_xml_signal_connect_data (gui->xml, "sigAddNewClicked",
				       G_CALLBACK (sig_add_new_signature), gui);
	
	if (!gui->dialog) {
		gtk_widget_hide (glade_xml_get_widget (gui->xml, "sigLabel"));
		gtk_widget_hide (glade_xml_get_widget (gui->xml, "sigOption"));
		gtk_widget_hide (glade_xml_get_widget (gui->xml, "sigAddNew"));
	} else {
		mail_config_signature_register_client ((MailConfigSignatureClient) sig_event_client, gui);
	}
}

MailAccountGui *
mail_account_gui_new (EAccount *account, MailAccountsTab *dialog)
{
	const char *allowed_types[] = { "mail/*", NULL };
	MailAccountGui *gui;
	GtkWidget *button;
	
	g_object_ref (account);
	
	gui = g_new0 (MailAccountGui, 1);
	gui->account = account;
	gui->dialog = dialog;
	gui->xml = glade_xml_new (EVOLUTION_GLADEDIR "/mail-config.glade", NULL, NULL);
	
	/* Management */
	gui->account_name = GTK_ENTRY (glade_xml_get_widget (gui->xml, "management_name"));
	gui->default_account = GTK_TOGGLE_BUTTON (glade_xml_get_widget (gui->xml, "management_default"));
	if (account->name)
		gtk_entry_set_text (gui->account_name, account->name);
	if (!mail_config_get_default_account ()
	    || (account == mail_config_get_default_account ()))
		gtk_toggle_button_set_active (gui->default_account, TRUE);
	
	/* Identity */
	gui->full_name = GTK_ENTRY (glade_xml_get_widget (gui->xml, "identity_full_name"));
	gui->email_address = GTK_ENTRY (glade_xml_get_widget (gui->xml, "identity_address"));
	gui->reply_to = GTK_ENTRY (glade_xml_get_widget (gui->xml, "identity_reply_to"));
	gui->organization = GTK_ENTRY (glade_xml_get_widget (gui->xml, "identity_organization"));
	
	prepare_signatures (gui);
	
	if (account->id->name)
		gtk_entry_set_text (gui->full_name, account->id->name);
	if (account->id->address)
		gtk_entry_set_text (gui->email_address, account->id->address);
	if (account->id->reply_to)
		gtk_entry_set_text (gui->reply_to, account->id->reply_to);
	if (account->id->organization)
		gtk_entry_set_text (gui->organization, account->id->organization);
	
	setup_signatures (gui);
	
	/* Source */
	gui->source.provider_type = CAMEL_PROVIDER_STORE;
	gui->source.type = GTK_OPTION_MENU (glade_xml_get_widget (gui->xml, "source_type_omenu"));
	gui->source.description = GTK_LABEL (glade_xml_get_widget (gui->xml, "source_description"));
	gui->source.hostname = GTK_ENTRY (glade_xml_get_widget (gui->xml, "source_host"));
	g_signal_connect (gui->source.hostname, "changed",
			  G_CALLBACK (service_changed), &gui->source);
	gui->source.username = GTK_ENTRY (glade_xml_get_widget (gui->xml, "source_user"));
	g_signal_connect (gui->source.username, "changed",
			  G_CALLBACK (service_changed), &gui->source);
	gui->source.path = GTK_ENTRY (glade_xml_get_widget (gui->xml, "source_path"));
	g_signal_connect (gui->source.path, "changed",
			  G_CALLBACK (service_changed), &gui->source);
	gui->source.ssl_hbox = glade_xml_get_widget (gui->xml, "source_ssl_hbox");
	gui->source.use_ssl = GTK_OPTION_MENU (glade_xml_get_widget (gui->xml, "source_use_ssl"));
	construct_ssl_menu (&gui->source);
	gui->source.no_ssl = glade_xml_get_widget (gui->xml, "source_ssl_disabled");
	gui->source.authtype = GTK_OPTION_MENU (glade_xml_get_widget (gui->xml, "source_auth_omenu"));
	gui->source.remember = GTK_TOGGLE_BUTTON (glade_xml_get_widget (gui->xml, "source_remember_password"));
	gui->source.check_supported = GTK_BUTTON (glade_xml_get_widget (gui->xml, "source_check_supported"));
	g_signal_connect (gui->source.check_supported, "clicked",
			  G_CALLBACK (service_check_supported), &gui->source);
	gui->source_auto_check = GTK_TOGGLE_BUTTON (glade_xml_get_widget (gui->xml, "extra_auto_check"));
	gui->source_auto_check_min = GTK_SPIN_BUTTON (glade_xml_get_widget (gui->xml, "extra_auto_check_min"));
	
	/* Transport */
	gui->transport.provider_type = CAMEL_PROVIDER_TRANSPORT;
	gui->transport.type = GTK_OPTION_MENU (glade_xml_get_widget (gui->xml, "transport_type_omenu"));
	gui->transport.description = GTK_LABEL (glade_xml_get_widget (gui->xml, "transport_description"));
	gui->transport.hostname = GTK_ENTRY (glade_xml_get_widget (gui->xml, "transport_host"));
	g_signal_connect (gui->transport.hostname, "changed",
			  G_CALLBACK (service_changed), &gui->transport);
	gui->transport.username = GTK_ENTRY (glade_xml_get_widget (gui->xml, "transport_user"));
	g_signal_connect (gui->transport.username, "changed",
			  G_CALLBACK (service_changed), &gui->transport);
	gui->transport.ssl_hbox = glade_xml_get_widget (gui->xml, "transport_ssl_hbox");
	gui->transport.use_ssl = GTK_OPTION_MENU (glade_xml_get_widget (gui->xml, "transport_use_ssl"));
	construct_ssl_menu (&gui->transport);
	gui->transport.no_ssl = glade_xml_get_widget (gui->xml, "transport_ssl_disabled");
	gui->transport_needs_auth = GTK_TOGGLE_BUTTON (glade_xml_get_widget (gui->xml, "transport_needs_auth"));
	g_signal_connect (gui->transport_needs_auth, "toggled",
			  G_CALLBACK (transport_needs_auth_toggled), gui);
	gui->transport.authtype = GTK_OPTION_MENU (glade_xml_get_widget (gui->xml, "transport_auth_omenu"));
	gui->transport.remember = GTK_TOGGLE_BUTTON (glade_xml_get_widget (gui->xml, "transport_remember_password"));
	gui->transport.check_supported = GTK_BUTTON (glade_xml_get_widget (gui->xml, "transport_check_supported"));
	g_signal_connect (gui->transport.check_supported, "clicked",
			  G_CALLBACK (service_check_supported), &gui->transport);
	
	/* Drafts folder */
	gui->drafts_folder_button = GTK_BUTTON (glade_xml_get_widget (gui->xml, "drafts_button"));
	g_signal_connect (gui->drafts_folder_button, "selected",
			  G_CALLBACK (folder_selected), &gui->drafts_folder_uri);
	if (account->drafts_folder_uri)
		gui->drafts_folder_uri = g_strdup (account->drafts_folder_uri);
	else
		gui->drafts_folder_uri = g_strdup (default_drafts_folder_uri);
	evolution_folder_selector_button_construct (EVOLUTION_FOLDER_SELECTOR_BUTTON (gui->drafts_folder_button),
						    global_shell_client,
						    _("Select Folder"),
						    gui->drafts_folder_uri,
						    allowed_types);
	
	/* Sent folder */
	gui->sent_folder_button = GTK_BUTTON (glade_xml_get_widget (gui->xml, "sent_button"));
	g_signal_connect (gui->sent_folder_button, "selected",
			  G_CALLBACK (folder_selected), &gui->sent_folder_uri);
	if (account->sent_folder_uri)
		gui->sent_folder_uri = g_strdup (account->sent_folder_uri);
	else
		gui->sent_folder_uri = g_strdup (default_sent_folder_uri);
	evolution_folder_selector_button_construct (EVOLUTION_FOLDER_SELECTOR_BUTTON (gui->sent_folder_button),
						    global_shell_client,
						    _("Select Folder"),
						    gui->sent_folder_uri,
						    allowed_types);
	
	/* Special Folders "Reset Defaults" button */
	button = glade_xml_get_widget (gui->xml, "default_folders_button");
	g_signal_connect (button, "clicked", G_CALLBACK (default_folders_clicked), gui);
	
	/* Always Cc */
	gui->always_cc = GTK_TOGGLE_BUTTON (glade_xml_get_widget (gui->xml, "always_cc"));
	gtk_toggle_button_set_active (gui->always_cc, account->always_cc);
	gui->cc_addrs = GTK_ENTRY (glade_xml_get_widget (gui->xml, "cc_addrs"));
	if (account->cc_addrs)
		gtk_entry_set_text (gui->cc_addrs, account->cc_addrs);
	
	/* Always Bcc */
	gui->always_bcc = GTK_TOGGLE_BUTTON (glade_xml_get_widget (gui->xml, "always_bcc"));
	gtk_toggle_button_set_active (gui->always_bcc, account->always_bcc);
	gui->bcc_addrs = GTK_ENTRY (glade_xml_get_widget (gui->xml, "bcc_addrs"));
	if (account->bcc_addrs)
		gtk_entry_set_text (gui->bcc_addrs, account->bcc_addrs);
	
	/* Security */
	gui->pgp_key = GTK_ENTRY (glade_xml_get_widget (gui->xml, "pgp_key"));
	if (account->pgp_key)
		gtk_entry_set_text (gui->pgp_key, account->pgp_key);
	gui->pgp_encrypt_to_self = GTK_TOGGLE_BUTTON (glade_xml_get_widget (gui->xml, "pgp_encrypt_to_self"));
	gtk_toggle_button_set_active (gui->pgp_encrypt_to_self, account->pgp_encrypt_to_self);
	gui->pgp_always_sign = GTK_TOGGLE_BUTTON (glade_xml_get_widget (gui->xml, "pgp_always_sign"));
	gtk_toggle_button_set_active (gui->pgp_always_sign, account->pgp_always_sign);
	gui->pgp_no_imip_sign = GTK_TOGGLE_BUTTON (glade_xml_get_widget (gui->xml, "pgp_no_imip_sign"));
	gtk_toggle_button_set_active (gui->pgp_no_imip_sign, account->pgp_no_imip_sign);
	gui->pgp_always_trust = GTK_TOGGLE_BUTTON (glade_xml_get_widget (gui->xml, "pgp_always_trust"));
	gtk_toggle_button_set_active (gui->pgp_always_trust, account->pgp_always_trust);
	
#if defined (HAVE_NSS) && defined (SMIME_SUPPORTED)
	gui->smime_key = GTK_ENTRY (glade_xml_get_widget (gui->xml, "smime_key"));
	if (account->smime_key)
		gtk_entry_set_text (gui->smime_key, account->smime_key);
	gui->smime_encrypt_to_self = GTK_TOGGLE_BUTTON (glade_xml_get_widget (gui->xml, "smime_encrypt_to_self"));
	gtk_toggle_button_set_active (gui->smime_encrypt_to_self, account->smime_encrypt_to_self);
	gui->smime_always_sign = GTK_TOGGLE_BUTTON (glade_xml_get_widget (gui->xml, "smime_always_sign"));
	gtk_toggle_button_set_active (gui->smime_always_sign, account->smime_always_sign);
#else
	{
		/* Since we don't have NSS, hide the S/MIME config options */
		GtkWidget *frame;
		
		frame = glade_xml_get_widget (gui->xml, "smime_frame");
		gtk_widget_destroy (frame);
	}
#endif /* HAVE_NSS && SMIME_SUPPORTED */
	
	return gui;
}

void
mail_account_gui_setup (MailAccountGui *gui, GtkWidget *top)
{
	GtkWidget *stores, *transports, *item;
	GtkWidget *fstore = NULL, *ftransport = NULL;
	int si = 0, hstore = 0, ti = 0, htransport = 0;
	int max_width = 0;
	char *max_authname = NULL;
	char *source_proto, *transport_proto;
	GList *providers, *l;
	
	if (gui->account->source && gui->account->source->url) {
		source_proto = gui->account->source->url;
		source_proto = g_strndup (source_proto, strcspn (source_proto, ":"));
	} else
		source_proto = NULL;
	
	if (gui->account->transport && gui->account->transport->url) {
		transport_proto = gui->account->transport->url;
		transport_proto = g_strndup (transport_proto, strcspn (transport_proto, ":"));
	} else
		transport_proto = NULL;
	
	/* Construct source/transport option menus */
	stores = gtk_menu_new ();
	transports = gtk_menu_new ();
	providers = camel_session_list_providers (session, TRUE);
	
	/* sort the providers, remote first */
	providers = g_list_sort (providers, (GCompareFunc) provider_compare);
	
	for (l = providers; l; l = l->next) {
		CamelProvider *provider = l->data;
		
		if (!(!strcmp (provider->domain, "mail") || !strcmp (provider->domain, "news")))
			continue;
		
		item = NULL;
		if (provider->object_types[CAMEL_PROVIDER_STORE] && provider->flags & CAMEL_PROVIDER_IS_SOURCE) {
			item = gtk_menu_item_new_with_label (provider->name);
			g_object_set_data ((GObject *) gui->source.type, provider->protocol, item);
			g_object_set_data ((GObject *) item, "provider", provider);
			g_object_set_data ((GObject *) item, "number", GUINT_TO_POINTER (si));
			g_signal_connect (item, "activate", G_CALLBACK (source_type_changed), gui);
			
			gtk_menu_shell_append(GTK_MENU_SHELL(stores), item);
			
			gtk_widget_show (item);
			
			if (!fstore) {
				fstore = item;
				hstore = si;
			}
			
			if (source_proto && !strcasecmp (provider->protocol, source_proto)) {
				fstore = item;
				hstore = si;
			}
			
			si++;
		}
		
		if (provider->object_types[CAMEL_PROVIDER_TRANSPORT]) {
			item = gtk_menu_item_new_with_label (provider->name);
			g_object_set_data ((GObject *) gui->transport.type, provider->protocol, item);
			g_object_set_data ((GObject *) item, "provider", provider);
			g_object_set_data ((GObject *) item, "number", GUINT_TO_POINTER (ti));
			g_signal_connect (item, "activate", G_CALLBACK (transport_type_changed), gui);
			
			gtk_menu_shell_append(GTK_MENU_SHELL(transports), item);
			
			gtk_widget_show (item);
			
			if (CAMEL_PROVIDER_IS_STORE_AND_TRANSPORT (provider))
				gtk_widget_set_sensitive (item, FALSE);
			
			if (!ftransport) {
				ftransport = item;
				htransport = ti;
			}
			
			if (transport_proto && !strcasecmp (provider->protocol, transport_proto)) {
				ftransport = item;
				htransport = ti;
			}
			
			ti++;
		}
		
		if (item && provider->authtypes) {
			/*GdkFont *font = GTK_WIDGET (item)->style->font;*/
			CamelServiceAuthType *at;
			int width;
			GList *a;
			
			for (a = provider->authtypes; a; a = a->next) {
				at = a->data;
				
				/* Just using string length is probably good enough,
				   as we only use the width of the widget, not the string */
				/*width = gdk_string_width (font, at->name);*/
				width = strlen(at->name) * 14;
				if (width > max_width) {
					max_authname = at->name;
					max_width = width;
				}
			}
		}
	}
	g_list_free (providers);
	
	/* add a "None" option to the stores menu */
	item = gtk_menu_item_new_with_label (_("None"));
	g_object_set_data ((GObject *) item, "provider", NULL);
	g_signal_connect (item, "activate", G_CALLBACK (source_type_changed), gui);
	
	gtk_menu_shell_append(GTK_MENU_SHELL(stores), item);
	
	gtk_widget_show (item);
	
	if (!fstore || !source_proto) {
		fstore = item;
		hstore = si;
	}
	
	/* set the menus on the optionmenus */
	gtk_option_menu_remove_menu (gui->source.type);
	gtk_option_menu_set_menu (gui->source.type, stores);
	
	gtk_option_menu_remove_menu (gui->transport.type);
	gtk_option_menu_set_menu (gui->transport.type, transports);
	
	/* Force the authmenus to the width of the widest element */
	if (max_authname) {
		GtkWidget *menu;
		GtkRequisition size_req;
		
		menu = gtk_menu_new ();
		item = gtk_menu_item_new_with_label (max_authname);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
		gtk_widget_show_all (menu);
		gtk_option_menu_set_menu (gui->source.authtype, menu);
		gtk_widget_show (GTK_WIDGET (gui->source.authtype));
		gtk_widget_size_request (GTK_WIDGET (gui->source.authtype),
					 &size_req);
		
		gtk_widget_set_size_request (GTK_WIDGET (gui->source.authtype),
				      size_req.width, -1);
		gtk_widget_set_size_request (GTK_WIDGET (gui->transport.authtype),
				      size_req.width, -1);
	}
	
	if (top != NULL) {
		gtk_widget_show_all (top);
	}
	
	if (fstore) {
		g_signal_emit_by_name (fstore, "activate");
		gtk_option_menu_set_history (gui->source.type, hstore);
	}
	
	if (ftransport) {
		g_signal_emit_by_name (ftransport, "activate");
		gtk_option_menu_set_history (gui->transport.type, htransport);
	}
	
	if (source_proto) {
		setup_service (&gui->source, gui->account->source);
		gui->source.provider_type = CAMEL_PROVIDER_STORE;
		g_free (source_proto);
		if (gui->account->source->auto_check) {
			gtk_toggle_button_set_active (gui->source_auto_check, TRUE);
			gtk_spin_button_set_value (gui->source_auto_check_min,
						   gui->account->source->auto_check_time);
		}
	}
	
	if (transport_proto) {
		if (setup_service (&gui->transport, gui->account->transport))
			gtk_toggle_button_set_active (gui->transport_needs_auth, TRUE);
		gui->transport.provider_type = CAMEL_PROVIDER_TRANSPORT;
		g_free (transport_proto);
	}
}

static void
save_service (MailAccountGuiService *gsvc, GHashTable *extra_config, EAccountService *service)
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
	EAccount *account = user_data;
	EvolutionStorage *storage;
	
	if (store == NULL)
		return;
	
	storage = mail_lookup_storage (store);
	if (storage) {
		/* store is already in the folder tree, so do nothing */
		bonobo_object_unref (BONOBO_OBJECT (storage));
	} else {
		/* store is *not* in the folder tree, so lets add it. */
		mail_add_storage (store, account->name, account->source->url);
	}
}

gboolean
mail_account_gui_save (MailAccountGui *gui)
{
	EAccount *account, *new;
	CamelProvider *provider = NULL;
	CamelURL *source_url = NULL, *url;
	gboolean is_new = FALSE;
	const char *new_name;
	gboolean is_storage;
	GSList *signatures;
	
	if (!mail_account_gui_identity_complete (gui, NULL) ||
	    !mail_account_gui_source_complete (gui, NULL) ||
	    !mail_account_gui_transport_complete (gui, NULL) ||
	    !mail_account_gui_management_complete (gui, NULL))
		return FALSE;
	
	new = gui->account;
	
	/* this would happen at an inconvenient time in the druid,
	 * but the druid performs its own check so this can't happen
	 * here. */
	
	new_name = gtk_entry_get_text (gui->account_name);
	account = mail_config_get_account_by_name (new_name);
	
	if (account && account != new) {
		e_notice (gui->account_name, GTK_MESSAGE_ERROR, _("You may not create two accounts with the same name."));
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
	signatures = mail_config_get_signature_list ();
	new->id->def_signature = g_slist_index (signatures, gui->def_signature);
	new->id->auto_signature = gui->auto_signature;
	
	/* source */
	save_service (&gui->source, gui->extra_config, new->source);
	if (new->source->url) {
		provider = camel_session_get_provider (session, new->source->url, NULL);
		source_url = provider ? camel_url_new (new->source->url, NULL) : NULL;
	}
	
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
	url = source_url && gui->drafts_folder_uri ? camel_url_new (gui->drafts_folder_uri, NULL) : NULL;
	if (mail_config_get_account_by_source_url (gui->drafts_folder_uri) ||
	    (url && provider->url_equal (source_url, url))) {
		new->drafts_folder_uri = g_strdup (gui->drafts_folder_uri);
	} else {
		/* assign defaults - the uri is unknown to us (probably pointed to an old source url) */
		new->drafts_folder_uri = g_strdup (default_drafts_folder_uri);
	}
	
	if (url)
		camel_url_free (url);
	
	/* Check to make sure that the Sent folder uri is "valid" before assigning it */
	url = source_url && gui->sent_folder_uri ? camel_url_new (gui->sent_folder_uri, NULL) : NULL;
	if (mail_config_get_account_by_source_url (gui->sent_folder_uri) ||
	    (url && provider->url_equal (source_url, url))) {
		new->sent_folder_uri = g_strdup (gui->sent_folder_uri);
	} else {
		/* assign defaults - the uri is unknown to us (probably pointed to an old source url) */
		new->sent_folder_uri = g_strdup (default_sent_folder_uri);
	}
	
	if (url)
		camel_url_free (url);
	
	if (source_url)
		camel_url_free (source_url);
	
	new->always_cc = gtk_toggle_button_get_active (gui->always_cc);
	new->cc_addrs = g_strdup (gtk_entry_get_text (gui->cc_addrs));
	new->always_bcc = gtk_toggle_button_get_active (gui->always_bcc);
	new->bcc_addrs = g_strdup (gtk_entry_get_text (gui->bcc_addrs));
	
	new->pgp_key = g_strdup (gtk_entry_get_text (gui->pgp_key));
	new->pgp_encrypt_to_self = gtk_toggle_button_get_active (gui->pgp_encrypt_to_self);
	new->pgp_always_sign = gtk_toggle_button_get_active (gui->pgp_always_sign);
	new->pgp_no_imip_sign = gtk_toggle_button_get_active (gui->pgp_no_imip_sign);
	new->pgp_always_trust = gtk_toggle_button_get_active (gui->pgp_always_trust);
	
#if defined (HAVE_NSS) && defined (SMIME_SUPPORTED)
	new->smime_key = g_strdup (gtk_entry_get_text (gui->smime_key));
	new->smime_encrypt_to_self = gtk_toggle_button_get_active (gui->smime_encrypt_to_self);
	new->smime_always_sign = gtk_toggle_button_get_active (gui->smime_always_sign);
#endif /* HAVE_NSS && SMIME_SUPPORTED */
	
	is_storage = provider && (provider->flags & CAMEL_PROVIDER_IS_STORAGE) &&
		!(provider->flags & CAMEL_PROVIDER_IS_EXTERNAL);
	
	if (!mail_config_find_account (account)) {
		/* this is a new account so add it to our account-list */
		is_new = TRUE;
	} else if (account->source->url) {
		/* this means the account was edited - if the old and
                   new source urls are not identical, replace the old
                   storage with the new storage */
#define sources_equal(old,new) (new->url && !strcmp (old->url, new->url))
		if (!sources_equal (account->source, new->source)) {
			/* Remove the old storage from the folder-tree */
			mail_remove_storage_by_uri (account->source->url);
		}
	}
	
	/* update the old account with the new settings */
	e_account_import (account, new);
	g_object_unref (new);
	
	if (is_new) {
		mail_config_add_account (account);
	} else {
		e_account_list_change (mail_config_get_accounts (), account);
	}
	
	/* if the account provider is something we can stick
	   in the folder-tree and not added by some other
	   component, then get the CamelStore and add it to
	   the shell storages */
	if (is_storage && account->enabled)
		mail_get_store (account->source->url, add_new_store, account);
	
	if (gtk_toggle_button_get_active (gui->default_account))
		mail_config_set_default_account (account);
	
	mail_config_save_accounts ();
	mail_config_write_account_sig (account, -1);
	
	mail_autoreceive_setup ();
	
	return TRUE;
}

void
mail_account_gui_destroy (MailAccountGui *gui)
{
	if (gui->dialog)
		mail_config_signature_unregister_client ((MailConfigSignatureClient) sig_event_client, gui);
	
	g_object_unref (gui->xml);
	g_object_unref (gui->account);
	
	if (gui->extra_config)
		g_hash_table_destroy (gui->extra_config);
	
	g_free (gui->drafts_folder_uri);
	g_free (gui->sent_folder_uri);
	g_free (gui);
}
