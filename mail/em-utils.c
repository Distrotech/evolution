/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
 *
 *  Copyright 2003 Ximian, Inc. (www.ximian.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Street #330, Boston, MA 02111-1307, USA.
 *
 */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>

#include <camel/camel-stream-fs.h>

#include <filter/filter-editor.h>

#include "mail-mt.h"
#include "mail-ops.h"
#include "mail-tools.h"
#include "mail-config.h"
#include "mail-config-druid.h"
#include "message-tag-followup.h"

#include <e-util/e-dialog-utils.h>

#include "em-utils.h"
#include "em-composer-utils.h"


static EAccount *guess_account (CamelMimeMessage *message);


/* FIXME: move me somewhere else... */
static gboolean
e_question (GtkWindow *parent, int def, gboolean *again, const char *fmt, ...)
{
	GtkWidget *mbox, *check = NULL;
	va_list ap;
	int button;
	char *str;
	
	va_start (ap, fmt);
	str = g_strdup_vprintf (fmt, ap);
	va_end (ap);
	mbox = gtk_message_dialog_new (parent, GTK_DIALOG_DESTROY_WITH_PARENT,
				       GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
				       "%s", str);
	g_free (str);
	gtk_dialog_set_default_response ((GtkDialog *) mbox, def);
	if (again) {
		check = gtk_check_button_new_with_label (_("Don't show this message again."));
		gtk_box_pack_start ((GtkBox *)((GtkDialog *) mbox)->vbox, check, TRUE, TRUE, 10);
		gtk_widget_show (check);
	}
	
	button = gtk_dialog_run ((GtkDialog *) mbox);
	if (again)
		*again = !gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (check));
	gtk_widget_destroy (mbox);
	
	return button == GTK_RESPONSE_YES;
}


GPtrArray *
em_utils_uids_copy (GPtrArray *uids)
{
	GPtrArray *copy;
	int i;
	
	copy = g_ptr_array_new ();
	g_ptr_array_set_size (copy, uids->len);
	
	for (i = 0; i < uids->len; i++)
		copy->pdata[i] = g_strdup (uids->pdata[i]);
	
	return copy;
}

void
em_utils_uids_free (GPtrArray *uids)
{
	int i;
	
	for (i = 0; i < uids->len; i++)
		g_free (uids->pdata[i]);
	
	g_ptr_array_free (uids, TRUE);
}


static void
druid_destroy_cb (gpointer user_data, GObject *deadbeef)
{
	gtk_main_quit ();
}

gboolean
em_utils_configure_account (GtkWidget *parent)
{
	MailConfigDruid *druid;
	
	druid = mail_config_druid_new ();
	g_object_weak_ref ((GObject *) druid, (GWeakNotify) druid_destroy_cb, NULL);
	gtk_widget_show ((GtkWidget *) druid);
	gtk_grab_add ((GtkWidget *) druid);
	gtk_main ();
	
	return mail_config_is_configured ();
}

gboolean
em_utils_check_user_can_send_mail (GtkWidget *parent)
{
	EAccount *account;
	
	if (!mail_config_is_configured ()) {
		if (!em_utils_configure_account (parent))
			return FALSE;
	}
	
	if (!(account = mail_config_get_default_account ()))
		return FALSE;
	
	/* Check for a transport */
	if (!account->transport->url)
		return FALSE;
	
	return TRUE;
}


/* Editing Filters/vFolders... */

static GtkWidget *filter_editor = NULL;

static void
filter_editor_response (GtkWidget *dialog, int button, gpointer user_data)
{
	extern char *evolution_dir;
	FilterContext *fc;
	
	if (button == GTK_RESPONSE_ACCEPT) {
		char *user;
		
		fc = g_object_get_data ((GObject *) dialog, "context");
		user = g_strdup_printf ("%s/filters.xml", evolution_dir);
		rule_context_save ((RuleContext *) fc, user);
		g_free (user);
	}
	
	gtk_widget_destroy (dialog);
	
	filter_editor = NULL;
}

static const char *filter_source_names[] = {
	"incoming",
	"outgoing",
	NULL,
};

void
em_utils_edit_filters (GtkWidget *parent)
{
	extern char *evolution_dir;
	char *user, *system;
	FilterContext *fc;
	
	if (filter_editor) {
		gdk_window_raise (GTK_WIDGET (filter_editor)->window);
		return;
	}
	
	fc = filter_context_new ();
	user = g_strdup_printf ("%s/filters.xml", evolution_dir);
	system = EVOLUTION_PRIVDATADIR "/filtertypes.xml";
	rule_context_load ((RuleContext *) fc, system, user);
	g_free (user);
	
	if (((RuleContext *) fc)->error) {
		e_notice (parent, GTK_MESSAGE_ERROR,
			  _("Error loading filter information:\n%s"),
			  ((RuleContext *) fc)->error);
		return;
	}
	
	filter_editor = (GtkWidget *) filter_editor_new (fc, filter_source_names);
	if (parent != NULL)
		e_dialog_set_transient_for ((GtkWindow *) filter_editor, parent);
	
	gtk_window_set_title (GTK_WINDOW (filter_editor), _("Filters"));
	g_object_set_data_full ((GObject *) filter_editor, "context", fc, (GtkDestroyNotify) g_object_unref);
	g_signal_connect (filter_editor, "response", G_CALLBACK (filter_editor_response), NULL);
	gtk_widget_show (GTK_WIDGET (filter_editor));
}


