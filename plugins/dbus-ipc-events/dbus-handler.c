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
 *		Srinivasa Ragavan <sragavan@gnome.org>
 *
 * Copyright (C) 2010 Intel Corporation. (www.intel.com)
 *
 */


#ifndef DBUS_API_SUBJECT_TO_CHANGE
#define DBUS_API_SUBJECT_TO_CHANGE
#endif

#include <stdio.h>
#include <string.h>
#include <dbus/dbus.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>

#include <camel/camel-folder.h>

#include <e-util/e-binding.h>
#include <e-util/e-config.h>
#include <e-util/gconf-bridge.h>
#include <mail/em-utils.h>
#include <mail/em-event.h>
#include <mail/em-folder-tree.h>
#include <shell/e-shell-view.h>

#include "composer/e-msg-composer.h"
#include "shell/es-event.h"

#include "shell/e-shell.h"
#include "shell/e-shell-backend.h"
#include "mail/e-mail-reader.h"
#include "mail/e-mail-browser.h"
#include "mail/mail-folder-cache.h"
#include "mail/mail-ops.h"
#include "mail/em-composer-utils.h"


void org_gnome_mail_draft_saved (EPlugin *ep, EMEventTargetFolder *t);
void org_gnome_mail_draft_deleted (EPlugin *ep, EMEventTargetFolder *t);
void org_gnome_mail_note (EPlugin *ep, EMEventTargetFolder *t);
void dbus_ipc_events (EPlugin *ep, ESEventTargetUpgrade *target);

gint e_plugin_lib_enable (EPlugin *ep, gint enable);

/* org.gnome.evolutionserver.LaunchComposer (draft-folder-uri, draft-mail-uid)
 * If UID not given, then the composer is launched.
 * Else, the draft folder is opened from the given uri
 * and the uid message will be opened in composer */

static void
dbus_handler_got_draft_folder_cb (gchar *folder_uri,
                           CamelFolder *folder,
                           gpointer user_data)
{
	char *uid = user_data;
	GPtrArray *uids = g_ptr_array_new ();

	g_ptr_array_add (uids, g_strdup(uid));

	em_utils_edit_messages (folder, uids, TRUE);

}

static DBusHandlerResult
launch_composer (DBusMessage *message)
{
	GtkWidget *composer;
	DBusMessageIter iter;

	if (!dbus_message_iter_init(message, &iter)) {
	
		composer = e_msg_composer_new ();

		gtk_widget_show (composer);
		gdk_window_raise (((GtkWidget *) composer)->window);
	} else {
		char *uri, *uid;
		dbus_message_iter_get_basic (&iter, &uri);
		dbus_message_iter_next(&iter);
		dbus_message_iter_get_basic (&iter, &uid);
		mail_get_folder (
			uri, 0, dbus_handler_got_draft_folder_cb,
			uid, mail_msg_unordered_push);


	}
	return DBUS_HANDLER_RESULT_HANDLED;
}

/* org.gnome.evolutionserver.ShowMail folder-uri mail-uid */
static void
dbus_handler_got_folder_cb (gchar *folder_uri,
                           CamelFolder *folder,
                           gpointer user_data)
{
	char *uid = user_data;
	EShellBackend *backend = e_shell_get_backend_by_name (e_shell_get_default(), "mail");
	GtkWidget *browser;

	browser = e_mail_browser_new (backend);

	e_mail_reader_set_folder (browser, folder, folder_uri);
	e_mail_reader_set_message (E_MAIL_READER (browser), uid);
	gtk_widget_show (browser);
}

