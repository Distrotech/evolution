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
#include <errno.h>
#include "camel/camel.h"
#include "camel/providers/vee/camel-vee-folder.h"
#include "e-util/e-setup.h"
#include "filter/filter-driver.h"
#include "mail.h" /*session*/
#include "mail-tools.h"

/* **************************************** */

G_LOCK_DEFINE_STATIC (camel);
G_LOCK_DEFINE_STATIC (camel_locklevel);
static GPrivate *camel_locklevel = NULL;

#define LOCK_VAL (GPOINTER_TO_INT (g_private_get (camel_locklevel)))
#define LOCK_SET(val) g_private_set (camel_locklevel, (GINT_TO_POINTER (val)))

void mail_tool_camel_lock_up (void)
{
	G_LOCK (camel_locklevel);

	if (camel_locklevel == NULL)
		camel_locklevel = g_private_new (GINT_TO_POINTER (0));
	
        if (LOCK_VAL == 0) {
		G_UNLOCK (camel_locklevel);
                G_LOCK (camel);
		G_LOCK (camel_locklevel);
	}

        LOCK_SET (LOCK_VAL + 1);

        G_UNLOCK (camel_locklevel);
}

void mail_tool_camel_lock_down (void)
{
        G_LOCK (camel_locklevel);

        if (camel_locklevel == NULL) {
                g_warning ("mail_tool_camel_lock_down: lock down before a lock up?");
                camel_locklevel = g_private_new (GINT_TO_POINTER (0));
                return;
        }

        LOCK_SET (LOCK_VAL - 1);

        if (LOCK_VAL == 0)
                G_UNLOCK (camel);

        G_UNLOCK (camel_locklevel);
}

/* **************************************** */

CamelFolder *
mail_tool_get_folder_from_urlname (const gchar *url, const gchar *name, CamelException *ex)
{
	CamelStore *store;
	CamelFolder *folder;

	mail_tool_camel_lock_up();

	store = camel_session_get_store (session, url, ex);
	if (!store) {
		mail_tool_camel_lock_down();
		return NULL;
	}

	camel_object_ref (CAMEL_OBJECT (store));

	camel_service_connect (CAMEL_SERVICE (store), ex);
	if (camel_exception_is_set (ex)) {
		camel_object_unref (CAMEL_OBJECT (store));
		mail_tool_camel_lock_down();
		return NULL;
	}

	folder = camel_store_get_folder (store, name, FALSE, ex);
	camel_object_unref (CAMEL_OBJECT (store));
	mail_tool_camel_lock_down();

	return folder;
}

gchar *
mail_tool_get_local_inbox_url (void)
{
	return g_strdup_printf ("mbox://%s/local/Inbox", evolution_dir);
}

CamelFolder *
mail_tool_get_local_inbox (CamelException *ex)
{
	gchar *url;
	CamelFolder *folder;

	url = mail_tool_get_local_inbox_url();
	folder = mail_tool_get_folder_from_urlname (url, "mbox", ex);
	g_free (url);
	return folder;
}

CamelFolder *
mail_tool_get_inbox (const gchar *url, CamelException *ex)
{
	/* FIXME: should be smarter? get_default_folder, etc */
	return mail_tool_get_folder_from_urlname (url, "inbox", ex);
}
	

CamelFolder *
mail_tool_do_movemail (const gchar *source_url, CamelException *ex)
{
	gchar *dest_url;
	gchar *dest_path;
	int tmpfd;
	int result;
	const gchar *source;

	g_return_val_if_fail (strncmp (source_url, "mbox:", 5) == 0, NULL);

	/* Set up our destination. */

	dest_url = mail_tool_get_local_inbox_url();
	dest_path = g_strdup_printf ("%s/local/Inbox/movemail", evolution_dir);

	/* Create a new movemail mailbox file of 0 size */

	tmpfd = open (dest_path, O_RDWR | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR);

	if (tmpfd == -1) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      "Couldn't create temporary "
				      "mbox `%s': %s", dest_path, g_strerror (errno));
		g_free (dest_path);
		g_free (dest_url);
		return NULL;
	}

	close (tmpfd);

	/* Skip over "mbox:" plus host part (if any) of url. */

	source = source_url + 5;
	if (!strncmp (source, "//", 2))
		source = strchr (source + 2, '/');


	/* Movemail from source (source_url) to dest_path */

	mail_tool_camel_lock_up();
	result = camel_movemail (source, dest_path, ex);
	mail_tool_camel_lock_down();
	g_free (dest_path);

	if (result == 0 || result == -1) {
		g_free (dest_url);
		return NULL;
	}

	g_free (dest_path);

	/* Get the CamelFolder for our dest_path. */

	return mail_tool_get_folder_from_urlname (dest_url, "movemail", ex);
}

