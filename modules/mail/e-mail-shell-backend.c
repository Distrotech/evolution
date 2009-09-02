/*
 * e-mail-shell-backend.c
 *
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
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#include "e-mail-shell-backend.h"

#include <glib/gi18n.h>
#include <camel/camel-disco-store.h>
#include <camel/camel-offline-store.h>
#include <camel/camel-session.h>
#include <camel/camel-url.h>

#include "e-util/e-account-utils.h"
#include "e-util/e-binding.h"
#include "e-util/e-error.h"
#include "e-util/e-import.h"
#include "e-util/e-util.h"
#include "shell/e-shell.h"
#include "shell/e-shell-window.h"
#include "composer/e-msg-composer.h"
#include "widgets/misc/e-preferences-window.h"

#include "e-mail-shell-migrate.h"
#include "e-mail-shell-settings.h"
#include "e-mail-shell-sidebar.h"
#include "e-mail-shell-view.h"

#include "e-mail-browser.h"
#include "e-mail-local.h"
#include "e-mail-reader.h"
#include "e-mail-store.h"
#include "em-account-editor.h"
#include "em-account-prefs.h"
#include "em-composer-prefs.h"
#include "em-composer-utils.h"
#include "em-folder-utils.h"
#include "em-format-hook.h"
#include "em-format-html-display.h"
#include "em-mailer-prefs.h"
#include "em-network-prefs.h"
#include "em-utils.h"
#include "mail-config.h"
#include "mail-ops.h"
#include "mail-send-recv.h"
#include "mail-session.h"
#include "mail-vfolder.h"
#include "importers/mail-importer.h"

#define E_MAIL_SHELL_BACKEND_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_MAIL_SHELL_BACKEND, EMailShellBackendPrivate))

#define BACKEND_NAME "mail"
#define QUIT_POLL_INTERVAL 1  /* seconds */

struct _EMailShellBackendPrivate {
	gint mail_sync_in_progress;
	guint mail_sync_timeout_source_id;
};

static gpointer parent_class;
static GType mail_shell_backend_type;

extern gint camel_application_is_exiting;

static void
mail_shell_backend_init_importers (void)
{
	EImportClass *import_class;
	EImportImporter *importer;

	import_class = g_type_class_ref (e_import_get_type ());

	importer = mbox_importer_peek ();
	e_import_class_add_importer (import_class, importer, NULL, NULL);

	importer = elm_importer_peek ();
	e_import_class_add_importer (import_class, importer, NULL, NULL);

	importer = pine_importer_peek ();
	e_import_class_add_importer (import_class, importer, NULL, NULL);
}

static void
mail_shell_backend_mail_icon_cb (EShellWindow *shell_window,
                                const gchar *icon_name)
{
	GtkAction *action;

	action = e_shell_window_get_shell_view_action (
		shell_window, BACKEND_NAME);
	g_object_set (action, "icon-name", icon_name, NULL);
}

static void
action_mail_folder_new_cb (GtkAction *action,
                           EShellWindow *shell_window)
{
	EMFolderTree *folder_tree = NULL;
	EMailShellSidebar *mail_shell_sidebar;
	EShellSidebar *shell_sidebar;
	EShellView *shell_view;
	const gchar *view_name;

	/* Take care not to unnecessarily load the mail shell view. */
	view_name = e_shell_window_get_active_view (shell_window);
	if (g_strcmp0 (view_name, BACKEND_NAME) != 0)
		goto exit;

	shell_view = e_shell_window_get_shell_view (shell_window, view_name);
	shell_sidebar = e_shell_view_get_shell_sidebar (shell_view);

	mail_shell_sidebar = E_MAIL_SHELL_SIDEBAR (shell_sidebar);
	folder_tree = e_mail_shell_sidebar_get_folder_tree (mail_shell_sidebar);

exit:
	em_folder_utils_create_folder (
		NULL, folder_tree, GTK_WINDOW (shell_window));
}

