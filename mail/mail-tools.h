/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>  
 *
 *
 * Authors:
 *		Peter Williams <peterw@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#ifndef MAIL_TOOLS_H
#define MAIL_TOOLS_H

#include <glib.h>

struct _CamelFolder;
struct _CamelException;
struct _CamelMimeMessage;
struct _CamelMimePart;
struct _camel_header_raw;

/* Get the "inbox" for a url (uses global session) */
struct _CamelFolder *mail_tool_get_inbox (const char *url, struct _CamelException *ex);

/* Get the "trash" for a url (uses global session) */
struct _CamelFolder *mail_tool_get_trash (const char *url, int connect, struct _CamelException *ex);

/* Does a camel_movemail into the local movemail folder
 * and returns the path to the new movemail folder that was created. which shoudl be freed later */
char *mail_tool_do_movemail (const char *source_url, struct _CamelException *ex);

struct _camel_header_raw *mail_tool_remove_xevolution_headers (struct _CamelMimeMessage *message);
void mail_tool_restore_xevolution_headers (struct _CamelMimeMessage *message, struct _camel_header_raw *);

/* Generates the subject for a message forwarding @msg */
gchar *mail_tool_generate_forward_subject (struct _CamelMimeMessage *msg);

/* Make a message into an attachment */
struct _CamelMimePart *mail_tool_make_message_attachment (struct _CamelMimeMessage *message);

/* Parse the ui into a real struct _CamelFolder any way we know how. */
struct _CamelFolder *mail_tool_uri_to_folder (const char *uri, guint32 flags, struct _CamelException *ex);

GHashTable *mail_lookup_url_table (struct _CamelMimeMessage *mime_message);

struct _CamelFolder *mail_tools_x_evolution_message_parse (char *in, unsigned int inlen, GPtrArray **uids);

char *mail_tools_folder_to_url (struct _CamelFolder *folder);

#endif