/* Composing messages... */

static EMsgComposer *
create_new_composer (GtkWidget *parent)
{
	EMsgComposer *composer;
	
	composer = e_msg_composer_new ();
	
	if (parent != NULL)
		e_dialog_set_transient_for ((GtkWindow *) composer, parent);
	
	em_composer_utils_setup_default_callbacks (composer);
	
	return composer;
}

void
em_utils_compose_new_message (GtkWidget *parent)
{
	GtkWidget *composer;
	
	composer = (GtkWidget *) create_new_composer (parent);
	
	gtk_widget_show (composer);
}

void
em_utils_compose_new_message_with_mailto (GtkWidget *parent, const char *url)
{
	EMsgComposer *composer;
	
	if (url != NULL)
		composer = e_msg_composer_new_from_url (url);
	else
		composer = e_msg_composer_new ();
	
	if (parent != NULL)
		e_dialog_set_transient_for ((GtkWindow *) composer, parent);
	
	em_composer_utils_setup_default_callbacks (composer);
	
	gtk_widget_show ((GtkWidget *) composer);
}

void
em_utils_post_to_url (GtkWidget *parent, const char *url)
{
	EMsgComposer *composer;
	
	composer = e_msg_composer_new_post ();
	
	if (parent != NULL)
		e_dialog_set_transient_for ((GtkWindow *) composer, parent);
	
	if (url != NULL)
		e_msg_composer_hdrs_set_post_to ((EMsgComposerHdrs *) ((EMsgComposer *) composer)->hdrs, url);
	
	em_composer_utils_setup_default_callbacks (composer);
	
	e_msg_composer_unset_changed (composer);
	e_msg_composer_drop_editor_undo (composer);
	
	gtk_widget_show ((GtkWidget *) composer);
}


/* Editing messages... */

static void
edit_message (GtkWidget *parent, CamelMimeMessage *message, CamelFolder *drafts, const char *uid)
{
	EMsgComposer *composer;
	
	composer = e_msg_composer_new_with_message (message);
	em_composer_utils_setup_callbacks (composer, NULL, NULL, 0, 0, drafts, uid);
	e_msg_composer_unset_changed (composer);
	e_msg_composer_drop_editor_undo (composer);
	
	gtk_widget_show (GTK_WIDGET (composer));
}

void
em_utils_edit_message (GtkWidget *parent, CamelMimeMessage *message)
{
	g_return_if_fail (CAMEL_IS_MIME_MESSAGE (message));
	
	edit_message (parent, message, NULL, NULL);
}

static void
edit_messages (CamelFolder *folder, GPtrArray *uids, GPtrArray *msgs, void *user_data)
{
	int i;
	
	if (msgs == NULL)
		return;
	
	for (i = 0; i < msgs->len; i++) {
		camel_medium_remove_header (CAMEL_MEDIUM (msgs->pdata[i]), "X-Mailer");
		
		edit_message ((GtkWidget *) user_data, msgs->pdata[i], folder, uids->pdata[i]);
	}
}

void
em_utils_edit_messages (GtkWidget *parent, CamelFolder *folder, GPtrArray *uids)
{
	g_return_if_fail (CAMEL_IS_FOLDER (folder));
	g_return_if_fail (uids != NULL);
	
	mail_get_messages (folder, uids, edit_messages, parent);
}

/* Forwarding messages... */

static void
forward_attached (CamelFolder *folder, GPtrArray *messages, CamelMimePart *part, char *subject, void *user_data)
{
	EMsgComposer *composer;
	
	if (part == NULL)
		return;
	
	composer = create_new_composer ((GtkWidget *) user_data);
	e_msg_composer_set_headers (composer, NULL, NULL, NULL, NULL, subject);
	e_msg_composer_attach (composer, part);
	
	e_msg_composer_unset_changed (composer);
	e_msg_composer_drop_editor_undo (composer);
	
	gtk_widget_show (GTK_WIDGET (composer));
}

void
em_utils_forward_attached (GtkWidget *parent, CamelFolder *folder, GPtrArray *uids)
{
	g_return_if_fail (CAMEL_IS_FOLDER (folder));
	g_return_if_fail (uids != NULL);
	
	mail_build_attachment (folder, uids, forward_attached, parent);
}


