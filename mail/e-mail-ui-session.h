/*
 * e-mail-session.h
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
 * Authors:
 *		Jeffrey Stedfast <fejj@ximian.com>
 *		Srinivasa Ragavan <sragavan@gnome.org>
 *		
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#ifndef E_MAIL_UI_SESSION_H
#define E_MAIL_UI_SESSION_H

#include <camel/camel.h>
#include <libemail-engine/e-mail-session.h>
#include <libemail-engine/mail-folder-cache.h>

/* Standard GObject macros */
#define E_TYPE_MAIL_UI_SESSION \
	(e_mail_ui_session_get_type ())
#define E_MAIL_UI_SESSION(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_MAIL_UI_SESSION, EMailUISession))
#define E_MAIL_UI_SESSION_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_MAIL_UI_SESSION, EMailUISessionClass))
#define E_IS_MAIL_UI_SESSION(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_MAIL_UI_SESSION))
#define E_IS_MAIL_UI_SESSION_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_MAIL_UI_SESSION))
#define E_MAIL_UI_SESSION_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_MAIL_UI_SESSION, EMailUISessionClass))

G_BEGIN_DECLS

typedef struct _EMailUISession EMailUISession;
typedef struct _EMailUISessionClass EMailUISessionClass;
typedef struct _EMailUISessionPrivate EMailUISessionPrivate;

struct _EMailUISession {
	EMailSession parent;
	EMailUISessionPrivate *priv;
};

struct _EMailUISessionClass {
	EMailSessionClass parent_class;
};

GType		e_mail_ui_session_get_type		(void);
EMailSession *	e_mail_ui_session_new			(void);

G_END_DECLS

#endif /* E_MAIL_UI_SESSION_H */
