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
#include <gal/widgets/e-unicode.h>
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

void
send_receive_mail (GtkWidget *widget, gpointer user_data)
{
	const MailConfigAccount *account;
	
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
ask_confirm_for_unwanted_html_mail (EMsgComposer *composer, EDestination **recipients)
{
	gboolean show_again = TRUE;
	GString *str;
	GtkWidget *mbox;
	gint i, button;
	
	if (!mail_config_get_confirm_unwanted_html ()) {
		g_message ("doesn't want to see confirm html messages!");
		return TRUE;
	}
	
	/* FIXME: this wording sucks */
	str = g_string_new (_("You are sending an HTML-formatted message, but the following recipients "
			      "do not want HTML-formatted mail:\n"));
	for (i = 0; recipients[i] != NULL; ++i) {
		if (!e_destination_get_html_mail_pref (recipients[i])) {
			const char *name;
			char *buf;
			
			name = e_destination_get_textrep (recipients[i]);
			buf = e_utf8_to_locale_string (name);
			
			g_string_sprintfa (str, "     %s\n", buf);
			g_free (buf);
		}
	}
	
	g_string_append (str, _("Send anyway?"));
	
	mbox = e_message_box_new (str->str,
				  E_MESSAGE_BOX_QUESTION,
				  GNOME_STOCK_BUTTON_YES,
				  GNOME_STOCK_BUTTON_NO,
				  NULL);
	
	g_string_free (str, TRUE);
	
	gtk_signal_connect (GTK_OBJECT (mbox), "destroy",
			    msgbox_destroyed, &show_again);
	
	button = gnome_dialog_run_and_close (GNOME_DIALOG (mbox));
	
	if (!show_again) {
		mail_config_set_confirm_unwanted_html (show_again);
		g_message ("don't show HTML warning again");
	}
	
	if (button == 0)
		return TRUE;
	else
		return FALSE;
}

static gboolean
ask_confirm_for_empty_subject (EMsgComposer *composer)
{
	/* FIXME: EMessageBox should really handle this stuff
           automagically. What Miguel thinks would be nice is to pass
           in a unique id which could be used as a key in the config
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
ask_confirm_for_only_bcc (EMsgComposer *composer, gboolean hidden_list_case)
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
	const gchar *first_text;
	gchar *message_text;
	
	if (!mail_config_get_prompt_only_bcc ())
		return TRUE;

	/* If the user is mailing a hidden contact list, it is possible for
	   them to create a message with only Bcc recipients without really
	   realizing it.  To try to avoid being totally confusing, I've changed
	   this dialog to provide slightly different text in that case, to
	   better explain what the hell is going on. */

	if (hidden_list_case) {
		first_text =  _("Since the contact list you are sending to "
				"is configured to hide the list's addresses, "
				"this message will contain only Bcc recipients.");
	} else {
		first_text = _("This message contains only Bcc recipients.");
	}

	message_text = g_strdup_printf ("%s\n%s", first_text,
					_("It is possible that the mail server may reveal the recipients "
					  "by adding an Apparently-To header.\nSend anyway?"));
	
	mbox = e_message_box_new (message_text, 
				  E_MESSAGE_BOX_QUESTION,
				  GNOME_STOCK_BUTTON_YES,
				  GNOME_STOCK_BUTTON_NO,
				  NULL);
	
	gtk_signal_connect (GTK_OBJECT (mbox), "destroy",
			    msgbox_destroyed, &show_again);
	
	button = gnome_dialog_run_and_close (GNOME_DIALOG (mbox));
	
	mail_config_set_prompt_only_bcc (show_again);

	g_free (message_text);
	
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
composer_sent_cb (char *uri, CamelMimeMessage *message, gboolean sent, void *data)
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
	EDestination **recipients;
	
	message = e_msg_composer_get_message (composer);
	if (message == NULL)
		return NULL;

	recipients = e_msg_composer_get_recipients (composer);

	/* Check for invalid recipients */
	if (recipients) {
		gboolean have_invalid = FALSE;
		gchar *msg, *new_msg;
		GtkWidget *message_box;
		
		for (i = 0; recipients[i] && !have_invalid; ++i) {
			if (!e_destination_is_valid (recipients[i]))
				have_invalid = TRUE;
		}
		
		if (have_invalid) {
			msg = g_strdup (_("This message contains invalid recipients:"));
			for (i = 0; recipients[i]; ++i) {
				if (!e_destination_is_valid (recipients[i])) {
					new_msg = g_strdup_printf ("%s\n    %s", msg,
								   e_destination_get_address (recipients[i]));
					g_free (msg);
					msg = new_msg;
				}
			}
			
			new_msg = e_utf8_from_locale_string (msg);
			g_free (msg);
			msg = new_msg;
			
			message_box = gnome_message_box_new (msg, GNOME_MESSAGE_BOX_WARNING, GNOME_STOCK_BUTTON_OK, NULL);
			g_free (msg);
			
			gnome_dialog_run_and_close (GNOME_DIALOG (message_box));
			
			camel_object_unref (CAMEL_OBJECT (message));
			message = NULL;
			goto finished;
		}
	}
	
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
		message = NULL;
		goto finished;
	}
	
	if (iaddr && num_addrs == camel_address_length (CAMEL_ADDRESS (iaddr))) {
		/* this means that the only recipients are Bcc's */

		/* OK, this is an abusive hack.  If someone sends a mail with a
		   hidden contact list on to to: line and no other recipients,
		   they will unknowingly create a message with only bcc: recipients.
		   We try to detect this and pass a flag to ask_confirm_for_only_bcc,
		   so that it can present the user with a dialog whose text has been
		   modified to reflect this situation. */

		const gchar *to_header = camel_medium_get_header (CAMEL_MEDIUM (message), CAMEL_RECIPIENT_TYPE_TO);
		gboolean hidden_list_case = FALSE;

		if (to_header && !strcmp (to_header, "Undisclosed-Recipient:;"))
			hidden_list_case = TRUE;

		if (!ask_confirm_for_only_bcc (composer, hidden_list_case)) {
			camel_object_unref (CAMEL_OBJECT (message));
			message = NULL;
			goto finished;
		}
	}

	/* Only show this warning if our default is to send html.  If it isn't, we've
	   manually switched into html mode in the composer and (presumably) had a good
	   reason for doing this. */
	if (e_msg_composer_get_send_html (composer)
	    && mail_config_get_send_html ()
	    && mail_config_get_confirm_unwanted_html ()) {
		gboolean html_problem = FALSE;
		for (i = 0; recipients[i] != NULL && !html_problem; ++i) {
			if (! e_destination_get_html_mail_pref (recipients[i]))
				html_problem = TRUE;
		}
		
		if (html_problem) {
			html_problem = ! ask_confirm_for_unwanted_html_mail (composer, recipients);
			if (html_problem) {
				camel_object_unref (CAMEL_OBJECT (message));
				message = NULL;
				goto finished;
			}
		}
	}
	
	/* Check for no subject */
	subject = camel_mime_message_get_subject (message);
	if (subject == NULL || subject[0] == '\0') {
		if (!ask_confirm_for_empty_subject (composer)) {
			camel_object_unref (CAMEL_OBJECT (message));
			message = NULL;
			goto finished;
		}
	}
	
	/* Add info about the sending account */
	account = e_msg_composer_get_preferred_account (composer);
	if (account) {
		camel_medium_set_header (CAMEL_MEDIUM (message), "X-Evolution-Account", account->name);
		camel_medium_set_header (CAMEL_MEDIUM (message), "X-Evolution-Transport", account->transport->url);
		camel_medium_set_header (CAMEL_MEDIUM (message), "X-Evolution-Fcc", account->sent_folder_uri);
	}

	/* Get the message recipients and 'touch' them, boosting their use scores */
	recipients = e_msg_composer_get_recipients (composer);
	e_destination_touchv (recipients);

 finished:
	e_destination_freev (recipients);
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
	if (!transport)
		return;
	
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
	
	if (psd)
		camel_folder_set_message_flags (psd->folder, psd->uid, psd->flags, psd->flags);
	
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
		e_msg_composer_hdrs_set_from_account (E_MSG_COMPOSER_HDRS (composer->hdrs), account->name);
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
	int i;
	
	for (i = 0; camel_internet_address_get (cia, i, &name, &addr); i++) {
		/* Make sure we don't want to ignore this address */
		if (!ignore_addr || g_strcasecmp (ignore_addr, addr)) {
			
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
				EDestination *dest;
				
				dest = e_destination_new ();
				e_destination_set_name (dest, name);
				e_destination_set_email (dest, addr);
				
				list = g_list_append (list, dest);
				g_hash_table_insert (rcpt_hash, (char *) addr, GINT_TO_POINTER (1));
			} 
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
				
				if (!g_strcasecmp (acnt->id->address, addr)) {
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
				
				if (!g_strcasecmp (acnt->id->address, addr)) {
					notme = FALSE;
					return acnt;
				}
				
				l = l->next;
			}
		}
	}
	
	return NULL;
}

