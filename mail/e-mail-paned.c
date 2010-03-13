/*
 * e-mail-paned.c
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

#include "e-mail-paned.h"

#include <config.h>
#include <glib/gi18n-lib.h>

#include <e-util/e-binding.h>

#include <mail/em-format-html-display.h>
#include <mail/mail-mt.h>
#include <mail/mail-ops.h>
#include <mail/message-list.h>

#define E_MAIL_PANED_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_MAIL_PANED, EMailPanedPrivate))

struct _EMailPanedPrivate {
	EShellBackend *shell_backend;
	EMFormatHTMLDisplay *formatter;

	/* The user can elect to automatically mark a message as read
	 * after a short delay when displaying the message's content. */
	guint mark_as_read_delay;
	gboolean mark_as_read_enabled;
	gchar *mark_as_read_message_uid;

	/* This timer runs when the user selects a single message. */
	guint message_selected_timeout_id;

	/* This is the ID of an asynchronous operation
	 * to retrieve a message from a mail folder. */
	gint retrieving_message_op_id;

	/* These flags work together to prevent message selection
	 * restoration after a folder switch from automatically
	 * marking the message as read.  We only want that to
	 * happen when the -user- selects a message. */
	guint folder_was_just_selected    : 1;
	guint restoring_message_selection : 1;
};

enum {
	PROP_0,
	PROP_FOLDER_PANE,
	PROP_MARK_AS_READ_DELAY,
	PROP_MARK_AS_READ_ENABLED,
	PROP_MESSAGE_PANE,
	PROP_SHELL_BACKEND
};

enum {
	MARK_AS_READ,
	LAST_SIGNAL
};

static gpointer parent_class;
static guint signals[LAST_SIGNAL];

static void
mail_paned_load_string (EMailPaned *paned,
                        const gchar *string)
{
	EMailMessagePane *message_pane;
	EMailDisplay *display;
	EMFormat *formatter;

	formatter = EM_FORMAT (paned->priv->formatter);

	if (em_format_busy (formatter))
		return;

	message_pane = e_mail_paned_get_message_pane (paned);
	display = e_mail_message_pane_get_display (message_pane);

	e_web_view_load_string (E_WEB_VIEW (display), string);
}

static void
mail_paned_folder_changed_cb (EMailPaned *paned)
{
	EMailFolderPane *folder_pane;
	CamelFolder *folder;

	folder_pane = e_mail_paned_get_folder_pane (paned);
	folder = e_mail_folder_pane_get_folder (folder_pane);

	paned->priv->folder_was_just_selected = (folder != NULL);
}

static gboolean
mail_paned_message_read_cb (EMailPaned *paned)
{
	EMailFolderPane *folder_pane;
	GtkWidget *message_list;
	const gchar *cursor_uid;
	const gchar *message_uid;

	message_uid = paned->priv->mark_as_read_message_uid;
	g_return_val_if_fail (message_uid != NULL, FALSE);

	folder_pane = e_mail_paned_get_folder_pane (paned);
	message_list = e_mail_folder_pane_get_message_list (folder_pane);
	cursor_uid = MESSAGE_LIST (message_list)->cursor_uid;

	if (g_strcmp0 (cursor_uid, message_uid) == 0)
		g_signal_emit (paned, signals[MARK_AS_READ], 0, message_uid);

	return FALSE;
}

