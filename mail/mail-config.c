/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
 *           Radek Doulik     <rodo@ximian.com>
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

#include <pwd.h>
#include <ctype.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>

#include <glib.h>
#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-config.h>
#include <libgnomeui/gnome-dialog.h>
#include <libgnomeui/gnome-stock.h>
#include <gtkhtml/gtkhtml.h>
#include <glade/glade.h>

#include <bonobo/bonobo-object.h>
#include <bonobo/bonobo-generic-factory.h>
#include <bonobo/bonobo-context.h>
#include <bonobo/bonobo-moniker-util.h>
#include <bonobo/bonobo-exception.h>
#include <bonobo-conf/bonobo-config-database.h>

#include <shell/evolution-shell-client.h>

#include <gal/util/e-unicode-i18n.h>
#include <gal/util/e-util.h>
#include <gal/unicode/gunicode.h>
#include <gal/widgets/e-gui-utils.h>
#include <e-util/e-html-utils.h>
#include <e-util/e-url.h>
#include <e-util/e-passwords.h>
#include "mail.h"
#include "mail-config.h"
#include "mail-mt.h"
#include "mail-tools.h"

#include "Mailer.h"


MailConfigLabel label_defaults[5] = {
	{ N_("Important"), 0x00ff0000, NULL },  /* red */
	{ N_("Work"),      0x00ff8c00, NULL },  /* orange */
	{ N_("Personal"),  0x00008b00, NULL },  /* forest green */
	{ N_("To Do"),     0x000000ff, NULL },  /* blue */
	{ N_("Later"),     0x008b008b, NULL }   /* magenta */
};

typedef struct {
	Bonobo_ConfigDatabase db;
	
	gboolean corrupt;
	
	gboolean show_preview;
	gboolean thread_list;
	gboolean hide_deleted;
	int paned_size;
	gboolean send_html;
	gboolean confirm_unwanted_html;
	gboolean citation_highlight;
	guint32  citation_color;
	gboolean prompt_empty_subject;
	gboolean prompt_only_bcc;
	gboolean confirm_expunge;
	gboolean confirm_goto_next_folder;
	gboolean goto_next_folder;
	gboolean do_seen_timeout;
	int seen_timeout;
	gboolean empty_trash_on_exit;
	
	gboolean thread_subject;
	
	GSList *accounts;
	int default_account;
	
	MailConfigHTTPMode http_mode;
	MailConfigForwardStyle default_forward_style;
	MailConfigReplyStyle default_reply_style;
	MailConfigDisplayStyle message_display_style;
	MailConfigXMailerDisplayStyle x_mailer_display_style;
	char *default_charset;
	
	GHashTable *threaded_hash;
	GHashTable *preview_hash;
	
	gboolean filter_log;
	char *filter_log_path;
	
	MailConfigNewMailNotify notify;
	char *notify_filename;
	
	char *last_filesel_dir;
	
	GList *signature_list;
	int signatures;
	
	MailConfigLabel labels[5];

	gboolean signature_info;

	/* readonly fields from calendar */
	int week_start_day;
	int time_24hour;
} MailConfig;

static MailConfig *config = NULL;
static guint config_write_timeout = 0;

#define MAIL_CONFIG_IID "OAFIID:GNOME_Evolution_MailConfig_Factory"

/* Prototypes */
static void config_read (void);
static void mail_config_set_default_account_num (int new_default);

/* signatures */
MailConfigSignature *
signature_copy (const MailConfigSignature *sig)
{
	MailConfigSignature *ns;
	
	g_return_val_if_fail (sig != NULL, NULL);
	
	ns = g_new (MailConfigSignature, 1);
	
	ns->id = sig->id;
	ns->name = g_strdup (sig->name);
	ns->filename = g_strdup (sig->filename);
	ns->script = g_strdup (sig->script);
	ns->html = sig->html;
	
	return ns;
}

void
signature_destroy (MailConfigSignature *sig)
{
	g_free (sig->name);
	g_free (sig->filename);
	g_free (sig->script);
	g_free (sig);
}

/* Identity */
MailConfigIdentity *
identity_copy (const MailConfigIdentity *id)
{
	MailConfigIdentity *new;
	
	g_return_val_if_fail (id != NULL, NULL);
	
	new = g_new0 (MailConfigIdentity, 1);
	new->name = g_strdup (id->name);
	new->address = g_strdup (id->address);
	new->reply_to = g_strdup (id->reply_to);
	new->organization = g_strdup (id->organization);
	new->def_signature = id->def_signature;
	new->auto_signature = id->auto_signature;
	
	return new;
}

void
identity_destroy (MailConfigIdentity *id)
{
	if (!id)
		return;
	
	g_free (id->name);
	g_free (id->address);
	g_free (id->reply_to);
	g_free (id->organization);
	
	g_free (id);
}

/* Service */
MailConfigService *
service_copy (const MailConfigService *source) 
{
	MailConfigService *new;
	
	g_return_val_if_fail (source != NULL, NULL);
	
	new = g_new0 (MailConfigService, 1);
	new->url = g_strdup (source->url);
	new->keep_on_server = source->keep_on_server;
	new->auto_check = source->auto_check;
	new->auto_check_time = source->auto_check_time;
	new->enabled = source->enabled;
	new->save_passwd = source->save_passwd;
	
	return new;
}

void
service_destroy (MailConfigService *source)
{
	if (!source)
		return;
	
	g_free (source->url);
	
	g_free (source);
}

void
service_destroy_each (gpointer item, gpointer data)
{
	service_destroy ((MailConfigService *)item);
}

/* Account */
MailConfigAccount *
account_copy (const MailConfigAccount *account) 
{
	MailConfigAccount *new;
	
	g_return_val_if_fail (account != NULL, NULL);
	
	new = g_new0 (MailConfigAccount, 1);
	new->name = g_strdup (account->name);
	
	new->id = identity_copy (account->id);
	new->source = service_copy (account->source);
	new->transport = service_copy (account->transport);
	
	new->drafts_folder_uri = g_strdup (account->drafts_folder_uri);
	new->sent_folder_uri = g_strdup (account->sent_folder_uri);
	
	new->always_cc = account->always_cc;
	new->cc_addrs = g_strdup (account->cc_addrs);
	new->always_bcc = account->always_bcc;
	new->bcc_addrs = g_strdup (account->bcc_addrs);
	
	new->pgp_key = g_strdup (account->pgp_key);
	new->pgp_encrypt_to_self = account->pgp_encrypt_to_self;
	new->pgp_always_sign = account->pgp_always_sign;
	new->pgp_no_imip_sign = account->pgp_no_imip_sign;
	new->pgp_always_trust = account->pgp_always_trust;
	
	new->smime_key = g_strdup (account->smime_key);
	new->smime_encrypt_to_self = account->smime_encrypt_to_self;
	new->smime_always_sign = account->smime_always_sign;
	
	return new;
}

void
account_destroy (MailConfigAccount *account)
{
	if (!account)
		return;
	
	g_free (account->name);
	
	identity_destroy (account->id);
	service_destroy (account->source);
	service_destroy (account->transport);
	
	g_free (account->drafts_folder_uri);
	g_free (account->sent_folder_uri);
	
	g_free (account->cc_addrs);
	g_free (account->bcc_addrs);
	
	g_free (account->pgp_key);
	g_free (account->smime_key);
	
	g_free (account);
}

void
account_destroy_each (gpointer item, gpointer data)
{
	account_destroy ((MailConfigAccount *)item);
}

/* Config struct routines */
void
mail_config_init (void)
{
	Bonobo_ConfigDatabase db;
	CORBA_Environment ev;
	
	if (config)
		return;
	
	CORBA_exception_init (&ev);
	
	db = bonobo_get_object ("wombat:", "Bonobo/ConfigDatabase", &ev);
	
	if (BONOBO_EX (&ev) || db == CORBA_OBJECT_NIL) {
		char *err;
		g_error ("Very serious error, cannot activate config database '%s'",
			 (err = bonobo_exception_get_text (&ev)));
		g_free (err);
		CORBA_exception_free (&ev);
		return;
 	}
	
	CORBA_exception_free (&ev);
	
	config = g_new0 (MailConfig, 1);
	
	config->db = db;
	
	config_read ();
}

void
mail_config_clear (void)
{
	int i;
	
	if (!config)
		return;
	
	if (config->accounts) {
		g_slist_foreach (config->accounts, account_destroy_each, NULL);
		g_slist_free (config->accounts);
		config->accounts = NULL;
	}
	
	g_free (config->default_charset);
	config->default_charset = NULL;
	
	g_free (config->filter_log_path);
	config->filter_log_path = NULL;
	
	g_free (config->notify_filename);
	config->notify_filename = NULL;
	
	g_free (config->last_filesel_dir);
	config->last_filesel_dir = NULL;
	
	for (i = 0; i < 5; i++) {
		g_free (config->labels[i].name);
		config->labels[i].name = NULL;
		g_free (config->labels[i].string);
		config->labels[i].string = NULL;
	}
}

static MailConfigSignature *
config_read_signature (gint i)
{
	MailConfigSignature *sig;
	char *path, *val;
	
	sig = g_new0 (MailConfigSignature, 1);
	
	sig->id = i;
	
	path = g_strdup_printf ("/Mail/Signatures/name_%d", i);
	val = bonobo_config_get_string (config->db, path, NULL);
	g_free (path);
	if (val && *val)
		sig->name = val;
	else
		g_free (val);
	
	path = g_strdup_printf ("/Mail/Signatures/filename_%d", i);
	val = bonobo_config_get_string (config->db, path, NULL);
	g_free (path);
	if (val && *val)
		sig->filename = val;
	else
		g_free (val);
	
	path = g_strdup_printf ("/Mail/Signatures/script_%d", i);
	val = bonobo_config_get_string (config->db, path, NULL);
	g_free (path);
	if (val && *val)
		sig->script = val;
	else
		g_free (val);

	path = g_strdup_printf ("/Mail/Signatures/html_%d", i);
	sig->html = bonobo_config_get_boolean_with_default (config->db, path, FALSE, NULL);
	g_free (path);
	
	return sig;
}

static void
config_read_signatures ()
{
	MailConfigSignature *sig;
	gint i;

	config->signature_list = NULL;
	config->signatures = bonobo_config_get_long_with_default (config->db, "/Mail/Signatures/num", 0, NULL);

	for (i = 0; i < config->signatures; i ++) {
		sig = config_read_signature (i);
		config->signature_list = g_list_append (config->signature_list, sig);
	}
}