static void
action_mail_message_new_cb (GtkAction *action,
                            EShellWindow *shell_window)
{
	EMailShellSidebar *mail_shell_sidebar;
	EShellSidebar *shell_sidebar;
	EShellView *shell_view;
	EMFolderTree *folder_tree;
	const gchar *view_name;
	gchar *uri = NULL;

	if (!em_utils_check_user_can_send_mail ())
		return;

	/* Take care not to unnecessarily load the mail shell view. */
	view_name = e_shell_window_get_active_view (shell_window);
	if (g_strcmp0 (view_name, BACKEND_NAME) != 0)
		goto exit;

	shell_view = e_shell_window_get_shell_view (shell_window, view_name);
	shell_sidebar = e_shell_view_get_shell_sidebar (shell_view);

	mail_shell_sidebar = E_MAIL_SHELL_SIDEBAR (shell_sidebar);
	folder_tree = e_mail_shell_sidebar_get_folder_tree (mail_shell_sidebar);
	uri = em_folder_tree_get_selected_uri (folder_tree);

exit:
	em_utils_compose_new_message (uri);

	g_free (uri);
}

static GtkActionEntry item_entries[] = {

	{ "mail-message-new",
	  "mail-message-new",
	  NC_("New", "_Mail Message"),
	  "<Shift><Control>m",
	  N_("Compose a new mail message"),
	  G_CALLBACK (action_mail_message_new_cb) }
};

static GtkActionEntry source_entries[] = {

	{ "mail-folder-new",
	  "folder-new",
	  NC_("New", "Mail _Folder"),
	  NULL,
	  N_("Create a new mail folder"),
	  G_CALLBACK (action_mail_folder_new_cb) }
};

static gboolean
mail_shell_backend_init_preferences (EShell *shell)
{
	EAccountList *account_list;
	GtkWidget *preferences_window;

	/* This is a main loop idle callback. */

	account_list = e_get_account_list ();
	preferences_window = e_shell_get_preferences_window (shell);

	e_preferences_window_add_page (
		E_PREFERENCES_WINDOW (preferences_window),
		"mail-accounts",
		"preferences-mail-accounts",
		_("Mail Accounts"),
		em_account_prefs_new (account_list),
		100);

	e_preferences_window_add_page (
		E_PREFERENCES_WINDOW (preferences_window),
		"mail",
		"preferences-mail",
		_("Mail Preferences"),
		em_mailer_prefs_new (shell),
		300);

	e_preferences_window_add_page (
		E_PREFERENCES_WINDOW (preferences_window),
		"composer",
		"preferences-composer",
		_("Composer Preferences"),
		em_composer_prefs_new (shell),
		400);

	e_preferences_window_add_page (
		E_PREFERENCES_WINDOW (preferences_window),
		"system-network-proxy",
		"preferences-system-network-proxy",
		_("Network Preferences"),
		em_network_prefs_new (),
		500);

	return FALSE;
}

static void
mail_shell_backend_sync_store_done_cb (CamelStore *store,
                                       gpointer user_data)
{
	EMailShellBackend *mail_shell_backend = user_data;

	mail_shell_backend->priv->mail_sync_in_progress--;
}

static void
mail_shell_backend_sync_store_cb (CamelStore *store,
                                  EMailShellBackend *mail_shell_backend)
{
	if (!camel_application_is_exiting) {
		mail_shell_backend->priv->mail_sync_in_progress++;
		mail_sync_store (
			store, FALSE,
			mail_shell_backend_sync_store_done_cb,
			mail_shell_backend);
	}
}

static gboolean
mail_shell_backend_mail_sync (EMailShellBackend *mail_shell_backend)
{
	if (camel_application_is_exiting)
		return FALSE;

	if (mail_shell_backend->priv->mail_sync_in_progress)
		goto exit;

	if (session == NULL || !camel_session_is_online (session))
		goto exit;

	e_mail_store_foreach (
		(GHFunc) mail_shell_backend_sync_store_cb,
		mail_shell_backend);

exit:
	return !camel_application_is_exiting;
}

static void
mail_shell_backend_notify_online_cb (EShell *shell,
                                    GParamSpec *pspec,
                                    EShellBackend *shell_backend)
{
	gboolean online;

	online = e_shell_get_online (shell);
	camel_session_set_online (session, online);
}