static void
forward_non_attached (GtkWidget *parent, CamelFolder *folder, GPtrArray *uids, GPtrArray *messages, int style)
{
	CamelMimeMessage *message;
	CamelDataWrapper *wrapper;
	EMsgComposer *composer;
	char *subject, *text;
	int i;
	
	if (messages->len == 0)
		return;
	
	for (i = 0; i < messages->len; i++) {
		message = messages->pdata[i];
		subject = mail_tool_generate_forward_subject (message);
		text = mail_tool_forward_message (message, style == MAIL_CONFIG_FORWARD_QUOTED);
		
		if (text) {
			composer = create_new_composer (parent);
			e_msg_composer_set_headers (composer, NULL, NULL, NULL, NULL, subject);
			e_msg_composer_set_body_text (composer, text);
			
			wrapper = camel_medium_get_content_object (CAMEL_MEDIUM (message));
			if (CAMEL_IS_MULTIPART (wrapper))
				e_msg_composer_add_message_attachments (composer, message, FALSE);
			
			e_msg_composer_unset_changed (composer);
			e_msg_composer_drop_editor_undo (composer);
			
			gtk_widget_show (GTK_WIDGET (composer));
			
			g_free (text);
		}
		
		g_free (subject);
	}
}

static void
forward_inline (CamelFolder *folder, GPtrArray *uids, GPtrArray *messages, void *user_data)
{
	forward_non_attached ((GtkWidget *) user_data, folder, uids, messages, MAIL_CONFIG_FORWARD_INLINE);
}

void
em_utils_forward_inline (GtkWidget *parent, CamelFolder *folder, GPtrArray *uids)
{
	g_return_if_fail (CAMEL_IS_FOLDER (folder));
	g_return_if_fail (uids != NULL);
	
	mail_get_messages (folder, uids, forward_inline, parent);
}

static void
forward_quoted (CamelFolder *folder, GPtrArray *uids, GPtrArray *messages, void *user_data)
{
	forward_non_attached ((GtkWidget *) user_data, folder, uids, messages, MAIL_CONFIG_FORWARD_QUOTED);
}

void
em_utils_forward_quoted (GtkWidget *parent, CamelFolder *folder, GPtrArray *uids)
{
	g_return_if_fail (CAMEL_IS_FOLDER (folder));
	g_return_if_fail (uids != NULL);
	
	mail_get_messages (folder, uids, forward_quoted, parent);
}


/* Redirecting messages... */

static EMsgComposer *
redirect_get_composer (GtkWidget *parent, CamelMimeMessage *message)
{
	EMsgComposer *composer;
	EAccount *account;
	
	/* QMail will refuse to send a message if it finds one of
	   it's Delivered-To headers in the message, so remove all
	   Delivered-To headers. Fixes bug #23635. */
	while (camel_medium_get_header (CAMEL_MEDIUM (message), "Delivered-To"))
		camel_medium_remove_header (CAMEL_MEDIUM (message), "Delivered-To");
	
	account = guess_account (message);
	
	composer = e_msg_composer_new_redirect (message, account ? account->name : NULL);
	
	if (parent != NULL)
		e_dialog_set_transient_for ((GtkWindow *) composer, parent);
	
	em_composer_utils_setup_default_callbacks (composer);
	
	return composer;
}

void
em_utils_redirect_message (GtkWidget *parent, CamelMimeMessage *message)
{
	EMsgComposer *composer;
	CamelDataWrapper *wrapper;
	
	g_return_if_fail (CAMEL_IS_MIME_MESSAGE (message));
	
	composer = redirect_get_composer (parent, message);
	
	wrapper = camel_medium_get_content_object (CAMEL_MEDIUM (message));
	if (CAMEL_IS_MULTIPART (wrapper))
		e_msg_composer_add_message_attachments (composer, message, FALSE);
	
	gtk_widget_show (GTK_WIDGET (composer));
	e_msg_composer_unset_changed (composer);
	e_msg_composer_drop_editor_undo (composer);
}

static void
redirect_msg (CamelFolder *folder, const char *uid, CamelMimeMessage *message, void *user_data)
{
	if (message == NULL)
		return;
	
	em_utils_redirect_message ((GtkWidget *) user_data, message);
}

void
em_utils_redirect_message_by_uid (GtkWidget *parent, CamelFolder *folder, const char *uid)
{
	g_return_if_fail (CAMEL_IS_FOLDER (folder));
	g_return_if_fail (uid != NULL);
	
	mail_get_message (folder, uid, redirect_msg, parent, mail_thread_new);
}


/* Replying to messages... */

static GHashTable *
generate_account_hash (void)
{
	GHashTable *account_hash;
	EAccount *account, *def;
	EAccountList *accounts;
	EIterator *iter;
	
	accounts = mail_config_get_accounts ();
	account_hash = g_hash_table_new (camel_strcase_hash, camel_strcase_equal);
	
	/* add the default account to the hash first */
	if ((def = mail_config_get_default_account ())) {
		if (def->id->address)
			g_hash_table_insert (account_hash, (char *) def->id->address, (void *) def);
	}
	
	iter = e_list_get_iterator ((EList *) accounts);
	while (e_iterator_is_valid (iter)) {
		account = (EAccount *) e_iterator_get (iter);
		
		if (account->id->address) {
			EAccount *acnt;
			
			/* Accounts with identical email addresses that are enabled
			 * take precedence over the accounts that aren't. If all
			 * accounts with matching email addresses are disabled, then
			 * the first one in the list takes precedence. The default
			 * account always takes precedence no matter what.
			 */
			acnt = g_hash_table_lookup (account_hash, account->id->address);
			if (acnt && acnt != def && !acnt->enabled && account->enabled) {
				g_hash_table_remove (account_hash, acnt->id->address);
				acnt = NULL;
			}
			
			if (!acnt)
				g_hash_table_insert (account_hash, (char *) account->id->address, (void *) account);
		}
		
		e_iterator_next (iter);
	}
	
	g_object_unref (iter);
	
	return account_hash;
}