static void
config_write_signature (MailConfigSignature *sig, gint i)
{
	char *path;

	printf ("config_write_signature i: %d id: %d\n", i, sig->id);

	path = g_strdup_printf ("/Mail/Signatures/name_%d", i);
	bonobo_config_set_string (config->db, path, sig->name ? sig->name : "", NULL);
	g_free (path);

	path = g_strdup_printf ("/Mail/Signatures/filename_%d", i);
	bonobo_config_set_string (config->db, path, sig->filename ? sig->filename : "", NULL);
	g_free (path);

	path = g_strdup_printf ("/Mail/Signatures/script_%d", i);
	bonobo_config_set_string (config->db, path, sig->script ? sig->script : "", NULL);
	g_free (path);

	path = g_strdup_printf ("/Mail/Signatures/html_%d", i);
	bonobo_config_set_boolean (config->db, path, sig->html, NULL);
	g_free (path);
}

static void
config_write_signatures_num ()
{
	bonobo_config_set_long (config->db, "/Mail/Signatures/num", config->signatures, NULL);
}

static void
config_write_signatures ()
{
	GList *l;
	gint id;

	for (id = 0, l = config->signature_list; l; l = l->next, id ++) {
		config_write_signature ((MailConfigSignature *) l->data, id);
	}

	config_write_signatures_num ();
}

static MailConfigSignature *
lookup_signature (gint i)
{
	MailConfigSignature *sig;
	GList *l;

	if (i == -1)
		return NULL;

	for (l = config->signature_list; l; l = l->next) {
		sig = (MailConfigSignature *) l->data;
		if (sig->id == i)
			return sig;
	}

	return NULL;
}

static void
config_write_imported_signature (gchar *filename, gint i, gboolean html)
{
	MailConfigSignature *sig = g_new0 (MailConfigSignature, 1);
	gchar *name;

	name = strrchr (filename, '/');
	if (!name)
		name = filename;
	else
		name ++;

	sig->name = g_strdup (name);
	sig->filename = filename;
	sig->html = html;

	config_write_signature (sig, i);
	signature_destroy (sig);
}

static void
config_import_old_signatures ()
{
	int num;
	
	num = bonobo_config_get_long_with_default (config->db, "/Mail/Signatures/num", -1, NULL);
	
	if (num == -1) {
		/* there are no signatures defined
		 * look for old config to create new ones from old ones
		 */
		GHashTable *cache;
		int i, accounts;
		
		cache = g_hash_table_new (g_str_hash, g_str_equal);
		accounts = bonobo_config_get_long_with_default (config->db, "/Mail/Accounts/num", 0, NULL);
		num = 0;
		for (i = 0; i < accounts; i ++) {
			char *path, *val;
			
			/* read text signature file */
			path = g_strdup_printf ("/Mail/Accounts/identity_signature_%d", i);
			val = bonobo_config_get_string (config->db, path, NULL);
			g_free (path);
			if (val && *val) {
				gpointer orig_key, node_val;
				int id;
				
				if (g_hash_table_lookup_extended (cache, val, &orig_key, &node_val)) {
					id = GPOINTER_TO_INT (node_val);
				} else {
					g_hash_table_insert (cache, g_strdup (val), GINT_TO_POINTER (num));
					config_write_imported_signature (val, num, FALSE);
					id = num;
					num ++;
				}
				
				/* set new text signature to this identity */
				path = g_strdup_printf ("/Mail/Accounts/identity_signature_text_%d", i);
				bonobo_config_set_long (config->db, path, id, NULL);
				g_free (path);
			} else
				g_free (val);
			
			path = g_strdup_printf ("/Mail/Accounts/identity_has_html_signature_%d", i);
			if (bonobo_config_get_boolean_with_default (config->db, path, FALSE, NULL)) {
				g_free (path);
				path = g_strdup_printf ("/Mail/Accounts/identity_html_signature_%d", i);
				val = bonobo_config_get_string (config->db, path, NULL);
				if (val && *val) {
					gpointer orig_key, node_val;
					int id;
					
					if (g_hash_table_lookup_extended (cache, val, &orig_key, &node_val)) {
						id = GPOINTER_TO_INT (node_val);
					} else {
						g_hash_table_insert (cache, g_strdup (val), GINT_TO_POINTER (num));
						config_write_imported_signature (val, num, TRUE);
						id = num;
						num ++;
					}
					
					/* set new html signature to this identity */
					g_free (path);
					path = g_strdup_printf ("/Mail/Accounts/identity_signature_html_%d", i);
					bonobo_config_set_long (config->db, path, id, NULL);
				} else
					g_free (val);
			}
			g_free (path);
		}
		bonobo_config_set_long (config->db, "/Mail/Signatures/num", num, NULL);
		g_hash_table_destroy (cache);
	}
}

/* copied from calendar-config */
static gboolean
locale_supports_12_hour_format(void)
{
	char s[16];
	time_t t = 0;
	
	strftime(s, sizeof s, "%p", gmtime (&t));
	return s[0] != '\0';
}

