/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* mail-ops.c: callbacks for the mail toolbar/menus */

/* 
 * Author : 
 *  Dan Winship <danw@helixcode.com>
 *  Peter Williams <peterw@helixcode.com>
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
#include <gnome.h>
#include "mail.h"
#include "mail-threads.h"
#include "mail-tools.h"
#include "mail-ops-new.h"
#include "e-util/e-setup.h"

/* ** FETCH MAIL ********************************************************** */

typedef struct fetch_mail_input_s {
	gchar *source_url;
	CamelFolder *destination;

	/* If destination is NULL, then we'll guess the
	 * default inbox and put the result, refed, into
	 * destination. Caller must unref it. If on completion,
	 * destination is NULL, then mail was not fetched
	 * (currently happens when source_url is IMAP). */
} fetch_mail_input_t;

static void setup_fetch_mail   (gpointer in_data, gpointer op_data, CamelException *ex);
static void do_fetch_mail      (gpointer in_data, gpointer op_data, CamelException *ex);
static void cleanup_fetch_mail (gpointer in_data, gpointer op_data, CamelException *ex);

static void
setup_fetch_mail (gpointer in_data, gpointer op_data, CamelException *ex)
{
	fetch_mail_input_t *input = (fetch_mail_input_t *) in_data;

	if (!input->source_url) {
		camel_exception_set (ex, CAMEL_EXCEPTION_INVALID_PARAM,
				     "You have no remote mail source configured "
				     "to fetch mail from.");
		return;
	}

	if (input->destination == NULL)
		return;

	if (!CAMEL_IS_FOLDER (input->destination)) {
		camel_exception_set (ex, CAMEL_EXCEPTION_INVALID_PARAM,
				     "Bad folder passed to fetch_mail");
		return;
	}

	camel_object_ref (CAMEL_OBJECT (input->destination));
}

static void
do_fetch_mail (gpointer in_data, gpointer op_data, CamelException *ex)
{
	fetch_mail_input_t *input;
	CamelFolder *search_folder = NULL;

	input = (fetch_mail_input_t *) in_data;

	if (input->destination == NULL) {
		input->destination = mail_tool_get_local_inbox (ex);

		if (input->destination == NULL)
			return;
	}

	search_folder = mail_tool_fetch_mail_into_searchable (input->source_url, ex);

	if (search_folder == NULL) {
		/* This happens with an IMAP source and on error */
		camel_object_unref (CAMEL_OBJECT (input->destination));
		input->destination = NULL;
		return;
	}

	mail_tool_filter_contents_into (search_folder, input->destination, ex);
	camel_object_unref (CAMEL_OBJECT (search_folder));
}

static void cleanup_fetch_mail (gpointer in_data, gpointer op_data, CamelException *ex)
{
	fetch_mail_input_t *input = (fetch_mail_input_t *) in_data;

	g_free (input->source_url);
}

static const mail_operation_spec op_fetch_mail =
{
	"Fetch email",
	"Fetching email",
	0,
	setup_fetch_mail,
	do_fetch_mail,
	cleanup_fetch_mail
};

void mail_do_fetch_mail (const gchar *source_url, CamelFolder *destination)
{
	fetch_mail_input_t *input;

	input = g_new (fetch_mail_input_t, 1);
	input->source_url = g_strdup (source_url);
	input->destination = destination;

	mail_operation_queue (&op_fetch_mail, input, TRUE);
}	

/* ** SEND MAIL *********************************************************** */

typedef struct send_mail_input_s {
	gchar *xport_uri;
	CamelMimeMessage *message;
	CamelInternetAddress *from;

	/* If done_folder != NULL, will add done_flags to
	 * the flags of the message done_uid in done_folder. */

	CamelFolder *done_folder;
	char *done_uid;
	guint32 done_flags;

	GtkWidget *composer;
} send_mail_input_t;

static void setup_send_mail   (gpointer in_data, gpointer op_data, CamelException *ex);
static void do_send_mail      (gpointer in_data, gpointer op_data, CamelException *ex);
static void cleanup_send_mail (gpointer in_data, gpointer op_data, CamelException *ex);

