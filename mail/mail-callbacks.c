/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* mail-ops.c: callbacks for the mail toolbar/menus */

/* 
 * Authors: 
 *  Dan Winship <danw@ximian.com>
 *  Peter Williams <peterw@ximian.com>
 *  Jeffrey Stedfast <fejj@ximian.com>
 *
 * Copyright 2000 Ximian, Inc. (www.ximian.com)
 *
 * This program is free software; you can redistribute it and/or 
 * modify it under the terms of the GNU General Public License as 
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <time.h>
#include <libgnome/gnome-paper.h>
#include <libgnomeui/gnome-dialog.h>
#include <libgnomeui/gnome-dialog-util.h>
#include <libgnomeui/gnome-stock.h>
#include <libgnome/gnome-paper.h>
#include <libgnomeprint/gnome-print-master.h>
#include <libgnomeprint/gnome-print-master-preview.h>
#include <bonobo/bonobo-widget.h>
#include <bonobo/bonobo-socket.h>
#include <gal/e-table/e-table.h>
#include <gal/widgets/e-gui-utils.h>
#include <filter/filter-editor.h>
#include "mail.h"
#include "message-browser.h"
#include "mail-callbacks.h"
#include "mail-config.h"
#include "mail-accounts.h"
#include "mail-config-druid.h"
#include "mail-mt.h"
#include "mail-tools.h"
#include "mail-ops.h"
#include "mail-local.h"
#include "mail-search.h"
#include "mail-send-recv.h"
#include "mail-vfolder.h"
#include "mail-folder-cache.h"
#include "folder-browser.h"
#include "subscribe-dialog.h"
#include "e-messagebox.h"

/* FIXME: is there another way to do this? */
#include "Evolution.h"
#include "evolution-storage.h"

#include "evolution-shell-client.h"

#ifndef HAVE_MKSTEMP
#include <fcntl.h>
#include <sys/stat.h>
#endif

struct post_send_data {
	CamelFolder *folder;
	gchar *uid;
	guint32 flags;
};

static void
druid_destroyed (void)
{
	gtk_main_quit ();
}

static gboolean
configure_mail (FolderBrowser *fb)
{
	MailConfigDruid *druid;
	
	if (fb) {
		GtkWidget *dialog;
		
		dialog = gnome_message_box_new (
			_("You have not configured the mail client.\n"
			  "You need to do this before you can send,\n"
			  "receive or compose mail.\n"
			  "Would you like to configure it now?"),
			GNOME_MESSAGE_BOX_QUESTION,
			GNOME_STOCK_BUTTON_YES,
			GNOME_STOCK_BUTTON_NO, NULL);
		
		/*
		 * Focus YES
		 */
		gnome_dialog_set_default (GNOME_DIALOG (dialog), 0);
		gtk_widget_grab_focus (GTK_WIDGET (GNOME_DIALOG (dialog)->buttons->data));
		
		gnome_dialog_set_parent (GNOME_DIALOG (dialog),
					 GTK_WINDOW (gtk_widget_get_ancestor (GTK_WIDGET (fb), GTK_TYPE_WINDOW)));
		
		switch (gnome_dialog_run_and_close (GNOME_DIALOG (dialog))) {
		case 0:
			druid = mail_config_druid_new (fb->shell);
			gtk_signal_connect (GTK_OBJECT (druid), "destroy",
					    GTK_SIGNAL_FUNC (druid_destroyed), NULL);
			gtk_widget_show (GTK_WIDGET (druid));
			gtk_grab_add (GTK_WIDGET (druid));
			gtk_main ();
			break;
		case 1:
		default:
			break;
		}
	}
	
	return mail_config_is_configured ();
}

static gboolean
check_send_configuration (FolderBrowser *fb)
{
	const MailConfigAccount *account;
	
	/* Check general */
	if (!mail_config_is_configured ()) {
		if (!configure_mail (fb))
			return FALSE;
	}
	
	/* Get the default account */
	account = mail_config_get_default_account ();
	
	/* Check for an identity */
	if (!account) {
		GtkWidget *message;
		
		message = gnome_warning_dialog_parented (_("You need to configure an identity\n"
							   "before you can compose mail."),
							 GTK_WINDOW (gtk_widget_get_ancestor (GTK_WIDGET (fb),
											      GTK_TYPE_WINDOW)));
		gnome_dialog_run_and_close (GNOME_DIALOG (message));
		return FALSE;
	}
	
	/* Check for a transport */
	if (!account->transport || !account->transport->url) {
		GtkWidget *message;
		
		message = gnome_warning_dialog_parented (_("You need to configure a mail transport\n"
							   "before you can compose mail."),
							 GTK_WINDOW (gtk_widget_get_ancestor (GTK_WIDGET (fb),
											      GTK_TYPE_WINDOW)));
		gnome_dialog_run_and_close (GNOME_DIALOG (message));
		return FALSE;
	}
	
	return TRUE;
}

#if 0
/* FIXME: is this still required when we send & receive email ?  I am not so sure ... */
static void
main_select_first_unread (CamelObject *object, gpointer event_data, gpointer data)
{
	FolderBrowser *fb = FOLDER_BROWSER (data);
	/*ETable *table = E_TABLE_SCROLLED (fb->message_list->etable)->table;*/
	
	message_list_select (fb->message_list, 0, MESSAGE_LIST_SELECT_NEXT,
  			     0, CAMEL_MESSAGE_SEEN);
}

static void
select_first_unread (CamelObject *object, gpointer event_data, gpointer data)
{
	mail_op_forward_event (main_select_first_unread, object, event_data, data);
}
#endif

void
send_receive_mail (GtkWidget *widget, gpointer user_data)
{
	const MailConfigAccount *account;
	
	/* receive first then send, this is a temp fix for POP-before-SMTP */
	if (!mail_config_is_configured ()) {
		if (!configure_mail (FOLDER_BROWSER (user_data)))
			return;
	}
	
	account = mail_config_get_default_account ();
	if (!account || !account->transport) {
		GtkWidget *win = gtk_widget_get_ancestor (GTK_WIDGET (user_data), GTK_TYPE_WINDOW);
		gnome_error_dialog_parented (_("You have not set a mail transport method"), GTK_WINDOW (win));
		return;
	}
	
	mail_send_receive ();
}

static void
msgbox_destroyed (GtkWidget *widget, gpointer data)
{
	gboolean *show_again = data;
	GtkWidget *checkbox;
	
	checkbox = e_message_box_get_checkbox (E_MESSAGE_BOX (widget));
	*show_again = !gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (checkbox));
}

static gboolean
ask_confirm_for_empty_subject (EMsgComposer *composer)
{
	/* FIXME: EMessageBox should really handle this stuff
           automagically. What Miguel thinks would be nice is to pass
           in a message-id which could be used as a key in the config
           file and the value would be an int. -1 for always show or
           the button pressed otherwise. This probably means we'd have
           to write e_messagebox_run () */
	gboolean show_again = TRUE;
	GtkWidget *mbox;
	int button;
	
	if (!mail_config_get_prompt_empty_subject ())
		return TRUE;
	
	mbox = e_message_box_new (_("This message has no subject.\nReally send?"),
				  E_MESSAGE_BOX_QUESTION,
				  GNOME_STOCK_BUTTON_YES,
				  GNOME_STOCK_BUTTON_NO,
				  NULL);
	
	gtk_signal_connect (GTK_OBJECT (mbox), "destroy",
			    msgbox_destroyed, &show_again);
	
	button = gnome_dialog_run_and_close (GNOME_DIALOG (mbox));
	
	mail_config_set_prompt_empty_subject (show_again);
	
	if (button == 0)
		return TRUE;
	else
		return FALSE;
}