static void
config_read (void)
{
	int len, i, default_num;
	char *path, *val, *p;
	
	mail_config_clear ();
	
	config_import_old_signatures ();
	config_read_signatures ();
	
	len = bonobo_config_get_long_with_default (config->db, 
	        "/Mail/Accounts/num", 0, NULL);
	
	for (i = 0; i < len; i++) {
		MailConfigAccount *account;
		MailConfigIdentity *id;
		MailConfigService *source;
		MailConfigService *transport;
		
		account = g_new0 (MailConfigAccount, 1);
		path = g_strdup_printf ("/Mail/Accounts/account_name_%d", i);
		val = bonobo_config_get_string (config->db, path, NULL);
		g_free (path);
		if (val && *val) {
			account->name = val;
		} else {
			g_free (val);
			account->name = g_strdup_printf (U_("Account %d"), i + 1);
			config->corrupt = TRUE;
		}
		
		path = g_strdup_printf ("/Mail/Accounts/account_drafts_folder_uri_%d", i);
		val = bonobo_config_get_string (config->db, path, NULL);
		g_free (path);
		if (val && *val)
			account->drafts_folder_uri = val;
		else
			g_free (val);
		
		path = g_strdup_printf ("/Mail/Accounts/account_sent_folder_uri_%d", i);
		val = bonobo_config_get_string (config->db, path, NULL);
		g_free (path);
		if (val && *val)
			account->sent_folder_uri = val;
		else
			g_free (val);
		
		path = g_strdup_printf ("/Mail/Accounts/account_always_cc_%d", i);
		account->always_cc = bonobo_config_get_boolean_with_default (
		        config->db, path, FALSE, NULL);
		g_free (path);
		
		path = g_strdup_printf ("/Mail/Accounts/account_always_cc_addrs_%d", i);
		val = bonobo_config_get_string (config->db, path, NULL);
		g_free (path);
		if (val && *val)
			account->cc_addrs = val;
		else
			g_free (val);
		
		path = g_strdup_printf ("/Mail/Accounts/account_always_bcc_%d", i);
		account->always_bcc = bonobo_config_get_boolean_with_default (
		        config->db, path, FALSE, NULL);
		g_free (path);
		
		path = g_strdup_printf ("/Mail/Accounts/account_always_bcc_addrs_%d", i);
		val = bonobo_config_get_string (config->db, path, NULL);
		g_free (path);
		if (val && *val)
			account->bcc_addrs = val;
		else
			g_free (val);
		
		/* get the pgp info */
		path = g_strdup_printf ("/Mail/Accounts/account_pgp_key_%d", i);
		val = bonobo_config_get_string (config->db, path, NULL);
		g_free (path);
		if (val && *val)
			account->pgp_key = val;
		else
			g_free (val);
		
		path = g_strdup_printf ("/Mail/Accounts/account_pgp_always_sign_%d", i);
		account->pgp_always_sign = bonobo_config_get_boolean_with_default (
		        config->db, path, FALSE, NULL);
		g_free (path);
		
		path = g_strdup_printf ("/Mail/Accounts/account_pgp_no_imip_sign_%d", i);
		account->pgp_no_imip_sign = bonobo_config_get_boolean_with_default (
		        config->db, path, FALSE, NULL);
		g_free (path);
		
		path = g_strdup_printf ("/Mail/Accounts/account_pgp_encrypt_to_self_%d", i);
		account->pgp_encrypt_to_self = bonobo_config_get_boolean_with_default (
		        config->db, path, TRUE, NULL);
		g_free (path);
		
		path = g_strdup_printf ("/Mail/Accounts/account_pgp_always_trust_%d", i);
		account->pgp_always_trust = bonobo_config_get_boolean_with_default (
		        config->db, path, FALSE, NULL);
		g_free (path);
		
		/* get the s/mime info */
		path = g_strdup_printf ("/Mail/Accounts/account_smime_key_%d", i);
		val = bonobo_config_get_string (config->db, path, NULL);
		g_free (path);
		if (val && *val)
			account->smime_key = val;
		else
			g_free (val);
		
		path = g_strdup_printf ("/Mail/Accounts/account_smime_always_sign_%d", i);
		account->smime_always_sign = bonobo_config_get_boolean_with_default (
		        config->db, path, FALSE, NULL);
		g_free (path);
		
		path = g_strdup_printf ("/Mail/Accounts/account_smime_encrypt_to_self_%d", i);
		account->smime_encrypt_to_self = bonobo_config_get_boolean_with_default (
		        config->db, path, TRUE, NULL);
		g_free (path);
		
		/* get the identity info */
		id = g_new0 (MailConfigIdentity, 1);		
		path = g_strdup_printf ("/Mail/Accounts/identity_name_%d", i);
		id->name = bonobo_config_get_string (config->db, path, NULL);
		g_free (path);
		
		path = g_strdup_printf ("/Mail/Accounts/identity_address_%d", i);
		id->address = bonobo_config_get_string (config->db, path, NULL);
		g_free (path);
		
		path = g_strdup_printf ("/Mail/Accounts/identity_reply_to_%d", i);
		id->reply_to = bonobo_config_get_string (config->db, path, NULL);
		g_free (path);
		
		path = g_strdup_printf ("/Mail/Accounts/identity_organization_%d", i);
		id->organization = bonobo_config_get_string (config->db, path, NULL);
		g_free (path);
		
		/* id signatures */
		path = g_strdup_printf ("/Mail/Accounts/identity_def_signature_%d", i);
		id->def_signature = lookup_signature (bonobo_config_get_long_with_default (config->db, path, -1, NULL));
		g_free (path);
		
		/* autogenerated signature */
		path = g_strdup_printf ("/Mail/Accounts/identity_autogenerated_signature_%d", i);
		id->auto_signature = bonobo_config_get_boolean_with_default (config->db, path, TRUE, NULL);
		g_free (path);
		
		/* get the source */
		source = g_new0 (MailConfigService, 1);
		
		path = g_strdup_printf ("/Mail/Accounts/source_url_%d", i);
		val = bonobo_config_get_string (config->db, path, NULL);
		g_free (path);
		if (val && *val)
			source->url = val;
		else
			g_free (val);
		
		path = g_strdup_printf ("/Mail/Accounts/source_keep_on_server_%d", i);
		source->keep_on_server = bonobo_config_get_boolean (config->db, path, NULL);
		g_free (path);
		
		path = g_strdup_printf ("/Mail/Accounts/source_auto_check_%d", i);
		source->auto_check = bonobo_config_get_boolean_with_default (
                        config->db, path, FALSE, NULL);
		g_free (path);
		
		path = g_strdup_printf ("/Mail/Accounts/source_auto_check_time_%d", i);
		source->auto_check_time = bonobo_config_get_long_with_default ( 
			config->db, path, -1, NULL);
		
		if (source->auto_check && source->auto_check_time <= 0) {
			source->auto_check_time = 5;
			source->auto_check = FALSE;
		}
		
		g_free (path);
		
		path = g_strdup_printf ("/Mail/Accounts/source_enabled_%d", i);
		source->enabled = bonobo_config_get_boolean_with_default (
			config->db, path, TRUE, NULL);
		g_free (path);
		
		path = g_strdup_printf 
			("/Mail/Accounts/source_save_passwd_%d", i);
		source->save_passwd = bonobo_config_get_boolean_with_default ( 
			config->db, path, TRUE, NULL);
		g_free (path);
		
		/* get the transport */
		transport = g_new0 (MailConfigService, 1);
		path = g_strdup_printf ("/Mail/Accounts/transport_url_%d", i);
		val = bonobo_config_get_string (config->db, path, NULL);
		g_free (path);
		if (val && *val)
			transport->url = val;
		else
			g_free (val);
		
		path = g_strdup_printf ("/Mail/Accounts/transport_save_passwd_%d", i);
		transport->save_passwd = bonobo_config_get_boolean (config->db, path, NULL);
		g_free (path);
		
		account->id = id;
		account->source = source;
		account->transport = transport;
		
		config->accounts = g_slist_append (config->accounts, account);
	}
	
	default_num = bonobo_config_get_long_with_default (config->db,
		"/Mail/Accounts/default_account", 0, NULL);
	
	mail_config_set_default_account_num (default_num);
	
	/* Format */
	config->send_html = bonobo_config_get_boolean_with_default (config->db,
	        "/Mail/Format/send_html", FALSE, NULL);
	
	/* Confirm Sending Unwanted HTML */
	config->confirm_unwanted_html = bonobo_config_get_boolean_with_default (config->db,
	        "/Mail/Format/confirm_unwanted_html", TRUE, NULL);
	
	/* Citation */
	config->citation_highlight = bonobo_config_get_boolean_with_default (
		config->db, "/Mail/Display/citation_highlight", TRUE, NULL);
	
	config->citation_color = bonobo_config_get_long_with_default (
		config->db, "/Mail/Display/citation_color", 0x737373, NULL);
	
	/* Mark as seen toggle */
	config->do_seen_timeout = bonobo_config_get_boolean_with_default (
		config->db, "/Mail/Display/do_seen_timeout", TRUE, NULL);
	
	/* Mark as seen timeout */
	config->seen_timeout = bonobo_config_get_long_with_default (config->db,
                "/Mail/Display/seen_timeout", 1500, NULL);
	
	/* Show Messages Threaded */
	config->thread_list = bonobo_config_get_boolean_with_default (
                config->db, "/Mail/Display/thread_list", FALSE, NULL);
	
	config->thread_subject = bonobo_config_get_boolean_with_default (
                config->db, "/Mail/Display/thread_subject", FALSE, NULL);
	
	config->show_preview = bonobo_config_get_boolean_with_default (
		config->db, "/Mail/Display/preview_pane", TRUE, NULL);
	
	/* Hide deleted automatically */
	config->hide_deleted = bonobo_config_get_boolean_with_default (
		config->db, "/Mail/Display/hide_deleted", FALSE, NULL);
	
	/* Size of vpaned in mail view */
	config->paned_size = bonobo_config_get_long_with_default (config->db, 
                "/Mail/Display/paned_size", 200, NULL);
	
	/* Goto next folder when user has reached the bottom of the message-list */
	config->goto_next_folder = bonobo_config_get_boolean_with_default (
		config->db, "/Mail/MessageList/goto_next_folder", FALSE, NULL);
	
	/* Empty Subject */
	config->prompt_empty_subject = bonobo_config_get_boolean_with_default (
		config->db, "/Mail/Prompts/empty_subject", TRUE, NULL);
	
	/* Only Bcc */
	config->prompt_only_bcc = bonobo_config_get_boolean_with_default (
		config->db, "/Mail/Prompts/only_bcc", TRUE, NULL);
	
	/* Expunge */
	config->confirm_expunge = bonobo_config_get_boolean_with_default (
		config->db, "/Mail/Prompts/confirm_expunge", TRUE, NULL);
	
	/* Goto next folder */
	config->confirm_goto_next_folder = bonobo_config_get_boolean_with_default (
		config->db, "/Mail/Prompts/confirm_goto_next_folder", TRUE, NULL);
	
	/* HTTP images */
	config->http_mode = bonobo_config_get_long_with_default (config->db, 
		"/Mail/Display/http_images", MAIL_CONFIG_HTTP_NEVER, NULL);
	
	/* Forwarding */
	config->default_forward_style = bonobo_config_get_long_with_default (
		config->db, "/Mail/Format/default_forward_style", 
		MAIL_CONFIG_FORWARD_ATTACHED, NULL);
	
	/* Replying */
	config->default_reply_style = bonobo_config_get_long_with_default (
		config->db, "/Mail/Format/default_reply_style", 
		MAIL_CONFIG_REPLY_QUOTED, NULL);
	
	/* Message Display */
	config->message_display_style = bonobo_config_get_long_with_default (
		config->db, "/Mail/Format/message_display_style", 
		MAIL_CONFIG_DISPLAY_NORMAL, NULL);
	
	/* Default charset */
	config->default_charset = bonobo_config_get_string (config->db, 
	        "/Mail/Format/default_charset", NULL);
	
	if (!config->default_charset) {
		g_get_charset (&config->default_charset);
		if (!config->default_charset ||
		    !g_strcasecmp (config->default_charset, "US-ASCII"))
			config->default_charset = g_strdup ("ISO-8859-1");
		else
			config->default_charset = g_strdup (config->default_charset);
	}
	
	/* Trash folders */
	config->empty_trash_on_exit = bonobo_config_get_boolean_with_default (
		config->db, "/Mail/Trash/empty_on_exit", FALSE, NULL);
	
	/* Filter logging */
	config->filter_log = bonobo_config_get_boolean_with_default (
		config->db, "/Mail/Filters/log", FALSE, NULL);
	
	config->filter_log_path = bonobo_config_get_string (
		config->db, "/Mail/Filters/log_path", NULL);
	
	/* New Mail Notification */
	config->notify = bonobo_config_get_long_with_default (
		config->db, "/Mail/Notify/new_mail_notification", 
		MAIL_CONFIG_NOTIFY_NOT, NULL);
	
	config->notify_filename = bonobo_config_get_string (
		config->db, "/Mail/Notify/new_mail_notification_sound_file", NULL);
	
	/* X-Mailer header display */
	config->x_mailer_display_style = bonobo_config_get_long_with_default (
		config->db, "/Mail/Display/x_mailer_display_style", 
		MAIL_CONFIG_XMAILER_NONE, NULL);
	
	/* last filesel dir */
	config->last_filesel_dir = bonobo_config_get_string (
		config->db, "/Mail/Filesel/last_filesel_dir", NULL);
	
	/* Color labels */
	/* Note: we avoid having to malloc/free 10 times this way... */
	path = g_malloc (sizeof ("/Mail/Labels/") + sizeof ("label_#") + 1);
	strcpy (path, "/Mail/Labels/label_#");
	p = path + strlen (path) - 1;
	for (i = 0; i < 5; i++) {
		*p = '0' + i;
		val = bonobo_config_get_string (config->db, path, NULL);
		if (!(val && *val)) {
			g_free (val);
			val = NULL;
		}
		config->labels[i].name = val;
	}
	strcpy (path, "/Mail/Labels/color_#");
	p = path + strlen (path) - 1;
	for (i = 0; i < 5; i++) {
		*p = '0' + i;
		config->labels[i].color = bonobo_config_get_long_with_default (config->db, path,
									       label_defaults[i].color, NULL);
	}
	g_free (path);
	
	config->week_start_day = bonobo_config_get_long_with_default(config->db, "/Calendar/Display/WeekStartDay", 1, NULL);
	if (locale_supports_12_hour_format()) {
		config->time_24hour = bonobo_config_get_boolean_with_default(config->db, "/Calendar/Display/Use24HourFormat", FALSE, NULL);
	} else {
		config->time_24hour = TRUE;
	}
}

#define bonobo_config_set_string_wrapper(db, path, val, ev) bonobo_config_set_string (db, path, val ? val : "", ev)

void
mail_config_write_account_sig (MailConfigAccount *account, gint i)
{
	char *path;
	
	mail_config_init ();
	
	if (i == -1) {
		GSList *link;
		
		link = g_slist_find (config->accounts, account);
		if (!link) {
			g_warning ("Can't find account in accounts list");
			return;
		}
		i = g_slist_position (config->accounts, link);
	}
	
	/* id signatures */
	path = g_strdup_printf ("/Mail/Accounts/identity_def_signature_%d", i);
	bonobo_config_set_long (config->db, path, account->id->def_signature
				? account->id->def_signature->id : -1, NULL);
	g_free (path);
	
	path = g_strdup_printf ("/Mail/Accounts/identity_autogenerated_signature_%d", i);
	bonobo_config_set_boolean (config->db, path, account->id->auto_signature, NULL);
	g_free (path);
}