static EMsgComposer *
mail_generate_reply (CamelFolder *folder, CamelMimeMessage *message, const char *uid, int mode)
{
	const CamelInternetAddress *reply_to, *sender, *to_addrs, *cc_addrs;
	const char *name = NULL, *address = NULL, *source = NULL;
	const char *message_id, *references, *reply_addr = NULL;
	char *text, *subject, date_str[100], *format;
	const MailConfigAccount *me = NULL;
	const GSList *accounts = NULL;
	GList *to = NULL, *cc = NULL;
	EDestination **tov, **ccv;
	EMsgComposer *composer;
	time_t date;
	const int max_subject_length = 1024;
	
	composer = e_msg_composer_new ();
	if (!composer)
		return NULL;
	
	sender = camel_mime_message_get_from (message);
	if (sender != NULL && camel_address_length (CAMEL_ADDRESS (sender)) > 0) {
		camel_internet_address_get (sender, 0, &name, &address);
	} else {
		name = _("an unknown sender");
	}
	
	date = camel_mime_message_get_date (message, NULL);
	
	strftime (date_str, sizeof (date_str), _("On %a, %Y-%m-%d at %H:%M, %%s wrote:"),
		  localtime (&date));
	format = e_utf8_from_locale_string (date_str);
	text = mail_tool_quote_message (message, format, name && *name ? name : address);
	g_free (format);
	
	if (text) {
		e_msg_composer_set_body_text (composer, text);
		g_free (text);
	}
	
	/* Set the recipients */
	accounts = mail_config_get_accounts ();
	
	to_addrs = camel_mime_message_get_recipients (message, CAMEL_RECIPIENT_TYPE_TO);
	cc_addrs = camel_mime_message_get_recipients (message, CAMEL_RECIPIENT_TYPE_CC);
	
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
				camel_internet_address_get (to_addrs, i, &name, &address);
				if (!g_strncasecmp (address, mlist, len))
					break;
			}
			
			if (i == max) {
				cc_addrs = camel_mime_message_get_recipients (message, CAMEL_RECIPIENT_TYPE_CC);
				max = camel_address_length (CAMEL_ADDRESS (cc_addrs));
				for (i = 0; i < max; i++) {
					camel_internet_address_get (cc_addrs, i, &name, &address);
					if (!g_strncasecmp (address, mlist, len))
						break;
				}
			}
			
			if (address && i != max) {
				EDestination *dest;
				
				dest = e_destination_new ();
				e_destination_set_name (dest, name);
				e_destination_set_email (dest, address);
				
				to = g_list_append (to, dest);
			}
		}
		
		me = guess_me (to_addrs, cc_addrs, accounts);
	} else {
		GHashTable *rcpt_hash;
		
		rcpt_hash = g_hash_table_new (g_str_hash, g_str_equal);
		
		reply_to = camel_mime_message_get_reply_to (message);
		if (!reply_to)
			reply_to = camel_mime_message_get_from (message);
		if (reply_to) {
			/* Get the Reply-To address so we can ignore references to it in the Cc: list */
			if (camel_internet_address_get (reply_to, 0, &name, &reply_addr)) {
				EDestination *dest;
				
				dest = e_destination_new ();
				e_destination_set_name (dest, name);
				e_destination_set_email (dest, reply_addr);
				g_message (">>>>>>>>>> [%s] [%s]", name, reply_addr);
				to = g_list_append (to, dest);
				g_hash_table_insert (rcpt_hash, (char *) reply_addr, GINT_TO_POINTER (1));
			}
		}
		
		if (mode == REPLY_ALL) {
			cc = list_add_addresses (cc, to_addrs, accounts, rcpt_hash, &me, NULL);
			cc = list_add_addresses (cc, cc_addrs, accounts, rcpt_hash, me ? NULL : &me, reply_addr);
		} else {
			me = guess_me (to_addrs, cc_addrs, accounts);
		}
		
		g_hash_table_destroy (rcpt_hash);
	}
	
	if (me == NULL) {
		/* as a last resort, set the replying account (aka me)
		   to the account this was fetched from */
		source = camel_mime_message_get_source (message);
		me = mail_config_get_account_by_source_url (source);
	}
	
	/* Set the subject of the new message. */
	subject = (char *)camel_mime_message_get_subject (message);
	if (!subject)
		subject = g_strdup ("");
	else {
		if (!g_strncasecmp (subject, "Re: ", 4))
			subject = g_strndup (subject, max_subject_length);
		else {
			if (strlen (subject) < max_subject_length)
				subject = g_strdup_printf ("Re: %s", subject);
			else
				subject = g_strdup_printf ("Re: %.*s...", max_subject_length, subject);
		}
	}

	tov = e_destination_list_to_vector (to);
	ccv = e_destination_list_to_vector (cc);
	
	g_list_free (to);
	g_list_free (cc);
	
	e_msg_composer_set_headers (composer, me ? me->name : NULL, tov, ccv, NULL, subject);
	
	e_destination_freev (tov);
	e_destination_freev (ccv);
	
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