static void
mail_shell_backend_handle_email_uri_cb (gchar *folder_uri,
                                        CamelFolder *folder,
                                        gpointer user_data)
{
	EShellBackend *shell_backend = user_data;
	CamelURL *url = user_data;
	const gchar *forward;
	const gchar *reply;
	const gchar *uid;

	if (folder == NULL) {
		g_warning ("Could not open folder '%s'", folder_uri);
		goto exit;
	}

	forward = camel_url_get_param (url, "forward");
	reply = camel_url_get_param (url, "reply");
	uid = camel_url_get_param (url, "uid");

	if (reply != NULL) {
		gint mode;

		if (g_strcmp0 (reply, "all") == 0)
			mode = REPLY_MODE_ALL;
		else if (g_strcmp0 (reply, "list") == 0)
			mode = REPLY_MODE_LIST;
		else
			mode = REPLY_MODE_SENDER;

		em_utils_reply_to_message (folder, uid, NULL, mode, NULL);

	} else if (forward != NULL) {
		GPtrArray *uids;

		uids = g_ptr_array_new ();
		g_ptr_array_add (uids, g_strdup (uid));

		if (g_strcmp0 (forward, "attached") == 0)
			em_utils_forward_attached (folder, uids, folder_uri);
		else if (g_strcmp0 (forward, "inline") == 0)
			em_utils_forward_inline (folder, uids, folder_uri);
		else if (g_strcmp0 (forward, "quoted") == 0)
			em_utils_forward_quoted (folder, uids, folder_uri);
		else
			em_utils_forward_messages (folder, uids, folder_uri);

	} else {
		GtkWidget *browser;

		/* FIXME Should pass in the shell module. */
		browser = e_mail_browser_new (shell_backend);
		e_mail_reader_set_folder (
			E_MAIL_READER (browser), folder, folder_uri);
		e_mail_reader_set_message (E_MAIL_READER (browser), uid);
		gtk_widget_show (browser);
	}

exit:
	camel_url_free (url);
}

static gboolean
mail_shell_backend_handle_uri_cb (EShell *shell,
                                  const gchar *uri,
                                  EMailShellBackend *mail_shell_backend)
{
	gboolean handled = TRUE;

	if (g_str_has_prefix (uri, "mailto:")) {
		if (em_utils_check_user_can_send_mail ())
			em_utils_compose_new_message_with_mailto (uri, NULL);

	} else if (g_str_has_prefix (uri, "email:")) {
		CamelURL *url;

		url = camel_url_new (uri, NULL);
		if (camel_url_get_param (url, "uid") != NULL) {
			gchar *curi = em_uri_to_camel (uri);

			mail_get_folder (
				curi, 0,
				mail_shell_backend_handle_email_uri_cb,
				mail_shell_backend, mail_msg_unordered_push);
			g_free (curi);

		} else {
			g_warning ("Email URI's must include a uid parameter");
			camel_url_free (url);
		}
	} else
		handled = FALSE;

	return handled;
}

/* Helper for mail_shell_backend_prepare_for_[off|on]line_cb() */
static void
mail_shell_store_line_transition_done_cb (CamelStore *store,
                                          gpointer user_data)
{
	EActivity *activity = user_data;

	g_object_unref (activity);
}

/* Helper for mail_shell_backend_prepare_for_offline_cb() */
static void
mail_shell_store_prepare_for_offline_cb (CamelService *service,
                                         gpointer unused,
                                         EActivity *activity)
{
	if (CAMEL_IS_DISCO_STORE (service) || CAMEL_IS_OFFLINE_STORE (service))
		mail_store_set_offline (
			CAMEL_STORE (service), TRUE,
			mail_shell_store_line_transition_done_cb,
			g_object_ref (activity));
}

static void
mail_shell_backend_prepare_for_offline_cb (EShell *shell,
                                           EActivity *activity,
                                           EMailShellBackend *mail_shell_backend)
{
	GList *watched_windows;
	GtkWidget *parent = NULL;
	gboolean synchronize = FALSE;

	watched_windows = e_shell_get_watched_windows (shell);
	if (watched_windows != NULL)
		parent = GTK_WIDGET (watched_windows->data);

	if (e_shell_get_network_available (shell))
		synchronize = em_utils_prompt_user (
			GTK_WINDOW (parent),
			"/apps/evolution/mail/prompts/quick_offline",
			"mail:ask-quick-offline", NULL);

	if (!synchronize) {
		mail_cancel_all ();
		camel_session_set_network_state (session, FALSE);
	}

	e_mail_store_foreach (
		(GHFunc) mail_shell_store_prepare_for_offline_cb, activity);
}

