/*
 * e-mail-message-pane.h
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

#ifndef E_MAIL_MESSAGE_PANE_H
#define E_MAIL_MESSAGE_PANE_H

#include <camel/camel.h>
#include <mail/e-mail-display.h>
#include <misc/e-preview-pane.h>

/* Standard GObject macros */
#define E_TYPE_MAIL_MESSAGE_PANE \
	(e_mail_message_pane_get_type ())
#define E_MAIL_MESSAGE_PANE(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_MAIL_MESSAGE_PANE, EMailMessagePane))
#define E_MAIL_MESSAGE_PANE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_MAIL_MESSAGE_PANE, EMailMessagePaneClass))
#define E_IS_MAIL_MESSAGE_PANE(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_MAIL_MESSAGE_PANE))
#define E_IS_MAIL_MESSAGE_PANE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_MAIL_MESSAGE_PANE))
#define E_MAIL_MESSAGE_PANE_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_MAIL_MESSAGE_PANE, EMailMessagePaneClass))

G_BEGIN_DECLS

typedef struct _EMailMessagePane EMailMessagePane;
typedef struct _EMailMessagePaneClass EMailMessagePaneClass;
typedef struct _EMailMessagePanePrivate EMailMessagePanePrivate;

struct _EMailMessagePane {
	EPreviewPane parent;
	EMailMessagePanePrivate *priv;
};

struct _EMailMessagePaneClass {
	EPreviewPaneClass parent_class;
};

GType		e_mail_message_pane_get_type	(void);
GtkWidget *	e_mail_message_pane_new		(EMailDisplay *display);
EMailDisplay *	e_mail_message_pane_get_display	(EMailMessagePane *message_pane);
CamelFolder *	e_mail_message_pane_get_folder	(EMailMessagePane *message_pane);
void		e_mail_message_pane_set_folder	(EMailMessagePane *message_pane,
						 CamelFolder *folder);
CamelMimeMessage *
		e_mail_message_pane_get_message	(EMailMessagePane *message_pane);
const gchar *	e_mail_message_pane_get_message_uid
						(EMailMessagePane *message_pane);
void		e_mail_message_pane_set_message	(EMailMessagePane *message_pane,
						 CamelMimeMessage *message,
						 const gchar *message_uid);

G_END_DECLS

#endif /* E_MAIL_MESSAGE_PANE_H */