static void 
setup_send_mail (gpointer in_data, gpointer op_data, CamelException *ex)
{
	send_mail_input_t *input = (send_mail_input_t *) in_data;

	if (!input->xport_uri) {
		camel_exception_set (ex, CAMEL_EXCEPTION_INVALID_PARAM,
				     "No transport URI specified for send_mail operation.");
		return;
	}

	if (!CAMEL_IS_MIME_MESSAGE (input->message)) {
		camel_exception_set (ex, CAMEL_EXCEPTION_INVALID_PARAM,
				     "No message specified for send_mail operation.");
		return;
	}

	if (input->from == NULL) {
		camel_exception_set (ex, CAMEL_EXCEPTION_INVALID_PARAM,
				     "No from address specified for send_mail operation.");
		return;
	}

	if (input->done_folder == NULL) {
		camel_object_ref (CAMEL_OBJECT (input->message));
		return;
	}

	if (!CAMEL_IS_FOLDER (input->done_folder)) {
		camel_exception_set (ex, CAMEL_EXCEPTION_INVALID_PARAM,
				     "Bad done_folder specified for send_mail operation.");
		return;
	}

	if (input->done_uid == NULL) {
		camel_exception_set (ex, CAMEL_EXCEPTION_INVALID_PARAM,
				     "No done_uid specified for send_mail operation.");
		return;
	}

	if (!GTK_IS_WIDGET (input->composer)) {
		camel_exception_set (ex, CAMEL_EXCEPTION_INVALID_PARAM,
				     "No composer specified for send_mail operation.");
		return;
	}

	camel_object_ref (CAMEL_OBJECT (input->message));
	camel_object_ref (CAMEL_OBJECT (input->done_folder));
	gtk_object_ref (GTK_OBJECT (input->composer));
	gtk_widget_hide (GTK_WIDGET (input->composer));
}

static void
do_send_mail (gpointer in_data, gpointer op_data, CamelException *ex)
{
	send_mail_input_t *input = (send_mail_input_t *) in_data;
	CamelTransport *xport;
	char *from_str;

	mail_tool_camel_lock_up();
	from_str = camel_address_encode (CAMEL_ADDRESS (input->from));
	camel_mime_message_set_from (input->message, from_str);
	g_free (from_str);

	camel_medium_add_header (CAMEL_MEDIUM (input->message), "X-Mailer",
				 "Evolution (Developer Preview)");
	camel_mime_message_set_date (input->message, CAMEL_MESSAGE_DATE_CURRENT, 0);

	xport = camel_session_get_transport (session, input->xport_uri, ex);
	mail_tool_camel_lock_down();
	if (camel_exception_is_set (ex))
		return;

	mail_tool_send_via_transport (xport, CAMEL_MEDIUM (input->message), ex);

	if (camel_exception_is_set (ex))
		return;

	if (input->done_folder) {
		guint32 set;

		mail_tool_camel_lock_up();
		set = camel_folder_get_message_flags (input->done_folder,
						      input->done_uid);
		camel_folder_set_message_flags (input->done_folder, input->done_uid,
						input->done_flags, ~set);
		mail_tool_camel_lock_down();
	}
}

static void 
cleanup_send_mail (gpointer in_data, gpointer op_data, CamelException *ex)
{
	send_mail_input_t *input = (send_mail_input_t *) in_data;

	camel_object_unref (CAMEL_OBJECT (input->message));
	if (input->done_folder)
		camel_object_unref (CAMEL_OBJECT (input->done_folder));

	g_free (input->xport_uri);
	g_free (input->done_uid);

	if (!camel_exception_is_set (ex))
		gtk_widget_destroy (input->composer);
	else
		gtk_widget_show (input->composer);
}

static const mail_operation_spec op_send_mail =
{
	"Send a message",
	"Sending a message",
	0,
	setup_send_mail,
	do_send_mail,
	cleanup_send_mail
};

