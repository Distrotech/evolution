/*
 * anjal-shell-view.h
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

#ifndef ANJAL_SHELL_VIEW_H
#define ANJAL_SHELL_VIEW_H

#include <shell/e-shell-view.h>
#include <mail/anjal-mail-view.h>

/* Standard GObject macros */
#define ANJAL_TYPE_SHELL_VIEW \
	(anjal_shell_view_get_type ())
#define ANJAL_SHELL_VIEW(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), ANJAL_TYPE_SHELL_VIEW, AnjalShellView))
#define ANJAL_SHELL_VIEW_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), ANJAL_TYPE_SHELL_VIEW, AnjalShellViewClass))
#define ANJAL_IS_SHELL_VIEW(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), ANJAL_TYPE_SHELL_VIEW))
#define ANJAL_IS_SHELL_VIEW_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), ANJAL_TYPE_SHELL_VIEW))
#define ANJAL_SHELL_VIEW_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), ANJAL_TYPE_SHELL_VIEW, AnjalShellViewClass))

G_BEGIN_DECLS

typedef struct _AnjalShellView AnjalShellView;
typedef struct _AnjalShellViewClass AnjalShellViewClass;
typedef struct _AnjalShellViewPrivate AnjalShellViewPrivate;

struct _AnjalShellView {
	EShellView parent;
	AnjalShellViewPrivate *priv;
};

struct _AnjalShellViewClass {
	EShellViewClass parent_class;
};

GType		anjal_shell_view_get_type	(void);
void		anjal_shell_view_register_type
					(GTypeModule *type_module);
gboolean	anjal_shell_view_get_show_deleted
					(AnjalShellView *mail_shell_view);
void		anjal_shell_view_set_show_deleted
					(AnjalShellView *mail_shell_view,
					 gboolean show_deleted);
void		anjal_shell_view_set_mail_view 
					(AnjalShellView *mail_shell_view, 
					 AnjalMailView *mail_view);

G_END_DECLS

#endif /* ANJAL_SHELL_VIEW_H */