/* Helper for mail_shell_backend_prepare_for_online_cb() */
static void
mail_shell_store_prepare_for_online_cb (CamelService *service,
                                        gpointer unused,
                                        EActivity *activity)
{
	if (CAMEL_IS_DISCO_STORE (service) || CAMEL_IS_OFFLINE_STORE (service))
		mail_store_set_offline (
			CAMEL_STORE (service), FALSE,
			mail_shell_store_line_transition_done_cb,
			g_object_ref (activity));
}

static void
mail_shell_backend_prepare_for_online_cb (EShell *shell,
                                          EActivity *activity,
                                          EMailShellBackend *mail_shell_backend)
{
	camel_session_set_online (session, TRUE);

	e_mail_store_foreach (
		(GHFunc) mail_shell_store_prepare_for_online_cb, activity);
}

/* Helper for mail_shell_backend_prepare_for_quit_cb() */
static void
mail_shell_backend_empty_junk (CamelStore *store,
                               gpointer opaque_store_info,
                               EMailShellBackend *mail_shell_backend)
{
	CamelFolder *folder;
	GPtrArray *uids;
	guint32 flags;
	guint32 mask;
	guint ii;

	folder = camel_store_get_junk (store, NULL);
	if (folder == NULL)
		return;

	uids = camel_folder_get_uids (folder);
	flags = mask = CAMEL_MESSAGE_DELETED | CAMEL_MESSAGE_SEEN;

	camel_folder_freeze (folder);

	for (ii = 0; ii < uids->len; ii++) {
		const gchar *uid = uids->pdata[ii];
		camel_folder_set_message_flags (folder, uid, flags, mask);
	}

	camel_folder_thaw (folder);

	camel_folder_free_uids (folder, uids);
}

/* Helper for mail_shell_backend_final_sync() */
static void
mail_shell_backend_final_sync_done_cb (CamelStore *store,
                                       gpointer user_data)
{
	g_object_unref (E_ACTIVITY (user_data));
}

/* Helper for mail_shell_backend_prepare_for_quit_cb() */
static void
mail_shell_backend_final_sync (CamelStore *store,
                               gpointer opaque_store_info,
                               gpointer user_data)
{
	struct {
		EActivity *activity;
		gboolean empty_trash;
	} *sync_data = user_data;

	/* Reffing the activity delays quitting; the reference count
	 * acts like a counting semaphore. */
	mail_sync_store (
		store, sync_data->empty_trash,
		mail_shell_backend_final_sync_done_cb,
		g_object_ref (sync_data->activity));
}

/* Helper for mail_shell_backend_prepare_for_quit_cb() */
static gboolean
mail_shell_backend_poll_to_quit (EActivity *activity)
{
	return mail_msg_active ((guint) -1);
}

/* Helper for mail_shell_backend_prepare_for_quit_cb() */
static void
mail_shell_backend_ready_to_quit (EActivity *activity)
{
	mail_session_shutdown ();
	g_object_unref (activity);
}