void mail_do_send_mail (const char *xport_uri,
			CamelMimeMessage *message,
			CamelInternetAddress *from,
			CamelFolder *done_folder,
			const char *done_uid,
			guint32 done_flags,
			GtkWidget *composer)
{
	send_mail_input_t *input;

	input = g_new (send_mail_input_t, 1);
	input->xport_uri = g_strdup (xport_uri);
	input->message = message;
	input->from = from;
	input->done_folder = done_folder;
	input->done_uid = g_strdup (done_uid);
	input->done_flags = done_flags;
	input->composer = composer;

	mail_operation_queue (&op_send_mail, input, TRUE);
}

/* ** EXPUNGE ************************************************************* */

static void setup_expunge_folder   (gpointer in_data, gpointer op_data, CamelException *ex);
static void do_expunge_folder      (gpointer in_data, gpointer op_data, CamelException *ex);
static void cleanup_expunge_folder (gpointer in_data, gpointer op_data, CamelException *ex);

static void setup_expunge_folder (gpointer in_data, gpointer op_data, CamelException *ex)
{
	if (!CAMEL_IS_FOLDER (in_data)) {
		camel_exception_set (ex, CAMEL_EXCEPTION_INVALID_PARAM,
				     "No folder is selected to be expunged");
		return;
	}

	camel_object_ref (CAMEL_OBJECT (in_data));
}

static void do_expunge_folder (gpointer in_data, gpointer op_data, CamelException *ex)
{
	mail_tool_camel_lock_up();
	camel_folder_expunge (CAMEL_FOLDER (in_data), ex);
	mail_tool_camel_lock_down();
}

static void cleanup_expunge_folder (gpointer in_data, gpointer op_data, CamelException *ex)
{
	camel_object_unref (CAMEL_OBJECT (in_data));
}	

static const mail_operation_spec op_expunge_folder =
{
	"Expunge a folder",
	"Expunging a folder",
	0,
	setup_expunge_folder,
	do_expunge_folder,
	cleanup_expunge_folder
};

void mail_do_expunge_folder (CamelFolder *folder)
{
	mail_operation_queue (&op_expunge_folder, folder, FALSE);
}

/* ** REFILE MESSAGES ***************************************************** */

typedef struct refile_messages_input_s {
	CamelFolder *source;
	GPtrArray *uids;
	gchar *dest_uri;
} refile_messages_input_t;

static void setup_refile_messages   (gpointer in_data, gpointer op_data, CamelException *ex);
static void do_refile_messages      (gpointer in_data, gpointer op_data, CamelException *ex);
static void cleanup_refile_messages (gpointer in_data, gpointer op_data, CamelException *ex);

static void setup_refile_messages (gpointer in_data, gpointer op_data, CamelException *ex)
{
	refile_messages_input_t *input = (refile_messages_input_t *) in_data;

	if (!CAMEL_IS_FOLDER (input->source)) {
		camel_exception_set (ex, CAMEL_EXCEPTION_INVALID_PARAM,
				     "No source folder to refile messages from specified.");
		return;
	}

	if (input->uids == NULL) {
		camel_exception_set (ex, CAMEL_EXCEPTION_INVALID_PARAM,
				     "No messages to refile have been specified.");
		return;
	}

	if (input->dest_uri == NULL) {
		camel_exception_set (ex, CAMEL_EXCEPTION_INVALID_PARAM,
				     "No URI to refile to has been specified.");
		return;
	}

	camel_object_ref (CAMEL_OBJECT (input->source));
}

static void do_refile_messages (gpointer in_data, gpointer op_data, CamelException *ex)
{
	refile_messages_input_t *input = (refile_messages_input_t *) in_data;
	CamelFolder *dest;
	gint i;

	dest = mail_tool_uri_to_folder (input->dest_uri, ex);
	if (camel_exception_is_set (ex))
		return;

	mail_tool_camel_lock_up();
	for (i = 0; i < input->uids->len; i++) {
		camel_folder_move_message_to (input->source, input->uids->pdata[i],
					      dest, ex);
		g_free (input->uids->pdata[i]);
		if (camel_exception_is_set (ex))
			break;
	}

	camel_object_unref (CAMEL_OBJECT (dest));
	mail_tool_camel_lock_down();
}