void
mail_tool_move_folder_contents (CamelFolder *source, CamelFolder *dest, CamelException *ex)
{
	GPtrArray *uids;
	int i;

	mail_tool_camel_lock_up();

	camel_object_ref (CAMEL_OBJECT (source));
	camel_object_ref (CAMEL_OBJECT (dest));

	/* Get all uids of source */

	uids = camel_folder_get_uids (source);
	printf ("mail_tool_move_folder: got %d messages in source\n", uids->len);

	/* Copy the messages */

	for (i = 0; i < uids->len; i++) {
		CamelMimeMessage *msg;
		
		/* Get the message */
		msg = camel_folder_get_message (source, uids->pdata[i], ex);
		if (camel_exception_is_set (ex)) {
			camel_object_unref (CAMEL_OBJECT (msg));
			goto cleanup;
		}
		
		/* Append it to dest */

		camel_folder_append_message (dest, msg, ex);
		if (camel_exception_is_set (ex)) {
			camel_object_unref (CAMEL_OBJECT (msg));
			goto cleanup;
		}

		/* Get rid of the message */

		camel_object_unref (CAMEL_OBJECT (msg));
		camel_folder_delete_message (source, uids->pdata[i]);
	}

	/* All done. Sync n' free. */

	camel_folder_free_uids (source, uids);
	camel_folder_sync (source, TRUE, ex);

 cleanup:
	camel_object_unref (CAMEL_OBJECT (source));
	camel_object_unref (CAMEL_OBJECT (dest));
	mail_tool_camel_lock_down();
}

void
mail_tool_set_uid_flags (CamelFolder *folder, const char *uid, guint32 set)
{
	guint32 flags;

	mail_tool_camel_lock_up();
	flags = camel_folder_get_message_flags (folder, uid);
	camel_folder_set_message_flags (folder, uid,
					set, ~flags);
	mail_tool_camel_lock_down();
}

gchar *
mail_tool_generate_forward_subject (CamelMimeMessage *msg)
{
	const gchar *from;
	const gchar *subject;
	gchar *fwd_subj;

	mail_tool_camel_lock_up();
	from = camel_mime_message_get_from (msg);
	subject = camel_mime_message_get_subject (msg);
	mail_tool_camel_lock_down();

	if (from) {
		if (subject && *subject) {
			while (*subject == ' ')
				subject++;
			fwd_subj = g_strdup_printf ("[%s] %s", from, subject);
		} else {
			fwd_subj = g_strdup_printf ("[%s] (forwarded message)",
						    from);
		}
	} else {
		if (subject && *subject) {
			if (strncmp (subject, "Fwd: ", 5) == 0)
				subject += 4;
			fwd_subj = g_strdup_printf ("Fwd: %s", subject);
		} else
			fwd_subj = g_strdup ("Fwd: (no subject)");
	}

	return fwd_subj;
}

void
mail_tool_send_via_transport (CamelTransport *transport, CamelMedium *medium, CamelException *ex)
{
	mail_tool_camel_lock_up();
	/*camel_object_ref (CAMEL_OBJECT (transport));*/
	/*camel_object_ref (CAMEL_OBJECT (medium));*/

	camel_service_connect (CAMEL_SERVICE (transport), ex);

	if (camel_exception_is_set (ex))
		goto cleanup;

	camel_transport_send (transport, medium, ex);

	if (camel_exception_is_set (ex))
		goto cleanup;

	camel_service_disconnect (CAMEL_SERVICE (transport), ex);

 cleanup:
	/*camel_object_unref (CAMEL_OBJECT (medium));*/
	/*camel_object_unref (CAMEL_OBJECT (transport));*/
	mail_tool_camel_lock_down();
}