static gboolean
ask_confirm_for_only_bcc (EMsgComposer *composer)
{
	/* FIXME: EMessageBox should really handle this stuff
           automagically. What Miguel thinks would be nice is to pass
           in a message-id which could be used as a key in the config
           file and the value would be an int. -1 for always show or
           the button pressed otherwise. This probably means we'd have
           to write e_messagebox_run () */
	gboolean show_again = TRUE;
	GtkWidget *mbox;
	int button;
	
	if (!mail_config_get_prompt_only_bcc ())
		return TRUE;
	
	mbox = e_message_box_new (_("This message contains only Bcc recipients.\nIt is "
				    "possible that the mail server may reveal the recipients "
				    "by adding an Apparently-To header.\nSend anyway?"),
				  E_MESSAGE_BOX_QUESTION,
				  GNOME_STOCK_BUTTON_YES,
				  GNOME_STOCK_BUTTON_NO,
				  NULL);
	
	gtk_signal_connect (GTK_OBJECT (mbox), "destroy",
			    msgbox_destroyed, &show_again);
	
	button = gnome_dialog_run_and_close (GNOME_DIALOG (mbox));
	
	mail_config_set_prompt_only_bcc (show_again);
	
	if (button == 0)
		return TRUE;
	else
		return FALSE;
}

static void
free_psd (GtkWidget *composer, gpointer user_data)
{
	struct post_send_data *psd = user_data;
	
	if (psd->folder)
		camel_object_unref (CAMEL_OBJECT (psd->folder));
	if (psd->uid)
		g_free (psd->uid);
	g_free (psd);
}

struct _send_data {
	EMsgComposer *composer;
	struct post_send_data *psd;
};

static void
composer_sent_cb(char *uri, CamelMimeMessage *message, gboolean sent, void *data)
{
	struct _send_data *send = data;
	
	if (sent) {
		if (send->psd) {
			camel_folder_set_message_flags (send->psd->folder, send->psd->uid,
							send->psd->flags, send->psd->flags);
		}
		gtk_widget_destroy (GTK_WIDGET (send->composer));
	} else {
		gtk_widget_show (GTK_WIDGET (send->composer));
		gtk_object_unref (GTK_OBJECT (send->composer));
	}
	
	g_free (send);
	camel_object_unref (CAMEL_OBJECT (message));
}

static CamelMimeMessage *
composer_get_message (EMsgComposer *composer)
{
	static char *recipient_type[] = {
		CAMEL_RECIPIENT_TYPE_TO,
		CAMEL_RECIPIENT_TYPE_CC,
		CAMEL_RECIPIENT_TYPE_BCC
	};
	const CamelInternetAddress *iaddr;
	const MailConfigAccount *account;
	CamelMimeMessage *message;
	const char *subject;
	int num_addrs, i;
	
	message = e_msg_composer_get_message (composer);
	if (message == NULL)
		return NULL;
	
	/* Check for recipients */
	for (num_addrs = 0, i = 0; i < 3; i++) {
		iaddr = camel_mime_message_get_recipients (message, recipient_type[i]);
		num_addrs += iaddr ? camel_address_length (CAMEL_ADDRESS (iaddr)) : 0;
	}
	
	/* I'm sensing a lack of love, er, I mean recipients. */
	if (num_addrs == 0) {
		GtkWidget *message_box;
		
		message_box = gnome_message_box_new (_("You must specify recipients in order to "
						       "send this message."),
						     GNOME_MESSAGE_BOX_WARNING,
						     GNOME_STOCK_BUTTON_OK,
						     NULL);
		
		gnome_dialog_run_and_close (GNOME_DIALOG (message_box));
		
		camel_object_unref (CAMEL_OBJECT (message));
		return NULL;
	}
	
	if (iaddr && num_addrs == camel_address_length (CAMEL_ADDRESS (iaddr))) {
		/* this means that the only recipients are Bcc's */
		if (!ask_confirm_for_only_bcc (composer)) {
			camel_object_unref (CAMEL_OBJECT (message));
			return NULL;
		}
	}

	/* Check for no subject */
	subject = camel_mime_message_get_subject (message);
	if (subject == NULL || subject[0] == '\0') {
		if (!ask_confirm_for_empty_subject (composer)) {
			camel_object_unref (CAMEL_OBJECT (message));
			return NULL;
		}
	}
	
	/* Add info about the sending account */
	account = e_msg_composer_get_preferred_account (composer);
	if (account) {
		camel_medium_set_header (CAMEL_MEDIUM (message), "X-Evolution-Account", account->name);
		camel_medium_set_header (CAMEL_MEDIUM (message), "X-Evolution-Transport", account->transport->url);
		camel_medium_set_header (CAMEL_MEDIUM (message), "X-Evolution-Fcc", account->sent_folder_uri);
	}
	
	return message;
}

void
composer_send_cb (EMsgComposer *composer, gpointer data)
{
	const MailConfigService *transport;
	CamelMimeMessage *message;
	struct post_send_data *psd = data;
	struct _send_data *send;
	
	if (!mail_config_is_configured ()) {
		GtkWidget *dialog;
		
		dialog = gnome_ok_dialog_parented (_("You must configure an account before you "
						     "can send this email."),
						   GTK_WINDOW (composer));
		gnome_dialog_run_and_close (GNOME_DIALOG (dialog));
		return;
	}
	
	message = composer_get_message (composer);
	if (!message)
		return;
	transport = mail_config_get_default_transport ();
	
	send = g_malloc (sizeof (*send));
	send->psd = psd;
	send->composer = composer;
	gtk_object_ref (GTK_OBJECT (composer));
	gtk_widget_hide (GTK_WIDGET (composer));
	mail_send_mail (transport->url, message, composer_sent_cb, send);
}

static void
append_mail_cleanup (CamelFolder *folder, CamelMimeMessage *msg, CamelMessageInfo *info, int ok, void *data)
{
	camel_message_info_free (info);
}

void
composer_postpone_cb (EMsgComposer *composer, gpointer data)
{
	extern CamelFolder *outbox_folder;
	CamelMimeMessage *message;
	CamelMessageInfo *info;
	struct post_send_data *psd = data;
	
	message = composer_get_message (composer);
	if (message == NULL)
		return;
	info = camel_message_info_new ();
	info->flags = CAMEL_MESSAGE_SEEN;
	
	mail_append_mail (outbox_folder, message, info, append_mail_cleanup, NULL);
	camel_object_unref (CAMEL_OBJECT (message));
	
	if (psd) {
		camel_folder_set_message_flags (psd->folder, psd->uid, psd->flags, psd->flags);
		free_psd (NULL, psd);
	}
	
	gtk_widget_destroy (GTK_WIDGET (composer));
}

static GtkWidget *
create_msg_composer (const char *url)
{
	const MailConfigAccount *account;
	gboolean send_html;
	EMsgComposer *composer;
	
	account   = mail_config_get_default_account ();
	send_html = mail_config_get_send_html ();
	
	composer = url ? e_msg_composer_new_from_url (url) : e_msg_composer_new ();

	if (composer) {
		e_msg_composer_set_send_html (composer, send_html);
		e_msg_composer_show_sig_file (composer);
	}

	return GTK_WIDGET (composer);
}

void
compose_msg (GtkWidget *widget, gpointer user_data)
{
	GtkWidget *composer;

	if (!check_send_configuration (FOLDER_BROWSER (user_data)))
		return;
	
	composer = create_msg_composer (NULL);
	if (!composer)
		return;
	
	gtk_signal_connect (GTK_OBJECT (composer), "send",
			    GTK_SIGNAL_FUNC (composer_send_cb), NULL);
	gtk_signal_connect (GTK_OBJECT (composer), "postpone",
			    GTK_SIGNAL_FUNC (composer_postpone_cb), NULL);
	
	gtk_widget_show (composer);
}