static void cleanup_refile_messages (gpointer in_data, gpointer op_data, CamelException *ex)
{
	refile_messages_input_t *input = (refile_messages_input_t *) in_data;

	camel_object_unref (CAMEL_OBJECT (input->source));
	g_free (input->dest_uri);
	g_ptr_array_free (input->uids, TRUE);
}

static const mail_operation_spec op_refile_messages =
{
	"Refile messages",
	"Refiling messages",
	0,
	setup_refile_messages,
	do_refile_messages,
	cleanup_refile_messages
};

void mail_do_refile_messages (CamelFolder *source, GPtrArray *uids, gchar *dest_uri)
{
	refile_messages_input_t *input;

	input = g_new (refile_messages_input_t, 1);
	input->source = source;
	input->uids = uids;
	input->dest_uri = g_strdup (dest_uri);
	
	mail_operation_queue (&op_refile_messages, input, TRUE);
}

/* ** FLAG MESSAGES ******************************************************* */

typedef struct flag_messages_input_s {
	CamelFolder *source;
	GPtrArray *uids;
	guint32 flags;
} flag_messages_input_t;

static void setup_flag_messages   (gpointer in_data, gpointer op_data, CamelException *ex);
static void do_flag_messages      (gpointer in_data, gpointer op_data, CamelException *ex);
static void cleanup_flag_messages (gpointer in_data, gpointer op_data, CamelException *ex);

static void setup_flag_messages (gpointer in_data, gpointer op_data, CamelException *ex)
{
	flag_messages_input_t *input = (flag_messages_input_t *) in_data;

	if (!CAMEL_IS_FOLDER (input->source)) {
		camel_exception_set (ex, CAMEL_EXCEPTION_INVALID_PARAM,
				     "No source folder to flag messages from specified.");
		return;
	}

	if (input->uids == NULL) {
		camel_exception_set (ex, CAMEL_EXCEPTION_INVALID_PARAM,
				     "No messages to flag have been specified.");
		return;
	}

	camel_object_ref (CAMEL_OBJECT (input->source));
}

static void do_flag_messages (gpointer in_data, gpointer op_data, CamelException *ex)
{
	flag_messages_input_t *input = (flag_messages_input_t *) in_data;
	gint i;

	for (i = 0; i < input->uids->len; i++) {
		mail_tool_set_uid_flags (input->source, input->uids->pdata[i], input->flags);
		g_free (input->uids->pdata[i]);
	}
}

static void cleanup_flag_messages (gpointer in_data, gpointer op_data, CamelException *ex)
{
	flag_messages_input_t *input = (flag_messages_input_t *) in_data;

	camel_object_unref (CAMEL_OBJECT (input->source));
	g_ptr_array_free (input->uids, TRUE);
}

static const mail_operation_spec op_flag_messages =
{
	"Flag messages",
	"Flagging messages",
	0,
	setup_flag_messages,
	do_flag_messages,
	cleanup_flag_messages
};

void mail_do_flag_messages (CamelFolder *source, GPtrArray *uids, guint32 flags)
{
	flag_messages_input_t *input;

	input = g_new (flag_messages_input_t, 1);
	input->source = source;
	input->uids = uids;
	input->flags = flags;
	
	mail_operation_queue (&op_flag_messages, input, TRUE);
}

/* ** SCAN SUBFOLDERS ***************************************************** */

typedef struct scan_subfolders_input_s {
	gchar *source_uri;
	gboolean add_INBOX;
	EvolutionStorage *storage;
} scan_subfolders_input_t;

typedef struct scan_subfolders_folderinfo_s {
	char *path;
	char *uri;
} scan_subfolders_folderinfo_t;

typedef struct scan_subfolders_op_s {
	GPtrArray *new_folders;
} scan_subfolders_op_t;