CamelMimePart *
mail_tool_make_message_attachment (CamelMimeMessage *message)
{
	CamelMimePart *part;
	const char *subject;
	gchar *desc;

	mail_tool_camel_lock_up();
	/*camel_object_ref (CAMEL_OBJECT (message));*/

	subject = camel_mime_message_get_subject (message);
	if (subject)
		desc = g_strdup_printf ("Forwarded message - %s", subject);
	else
		desc = g_strdup ("Forwarded message (no subjecr)");

	part = camel_mime_part_new ();
	camel_mime_part_set_disposition (part, "inline");
	camel_mime_part_set_description (part, desc);
	camel_medium_set_content_object (CAMEL_MEDIUM (part),
					 CAMEL_DATA_WRAPPER (message));
	camel_mime_part_set_content_type (part, "message/rfc822");
	/*camel_object_unref (CAMEL_OBJECT (message));*/
	mail_tool_camel_lock_down();
	return part;
}

CamelFolder *
mail_tool_fetch_mail_into_searchable (const char *source_url, CamelException *ex)
{
	CamelFolder *search_folder = NULL;
	CamelFolder *spool_folder = NULL;

	/* If using IMAP, don't do anything... */

	if (!strncmp (source_url, "imap:", 5))
		return NULL;

	/* If fetching mail from an mbox store, safely copy it to a
	 * temporary store first.
	 */

	if (!strncmp (source_url, "mbox:", 5))
		spool_folder = mail_tool_do_movemail (source_url, ex);
	else
		spool_folder = mail_tool_get_inbox (source_url, ex);

	if (camel_exception_is_set (ex))
		goto cleanup;

	/* can we perform filtering on this source? */

	if (!(spool_folder->has_summary_capability
	      && spool_folder->has_search_capability)) {

		/* no :-(. Copy the messages to a local tempbox
		 * so that the folder browser can search it. */
		gchar *url;

		url = mail_tool_get_local_inbox_url();
		search_folder = mail_tool_get_folder_from_urlname (url, "movemail", ex);
		g_free (url);
		if (camel_exception_is_set (ex))
			goto cleanup;

		mail_tool_move_folder_contents (spool_folder, search_folder, ex);
		if (camel_exception_is_set (ex))
			goto cleanup;

	} else {
		/* we can search! don't bother movemailing */
		search_folder = spool_folder;
		mail_tool_camel_lock_up();
		camel_object_ref (CAMEL_OBJECT (search_folder));
		mail_tool_camel_lock_down();
	}

 cleanup:
	mail_tool_camel_lock_up();
	camel_object_unref (CAMEL_OBJECT (spool_folder));
	mail_tool_camel_lock_down();
	return search_folder;
}

void
mail_tool_filter_contents_into (CamelFolder *source, CamelFolder *dest, 
				gpointer hook_func, gpointer hook_data,
				CamelException *ex)
{
	gchar *userrules;
	gchar *systemrules;
	FilterDriver *filter;

        userrules = g_strdup_printf ("%s/filters.xml", evolution_dir);
        systemrules = g_strdup_printf ("%s/evolution/filtertypes.xml", EVOLUTION_DATADIR);
        filter = filter_driver_new (systemrules, userrules, mail_tool_uri_to_folder_noex);
        g_free (userrules);
        g_free (systemrules);

	if (hook_func)
		camel_object_hook_event (CAMEL_OBJECT (dest), "folder_changed",
					 hook_func, hook_data);

        filter_driver_run (filter, source, dest, TRUE, hook_func, hook_data);
}

CamelFolder *
mail_tool_get_root_of_store (const char *source_uri, CamelException *ex)
{
	CamelStore *store;
	CamelFolder *folder;

	mail_tool_camel_lock_up();

	store = camel_session_get_store (session, source_uri, ex);
	if (!store) {
		mail_tool_camel_lock_down();
		return NULL;
	}

	camel_object_ref (CAMEL_OBJECT (store));

	camel_service_connect (CAMEL_SERVICE (store), ex);
	if (camel_exception_is_set (ex)) {
		camel_object_unref (CAMEL_OBJECT (store));
		mail_tool_camel_lock_down();
		return NULL;
	}

	folder = camel_store_get_root_folder (store, ex);
	camel_object_unref (CAMEL_OBJECT (store));
	mail_tool_camel_lock_down();

	return folder;
}