void
mail_config_write (void)
{
	CORBA_Environment ev;
	int len, i, default_num;
	
	/* Accounts */
	
	if (!config)
		return;
	
	CORBA_exception_init (&ev);
	Bonobo_ConfigDatabase_removeDir (config->db, "/Mail/Accounts", &ev);
	CORBA_exception_init (&ev);
	Bonobo_ConfigDatabase_removeDir (config->db, "/News/Sources", &ev);
	CORBA_exception_init (&ev);
	Bonobo_ConfigDatabase_sync (config->db, &ev);
	
	config_write_signatures ();
	
	len = g_slist_length (config->accounts);
	bonobo_config_set_long (config->db,
				"/Mail/Accounts/num", len, NULL);
	
	default_num = mail_config_get_default_account_num ();
	bonobo_config_set_long (config->db,
				"/Mail/Accounts/default_account", default_num, NULL);
	
	for (i = 0; i < len; i++) {
		MailConfigAccount *account;
		char *path;
		
		account = g_slist_nth_data (config->accounts, i);
		
		/* account info */
		path = g_strdup_printf ("/Mail/Accounts/account_name_%d", i);
		bonobo_config_set_string_wrapper (config->db, path, account->name, NULL);
		g_free (path);
		
		path = g_strdup_printf ("/Mail/Accounts/account_drafts_folder_uri_%d", i);
		bonobo_config_set_string_wrapper (config->db, path, 
						  account->drafts_folder_uri, NULL);
		g_free (path);
		
		path = g_strdup_printf ("/Mail/Accounts/account_sent_folder_uri_%d", i);
		bonobo_config_set_string_wrapper (config->db, path, 
						  account->sent_folder_uri, NULL);
		g_free (path);
		
		path = g_strdup_printf ("/Mail/Accounts/account_always_cc_%d", i);
		bonobo_config_set_boolean (config->db, path, account->always_cc, NULL);
		g_free (path);
		
		path = g_strdup_printf ("/Mail/Accounts/account_always_cc_addrs_%d", i);
		bonobo_config_set_string_wrapper (config->db, path, 
						  account->cc_addrs, NULL);
		g_free (path);
		
		path = g_strdup_printf ("/Mail/Accounts/account_always_bcc_%d", i);
		bonobo_config_set_boolean (config->db, path, account->always_bcc, NULL);
		g_free (path);
		
		path = g_strdup_printf ("/Mail/Accounts/account_always_bcc_addrs_%d", i);
		bonobo_config_set_string_wrapper (config->db, path, 
						  account->bcc_addrs, NULL);
		g_free (path);
		
		/* account pgp options */
		path = g_strdup_printf ("/Mail/Accounts/account_pgp_key_%d", i);
		bonobo_config_set_string_wrapper (config->db, path, account->pgp_key, NULL);
		g_free (path);
		
		path = g_strdup_printf ("/Mail/Accounts/account_pgp_always_sign_%d", i);
		bonobo_config_set_boolean (config->db, path, account->pgp_always_sign, NULL);
		g_free (path);
		
		path = g_strdup_printf ("/Mail/Accounts/account_pgp_no_imip_sign_%d", i);
		bonobo_config_set_boolean (config->db, path, account->pgp_no_imip_sign, NULL);
		g_free (path);
		
		path = g_strdup_printf ("/Mail/Accounts/account_pgp_encrypt_to_self_%d", i);
		bonobo_config_set_boolean (config->db, path, 
					   account->pgp_encrypt_to_self, NULL);
		g_free (path);
		
		path = g_strdup_printf ("/Mail/Accounts/account_pgp_always_trust_%d", i);
		bonobo_config_set_boolean (config->db, path, account->pgp_always_trust, NULL);
		g_free (path);
		
		/* account s/mime options */
		path = g_strdup_printf ("/Mail/Accounts/account_smime_key_%d", i);
		bonobo_config_set_string_wrapper (config->db, path, account->smime_key, NULL);
		g_free (path);
		
		path = g_strdup_printf ("/Mail/Accounts/account_smime_always_sign_%d", i);
		bonobo_config_set_boolean (config->db, path, account->smime_always_sign, NULL);
		g_free (path);
		
		path = g_strdup_printf ("/Mail/Accounts/account_smime_encrypt_to_self_%d", i);
		bonobo_config_set_boolean (config->db, path, account->smime_encrypt_to_self, NULL);
		g_free (path);
		
		/* identity info */
		path = g_strdup_printf ("/Mail/Accounts/identity_name_%d", i);
		bonobo_config_set_string_wrapper (config->db, path, account->id->name, NULL);
		g_free (path);
		
		path = g_strdup_printf ("/Mail/Accounts/identity_address_%d", i);
		bonobo_config_set_string_wrapper (config->db, path, account->id->address, NULL);
		g_free (path);
		
		path = g_strdup_printf ("/Mail/Accounts/identity_reply_to_%d", i);
		bonobo_config_set_string_wrapper (config->db, path, account->id->reply_to, NULL);
		g_free (path);
		
		path = g_strdup_printf ("/Mail/Accounts/identity_organization_%d", i);
		bonobo_config_set_string_wrapper (config->db, path, account->id->organization, NULL);
		g_free (path);
		
		mail_config_write_account_sig (account, i);
		
		path = g_strdup_printf ("/Mail/Accounts/identity_autogenerated_signature_%d", i);
		bonobo_config_set_boolean (config->db, path, account->id->auto_signature, NULL);
		g_free (path);
		
		/* source info */
		path = g_strdup_printf ("/Mail/Accounts/source_url_%d", i);
		bonobo_config_set_string_wrapper (config->db, path, account->source->url, NULL);
		g_free (path);
		
		path = g_strdup_printf ("/Mail/Accounts/source_keep_on_server_%d", i);
		bonobo_config_set_boolean (config->db, path, account->source->keep_on_server, NULL);
		g_free (path);
		
		path = g_strdup_printf ("/Mail/Accounts/source_auto_check_%d", i);
		bonobo_config_set_boolean (config->db, path, account->source->auto_check, NULL);
		g_free (path);
		
		path = g_strdup_printf ("/Mail/Accounts/source_auto_check_time_%d", i);
		bonobo_config_set_long (config->db, path, account->source->auto_check_time, NULL);
		g_free (path);
		
		path = g_strdup_printf ("/Mail/Accounts/source_enabled_%d", i);
		bonobo_config_set_boolean (config->db, path, account->source->enabled, NULL);
		g_free (path);
		
		path = g_strdup_printf ("/Mail/Accounts/source_save_passwd_%d", i);
		bonobo_config_set_boolean (config->db, path, account->source->save_passwd, NULL);
		g_free (path);
		
		/* transport info */
		path = g_strdup_printf ("/Mail/Accounts/transport_url_%d", i);
		bonobo_config_set_string_wrapper (config->db, path, account->transport->url, NULL);
		g_free (path);
		
		path = g_strdup_printf ("/Mail/Accounts/transport_save_passwd_%d", i);
		bonobo_config_set_boolean (config->db, path, account->transport->save_passwd, NULL);
		g_free (path);
	}
	
	CORBA_exception_init (&ev);
	Bonobo_ConfigDatabase_sync (config->db, &ev);
	CORBA_exception_free (&ev);
}

static gboolean
hash_save_state (gpointer key, gpointer value, gpointer user_data)
{
	char *path;
	gboolean bool = GPOINTER_TO_INT (value);
	
	path = g_strconcat ("/Mail/", (char *)user_data, "/", (char *)key, 
			    NULL);
	bonobo_config_set_boolean (config->db, path, bool, NULL);
	g_free (path);
	g_free (key);
	
	return TRUE;
}

