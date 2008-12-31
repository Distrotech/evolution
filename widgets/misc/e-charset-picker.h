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
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#ifndef E_CHARSETPICKER_H
#define E_CHARSETPICKER_H

#include <gtk/gtk.h>
#include <bonobo/bonobo-ui-component.h>

G_BEGIN_DECLS

GtkWidget *	e_charset_picker_new		(const char *default_charset);
char *		e_charset_picker_get_charset	(GtkWidget *picker);
char *		e_charset_picker_dialog		(const char *title,
						 const char *prompt,
						 const char *default_charset,
						 GtkWindow *parent);

GSList *	e_charset_add_radio_actions	(GtkActionGroup *action_group,
						 const gchar *action_prefix,
						 const gchar *default_charset,
						 GCallback callback,
						 gpointer user_data);

G_END_DECLS

#endif /* E_CHARSETPICKER_H */