/* Send according to a mailto (RFC 2368) URL. */
void
send_to_url (const char *url)
{
	GtkWidget *composer;
	
	/* FIXME: no way to get folder browser? Not without
	 * big pain in the ass, as far as I can tell */
	if (!check_send_configuration (NULL))
		return;
	
	composer = create_msg_composer (url);
	if (!composer)
		return;
	
	gtk_signal_connect (GTK_OBJECT (composer), "send",
			    GTK_SIGNAL_FUNC (composer_send_cb), NULL);
	gtk_signal_connect (GTK_OBJECT (composer), "postpone",
			    GTK_SIGNAL_FUNC (composer_postpone_cb), NULL);

	gtk_widget_show (composer);
}	

static GList *
list_add_addresses (GList *list, const CamelInternetAddress *cia, const GSList *accounts,
		    GHashTable *rcpt_hash, const MailConfigAccount **me,
		    const char *ignore_addr)
{
	const char *name, *addr;
	const GSList *l;
	gboolean notme;
	char *full;
	int i;
	
	for (i = 0; camel_internet_address_get (cia, i, &name, &addr); i++) {
		/* Make sure we don't want to ignore this address */
		if (!ignore_addr || g_strcasecmp (ignore_addr, addr)) {
			/* now, we format this, as if for display, but does the composer
			   then use it as a real address?  If so, very broken. */
			/* we should probably pass around CamelAddresse's if thats what
			   we mean */
			full = camel_internet_address_format_address (name, addr);
			
			/* Here I'll check to see if the cc:'d address is the address
			   of the sender, and if so, don't add it to the cc: list; this
			   is to fix Bugzilla bug #455. */
			notme = TRUE;
			l = accounts;
			while (l) {
				const MailConfigAccount *acnt = l->data;
				
				if (!strcmp (acnt->id->address, addr)) {
					notme = FALSE;
					if (me && !*me)
						*me = acnt;
					break;
				}
				
				l = l->next;
			}
			
			if (notme && !g_hash_table_lookup (rcpt_hash, addr)) {
				g_hash_table_insert (rcpt_hash, (char *) addr, GINT_TO_POINTER (1));
				list = g_list_append (list, full);
			} else
				g_free (full);
		}
	}
	
	return list;
}

static const MailConfigAccount *
guess_me (const CamelInternetAddress *to, const CamelInternetAddress *cc, const GSList *accounts)
{
	const char *name, *addr;
	const GSList *l;
	gboolean notme;
	char *full;
	int i;
	
	if (to) {
		for (i = 0; camel_internet_address_get (to, i, &name, &addr); i++) {
			full = camel_internet_address_format_address (name, addr);
			l = accounts;
			while (l) {
				const MailConfigAccount *acnt = l->data;
				
				if (!strcmp (acnt->id->address, addr)) {
					notme = FALSE;
					return acnt;
				}
				
				l = l->next;
			}
		}
	}
	
	if (cc) {
		for (i = 0; camel_internet_address_get (cc, i, &name, &addr); i++) {
			full = camel_internet_address_format_address (name, addr);
			l = accounts;
			while (l) {
				const MailConfigAccount *acnt = l->data;
				
				if (!strcmp (acnt->id->address, addr)) {
					notme = FALSE;
					return acnt;
				}
				
				l = l->next;
			}
		}
	}
	
	return NULL;
}

static void
free_recipients (GList *list)
{
	GList *l;
	
	for (l = list; l; l = l->next)
		g_free (l->data);
	g_list_free (list);
}

static EMsgComposer *
mail_generate_reply (CamelFolder *folder, CamelMimeMessage *message, const char *uid, int mode)
{
	const CamelInternetAddress *reply_to, *sender, *to_addrs, *cc_addrs;
	const char *name = NULL, *address = NULL, *source = NULL;
	const char *message_id, *references, *reply_addr = NULL;
	char *text, *subject, *date_str;
	const MailConfigAccount *me;
	const GSList *accounts = NULL;
	GList *to = NULL, *cc = NULL;
	EMsgComposer *composer;
	time_t date;
	int offset;
	
	source = camel_mime_message_get_source (message);
	me = mail_config_get_account_by_source_url (source);

	composer = e_msg_composer_new_with_sig_file ();
	if (!composer)
		return NULL;
	
	/* FIXME: should probably use a shorter date string */
	sender = camel_mime_message_get_from (message);
	camel_internet_address_get (sender, 0, &name, &address);
	date = camel_mime_message_get_date (message, &offset);
	date_str = header_format_date (date, offset);
	text = mail_tool_quote_message (message, _("On %s, %s wrote:"), date_str, name && *name ? name : address);
	g_free (date_str);
	
	if (text) {
		e_msg_composer_set_body_text (composer, text);
		g_free (text);
	}
	
	/* Set the recipients */
	accounts = mail_config_get_accounts ();
	
	if (mode == REPLY_LIST) {
		CamelMessageInfo *info;
		const char *mlist;
		int i, max, len;
		
		info = camel_folder_get_message_info (folder, uid);
		mlist = camel_message_info_mlist (info);
		
		if (mlist) {
			/* look through the recipients to find the *real* mailing list address */
			len = strlen (mlist);
			
			to_addrs = camel_mime_message_get_recipients (message, CAMEL_RECIPIENT_TYPE_TO);
			max = camel_address_length (CAMEL_ADDRESS (to_addrs));
			for (i = 0; i < max; i++) {
				camel_internet_address_get (to_addrs, i, NULL, &address);
				if (!g_strncasecmp (address, mlist, len))
					break;
			}
			
			if (i == max) {
				cc_addrs = camel_mime_message_get_recipients (message, CAMEL_RECIPIENT_TYPE_CC);
				max = camel_address_length (CAMEL_ADDRESS (cc_addrs));
				for (i = 0; i < max; i++) {
					camel_internet_address_get (cc_addrs, i, NULL, &address);
					if (!g_strncasecmp (address, mlist, len))
						break;
				}
			}
			
			/* We only want to reply to the list address - if it even exists */
			to = address && i != max ? g_list_append (to, g_strdup (address)) : to;
		}
	} else {
		GHashTable *rcpt_hash;
			
		rcpt_hash = g_hash_table_new (g_str_hash, g_str_equal);
		
		reply_to = camel_mime_message_get_reply_to (message);
		if (!reply_to)
			reply_to = camel_mime_message_get_from (message);
		if (reply_to) {
			/* Get the Reply-To address so we can ignore references to it in the Cc: list */
			camel_internet_address_get (reply_to, 0, NULL, &reply_addr);
			
			g_hash_table_insert (rcpt_hash, (char *) reply_addr, GINT_TO_POINTER (1));
			to = g_list_append (to, camel_address_format (CAMEL_ADDRESS (reply_to)));
		}
		
		to_addrs = camel_mime_message_get_recipients (message, CAMEL_RECIPIENT_TYPE_TO);
		cc_addrs = camel_mime_message_get_recipients (message, CAMEL_RECIPIENT_TYPE_CC);
		
		if (mode == REPLY_ALL) {
			cc = list_add_addresses (cc, to_addrs, accounts, rcpt_hash, &me, NULL);
			cc = list_add_addresses (cc, cc_addrs, accounts, rcpt_hash, me ? NULL : &me, reply_addr);
		} else if (me == NULL) {
			me = guess_me (to_addrs, cc_addrs, accounts);
		}
		
		g_hash_table_destroy (rcpt_hash);
	}
	
	/* Set the subject of the new message. */
	subject = (char *)camel_mime_message_get_subject (message);
	if (!subject)
		subject = g_strdup ("");
	else {
		if (!g_strncasecmp (subject, "Re: ", 4))
			subject = g_strdup (subject);
		else
			subject = g_strdup_printf ("Re: %s", subject);
	}
	
	e_msg_composer_set_headers (composer, me ? me->name : NULL, to, cc, NULL, subject);
	free_recipients (to);
	free_recipients (cc);
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
	
	return composer;
}

