/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
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

#include <unistd.h>
#include <pthread.h>
#include <ctype.h>
#include <errno.h>
#include <gal/widgets/e-unicode.h>
#include "camel/camel.h"
#include "camel/camel-vee-folder.h"
#include "mail-vfolder.h"
#include "filter/vfolder-rule.h"
#include "filter/vfolder-context.h"
#include "filter/filter-option.h"
#include "filter/filter-input.h"
#include "mail.h" /*session*/
#include "mail-tools.h"
#include "mail-local.h"
#include "mail-mt.h"
#include "mail-folder-cache.h"
#include "e-util/e-html-utils.h"

/* **************************************** */

gchar *
mail_tool_get_local_movemail_path (void)
{
	static gint count = 0;
	static pthread_mutex_t movemail_path_lock = PTHREAD_MUTEX_INITIALIZER;
	gint my_count;

	/* Ah, the joys of being multi-threaded... */
	pthread_mutex_lock (&movemail_path_lock);
	my_count = count;
	++count;
	pthread_mutex_unlock (&movemail_path_lock);

	return g_strdup_printf ("%s/local/Inbox/movemail.%d", evolution_dir, my_count);
}

CamelFolder *
mail_tool_get_local_inbox (CamelException *ex)
{
	gchar *url;
	CamelFolder *folder;

	url = g_strdup_printf("file://%s/local/Inbox", evolution_dir);
	folder = mail_tool_uri_to_folder (url, ex);
	g_free (url);
	return folder;
}

CamelFolder *
mail_tool_get_inbox (const gchar *url, CamelException *ex)
{
	CamelStore *store;
	CamelFolder *folder;

	store = camel_session_get_store (session, url, ex);
	if (!store)
		return NULL;

	folder = camel_store_get_inbox (store, ex);
	camel_object_unref (CAMEL_OBJECT (store));

	return folder;
}

CamelFolder *
mail_tool_get_trash (const gchar *url, CamelException *ex)
{
	CamelStore *store;
	CamelFolder *trash;
	
	store = camel_session_get_store (session, url, ex);
	if (!store)
		return NULL;
	
	trash = camel_store_get_trash (store, ex);
	camel_object_unref (CAMEL_OBJECT (store));
	
	return trash;
}

/* why is this function so stupidly complex when allthe work is done elsehwere? */
char *
mail_tool_do_movemail (const gchar *source_url, CamelException *ex)
{
	gchar *dest_path;
	const gchar *source;
	struct stat sb;
#ifndef MOVEMAIL_PATH
	int tmpfd;
#endif
	g_return_val_if_fail (strncmp (source_url, "mbox:", 5) == 0, NULL);
	
	/* Set up our destination. */
	dest_path = mail_tool_get_local_movemail_path();
	
	/* Create a new movemail mailbox file of 0 size */
	
#ifndef MOVEMAIL_PATH
	tmpfd = open (dest_path, O_RDWR | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR);
	
	if (tmpfd == -1) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Couldn't create temporary mbox `%s': %s"),
				      dest_path, g_strerror (errno));
		g_free (dest_path);
		return NULL;
	}
	
	close (tmpfd);
#endif
	
	/* Skip over "mbox:" plus host part (if any) of url. */
	source = source_url + 5;
	if (!strncmp (source, "//", 2))
		source = strchr (source + 2, '/');
	
	/* Movemail from source (source_url) to dest_path */
	camel_movemail (source, dest_path, ex);
	
	if (stat (dest_path, &sb) < 0 || sb.st_size == 0) {
		unlink (dest_path); /* Clean up the movemail.foo file. */
		g_free (dest_path);
		return NULL;
	}
	
	if (camel_exception_is_set (ex)) {
		g_free (dest_path);
		return NULL;
	}
	
	return dest_path;
}

