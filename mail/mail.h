/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Copyright 2000, Helix Code, Inc. (http://www.helixcode.com)
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
 */

/* This file is a F*CKING MESS.  Shame to us!  */

#include <gtkhtml/gtkhtml.h>
#include <libgnomevfs/gnome-vfs-mime-handlers.h>
#include "camel/camel.h"
#include "composer/e-msg-composer.h"
#include "mail-config.h"
#include "mail-config-gui.h"
#include "folder-browser.h"

extern char *evolution_dir;

/* mail-crypto */
char *mail_crypto_openpgp_decrypt (const char *ciphertext,
				   CamelException *ex);

char *mail_crypto_openpgp_encrypt (const char *plaintext,
				   const GPtrArray *recipients,
				   gboolean sign,
				   CamelException *ex);

char *mail_crypto_openpgp_clearsign (const char *plaintext,
				     const char *userid,
				     CamelException *ex);

/* mail-format */
void mail_format_mime_message (CamelMimeMessage *mime_message,
			       MailDisplay *md);

typedef gboolean (*MailMimeHandlerFn) (CamelMimePart *part,
				       const char *mime_type,
				       MailDisplay *md);
typedef struct {
	gboolean generic;
	OAF_ServerInfo *component;
	GnomeVFSMimeApplication *application;
	MailMimeHandlerFn builtin;
} MailMimeHandler;
MailMimeHandler *mail_lookup_handler (const char *mime_type);

gboolean mail_part_is_inline (CamelMimePart *part);

EMsgComposer *mail_generate_reply (CamelMimeMessage *mime_message,
				   gboolean to_all);

char *mail_get_message_body (CamelDataWrapper *data, gboolean want_plain,
			     gboolean *is_html);

/* mail-identify */
char *mail_identify_mime_part (CamelMimePart *part);

/* mail-callbacks */
void fetch_mail (GtkWidget *widget, gpointer user_data);
void send_queued_mail (GtkWidget *widget, gpointer user_data);
void send_receieve_mail (GtkWidget *widget, gpointer user_data);

void compose_msg (GtkWidget *widget, gpointer user_data);
void send_to_url (const char *url);
void forward_msg (GtkWidget *widget, gpointer user_data);
void reply_to_sender (GtkWidget *widget, gpointer user_data);
void reply_to_all (GtkWidget *widget, gpointer user_data);
void delete_msg (GtkWidget *widget, gpointer user_data);
void move_msg (GtkWidget *widget, gpointer user_data);
void copy_msg (GtkWidget *widget, gpointer user_data);
void print_msg (GtkWidget *widget, gpointer user_data);
void edit_msg (GtkWidget *widget, gpointer user_data);
void view_msg (GtkWidget *widget, gpointer user_data);

void mark_all_seen (BonoboUIHandler *uih, void *user_data, const char *path);
void mark_all_deleted (BonoboUIHandler *uih, void *user_data, const char *path);
void edit_message (BonoboUIHandler *uih, void *user_data, const char *path);
void view_message (BonoboUIHandler *uih, void *user_data, const char *path);
void expunge_folder (BonoboUIHandler *uih, void *user_data, const char *path);
void filter_edit (BonoboUIHandler *uih, void *user_data, const char *path);
void vfolder_edit_vfolders (BonoboUIHandler *uih, void *user_data, const char *path);
void providers_config (BonoboUIHandler *uih, void *user_data, const char *path);

void configure_folder(BonoboUIHandler *uih, void *user_data, const char *path);

void mail_reply (CamelFolder *folder, CamelMimeMessage *msg, const char *uid, gboolean to_all);
void composer_send_cb (EMsgComposer *composer, gpointer data);
void composer_postpone_cb (EMsgComposer *composer, gpointer data);
void mail_print_msg (MailDisplay *md);

void run_filter_ondemand (BonoboUIHandler *uih, gpointer user_data, const char *path);

/* mail view */
GtkWidget *mail_view_create (CamelFolder *source, const char *uid, CamelMimeMessage *msg);

/* component factory for lack of a better place */
/*takes a GSList of MailConfigServices */
void mail_load_storages (Evolution_Shell corba_shell, GSList *sources);
void mail_add_new_storage (const char *uri, Evolution_Shell corba_shell, CamelException *ex);

/* session */
void session_init (void);
char *mail_request_dialog (const char *prompt, gboolean secret,
			   const char *key, gboolean async);
void forget_passwords (BonoboUIHandler *uih, void *user_data,
		       const char *path);
extern CamelSession *session;

/* message-list */
void vfolder_subject(GtkWidget *w, FolderBrowser *fb);
void vfolder_sender(GtkWidget *w, FolderBrowser *fb);
void vfolder_recipient(GtkWidget *w, FolderBrowser *fb);
void filter_subject(GtkWidget *w, FolderBrowser *fb);
void filter_sender(GtkWidget *w, FolderBrowser *fb);
void filter_recipient(GtkWidget *w, FolderBrowser *fb);
void filter_mlist(GtkWidget *w, FolderBrowser *fb);