static EDestination **
em_utils_camel_address_to_destination (CamelInternetAddress *iaddr)
{
	EDestination *dest, **destv;
	int n, i, j;
	
	if (iaddr == NULL)
		return NULL;
	
	if ((n = camel_address_length ((CamelAddress *) iaddr)) == 0)
		return NULL;
	
	destv = g_malloc (sizeof (EDestination *) * (n + 1));
	for (i = 0, j = 0; i < n; i++) {
		const char *name, *addr;
		
		if (camel_internet_address_get (iaddr, i, &name, &addr)) {
			dest = e_destination_new ();
			e_destination_set_name (dest, name);
			e_destination_set_email (dest, addr);
			
			destv[j++] = dest;
		}
	}
	
	if (j == 0) {
		g_free (destv);
		return NULL;
	}
	
	destv[j] = NULL;
	
	return destv;
}

static EMsgComposer *
reply_get_composer (GtkWidget *parent, CamelMimeMessage *message, EAccount *account,
		    CamelInternetAddress *to, CamelInternetAddress *cc)
{
	const char *message_id, *references;
	EDestination **tov, **ccv;
	EMsgComposer *composer;
	char *subject;
	
	g_return_val_if_fail (CAMEL_IS_MIME_MESSAGE (message), NULL);
	g_return_val_if_fail (to == NULL || CAMEL_IS_INTERNET_ADDRESS (to), NULL);
	g_return_val_if_fail (cc == NULL || CAMEL_IS_INTERNET_ADDRESS (cc), NULL);
	
	composer = e_msg_composer_new ();
	
	if (parent != NULL)
		e_dialog_set_transient_for ((GtkWindow *) composer, parent);
	
	/* construct the tov/ccv */
	tov = em_utils_camel_address_to_destination (to);
	ccv = em_utils_camel_address_to_destination (cc);
	
	/* Set the subject of the new message. */
	if ((subject = (char *) camel_mime_message_get_subject (message))) {
		if (strncasecmp (subject, "Re: ", 4) != 0)
			subject = g_strdup_printf ("Re: %s", subject);
		else
			subject = g_strdup (subject);
	} else {
		subject = g_strdup ("");
	}
	
	e_msg_composer_set_headers (composer, account ? account->name : NULL, tov, ccv, NULL, subject);
	
	g_free (subject);
	
	/* Add In-Reply-To and References. */
	message_id = camel_medium_get_header (CAMEL_MEDIUM (message), "Message-Id");
	references = camel_medium_get_header (CAMEL_MEDIUM (message), "References");
	if (message_id) {
		char *reply_refs;
		
		e_msg_composer_add_header (composer, "In-Reply-To", message_id);
		
		if (references)
			reply_refs = g_strdup_printf ("%s %s", references, message_id);
		else
			reply_refs = g_strdup (message_id);
		
		e_msg_composer_add_header (composer, "References", reply_refs);
		g_free (reply_refs);
	} else if (references) {
		e_msg_composer_add_header (composer, "References", references);
	}
	
	e_msg_composer_drop_editor_undo (composer);
	
	return composer;
}

static EAccount *
guess_account (CamelMimeMessage *message)
{
	const CamelInternetAddress *to, *cc;
	GHashTable *account_hash;
	EAccount *account = NULL;
	const char *addr;
	int i;
	
	to = camel_mime_message_get_recipients (message, CAMEL_RECIPIENT_TYPE_TO);
	cc = camel_mime_message_get_recipients (message, CAMEL_RECIPIENT_TYPE_CC);
	
	if (to == NULL && cc == NULL)
		return NULL;
	
	account_hash = generate_account_hash ();
	
	if (to) {
		for (i = 0; camel_internet_address_get (to, i, NULL, &addr); i++) {
			account = g_hash_table_lookup (account_hash, addr);
			if (account)
				goto found;
		}
	}
	
	if (cc) {
		for (i = 0; camel_internet_address_get (cc, i, NULL, &addr); i++) {
			account = g_hash_table_lookup (account_hash, addr);
			if (account)
				goto found;
		}
	}
	
 found:
	
	g_hash_table_destroy (account_hash);
	
	return account;
}