char *
mail_tool_generate_forward_subject (CamelMimeMessage *msg)
{
	const char *subject;
	char *fwd_subj;
	const int max_subject_length = 1024;
	
	subject = camel_mime_message_get_subject(msg);
	
	if (subject && *subject) {
		/* Truncate insanely long subjects */
		if (strlen (subject) < max_subject_length)
			fwd_subj = g_strdup_printf ("[Fwd: %s]", subject);
		else
			fwd_subj = g_strdup_printf ("[Fwd: %.*s...]", max_subject_length, subject);
	} else {
		const CamelInternetAddress *from;
		char *fromstr;
		
		from = camel_mime_message_get_from (msg);
		if (from) {
			fromstr = camel_address_format (CAMEL_ADDRESS (from));
			fwd_subj = g_strdup_printf ("[Fwd: %s]", fromstr);
			g_free (fromstr);
		} else
			fwd_subj = g_strdup ("[Fwd: No Subject]");
	}
	
	return fwd_subj;
}

XEvolution *
mail_tool_remove_xevolution_headers (CamelMimeMessage *message)
{
	XEvolution *xev;
	
	xev = g_new (XEvolution, 1);
	xev->flags = g_strdup (camel_medium_get_header (CAMEL_MEDIUM (message), "X-Evolution"));
	xev->source = g_strdup (camel_medium_get_header (CAMEL_MEDIUM (message), "X-Evolution-Source"));
	xev->transport = g_strdup (camel_medium_get_header (CAMEL_MEDIUM (message), "X-Evolution-Transport"));
	xev->account = g_strdup (camel_medium_get_header (CAMEL_MEDIUM (message), "X-Evolution-Account"));
	xev->fcc = g_strdup (camel_medium_get_header (CAMEL_MEDIUM (message), "X-Evolution-Fcc"));
	xev->format = g_strdup (camel_medium_get_header (CAMEL_MEDIUM (message), "X-Evolution-Format"));
	
	/* rip off the X-Evolution* headers */
	camel_medium_remove_header (CAMEL_MEDIUM (message), "X-Evolution");
	camel_medium_remove_header (CAMEL_MEDIUM (message), "X-Evolution-Source");
	camel_medium_remove_header (CAMEL_MEDIUM (message), "X-Evolution-Transport");
	camel_medium_remove_header (CAMEL_MEDIUM (message), "X-Evolution-Account");
	camel_medium_remove_header (CAMEL_MEDIUM (message), "X-Evolution-Fcc");
	camel_medium_remove_header (CAMEL_MEDIUM (message), "X-Evolution-Format");
	
	return xev;
}

void
mail_tool_restore_xevolution_headers (CamelMimeMessage *message, XEvolution *xev)
{
	if (xev->flags)
		camel_medium_set_header (CAMEL_MEDIUM (message), "X-Evolution", xev->flags);
	if (xev->source)
		camel_medium_set_header (CAMEL_MEDIUM (message), "X-Evolution-Source", xev->source);
	if (xev->transport)
		camel_medium_set_header (CAMEL_MEDIUM (message), "X-Evolution-Transport", xev->transport);
	if (xev->account)
		camel_medium_set_header (CAMEL_MEDIUM (message), "X-Evolution-Account", xev->account);
	if (xev->fcc)
		camel_medium_set_header (CAMEL_MEDIUM (message), "X-Evolution-Fcc", xev->fcc);
	if (xev->format)
		camel_medium_set_header (CAMEL_MEDIUM (message), "X-Evolution-Format", xev->format);
}

void
mail_tool_destroy_xevolution (XEvolution *xev)
{
	g_free (xev->flags);
	g_free (xev->source);
	g_free (xev->transport);
	g_free (xev->account);
	g_free (xev->fcc);
	g_free (xev);
}

CamelMimePart *
mail_tool_make_message_attachment (CamelMimeMessage *message)
{
	CamelMimePart *part;
	const char *subject;
	XEvolution *xev;
	char *desc;
	
	subject = camel_mime_message_get_subject (message);
	if (subject) {
		char *fmt;
		
		fmt = e_utf8_from_locale_string (_("Forwarded message - %s"));
		desc = g_strdup_printf (fmt, subject);
		g_free (fmt);
	} else {
		desc = e_utf8_from_locale_string (_("Forwarded message"));
	}
	
	/* rip off the X-Evolution headers */
	xev = mail_tool_remove_xevolution_headers (message);
	mail_tool_destroy_xevolution (xev);
	
	part = camel_mime_part_new ();
	camel_mime_part_set_disposition (part, "inline");
	camel_mime_part_set_description (part, desc);
	camel_medium_set_content_object (CAMEL_MEDIUM (part),
					 CAMEL_DATA_WRAPPER (message));
	camel_mime_part_set_content_type (part, "message/rfc822");
	g_free (desc);
	
	return part;
}

