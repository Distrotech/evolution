/*
 * e-mail-shell-content.h
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

#ifndef E_MAIL_SHELL_CONTENT_H
#define E_MAIL_SHELL_CONTENT_H

#include <shell/e-shell-content.h>
#include <shell/e-shell-searchbar.h>
#include <shell/e-shell-view.h>

#include <mail/em-format-html-display.h>

/* Standard GObject macros */
#define E_TYPE_MAIL_SHELL_CONTENT \
	(e_mail_shell_content_get_type ())
#define E_MAIL_SHELL_CONTENT(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_MAIL_SHELL_CONTENT, EMailShellContent))
#define E_MAIL_SHELL_CONTENT_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_MAIL_SHELL_CONTENT, EMailShellContentClass))
#define E_IS_MAIL_SHELL_CONTENT(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_MAIL_SHELL_CONTENT))
#define E_IS_MAIL_SHELL_CONTENT_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_MAIL_SHELL_CONTENT))
#define E_MAIL_SHELL_CONTENT_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_MAIL_SHELL_CONTENT, EMailShellContentClass))

#define STATE_KEY_PREVIEW "Preview"
#define STATE_KEY_THREAD_LIST "ThreadList"

G_BEGIN_DECLS

typedef struct _EMailShellContent EMailShellContent;
typedef struct _EMailShellContentClass EMailShellContentClass;
typedef struct _EMailShellContentPrivate EMailShellContentPrivate;

struct _EMailShellContent {
	EShellContent parent;
	EMailShellContentPrivate *priv;
};

struct _EMailShellContentClass {
	EShellContentClass parent_class;
};

GType		e_mail_shell_content_get_type	(void);
void		e_mail_shell_content_register_type
					(GTypeModule *type_module);
GtkWidget *	e_mail_shell_content_new(EShellView *shell_view);
gboolean	e_mail_shell_content_get_preview_visible
					(EMailShellContent *mail_shell_content);
void		e_mail_shell_content_set_preview_visible
					(EMailShellContent *mail_shell_content,
						 gboolean preview_visible);
EShellSearchbar *
		e_mail_shell_content_get_searchbar
					(EMailShellContent *mail_shell_content);
gboolean	e_mail_shell_content_get_show_deleted
					(EMailShellContent *mail_shell_content);
void		e_mail_shell_content_set_show_deleted
					(EMailShellContent *mail_shell_content,
					 gboolean show_deleted);
GalViewInstance *
		e_mail_shell_content_get_view_instance
					(EMailShellContent *mail_shell_content);
void		e_mail_shell_content_set_search_strings
					(EMailShellContent *mail_shell_content,
					 GSList *search_strings);
void		e_mail_shell_content_update_view_instance
					(EMailShellContent *mail_shell_content);

G_END_DECLS

#endif /* E_MAIL_SHELL_CONTENT_H */