void
mail_config_write_on_exit (void)
{
	CORBA_Environment ev;
	MailConfigAccount *account;
	const GSList *accounts;
	char *path, *p;
	int i;
	
	if (config_write_timeout) {
		g_source_remove (config_write_timeout);
		config_write_timeout = 0;
		mail_config_write ();
	}

	/* Show Messages Threaded */
	bonobo_config_set_boolean (config->db, "/Mail/Display/thread_list", 
				   config->thread_list, NULL);
	
	bonobo_config_set_boolean (config->db, "/Mail/Display/thread_subject", 
				   config->thread_subject, NULL);
	
	/* Show Message Preview */
	bonobo_config_set_boolean (config->db, "/Mail/Display/preview_pane",
				   config->show_preview, NULL);
	
	/* Hide deleted automatically */
	bonobo_config_set_boolean (config->db, "/Mail/Display/hide_deleted", 
				   config->hide_deleted, NULL);
	
	/* Size of vpaned in mail view */
	bonobo_config_set_long (config->db, "/Mail/Display/paned_size", 
				config->paned_size, NULL);
	
	/* Mark as seen toggle */
	bonobo_config_set_boolean (config->db, "/Mail/Display/do_seen_timeout",
				   config->do_seen_timeout, NULL);
	/* Mark as seen timeout */
	bonobo_config_set_long (config->db, "/Mail/Display/seen_timeout", 
				config->seen_timeout, NULL);
	
	/* Format */
	bonobo_config_set_boolean (config->db, "/Mail/Format/send_html", 
				   config->send_html, NULL);
	
	/* Confirm Sending Unwanted HTML */
	bonobo_config_set_boolean (config->db, "/Mail/Format/confirm_unwanted_html",
				   config->confirm_unwanted_html, NULL);
	
	/* Citation */
	bonobo_config_set_boolean (config->db, 
				   "/Mail/Display/citation_highlight", 
				   config->citation_highlight, NULL);
	
	bonobo_config_set_long (config->db, "/Mail/Display/citation_color",
				config->citation_color, NULL);
	
	/* Goto next folder */
	bonobo_config_set_boolean (config->db, "/Mail/MessageList/goto_next_folder",
				   config->goto_next_folder, NULL);
	
	/* Empty Subject */
	bonobo_config_set_boolean (config->db, "/Mail/Prompts/empty_subject",
				   config->prompt_empty_subject, NULL);
	
	/* Only Bcc */
	bonobo_config_set_boolean (config->db, "/Mail/Prompts/only_bcc",
				   config->prompt_only_bcc, NULL);
	
	/* Expunge */
	bonobo_config_set_boolean (config->db, "/Mail/Prompts/confirm_expunge",
				   config->confirm_expunge, NULL);
	
	/* Goto next folder */
	bonobo_config_set_boolean (config->db, "/Mail/Prompts/confirm_goto_next_folder",
				   config->confirm_goto_next_folder, NULL);
	
	/* HTTP images */
	bonobo_config_set_long (config->db, "/Mail/Display/http_images", 
				config->http_mode, NULL);
	
	/* Forwarding */
	bonobo_config_set_long (config->db, 
				"/Mail/Format/default_forward_style", 
				config->default_forward_style, NULL);
	
	/* Replying */
	bonobo_config_set_long (config->db, 
				"/Mail/Format/default_reply_style", 
				config->default_reply_style, NULL);
	
	/* Message Display */
	bonobo_config_set_long (config->db, 
				"/Mail/Format/message_display_style", 
				config->message_display_style, NULL);
	
	/* Default charset */
	bonobo_config_set_string_wrapper (config->db, "/Mail/Format/default_charset", 
					  config->default_charset, NULL);
	
	/* Trash folders */
	bonobo_config_set_boolean (config->db, "/Mail/Trash/empty_on_exit", 
				   config->empty_trash_on_exit, NULL);
	
	/* Filter logging */
	bonobo_config_set_boolean (config->db, "/Mail/Filters/log",
				   config->filter_log, NULL);
	
	bonobo_config_set_string_wrapper (config->db, "/Mail/Filters/log_path",
					  config->filter_log_path, NULL);
	
	/* New Mail Notification */
	bonobo_config_set_long (config->db, "/Mail/Notify/new_mail_notification", 
				config->notify, NULL);
	
	bonobo_config_set_string_wrapper (config->db, "/Mail/Notify/new_mail_notification_sound_file",
					  config->notify_filename, NULL);
	
	/* X-Mailer Display */
	bonobo_config_set_long (config->db, 
				"/Mail/Display/x_mailer_display_style", 
				config->x_mailer_display_style, NULL);
	
	/* last filesel dir */
	bonobo_config_set_string_wrapper (config->db, "/Mail/Filesel/last_filesel_dir",
					  config->last_filesel_dir, NULL);
	
	/* Color labels */
	/* Note: we avoid having to malloc/free 10 times this way... */
	path = g_malloc (sizeof ("/Mail/Labels/") + sizeof ("label_#") + 1);
	strcpy (path, "/Mail/Labels/label_#");
	p = path + strlen (path) - 1;
	for (i = 0; i < 5; i++) {
		*p = '0' + i;
		bonobo_config_set_string_wrapper (config->db, path, config->labels[i].name, NULL);
	}
	strcpy (path, "/Mail/Labels/color_#");
	p = path + strlen (path) - 1;
	for (i = 0; i < 5; i++) {
		*p = '0' + i;
		bonobo_config_set_long (config->db, path, config->labels[i].color, NULL);
	}
	g_free (path);
	
	/* Message Threading */
	if (config->threaded_hash)
		g_hash_table_foreach_remove (config->threaded_hash, hash_save_state, "Threads");
	
	/* Message Preview */
	if (config->preview_hash)
		g_hash_table_foreach_remove (config->preview_hash, hash_save_state, "Preview");

	CORBA_exception_init (&ev);
	Bonobo_ConfigDatabase_sync (config->db, &ev);
	CORBA_exception_free (&ev);
	
	/* Passwords */
	
	/* then we make sure the ones we want to remember are in the
           session cache */
	accounts = mail_config_get_accounts ();
	for ( ; accounts; accounts = accounts->next) {
		char *passwd;
		account = accounts->data;
		if (account->source->save_passwd && account->source->url) {
			passwd = mail_session_get_password (account->source->url);
			mail_session_forget_password (account->source->url);
			mail_session_add_password (account->source->url, passwd);
			g_free (passwd);
		}
		
		if (account->transport->save_passwd && account->transport->url) {
			passwd = mail_session_get_password (account->transport->url);
			mail_session_forget_password (account->transport->url);
			mail_session_add_password (account->transport->url, passwd);
			g_free (passwd);
		}
	}
	
	/* then we clear out our component passwords */
	e_passwords_clear_component_passwords ();
	
	/* then we remember them */
	accounts = mail_config_get_accounts ();
	for ( ; accounts; accounts = accounts->next) {
		account = accounts->data;
		if (account->source->save_passwd && account->source->url)
			mail_session_remember_password (account->source->url);
		
		if (account->transport->save_passwd && account->transport->url)
			mail_session_remember_password (account->transport->url);
	}
	
	/* now do cleanup */
	mail_config_clear ();
}

/* Accessor functions */
gboolean
mail_config_is_configured (void)
{
	return config->accounts != NULL;
}

gboolean
mail_config_is_corrupt (void)
{
	return config->corrupt;
}

static char *
uri_to_key (const char *uri)
{
	char *rval, *ptr;
	
	if (!uri)
		return NULL;
	
	rval = g_strdup (uri);
	
	for (ptr = rval; *ptr; ptr++)
		if (*ptr == '/' || *ptr == ':')
			*ptr = '_';
	
	return rval;
}

gboolean
mail_config_get_thread_subject (void)
{
	return config->thread_subject;
}

void
mail_config_set_thread_subject (gboolean thread_subject)
{
	config->thread_subject = thread_subject;
}

gboolean
mail_config_get_empty_trash_on_exit (void)
{
	return config->empty_trash_on_exit;
}

void
mail_config_set_empty_trash_on_exit (gboolean value)
{
	config->empty_trash_on_exit = value;
}

gboolean
mail_config_get_show_preview (const char *uri)
{
	if (uri && *uri) {
		gpointer key, val;
		char *dbkey;
		
		dbkey = uri_to_key (uri);
		
		if (!config->preview_hash)
			config->preview_hash = g_hash_table_new (g_str_hash, g_str_equal);
		
		if (!g_hash_table_lookup_extended (config->preview_hash, dbkey, &key, &val)) {
			gboolean value;
			char *str;
			
			str = g_strdup_printf ("/Mail/Preview/%s", dbkey);
			value = bonobo_config_get_boolean_with_default (config->db, str, TRUE, NULL);
			g_free (str);
			
			g_hash_table_insert (config->preview_hash, dbkey,
					     GINT_TO_POINTER (value));
			
			return value;
		} else {
			g_free (dbkey);
			return GPOINTER_TO_INT (val);
		}
	}
	
	/* return the default value */
	
	return config->show_preview;
}

void
mail_config_set_show_preview (const char *uri, gboolean value)
{
	if (uri && *uri) {
		char *dbkey = uri_to_key (uri);
		gpointer key, val;
		
		if (!config->preview_hash)
			config->preview_hash = g_hash_table_new (g_str_hash, g_str_equal);
		
		if (g_hash_table_lookup_extended (config->preview_hash, dbkey, &key, &val)) {
			g_hash_table_insert (config->preview_hash, dbkey,
					     GINT_TO_POINTER (value));
			g_free (dbkey);
		} else {
			g_hash_table_insert (config->preview_hash, dbkey, 
					     GINT_TO_POINTER (value));
		}
	} else
		config->show_preview = value;
}

gboolean
mail_config_get_thread_list (const char *uri)
{
	if (uri && *uri) {
		gpointer key, val;
		char *dbkey;
		
		dbkey = uri_to_key (uri);
		
		if (!config->threaded_hash)
			config->threaded_hash = g_hash_table_new (g_str_hash, g_str_equal);
		
		if (!g_hash_table_lookup_extended (config->threaded_hash, dbkey, &key, &val)) {
			gboolean value;
			char *str;
			
			str = g_strdup_printf ("/Mail/Threads/%s", dbkey);
			value = bonobo_config_get_boolean_with_default (config->db, str, FALSE, NULL);
			g_free (str);
			
			g_hash_table_insert (config->threaded_hash, dbkey,
					     GINT_TO_POINTER (value));
			
			return value;
		} else {
			g_free(dbkey);
			return GPOINTER_TO_INT (val);
		}
	}
	
	/* return the default value */
	
	return config->thread_list;
}

void
mail_config_set_thread_list (const char *uri, gboolean value)
{
	if (uri && *uri) {
		char *dbkey = uri_to_key (uri);
		gpointer key, val;
		
		if (!config->threaded_hash)
			config->threaded_hash = g_hash_table_new (g_str_hash, g_str_equal);
		
		if (g_hash_table_lookup_extended (config->threaded_hash, dbkey, &key, &val)) {
			g_hash_table_insert (config->threaded_hash, dbkey,
					     GINT_TO_POINTER (value));
			g_free (dbkey);
		} else {
			g_hash_table_insert (config->threaded_hash, dbkey, 
					     GINT_TO_POINTER (value));
		}
	} else
		config->thread_list = value;
}

gboolean
mail_config_get_filter_log (void)
{
	return config->filter_log;
}

void
mail_config_set_filter_log (gboolean value)
{
	config->filter_log = value;
}

const char *
mail_config_get_filter_log_path (void)
{
	return config->filter_log_path;
}

void
mail_config_set_filter_log_path (const char *path)
{
	g_free (config->filter_log_path);
	config->filter_log_path = g_strdup (path);
}

const char *
mail_config_get_last_filesel_dir (void)
{
	if (config->last_filesel_dir)
		return config->last_filesel_dir;
	else
		return g_get_home_dir ();
}

void
mail_config_set_last_filesel_dir (const char *path)
{
	g_free (config->last_filesel_dir);
	config->last_filesel_dir = g_strdup (path);
}

gboolean
mail_config_get_hide_deleted (void)
{
	return config->hide_deleted;
}

void
mail_config_set_hide_deleted (gboolean value)
{
	config->hide_deleted = value;
}

int
mail_config_get_paned_size (void)
{
	return config->paned_size;
}

void
mail_config_set_paned_size (int value)
{
	config->paned_size = value;
}

gboolean
mail_config_get_send_html (void)
{
	return config->send_html;
}

void
mail_config_set_send_html (gboolean send_html)
{
	config->send_html = send_html;
}

gboolean
mail_config_get_confirm_unwanted_html (void)
{
	return config->confirm_unwanted_html;
}

void
mail_config_set_confirm_unwanted_html (gboolean confirm)
{
	config->confirm_unwanted_html = confirm;
}

gboolean
mail_config_get_citation_highlight (void)
{
	return config->citation_highlight;
}

void
mail_config_set_citation_highlight (gboolean citation_highlight)
{
	config->citation_highlight = citation_highlight;
}

guint32
mail_config_get_citation_color (void)
{
	return config->citation_color;
}

void
mail_config_set_citation_color (guint32 citation_color)
{
	config->citation_color = citation_color;
}

gboolean
mail_config_get_do_seen_timeout (void)
{
	return config->do_seen_timeout;
}

void
mail_config_set_do_seen_timeout (gboolean do_seen_timeout)
{
	config->do_seen_timeout = do_seen_timeout;
}

int
mail_config_get_mark_as_seen_timeout (void)
{
	return config->seen_timeout;
}

void
mail_config_set_mark_as_seen_timeout (int timeout)
{
	config->seen_timeout = timeout;
}

