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

#ifndef HAVE_MKSTEMP
#include <fcntl.h>
#include <sys/stat.h>
#endif

/* ** FETCH MAIL ********************************************************** */

static void setup_fetch_mail   (gpointer in_data, gpointer op_data, CamelException *ex);
static void do_fetch_mail      (gpointer in_data, gpointer op_data, CamelException *ex);
/* static void cleanup_fetch_mail (gpointer in_data, gpointer op_data, CamelException *ex); */

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

	mail_tool_camel_lock_up();
	camel_object_ref (CAMEL_OBJECT (input->destination));
	mail_tool_camel_lock_down();
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
	mail_tool_camel_lock_up();
	camel_object_unref (CAMEL_OBJECT (search_folder));
	mail_tool_camel_lock_down();
}

/*
 *static void cleanup_fetch_mail (gpointer in_data, gpointer op_data, CamelException *ex)
 *{
 *	fetch_mail_input_t *input = (fetch_mail_input_t *) in_data;
 *
 *	input->destination = info->destination;
 *	g_free (info->source_url);
 *}
 */

const mail_operation_spec op_fetch_mail =
{
	"fetch email",
	0,
	setup_fetch_mail,
	do_fetch_mail,
	NULL
};

/* ** SEND MAIL *********************************************************** */

static void setup_send_mail   (gpointer in_data, gpointer op_data, CamelException *ex);
static void do_send_mail      (gpointer in_data, gpointer op_data, CamelException *ex);
static void cleanup_send_mail (gpointer in_data, gpointer op_data, CamelException *ex);

static void 
setup_send_mail (gpointer in_data, gpointer op_data, CamelException *ex)
{
	send_mail_input_t *input = (send_mail_input_t *) in_data;

	if (!CAMEL_IS_TRANSPORT (input->transport)) {
		camel_exception_set (ex, CAMEL_EXCEPTION_INVALID_PARAM,
				     "No transport specified for send_mail operation.");
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
		mail_tool_camel_lock_up();
		camel_object_ref (CAMEL_OBJECT (input->transport));
		camel_object_ref (CAMEL_OBJECT (input->message));
		mail_tool_camel_lock_down();
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

	mail_tool_camel_lock_up();
	camel_object_ref (CAMEL_OBJECT (input->transport));
	camel_object_ref (CAMEL_OBJECT (input->message));
	camel_object_ref (CAMEL_OBJECT (input->done_folder));
	mail_tool_camel_lock_down();
}

static void
do_send_mail (gpointer in_data, gpointer op_data, CamelException *ex)
{
	send_mail_input_t *input = (send_mail_input_t *) in_data;
	char *from_str;

	mail_tool_camel_lock_up();
	from_str = camel_address_encode (CAMEL_ADDRESS (input->from));
	camel_mime_message_set_from (input->message, from_str);
	g_free (from_str);

	camel_medium_add_header (CAMEL_MEDIUM (input->message), "X-Mailer",
				 "Evolution (Developer Preview)");
	camel_mime_message_set_date (input->message, CAMEL_MESSAGE_DATE_CURRENT, 0);
	mail_tool_camel_lock_down();

	mail_tool_send_via_transport (input->transport, CAMEL_MEDIUM (input->message), ex);

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

	mail_tool_camel_lock_up();
	camel_object_unref (CAMEL_OBJECT (input->transport));
	camel_object_unref (CAMEL_OBJECT (input->message));
	if (input->done_folder)
		camel_object_unref (CAMEL_OBJECT (input->done_folder));
	mail_tool_camel_lock_down();
}

const mail_operation_spec op_send_mail =
{
	"send a message",
	0,
	setup_send_mail,
	do_send_mail,
	cleanup_send_mail
};

/* ** EXPUNGE ************************************************************* */

static void setup_expunge_folder   (gpointer in_data, gpointer op_data, CamelException *ex);
static void do_expunge_folder      (gpointer in_data, gpointer op_data, CamelException *ex);
static void cleanup_expunge_folder (gpointer in_data, gpointer op_data, CamelException *ex);

static void setup_expunge_folder (gpointer in_data, gpointer op_data, CamelException *ex)
{
	expunge_folder_input_t *input = (expunge_folder_input_t *) in_data;

	if (!CAMEL_IS_FOLDER (input->folder)) {
		camel_exception_set (ex, CAMEL_EXCEPTION_INVALID_PARAM,
				     "No folder is selected to be expunged");
		return;
	}

	mail_tool_camel_lock_up();
	camel_object_ref (CAMEL_OBJECT (input->folder));
	mail_tool_camel_lock_down();
}

static void do_expunge_folder (gpointer in_data, gpointer op_data, CamelException *ex)
{
	expunge_folder_input_t *input = (expunge_folder_input_t *) in_data;

	mail_tool_camel_lock_up();
	camel_folder_expunge (input->folder, ex);
	mail_tool_camel_lock_down();
}

static void cleanup_expunge_folder (gpointer in_data, gpointer op_data, CamelException *ex)
{
	expunge_folder_input_t *input = (expunge_folder_input_t *) in_data;

	mail_tool_camel_lock_up();
	camel_object_unref (CAMEL_OBJECT (input->folder));
	mail_tool_camel_lock_down();
}	

const mail_operation_spec op_expunge_folder =
{
	"expunge a folder",
	0,
	setup_expunge_folder,
	do_expunge_folder,
	cleanup_expunge_folder
};

/* ** REFILE MESSAGES ***************************************************** */

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

	mail_tool_camel_lock_up();
	camel_object_ref (CAMEL_OBJECT (input->source));
	mail_tool_camel_lock_down();
}