static void
mail_paned_message_loaded_cb (CamelFolder *folder,
                              const gchar *message_uid,
                              CamelMimeMessage *message,
                              gpointer user_data,
                              CamelException *ex)
{
	EMailPaned *paned = user_data;
	EMailFolderPane *folder_pane;
	EMailMessagePane *message_pane;
	GtkWidget *message_list;
	EMFormat *formatter;
	const gchar *cursor_uid;
	gboolean schedule_timeout;
	guint timeout_interval;

	formatter = EM_FORMAT (paned->priv->formatter);
	folder_pane = e_mail_paned_get_folder_pane (paned);
	message_pane = e_mail_paned_get_message_pane (paned);

	message_list = e_mail_folder_pane_get_message_list (folder_pane);

	cursor_uid = MESSAGE_LIST (message_list)->cursor_uid;

	/* If the user picked a different message in the time it took
	 * to fetch this message, then don't bother rendering it. */
	if (g_strcmp0 (cursor_uid, message_uid) != 0)
		goto exit;

	e_mail_message_pane_set_message (message_pane, message, message_uid);

	/* Determine whether to mark the message as read. */
	schedule_timeout =
		(message != NULL) &&
		e_mail_paned_get_mark_as_read_enabled (paned) &&
		!paned->priv->restoring_message_selection;
	timeout_interval = e_mail_paned_get_mark_as_read_delay (paned);

	g_free (paned->priv->mark_as_read_message_uid);
	paned->priv->mark_as_read_message_uid = g_strdup (message_uid);

	if (MESSAGE_LIST (message_list)->seen_id > 0) {
		g_source_remove (MESSAGE_LIST (message_list)->seen_id);
		MESSAGE_LIST (message_list)->seen_id = 0;
	}

	if (schedule_timeout)
		MESSAGE_LIST (message_list)->seen_id = g_timeout_add (
			timeout_interval, (GSourceFunc)
			mail_paned_message_read_cb, paned);

	else if (camel_exception_is_set (ex)) {
		gchar *string;

		if (ex->id != CAMEL_EXCEPTION_OPERATION_IN_PROGRESS)
			string = g_strdup_printf (
				"<h2>%s</h2><p>%s</p>",
				_("Unable to retrieve message"),
				ex->desc);
		else
			string = g_strdup_printf (
				_("Retrieving message '%s'"), cursor_uid);

		mail_paned_load_string (paned, string);
		g_free (string);

		camel_exception_clear (ex);
	}

	/* We referenced this in the call to mail_get_messagex(). */
	g_object_unref (paned);

exit:
	paned->priv->restoring_message_selection = FALSE;
}

static gboolean
mail_paned_message_selected_timeout_cb (EMailPaned *paned)
{
	EMailFolderPane *folder_pane;
	EMailMessagePane *message_pane;
	GtkWidget *message_list;
	CamelFolder *folder;
	EMFormat *formatter;
	gboolean store_async;

	formatter = EM_FORMAT (paned->priv->formatter);
	folder_pane = e_mail_paned_get_folder_pane (paned);
	message_pane = e_mail_paned_get_message_pane (paned);

	folder = e_mail_folder_pane_get_folder (folder_pane);
	message_list = e_mail_folder_pane_get_message_list (folder_pane);
	store_async = folder->parent_store->flags & CAMEL_STORE_ASYNC;

	if (MESSAGE_LIST (message_list)->last_sel_single) {
		gboolean message_pane_visible;
		gboolean selected_uid_changed;
		const gchar *cursor_uid;
		const gchar *format_uid;

		/* Decide whether to download the full message now. */

		cursor_uid = MESSAGE_LIST (message_list)->cursor_uid;
		format_uid = formatter->uid;

#if GTK_CHECK_VERSION(2,19,7)
		message_pane_visible =
			gtk_widget_get_visible (GTK_WIDGET (message_pane));
#else
		message_pane_visible = GTK_WIDGET_VISIBLE (message_pane);
#endif
		selected_uid_changed = g_strcmp0 (cursor_uid, format_uid);

		if (message_pane_visible && selected_uid_changed) {
			MailMsgDispatchFunc dispatch_func;
			gchar *string;
			gint op_id;

			string = g_strdup_printf (
				_("Retrieving message '%s'"), cursor_uid);
			mail_paned_load_string (paned, string);
			g_free (string);

			if (store_async)
				dispatch_func = mail_msg_unordered_push;
			else
				dispatch_func = mail_msg_fast_ordered_push;

			op_id = mail_get_messagex (
				folder, cursor_uid,
				mail_paned_message_loaded_cb,
				g_object_ref (paned),
				dispatch_func);

			if (!store_async)
				paned->priv->retrieving_message_op_id = op_id;
		}
	} else {
		e_mail_message_pane_set_message (message_pane, NULL, NULL);
		paned->priv->restoring_message_selection = FALSE;
	}

	paned->priv->message_selected_timeout_id = 0;

	return FALSE;
}