gboolean
mail_config_get_prompt_empty_subject (void)
{
	return config->prompt_empty_subject;
}

void
mail_config_set_prompt_empty_subject (gboolean value)
{
	config->prompt_empty_subject = value;
}

gboolean
mail_config_get_prompt_only_bcc (void)
{
	return config->prompt_only_bcc;
}

void
mail_config_set_prompt_only_bcc (gboolean value)
{
	config->prompt_only_bcc = value;
}

gboolean
mail_config_get_confirm_expunge (void)
{
	return config->confirm_expunge;
}

void
mail_config_set_confirm_expunge (gboolean value)
{
	config->confirm_expunge = value;
}

gboolean
mail_config_get_confirm_goto_next_folder (void)
{
	return config->confirm_goto_next_folder;
}

void
mail_config_set_confirm_goto_next_folder (gboolean value)
{
	config->confirm_goto_next_folder = value;
}

gboolean
mail_config_get_goto_next_folder (void)
{
	return config->goto_next_folder;
}

void
mail_config_set_goto_next_folder (gboolean value)
{
	config->goto_next_folder = value;
}

MailConfigHTTPMode
mail_config_get_http_mode (void)
{
	return config->http_mode;
}

void
mail_config_set_http_mode (MailConfigHTTPMode mode)
{
	config->http_mode = mode;
}

MailConfigForwardStyle
mail_config_get_default_forward_style (void)
{
	return config->default_forward_style;
}

void
mail_config_set_default_forward_style (MailConfigForwardStyle style)
{
	config->default_forward_style = style;
}

MailConfigReplyStyle
mail_config_get_default_reply_style (void)
{
	return config->default_reply_style;
}

void
mail_config_set_default_reply_style (MailConfigReplyStyle style)
{
	config->default_reply_style = style;
}

MailConfigDisplayStyle
mail_config_get_message_display_style (void)
{
	return config->message_display_style;
}

void
mail_config_set_message_display_style (MailConfigDisplayStyle style)
{
	config->message_display_style = style;
}

const char *
mail_config_get_default_charset (void)
{
	return config->default_charset;
}

void
mail_config_set_default_charset (const char *charset)
{
	g_free (config->default_charset);
	config->default_charset = g_strdup (charset);
}

MailConfigNewMailNotify
mail_config_get_new_mail_notify (void)
{
	return config->notify;
}

void
mail_config_set_new_mail_notify (MailConfigNewMailNotify type)
{
	config->notify = type;
}

const char *
mail_config_get_new_mail_notify_sound_file (void)
{
	return config->notify_filename;
}

void
mail_config_set_new_mail_notify_sound_file (const char *filename)
{
	g_free (config->notify_filename);
	config->notify_filename = g_strdup (filename);
}

MailConfigXMailerDisplayStyle
mail_config_get_x_mailer_display_style (void)
{
	return config->x_mailer_display_style;
}

void
mail_config_set_x_mailer_display_style (MailConfigXMailerDisplayStyle style)
{
	config->x_mailer_display_style = style;
}

const char *
mail_config_get_label_name (int label)
{
	g_return_val_if_fail (label >= 0 && label < 5, NULL);
	
	if (!config->labels[label].name)
		config->labels[label].name = g_strdup (U_(label_defaults[label].name));
	
	return config->labels[label].name;
}

void
mail_config_set_label_name (int label, const char *name)
{
	g_return_if_fail (label >= 0 && label < 5);
	
	if (!name)
		name = U_(label_defaults[label].name);
	
	g_free (config->labels[label].name);
	config->labels[label].name = g_strdup (name);
}

guint32
mail_config_get_label_color (int label)
{
	g_return_val_if_fail (label >= 0 && label < 5, 0);
	
	return config->labels[label].color;
}

void
mail_config_set_label_color (int label, guint32 color)
{
	g_return_if_fail (label >= 0 && label < 5);
	
	g_free (config->labels[label].string);
	config->labels[label].string = NULL;
	
	config->labels[label].color = color;
}

const char *
mail_config_get_label_color_string (int label)
{
	g_return_val_if_fail (label >= 0 && label < 5, NULL);
	
	if (!config->labels[label].string) {
		guint32 rgb = config->labels[label].color;
		char *colour;
		
		colour = g_strdup_printf ("#%.2x%.2x%.2x",
					  (rgb & 0xff0000) >> 16,
					  (rgb & 0xff00) >> 8,
					  rgb & 0xff);
		
		config->labels[label].string = colour;
	}
	
	return config->labels[label].string;
}

gboolean
mail_config_find_account (const MailConfigAccount *account)
{
	return g_slist_find (config->accounts, (gpointer) account) != NULL;
}

const MailConfigAccount *
mail_config_get_default_account (void)
{
	MailConfigAccount *account;
	
	if (config == NULL) {
		mail_config_init ();
	}
	
	if (!config->accounts)
		return NULL;
	
	account = g_slist_nth_data (config->accounts,
				    config->default_account);
	
	/* Looks like we have no default, so make the first account
           the default */
	if (account == NULL) {
		mail_config_set_default_account_num (0);
		account = config->accounts->data;
	}
	
	return account;
}

const MailConfigAccount *
mail_config_get_account_by_name (const char *account_name)
{
	/* FIXME: this should really use a hash */
	const MailConfigAccount *account;
	GSList *l;
	
	l = config->accounts;
	while (l) {
		account = l->data;
		if (account && !strcmp (account->name, account_name))
			return account;
		
		l = l->next;
	}
	
	return NULL;
}

const MailConfigAccount *
mail_config_get_account_by_source_url (const char *source_url)
{
	const MailConfigAccount *account;
	CamelProvider *provider;
	CamelURL *source;
	GSList *l;
	
	g_return_val_if_fail (source_url != NULL, NULL);
	
	provider = camel_session_get_provider (session, source_url, NULL);
	if (!provider)
		return NULL;
	
	source = camel_url_new (source_url, NULL);
	if (!source)
		return NULL;
	
	l = config->accounts;
	while (l) {
		account = l->data;
		
		if (account && account->source && account->source->url) {
			CamelURL *url;
			
			url = camel_url_new (account->source->url, NULL);
			if (url && provider->url_equal (url, source)) {
				camel_url_free (url);
				camel_url_free (source);
				return account;
			}
			
			if (url)
				camel_url_free (url);
		}
		
		l = l->next;
	}
	
	camel_url_free (source);
	
	return NULL;
}

const MailConfigAccount *
mail_config_get_account_by_transport_url (const char *transport_url)
{
	const MailConfigAccount *account;
	CamelProvider *provider;
	CamelURL *transport;
	GSList *l;
	
	g_return_val_if_fail (transport_url != NULL, NULL);
	
	provider = camel_session_get_provider (session, transport_url, NULL);
	if (!provider)
		return NULL;
	
	transport = camel_url_new (transport_url, NULL);
	if (!transport)
		return NULL;
	
	l = config->accounts;
	while (l) {
		account = l->data;
		
		if (account && account->transport && account->transport->url) {
			CamelURL *url;
			
			url = camel_url_new (account->transport->url, NULL);
			if (url && provider->url_equal (url, transport)) {
				camel_url_free (url);
				camel_url_free (transport);
				return account;
			}
			
			if (url)
				camel_url_free (url);
		}
		
		l = l->next;
	}
	
	camel_url_free (transport);
	
	return NULL;
}

const GSList *
mail_config_get_accounts (void)
{
	g_assert (config != NULL);
	
	return config->accounts;
}

void
mail_config_add_account (MailConfigAccount *account)
{
	config->accounts = g_slist_append (config->accounts, account);
}

const GSList *
mail_config_remove_account (MailConfigAccount *account)
{
	int index;
	
	/* Removing the current default, so make the first account the
           default */
	if (account == mail_config_get_default_account ()) {
		config->default_account = 0;
	} else {
		/* adjust the default to make sure it points to the same one */
		index = g_slist_index (config->accounts, account);
		if (config->default_account > index)
			config->default_account--;
	}
	
	config->accounts = g_slist_remove (config->accounts, account);
	account_destroy (account);
	
	return config->accounts;
}

int
mail_config_get_default_account_num (void)
{
	return config->default_account;
}

static void
mail_config_set_default_account_num (int new_default)
{
	config->default_account = new_default;
}

void
mail_config_set_default_account (const MailConfigAccount *account)
{
	int index;
	
	index = g_slist_index (config->accounts, (void *) account);
	if (index == -1)
		return;
	
	config->default_account = index;
	
	return;
}

const MailConfigIdentity *
mail_config_get_default_identity (void)
{
	const MailConfigAccount *account;
	
	account = mail_config_get_default_account ();
	if (account)
		return account->id;
	else
		return NULL;
}

const MailConfigService *
mail_config_get_default_transport (void)
{
	const MailConfigAccount *account;
	const GSList *accounts;
	
	account = mail_config_get_default_account ();
	if (account && account->transport && account->transport->url)
		return account->transport;
	
	/* return the first account with a transport? */
	accounts = config->accounts;
	while (accounts) {
		account = accounts->data;
		
		if (account->transport && account->transport->url)
			return account->transport;
		
		accounts = accounts->next;
	}
	
	return NULL;
}

static char *
uri_to_evname (const char *uri, const char *prefix)
{
	char *safe;
	char *tmp;

	safe = g_strdup (uri);
	e_filename_make_safe (safe);
	/* blah, easiest thing to do */
	if (prefix[0] == '*')
		tmp = g_strdup_printf ("%s/%s%s.xml", evolution_dir, prefix + 1, safe);
	else
		tmp = g_strdup_printf ("%s/%s%s", evolution_dir, prefix, safe);
	g_free (safe);
	return tmp;
}