static void
requeue_mail_reply (CamelFolder *folder, char *uid, CamelMimeMessage *msg, void *data)
{
	int mode = GPOINTER_TO_INT (data);
	
	mail_reply (folder, msg, uid, mode);
}

void
mail_reply (CamelFolder *folder, CamelMimeMessage *msg, const char *uid, int mode)
{
	EMsgComposer *composer;
	struct post_send_data *psd;
	
	g_return_if_fail (folder != NULL);
	g_return_if_fail (uid != NULL);
	
	if (!msg) {
		mail_get_message (folder, uid, requeue_mail_reply,
				  GINT_TO_POINTER (mode), mail_thread_new);
		return;
	}
	
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
forward_get_composer (CamelMimeMessage *message, const char *subject)
{
	const MailConfigAccount *account = NULL;
	EMsgComposer *composer;
	
	if (message) {
		const CamelInternetAddress *to_addrs, *cc_addrs;
		const GSList *accounts = NULL;
		
		accounts = mail_config_get_accounts ();
		to_addrs = camel_mime_message_get_recipients (message, CAMEL_RECIPIENT_TYPE_TO);
		cc_addrs = camel_mime_message_get_recipients (message, CAMEL_RECIPIENT_TYPE_CC);
		
		account = guess_me (to_addrs, cc_addrs, accounts);
		
		if (!account) {
			const char *source;
			
			source = camel_mime_message_get_source (message);
			account = mail_config_get_account_by_source_url (source);
		}
	}
	
	if (!account)
		account = mail_config_get_default_account ();
	
	composer = e_msg_composer_new ();
	if (composer) {
		gtk_signal_connect (GTK_OBJECT (composer), "send",
				    GTK_SIGNAL_FUNC (composer_send_cb), NULL);
		gtk_signal_connect (GTK_OBJECT (composer), "postpone",
				    GTK_SIGNAL_FUNC (composer_postpone_cb), NULL);
		e_msg_composer_set_headers (composer, account->name, NULL, NULL, NULL, subject);
	} else {
		g_warning ("Could not create composer");
	}
	
	return composer;
}

static void
do_forward_non_attached (CamelFolder *folder, char *uid, CamelMimeMessage *message, void *data)
{
	char *subject, *text;
	MailConfigForwardStyle style = GPOINTER_TO_INT (data);
	
	if (!message)
		return;
	
	subject = mail_tool_generate_forward_subject (message);
	text = mail_tool_forward_message (message, style == MAIL_CONFIG_FORWARD_QUOTED);
	
	if (text) {
		EMsgComposer *composer = forward_get_composer (message, subject);
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
	CamelMimeMessage *message = data;
	
	if (part) {
		EMsgComposer *composer = forward_get_composer (message, subject);
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
	FolderBrowser *fb = (FolderBrowser *)user_data;
	GPtrArray *uids;
	
	if (!check_send_configuration (fb))
		return;
	
	uids = g_ptr_array_new ();
	message_list_foreach (fb->message_list, enumerate_msg, uids);
	mail_build_attachment (fb->message_list->folder, uids, do_forward_attach,
			       uids->len == 1 ? fb->mail_display->current_message : NULL);
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
	const char *allowed_types[] = { "mail", /*"vtrash",*/ NULL };
	extern EvolutionShellClient *global_shell_client;
	static char *last = NULL;
	
	if (last == NULL)
		last = g_strdup ("");
	
	if (delete_from_source)
		desc = _("Move message(s) to");
	else
		desc = _("Copy message(s) to");
	
	evolution_shell_client_user_select_folder (global_shell_client, desc, last,
						   allowed_types, &uri, &physical);
	if (!uri)
		return;
	
	path = strchr (uri, '/');
	if (path && strcmp (last, path) != 0) {
		g_free (last);
		last = g_strdup_printf ("evolution:%s", path);
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
flag_messages (FolderBrowser *fb, guint32 mask, guint32 set)
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
	flag_messages (FOLDER_BROWSER (user_data), CAMEL_MESSAGE_SEEN, CAMEL_MESSAGE_SEEN);
}

void
mark_as_unseen (BonoboUIComponent *uih, void *user_data, const char *path)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);

	/* Remove the automatic mark-as-read timer first */
	if (fb->seen_id) {
		gtk_timeout_remove (fb->seen_id);
		fb->seen_id = 0;
	}

	flag_messages (fb, CAMEL_MESSAGE_SEEN, 0);
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
mark_as_unimportant (BonoboUIComponent *uih, void *user_data, const char *path)
{
	flag_messages (FOLDER_BROWSER (user_data), CAMEL_MESSAGE_FLAGGED, 0);
}

void
toggle_as_important (BonoboUIComponent *uih, void *user_data, const char *path)
{
	toggle_flags (FOLDER_BROWSER (user_data), CAMEL_MESSAGE_FLAGGED);
}

static void
do_edit_messages (CamelFolder *folder, GPtrArray *uids, GPtrArray *messages, void *data)
{
	/*FolderBrowser *fb = data;*/
	int i;
	
	for (i = 0; i < messages->len; i++) {
		EMsgComposer *composer;
		XEvolution *hdrs;
		
		hdrs = mail_tool_remove_xevolution_headers (messages->pdata[i]);
		mail_tool_destroy_xevolution (hdrs);
		camel_medium_remove_header (CAMEL_MEDIUM (messages->pdata[i]), "X-Mailer");
		
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
	const char *path;
	int fd, ret = 0;
	
	path = gtk_file_selection_get_filename (GTK_FILE_SELECTION (user_data));
	if (path[0] == '\0')
		return;
	
	fd = open (path, O_RDONLY);
	if (fd != -1) {
		GtkWidget *dialog;
		GtkWidget *text;
		
		close (fd);
		
		dialog = gnome_dialog_new (_("Overwrite file?"),
					   GNOME_STOCK_BUTTON_YES, 
					   GNOME_STOCK_BUTTON_NO,
					   NULL);
		
		gtk_widget_set_parent (dialog, GTK_WIDGET (user_data));
		text = gtk_label_new (_("A file by that name already exists.\nOverwrite it?"));
		gtk_box_pack_start (GTK_BOX (GNOME_DIALOG (dialog)->vbox), text, TRUE, TRUE, 4);
		gtk_window_set_policy (GTK_WINDOW (dialog), FALSE, TRUE, FALSE);
		gtk_widget_show (text);
		
		ret = gnome_dialog_run_and_close (GNOME_DIALOG (dialog));
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
	
	/* Select the next message if we are only deleting one message */
	if (deleted) {
		row = e_tree_row_of_node (fb->message_list->tree,
					  e_tree_get_cursor (fb->message_list->tree));
		
		/* If this is the last message and deleted messages
                   are hidden, select the previous */
		if ((row+1 == e_tree_row_count (fb->message_list->tree))
		    && mail_config_get_hide_deleted ())
			message_list_select (fb->message_list, row, MESSAGE_LIST_SELECT_PREVIOUS,
					     0, CAMEL_MESSAGE_DELETED, FALSE);
		else
			message_list_select (fb->message_list, row, MESSAGE_LIST_SELECT_NEXT,
					     0, 0, FALSE);
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
	message_list_select (fb->message_list, row, MESSAGE_LIST_SELECT_NEXT, 0, 0, FALSE);
}

void
next_unread_msg (GtkWidget *button, gpointer user_data)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
	int row;
	
	row = e_tree_row_of_node (fb->message_list->tree, e_tree_get_cursor (fb->message_list->tree));
	message_list_select (fb->message_list, row,
			     MESSAGE_LIST_SELECT_NEXT,
			     0, CAMEL_MESSAGE_SEEN, TRUE);
}

void
next_flagged_msg (GtkWidget *button, gpointer user_data)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
	int row;
	
	row = e_tree_row_of_node (fb->message_list->tree, e_tree_get_cursor (fb->message_list->tree));
	message_list_select (fb->message_list, row,
			     MESSAGE_LIST_SELECT_NEXT,
			     CAMEL_MESSAGE_FLAGGED, CAMEL_MESSAGE_FLAGGED, FALSE);
}

void
previous_msg (GtkWidget *button, gpointer user_data)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
	int row;
	
	row = e_tree_row_of_node (fb->message_list->tree, e_tree_get_cursor (fb->message_list->tree));
	message_list_select (fb->message_list, row,
			     MESSAGE_LIST_SELECT_PREVIOUS,
			     0, 0, FALSE);
}

void
previous_unread_msg (GtkWidget *button, gpointer user_data)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
	int row;
	
	row = e_tree_row_of_node (fb->message_list->tree, e_tree_get_cursor (fb->message_list->tree));
	message_list_select (fb->message_list, row,
			     MESSAGE_LIST_SELECT_PREVIOUS,
			     0, CAMEL_MESSAGE_SEEN, FALSE);
}

void
previous_flagged_msg (GtkWidget *button, gpointer user_data)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
	int row;
	
	row = e_tree_row_of_node (fb->message_list->tree, e_tree_get_cursor (fb->message_list->tree));
	message_list_select (fb->message_list, row,
			     MESSAGE_LIST_SELECT_PREVIOUS,
			     CAMEL_MESSAGE_FLAGGED, CAMEL_MESSAGE_FLAGGED, TRUE);
}

struct _expunged_folder_data {
	FolderBrowser *fb;
	gboolean hidedeleted;
};

static void
expunged_folder (CamelFolder *f, void *data)
{
	FolderBrowser *fb = ((struct _expunged_folder_data *) data)->fb;
	gboolean hidedeleted = ((struct _expunged_folder_data *) data)->hidedeleted;
	
	fb->expunging = NULL;
	message_list_set_hidedeleted (fb->message_list, hidedeleted);
	
	g_free (data);
}

static gboolean
confirm_expunge (void)
{
	GtkWidget *dialog, *label, *checkbox;
	int button;
	
	if (!mail_config_get_confirm_expunge ())
		return TRUE;
	
	dialog = gnome_dialog_new (_("Warning"),
				   GNOME_STOCK_BUTTON_YES,
				   GNOME_STOCK_BUTTON_NO,
				   NULL);
	
	label = gtk_label_new (_("This operation will permanently erase all messages marked as deleted. If you continue, you will not be able to recover these messages.\n\nReally erase these messages?"));
	
	gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
	gtk_label_set_justify (GTK_LABEL (label), GTK_JUSTIFY_LEFT);
	gtk_widget_show (label);
	gtk_box_pack_start (GTK_BOX (GNOME_DIALOG (dialog)->vbox), label, TRUE, TRUE, 4);
	
	checkbox = gtk_check_button_new_with_label (_("Do not ask me again."));
	gtk_object_ref (GTK_OBJECT (checkbox));
	gtk_widget_show (checkbox);
	gtk_box_pack_start (GTK_BOX (GNOME_DIALOG (dialog)->vbox), checkbox, TRUE, TRUE, 4);
	
	button = gnome_dialog_run_and_close (GNOME_DIALOG (dialog));
	
	if (button == 0 && gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (checkbox)))
		mail_config_set_confirm_expunge (FALSE);
	
	gtk_object_unref (GTK_OBJECT (checkbox));
	
	if (button == 0)
		return TRUE;
	else
		return FALSE;
}