CamelFolder *
mail_tool_uri_to_folder (const char *uri, CamelException *ex)
{
	CamelStore *store = NULL;
	CamelFolder *folder = NULL;
	char *store_uri;

	if (!strncmp (uri, "vfolder:", 8)) {
		char *query, *newquery;

		store_uri = g_strdup (uri);
		query = strchr (store_uri, '?');
		if (query) {
			*query++ = 0;
		} else {
			query = "";
		}
		newquery = g_strdup_printf("mbox?%s", query);

		mail_tool_camel_lock_up();
		store = camel_session_get_store (session, store_uri, ex);

		if (store) {
			char *source_uri;

			folder = camel_store_get_folder (store, newquery, TRUE, ex);
			/* FIXME: do this properly rather than hardcoding */
			/* FIXME: Now we REALLY need to do this properly... */
#warning "Find a way not to hardcode vfolder source"

			source_uri = g_strdup_printf ("file://%s/local/Inbox", evolution_dir);
			camel_vee_folder_add_folder (CAMEL_VEE_FOLDER (folder), 
						     mail_tool_uri_to_folder (source_uri, ex));
		}

		mail_tool_camel_lock_down();
		g_free (newquery);
		g_free (store_uri);

	} else if (!strncmp (uri, "imap:", 5)) {
		char *service, *ptr;
		
		service = g_strdup_printf ("%s/", uri);
		for (ptr = service + 7; *ptr && *ptr != '/'; ptr++) {};
		ptr++;
		*ptr = '\0';

		mail_tool_camel_lock_up();
		store = camel_session_get_store (session, service, ex);
		g_free (service);
		if (store) {
			CamelURL *url = CAMEL_SERVICE (store)->url;
			char *folder_uri;

			for (ptr = (char *)(uri + 7); *ptr && *ptr != '/'; ptr++) {};
			if (*ptr == '/') {
				if (url && url->path) {
					ptr += strlen (url->path);
					if (*ptr == '/')
						ptr++;
				}

				if (*ptr == '/')
					ptr++;
				/*for ( ; *ptr && *ptr == '/'; ptr++);*/

				folder_uri = g_strdup (ptr);				
				folder = camel_store_get_folder (store, folder_uri, TRUE, ex);
				g_free (folder_uri);
			}
		}

		mail_tool_camel_lock_down();

	} else if (!strncmp(uri, "news:", 5)) {
		mail_tool_camel_lock_up();
		store = camel_session_get_store (session, uri, ex);
		if (store) {
			const char *folder_path;

			folder_path = uri + 5;
			folder = camel_store_get_folder (store, folder_path, FALSE, ex);
		}

		mail_tool_camel_lock_down();

	} else if (!strncmp (uri, "file:", 5)) {
		/* Change "file:" to "mbox:". */
		store_uri = g_strdup_printf ("mbox:%s", uri + 5);
		store = camel_session_get_store (session, store_uri, ex);
		g_free (store_uri);
		if (store) {
			folder = camel_store_get_folder (store, "mbox", FALSE, ex);
		}
	} else {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      "Don't know protocol to open URI `%s'", uri);
	}

	if (camel_exception_is_set (ex)) {
		if (folder) {
			camel_object_unref (CAMEL_OBJECT (folder));
			folder = NULL;
		}
	}

	if (store)
		camel_object_unref (CAMEL_OBJECT (store));

	return folder;
}

CamelFolder *
mail_tool_uri_to_folder_noex (const char *uri)
{
	CamelException ex;
	CamelFolder *result;

	camel_exception_init (&ex);
	result = mail_tool_uri_to_folder (uri, &ex);

	if (camel_exception_is_set (&ex)) {
		gchar *msg;
		GtkWidget *dialog;

		msg = g_strdup_printf ("Cannot open location `%s':\n"
				       "%s",
				       uri,
				       camel_exception_get_description (&ex));
		dialog = gnome_error_dialog (msg);
		g_free (msg);
		gnome_dialog_run_and_close (GNOME_DIALOG (dialog));
		gtk_widget_destroy (dialog);
	}

	return result;
}