static void setup_scan_subfolders   (gpointer in_data, gpointer op_data, CamelException *ex);
static void do_scan_subfolders      (gpointer in_data, gpointer op_data, CamelException *ex);
static void cleanup_scan_subfolders (gpointer in_data, gpointer op_data, CamelException *ex);

static void setup_scan_subfolders (gpointer in_data, gpointer op_data, CamelException *ex)
{
	scan_subfolders_input_t *input = (scan_subfolders_input_t *) in_data;
	scan_subfolders_op_t *data = (scan_subfolders_op_t *) op_data;

	if (!input->source_uri) {
		camel_exception_set (ex, CAMEL_EXCEPTION_INVALID_PARAM, 
				     "No source uri to scan subfolders from was provided.");
		return;
	}

	if (!EVOLUTION_IS_STORAGE(input->storage)) {
		camel_exception_set (ex, CAMEL_EXCEPTION_INVALID_PARAM, 
				     "No storage to scan subfolders into was provided.");
		return;
	}

	camel_object_ref (CAMEL_OBJECT (input->storage));
	data->new_folders = g_ptr_array_new ();
}

static void do_scan_subfolders (gpointer in_data, gpointer op_data, CamelException *ex)
{
	scan_subfolders_input_t *input = (scan_subfolders_input_t *) in_data;
	scan_subfolders_op_t *data = (scan_subfolders_op_t *) op_data;

	scan_subfolders_folderinfo_t *info;
	GPtrArray *lsub;
	CamelFolder *folder;
	int i;
	char *splice;

	if (input->source_uri[strlen (input->source_uri) - 1] == '/')
		splice = "";
	else
		splice = "/";

	folder = mail_tool_get_root_of_store (input->source_uri, ex);
	if (camel_exception_is_set (ex))
		return;

	mail_tool_camel_lock_up();

	/* we need a way to set the namespace */
	lsub = camel_folder_get_subfolder_names (folder);

	mail_tool_camel_lock_down();

	if (input->add_INBOX) {
		info = g_new (scan_subfolders_folderinfo_t, 1);
		info->path = g_strdup ("/INBOX");
		info->uri = g_strdup_printf ("%s%sINBOX", input->source_uri, splice);
		g_ptr_array_add (data->new_folders, info);
	}

	for (i = 0; i < lsub->len; i++) {
		info = g_new (scan_subfolders_folderinfo_t, 1);
		info->path = g_strdup_printf ("/%s", (char *)lsub->pdata[i]);
		info->uri = g_strdup_printf ("%s%s%s", input->source_uri, splice, info->path);
		g_ptr_array_add (data->new_folders, info);
	}
	
	camel_folder_free_subfolder_names (folder, lsub);
	camel_object_unref (CAMEL_OBJECT (folder));
}

static void cleanup_scan_subfolders (gpointer in_data, gpointer op_data, CamelException *ex)
{
	scan_subfolders_input_t *input = (scan_subfolders_input_t *) in_data;
	scan_subfolders_op_t *data = (scan_subfolders_op_t *) op_data;

	int i;

	for (i = 0; i < data->new_folders->len; i++) {
		scan_subfolders_folderinfo_t *info;

		info = data->new_folders->pdata[i];
		evolution_storage_new_folder (input->storage,
					      info->path,
					      "mail",
					      info->uri,
					      "(No description)");
		g_free (info->path);
		g_free (info->uri);
		g_free (info);
	}

	g_ptr_array_free (data->new_folders, TRUE);
	g_free (input->source_uri);
}

static const mail_operation_spec op_scan_subfolders =
{
	"Scan a folder tree",
	"Scanning a folder tree",
	sizeof (scan_subfolders_op_t),
	setup_scan_subfolders,
	do_scan_subfolders,
	cleanup_scan_subfolders
};

void mail_do_scan_subfolders (const gchar *source_uri, gboolean add_INBOX, EvolutionStorage *storage)
{
	scan_subfolders_input_t *input;

	input = g_new (scan_subfolders_input_t, 1);
	input->source_uri = g_strdup (source_uri);
	input->add_INBOX = add_INBOX;
	input->storage = storage;
	
	mail_operation_queue (&op_scan_subfolders, input, TRUE);
}