static void
mail_shell_backend_prepare_for_quit_cb (EShell *shell,
                                        EActivity *activity,
                                        EMailShellBackend *mail_shell_backend)
{
	EShellSettings *shell_settings;
	EAccountList *account_list;
	GConfClient *client;
	const gchar *key;
	gboolean empty_junk;
	gboolean empty_trash;
	gint empty_date;
	gint empty_days;
	gint now;
	GError *error = NULL;

	struct {
		EActivity *activity;
		gboolean empty_trash;
	} sync_data;

	client = e_shell_get_gconf_client (shell);
	shell_settings = e_shell_get_shell_settings (shell);

	camel_application_is_exiting = TRUE;
	now = time (NULL) / 60 / 60 / 24;

	account_list = e_get_account_list ();
	e_account_list_prune_proxies (account_list);

	mail_vfolder_shutdown ();

	empty_junk = e_shell_settings_get_boolean (
		shell_settings, "mail-empty-junk-on-exit");

	empty_trash = e_shell_settings_get_boolean (
		shell_settings, "mail-empty-trash-on-exit");

	/* XXX No EShellSettings properties for these keys. */

	empty_date = empty_days = 0;

	if (empty_junk) {
		key = "/apps/evolution/mail/junk/empty_on_exit_days";
		empty_days = gconf_client_get_int (client, key, &error);
		if (error == NULL) {
			g_warning ("%s", error->message);
			g_clear_error (&error);
			empty_trash = FALSE;
		}
	}

	if (empty_junk) {
		key = "/apps/evolution/mail/junk/empty_date";
		empty_date = gconf_client_get_int (client, key, &error);
		if (error == NULL) {
			g_warning ("%s", error->message);
			g_clear_error (&error);
			empty_trash = FALSE;
		}
	}

	empty_junk &= (empty_days = 0) || (empty_date + empty_days <= now);

	if (empty_junk) {
		e_mail_store_foreach (
			(GHFunc) mail_shell_backend_empty_junk,
			mail_shell_backend);

		key = "/apps/evolution/mail/junk/empty_date";
		gconf_client_set_int (client, key, now, NULL);
	}

	empty_date = empty_days = 0;

	if (empty_trash) {
		key = "/apps/evolution/mail/trash/empty_on_exit_days";
		empty_days = gconf_client_get_int (client, key, &error);
		if (error != NULL) {
			g_warning ("%s", error->message);
			g_clear_error (&error);
			empty_trash = FALSE;
		}
	}

	if (empty_trash) {
		key = "/apps/evolution/mail/trash/empty_date";
		empty_date = gconf_client_get_int (client, key, &error);
		if (error != NULL) {
			g_warning ("%s", error->message);
			g_clear_error (&error);
			empty_trash = FALSE;
		}
	}

	empty_trash &= (empty_days == 0) || (empty_date + empty_days <= now);

	sync_data.activity = activity;
	sync_data.empty_trash = empty_trash;

	e_mail_store_foreach (
		(GHFunc) mail_shell_backend_final_sync, &sync_data);

	if (empty_trash) {
		key = "/apps/evolution/mail/trash/empty_date";
		gconf_client_set_int (client, key, now, NULL);
	}

	/* Cancel all activities. */
	mail_cancel_all ();

	/* Now we poll until all activities are actually cancelled.
	 * Reffing the activity delays quitting; the reference count
	 * acts like a counting semaphore. */
	if (mail_msg_active ((guint) -1))
		g_timeout_add_seconds_full (
			G_PRIORITY_DEFAULT, QUIT_POLL_INTERVAL,
			(GSourceFunc) mail_shell_backend_poll_to_quit,
			g_object_ref (activity),
			(GDestroyNotify) mail_shell_backend_ready_to_quit);
	else
		mail_shell_backend_ready_to_quit (g_object_ref (activity));
}

static void
mail_shell_backend_quit_requested_cb (EShell *shell,
                                      EShellBackend *shell_backend)
{
	CamelFolder *folder;
	GList *watched_windows;
	GtkWindow *parent = NULL;
	guint32 unsent;
	gint response;

	/* We can quit immediately if offline. */
	if (!camel_session_is_online (session))
		return;

	/* Check Outbox for any unsent messages. */

	folder = e_mail_local_get_folder (E_MAIL_FOLDER_OUTBOX);
	if (folder == NULL)
		return;

	if (camel_object_get (
		folder, NULL, CAMEL_FOLDER_VISIBLE, &unsent, 0) != 0)
		return;

	if (unsent == 0)
		return;

	/* Try to find a parent window for the dialog.
	 * First list item is what's currently focused. */
	watched_windows = e_shell_get_watched_windows (shell);
	if (watched_windows != NULL)
		parent = GTK_WINDOW (watched_windows->data);
	response = e_error_run (parent, "mail:exit-unsaved", NULL);

	if (response == GTK_RESPONSE_YES)
		return;

	e_shell_cancel_quit (shell);
}

static void
mail_shell_backend_send_receive_cb (EShell *shell,
                                   GtkWindow *parent,
                                   EShellBackend *shell_backend)
{
	em_utils_clear_get_password_canceled_accounts_flag ();
	mail_send_receive (parent);
}

static void
mail_shell_backend_window_weak_notify_cb (EShell *shell,
                                         GObject *where_the_object_was)
{
	g_signal_handlers_disconnect_by_func (
		shell, mail_shell_backend_mail_icon_cb,
		where_the_object_was);
}