static void
get_reply_sender (CamelMimeMessage *message, CamelInternetAddress **to)
{
	const CamelInternetAddress *reply_to;
	const char *name, *addr;
	int i;
	
	reply_to = camel_mime_message_get_reply_to (message);
	if (!reply_to)
		reply_to = camel_mime_message_get_from (message);
	
	if (reply_to) {
		*to = camel_internet_address_new ();
		
		for (i = 0; camel_internet_address_get (reply_to, i, &name, &addr); i++)
			camel_internet_address_add (*to, name, addr);
	}
}

static gboolean
get_reply_list (CamelMimeMessage *message, CamelInternetAddress **to)
{
	/* FIXME: implement me */
	return FALSE;
}

static void
concat_unique_addrs (CamelInternetAddress *dest, const CamelInternetAddress *src, GHashTable *rcpt_hash)
{
	const char *name, *addr;
	int i;
	
	for (i = 0; camel_internet_address_get (src, i, &name, &addr); i++) {
		if (!g_hash_table_lookup (rcpt_hash, addr)) {
			camel_internet_address_add (dest, name, addr);
			g_hash_table_insert (rcpt_hash, (char *) addr, GINT_TO_POINTER (1));
		}
	}
}

static void
get_reply_all (CamelMimeMessage *message, CamelInternetAddress **to, CamelInternetAddress **cc)
{
	const CamelInternetAddress *reply_to, *to_addrs, *cc_addrs;
	const char *name, *addr;
	GHashTable *rcpt_hash;
	int i;
	
	rcpt_hash = generate_account_hash ();
	
	reply_to = camel_mime_message_get_reply_to (message);
	if (!reply_to)
		reply_to = camel_mime_message_get_from (message);
	
	to_addrs = camel_mime_message_get_recipients (message, CAMEL_RECIPIENT_TYPE_TO);
	cc_addrs = camel_mime_message_get_recipients (message, CAMEL_RECIPIENT_TYPE_CC);
	
	*to = camel_internet_address_new ();
	*cc = camel_internet_address_new ();
	
	if (reply_to) {
		for (i = 0; camel_internet_address_get (reply_to, i, &name, &addr); i++) {
			/* ignore references to the Reply-To address in the To and Cc lists */
			if (addr && !g_hash_table_lookup (rcpt_hash, addr)) {
				/* In the case that we are doing a Reply-To-All, we do not want
				   to include the user's email address because replying to oneself
				   is kinda silly. */
				
				camel_internet_address_add (*to, name, addr);
				g_hash_table_insert (rcpt_hash, (char *) addr, GINT_TO_POINTER (1));
			}
		}
	}
	
	concat_unique_addrs (*cc, to_addrs, rcpt_hash);
	concat_unique_addrs (*cc, cc_addrs, rcpt_hash);
	
	/* promote the first Cc: address to To: if To: is empty */
	if (camel_address_length ((CamelAddress *) *to) == 0 && camel_address_length ((CamelAddress *) *cc) > 0) {
		camel_internet_address_get (*cc, 0, &name, &addr);
		camel_internet_address_add (*to, name, addr);
		camel_address_remove ((CamelAddress *) *cc, 0);
	}
	
	g_hash_table_destroy (rcpt_hash);
}

static void
composer_set_body (EMsgComposer *composer, CamelMimeMessage *message)
{
	const CamelInternetAddress *sender;
	const char *name, *addr;
	char *text, format[256];
	CamelMimePart *part;
	GConfClient *gconf;
	time_t date;
	
	gconf = mail_config_get_gconf_client ();
	
	switch (gconf_client_get_int (gconf, "/apps/evolution/mail/format/reply_style", NULL)) {
	case MAIL_CONFIG_REPLY_DO_NOT_QUOTE:
		/* do nothing */
		break;
	case MAIL_CONFIG_REPLY_ATTACH:
		/* attach the original message as an attachment */
		part = mail_tool_make_message_attachment (message);
		e_msg_composer_attach (composer, part);
		camel_object_unref (part);
		break;
	case MAIL_CONFIG_REPLY_QUOTED:
	default:
		/* do what any sane user would want when replying... */
		sender = camel_mime_message_get_from (message);
		if (sender != NULL && camel_address_length (CAMEL_ADDRESS (sender)) > 0) {
			camel_internet_address_get (sender, 0, &name, &addr);
		} else {
			name = _("an unknown sender");
		}
		
		date = camel_mime_message_get_date (message, NULL);
		e_utf8_strftime (format, sizeof (format), _("On %a, %Y-%m-%d at %H:%M, %%s wrote:"), localtime (&date));
		text = mail_tool_quote_message (message, format, name && *name ? name : addr);
		if (text) {
			e_msg_composer_set_body_text (composer, text);
			g_free (text);
		}
		break;
	}
	
	e_msg_composer_drop_editor_undo (composer);
}