/* ** ATTACH MESSAGE ****************************************************** */

typedef struct attach_message_input_s {
	EMsgComposer *composer;
	CamelFolder *folder;
	gchar *uid;
} attach_message_input_t;

typedef struct attach_message_data_s {
	CamelMimePart *part;
} attach_message_data_t;

static void setup_attach_message   (gpointer in_data, gpointer op_data, CamelException *ex);
static void do_attach_message      (gpointer in_data, gpointer op_data, CamelException *ex);
static void cleanup_attach_message (gpointer in_data, gpointer op_data, CamelException *ex);

static void setup_attach_message (gpointer in_data, gpointer op_data, CamelException *ex)
{
	attach_message_input_t *input = (attach_message_input_t *) in_data;

	if (!input->uid) {
		camel_exception_set (ex, CAMEL_EXCEPTION_INVALID_PARAM,
				     "No UID specified to attach.");
		return;
	}

	if (!CAMEL_IS_FOLDER(input->folder)) {
		camel_exception_set (ex, CAMEL_EXCEPTION_INVALID_PARAM,
				     "No folder to fetch the message from specified.");
		return;
	}

	if (!E_IS_MSG_COMPOSER(input->composer)) {
		camel_exception_set (ex, CAMEL_EXCEPTION_INVALID_PARAM,
				     "No message composer from specified.");
		return;
	}

	camel_object_ref (CAMEL_OBJECT (input->folder));
	gtk_object_ref (GTK_OBJECT (input->composer));
}

static void do_attach_message (gpointer in_data, gpointer op_data, CamelException *ex)
{
	attach_message_input_t *input = (attach_message_input_t *) in_data;
	attach_message_data_t *data = (attach_message_data_t *) op_data;

	CamelMimeMessage *message;
	CamelMimePart *part;
	
	mail_tool_camel_lock_up();
	message = camel_folder_get_message (input->folder, input->uid, ex);
	if (!message) {
		mail_tool_camel_lock_down();
		return;
	}

	part = mail_tool_make_message_attachment (message);
	camel_object_unref (CAMEL_OBJECT (message));
	mail_tool_camel_lock_down();
	if (!part)
		return;

	data->part = part;
}

static void cleanup_attach_message (gpointer in_data, gpointer op_data, CamelException *ex)
{
	attach_message_input_t *input = (attach_message_input_t *) in_data;
	attach_message_data_t *data = (attach_message_data_t *) op_data;

	e_msg_composer_attach (input->composer, data->part);
	camel_object_unref (CAMEL_OBJECT (data->part));
	camel_object_unref (CAMEL_OBJECT (input->folder));
	gtk_object_unref (GTK_OBJECT (input->composer));
	g_free (input->uid);
}

static const mail_operation_spec op_attach_message =
{
	"Attach a message",
	"Attaching a message",
	0,
	setup_attach_message,
	do_attach_message,
	cleanup_attach_message
};

void mail_do_attach_message (CamelFolder *folder, const char *uid, EMsgComposer *composer)
{
	attach_message_input_t *input;

	input = g_new (attach_message_input_t, 1);
	input->folder = folder;
	input->uid = g_strdup (uid);
	input->composer = composer;

	mail_operation_queue (&op_attach_message, input, TRUE);
}

/* ** FORWARD MESSAGES **************************************************** */

typedef struct forward_messages_input_s {
	CamelMimeMessage *basis;
	CamelFolder *source;
	GPtrArray *uids;
	EMsgComposer *composer;
} forward_messages_input_t;

typedef struct forward_messages_data_s {
	gchar *subject;
	GPtrArray *parts;
} forward_messages_data_t;

static void setup_forward_messages   (gpointer in_data, gpointer op_data, CamelException *ex);
static void do_forward_messages      (gpointer in_data, gpointer op_data, CamelException *ex);
static void cleanup_forward_messages (gpointer in_data, gpointer op_data, CamelException *ex);

