/*
 * e-mail-message-pane.c
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

#include "e-mail-message-pane.h"

#include <e-util/e-util.h>

#include <mail/em-event.h>

#define E_MAIL_MESSAGE_PANE_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_MAIL_MESSAGE_PANE, EMailMessagePanePrivate))

struct _EMailMessagePanePrivate {
	CamelFolder *folder;
	CamelMimeMessage *message;
	gchar *message_uid;
};

enum {
	PROP_0,
	PROP_FOLDER,
	PROP_MESSAGE,
	PROP_MESSAGE_UID
};

static gpointer parent_class;

static void
mail_message_pane_format_message (EMailMessagePane *message_pane)
{
	EMailDisplay *display;
	EMFormatHTML *formatter;
	CamelFolder *folder;
	CamelMimeMessage *message;
	const gchar *message_uid;

	folder = e_mail_message_pane_get_folder (message_pane);
	message = e_mail_message_pane_get_message (message_pane);
	message_uid = e_mail_message_pane_get_message_uid (message_pane);

	display = e_mail_message_pane_get_display (message_pane);
	formatter = e_mail_display_get_formatter (display);

	if (folder != NULL && message != NULL && message_uid != NULL)
		em_format_format (
			EM_FORMAT (formatter), folder,
			message_uid, message);
	else
		em_format_format (
			EM_FORMAT (formatter), NULL, NULL, NULL);
}

static void
mail_message_pane_reading_event (EMailMessagePane *message_pane)
{
	EMEvent *event;
	EMEventTargetMessage *target;
	CamelFolder *folder;
	CamelMimeMessage *message;
	const gchar *message_uid;

	folder = e_mail_message_pane_get_folder (message_pane);
	message = e_mail_message_pane_get_message (message_pane);
	message_uid = e_mail_message_pane_get_message_uid (message_pane);

	if (message == NULL)
		return;

	/* @Event: message.reading
	 * @Title: Viewing a message
	 * @Target: EMEventTargetMessage
	 *
	 * message.reading is emitted whenever a user views a message.
	 */
	event = em_event_peek ();
	target = em_event_target_new_message (
		event, folder, message, message_uid, 0, NULL);
	e_event_emit (
		(EEvent *) event, "message.reading",
		(EEventTarget *) target);
}