void
mail_reply (CamelFolder *folder, CamelMimeMessage *msg, const char *uid, int mode)
{
	EMsgComposer *composer;
	struct post_send_data *psd;
	
	g_return_if_fail (folder != NULL);
	g_return_if_fail (msg != NULL);
	g_return_if_fail (uid != NULL);
	
	psd = g_new (struct post_send_data, 1);
	psd->folder = folder;
	camel_object_ref (CAMEL_OBJECT (psd->folder));
	psd->uid = g_strdup (uid);
	psd->flags = CAMEL_MESSAGE_ANSWERED;
	
	composer = mail_generate_reply (folder, msg, uid, mode);
	if (!composer)
		return;
	
	gtk_signal_connect (GTK_OBJECT (composer), "send",
			    GTK_SIGNAL_FUNC (composer_send_cb), psd);
	gtk_signal_connect (GTK_OBJECT (composer), "postpone",
			    GTK_SIGNAL_FUNC (composer_postpone_cb), psd);
	gtk_signal_connect (GTK_OBJECT (composer), "destroy",
			    GTK_SIGNAL_FUNC (free_psd), psd);
	
	gtk_widget_show (GTK_WIDGET (composer));	
	e_msg_composer_unset_changed (composer);
}

void
reply_to_sender (GtkWidget *widget, gpointer user_data)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
	
	if (!check_send_configuration (fb))
		return;
	
	mail_reply (fb->folder, fb->mail_display->current_message, 
		    fb->message_list->cursor_uid, REPLY_SENDER);
}

void
reply_to_list (GtkWidget *widget, gpointer user_data)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
	
	if (!check_send_configuration (fb))
		return;
	
	mail_reply (fb->folder, fb->mail_display->current_message, 
		    fb->message_list->cursor_uid, REPLY_LIST);
}

void
reply_to_all (GtkWidget *widget, gpointer user_data)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
	
	if (!check_send_configuration (fb))
		return;
	
	mail_reply (fb->folder, fb->mail_display->current_message, 
		    fb->message_list->cursor_uid, REPLY_ALL);
}

void
enumerate_msg (MessageList *ml, const char *uid, gpointer data)
{
	g_ptr_array_add ((GPtrArray *) data, g_strdup (uid));
}


static EMsgComposer *
forward_get_composer (const char *subject)
{
	const MailConfigAccount *account;
	EMsgComposer *composer;
	
	account  = mail_config_get_default_account ();
	composer = e_msg_composer_new_with_sig_file ();
	if (composer) {
		gtk_signal_connect (GTK_OBJECT (composer), "send",
				    GTK_SIGNAL_FUNC (composer_send_cb), NULL);
		gtk_signal_connect (GTK_OBJECT (composer), "postpone",
				    GTK_SIGNAL_FUNC (composer_postpone_cb), NULL);
		e_msg_composer_set_headers (composer, account->name, NULL, NULL, NULL, subject);
	} else {
		g_warning("Could not create composer");
	}
	
	return composer;
}

static void
do_forward_non_attached (CamelFolder *folder, char *uid, CamelMimeMessage *message, void *data)
{
	char *subject;
	char *text;
	
	if (!message)
		return;

	subject = mail_tool_generate_forward_subject (message);
	if (GPOINTER_TO_INT (data) == MAIL_CONFIG_FORWARD_INLINE)
		text = mail_tool_forward_message (message);
	else
		text = mail_tool_quote_message (message, _("Forwarded message:\n"));

	if (text) {
		EMsgComposer *composer = forward_get_composer (subject);
		if (composer) {
			e_msg_composer_set_body_text (composer, text);
			gtk_widget_show (GTK_WIDGET (composer));
			e_msg_composer_unset_changed (composer);
		}
		g_free (text);
	}

	g_free (subject);
}

static void
forward_message (FolderBrowser *fb, MailConfigForwardStyle style)
{
	if (!fb->message_list->cursor_uid)
		return;
	if (!check_send_configuration (fb))
		return;

	mail_get_message (fb->folder, fb->message_list->cursor_uid,
			  do_forward_non_attached, GINT_TO_POINTER (style),
			  mail_thread_new);
}

void
forward_inline (GtkWidget *widget, gpointer user_data)
{
	forward_message (user_data, MAIL_CONFIG_FORWARD_INLINE);
}

void
forward_quoted (GtkWidget *widget, gpointer user_data)
{
	forward_message (user_data, MAIL_CONFIG_FORWARD_QUOTED);
}

static void
do_forward_attach (CamelFolder *folder, GPtrArray *messages, CamelMimePart *part, char *subject, void *data)
{
	if (part) {
		EMsgComposer *composer = forward_get_composer (subject);
		if (composer) {
			e_msg_composer_attach (composer, part);
			gtk_widget_show (GTK_WIDGET (composer));
			e_msg_composer_unset_changed (composer);
		}
	}
}

void
forward_attached (GtkWidget *widget, gpointer user_data)
{
	GPtrArray *uids;
	FolderBrowser *fb = (FolderBrowser *)user_data;
	
	if (!check_send_configuration (fb))
		return;
	
	uids = g_ptr_array_new ();
	message_list_foreach (fb->message_list, enumerate_msg, uids);
	mail_build_attachment (fb->message_list->folder, uids, do_forward_attach, NULL);
}

void
forward (GtkWidget *widget, gpointer user_data)
{
	MailConfigForwardStyle style = mail_config_get_default_forward_style ();

	if (style == MAIL_CONFIG_FORWARD_ATTACHED)
		forward_attached (widget, user_data);
	else
		forward_message (user_data, style);
}

static void
transfer_msg (GtkWidget *widget, gpointer user_data, gboolean delete_from_source)
{
	FolderBrowser *fb = user_data;
	MessageList *ml = fb->message_list;
	GPtrArray *uids;
	char *uri, *physical, *path;
	char *desc;
	const char *allowed_types[] = { "mail", "vtrash", NULL };
	extern EvolutionShellClient *global_shell_client;
	static char *last = NULL;
	
	if (last == NULL)
		last = g_strdup ("");
	
	if (delete_from_source)
		desc = _("Move message(s) to");
	else
		desc = _("Copy message(s) to");
	
	evolution_shell_client_user_select_folder  (global_shell_client,
						    desc,
						    last, allowed_types, &uri, &physical);
	if (!uri)
		return;
	
	path = strchr (uri, '/');
	if (path && strcmp (last, path) != 0) {
		g_free (last);
		last = g_strdup (path);
	}
	g_free (uri);
	
	uids = g_ptr_array_new ();
	message_list_foreach (ml, enumerate_msg, uids);
	mail_transfer_messages (ml->folder, uids, delete_from_source,
				physical, NULL, NULL);
}

void
move_msg (GtkWidget *widget, gpointer user_data)
{
	transfer_msg (widget, user_data, TRUE);
}

void
copy_msg (GtkWidget *widget, gpointer user_data)
{
	transfer_msg (widget, user_data, FALSE);
}

/* Copied from e-shell-view.c */
static GtkWidget *
find_socket (GtkContainer *container)
{
        GList *children, *tmp;

        children = gtk_container_children (container);
        while (children) {
                if (BONOBO_IS_SOCKET (children->data))
                        return children->data;
                else if (GTK_IS_CONTAINER (children->data)) {
                        GtkWidget *socket = find_socket (children->data);
                        if (socket)
                                return socket;
                }
                tmp = children->next;
                g_list_free_1 (children);
                children = tmp;
        }
        return NULL;
}