static void
mail_shell_backend_window_created_cb (EShell *shell,
                                     GtkWindow *window,
                                     EShellBackend *shell_backend)
{
	EShellSettings *shell_settings;
	static gboolean first_time = TRUE;
	const gchar *backend_name;

	shell_settings = e_shell_get_shell_settings (shell);

	/* This applies to both the composer and signature editor. */
	if (GTKHTML_IS_EDITOR (window)) {
		GList *spell_languages;

		e_binding_new (
			shell_settings, "composer-inline-spelling",
			window, "inline-spelling");

		e_binding_new (
			shell_settings, "composer-magic-links",
			window, "magic-links");

		e_binding_new (
			shell_settings, "composer-magic-smileys",
			window, "magic-smileys");

		spell_languages = e_load_spell_languages ();
		gtkhtml_editor_set_spell_languages (
			GTKHTML_EDITOR (window), spell_languages);
		g_list_free (spell_languages);
	}

	if (E_IS_MSG_COMPOSER (window)) {
		/* Integrate the new composer into the mail module. */
		em_configure_new_composer (E_MSG_COMPOSER (window));
		return;
	}

	if (!E_IS_SHELL_WINDOW (window))
		return;

	backend_name = E_SHELL_BACKEND_GET_CLASS (shell_backend)->name;

	e_shell_window_register_new_item_actions (
		E_SHELL_WINDOW (window), backend_name,
		item_entries, G_N_ELEMENTS (item_entries));

	e_shell_window_register_new_source_actions (
		E_SHELL_WINDOW (window), backend_name,
		source_entries, G_N_ELEMENTS (source_entries));

	g_signal_connect_swapped (
		shell, "event::mail-icon",
		G_CALLBACK (mail_shell_backend_mail_icon_cb), window);

	g_object_weak_ref (
		G_OBJECT (window), (GWeakNotify)
		mail_shell_backend_window_weak_notify_cb, shell);

	if (first_time) {
		g_signal_connect (
			window, "map-event",
			G_CALLBACK (e_msg_composer_check_autosave), NULL);
		first_time = FALSE;
	}
}

static void
mail_shell_backend_constructed (GObject *object)
{
	EMailShellBackendPrivate *priv;
	EShell *shell;
	EShellBackend *shell_backend;
	const gchar *data_dir;

	priv = E_MAIL_SHELL_BACKEND_GET_PRIVATE (object);

	shell_backend = E_SHELL_BACKEND (object);
	shell = e_shell_backend_get_shell (shell_backend);

	/* This also initializes Camel, so it needs to happen early. */
	mail_session_init (shell_backend);

	/* Register format types for EMFormatHook. */
	em_format_hook_register_type (em_format_get_type ());
	em_format_hook_register_type (em_format_html_get_type ());
	em_format_hook_register_type (em_format_html_display_get_type ());

	/* Register plugin hook types. */
	em_format_hook_get_type ();

	mail_shell_backend_init_importers ();

	g_signal_connect (
		shell, "notify::online",
		G_CALLBACK (mail_shell_backend_notify_online_cb),
		shell_backend);

	g_signal_connect (
		shell, "handle-uri",
		G_CALLBACK (mail_shell_backend_handle_uri_cb),
		shell_backend);

	g_signal_connect (
		shell, "prepare-for-offline",
		G_CALLBACK (mail_shell_backend_prepare_for_offline_cb),
		shell_backend);

	g_signal_connect (
		shell, "prepare-for-online",
		G_CALLBACK (mail_shell_backend_prepare_for_online_cb),
		shell_backend);

	g_signal_connect (
		shell, "prepare-for-quit",
		G_CALLBACK (mail_shell_backend_prepare_for_quit_cb),
		shell_backend);

	g_signal_connect (
		shell, "quit-requested",
		G_CALLBACK (mail_shell_backend_quit_requested_cb),
		shell_backend);

	g_signal_connect (
		shell, "send-receive",
		G_CALLBACK (mail_shell_backend_send_receive_cb),
		shell_backend);

	g_signal_connect (
		shell, "window-created",
		G_CALLBACK (mail_shell_backend_window_created_cb),
		shell_backend);

	mail_config_init ();
	mail_msg_init ();

	data_dir = e_shell_backend_get_data_dir (shell_backend);
	e_mail_store_init (data_dir);

	e_mail_shell_settings_init (shell);

	/* Initialize preferences after the main loop starts so
	 * that all EPlugins and EPluginHooks are loaded first. */
	g_idle_add ((GSourceFunc) mail_shell_backend_init_preferences, shell);
}

