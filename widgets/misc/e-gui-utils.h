/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * e-gui-utils.h
 * Copyright 2000, 2001, Ximian, Inc.
 *
 * Authors:
 *   Miguel de Icaza <miguel@ximian.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License, version 2, as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#ifndef GAL_GUI_UTILS_H
#define GAL_GUI_UTILS_H

#include <gtk/gtkmenu.h>
#include <gtk/gtkwindow.h>

#include <libgnomeui/gnome-messagebox.h>

BEGIN_GNOME_DECLS

void  e_popup_menu                    (GtkMenu      *menu,
				       GdkEvent     *event);
void  e_auto_kill_popup_menu_on_hide  (GtkMenu      *menu);
void  e_notice                        (GtkWindow    *window,
				       const char   *type,
				       const char   *format,
				       ...);
void  e_container_foreach_leaf        (GtkContainer *container,
				       GtkCallback   callback,
				       gpointer      closure);
void  e_container_focus_nth_entry     (GtkContainer *container,
				       int           n);
gint  e_container_change_tab_order    (GtkContainer *container,
				       GList        *widgets);

END_GNOME_DECLS

#endif /* GAL_GUI_UTILS_H */
