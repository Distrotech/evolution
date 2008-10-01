/*
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
 *		Ettore Perazzoli <ettore@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

/**
 * SECTION: e-shell-window
 * @short_description: the main window
 * @include: shell/e-shell-window.h
 **/

#ifndef E_SHELL_WINDOW_H
#define E_SHELL_WINDOW_H

#include <shell/e-shell.h>

/* Standard GObject macros */
#define E_TYPE_SHELL_WINDOW \
	(e_shell_window_get_type ())
#define E_SHELL_WINDOW(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_SHELL_WINDOW, EShellWindow))
#define E_SHELL_WINDOW_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_SHELL_WINDOW, EShellWindowClass))
#define E_IS_SHELL_WINDOW(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_SHELL_WINDOW))
#define E_IS_SHELL_WINDOW_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((obj), E_TYPE_SHELL_WINDOW))
#define E_SHELL_WINDOW_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_SHELL_WINDOW, EShellWindowClass))

G_BEGIN_DECLS

typedef struct _EShellWindow EShellWindow;
typedef struct _EShellWindowClass EShellWindowClass;
typedef struct _EShellWindowPrivate EShellWindowPrivate;

/**
 * EShellWindow:
 *
 * Contains only private data that should be read and manipulated using the
 * functions below.
 **/
struct _EShellWindow {
	GtkWindow parent;
	EShellWindowPrivate *priv;
};

struct _EShellWindowClass {
	GtkWindowClass parent_class;
};

GType		e_shell_window_get_type		(void);
GtkWidget *	e_shell_window_new		(EShell *shell,
						 gboolean safe_mode);
EShell *	e_shell_window_get_shell	(EShellWindow *shell_window);
gpointer	e_shell_window_get_shell_view	(EShellWindow *shell_window,
						 const gchar *view_name);
GtkUIManager *	e_shell_window_get_ui_manager	(EShellWindow *shell_window);
GtkAction *	e_shell_window_get_action	(EShellWindow *shell_window,
						 const gchar *action_name);
GtkActionGroup *e_shell_window_get_action_group	(EShellWindow *shell_window,
						 const gchar *group_name);
GtkWidget *	e_shell_window_get_managed_widget
						(EShellWindow *shell_window,
						 const gchar *widget_path);
const gchar *	e_shell_window_get_active_view	(EShellWindow *shell_window);
void		e_shell_window_set_active_view	(EShellWindow *shell_window,
						 const gchar *view_name);
gboolean	e_shell_window_get_safe_mode	(EShellWindow *shell_window);
void		e_shell_window_set_safe_mode	(EShellWindow *shell_window,
						 gboolean safe_mode);

/* These should be called from the shell module's window_created() handler. */

void		e_shell_window_register_new_item_actions
						(EShellWindow *shell_window,
						 const gchar *module_name,
						 const GtkActionEntry *entries,
						 guint n_entries);
void		e_shell_window_register_new_source_actions
						(EShellWindow *shell_window,
						 const gchar *module_name,
						 const GtkActionEntry *entries,
						 guint n_entries);

G_END_DECLS

#endif /* E_SHELL_WINDOW_H */