static void
mail_paned_message_selected_cb (EMailPaned *paned,
                                const gchar *uid,
                                MessageList *message_list)
{
	EMailFolderPane *folder_pane;
	CamelFolder *folder;
	gboolean store_async;

	folder_pane = e_mail_paned_get_folder_pane (paned);

	folder = e_mail_folder_pane_get_folder (folder_pane);
	store_async = folder->parent_store->flags & CAMEL_STORE_ASYNC;

	/* Cancel previous message retrieval if the store is not async. */
	if (!store_async && paned->priv->retrieving_message_op_id > 0)
		mail_msg_cancel (paned->priv->retrieving_message_op_id);

	/* Cancel the seen timer. */
	if (message_list->seen_id > 0) {
		g_source_remove (message_list->seen_id);
		message_list->seen_id = 0;
	}

	/* Cancel the message selected timer. */
	if (paned->priv->message_selected_timeout_id > 0) {
		g_source_remove (paned->priv->message_selected_timeout_id);
		paned->priv->message_selected_timeout_id = 0;
	}

	/* If a folder was just selected then we are not automatically
	 * restoring the previous message selection.  We behave slightly
	 * differently than if the user had selected the message. */
	paned->priv->restoring_message_selection =
		paned->priv->folder_was_just_selected;
	paned->priv->folder_was_just_selected = FALSE;

	/* Skip the timeout if we're restoring the previous message
	 * selection.  The timeout is there for when we're scrolling
	 * rapidly through the message list. */
	if (paned->priv->restoring_message_selection)
		mail_paned_message_selected_timeout_cb (paned);
	else
		paned->priv->message_selected_timeout_id = g_timeout_add (
			100, (GSourceFunc)
			mail_paned_message_selected_timeout_cb, paned);
}

static void
mail_paned_set_shell_backend (EMailPaned *paned,
                              EShellBackend *shell_backend)
{
	g_return_if_fail (E_IS_SHELL_BACKEND (shell_backend));
	g_return_if_fail (paned->priv->shell_backend == NULL);

	paned->priv->shell_backend = g_object_ref (shell_backend);
}