static DBusHandlerResult
show_email (DBusMessage *msg)
{
	char *uri = NULL, *uid=NULL;
	EShellBackend *backend = e_shell_get_backend_by_name (e_shell_get_default(), "mail");
	GtkWidget *browser;
	CamelFolder *folder;

	dbus_message_get_args(msg, NULL,
				DBUS_TYPE_STRING, &uri,
				DBUS_TYPE_STRING, &uid,
				DBUS_TYPE_INVALID);


	if (mail_folder_cache_get_folder_from_uri(mail_folder_cache_get_default(),
				uri, &folder)) {

		browser = e_mail_browser_new (backend);
		e_mail_reader_set_folder(
			E_MAIL_READER (browser), folder, uri);
		e_mail_reader_set_message (E_MAIL_READER (browser), uid);
		gtk_widget_show (browser);
	} else {
		/* Get the folder first. */
		mail_get_folder (
			uri, 0, dbus_handler_got_folder_cb,
			uid, mail_msg_unordered_push);

	}
	return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult agent_message(DBusConnection *conn,
                                                DBusMessage *msg, void *data)
{
/*	printf("DBUS MSG : %s %s %s\n", dbus_message_get_path(msg),
				dbus_message_get_member(msg),
				dbus_message_get_interface(msg)); */

        if (dbus_message_is_method_call(msg, "org.gnome.evolutionserver", "LaunchComposer"))
                return launch_composer(msg);

	if (dbus_message_is_method_call(msg, "org.gnome.evolutionserver", "LaunchEMail"))
                return show_email(msg);

        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static DBusObjectPathVTable vtable = {NULL, &agent_message, NULL, NULL, NULL, NULL};
static DBusConnection *evolution_dbus=NULL;
static gboolean enabled = FALSE;
#define DBUS_PATH_EVOLUTION_SERVER "/org/gnome/evolution/server"
#define DBUS_PATH_NEWMAIL		"/org/gnome/evolution/mail/newmail"
#define DBUS_PATH_NOTIFICATION		"/org/gnome/evolution/mail/notification"

#define DBUS_INTERFACE		"org.gnome.evolution.mail.dbus.Signal"
#define DBUS_EVOLUTION_NAME 	"org.gnome.evolutionserver"

static void
send_dbus_message (const char *path, const gchar *name, CamelFolder *folder, const gchar *data, guint new, const gchar *msg_uid, const gchar *msg_sender, const gchar *msg_subject)
{
	DBusMessage *message;

	/* Create a new message on the DBUS_INTERFACE */
	if (!(message = dbus_message_new_signal (path, DBUS_INTERFACE, name)))
		return;

	/* Appends the data as an argument to the message */
	dbus_message_append_args (message, DBUS_TYPE_STRING, &data, DBUS_TYPE_INVALID);

	if (new) {
		gchar * display_name = em_utils_folder_name_from_uri (data);
		dbus_message_append_args (message,
					  DBUS_TYPE_STRING, &display_name, DBUS_TYPE_UINT32, &new,
					  DBUS_TYPE_INVALID);
	}

	#define add_named_param(name, value)	\
		if (value) {	\
			gchar *val;	\
			val = g_strconcat (name, ":", value, NULL);	\
			dbus_message_append_args (message, DBUS_TYPE_STRING, &val, DBUS_TYPE_INVALID);	\
		}
	add_named_param ("msg_uid", msg_uid);
	add_named_param ("msg_sender", msg_sender);
	add_named_param ("msg_subject", msg_subject);
	if (new && msg_uid && *msg_uid) {
		char *preview;
		CamelMessageInfoBase *info = camel_folder_summary_uid (folder->summary, msg_uid);

		if (info) {
			time_t dsent = camel_message_info_date_sent (info);
			guint32 time_sent = (gint32) dsent;

			preview = camel_message_info_preview (info);
			if (preview && *preview) {
				char *nline = strchr (preview, '\n');
				if (nline)
					*nline = 0; /* End the string */
			add_named_param ("msg_preview", preview);
				
			} 
			dbus_message_append_args (message, DBUS_TYPE_UINT32, &time_sent, DBUS_TYPE_INVALID);
			camel_message_info_free(info);
		} else printf("Unable to get info %s\n", msg_uid);
	}
	#undef add_named_param

	/* Sends the message */
	dbus_connection_send (evolution_dbus, message, NULL);

	/* Frees the message */
	dbus_message_unref (message);
}

static gboolean init_dbus_server (void);

static gboolean
reinit_dbus (gpointer user_data)
{
	if (!enabled || init_dbus_server ())
		return FALSE;

	/* keep trying to re-establish dbus connection */
	return TRUE;
}

static DBusHandlerResult
filter_function (DBusConnection *connection, DBusMessage *message, gpointer user_data)
{
	if (dbus_message_is_signal (message, DBUS_INTERFACE_LOCAL, "Disconnected") &&
	    strcmp (dbus_message_get_path (message), DBUS_PATH_LOCAL) == 0) {
		dbus_connection_unref (evolution_dbus);
		evolution_dbus = NULL;

		g_timeout_add (3000, reinit_dbus, NULL);

		return DBUS_HANDLER_RESULT_HANDLED;
	}

	return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

gboolean
init_dbus_server ()
{

	evolution_dbus = dbus_bus_get(DBUS_BUS_SESSION, NULL);
        if (!evolution_dbus) {
                printf("Can't get on session bus");
                return FALSE;
        }

	dbus_bus_request_name(evolution_dbus,
                        DBUS_EVOLUTION_NAME, 0, NULL);

	dbus_connection_setup_with_g_main (evolution_dbus, NULL);	
	dbus_connection_set_exit_on_disconnect (evolution_dbus, FALSE);

	dbus_connection_add_filter (evolution_dbus, filter_function, NULL, NULL);

        if (!dbus_connection_register_object_path(evolution_dbus,
                        DBUS_PATH_EVOLUTION_SERVER, &vtable, NULL))
        {
                return FALSE;
        }

	return TRUE;
}

void
dbus_ipc_events (EPlugin *ep, ESEventTargetUpgrade *target)
{
	if (!enabled)
		return;

	init_dbus_server ();
}

/* DBus Events */
void
org_gnome_mail_draft_deleted (EPlugin *ep, EMEventTargetFolder *t)
{
	g_return_if_fail (t != NULL);
	
	if (!enabled )
		return;
	
	send_dbus_message (DBUS_PATH_NOTIFICATION, "DraftDeleted", t->folder, t->uri, 0, t->msg_uid, t->msg_sender, t->msg_subject);

}

void
org_gnome_mail_note (EPlugin *ep, EMEventTargetFolder *t)
{
	g_return_if_fail (t != NULL);
	
	if (!enabled )
		return;
	
	send_dbus_message (DBUS_PATH_NOTIFICATION, "MailNote", t->folder, t->uri, 1, t->msg_uid, t->msg_sender, t->msg_subject);

}

void
org_gnome_mail_draft_saved (EPlugin *ep, EMEventTargetFolder *t)
{
	g_return_if_fail (t != NULL);

	if (!enabled )
		return;
	send_dbus_message (DBUS_PATH_NOTIFICATION, "DraftSaved", t->folder, t->uri, 1, t->msg_uid, t->msg_sender, t->msg_subject);
	
}

gint
e_plugin_lib_enable (EPlugin *ep, gint enable)
{
	if (enable) {
		enabled = TRUE;
	} else {
		enabled = FALSE;
	}

	return 0;
}

