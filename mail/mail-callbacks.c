/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* mail-ops.c: callbacks for the mail toolbar/menus */

/* 
 * Author : 
 *  Dan Winship <danw@helixcode.com>
 *
 * Copyright 2000 Helix Code, Inc. (http://www.helixcode.com)
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

#include <config.h>
#include <errno.h>
#include <gnome.h>
#include <libgnomeprint/gnome-print-master.h>
#include <libgnomeprint/gnome-print-master-preview.h>
#include "mail.h"
#include "mail-threads.h"
#include "mail-tools.h"
#include "mail-ops.h"
#include "folder-browser.h"
#include "e-util/e-setup.h"
#include "filter/filter-editor.h"
#include "filter/filter-driver.h"
#include "widgets/e-table/e-table.h"

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
	const char *uid;
	guint32 flags;
};

static gboolean
check_configured (void)
{
	char *path;
	gboolean configured;

	path = g_strdup_printf ("=%s/config=/mail/configured", evolution_dir);
	if (gnome_config_get_bool (path)) {
		g_free (path);
		return TRUE;
	}

	mail_config_druid ();

	configured = gnome_config_get_bool (path);
	g_free (path);
	return configured;
}

static void
select_first_unread (CamelFolder *folder, gpointer event_data, gpointer data)
{
	FolderBrowser *fb = FOLDER_BROWSER (data);

	message_list_select (fb->message_list, 0, MESSAGE_LIST_SELECT_NEXT,
			     0, CAMEL_MESSAGE_SEEN);
}

void
fetch_mail (GtkWidget *button, gpointer user_data)
{
	char *path, *url = NULL;

	if (!check_configured ())
		return;

	path = g_strdup_printf ("=%s/config=/mail/source", evolution_dir);
	url = gnome_config_get_string (path);
	g_free (path);

	if (!url) {
		GtkWidget *win = gtk_widget_get_ancestor (GTK_WIDGET (user_data),
							  GTK_TYPE_WINDOW);

		gnome_error_dialog_parented ("You have no remote mail source "
					     "configured", GTK_WINDOW (win));
		return;
	}

	mail_do_fetch_mail (url, NULL, select_first_unread, user_data);
	g_free (url);
}

static gboolean
ask_confirm_for_empty_subject (EMsgComposer *composer)
{
	GtkWidget *message_box;
	int button;

	message_box = gnome_message_box_new (_("This message has no subject.\nReally send?"),
					     GNOME_MESSAGE_BOX_QUESTION,
					     GNOME_STOCK_BUTTON_YES, GNOME_STOCK_BUTTON_NO,
					     NULL);

	button = gnome_dialog_run_and_close (GNOME_DIALOG (message_box));

	if (button == 0)
		return TRUE;
	else
		return FALSE;
}

static void
composer_send_cb (EMsgComposer *composer, gpointer data)
{
	static CamelInternetAddress *ciaddr = NULL;
	static char *xport_url = NULL;
	CamelMimeMessage *message;
	const char *subject;

	struct post_send_data *psd = data;

	/* Generate our from address if nonexistant */

	if (!ciaddr) {
		char *path;
		char *name;
		char *addr;

		path = g_strdup_printf ("=%s/config=/mail/id_name", evolution_dir);
		name = gnome_config_get_string (path);
		g_assert (name);
		g_free (path);

		path = g_strdup_printf ("=%s/config=/mail/id_addr", evolution_dir);
		addr = gnome_config_get_string (path);
		g_assert (addr);
		g_free (path);

		ciaddr = camel_internet_address_new ();
		camel_internet_address_add (ciaddr, name, addr);

		g_free (name);
		g_free (addr);
	}

	/* Get our transport URL if unspecified */

	if (!xport_url) {
		gchar *path;

		path = g_strdup_printf ("=%s/config=/mail/transport",
					evolution_dir);
		xport_url = gnome_config_get_string (path);
		g_assert (xport_url);
		g_free (path);
	}

	/* Get the message */

	message = e_msg_composer_get_message (composer);

	/* Check for no subject */

	subject = camel_mime_message_get_subject (message);
	if (subject == NULL || subject[0] == '\0') {
		if (! ask_confirm_for_empty_subject (composer)) {
			camel_object_unref (CAMEL_OBJECT (message));
			return;
		}
	}

	if (psd) {
		mail_do_send_mail (xport_url, message, ciaddr,
				   psd->folder, psd->uid, psd->flags, 
				   GTK_WIDGET (composer));
	} else {
		mail_do_send_mail (xport_url, message, ciaddr,
				   NULL, NULL, 0,
				   GTK_WIDGET (composer));
	}
}