void
addrbook_sender (GtkWidget *widget, gpointer user_data)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
	CamelMimeMessage *msg = NULL;
	const CamelInternetAddress *addr;
	gchar *addr_str;
	GtkWidget *win;
	GtkWidget *control;
	GtkWidget *socket;

	if (fb && fb->mail_display)
		msg = fb->mail_display->current_message;

	if (msg == NULL)
		return;

	addr = camel_mime_message_get_from (msg);
	if (addr == NULL)
		return;

	addr_str = camel_address_format (CAMEL_ADDRESS (addr));

	win = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title (GTK_WINDOW (win), _("Sender"));

	control = bonobo_widget_new_control ("OAFIID:GNOME_Evolution_Addressbook_AddressPopup",
					     CORBA_OBJECT_NIL);
	bonobo_widget_set_property (BONOBO_WIDGET (control),
				    "email", addr_str,
				    NULL);

	socket = find_socket (GTK_CONTAINER (control));
	gtk_signal_connect_object (GTK_OBJECT (socket),
				   "destroy",
				   GTK_SIGNAL_FUNC (gtk_widget_destroy),
				   GTK_OBJECT (win));

	gtk_container_add (GTK_CONTAINER (win), control);
	gtk_widget_show_all (win);
}

void
apply_filters (GtkWidget *widget, gpointer user_data)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
        MessageList *ml = fb->message_list;
	GPtrArray *uids;
	
	uids = g_ptr_array_new ();
	message_list_foreach (ml, enumerate_msg, uids);

	mail_filter_on_demand(fb->folder, uids);
}

void
select_all (BonoboUIComponent *uih, void *user_data, const char *path)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
        MessageList *ml = fb->message_list;
	ESelectionModel *etsm = e_tree_get_selection_model (ml->tree);

	if (ml->folder == NULL)
		return;

	e_selection_model_select_all (etsm);
}

/* Thread selection */

typedef struct thread_select_info {
	MessageList *ml;
	GPtrArray *paths;
} thread_select_info_t;

static gboolean
select_node (ETreeModel *tm, ETreePath path, gpointer user_data)
{
	thread_select_info_t *tsi = (thread_select_info_t *) user_data;

	g_ptr_array_add (tsi->paths, path);
	return FALSE; /*not done yet*/
}

static void
thread_select_foreach (ETreePath path, gpointer user_data)
{
	thread_select_info_t *tsi = (thread_select_info_t *) user_data;
	ETreeModel *tm = tsi->ml->model;
	ETreePath node;

	/* @path part of the initial selection. If it has children,
	 * we select them as well. If it doesn't, we select its siblings and
	 * their children (ie, the current node must be inside the thread
	 * that the user wants to mark.
	 */

	if (e_tree_model_node_get_first_child (tm, path)) 
		node = path;
	else {
		node = e_tree_model_node_get_parent (tm, path);

		/* Let's make an exception: if no parent, then we're about
		 * to mark the whole tree. No. */
		if (e_tree_model_node_is_root (tm, node)) 
			node = path;
	}

	e_tree_model_node_traverse (tm, node, select_node, tsi);
}

void
select_thread (BonoboUIComponent *uih, void *user_data, const char *path)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
        MessageList *ml = fb->message_list;
	ETreeSelectionModel *selection_model;
	thread_select_info_t tsi;
	int i;

	if (ml->folder == NULL)
		return;

	/* For every selected item, select the thread containing it.
	 * We can't alter the selection while iterating through it,
	 * so build up a list of paths.
	 */

	tsi.ml = ml;
	tsi.paths = g_ptr_array_new ();

	e_tree_selected_path_foreach (ml->tree, thread_select_foreach, &tsi);

	selection_model = E_TREE_SELECTION_MODEL (e_tree_get_selection_model (ml->tree));

	for (i = 0; i < tsi.paths->len; i++)
		e_tree_selection_model_add_to_selection (selection_model,
							 tsi.paths->pdata[i]);
	g_ptr_array_free (tsi.paths, TRUE);
}

void
invert_selection (BonoboUIComponent *uih, void *user_data, const char *path)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
        MessageList *ml = fb->message_list;
	ESelectionModel *etsm = e_tree_get_selection_model (ml->tree);

	if (ml->folder == NULL)
		return;

	e_selection_model_invert_selection (etsm);
}

/* flag all selected messages. Return number flagged */
static int
flag_messages(FolderBrowser *fb, guint32 mask, guint32 set)
{
        MessageList *ml = fb->message_list;
	GPtrArray *uids;
	int i;
	
	if (ml->folder == NULL)
		return 0;
	
	/* could just use specific callback but i'm lazy */
	uids = g_ptr_array_new ();
	message_list_foreach (ml, enumerate_msg, uids);
	camel_folder_freeze (ml->folder);
	for (i = 0; i < uids->len; i++) {
		camel_folder_set_message_flags (ml->folder, uids->pdata[i], mask, set);
		g_free (uids->pdata[i]);
	}
	camel_folder_thaw (ml->folder);
	
	g_ptr_array_free (uids, TRUE);

	return i;
}

static int
toggle_flags (FolderBrowser *fb, guint32 mask)
{
        MessageList *ml = fb->message_list;
	GPtrArray *uids;
	int i;
	
	if (ml->folder == NULL)
		return 0;
	
	/* could just use specific callback but i'm lazy */
	uids = g_ptr_array_new ();
	message_list_foreach (ml, enumerate_msg, uids);
	camel_folder_freeze (ml->folder);
	for (i = 0; i < uids->len; i++) {
		gint flags;

		flags = camel_folder_get_message_flags (ml->folder, uids->pdata[i]);

		if (flags & mask)
			camel_folder_set_message_flags (ml->folder, uids->pdata[i], mask, 0);
		else {
			if ((mask & CAMEL_MESSAGE_FLAGGED) && (flags & CAMEL_MESSAGE_DELETED))
				camel_folder_set_message_flags (ml->folder, uids->pdata[i], CAMEL_MESSAGE_DELETED, 0);
			camel_folder_set_message_flags (ml->folder, uids->pdata[i], mask, mask);
		}
		
		g_free (uids->pdata[i]);
	}
	camel_folder_thaw (ml->folder);
	
	g_ptr_array_free (uids, TRUE);

	return i;
}

void
mark_as_seen (BonoboUIComponent *uih, void *user_data, const char *path)
{
	flag_messages (FOLDER_BROWSER(user_data), CAMEL_MESSAGE_SEEN, CAMEL_MESSAGE_SEEN);
}

void
mark_as_unseen (BonoboUIComponent *uih, void *user_data, const char *path)
{
	flag_messages (FOLDER_BROWSER(user_data), CAMEL_MESSAGE_SEEN, 0);
}

void
mark_all_as_seen (BonoboUIComponent *uih, void *user_data, const char *path)
{
	select_all (uih, user_data, path);
	flag_messages (FOLDER_BROWSER (user_data), CAMEL_MESSAGE_SEEN, CAMEL_MESSAGE_SEEN);
}

void
mark_as_important (BonoboUIComponent *uih, void *user_data, const char *path)
{
	flag_messages (FOLDER_BROWSER (user_data), CAMEL_MESSAGE_DELETED, 0);
	flag_messages (FOLDER_BROWSER (user_data), CAMEL_MESSAGE_FLAGGED, CAMEL_MESSAGE_FLAGGED);
}

void
toggle_as_important (BonoboUIComponent *uih, void *user_data, const char *path)
{
	toggle_flags (FOLDER_BROWSER (user_data), CAMEL_MESSAGE_FLAGGED);
}