void
em_utils_reply_to_message (GtkWidget *parent, CamelMimeMessage *message, int mode)
{
	CamelInternetAddress *to = NULL, *cc = NULL;
	EMsgComposer *composer;
	EAccount *account;
	
	account = guess_account (message);
	
	switch (mode) {
	case REPLY_MODE_SENDER:
		get_reply_sender (message, &to);
		break;
	case REPLY_MODE_LIST:
		if (get_reply_list (message, &to))
			break;
	case REPLY_MODE_ALL:
		get_reply_all (message, &to, &cc);
		break;
	}
	
	composer = reply_get_composer (parent, message, account, to, cc);
	e_msg_composer_add_message_attachments (composer, message, TRUE);
	
	if (to != NULL)
		camel_object_unref (to);
	
	if (cc != NULL)
		camel_object_unref (cc);
	
	composer_set_body (composer, message);
	
	em_composer_utils_setup_default_callbacks (composer);
	
	gtk_widget_show (GTK_WIDGET (composer));
	e_msg_composer_unset_changed (composer);
}

struct rtm_t {
	GtkWidget *parent;
	int mode;
};

static void
reply_to_message (CamelFolder *folder, const char *uid, CamelMimeMessage *message, void *user_data)
{
	CamelInternetAddress *to = NULL, *cc = NULL;
	struct rtm_t *rtm = user_data;
	EMsgComposer *composer;
	EAccount *account;
	guint32 flags;
	
	account = guess_account (message);
	flags = CAMEL_MESSAGE_ANSWERED | CAMEL_MESSAGE_SEEN;
	
	switch (rtm->mode) {
	case REPLY_MODE_SENDER:
		get_reply_sender (message, &to);
		break;
	case REPLY_MODE_LIST:
		flags |= CAMEL_MESSAGE_ANSWERED_ALL;
		if (get_reply_list (message, &to))
			break;
	case REPLY_MODE_ALL:
		flags |= CAMEL_MESSAGE_ANSWERED_ALL;
		get_reply_all (message, &to, &cc);
		break;
	}
	
	composer = reply_get_composer (rtm->parent, message, account, to, cc);
	e_msg_composer_add_message_attachments (composer, message, TRUE);
	
	if (to != NULL)
		camel_object_unref (to);
	
	if (cc != NULL)
		camel_object_unref (cc);
	
	composer_set_body (composer, message);
	
	em_composer_utils_setup_callbacks (composer, folder, uid, flags, flags, NULL, NULL);
	
	gtk_widget_show (GTK_WIDGET (composer));
	e_msg_composer_unset_changed (composer);
}

void
em_utils_reply_to_message_by_uid (GtkWidget *parent, CamelFolder *folder, const char *uid, int mode)
{
	struct rtm_t *rtm;
	
	g_return_if_fail (CAMEL_IS_FOLDER (folder));
	g_return_if_fail (uid != NULL);
	
	rtm = g_malloc (sizeof (struct rtm_t));
	rtm->parent = parent;
	rtm->mode = mode;
	
	mail_get_message (folder, uid, reply_to_message, rtm, mail_thread_new);
}


/* Posting replies... */

static void
post_reply_to_message (CamelFolder *folder, const char *uid, CamelMimeMessage *message, void *user_data)
{
	/* FIXME: would be nice if this shared more code with reply_get_composer() */
	const char *message_id, *references;
	CamelInternetAddress *to = NULL;
	GtkWidget *parent = user_data;
	EDestination **tov = NULL;
	EMsgComposer *composer;
	char *subject, *url;
	EAccount *account;
	guint32 flags;
	
	account = guess_account (message);
	flags = CAMEL_MESSAGE_ANSWERED | CAMEL_MESSAGE_SEEN;
	
	get_reply_sender (message, &to);
	
	composer = e_msg_composer_new_post ();
	
	if (parent != NULL)
		e_dialog_set_transient_for ((GtkWindow *) composer, parent);
	
	/* construct the tov/ccv */
	tov = em_utils_camel_address_to_destination (to);
	
	/* Set the subject of the new message. */
	if ((subject = (char *) camel_mime_message_get_subject (message))) {
		if (strncasecmp (subject, "Re: ", 4) != 0)
			subject = g_strdup_printf ("Re: %s", subject);
		else
			subject = g_strdup (subject);
	} else {
		subject = g_strdup ("");
	}
	
	e_msg_composer_set_headers (composer, account ? account->name : NULL, tov, NULL, NULL, subject);
	
	g_free (subject);
	
	url = mail_tools_folder_to_url (folder);
	e_msg_composer_hdrs_set_post_to ((EMsgComposerHdrs *) composer->hdrs, url);
	g_free (url);
	
	/* Add In-Reply-To and References. */
	message_id = camel_medium_get_header (CAMEL_MEDIUM (message), "Message-Id");
	references = camel_medium_get_header (CAMEL_MEDIUM (message), "References");
	if (message_id) {
		char *reply_refs;
		
		e_msg_composer_add_header (composer, "In-Reply-To", message_id);
		
		if (references)
			reply_refs = g_strdup_printf ("%s %s", references, message_id);
		else
			reply_refs = g_strdup (message_id);
		
		e_msg_composer_add_header (composer, "References", reply_refs);
		g_free (reply_refs);
	} else if (references) {
		e_msg_composer_add_header (composer, "References", references);
	}
	
	e_msg_composer_drop_editor_undo (composer);
	
	e_msg_composer_add_message_attachments (composer, message, TRUE);
	
	if (to != NULL)
		camel_object_unref (to);
	
	composer_set_body (composer, message);
	
	em_composer_utils_setup_callbacks (composer, folder, uid, flags, flags, NULL, NULL);
	
	gtk_widget_show (GTK_WIDGET (composer));	
	e_msg_composer_unset_changed (composer);
}