static void
mail_paned_set_property (GObject *object,
                         guint property_id,
                         const GValue *value,
                         GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_MARK_AS_READ_DELAY:
			e_mail_paned_set_mark_as_read_delay (
				E_MAIL_PANED (object),
				g_value_get_uint (value));
			return;

		case PROP_MARK_AS_READ_ENABLED:
			e_mail_paned_set_mark_as_read_enabled (
				E_MAIL_PANED (object),
				g_value_get_boolean (value));
			return;

		case PROP_SHELL_BACKEND:
			mail_paned_set_shell_backend (
				E_MAIL_PANED (object),
				g_value_get_object (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
mail_paned_get_property (GObject *object,
                         guint property_id,
                         GValue *value,
                         GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_FOLDER_PANE:
			g_value_set_object (
				value,
				e_mail_paned_get_folder_pane (
				E_MAIL_PANED (object)));
			return;

		case PROP_MARK_AS_READ_DELAY:
			g_value_set_uint (
				value,
				e_mail_paned_get_mark_as_read_delay (
				E_MAIL_PANED (object)));
			return;

		case PROP_MARK_AS_READ_ENABLED:
			g_value_set_boolean (
				value,
				e_mail_paned_get_mark_as_read_enabled (
				E_MAIL_PANED (object)));
			return;

		case PROP_MESSAGE_PANE:
			g_value_set_object (
				value,
				e_mail_paned_get_message_pane (
				E_MAIL_PANED (object)));
			return;

		case PROP_SHELL_BACKEND:
			g_value_set_object (
				value,
				e_mail_paned_get_shell_backend (
				E_MAIL_PANED (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
mail_paned_dispose (GObject *object)
{
	EMailPanedPrivate *priv;

	priv = E_MAIL_PANED_GET_PRIVATE (object);

	if (priv->shell_backend != NULL) {
		g_object_unref (priv->shell_backend);
		priv->shell_backend = NULL;
	}

	if (priv->formatter != NULL) {
		g_object_unref (priv->formatter);
		priv->formatter = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
mail_paned_finalize (GObject *object)
{
	EMailPanedPrivate *priv;

	priv = E_MAIL_PANED_GET_PRIVATE (object);

	if (priv->message_selected_timeout_id > 0)
		g_source_remove (priv->message_selected_timeout_id);

	g_free (priv->mark_as_read_message_uid);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
mail_paned_constructed (GObject *object)
{
	EMailPanedPrivate *priv;
	EMailDisplay *display;
	EMailFolderPane *folder_pane;
	EMailMessagePane *message_pane;
	GtkWidget *message_list;
	GtkWidget *widget;

	priv = E_MAIL_PANED_GET_PRIVATE (object);

	display = E_MAIL_DISPLAY (EM_FORMAT_HTML (priv->formatter)->html);

	widget = e_mail_folder_pane_new (priv->shell_backend);
	gtk_paned_pack1 (GTK_PANED (object), widget, TRUE, FALSE);
	folder_pane = E_MAIL_FOLDER_PANE (widget);
	gtk_widget_show (widget);

	widget = e_mail_message_pane_new (display);
	gtk_paned_pack2 (GTK_PANED (object), widget, FALSE, FALSE);
	message_pane = E_MAIL_MESSAGE_PANE (widget);
	gtk_widget_show (widget);

	message_list = e_mail_folder_pane_get_message_list (folder_pane);

	/* Connect signals. */

	g_signal_connect_swapped (
		folder_pane, "notify::folder",
		G_CALLBACK (mail_paned_folder_changed_cb), object);

	g_signal_connect_swapped (
		message_list, "message-selected",
		G_CALLBACK (mail_paned_message_selected_cb), object);

	e_binding_new (folder_pane, "folder", message_pane, "folder");
}

static void
mail_paned_class_init (EMailPanedClass *class)
{
	GObjectClass *object_class;

	parent_class = g_type_class_peek_parent (class);
	g_type_class_add_private (class, sizeof (EMailPanedPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = mail_paned_set_property;
	object_class->get_property = mail_paned_get_property;
	object_class->dispose = mail_paned_dispose;
	object_class->finalize = mail_paned_finalize;
	object_class->constructed = mail_paned_constructed;

	g_object_class_install_property (
		object_class,
		PROP_FOLDER_PANE,
		g_param_spec_object (
			"folder-pane",
			NULL,
			NULL,
			E_TYPE_MAIL_FOLDER_PANE,
			G_PARAM_READABLE));

	g_object_class_install_property (
		object_class,
		PROP_MARK_AS_READ_DELAY,
		g_param_spec_uint (
			"mark-as-read-delay",
			NULL,
			NULL,
			0,
			G_MAXUINT,
			1500,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT));

	g_object_class_install_property (
		object_class,
		PROP_MARK_AS_READ_ENABLED,
		g_param_spec_boolean (
			"mark-as-read-enabled",
			NULL,
			NULL,
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT));

	g_object_class_install_property (
		object_class,
		PROP_MESSAGE_PANE,
		g_param_spec_object (
			"message-pane",
			NULL,
			NULL,
			E_TYPE_MAIL_MESSAGE_PANE,
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

	signals[MARK_AS_READ] = g_signal_new (
		"mark-as-read",
		G_OBJECT_CLASS_TYPE (class),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (EMailPanedClass, mark_as_read),
		NULL, NULL,
		g_cclosure_marshal_VOID__STRING,
		G_TYPE_NONE, 1,
		G_TYPE_STRING);
}

static void
mail_paned_init (EMailPaned *paned)
{
	paned->priv = E_MAIL_PANED_GET_PRIVATE (paned);

	/* EMFormatHTMLDisplay is hard-wired to create its own
	 * EMailDisplay instance, which we will share with the
	 * EMailMessagePane widget.  Kind of confusing. */
	paned->priv->formatter = em_format_html_display_new ();
}

GType
e_mail_paned_get_type (void)
{
	static GType type = 0;

	if (G_UNLIKELY (type == 0)) {
		static const GTypeInfo type_info = {
			sizeof (EMailPanedClass),
			(GBaseInitFunc) NULL,
			(GBaseFinalizeFunc) NULL,
			(GClassInitFunc) mail_paned_class_init,
			(GClassFinalizeFunc) NULL,
			NULL,  /* class_data */
			sizeof (EMailPaned),
			0,     /* n_preallocs */
			(GInstanceInitFunc) mail_paned_init,
			NULL   /* value_table */
		};

		type = g_type_register_static (
			E_TYPE_PANED, "EMailPaned", &type_info, 0);
	}

	return type;
}

GtkWidget *
e_mail_paned_new (EShellBackend *shell_backend)
{
	g_return_val_if_fail (E_IS_SHELL_BACKEND (shell_backend), NULL);

	return g_object_new (
		E_TYPE_MAIL_PANED, "shell-backend", shell_backend, NULL);
}

EShellBackend *
e_mail_paned_get_shell_backend (EMailPaned *paned)
{
	g_return_val_if_fail (E_IS_SHELL_BACKEND (paned), NULL);

	return paned->priv->shell_backend;
}

EMailFolderPane *
e_mail_paned_get_folder_pane (EMailPaned *paned)
{
	GtkWidget *child;

	g_return_val_if_fail (E_IS_MAIL_PANED (paned), NULL);

	child = gtk_paned_get_child1 (GTK_PANED (paned));

	return E_MAIL_FOLDER_PANE (child);
}

EMailMessagePane *
e_mail_paned_get_message_pane (EMailPaned *paned)
{
	GtkWidget *child;

	g_return_val_if_fail (E_IS_MAIL_PANED (paned), NULL);

	child = gtk_paned_get_child2 (GTK_PANED (paned));

	return E_MAIL_MESSAGE_PANE (child);
}

guint
e_mail_paned_get_mark_as_read_delay (EMailPaned *paned)
{
	g_return_val_if_fail (E_IS_MAIL_PANED (paned), 0);

	return paned->priv->mark_as_read_delay;
}

void
e_mail_paned_set_mark_as_read_delay (EMailPaned *paned,
                                     guint mark_as_read_delay)
{
	g_return_if_fail (E_IS_MAIL_PANED (paned));

	paned->priv->mark_as_read_delay = mark_as_read_delay;

	g_object_notify (G_OBJECT (paned), "mark-as-read-delay");
}

gboolean
e_mail_paned_get_mark_as_read_enabled (EMailPaned *paned)
{
	g_return_val_if_fail (E_IS_MAIL_PANED (paned), FALSE);

	return paned->priv->mark_as_read_enabled;
}

void
e_mail_paned_set_mark_as_read_enabled (EMailPaned *paned,
                                       gboolean mark_as_read_enabled)
{
	g_return_if_fail (E_IS_MAIL_PANED (paned));

	paned->priv->mark_as_read_enabled = mark_as_read_enabled;

	g_object_notify (G_OBJECT (paned), "mark-as-read-enabled");
}

