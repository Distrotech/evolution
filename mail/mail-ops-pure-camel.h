/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* 
 * Author : 
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

#include <camel/camel.h>
#include "mail-threads.h"

/* ** FETCH MAIL ********************************************************** */

typedef struct fetch_mail_input_s {
	const gchar *source_url;
	CamelFolder *destination;

	/* If destination is NULL, then we'll guess the
	 * default inbox and put the result, refed, into
	 * destination. Caller must unref it. If on completion,
	 * destination is NULL, then mail was not fetched
	 * (currently happens when source_url is IMAP). */
} fetch_mail_input_t;

extern const mail_operation_spec op_fetch_mail;

/* ** SEND MAIL *********************************************************** */

typedef struct send_mail_input_s {
	CamelTransport *transport;
	CamelMimeMessage *message;
	CamelInternetAddress *from;

	/* If done_folder != NULL, will add done_flags to
	 * the flags of the message done_uid in done_folder. */

	CamelFolder *done_folder;
	const char *done_uid;
	guint32 done_flags;
} send_mail_input_t;

extern const mail_operation_spec op_send_mail;

/* ** EXPUNGE ************************************************************* */

typedef struct expunge_folder_input_s {
	CamelFolder *folder;
} expunge_folder_input_t;

extern const mail_operation_spec op_expunge_folder;

/* ** REFILE ************************************************************** */

typedef struct refile_messages_input_s {
	CamelFolder *source;
	GPtrArray *uids;
	gchar *dest_uri;
} refile_messages_input_t;

extern const mail_operation_spec op_refile_messages;

/* ** FLAG MESSAGES ******************************************************* */

typedef struct flag_messages_input_s {
	CamelFolder *source;
	GPtrArray *uids;
	guint32 flags;
} flag_messages_input_t;

extern const mail_operation_spec op_flag_messages;

/* ** SCAN SUBFOLDERS ***************************************************** */

typedef struct scan_subfolders_input_s {
	const gchar *source_uri;
	gboolean add_INBOX;
	GPtrArray *new_folders;
} scan_subfolders_input_t;

typedef struct scan_subfolders_folderinfo_s {
	char *path;
	char *uri;
} scan_subfolders_folderinfo_t;

extern const mail_operation_spec op_scan_subfolders;