void
em_utils_post_reply_to_message_by_uid (GtkWidget *parent, CamelFolder *folder, const char *uid)
{
	g_return_if_fail (CAMEL_IS_FOLDER (folder));
	g_return_if_fail (uid != NULL);
	
	mail_get_message (folder, uid, post_reply_to_message, parent, mail_thread_new);
}


/* Saving messages... */

typedef void (* OKCallback) (GtkFileSelection *filesel, gpointer user_data);

static void
filesel_ok_cb (GtkWidget *ok_button, gpointer user_data)
{
	GtkFileSelection *filesel = user_data;
	OKCallback ok_cb;
	
	user_data = g_object_get_data ((GObject *) filesel, "user_data");
	ok_cb = g_object_get_data ((GObject *) filesel, "ok_cb");
	
	ok_cb (filesel, user_data);
}

static void
em_utils_filesel_prompt (GtkWidget *parent, const char *title, const char *default_path,
			 OKCallback ok_cb, GWeakNotify destroy_cb, gpointer user_data)
{
	GtkFileSelection *filesel;
	
	filesel = (GtkFileSelection *) gtk_file_selection_new (title);
	gtk_file_selection_set_filename (filesel, default_path);
	
	if (parent != NULL)
		e_dialog_set_transient_for ((GtkWindow *) filesel, parent);
	
	g_object_set_data ((GObject *) filesel, "ok_cb", ok_cb);
	g_object_set_data ((GObject *) filesel, "user_data", user_data);
	
	g_object_weak_ref ((GObject *) filesel, destroy_cb, user_data);
	
	g_signal_connect (filesel->ok_button, "clicked", G_CALLBACK (filesel_ok_cb), filesel);
	g_signal_connect_swapped (filesel->cancel_button, "clicked",
				  G_CALLBACK (gtk_widget_destroy), filesel);
	
	gtk_widget_show (GTK_WIDGET (filesel));
}


static gboolean
can_save (GtkWindow *parent, const char *path)
{
	struct stat st;
	
	/* make sure we can actually save to it... */
	if (stat (path, &st) != -1 && !S_ISREG (st.st_mode))
		return FALSE;
	
	if (access (path, F_OK) == 0) {
		if (access (path, W_OK) != 0) {
			e_notice (parent, GTK_MESSAGE_ERROR,
				 _("Cannot save to `%s'\n %s"), path, g_strerror (errno));
			return FALSE;
		}
		
		return e_question (parent, GTK_RESPONSE_NO, NULL,
				   _("`%s' already exists.\nOverwrite it?"), path);
	}
	
	return TRUE;
}

