/*
 * e-mail-folder-pane.c
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
 */

#include "e-mail-folder-pane.h"

#include "e-util/e-util.h"

#include "mail/em-utils.h"
#include "mail/message-list.h"
#include "mail/mail-ops.h"

#define E_MAIL_FOLDER_PANE_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_MAIL_FOLDER_PANE, EMailFolderPanePrivate))

struct _EMailFolderPanePrivate {
	EShellBackend *shell_backend;

	GtkWidget *message_list;  /* not referenced */

	guint group_by_threads : 1;
	guint hide_deleted     : 1;
};

enum {
	PROP_0,
	PROP_FOLDER,
	PROP_FOLDER_URI,
	PROP_GROUP_BY_THREADS,
	PROP_HIDE_DELETED,
	PROP_MESSAGE_LIST,
	PROP_SHELL_BACKEND
};

static gpointer parent_class;

static void
mail_folder_pane_set_shell_backend (EMailFolderPane *folder_pane,
                                    EShellBackend *shell_backend)
{
	g_return_if_fail (E_IS_SHELL_BACKEND (shell_backend));
	g_return_if_fail (folder_pane->priv->shell_backend == NULL);

	folder_pane->priv->shell_backend = g_object_ref (shell_backend);
}

static void
mail_folder_pane_set_property (GObject *object,
                               guint property_id,
                               const GValue *value,
                               GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_GROUP_BY_THREADS:
			e_mail_folder_pane_set_group_by_threads (
				E_MAIL_FOLDER_PANE (object),
				g_value_get_boolean (value));
			return;

		case PROP_HIDE_DELETED:
			e_mail_folder_pane_set_hide_deleted (
				E_MAIL_FOLDER_PANE (object),
				g_value_get_boolean (value));
			return;

		case PROP_SHELL_BACKEND:
			mail_folder_pane_set_shell_backend (
				E_MAIL_FOLDER_PANE (object),
				g_value_get_object (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
mail_folder_pane_get_property (GObject *object,
                               guint property_id,
                               GValue *value,
                               GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_FOLDER:
			g_value_set_boxed (
				value,
				e_mail_folder_pane_get_folder (
				E_MAIL_FOLDER_PANE (object)));
			return;

		case PROP_FOLDER_URI:
			g_value_set_string (
				value,
				e_mail_folder_pane_get_folder_uri (
				E_MAIL_FOLDER_PANE (object)));
			return;

		case PROP_GROUP_BY_THREADS:
			g_value_set_boolean (
				value,
				e_mail_folder_pane_get_group_by_threads (
				E_MAIL_FOLDER_PANE (object)));
			return;

		case PROP_HIDE_DELETED:
			g_value_set_boolean (
				value,
				e_mail_folder_pane_get_hide_deleted (
				E_MAIL_FOLDER_PANE (object)));
			return;

		case PROP_MESSAGE_LIST:
			g_value_set_object (
				value,
				e_mail_folder_pane_get_message_list (
				E_MAIL_FOLDER_PANE (object)));
			return;

		case PROP_SHELL_BACKEND:
			g_value_set_object (
				value,
				e_mail_folder_pane_get_shell_backend (
				E_MAIL_FOLDER_PANE (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
mail_folder_pane_dispose (GObject *object)
{
	EMailFolderPanePrivate *priv;

	priv = E_MAIL_FOLDER_PANE_GET_PRIVATE (object);

	if (priv->shell_backend != NULL) {
		g_object_unref (priv->shell_backend);
		priv->shell_backend = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
mail_folder_pane_constructed (GObject *object)
{
	EMailFolderPanePrivate *priv;
	GtkWidget *container;
	GtkWidget *widget;

	priv = E_MAIL_FOLDER_PANE_GET_PRIVATE (object);

	container = GTK_WIDGET (object);

	widget = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (
		GTK_SCROLLED_WINDOW (widget),
		GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
	gtk_scrolled_window_set_shadow_type (
		GTK_SCROLLED_WINDOW (widget), GTK_SHADOW_IN);
	gtk_box_pack_start (GTK_BOX (container), widget, TRUE, TRUE, 0);
	gtk_widget_show (widget);

	container = widget;

	widget = message_list_new (priv->shell_backend);
	gtk_container_add (GTK_CONTAINER (container), widget);
	priv->message_list = widget;
	gtk_widget_show (widget);
}

static void
mail_folder_pane_class_init (EMailFolderPaneClass *class)
{
	GObjectClass *object_class;

	parent_class = g_type_class_peek_parent (class);
	g_type_class_add_private (class, sizeof (EMailFolderPanePrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = mail_folder_pane_set_property;
	object_class->get_property = mail_folder_pane_get_property;
	object_class->dispose = mail_folder_pane_dispose;
	object_class->constructed = mail_folder_pane_constructed;

	g_object_class_install_property (
		object_class,
		PROP_FOLDER,
		g_param_spec_boxed (
			"folder",
			NULL,
			NULL,
			E_TYPE_CAMEL_OBJECT,
			G_PARAM_READABLE));

	g_object_class_install_property (
		object_class,
		PROP_FOLDER_URI,
		g_param_spec_string (
			"folder-uri",
			NULL,
			NULL,
			NULL,
			G_PARAM_READABLE));

	g_object_class_install_property (
		object_class,
		PROP_GROUP_BY_THREADS,
		g_param_spec_boolean (
			"group-by-threads",
			NULL,
			NULL,
			FALSE,
			G_PARAM_READWRITE));

	g_object_class_install_property (
		object_class,
		PROP_HIDE_DELETED,
		g_param_spec_boolean (
			"hide-deleted",
			NULL,
			NULL,
			TRUE,
			G_PARAM_READWRITE));

	g_object_class_install_property (
		object_class,
		PROP_MESSAGE_LIST,
		g_param_spec_object (
			"message-list",
			NULL,
			NULL,
			GTK_TYPE_WIDGET,
			G_PARAM_READABLE));

	g_object_class_install_property (
		object_class,
		PROP_SHELL_BACKEND,
		g_param_spec_object (
			"shell-backend",
			NULL,
			NULL,
			E_TYPE_SHELL_BACKEND,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY));
}

static void
mail_folder_pane_init (EMailFolderPane *folder_pane)
{
	folder_pane->priv = E_MAIL_FOLDER_PANE_GET_PRIVATE (folder_pane);
}

GType
e_mail_folder_pane_get_type (void)
{
	static GType type = 0;

	if (G_UNLIKELY (type == 0)) {
		static const GTypeInfo type_info = {
			sizeof (EMailFolderPaneClass),
			(GBaseInitFunc) NULL,
			(GBaseFinalizeFunc) NULL,
			(GClassInitFunc) mail_folder_pane_class_init,
			(GClassFinalizeFunc) NULL,
			NULL,  /* class_data */
			sizeof (EMailFolderPane),
			0,     /* n_preallocs */
			(GInstanceInitFunc) mail_folder_pane_init,
			NULL   /* value_table */
		};

		type = g_type_register_static (
			GTK_TYPE_VBOX, "EMailFolderPane", &type_info, 0);
	}

	return type;
}

GtkWidget *
e_mail_folder_pane_new (EShellBackend *shell_backend)
{
	g_return_val_if_fail (E_IS_SHELL_BACKEND (shell_backend), NULL);

	return g_object_new (
		E_TYPE_MAIL_FOLDER_PANE,
		"shell-backend", shell_backend, NULL);
}

CamelFolder *
e_mail_folder_pane_get_folder (EMailFolderPane *folder_pane)
{
	GtkWidget *message_list;

	g_return_val_if_fail (E_IS_MAIL_FOLDER_PANE (folder_pane), NULL);

	message_list = e_mail_folder_pane_get_message_list (folder_pane);

	return MESSAGE_LIST (message_list)->folder;
}

const gchar *
e_mail_folder_pane_get_folder_uri (EMailFolderPane *folder_pane)
{
	GtkWidget *message_list;

	g_return_val_if_fail (E_IS_MAIL_FOLDER_PANE (folder_pane), NULL);

	message_list = e_mail_folder_pane_get_message_list (folder_pane);

	return MESSAGE_LIST (message_list)->folder_uri;
}

void
e_mail_folder_pane_set_folder (EMailFolderPane *folder_pane,
                               CamelFolder *folder,
                               const gchar *folder_uri)
{
	GtkWidget *message_list;
	CamelFolder *previous_folder;
	const gchar *previous_folder_uri;
	gboolean outgoing;

	g_return_if_fail (E_IS_MAIL_FOLDER_PANE (folder_pane));
	g_return_if_fail (folder == NULL || CAMEL_IS_FOLDER (folder));

	message_list = e_mail_folder_pane_get_message_list (folder_pane);

	previous_folder = e_mail_folder_pane_get_folder (folder_pane);
	previous_folder_uri = e_mail_folder_pane_get_folder_uri (folder_pane);

	if (previous_folder != NULL)
		mail_sync_folder (previous_folder, NULL, NULL);

	/* Skip the rest if we're already viewing the folder. */
	if (g_strcmp0 (folder_uri, previous_folder_uri) == 0)
		return;

	outgoing = folder != NULL && folder_uri != NULL && (
		em_utils_folder_is_drafts (folder, folder_uri) ||
		em_utils_folder_is_outbox (folder, folder_uri) ||
		em_utils_folder_is_sent (folder, folder_uri));

	message_list_set_folder (
		MESSAGE_LIST (message_list), folder, folder_uri, outgoing);

	g_object_freeze_notify (G_OBJECT (folder_pane));
	g_object_notify (G_OBJECT (folder_pane), "folder");
	g_object_notify (G_OBJECT (folder_pane), "folder-uri");
	g_object_thaw_notify (G_OBJECT (folder_pane));
}

gboolean
e_mail_folder_pane_get_group_by_threads (EMailFolderPane *folder_pane)
{
	g_return_val_if_fail (E_IS_MAIL_FOLDER_PANE (folder_pane), FALSE);

	return folder_pane->priv->group_by_threads;
}

void
e_mail_folder_pane_set_group_by_threads (EMailFolderPane *folder_pane,
                                         gboolean group_by_threads)
{
	GtkWidget *message_list;

	g_return_if_fail (E_IS_MAIL_FOLDER_PANE (folder_pane));

	folder_pane->priv->group_by_threads = group_by_threads;

	/* XXX MessageList should define a property for this. */
	message_list = e_mail_folder_pane_get_message_list (folder_pane);
	message_list_set_threaded (
		MESSAGE_LIST (message_list), group_by_threads);

	g_object_notify (G_OBJECT (folder_pane), "group-by-threads");
}

gboolean
e_mail_folder_pane_get_hide_deleted (EMailFolderPane *folder_pane)
{
	g_return_val_if_fail (E_IS_MAIL_FOLDER_PANE (folder_pane), FALSE);

	return folder_pane->priv->hide_deleted;
}

void
e_mail_folder_pane_set_hide_deleted (EMailFolderPane *folder_pane,
                                     gboolean hide_deleted)
{
	GtkWidget *message_list;

	g_return_if_fail (E_IS_MAIL_FOLDER_PANE (folder_pane));

	folder_pane->priv->hide_deleted = hide_deleted;

	/* XXX MessageList should define a property for this. */
	message_list = e_mail_folder_pane_get_message_list (folder_pane);
	message_list_set_hidedeleted (
		MESSAGE_LIST (message_list), hide_deleted);

	g_object_notify (G_OBJECT (folder_pane), "hide-deleted");
}

GtkWidget *
e_mail_folder_pane_get_message_list (EMailFolderPane *folder_pane)
{
	g_return_val_if_fail (E_IS_MAIL_FOLDER_PANE (folder_pane), NULL);

	return folder_pane->priv->message_list;
}

EShellBackend *
e_mail_folder_pane_get_shell_backend (EMailFolderPane *folder_pane)
{
	g_return_val_if_fail (E_IS_MAIL_FOLDER_PANE (folder_pane), NULL);

	return folder_pane->priv->shell_backend;
}