static void
mail_message_pane_set_property (GObject *object,
                                guint property_id,
                                const GValue *value,
                                GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_FOLDER:
			e_mail_message_pane_set_folder (
				E_MAIL_MESSAGE_PANE (object),
				g_value_get_boxed (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
mail_message_pane_get_property (GObject *object,
                                guint property_id,
                                GValue *value,
                                GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_FOLDER:
			g_value_set_boxed (
				value,
				e_mail_message_pane_get_folder (
				E_MAIL_MESSAGE_PANE (object)));
			return;

		case PROP_MESSAGE:
			g_value_set_boxed (
				value,
				e_mail_message_pane_get_message (
				E_MAIL_MESSAGE_PANE (object)));
			return;

		case PROP_MESSAGE_UID:
			g_value_set_string (
				value,
				e_mail_message_pane_get_message_uid (
				E_MAIL_MESSAGE_PANE (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
mail_message_pane_dispose (GObject *object)
{
	EMailMessagePanePrivate *priv;

	priv = E_MAIL_MESSAGE_PANE_GET_PRIVATE (object);

	if (priv->folder != NULL) {
		camel_object_unref (priv->folder);
		priv->folder = NULL;
	}

	if (priv->message != NULL) {
		camel_object_unref (priv->message);
		priv->message = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
mail_message_pane_finalize (GObject *object)
{
	EMailMessagePanePrivate *priv;

	priv = E_MAIL_MESSAGE_PANE_GET_PRIVATE (object);

	g_free (priv->message_uid);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
mail_message_pane_constructed (GObject *object)
{
	EMailMessagePane *message_pane;
	EPreviewPane *preview_pane;
	EMFormatHTML *formatter;
	ESearchBar *search_bar;
	EMailDisplay *display;

	/* Chain up to parent's constructed() method. */
	G_OBJECT_CLASS (parent_class)->constructed (object);

	preview_pane = E_PREVIEW_PANE (object);
	search_bar = e_preview_pane_get_search_bar (preview_pane);

	message_pane = E_MAIL_MESSAGE_PANE (object);
	display = e_mail_message_pane_get_display (message_pane);
	formatter = e_mail_display_get_formatter (display);

	g_signal_connect_swapped (
		search_bar, "changed",
		G_CALLBACK (em_format_redraw), formatter);
}

static void
mail_message_pane_class_init (EMailMessagePaneClass *class)
{
	GObjectClass *object_class;

	parent_class = g_type_class_peek_parent (class);
	g_type_class_add_private (class, sizeof (EMailMessagePanePrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = mail_message_pane_set_property;
	object_class->get_property = mail_message_pane_get_property;
	object_class->dispose = mail_message_pane_dispose;
	object_class->finalize = mail_message_pane_finalize;
	object_class->constructed = mail_message_pane_constructed;

	g_object_class_install_property (
		object_class,
		PROP_FOLDER,
		g_param_spec_boxed (
			"folder",
			NULL,
			NULL,
			E_TYPE_CAMEL_OBJECT,
			G_PARAM_READWRITE));

	g_object_class_install_property (
		object_class,
		PROP_MESSAGE,
		g_param_spec_boxed (
			"message",
			NULL,
			NULL,
			E_TYPE_CAMEL_OBJECT,
			G_PARAM_READABLE));

	g_object_class_install_property (
		object_class,
		PROP_MESSAGE_UID,
		g_param_spec_string (
			"message-uid",
			NULL,
			NULL,
			NULL,
			G_PARAM_READABLE));
}

static void
mail_message_pane_init (EMailMessagePane *message_pane)
{
	message_pane->priv = E_MAIL_MESSAGE_PANE_GET_PRIVATE (message_pane);

	g_signal_connect (
		message_pane, "notify::message",
		G_CALLBACK (mail_message_pane_reading_event), NULL);

	g_signal_connect (
		message_pane, "notify::message",
		G_CALLBACK (mail_message_pane_format_message), NULL);
}

GType
e_mail_message_pane_get_type (void)
{
	static GType type = 0;

	if (G_UNLIKELY (type == 0)) {
		static const GTypeInfo type_info = {
			sizeof (EMailMessagePaneClass),
			(GBaseInitFunc) NULL,
			(GBaseFinalizeFunc) NULL,
			(GClassInitFunc) mail_message_pane_class_init,
			(GClassFinalizeFunc) NULL,
			NULL,  /* class_data */
			sizeof (EMailMessagePane),
			0,     /* n_preallocs */
			(GInstanceInitFunc) mail_message_pane_init,
			NULL   /* value_table */
		};

		type = g_type_register_static (
			E_TYPE_PREVIEW_PANE, "EMailMessagePane",
			&type_info, 0);
	}

	return type;
}

GtkWidget *
e_mail_message_pane_new (EMailDisplay *display)
{
	g_return_val_if_fail (E_IS_MAIL_DISPLAY (display), NULL);

	return g_object_new (
		E_TYPE_MAIL_MESSAGE_PANE, "web-view", display, NULL);
}

EMailDisplay *
e_mail_message_pane_get_display (EMailMessagePane *message_pane)
{
	EPreviewPane *preview_pane;
	EWebView *web_view;

	/* This is purely a convenience function. */

	g_return_val_if_fail (E_IS_MAIL_MESSAGE_PANE (message_pane), NULL);

	preview_pane = E_PREVIEW_PANE (message_pane);
	web_view = e_preview_pane_get_web_view (preview_pane);

	return E_MAIL_DISPLAY (web_view);
}

CamelFolder *
e_mail_message_pane_get_folder (EMailMessagePane *message_pane)
{
	g_return_val_if_fail (E_IS_MAIL_MESSAGE_PANE (message_pane), NULL);

	return message_pane->priv->folder;
}

void
e_mail_message_pane_set_folder (EMailMessagePane *message_pane,
                                CamelFolder *folder)
{
	g_return_if_fail (E_IS_MAIL_MESSAGE_PANE (message_pane));

	if (folder != NULL) {
		g_return_if_fail (CAMEL_IS_FOLDER (folder));
		camel_object_ref (folder);
	}

	if (message_pane->priv->folder != NULL)
		camel_object_unref (message_pane->priv->folder);

	message_pane->priv->folder = folder;

	/* Changing folders resets the message. */

	if (message_pane->priv->message != NULL) {
		camel_object_unref (message_pane->priv->message);
		g_free (message_pane->priv->message_uid);
	}

	message_pane->priv->message = NULL;
	message_pane->priv->message_uid = NULL;

	g_object_freeze_notify (G_OBJECT (message_pane));
	g_object_notify (G_OBJECT (message_pane), "folder");
	g_object_notify (G_OBJECT (message_pane), "message");
	g_object_notify (G_OBJECT (message_pane), "message-uid");
	g_object_thaw_notify (G_OBJECT (message_pane));
}

CamelMimeMessage *
e_mail_message_pane_get_message (EMailMessagePane *message_pane)
{
	g_return_val_if_fail (E_IS_MAIL_MESSAGE_PANE (message_pane), NULL);

	return message_pane->priv->message;
}

const gchar *
e_mail_message_pane_get_message_uid (EMailMessagePane *message_pane)
{
	g_return_val_if_fail (E_IS_MAIL_MESSAGE_PANE (message_pane), NULL);

	return message_pane->priv->message_uid;
}

void
e_mail_message_pane_set_message (EMailMessagePane *message_pane,
                                 CamelMimeMessage *message,
                                 const gchar *message_uid)
{
	g_return_if_fail (E_IS_MAIL_MESSAGE_PANE (message_pane));

	if (message != NULL) {
		g_return_if_fail (CAMEL_IS_MIME_MESSAGE (message));
		camel_object_ref (message);
	} else
		message_uid = NULL;

	if (message_pane->priv->message != NULL) {
		camel_object_unref (message_pane->priv->message);
		g_free (message_pane->priv->message_uid);
	}

	message_pane->priv->message = message;
	message_pane->priv->message_uid = g_strdup (message_uid);

	g_object_freeze_notify (G_OBJECT (message_pane));
	g_object_notify (G_OBJECT (message_pane), "message");
	g_object_notify (G_OBJECT (message_pane), "message-uid");
	g_object_thaw_notify (G_OBJECT (message_pane));
}
