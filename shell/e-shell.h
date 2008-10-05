/*
 * e-shell.h
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

/**
 * SECTION: e-shell
 * @short_description: the backbone of Evolution
 * @include: shell/e-shell.h
 **/

#ifndef E_SHELL_H
#define E_SHELL_H

#include <shell/e-shell-common.h>
#include <shell/e-shell-module.h>

/* Standard GObject macros */
#define E_TYPE_SHELL \
	(e_shell_get_type ())
#define E_SHELL(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_SHELL, EShell))
#define E_SHELL_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_SHELL, EShellClass))
#define E_IS_SHELL(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_SHELL))
#define E_IS_SHELL_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_SHELL))
#define E_SHELL_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_SHELL, EShellClass))

G_BEGIN_DECLS

typedef struct _EShell EShell;
typedef struct _EShellClass EShellClass;
typedef struct _EShellPrivate EShellPrivate;

typedef enum _EShellLineStatus EShellLineStatus;

/**
 * EShell:
 *
 * Contains only private data that should be read and manipulated using the
 * functions below.
 **/
struct _EShell {
	GObject parent;
	EShellPrivate *priv;
};

struct _EShellClass {
	GObjectClass parent_class;
};

enum _EShellLineStatus {
	E_SHELL_LINE_STATUS_ONLINE,
	E_SHELL_LINE_STATUS_GOING_OFFLINE, /* NB: really means changing state in either direction */
	E_SHELL_LINE_STATUS_OFFLINE,
	E_SHELL_LINE_STATUS_FORCED_OFFLINE
};

GType		e_shell_get_type		(void);
EShell *	e_shell_new			(gboolean online);
GList *		e_shell_list_modules		(EShell *shell);
const gchar *	e_shell_get_canonical_name	(EShell *shell,
						 const gchar *name);
EShellModule *	e_shell_get_module_by_name	(EShell *shell,
						 const gchar *name);
EShellModule *	e_shell_get_module_by_scheme	(EShell *shell,
						 const gchar *scheme);
GtkWidget *	e_shell_create_window		(EShell *shell);
GtkWidget *	e_shell_get_focused_window	(EShell *shell);
gboolean	e_shell_handle_uri		(EShell *shell,
                                                 const gchar *uri);
void		e_shell_send_receive		(EShell *shell,
						 GtkWindow *parent);
gboolean	e_shell_get_online_mode		(EShell *shell);
void		e_shell_set_online_mode		(EShell *shell,
						 gboolean online_mode);
EShellLineStatus
		e_shell_get_line_status		(EShell *shell);
void		e_shell_set_line_status		(EShell *shell,
                                                 EShellLineStatus status);
GtkWidget *	e_shell_get_preferences_window	(void);
gboolean	e_shell_is_busy			(EShell *shell);
gboolean	e_shell_do_quit			(EShell *shell);
gboolean	e_shell_quit			(EShell *shell);

G_END_DECLS

#endif /* E_SHELL_H */