static void
free_psd (GtkWidget *composer, gpointer user_data)
{
	struct post_send_data *psd = user_data;

	camel_object_unref (CAMEL_OBJECT (psd->folder));
	g_free (psd);
}

void
compose_msg (GtkWidget *widget, gpointer user_data)
{
	GtkWidget *composer;

	if (!check_configured ())
		return;

	composer = e_msg_composer_new ();

	gtk_signal_connect (GTK_OBJECT (composer), "send",
			    GTK_SIGNAL_FUNC (composer_send_cb), NULL);
	gtk_widget_show (composer);
}

/* Send according to a mailto (RFC 2368) URL. */
void
send_to_url (const char *url)
{
	GtkWidget *composer;

	if (!check_configured ())
		return;

	composer = e_msg_composer_new_from_url (url);

	gtk_signal_connect (GTK_OBJECT (composer), "send",
			    GTK_SIGNAL_FUNC (composer_send_cb), NULL);
	gtk_widget_show (composer);
}	

static void
reply (FolderBrowser *fb, gboolean to_all)
{
	EMsgComposer *composer;
	struct post_send_data *psd;

	if (!check_configured () || !fb->message_list->cursor_uid)
		return;

	psd = g_new (struct post_send_data, 1);
	psd->folder = fb->folder;
	camel_object_ref (CAMEL_OBJECT (psd->folder));
	psd->uid = fb->message_list->cursor_uid;
	psd->flags = CAMEL_MESSAGE_ANSWERED;

	composer = mail_generate_reply (fb->mail_display->current_message, to_all);

	gtk_signal_connect (GTK_OBJECT (composer), "send",
			    GTK_SIGNAL_FUNC (composer_send_cb), psd); 
	gtk_signal_connect (GTK_OBJECT (composer), "destroy",
			    GTK_SIGNAL_FUNC (free_psd), psd); 

	gtk_widget_show (GTK_WIDGET (composer));	
}

void
reply_to_sender (GtkWidget *button, gpointer user_data)
{
	reply (FOLDER_BROWSER (user_data), FALSE);
}

void
reply_to_all (GtkWidget *button, gpointer user_data)
{
	reply (FOLDER_BROWSER (user_data), TRUE);
}

static void
enumerate_msg (MessageList *ml, const char *uid, gpointer data)
{
	g_ptr_array_add ((GPtrArray *) data, g_strdup (uid));
}

void
forward_msg (GtkWidget *button, gpointer user_data)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
	EMsgComposer *composer;
	CamelMimeMessage *cursor_msg;
	GPtrArray *uids;
	
	cursor_msg = fb->mail_display->current_message;
	if (!check_configured () || !cursor_msg)
		return;

	composer = E_MSG_COMPOSER (e_msg_composer_new ());

	uids = g_ptr_array_new();
	message_list_foreach (fb->message_list, enumerate_msg, uids);

	gtk_signal_connect (GTK_OBJECT (composer), "send",
			    GTK_SIGNAL_FUNC (composer_send_cb), NULL);

	mail_do_forward_message (cursor_msg,
				 fb->message_list->folder,
				 uids,
				 composer);
}

