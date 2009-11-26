/*
 * anjal-shell-content.h
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

#ifndef ANJAL_SHELL_CONTENT_H
#define ANJAL_SHELL_CONTENT_H

#include <shell/e-shell-content.h>
#include <shell/e-shell-view.h>

/* Standard GObject macros */
#define ANJAL_TYPE_SHELL_CONTENT \
	(anjal_shell_content_get_type ())
#define ANJAL_SHELL_CONTENT(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), ANJAL_TYPE_SHELL_CONTENT, AnjalShellContent))
#define ANJAL_SHELL_CONTENT_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), ANJAL_TYPE_SHELL_CONTENT, AnjalShellContentClass))
#define ANJAL_IS_SHELL_CONTENT(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), ANJAL_TYPE_SHELL_CONTENT))
#define ANJAL_IS_SHELL_CONTENT_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), ANJAL_TYPE_SHELL_CONTENT))
#define ANJAL_SHELL_CONTENT_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), ANJAL_TYPE_SHELL_CONTENT, AnjalShellContentClass))

G_BEGIN_DECLS

typedef struct _AnjalShellContent AnjalShellContent;
typedef struct _AnjalShellContentClass AnjalShellContentClass;
typedef struct _AnjalShellContentPrivate AnjalShellContentPrivate;

struct _AnjalShellContent {
	EShellContent parent;
	AnjalShellContentPrivate *priv;
};

struct _AnjalShellContentClass {
	EShellContentClass parent_class;
};

GType		anjal_shell_content_get_type	(void);
void		anjal_shell_content_register_type
					(GTypeModule *type_module);
GtkWidget *	anjal_shell_content_new(EShellView *shell_view);
gboolean	anjal_shell_content_get_preview_visible
					(AnjalShellContent *mail_shell_content);
void		anjal_shell_content_set_preview_visible
					(AnjalShellContent *mail_shell_content,
						 gboolean preview_visible);
gboolean	anjal_shell_content_get_show_deleted
					(AnjalShellContent *mail_shell_content);
void		anjal_shell_content_set_show_deleted
					(AnjalShellContent *mail_shell_content,
					 gboolean show_deleted);
GalViewInstance *
		anjal_shell_content_get_view_instance
					(AnjalShellContent *mail_shell_content);
void		anjal_shell_content_set_search_strings
					(AnjalShellContent *mail_shell_content,
					 GSList *search_strings);
void		anjal_shell_content_update_view_instance
					(AnjalShellContent *mail_shell_content);
GtkWidget *	anjal_shell_content_get_search_entry 
					(EShellContent *shell_content);
void		anjal_shell_content_pack_view 
					(EShellContent *shell_content, 
					 GtkWidget *view);
					
G_END_DECLS

#endif /* ANJAL_SHELL_CONTENT_H */