CamelFolder *
mail_tool_uri_to_folder (const char *uri, CamelException *ex)
{
	CamelURL *url;
	CamelStore *store = NULL;
	CamelFolder *folder = NULL;
	int offset = 0;
	
	g_return_val_if_fail (uri != NULL, NULL);
	
	/* This hack is still needed for file:/ since it's its own EvolutionStorage type */
	if (!strncmp (uri, "vtrash:", 7))
		offset = 7;
	
	url = camel_url_new (uri + offset, ex);
	if (!url) {
		return NULL;
	}

	store = camel_session_get_store (session, uri + offset, ex);
	if (store) {
		const char *name;
		
		/* if we have a fragment, then the path is actually used by the store,
		   so the fragment is the path to the folder instead */
		if (url->fragment) {
			name = url->fragment;
		} else {
			if (url->path && *url->path)
				name = url->path + 1;
			else
				name = "";
		}
		
		if (offset)
			folder = camel_store_get_trash (store, ex);
		else
			folder = camel_store_get_folder (store, name, CAMEL_STORE_FOLDER_CREATE, ex);
		camel_object_unref (CAMEL_OBJECT (store));
	}
	
	if (folder)
		mail_note_folder (folder);
	
	camel_url_free (url);
	
	return folder;
}

/**
 * mail_tool_quote_message:
 * @message: mime message to quote
 * @fmt: credits format - example: "On %s, %s wrote:\n"
 * @Varargs: arguments
 *
 * Returns an allocated buffer containing the quoted message.
 */
gchar *
mail_tool_quote_message (CamelMimeMessage *message, const char *fmt, ...)
{
	CamelDataWrapper *contents;
	gboolean want_plain;
	gchar *text;
	
	want_plain = !mail_config_get_send_html ();
	contents = camel_medium_get_content_object (CAMEL_MEDIUM (message));
	/* We pass "want_plain" for "cite", since if it's HTML, we'll
	 * do the citing ourself below.
	 */
	text = mail_get_message_body (contents, want_plain, want_plain);
	
	/* Set the quoted reply text. */
	if (text) {
		gchar *ret_text, *credits = NULL;
		
		/* create credits */
		if (fmt) {
			va_list ap;
			
			va_start (ap, fmt);
			credits = g_strdup_vprintf (fmt, ap);
			va_end (ap);
		}
		
		ret_text = g_strdup_printf ("%s<!--+GtkHTML:<DATA class=\"ClueFlow\" key=\"orig\" value=\"1\">-->"
					    "<font color=\"%06x\">\n%s%s%s</font>"
					    "<!--+GtkHTML:<DATA class=\"ClueFlow\" clear=\"orig\">-->",
					    credits ? credits : "",
					    mail_config_get_citation_color (),
					    want_plain ? "" : "<blockquote><i>",
					    text,
					    want_plain ? "" : "</i></blockquote>");
		g_free (text);
		g_free (credits);
		return ret_text;
	}
	
	return NULL;
}

/**
 * mail_tool_forward_message:
 * @message: mime message to forward
 * @quoted: whether to forwarded it quoted (%TRUE) or inline (%FALSE)
 *
 * Returns an allocated buffer containing the forwarded message.
 */
gchar *
mail_tool_forward_message (CamelMimeMessage *message, gboolean quoted)
{
	gchar *title, *body, *ret;
	
	body = mail_get_message_body (CAMEL_DATA_WRAPPER (message),
				      !mail_config_get_send_html (),
				      quoted);
	title = e_utf8_from_locale_string (_("Forwarded Message"));
	ret = g_strdup_printf ("-----%s-----<br>%s", title, body ? body : "");
	g_free (title);
	g_free (body);
	return ret;
}