static void
do_edit_messages(CamelFolder *folder, GPtrArray *uids, GPtrArray *messages, void *data)
{
	/*FolderBrowser *fb = data;*/
	int i;
	
	for (i = 0; i < messages->len; i++) {
		EMsgComposer *composer;

		composer = e_msg_composer_new_with_message (messages->pdata[i]);

		if (composer) {
			gtk_signal_connect (GTK_OBJECT (composer), "send",
					    composer_send_cb, NULL);
			gtk_signal_connect (GTK_OBJECT (composer), "postpone",
					    composer_postpone_cb, NULL);
			gtk_widget_show (GTK_WIDGET (composer));
		}
	}
}

static gboolean
are_you_sure (const char *msg, GPtrArray *uids, FolderBrowser *fb)
{
	GtkWidget *window = gtk_widget_get_ancestor (GTK_WIDGET (fb), GTK_TYPE_WINDOW);
	GtkWidget *dialog;
	char *buf;
	int button, i;

	buf = g_strdup_printf (msg, uids->len);
	dialog = gnome_ok_cancel_dialog_parented (buf, NULL, NULL, (GtkWindow *)window);
	button = gnome_dialog_run_and_close (GNOME_DIALOG (dialog));
	if (button != 0) {
		for (i = 0; i < uids->len; i++)
			g_free (uids->pdata[i]);
		g_ptr_array_free (uids, TRUE);
	}

	return button == 0;
}

static void
edit_msg_internal (FolderBrowser *fb)
{
	GPtrArray *uids;
	
	if (!check_send_configuration (fb))
		return;
	
	uids = g_ptr_array_new ();
	message_list_foreach (fb->message_list, enumerate_msg, uids);
	
	if (uids->len > 10 && !are_you_sure (_("Are you sure you want to edit all %d messages?"), uids, fb)) {
		int i;
		
		for (i = 0; i < uids->len; i++)
			g_free (uids->pdata[i]);
		
		g_ptr_array_free (uids, TRUE);
		
		return;
	}

	mail_get_messages (fb->folder, uids, do_edit_messages, fb);
}

void
edit_msg (GtkWidget *widget, gpointer user_data)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
	
	if (!folder_browser_is_drafts (fb)) {
		GtkWidget *message;
		
		message = gnome_warning_dialog (_("You may only edit messages saved\n"
						  "in the Drafts folder."));
		gnome_dialog_run_and_close (GNOME_DIALOG (message));
		return;
	}
	
	edit_msg_internal (fb);
}

static void
do_resend_messages (CamelFolder *folder, GPtrArray *uids, GPtrArray *messages, void *data)
{
	int i;
	
	for (i = 0; i < messages->len; i++) {
		/* generate a new Message-Id because they need to be unique */
		camel_mime_message_set_message_id (messages->pdata[i], NULL);
	}
	
	/* "Resend" should open up the composer to let the user edit the message */
	do_edit_messages (folder, uids, messages, data);
}



void
resend_msg (GtkWidget *widget, gpointer user_data)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
	GPtrArray *uids;
	
	if (!folder_browser_is_sent (fb)) {
		GtkWidget *message;
		
		message = gnome_warning_dialog (_("You may only resend messages\n"
						  "in the Sent folder."));
		gnome_dialog_run_and_close (GNOME_DIALOG (message));
		return;
	}
	
	if (!check_send_configuration (fb))
		return;
	
	uids = g_ptr_array_new ();
	message_list_foreach (fb->message_list, enumerate_msg, uids);
	
	if (uids->len > 10 && !are_you_sure (_("Are you sure you want to resend all %d messages?"), uids, fb)) {
		int i;
		
		for (i = 0; i < uids->len; i++)
			g_free (uids->pdata[i]);
		
		g_ptr_array_free (uids, TRUE);
		
		return;
	}
	
	mail_get_messages (fb->folder, uids, do_resend_messages, fb);
}

void
search_msg (GtkWidget *widget, gpointer user_data)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
	GtkWidget *w;

	if (fb->mail_display->current_message == NULL) {
		gtk_widget_show_all (gnome_warning_dialog (_("No Message Selected")));
		return;
	}

	w = mail_search_new (fb->mail_display);
	gtk_widget_show_all (w);
}

void
load_images (GtkWidget *widget, gpointer user_data)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);

	mail_display_load_images (fb->mail_display);
}

static void
save_msg_ok (GtkWidget *widget, gpointer user_data)
{
	CamelFolder *folder;
	GPtrArray *uids;
	char *path;
	int fd, ret = 0;
	
	/* FIXME: is path an allocated string? */
	path = gtk_file_selection_get_filename (GTK_FILE_SELECTION (user_data));
	
        fd = open (path, O_RDONLY);
	if (fd != -1) {
		GtkWidget *dlg;
		GtkWidget *text;
		
		close (fd);
		
		dlg = gnome_dialog_new (_("Overwrite file?"),
					GNOME_STOCK_BUTTON_YES, 
					GNOME_STOCK_BUTTON_NO,
					NULL);
		text = gtk_label_new (_("A file by that name already exists.\nOverwrite it?"));
		gtk_box_pack_start (GTK_BOX (GNOME_DIALOG (dlg)->vbox), text, TRUE, TRUE, 4);
		gtk_window_set_policy (GTK_WINDOW (dlg), FALSE, TRUE, FALSE);
		gtk_widget_show (text);
		
		ret = gnome_dialog_run_and_close (GNOME_DIALOG (dlg));
	}
	
	if (ret == 0) {
		folder = gtk_object_get_data (GTK_OBJECT (user_data), "folder");
		uids = gtk_object_get_data (GTK_OBJECT (user_data), "uids");
		gtk_object_remove_no_notify (GTK_OBJECT (user_data), "uids");
		mail_save_messages (folder, uids, path, NULL, NULL);
		gtk_widget_destroy (GTK_WIDGET (user_data));
	}
}

static void
save_msg_destroy (gpointer user_data)
{
	GPtrArray *uids = user_data;
	
	if (uids) {
		int i;
		
		for (i = 0; i < uids->len; i++)
			g_free (uids->pdata[i]);
		
		g_ptr_array_free (uids, TRUE);
	}
}

void
save_msg (GtkWidget *widget, gpointer user_data)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
	GtkFileSelection *filesel;
	GPtrArray *uids;
	char *title, *path;
	
	uids = g_ptr_array_new ();
	message_list_foreach (fb->message_list, enumerate_msg, uids);
	
	if (uids->len == 1)
		title = _("Save Message As...");
	else
		title = _("Save Messages As...");
	
	filesel = GTK_FILE_SELECTION (gtk_file_selection_new (title));
	path = g_strdup_printf ("%s/", g_get_home_dir ());
	gtk_file_selection_set_filename (filesel, path);
	g_free (path);
	gtk_object_set_data_full (GTK_OBJECT (filesel), "uids", uids, save_msg_destroy);
	gtk_object_set_data (GTK_OBJECT (filesel), "folder", fb->folder);
	gtk_signal_connect (GTK_OBJECT (filesel->ok_button),
			    "clicked", GTK_SIGNAL_FUNC (save_msg_ok), filesel);
	gtk_signal_connect_object (GTK_OBJECT (filesel->cancel_button),
				   "clicked", GTK_SIGNAL_FUNC (gtk_widget_destroy),
				   GTK_OBJECT (filesel));
	
	gtk_widget_show (GTK_WIDGET (filesel));
}

void
delete_msg (GtkWidget *button, gpointer user_data)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
	int deleted, row;

	deleted = flag_messages (fb, CAMEL_MESSAGE_DELETED | CAMEL_MESSAGE_SEEN,
				 CAMEL_MESSAGE_DELETED | CAMEL_MESSAGE_SEEN);
	if (deleted == 1) {
		row = e_tree_row_of_node (fb->message_list->tree,
					  e_tree_get_cursor (fb->message_list->tree));
		message_list_select (fb->message_list, row, MESSAGE_LIST_SELECT_NEXT,
				     0, CAMEL_MESSAGE_DELETED);
	}
}