static void setup_forward_messages (gpointer in_data, gpointer op_data, CamelException *ex)
{
	forward_messages_input_t *input = (forward_messages_input_t *) in_data;

	if (!input->uids) {
		camel_exception_set (ex, CAMEL_EXCEPTION_INVALID_PARAM,
				     "No UIDs specified to attach.");
		return;
	}

	if (!CAMEL_IS_MIME_MESSAGE(input->basis)) {
		camel_exception_set (ex, CAMEL_EXCEPTION_INVALID_PARAM,
				     "No basic message to forward was specified.");
		return;
	}

	if (!CAMEL_IS_FOLDER(input->source)) {
		camel_exception_set (ex, CAMEL_EXCEPTION_INVALID_PARAM,
				     "No folder to fetch the messages from specified.");
		return;
	}

	if (!E_IS_MSG_COMPOSER(input->composer)) {
		camel_exception_set (ex, CAMEL_EXCEPTION_INVALID_PARAM,
				     "No message composer from specified.");
		return;
	}

	camel_object_ref (CAMEL_OBJECT (input->basis));
	camel_object_ref (CAMEL_OBJECT (input->source));
	gtk_object_ref (GTK_OBJECT (input->composer));
}

static void do_forward_messages (gpointer in_data, gpointer op_data, CamelException *ex)
{
	forward_messages_input_t *input = (forward_messages_input_t *) in_data;
	forward_messages_data_t *data = (forward_messages_data_t *) op_data;

	CamelMimeMessage *message;
	CamelMimePart *part;
	int i;

	data->parts = g_ptr_array_new ();

	mail_tool_camel_lock_up();
	for (i = 0; i < input->uids->len; i++) {
		message = camel_folder_get_message (input->source, input->uids->pdata[i], ex);
		g_free (input->uids->pdata[i]);
		if (!message) {
			mail_tool_camel_lock_down();
			return;
		}
		part = mail_tool_make_message_attachment (message);
		if (!part) {
			camel_exception_set (ex, CAMEL_EXCEPTION_SYSTEM,
					     "Failed to generate mime part from "
					     "message while generating forwarded message.");
			mail_tool_camel_lock_down();
			return;
		}
		camel_object_unref (CAMEL_OBJECT (message));
		g_ptr_array_add (data->parts, part);
	}

	mail_tool_camel_lock_down();

	data->subject = mail_tool_generate_forward_subject (input->basis);
}

static void cleanup_forward_messages (gpointer in_data, gpointer op_data, CamelException *ex)
{
	forward_messages_input_t *input = (forward_messages_input_t *) in_data;
	forward_messages_data_t *data = (forward_messages_data_t *) op_data;

	int i;

	for (i = 0; i < data->parts->len; i++) {
		e_msg_composer_attach (input->composer, data->parts->pdata[i]);
		camel_object_unref (CAMEL_OBJECT (data->parts->pdata[i]));
	}
	camel_object_unref (CAMEL_OBJECT (input->source));

	e_msg_composer_set_headers (input->composer, NULL, NULL, NULL, data->subject);

	gtk_object_unref (GTK_OBJECT (input->composer));
	g_free (data->subject);
	g_ptr_array_free (data->parts, TRUE);
	g_ptr_array_free (input->uids, TRUE);
	gtk_widget_show (GTK_WIDGET (input->composer));
}

static const mail_operation_spec op_forward_messages =
{
	"Forward messages",
	"Forwarding messages",
	sizeof (forward_messages_data_t),
	setup_forward_messages,
	do_forward_messages,
	cleanup_forward_messages
};

void mail_do_forward_message (CamelMimeMessage *basis, 
			      CamelFolder *source, 
			      GPtrArray *uids,
			      EMsgComposer *composer)
{
	forward_messages_input_t *input;

	input = g_new (forward_messages_input_t, 1);
	input->basis = basis;
	input->source = source;
	input->uids = uids;
	input->composer = composer;

	mail_operation_queue (&op_forward_messages, input, TRUE);
}