static void do_refile_messages (gpointer in_data, gpointer op_data, CamelException *ex)
{
	refile_messages_input_t *input = (refile_messages_input_t *) in_data;
	CamelFolder *dest;
	gint i;

	dest = mail_uri_to_folder_sync (input->dest_uri);

	mail_tool_camel_lock_up();
	for (i = 0; i < input->uids->len; i++) {
		camel_folder_move_message_to (input->source, input->uids->pdata[i],
					      dest, ex);
		if (camel_exception_is_set (ex))
			break;
	}

	camel_object_unref (CAMEL_OBJECT (dest));
	mail_tool_camel_lock_down();
}

static void cleanup_refile_messages (gpointer in_data, gpointer op_data, CamelException *ex)
{
	refile_messages_input_t *input = (refile_messages_input_t *) in_data;

	mail_tool_camel_lock_up();
	camel_object_unref (CAMEL_OBJECT (input->source));
	mail_tool_camel_lock_down();
}

const mail_operation_spec op_refile_messages =
{
	"refile messages",
	0,
	setup_refile_messages,
	do_refile_messages,
	cleanup_refile_messages
};

/* ** FLAG MESSAGES ******************************************************* */

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

	mail_tool_camel_lock_up();
	camel_object_ref (CAMEL_OBJECT (input->source));
	mail_tool_camel_lock_down();
}

static void do_flag_messages (gpointer in_data, gpointer op_data, CamelException *ex)
{
	flag_messages_input_t *input = (flag_messages_input_t *) in_data;
	gint i;

	for (i = 0; i < input->uids->len; i++)
		mail_tool_set_uid_flags (input->source, input->uids->pdata[i], input->flags);
}

static void cleanup_flag_messages (gpointer in_data, gpointer op_data, CamelException *ex)
{
	flag_messages_input_t *input = (flag_messages_input_t *) in_data;

	mail_tool_camel_lock_up();
	camel_object_unref (CAMEL_OBJECT (input->source));
	mail_tool_camel_lock_down();
}

const mail_operation_spec op_flag_messages =
{
	"flag messages",
	0,
	setup_flag_messages,
	do_flag_messages,
	cleanup_flag_messages
};

/* ** SCAN SUBFOLDERS ***************************************************** */

static void setup_scan_subfolders   (gpointer in_data, gpointer op_data, CamelException *ex);
static void do_scan_subfolders      (gpointer in_data, gpointer op_data, CamelException *ex);
/*static void cleanup_scan_subfolders (gpointer in_data, gpointer op_data, CamelException *ex);*/

static void setup_scan_subfolders (gpointer in_data, gpointer op_data, CamelException *ex)
{
	scan_subfolders_input_t *input = (scan_subfolders_input_t *) in_data;

	if (!input->source_uri) {
		camel_exception_set (ex, CAMEL_EXCEPTION_INVALID_PARAM, 
				     "No source uri to scan subfolders from was provided.");
		return;
	}

	input->new_folders = g_ptr_array_new ();
}

static void do_scan_subfolders (gpointer in_data, gpointer op_data, CamelException *ex)
{
	scan_subfolders_input_t *input = (scan_subfolders_input_t *) in_data;
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
		g_ptr_array_add (input->new_folders, info);
	}

	for (i = 0; i < lsub->len; i++) {
		info = g_new (scan_subfolders_folderinfo_t, 1);
		info->path = g_strdup_printf ("/%s", (char *)lsub->pdata[i]);
		info->uri = g_strdup_printf ("%s%s%s", input->source_uri, splice, info->path);
		g_ptr_array_add (input->new_folders, info);
	}
	
	mail_tool_camel_lock_up();
	camel_folder_free_subfolder_names (folder, lsub);
	camel_object_unref (CAMEL_OBJECT (folder));
	mail_tool_camel_lock_down();
}

/*
 *static void cleanup_scan_subfolders (gpointer in_data, gpointer op_data, CamelException *ex)
 *{
 *	scan_subfolders_input_t *input = (scan_subfolders_input_t *) in_data;
 *}
 */

const mail_operation_spec op_scan_subfolders =
{
	"scan a folder tree",
	0,
	setup_scan_subfolders,
	do_scan_subfolders,
	/*cleanup_scan_subfolders*/NULL
};
