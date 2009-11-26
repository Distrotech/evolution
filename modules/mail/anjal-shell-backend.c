/*
 * anjal-shell-backend.c
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

#include "anjal-shell-backend.h"

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
#include "anjal-shell-view.h"

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

#define ANJAL_SHELL_BACKEND_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), ANJAL_TYPE_SHELL_BACKEND, AnjalShellBackendPrivate))

#define BACKEND_NAME "anjal"
#define QUIT_POLL_INTERVAL 1  /* seconds */

struct _AnjalShellBackendPrivate {
	gint mail_sync_in_progress;
	guint mail_sync_timeout_source_id;
};

static gpointer parent_class;
static GType anjal_shell_backend_type;

extern gint camel_application_is_exiting;

#include "e-mail-anjal-shared.c"

static void
anjal_shell_backend_sync_store_done_cb (CamelStore *store,
                                       gpointer user_data)
{
	AnjalShellBackend *anjal_shell_backend = user_data;

	anjal_shell_backend->priv->mail_sync_in_progress--;
}

static void
anjal_shell_backend_sync_store_cb (CamelStore *store,
                                  AnjalShellBackend *anjal_shell_backend)
{
	if (!camel_application_is_exiting) {
		anjal_shell_backend->priv->mail_sync_in_progress++;
		mail_sync_store (
			store, FALSE,
			anjal_shell_backend_sync_store_done_cb,
			anjal_shell_backend);
	}
}

static gboolean
anjal_shell_backend_mail_sync (AnjalShellBackend *anjal_shell_backend)
{
	if (camel_application_is_exiting)
		return FALSE;

	if (anjal_shell_backend->priv->mail_sync_in_progress)
		goto exit;

	if (session == NULL || !camel_session_is_online (session))
		goto exit;

	e_mail_store_foreach (
		(GHFunc) anjal_shell_backend_sync_store_cb,
		anjal_shell_backend);

exit:
	return !camel_application_is_exiting;
}

static void
anjal_shell_backend_constructed (GObject *object)
{
	AnjalShellBackendPrivate *priv;
	EShell *shell;
	EShellBackend *shell_backend;
	char *custom_dir;

	priv = ANJAL_SHELL_BACKEND_GET_PRIVATE (object);

	shell_backend = E_SHELL_BACKEND (object);
	shell = e_shell_backend_get_shell (shell_backend);

	/* This also initializes Camel, so it needs to happen early. */
	mail_session_init (shell_backend);

	/* Register format types for EMFormatHook. */
	em_format_hook_register_type (em_format_get_type ());
	//em_format_hook_register_type (em_format_html_get_type ());
	//em_format_hook_register_type (em_format_html_display_get_type ());

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

	custom_dir = g_build_filename (e_get_user_data_dir (), "mail", NULL);
	e_mail_store_init (custom_dir);
	g_free (custom_dir);
	e_mail_shell_settings_init (shell);

	/* Initialize preferences after the main loop starts so
	 * that all EPlugins and EPluginHooks are loaded first. */
	g_idle_add ((GSourceFunc) mail_shell_backend_init_preferences, shell);
}

static void
anjal_shell_backend_start (EShellBackend *shell_backend)
{
	AnjalShellBackendPrivate *priv;
	EShell *shell;
	EShellSettings *shell_settings;
	gboolean enable_search_folders;

	priv = ANJAL_SHELL_BACKEND_GET_PRIVATE (shell_backend);

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
			(GSourceFunc) anjal_shell_backend_mail_sync,
			shell_backend);
}

static void
anjal_shell_backend_class_init (AnjalShellBackendClass *class)
{
	GObjectClass *object_class;
	EShellBackendClass *shell_backend_class;

	parent_class = g_type_class_peek_parent (class);
	g_type_class_add_private (class, sizeof (AnjalShellBackendPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->constructed = anjal_shell_backend_constructed;

	shell_backend_class = E_SHELL_BACKEND_CLASS (class);
	shell_backend_class->shell_view_type = ANJAL_TYPE_SHELL_VIEW;
	shell_backend_class->name = BACKEND_NAME;
	shell_backend_class->aliases = "";
	shell_backend_class->schemes = "mailto:email";
	shell_backend_class->sort_order = 200;
	shell_backend_class->preferences_page = "mail-accounts";
	shell_backend_class->start = anjal_shell_backend_start;
	shell_backend_class->migrate = e_mail_shell_migrate;
}

static void
anjal_shell_backend_init (AnjalShellBackend *mail_shell_backend)
{
	mail_shell_backend->priv =
		ANJAL_SHELL_BACKEND_GET_PRIVATE (mail_shell_backend);
}

GType
anjal_shell_backend_get_type (void)
{
	return anjal_shell_backend_type;
}

void
anjal_shell_backend_register_type (GTypeModule *type_module)
{
	const GTypeInfo type_info = {
		sizeof (AnjalShellBackendClass),
		(GBaseInitFunc) NULL,
		(GBaseFinalizeFunc) NULL,
		(GClassInitFunc) anjal_shell_backend_class_init,
		(GClassFinalizeFunc) NULL,
		NULL,  /* class_data */
		sizeof (AnjalShellBackend),
		0,     /* n_preallocs */
		(GInstanceInitFunc) anjal_shell_backend_init,
		NULL   /* value_table */
	};

	anjal_shell_backend_type = g_type_module_register_type (
		type_module, E_TYPE_SHELL_BACKEND,
		"AnjalShellBackend", &type_info, 0);
}