static void
mail_shell_backend_start (EShellBackend *shell_backend)
{
	EMailShellBackendPrivate *priv;
	EShell *shell;
	EShellSettings *shell_settings;
	gboolean enable_search_folders;

	priv = E_MAIL_SHELL_BACKEND_GET_PRIVATE (shell_backend);

	shell = e_shell_backend_get_shell (shell_backend);
	shell_settings = e_shell_get_shell_settings (shell);

	/* XXX Do we really still need this flag? */
	mail_session_set_interactive (TRUE);

	enable_search_folders = e_shell_settings_get_boolean (
		shell_settings, "mail-enable-search-folders");
	if (enable_search_folders)
		vfolder_load_storage ();

	mail_autoreceive_init (shell_backend, session);

	if (g_getenv ("CAMEL_FLUSH_CHANGES") != NULL)
		priv->mail_sync_timeout_source_id = g_timeout_add_seconds (
			mail_config_get_sync_timeout (),
			(GSourceFunc) mail_shell_backend_mail_sync,
			shell_backend);
}

static void
mail_shell_backend_class_init (EMailShellBackendClass *class)
{
	GObjectClass *object_class;
	EShellBackendClass *shell_backend_class;

	parent_class = g_type_class_peek_parent (class);
	g_type_class_add_private (class, sizeof (EMailShellBackendPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->constructed = mail_shell_backend_constructed;

	shell_backend_class = E_SHELL_BACKEND_CLASS (class);
	shell_backend_class->shell_view_type = E_TYPE_MAIL_SHELL_VIEW;
	shell_backend_class->name = BACKEND_NAME;
	shell_backend_class->aliases = "";
	shell_backend_class->schemes = "mailto:email";
	shell_backend_class->sort_order = 200;
	shell_backend_class->start = mail_shell_backend_start;
	shell_backend_class->migrate = e_mail_shell_migrate;
}

static void
mail_shell_backend_init (EMailShellBackend *mail_shell_backend)
{
	mail_shell_backend->priv =
		E_MAIL_SHELL_BACKEND_GET_PRIVATE (mail_shell_backend);
}

GType
e_mail_shell_backend_get_type (void)
{
	return mail_shell_backend_type;
}

void
e_mail_shell_backend_register_type (GTypeModule *type_module)
{
	const GTypeInfo type_info = {
		sizeof (EMailShellBackendClass),
		(GBaseInitFunc) NULL,
		(GBaseFinalizeFunc) NULL,
		(GClassInitFunc) mail_shell_backend_class_init,
		(GClassFinalizeFunc) NULL,
		NULL,  /* class_data */
		sizeof (EMailShellBackend),
		0,     /* n_preallocs */
		(GInstanceInitFunc) mail_shell_backend_init,
		NULL   /* value_table */
	};

	mail_shell_backend_type = g_type_module_register_type (
		type_module, E_TYPE_SHELL_BACKEND,
		"EMailShellBackend", &type_info, 0);
}

/******************* Code below here belongs elsewhere. *******************/

#include "filter/filter-option.h"
#include "shell/e-shell-settings.h"
#include "mail/e-mail-label-list-store.h"

GSList *
e_mail_labels_get_filter_options (void)
{
	EShell *shell;
	EShellSettings *shell_settings;
	EMailLabelListStore *list_store;
	GtkTreeModel *model;
	GtkTreeIter iter;
	GSList *list = NULL;
	gboolean valid;

	shell = e_shell_get_default ();
	shell_settings = e_shell_get_shell_settings (shell);
	list_store = e_shell_settings_get_object (
		shell_settings, "mail-label-list-store");

	model = GTK_TREE_MODEL (list_store);
	valid = gtk_tree_model_get_iter_first (model, &iter);

	while (valid) {
		struct _filter_option *option;
		gchar *name, *tag;

		name = e_mail_label_list_store_get_name (list_store, &iter);
		tag = e_mail_label_list_store_get_tag (list_store, &iter);

		option = g_new0 (struct _filter_option, 1);
		option->title = e_str_without_underscores (name);
		option->value = tag;  /* takes ownership */

		g_free (name);

		valid = gtk_tree_model_iter_next (model, &iter);
	}

	g_object_unref (list_store);

	return list;
}