void
undelete_msg (GtkWidget *button, gpointer user_data)
{
	flag_messages (FOLDER_BROWSER (user_data), CAMEL_MESSAGE_DELETED, 0);
}

void
next_msg (GtkWidget *button, gpointer user_data)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
	int row;
	
	row = e_tree_row_of_node (fb->message_list->tree, e_tree_get_cursor (fb->message_list->tree));
	message_list_select (fb->message_list, row, MESSAGE_LIST_SELECT_NEXT, 0, 0);
}

void
next_unread_msg (GtkWidget *button, gpointer user_data)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
	int row;
	
	row = e_tree_row_of_node (fb->message_list->tree, e_tree_get_cursor (fb->message_list->tree));
	message_list_select (fb->message_list, row,
			     MESSAGE_LIST_SELECT_NEXT,
			     0, CAMEL_MESSAGE_SEEN);
}

void
next_flagged_msg (GtkWidget *button, gpointer user_data)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
	int row;
	
	row = e_tree_row_of_node (fb->message_list->tree, e_tree_get_cursor (fb->message_list->tree));
	message_list_select (fb->message_list, row,
			     MESSAGE_LIST_SELECT_NEXT,
			     CAMEL_MESSAGE_FLAGGED, CAMEL_MESSAGE_FLAGGED);
}

void
previous_msg (GtkWidget *button, gpointer user_data)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
	int row;
	
	row = e_tree_row_of_node (fb->message_list->tree, e_tree_get_cursor (fb->message_list->tree));
	message_list_select (fb->message_list, row, MESSAGE_LIST_SELECT_PREVIOUS, 0, 0);
}

void
previous_unread_msg (GtkWidget *button, gpointer user_data)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
	int row;
	
	row = e_tree_row_of_node (fb->message_list->tree, e_tree_get_cursor (fb->message_list->tree));
	message_list_select (fb->message_list, row,
			     MESSAGE_LIST_SELECT_PREVIOUS,
			     0, CAMEL_MESSAGE_SEEN);
}

void
previous_flagged_msg (GtkWidget *button, gpointer user_data)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
	int row;
	
	row = e_tree_row_of_node (fb->message_list->tree, e_tree_get_cursor (fb->message_list->tree));
	message_list_select (fb->message_list, row,
			     MESSAGE_LIST_SELECT_PREVIOUS,
			     CAMEL_MESSAGE_FLAGGED, CAMEL_MESSAGE_FLAGGED);
}

static void
expunged_folder (CamelFolder *f, void *data)
{
	FolderBrowser *fb = data;

	fb->expunging = NULL;
}

void
expunge_folder (BonoboUIComponent *uih, void *user_data, const char *path)
{
	FolderBrowser *fb = FOLDER_BROWSER(user_data);

	if (fb->folder
	    && (fb->expunging == NULL
		|| fb->folder != fb->expunging)) {
		fb->expunging = fb->folder;
		mail_expunge_folder(fb->folder, expunged_folder, fb);
	}
}

/********************** Begin Filter Editor ********************/

static GtkWidget *filter_editor = NULL;

static void
filter_editor_destroy (GtkWidget *dialog, gpointer user_data)
{
	filter_editor = NULL;
}

static void
filter_editor_clicked (GtkWidget *dialog, int button, FolderBrowser *fb)
{
	FilterContext *fc;
	
	if (button == 0) {
		char *user;
		
		fc = gtk_object_get_data (GTK_OBJECT (dialog), "context");
		user = g_strdup_printf ("%s/filters.xml", evolution_dir);
		rule_context_save ((RuleContext *)fc, user);
		g_free (user);
	}
	
	if (button != -1) {
		gnome_dialog_close (GNOME_DIALOG (dialog));
	}
}

static const char *filter_source_names[] = {
	"incoming",
	"outgoing",
	NULL,
};

void
filter_edit (BonoboUIComponent *uih, void *user_data, const char *path)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
	FilterContext *fc;
	char *user, *system;
	
	if (filter_editor) {
		/* FIXME: raise the filter_editor dialog? */
		return;
	}
	
	fc = filter_context_new ();
	user = g_strdup_printf ("%s/filters.xml", evolution_dir);
	system = EVOLUTION_DATADIR "/evolution/filtertypes.xml";
	rule_context_load ((RuleContext *)fc, system, user);
	g_free (user);
	
	if (((RuleContext *)fc)->error) {
		GtkWidget *dialog;
		gchar *err;
		
		err = g_strdup_printf (_("Error loading filter information:\n%s"),
				       ((RuleContext *)fc)->error);
		dialog = gnome_warning_dialog (err);
		g_free (err);
		
		gnome_dialog_run_and_close (GNOME_DIALOG (dialog));
		return;
	}
	
	filter_editor = (GtkWidget *)filter_editor_new (fc, filter_source_names);
	gtk_window_set_title (GTK_WINDOW (filter_editor), _("Filters"));
	
	gtk_object_set_data_full (GTK_OBJECT (filter_editor), "context", fc, (GtkDestroyNotify)gtk_object_unref);
	gtk_signal_connect (GTK_OBJECT (filter_editor), "clicked", filter_editor_clicked, fb);
	gtk_signal_connect (GTK_OBJECT (filter_editor), "destroy", filter_editor_destroy, NULL);
	gtk_widget_show (GTK_WIDGET (filter_editor));
}

/********************** End Filter Editor ********************/

void
vfolder_edit_vfolders (BonoboUIComponent *uih, void *user_data, const char *path)
{
	vfolder_edit ();
}

void
providers_config (BonoboUIComponent *uih, void *user_data, const char *path)
{
	static MailAccountsDialog *dialog = NULL;
	
	if (!dialog) {
		dialog = mail_accounts_dialog_new ((FOLDER_BROWSER (user_data))->shell);
		gnome_dialog_run_and_close (GNOME_DIALOG (dialog));
		dialog = NULL;
	} else {
		/* FIXME: raise the dialog? */
	}
}

/*
 * FIXME: This routine could be made generic, by having a closure
 * function plus data, and having the whole process be taken care
 * of for you
 */
static void
do_mail_print (MailDisplay *md, gboolean preview)
{
	GnomePrintContext *print_context;
	GnomePrintMaster *print_master;
	GnomePrintDialog *gpd;
	GnomePrinter *printer = NULL;
	int copies = 1;
	int collate = FALSE;

	if (!preview){

		gpd = GNOME_PRINT_DIALOG (
			gnome_print_dialog_new (_("Print Message"), GNOME_PRINT_DIALOG_COPIES));
		gnome_dialog_set_default (GNOME_DIALOG (gpd), GNOME_PRINT_PRINT);

		switch (gnome_dialog_run (GNOME_DIALOG (gpd))){
		case GNOME_PRINT_PRINT:
			break;
			
		case GNOME_PRINT_PREVIEW:
			preview = TRUE;
			break;

		case -1:
			return;

		default:
			gnome_dialog_close (GNOME_DIALOG (gpd));
			return;
		}

		gnome_print_dialog_get_copies (gpd, &copies, &collate);
		printer = gnome_print_dialog_get_printer (gpd);
		gnome_dialog_close (GNOME_DIALOG (gpd));
	}

	print_master = gnome_print_master_new ();

/*	FIXME: set paper size gnome_print_master_set_paper (print_master,  */

	if (printer)
		gnome_print_master_set_printer (print_master, printer);
	gnome_print_master_set_copies (print_master, copies, collate);
	print_context = gnome_print_master_get_context (print_master);
	gtk_html_print (md->html, print_context);
	gnome_print_master_close (print_master);

	if (preview){
		gboolean landscape = FALSE;
		GnomePrintMasterPreview *preview;
		
		preview = gnome_print_master_preview_new_with_orientation (
			print_master, _("Print Preview"), landscape);
		gtk_widget_show (GTK_WIDGET (preview));
	} else {
		int result = gnome_print_master_print (print_master);

		if (result == -1){
			e_notice (NULL, GNOME_MESSAGE_BOX_ERROR,
				  _("Printing of message failed"));
		}
	}
	gtk_object_unref (GTK_OBJECT (print_master));
}