void
expunge_folder (BonoboUIComponent *uih, void *user_data, const char *path)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
	
	if (fb->folder && (fb->expunging == NULL || fb->folder != fb->expunging) && confirm_expunge ()) {
		struct _expunged_folder_data *data;
		CamelMessageInfo *info;
		
		data = g_malloc (sizeof (*data));
		data->fb = fb;
		data->hidedeleted = fb->message_list->hidedeleted;
		
		/* hide the deleted messages so user can't click on them while we expunge */
		message_list_set_hidedeleted (fb->message_list, TRUE);
		
		/* Only blank the mail display if the message being
                   viewed is one of those to be expunged */
		if (fb->loaded_uid) {
			info = camel_folder_get_message_info (fb->folder, fb->loaded_uid);
			
			if (!info || info->flags & CAMEL_MESSAGE_DELETED)
				mail_display_set_message (fb->mail_display, NULL);
		}
		
		fb->expunging = fb->folder;
		mail_expunge_folder (fb->folder, expunged_folder, data);
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
		gdk_window_raise (GTK_WIDGET (filter_editor)->window);
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
		gdk_window_raise (GTK_WIDGET (dialog)->window);
	}
}

/*
 * FIXME: This routine could be made generic, by having a closure
 * function plus data, and having the whole process be taken care
 * of for you
 */
