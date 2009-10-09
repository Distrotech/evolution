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
 *		Jeffrey Stedfast <fejj@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#ifndef MAIL_SESSION_H
#define MAIL_SESSION_H

#include <glib.h>
#include <camel/camel.h>
#include <shell/e-shell-backend.h>
#include <mail/mail-mt.h>

#define MAIL_TYPE_SESSION \
	(mail_session_get_type ())
#define MAIL_SESSION(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), MAIL_TYPE_SESSION, MailSession))
#define MAIL_SESSION_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), MAIL_TYPE_SESSION, MailSessionClass))
#define MAIL_IS_SESSION(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), MAIL_TYPE_SESSION))
#define MAIL_IS_SESSION_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), MAIL_TYPE_SESSION))
#define MAIL_SESSION_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), MAIL_TYPE_SESSION, MailSessionClass))

G_BEGIN_DECLS

typedef struct _MailSession MailSession;
typedef struct _MailSessionClass MailSessionClass;

struct _MailSession {
	CamelSession parent;

	gboolean interactive;
	FILE *filter_logfile;
	GList *junk_plugins;

	MailAsyncEvent *async;
};

struct _MailSessionClass {
	CamelSessionClass parent_class;
};

GType mail_session_get_type (void);
void mail_session_init (EShellBackend *shell_backend);
void mail_session_shutdown (void);
gboolean mail_session_get_interactive (void);
void mail_session_set_interactive (gboolean interactive);
gchar *mail_session_request_dialog (const gchar *prompt, gboolean secret,
				   const gchar *key, gboolean async);
gboolean mail_session_accept_dialog (const gchar *prompt, const gchar *key,
				     gboolean async);
gchar *mail_session_get_password (const gchar *url);
void mail_session_add_password (const gchar *url, const gchar *passwd);
void mail_session_remember_password (const gchar *url);

void mail_session_forget_password (const gchar *key);

void mail_session_flush_filter_log (void);

void mail_session_add_junk_plugin (const gchar *plugin_name, CamelJunkPlugin *junk_plugin);

const GList * mail_session_get_junk_plugins (void);
void mail_session_set_junk_headers (const gchar **name, const gchar **value, gint len);

extern CamelSession *session;

G_END_DECLS

#endif /* MAIL_SESSION_H */