void
move_msg (GtkWidget *button, gpointer user_data)
{
	FolderBrowser *fb = user_data;
	MessageList *ml = fb->message_list;
	GPtrArray *uids;
	char *uri, *physical, *path;
	const char *allowed_types[] = { "mail", NULL };
	extern EvolutionShellClient *global_shell_client;
	static char *last;

	if (last == NULL)
		last = g_strdup ("");

	evolution_shell_client_user_select_folder  (global_shell_client,
						    _("Move message(s) to"),
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
	mail_do_refile_messages (ml->folder, uids, physical);
}

void
delete_msg (GtkWidget *button, gpointer user_data)
{
	FolderBrowser *fb = user_data;
	MessageList *ml = fb->message_list;
	GPtrArray *uids;

	uids = g_ptr_array_new ();
	message_list_foreach (ml, enumerate_msg, uids);
	mail_do_flag_messages (ml->folder, uids, 
			       CAMEL_MESSAGE_DELETED, CAMEL_MESSAGE_DELETED);
}

void
mark_all_seen (BonoboUIHandler *uih, void *user_data, const char *path)
{
        FolderBrowser *fb = FOLDER_BROWSER(user_data);
        MessageList *ml = fb->message_list;
        GPtrArray *uids;

        uids = camel_folder_get_uids (ml->folder);
	mail_do_flag_messages (ml->folder, uids, CAMEL_MESSAGE_SEEN,
			       CAMEL_MESSAGE_SEEN);
}

void
expunge_folder (BonoboUIHandler *uih, void *user_data, const char *path)
{
	FolderBrowser *fb = FOLDER_BROWSER(user_data);

	if (fb->message_list->folder)
		mail_do_expunge_folder (fb->message_list->folder);
}

static void
filter_druid_clicked (GtkWidget *w, int button, FolderBrowser *fb)
{
	FilterContext *fc;

	if (button == 0) {
		char *user;

		fc = gtk_object_get_data (GTK_OBJECT (w), "context");
		user = g_strdup_printf ("%s/filters.xml", evolution_dir);
		rule_context_save ((RuleContext *)fc, user);
		g_free (user);
	}
	
	if (button != -1) {
		gnome_dialog_close (GNOME_DIALOG (w));
	}
}

void
filter_edit (BonoboUIHandler *uih, void *user_data, const char *path)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
	FilterContext *fc;
	char *user, *system;
	GtkWidget *w;

	fc = filter_context_new();
	user = g_strdup_printf ("%s/filters.xml", evolution_dir);
	system = g_strdup_printf ("%s/evolution/filtertypes.xml", EVOLUTION_DATADIR);
	rule_context_load ((RuleContext *)fc, system, user);
	g_free (user);
	g_free (system);

	if (((RuleContext *)fc)->error) {
		GtkWidget *dialog;
		gchar *err;

		err = g_strdup_printf (_("Error loading filter information:\n"
					 "%s"), ((RuleContext *)fc)->error);
		dialog = gnome_warning_dialog (err);
		g_free (err);
		
		/* These are necessary because gtk_main, called by
		 * g_d_r_a_c, does a LEAVE/ENTER pair when running
		 * a main loop recursively. I don't know why the threads
		 * lock isn't being held at this point, as we're in a
		 * callback, but I don't ask questions. It works, and
		 * threads are enabled so we know that it works.
		 */
		GDK_THREADS_ENTER();
		gnome_dialog_run_and_close (GNOME_DIALOG (dialog));
		GDK_THREADS_LEAVE();
		return;
	}

	w = filter_editor_construct (fc);
	gtk_object_set_data_full (GTK_OBJECT (w), "context", fc, (GtkDestroyNotify)gtk_object_unref);
	gtk_signal_connect (GTK_OBJECT (w), "clicked", filter_druid_clicked, fb);
	gtk_widget_show (GTK_WIDGET (w));
}

void
vfolder_edit_vfolders (BonoboUIHandler *uih, void *user_data, const char *path)
{
        void vfolder_edit(void);

        vfolder_edit();
}

/*
 *void
 *providers_config (BonoboUIHandler *uih, void *user_data, const char *path)
 *{
 *	mail_config();
 *}
 */

void
print_msg (GtkWidget *button, gpointer user_data)
{
	FolderBrowser *fb = user_data;
	GnomePrintMaster *print_master;
	GnomePrintContext *print_context;
	GtkWidget *preview;

	print_master = gnome_print_master_new ();

	print_context = gnome_print_master_get_context (print_master);
	gtk_html_print (fb->mail_display->html, print_context);

	preview = GTK_WIDGET (gnome_print_master_preview_new (
		print_master, "Mail Print Preview"));
	gtk_widget_show (preview);

	gtk_object_unref (GTK_OBJECT (print_master));
}