static void
do_mail_print (FolderBrowser *fb, gboolean preview)
{
	GnomePrintContext *print_context;
	GnomePrintMaster *print_master;
	GnomePrintDialog *dialog;
	GnomePrinter *printer = NULL;
	int copies = 1;
	int collate = FALSE;
	
	if (!preview) {
		dialog = GNOME_PRINT_DIALOG (gnome_print_dialog_new (_("Print Message"),
								     GNOME_PRINT_DIALOG_COPIES));
		gnome_dialog_set_default (GNOME_DIALOG (dialog), GNOME_PRINT_PRINT);
		gnome_dialog_set_parent (GNOME_DIALOG (dialog), GTK_WINDOW (fb));
		
		switch (gnome_dialog_run (GNOME_DIALOG (dialog))) {
		case GNOME_PRINT_PRINT:
			break;	
		case GNOME_PRINT_PREVIEW:
			preview = TRUE;
			break;
		case -1:
			return;
		default:
			gnome_dialog_close (GNOME_DIALOG (dialog));
			return;
		}
		
		gnome_print_dialog_get_copies (dialog, &copies, &collate);
		printer = gnome_print_dialog_get_printer (dialog);
		gnome_dialog_close (GNOME_DIALOG (dialog));
	}
	
	print_master = gnome_print_master_new ();
	
/*	FIXME: set paper size gnome_print_master_set_paper (print_master,  */
	
	if (printer)
		gnome_print_master_set_printer (print_master, printer);
	gnome_print_master_set_copies (print_master, copies, collate);
	print_context = gnome_print_master_get_context (print_master);
	gtk_html_print (fb->mail_display->html, print_context);
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
print_msg (GtkWidget *button, gpointer user_data)
{
	FolderBrowser *fb = user_data;
	
	do_mail_print (fb, FALSE);
}

void
print_preview_msg (GtkWidget *button, gpointer user_data)
{
	FolderBrowser *fb = user_data;
	
	do_mail_print (fb, TRUE);
}

/******************** Begin Subscription Dialog ***************************/

static GtkObject *subscribe_dialog = NULL;

static void
subscribe_dialog_destroy (GtkWidget *widget, gpointer user_data)
{
	subscribe_dialog = NULL;
}

void
manage_subscriptions (BonoboUIComponent *uih, void *user_data, const char *path)
{
	if (!subscribe_dialog) {
		subscribe_dialog = subscribe_dialog_new ();
		gtk_signal_connect (GTK_OBJECT (subscribe_dialog), "destroy",
				    subscribe_dialog_destroy, NULL);
		
		subscribe_dialog_run_and_close (SUBSCRIBE_DIALOG (subscribe_dialog));
		gtk_object_unref (GTK_OBJECT (subscribe_dialog));
	} else {
		gdk_window_raise (SUBSCRIBE_DIALOG (subscribe_dialog)->app->window);
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
	extern CamelFolder *outbox_folder;
	
	if (folder_browser_is_drafts (fb) || fb->folder == outbox_folder)
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
	CamelException ex;
	gboolean async;
	
	/* the only time all three args are NULL is for empty-on-exit */
	async = !(uih == NULL && user_data == NULL && path == NULL);
	
	camel_exception_init (&ex);
	
	/* expunge all remote stores */
	accounts = mail_config_get_accounts ();
	while (accounts) {
		account = accounts->data;
		
		/* make sure this is a valid source */
		if (account->source && account->source->url) {
			provider = camel_session_get_provider (session, account->source->url, &ex);			
			if (provider) {
				/* make sure this store is a remote store */
				if (provider->flags & CAMEL_PROVIDER_IS_STORAGE &&
				    provider->flags & CAMEL_PROVIDER_IS_REMOTE) {
					vtrash = mail_tool_get_trash (account->source->url, &ex);
					
					if (vtrash) {
						if (async)
							mail_expunge_folder (vtrash, empty_trash_expunged_cb, NULL);
						else
							camel_folder_sync (vtrash, TRUE, NULL);
					}
				}
			}
			
			/* clear the exception for the next round */
			camel_exception_clear (&ex);
		}
		accounts = accounts->next;
	}
	
	/* Now empty the local trash folder */
	vtrash = mail_tool_get_trash ("file:/", &ex);
	if (vtrash) {
		if (async)
			mail_expunge_folder (vtrash, empty_trash_expunged_cb, NULL);
		else
			camel_folder_sync (vtrash, TRUE, NULL);
	}
	
	camel_exception_clear (&ex);
}