static void
save_message_ok (GtkFileSelection *filesel, gpointer user_data)
{
	CamelMimeMessage *message = user_data;
	CamelStream *stream;
	const char *path;
	
	path = gtk_file_selection_get_filename (filesel);
	if (path[0] == '\0')
		return;
	
	if (!can_save (GTK_WINDOW (filesel), path))
		return;
	
	stream = camel_stream_fs_new_with_name (path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	camel_data_wrapper_write_to_stream ((CamelDataWrapper *) message, stream);
	camel_stream_flush (stream);
	camel_object_unref (stream);
	
	gtk_widget_destroy (GTK_WIDGET (filesel));
}

void
em_utils_save_message (GtkWidget *parent, CamelMimeMessage *message)
{
	char *path;
	
	camel_object_ref (message);
	path = g_strdup_printf ("%s/", g_get_home_dir ());
	em_utils_filesel_prompt (parent, _("Save Message..."), path, save_message_ok,
				 (GWeakNotify) camel_object_unref, message);
	g_free (path);
}


struct _save_messages_data {
	CamelFolder *folder;
	GPtrArray *uids;
};

static void
save_messages_ok (GtkFileSelection *filesel, gpointer user_data)
{
	struct _save_messages_data *data = user_data;
	const char *path;
	
	path = gtk_file_selection_get_filename (filesel);
	if (path[0] == '\0')
		return;
	
	if (can_save (GTK_WINDOW (filesel), path)) {
		mail_save_messages (data->folder, data->uids, path, NULL, NULL);
		data->uids = NULL;
		
		gtk_widget_destroy (GTK_WIDGET (filesel));
	}
}

static void
save_messages_destroy (gpointer user_data, GObject *deadbeef)
{
	struct _save_messages_data *data = user_data;
	
	camel_object_unref (data->folder);
	if (data->uids)
		em_utils_uids_free (data->uids);
	g_free (data);
}

void
em_utils_save_messages (GtkWidget *parent, CamelFolder *folder, GPtrArray *uids)
{
	struct _save_messages_data *data;
	char *path;
	
	g_return_if_fail (CAMEL_IS_FOLDER (folder));
	g_return_if_fail (uids != NULL);
	
	camel_object_ref (folder);
	
	data = g_malloc (sizeof (struct _save_messages_data));
	data->folder = folder;
	data->uids = uids;
	
	path = g_strdup_printf ("%s/", g_get_home_dir ());
	em_utils_filesel_prompt (parent, _("Save Message..."), path, save_messages_ok,
				 (GWeakNotify) save_messages_destroy, data);
	g_free (path);
}


/* Flag-for-Followup... */

/* tag-editor callback data */
struct ted_t {
	MessageTagEditor *editor;
	CamelFolder *folder;
	GPtrArray *uids;
};

static void
ted_free (struct ted_t *ted)
{
	camel_object_unref (ted->folder);
	em_utils_uids_free (ted->uids);
	g_free (ted);
}

static void
tag_editor_response (GtkWidget *dialog, int button, struct ted_t *ted)
{
	CamelFolder *folder;
	CamelTag *tags, *t;
	GPtrArray *uids;
	int i;
	
	if (button == GTK_RESPONSE_OK && (tags = message_tag_editor_get_tag_list (ted->editor))) {
		folder = ted->folder;
		uids = ted->uids;
		
		camel_folder_freeze (folder);
		for (i = 0; i < uids->len; i++) {
			for (t = tags; t; t = t->next)
				camel_folder_set_message_user_tag (folder, uids->pdata[i], t->name, t->value);
		}
		
		camel_folder_thaw (folder);
		camel_tag_list_free (&tags);
	}
	
	gtk_widget_destroy (dialog);
}

void
em_utils_flag_for_followup (GtkWidget *parent, CamelFolder *folder, GPtrArray *uids)
{
	GtkWidget *editor;
	struct ted_t *ted;
	int i;
	
	g_return_if_fail (CAMEL_IS_FOLDER (folder));
	g_return_if_fail (uids != NULL);
	
	editor = (GtkWidget *) message_tag_followup_new ();
	
	if (parent != NULL)
		e_dialog_set_transient_for ((GtkWindow *) editor, parent);
	
	camel_object_ref (folder);
	
	ted = g_new (struct ted_t, 1);
	ted->editor = MESSAGE_TAG_EDITOR (editor);
	ted->folder = folder;
	ted->uids = uids;
	
	for (i = 0; i < uids->len; i++) {
		CamelMessageInfo *info;
		
		info = camel_folder_get_message_info (folder, uids->pdata[i]);
		message_tag_followup_append_message (MESSAGE_TAG_FOLLOWUP (editor),
						     camel_message_info_from (info),
						     camel_message_info_subject (info));
	}
	
	/* special-case... */
	if (uids->len == 1) {
		CamelMessageInfo *info;
		
		info = camel_folder_get_message_info (folder, uids->pdata[0]);
		if (info) {
			if (info->user_tags)
				message_tag_editor_set_tag_list (MESSAGE_TAG_EDITOR (editor), info->user_tags);
			camel_folder_free_message_info (folder, info);
		}
	}
	
	g_signal_connect (editor, "response", G_CALLBACK (tag_editor_response), ted);
	g_object_weak_ref ((GObject *) editor, (GWeakNotify) ted_free, ted);
	
	gtk_widget_show (editor);
}

void
em_utils_flag_for_followup_clear (GtkWidget *parent, CamelFolder *folder, GPtrArray *uids)
{
	int i;
	
	g_return_if_fail (CAMEL_IS_FOLDER (folder));
	g_return_if_fail (uids != NULL);
	
	camel_folder_freeze (folder);
	for (i = 0; i < uids->len; i++) {
		camel_folder_set_message_user_tag (folder, uids->pdata[i], "follow-up", "");
		camel_folder_set_message_user_tag (folder, uids->pdata[i], "due-by", "");
		camel_folder_set_message_user_tag (folder, uids->pdata[i], "completed-on", "");
	}
	camel_folder_thaw (folder);
	
	em_utils_uids_free (uids);
}

void
em_utils_flag_for_followup_completed (GtkWidget *parent, CamelFolder *folder, GPtrArray *uids)
{
	char *now;
	int i;
	
	g_return_if_fail (CAMEL_IS_FOLDER (folder));
	g_return_if_fail (uids != NULL);
	
	now = header_format_date (time (NULL), 0);
	
	camel_folder_freeze (folder);
	for (i = 0; i < uids->len; i++) {
		const char *tag;
		
		tag = camel_folder_get_message_user_tag (folder, uids->pdata[i], "follow-up");
		if (tag == NULL || *tag == '\0')
			continue;
		
		camel_folder_set_message_user_tag (folder, uids->pdata[i], "completed-on", now);
	}
	camel_folder_thaw (folder);
	
	g_free (now);
	
	em_utils_uids_free (uids);
}