void
mail_print_preview_msg (MailDisplay *md)
{
	do_mail_print (md, TRUE);
}

void
mail_print_msg (MailDisplay *md)
{
	do_mail_print (md, FALSE);
}

void
print_msg (GtkWidget *button, gpointer user_data)
{
	FolderBrowser *fb = user_data;

	mail_print_msg (fb->mail_display);
}

void
print_preview_msg (GtkWidget *button, gpointer user_data)
{
	FolderBrowser *fb = user_data;

	mail_print_preview_msg (fb->mail_display);
}

/******************** Begin Subscription Dialog ***************************/

static GtkWidget *subscribe_dialog = NULL;

static void
subscribe_dialog_destroy (GtkWidget *widget, gpointer user_data)
{
	subscribe_dialog = NULL;
}

void
manage_subscriptions (BonoboUIComponent *uih, void *user_data, const char *path)
{
	if (!subscribe_dialog) {
		subscribe_dialog = subscribe_dialog_new ((FOLDER_BROWSER (user_data))->shell);
		gtk_signal_connect (GTK_OBJECT (subscribe_dialog), "destroy",
				    subscribe_dialog_destroy, NULL);
		
		gtk_widget_show (subscribe_dialog);
	} else {
		/* FIXME: raise the subscription dialog window... */
	}
}

/******************** End Subscription Dialog ***************************/

void
configure_folder (BonoboUIComponent *uih, void *user_data, const char *path)
{
	FolderBrowser *fb = FOLDER_BROWSER(user_data);

	mail_local_reconfigure_folder(fb);
}

static void
do_view_message (CamelFolder *folder, char *uid, CamelMimeMessage *message, void *data)
{
	FolderBrowser *fb = FOLDER_BROWSER (data);
	
	if (message && fb) {
		GtkWidget *mb;
		
		camel_folder_set_message_flags (folder, uid, CAMEL_MESSAGE_SEEN, CAMEL_MESSAGE_SEEN);
		mb = message_browser_new (fb->shell, fb->uri, uid);
		gtk_widget_show (mb);
	}
}

void
view_msg (GtkWidget *widget, gpointer user_data)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
	GPtrArray *uids;
	int i;
	
	if (!fb->folder)
		return;
	
	uids = g_ptr_array_new ();
	message_list_foreach (fb->message_list, enumerate_msg, uids);

	if (uids->len > 10 && !are_you_sure (_("Are you sure you want to open all %d messages in separate windows?"), uids, fb))
		return;

	for (i = 0; i < uids->len; i++) {
		mail_get_message (fb->folder, uids->pdata [i], do_view_message, fb, mail_thread_queued);
		g_free (uids->pdata [i]);
	}
	g_ptr_array_free (uids, TRUE);
}

void
open_msg (GtkWidget *widget, gpointer user_data)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
	
	if (folder_browser_is_drafts (fb))
		edit_msg_internal (fb);
	else
		view_msg (NULL, user_data);
}

void
open_message (BonoboUIComponent *uih, void *user_data, const char *path)
{
	open_msg (NULL, user_data);
}

void
edit_message (BonoboUIComponent *uih, void *user_data, const char *path)
{
        edit_msg (NULL, user_data);
}

void
stop_threads (BonoboUIComponent *uih, void *user_data, const char *path)
{
	camel_operation_cancel (NULL);
}

static void
empty_trash_expunged_cb (CamelFolder *folder, void *data)
{
	camel_object_unref (CAMEL_OBJECT (folder));
}

void
empty_trash (BonoboUIComponent *uih, void *user_data, const char *path)
{
	MailConfigAccount *account;
	CamelProvider *provider;
	CamelFolder *vtrash;
	const GSList *accounts;
	CamelException *ex;
	
	ex = camel_exception_new ();
	
	/* expunge all remote stores */
	accounts = mail_config_get_accounts ();
	while (accounts) {
		account = accounts->data;
		
		/* make sure this is a valid source */
		if (account->source && account->source->url) {
			provider = camel_session_get_provider (session, account->source->url, NULL);			
			if (provider) {
				/* make sure this store is a remote store */
				if (provider->flags & CAMEL_PROVIDER_IS_STORAGE &&
				    provider->flags & CAMEL_PROVIDER_IS_REMOTE) {
					char *url;
					
					url = g_strdup_printf ("vtrash:%s", account->source->url);
					vtrash = mail_tool_uri_to_folder (url, NULL);
					g_free (url);
					
					if (vtrash)
						mail_expunge_folder (vtrash, empty_trash_expunged_cb, NULL);
				}
			}
		}
		accounts = accounts->next;
	}
	
	/* Now empty the local trash folder */
	vtrash = mail_tool_uri_to_folder ("vtrash:file:/", ex);
	if (vtrash)
		mail_expunge_folder (vtrash, empty_trash_expunged_cb, NULL);
	
	camel_exception_free (ex);
}

static void
create_folders (EvolutionStorage *storage, const char *prefix, CamelFolderInfo *fi)
{
	char *path;
	
	if (fi->url) {
		mail_folder_cache_set_update_estorage (fi->url, storage);
		mail_folder_cache_note_folderinfo (fi->url, fi);
	}

	path = g_strdup_printf ("%s/%s", prefix, fi->name);
	evolution_storage_new_folder (storage, path, fi->name,
				      "mail", fi->url ? fi->url : "",
				      fi->name, /* description */
				      fi->unread_message_count > 0);

	if (fi->child)
		create_folders (storage, path, fi->child);
	g_free (path);
	
	if (fi->sibling)
		create_folders (storage, prefix, fi->sibling);
}

void
folder_created (CamelStore *store, const char *prefix, CamelFolderInfo *fi)
{
	EvolutionStorage *storage;
	
	if ((storage = mail_lookup_storage (store))) {
		create_folders (storage, prefix, fi);
		gtk_object_unref (GTK_OBJECT (storage));
	}
}

void
mail_storage_create_folder (EvolutionStorage *storage, CamelStore *store, CamelFolderInfo *fi)
{
	gboolean unref = FALSE;
	
	if (!storage && store) {
		storage = mail_lookup_storage (store);
		unref = TRUE;
	}
	
	if (storage) {
		if (fi)
			create_folders (storage, "", fi);
		
		if (unref)
			gtk_object_unref (GTK_OBJECT (storage));
	}
}

static void
delete_folders (EvolutionStorage *storage, CamelFolderInfo *fi)
{
	char *path;
	
	if (fi->child)
		delete_folders (storage, fi->child);
	
	path = g_strdup_printf ("/%s", fi->full_name);
	evolution_storage_removed_folder (storage, path);
	g_free (path);
	
	if (fi->sibling)
		delete_folders (storage, fi->sibling);
}

void
folder_deleted (CamelStore *store, CamelFolderInfo *fi)
{
	EvolutionStorage *storage;
	
	if ((storage = mail_lookup_storage (store))) {
		if (fi)
			delete_folders (storage, fi);
		
		gtk_object_unref (GTK_OBJECT (storage));
	}
}