void
mail_config_uri_renamed(GCompareFunc uri_cmp, const char *old, const char *new)
{
	MailConfigAccount *ac;
	const GSList *l;
	int work = 0;
	gpointer oldkey, newkey, hashkey;
	gpointer val;
	char *oldname, *newname;
	char *cachenames[] = { "config/hidestate-", 
			       "config/et-expanded-", 
			       "config/et-header-", 
			       "*views/mail/current_view-",
			       "*views/mail/custom_view-",
			       NULL };
	int i;

	l = mail_config_get_accounts();
	while (l) {
		ac = l->data;
		if (ac->sent_folder_uri && uri_cmp(ac->sent_folder_uri, old)) {
			g_free(ac->sent_folder_uri);
			ac->sent_folder_uri = g_strdup(new);
			work = 1;
		}
		if (ac->drafts_folder_uri && uri_cmp(ac->drafts_folder_uri, old)) {
			g_free(ac->drafts_folder_uri);
			ac->drafts_folder_uri = g_strdup(new);
			work = 1;
		}
		l = l->next;
	}

	oldkey = uri_to_key (old);
	newkey = uri_to_key (new);

	/* call this to load the hash table and the key */
	mail_config_get_thread_list (old);
	if (g_hash_table_lookup_extended (config->threaded_hash, oldkey, &hashkey, &val)) {
		/*printf ("changing key in threaded_hash\n");*/
		g_hash_table_remove (config->threaded_hash, hashkey);
		g_hash_table_insert (config->threaded_hash, g_strdup(newkey), val);
		work = 2;
	}

	/* ditto */
	mail_config_get_show_preview (old);
	if (g_hash_table_lookup_extended (config->preview_hash, oldkey, &hashkey, &val)) {
		/*printf ("changing key in preview_hash\n");*/
		g_hash_table_remove (config->preview_hash, hashkey);
		g_hash_table_insert (config->preview_hash, g_strdup(newkey), val);
		work = 2;
	}

	g_free (oldkey);
	g_free (newkey);

	/* ignore return values or if the files exist or
	 * not, doesn't matter */

	for (i = 0; cachenames[i]; i++) {
		oldname = uri_to_evname (old, cachenames[i]);
		newname = uri_to_evname (new, cachenames[i]);
		/*printf ("** renaming %s to %s\n", oldname, newname);*/
		rename (oldname, newname);
		g_free (oldname);
		g_free (newname);
	}

	/* nasty ... */
	if (work)
		mail_config_write();
}

void
mail_config_uri_deleted(GCompareFunc uri_cmp, const char *uri)
{
	MailConfigAccount *ac;
	const GSList *l;
	int work = 0;
	/* assumes these can't be removed ... */
	extern char *default_sent_folder_uri, *default_drafts_folder_uri;

	l = mail_config_get_accounts();
	while (l) {
		ac = l->data;
		if (ac->sent_folder_uri && uri_cmp(ac->sent_folder_uri, uri)) {
			g_free(ac->sent_folder_uri);
			ac->sent_folder_uri = g_strdup(default_sent_folder_uri);
			work = 1;
		}
		if (ac->drafts_folder_uri && uri_cmp(ac->drafts_folder_uri, uri)) {
			g_free(ac->drafts_folder_uri);
			ac->drafts_folder_uri = g_strdup(default_drafts_folder_uri);
			work = 1;
		}
		l = l->next;
	}

	/* nasty again */
	if (work)
		mail_config_write();
}

GSList *
mail_config_get_sources (void)
{
	const GSList *accounts;
	GSList *sources = NULL;
	
	accounts = mail_config_get_accounts ();
	while (accounts) {
		const MailConfigAccount *account = accounts->data;
		
		if (account->source)
			sources = g_slist_append (sources, account->source);
		
		accounts = accounts->next;
	}
	
	return sources;
}

void 
mail_config_service_set_save_passwd (MailConfigService *service, gboolean save_passwd)
{
	service->save_passwd = save_passwd;
}

char *
mail_config_folder_to_safe_url (CamelFolder *folder)
{
	char *url;
	
	url = mail_tools_folder_to_url (folder);
	e_filename_make_safe (url);
	
	return url;
}

char *
mail_config_folder_to_cachename (CamelFolder *folder, const char *prefix)
{
	char *url, *filename;
	
	url = mail_config_folder_to_safe_url (folder);
	filename = g_strdup_printf ("%s/config/%s%s", evolution_dir, prefix, url);
	g_free (url);
	
	return filename;
}


/* Async service-checking/authtype-lookup code. */
struct _check_msg {
	struct _mail_msg msg;

	const char *url;
	CamelProviderType type;
	GList **authtypes;
	gboolean *success;
};

static char *
check_service_describe (struct _mail_msg *mm, int complete)
{
	return g_strdup (_("Checking Service"));
}

static void
check_service_check (struct _mail_msg *mm)
{
	struct _check_msg *m = (struct _check_msg *)mm;
	CamelService *service = NULL;

	camel_operation_register(mm->cancel);

	service = camel_session_get_service (session, m->url, m->type, &mm->ex);
	if (!service) {
		camel_operation_unregister(mm->cancel);
		return;
	}

	if (m->authtypes)
		*m->authtypes = camel_service_query_auth_types (service, &mm->ex);
	else
		camel_service_connect (service, &mm->ex);

	camel_object_unref (CAMEL_OBJECT (service));
	*m->success = !camel_exception_is_set(&mm->ex);

	camel_operation_unregister(mm->cancel);
}

static struct _mail_msg_op check_service_op = {
	check_service_describe,
	check_service_check,
	NULL,
	NULL
};

static void
check_cancelled (GnomeDialog *dialog, int button, gpointer data)
{
	int *msg_id = data;

	mail_msg_cancel (*msg_id);
}

/**
 * mail_config_check_service:
 * @url: service url
 * @type: provider type
 * @authtypes: set to list of supported authtypes on return if non-%NULL.
 *
 * Checks the service for validity. If @authtypes is non-%NULL, it will
 * be filled in with a list of supported authtypes.
 *
 * Return value: %TRUE on success or %FALSE on error.
 **/
gboolean
mail_config_check_service (const char *url, CamelProviderType type, GList **authtypes, GtkWindow *window)
{
	static GtkWidget *dialog = NULL;
	gboolean ret = FALSE;
	struct _check_msg *m;
	GtkWidget *label;
	int id;
	
	if (dialog) {
		gdk_window_raise (dialog->window);
		*authtypes = NULL;
		return FALSE;
	}
	
	m = mail_msg_new (&check_service_op, NULL, sizeof(*m));
	m->url = url;
	m->type = type;
	m->authtypes = authtypes;
	m->success = &ret;
	
	id = m->msg.seq;
	e_thread_put(mail_thread_queued, (EMsg *)m);
	
	dialog = gnome_dialog_new (_("Connecting to server..."),
				   GNOME_STOCK_BUTTON_CANCEL,
				   NULL);
	gnome_dialog_set_parent (GNOME_DIALOG (dialog), window);
	label = gtk_label_new (_("Connecting to server..."));
	gtk_box_pack_start (GTK_BOX(GNOME_DIALOG (dialog)->vbox),
			    label, TRUE, TRUE, 10);
	gnome_dialog_set_close (GNOME_DIALOG (dialog), FALSE);
	gtk_signal_connect (GTK_OBJECT (dialog), "clicked",
			    GTK_SIGNAL_FUNC (check_cancelled), &id);
	gtk_signal_connect (GTK_OBJECT (dialog), "delete_event",
			    GTK_SIGNAL_FUNC (check_cancelled), &id);
	gtk_window_set_modal (GTK_WINDOW (dialog), FALSE);
	gtk_widget_show_all (dialog);
	
	mail_msg_wait(id);
	
	gtk_widget_destroy (dialog);
	dialog = NULL;
	
	return ret;
}

/* MailConfig Bonobo object */
#define PARENT_TYPE BONOBO_X_OBJECT_TYPE
static BonoboObjectClass *parent_class = NULL;

/* For the bonobo object */
typedef struct _EvolutionMailConfig EvolutionMailConfig;
typedef struct _EvolutionMailConfigClass EvolutionMailConfigClass;

struct _EvolutionMailConfig {
	BonoboXObject parent;
};

struct _EvolutionMailConfigClass {
	BonoboXObjectClass parent_class;

	POA_GNOME_Evolution_MailConfig__epv epv;
};

static gboolean
do_config_write (gpointer data)
{
	config_write_timeout = 0;
	mail_config_write ();
	return FALSE;
}

static void
impl_GNOME_Evolution_MailConfig_addAccount (PortableServer_Servant servant,
					    const GNOME_Evolution_MailConfig_Account *account,
					    CORBA_Environment *ev)
{
	GNOME_Evolution_MailConfig_Service source, transport;
	GNOME_Evolution_MailConfig_Identity id;
	MailConfigAccount *mail_account;
	MailConfigService *mail_service;
	MailConfigIdentity *mail_id;
	
	if (mail_config_get_account_by_name (account->name)) {
		/* FIXME: we need an exception. */
		return;
	}
	
	mail_account = g_new0 (MailConfigAccount, 1);
	mail_account->name = g_strdup (account->name);
	
	/* Copy ID */
	id = account->id;
	mail_id = g_new0 (MailConfigIdentity, 1);
	mail_id->name = g_strdup (id.name);
	mail_id->address = g_strdup (id.address);
	mail_id->reply_to = g_strdup (id.reply_to);
	mail_id->organization = g_strdup (id.organization);
	
	mail_account->id = mail_id;
	
	/* Copy source */
	source = account->source;
	mail_service = g_new0 (MailConfigService, 1);
	if (source.url == NULL || strcmp (source.url, "none://") == 0) {
		mail_service->url = NULL;
	} else {
		mail_service->url = g_strdup (source.url);
	}
	mail_service->keep_on_server = source.keep_on_server;
	mail_service->auto_check = source.auto_check;
	mail_service->auto_check_time = source.auto_check_time;
	mail_service->save_passwd = source.save_passwd;
	mail_service->enabled = source.enabled;
	
	mail_account->source = mail_service;
	
	/* Copy transport */
	transport = account->transport;
	mail_service = g_new0 (MailConfigService, 1);
	if (transport.url == NULL) {
		mail_service->url = NULL;
	} else {
		mail_service->url = g_strdup (transport.url);
	}
	mail_service->url = g_strdup (transport.url);
	mail_service->keep_on_server = transport.keep_on_server;
	mail_service->auto_check = transport.auto_check;
	mail_service->auto_check_time = transport.auto_check_time;
	mail_service->save_passwd = transport.save_passwd;
	mail_service->enabled = transport.enabled;
	
	mail_account->transport = mail_service;
	
	/* Add new account */
	mail_config_add_account (mail_account);

	/* Don't write out the config right away in case the remote
	 * component is creating or removing multiple accounts.
	 */
	if (!config_write_timeout)
		config_write_timeout = g_timeout_add (2000, do_config_write, NULL);
}

static void
impl_GNOME_Evolution_MailConfig_removeAccount (PortableServer_Servant servant,
					       const CORBA_char *name,
					       CORBA_Environment *ev)
{
	MailConfigAccount *account;

	account = (MailConfigAccount *)mail_config_get_account_by_name (name);
	if (account)
		mail_config_remove_account (account);

	/* Don't write out the config right away in case the remote
	 * component is creating or removing multiple accounts.
	 */
	if (!config_write_timeout)
		config_write_timeout = g_timeout_add (2000, do_config_write, NULL);
}

static void
evolution_mail_config_class_init (EvolutionMailConfigClass *klass)
{
	POA_GNOME_Evolution_MailConfig__epv *epv = &klass->epv;

	parent_class = gtk_type_class (PARENT_TYPE);
	epv->addAccount = impl_GNOME_Evolution_MailConfig_addAccount;
	epv->removeAccount = impl_GNOME_Evolution_MailConfig_removeAccount;
}

static void
evolution_mail_config_init (EvolutionMailConfig *config)
{
}

BONOBO_X_TYPE_FUNC_FULL (EvolutionMailConfig,
			 GNOME_Evolution_MailConfig,
			 PARENT_TYPE,
			 evolution_mail_config);

static BonoboObject *
evolution_mail_config_factory_fn (BonoboGenericFactory *factory,
				  void *closure)
{
	EvolutionMailConfig *config;

	config = gtk_type_new (evolution_mail_config_get_type ());
	return BONOBO_OBJECT (config);
}

gboolean
evolution_mail_config_factory_init (void)
{
	BonoboGenericFactory *factory;
	
	factory = bonobo_generic_factory_new (MAIL_CONFIG_IID, 
					      evolution_mail_config_factory_fn,
					      NULL);
	if (factory == NULL) {
		g_warning ("Error starting MailConfig");
		return FALSE;
	}

	bonobo_running_context_auto_exit_unref (BONOBO_OBJECT (factory));
	return TRUE;
}

GList *
mail_config_get_signature_list (void)
{
	return config->signature_list;
}

static gchar *
get_new_signature_filename ()
{
	struct stat st_buf;
	gchar *filename;
	gint i;

	filename = g_strconcat (evolution_dir, "/signatures", NULL);
	if (lstat (filename, &st_buf)) {
		if (errno == ENOENT) {
			if (mkdir (filename, 0700))
				g_warning ("Fatal problem creating %s/signatures directory.", evolution_dir);
		} else
			g_warning ("Fatal problem with %s/signatures directory.", evolution_dir);
	}
	g_free (filename);

	for (i = 0; ; i ++) {
		filename = g_strdup_printf ("%s/signatures/signature-%d", evolution_dir, i);
		if (lstat (filename, &st_buf) == - 1 && errno == ENOENT) {
			gint fd;

			fd = creat (filename, 0600);
			if (fd >= 0) {
				close (fd);
				return filename;
			}
		}
		g_free (filename);
	}

	return NULL;
}

MailConfigSignature *
mail_config_signature_add (gboolean html, const gchar *script)
{
	MailConfigSignature *sig;

	sig = g_new0 (MailConfigSignature, 1);

	/* printf ("mail_config_signature_add %d\n", config->signatures); */
	sig->id = config->signatures;
	sig->name = g_strdup (U_("Unnamed"));
	if (script)
		sig->script = g_strdup (script);
	else
		sig->filename = get_new_signature_filename ();
	sig->html = html;

	config->signature_list = g_list_append (config->signature_list, sig);
	config->signatures ++;

	config_write_signature (sig, sig->id);
	config_write_signatures_num ();

	mail_config_signature_emit_event (MAIL_CONFIG_SIG_EVENT_ADDED, sig);
	/* printf ("mail_config_signature_add end\n"); */

	return sig;
}

static void
delete_unused_signature_file (const gchar *filename)
{
	gint len;
	gchar *signatures_dir;

	signatures_dir = g_strconcat (evolution_dir, "/signatures", NULL);

	/* remove signature file if it's in evolution dir and no other signature uses it */
	len = strlen (signatures_dir);
	if (filename && !strncmp (filename, signatures_dir, len)) {
		GList *l;
		gboolean only_one = TRUE;

		for (l = config->signature_list; l; l = l->next) {
			if (((MailConfigSignature *)l->data)->filename
			    && !strcmp (filename, ((MailConfigSignature *)l->data)->filename)) {
				only_one = FALSE;
				break;
			}
		}

		if (only_one) {
			unlink (filename);
		}
	}

	g_free (signatures_dir);
}

void
mail_config_signature_delete (MailConfigSignature *sig)
{
	GList *l, *next;
	GSList *al;
	gboolean after = FALSE;

	for (al = config->accounts; al; al = al->next) {
		MailConfigAccount *account;

		account = (MailConfigAccount *) al->data;

		if (account->id->def_signature == sig)
			account->id->def_signature = NULL;
	}

	for (l = config->signature_list; l; l = next) {
		next = l->next;
		if (after)
			((MailConfigSignature *) l->data)->id --;
		else if (l->data == sig) {
			config->signature_list = g_list_remove_link (config->signature_list, l);
			after = TRUE;
			config->signatures --;
		}
	}

	config_write_signatures ();
	delete_unused_signature_file (sig->filename);
	/* printf ("signatures: %d\n", config->signatures); */
	mail_config_signature_emit_event (MAIL_CONFIG_SIG_EVENT_DELETED, sig);
	signature_destroy (sig);
}

void
mail_config_signature_write (MailConfigSignature *sig)
{
	config_write_signature (sig, sig->id);
}

void
mail_config_signature_set_filename (MailConfigSignature *sig, const gchar *filename)
{
	gchar *old_filename = sig->filename;

	sig->filename = g_strdup (filename);
	if (old_filename) {
		delete_unused_signature_file (old_filename);
		g_free (old_filename);
	}
	mail_config_signature_write (sig);
}

void
mail_config_signature_set_name (MailConfigSignature *sig, const gchar *name)
{
	g_free (sig->name);
	sig->name = g_strdup (name);

	mail_config_signature_write (sig);
	mail_config_signature_emit_event (MAIL_CONFIG_SIG_EVENT_NAME_CHANGED, sig);
}

static GList *clients = NULL;

void
mail_config_signature_register_client (MailConfigSignatureClient client, gpointer data)
{
	clients = g_list_append (clients, client);
	clients = g_list_append (clients, data);
}

void
mail_config_signature_unregister_client (MailConfigSignatureClient client, gpointer data)
{
	GList *link;

	link = g_list_find (clients, data);
	clients = g_list_remove_link (clients, link->prev);
	clients = g_list_remove_link (clients, link);
}

void
mail_config_signature_emit_event (MailConfigSigEvent event, MailConfigSignature *sig)
{
	GList *l, *next;

	for (l = clients; l; l = next) {
		next = l->next->next;
		(*((MailConfigSignatureClient) l->data)) (event, sig, l->next->data);
	}
}

gchar *
mail_config_signature_run_script (gchar *script)
{
	int result, status;
	int in_fds[2];
	pid_t pid;
	
	if (pipe (in_fds) == -1) {
		g_warning ("Failed to create pipe to '%s': %s", script, g_strerror (errno));
		return NULL;
	}
	
	if (!(pid = fork ())) {
		/* child process */
		int maxfd, i;
		
		close (in_fds [0]);
		if (dup2 (in_fds[1], STDOUT_FILENO) < 0)
			_exit (255);
		close (in_fds [1]);
		
		setsid ();
		
		maxfd = sysconf (_SC_OPEN_MAX);
		if (maxfd > 0) {
			for (i = 0; i < maxfd; i++) {
				if (i != STDIN_FILENO && i != STDOUT_FILENO && i != STDERR_FILENO)
					close (i);
			}
		}
		
		
		execlp (script, script, NULL);
		g_warning ("Could not execute %s: %s\n", script, g_strerror (errno));
		_exit (255);
	} else if (pid < 0) {
		g_warning ("Failed to create create child process '%s': %s", script, g_strerror (errno));
		return NULL;
	} else {
		CamelStreamFilter *filtered_stream;
		CamelStreamMem *memstream;
		CamelMimeFilter *charenc;
		CamelStream *stream;
		GByteArray *buffer;
		const char *charset;
		char *content;
		
		/* parent process */
		close (in_fds[1]);
		
		stream = camel_stream_fs_new_with_fd (in_fds[0]);
		
		memstream = (CamelStreamMem *) camel_stream_mem_new ();
		buffer = g_byte_array_new ();
		camel_stream_mem_set_byte_array (memstream, buffer);
		
		camel_stream_write_to_stream (stream, (CamelStream *) memstream);
		camel_object_unref (stream);
		
		/* signature scripts are supposed to generate UTF-8 content, but because users
		   are known to not ever read the manual... we try to do our best if the
                   content isn't valid UTF-8 by assuming that the content is in the user's
		   preferred charset. */
		if (!g_utf8_validate (buffer->data, buffer->len, NULL)) {
			stream = (CamelStream *) memstream;
			memstream = (CamelStreamMem *) camel_stream_mem_new ();
			camel_stream_mem_set_byte_array (memstream, g_byte_array_new ());
			
			filtered_stream = camel_stream_filter_new_with_stream (stream);
			camel_object_unref (stream);
			
			charset = mail_config_get_default_charset ();
			charenc = (CamelMimeFilter *) camel_mime_filter_charset_new_convert (charset, "utf-8");
			camel_stream_filter_add (filtered_stream, charenc);
			camel_object_unref (charenc);
			
			camel_stream_write_to_stream ((CamelStream *) filtered_stream, (CamelStream *) memstream);
			camel_object_unref (filtered_stream);
			g_byte_array_free (buffer, TRUE);
			
			buffer = memstream->buffer;
		}
		
		camel_object_unref (memstream);
		
		g_byte_array_append (buffer, "", 1);
		content = buffer->data;
		g_byte_array_free (buffer, FALSE);
		
		/* wait for the script process to terminate */
		result = waitpid (pid, &status, 0);
		
		if (result == -1 && errno == EINTR) {
			/* child process is hanging... */
			kill (pid, SIGTERM);
			sleep (1);
			result = waitpid (pid, &status, WNOHANG);
			if (result == 0) {
				/* ...still hanging, set phasers to KILL */
				kill (pid, SIGKILL);
				sleep (1);
				result = waitpid (pid, &status, WNOHANG);
			}
		}
		
		return content;
	}
}

void
mail_config_signature_set_html (MailConfigSignature *sig, gboolean html)
{
	if (sig->html != html) {
		sig->html = html;
		mail_config_signature_write (sig);
		mail_config_signature_emit_event (MAIL_CONFIG_SIG_EVENT_HTML_CHANGED, sig);
	}
}

int
mail_config_get_week_start_day(void)
{
	return config->week_start_day;
}

int
mail_config_get_time_24hour(void)
{
	return config->time_24hour;
}

