/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-gtk-utils.c
 *
 * Copyright (C) 2000  Ximian, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: Ettore Perazzoli
 */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gtk/gtklayout.h>
#include <gtk/gtksignal.h>
#include <gtk/gtkwidget.h>

#include <gdk/gdkx.h>

#include <X11/Xlib.h>

#include "e-gtk-utils.h"


/* (Cut and pasted from Gtk.)  */

typedef struct DisconnectInfo {
	unsigned int signal_handler;

	GtkObject *object1;
	unsigned int disconnect_handler1;

	GtkObject *object2;
	unsigned int disconnect_handler2;
} DisconnectInfo;

static unsigned int
alive_disconnecter (GtkObject *object,
		    DisconnectInfo *info)
{
	g_assert (info != NULL);

	gtk_signal_disconnect (info->object1, info->disconnect_handler1);
	gtk_signal_disconnect (info->object1, info->signal_handler);
	gtk_signal_disconnect (info->object2, info->disconnect_handler2);
	
	g_free (info);
	
	return 0;
}

/**
 * e_gtk_signal_connect_full_while_alive:
 * @object: 
 * @name: 
 * @func: 
 * @marshal: 
 * @data: 
 * @destroy_func: 
 * @object_signal: 
 * @after: 
 * @alive_object: 
 * 
 * Connect a signal like `gtk_signal_connect_while_alive()', but with full
 * params like `gtk_signal_connect_full()'.
 **/
void
e_gtk_signal_connect_full_while_alive (GtkObject *object,
				       const char *name,
				       GtkSignalFunc func,
				       GtkCallbackMarshal marshal,
				       void *data,
				       GtkDestroyNotify destroy_func,
				       gboolean object_signal,
				       gboolean after,
				       GtkObject *alive_object)
{
	DisconnectInfo *info;
	
	g_return_if_fail (GTK_IS_OBJECT (object));
	g_return_if_fail (name != NULL);
	g_return_if_fail (func != NULL);
	g_return_if_fail (GTK_IS_OBJECT (alive_object));
	
	info = g_new (DisconnectInfo, 1);

	info->signal_handler = gtk_signal_connect_full (object, name,
							func, marshal, data,
							destroy_func,
							object_signal, after);

	info->object1 = object;
	info->disconnect_handler1 = gtk_signal_connect (object, "destroy",
							GTK_SIGNAL_FUNC (alive_disconnecter), info);

	info->object2 = alive_object;
	info->disconnect_handler2 = gtk_signal_connect (alive_object, "destroy",
							GTK_SIGNAL_FUNC (alive_disconnecter), info);
}


/* BackingStore support.  */

static void
widget_realize_callback_for_backing_store (GtkWidget *widget,
					   void *data)
{
	XSetWindowAttributes attributes;
	GdkWindow *window;

	if (GTK_IS_LAYOUT (widget))
		window = GTK_LAYOUT (widget)->bin_window;
	else
		window = widget->window;

	attributes.backing_store = Always;
	XChangeWindowAttributes (GDK_WINDOW_XDISPLAY (window), GDK_WINDOW_XWINDOW (window),
				 CWBackingStore, &attributes);
}

/**
 * e_make_widget_backing_stored:
 * @widget: A GtkWidget
 * 
 * Make sure that the window for @widget has the BackingStore attribute set to
 * Always when realized.  This will allow the widget to be refreshed by the X
 * server even if the application is currently not responding to X events (this
 * is e.g. very useful for the splash screen).
 *
 * Notice that this will not work 100% in all cases as the server might not
 * support that or just refuse to do so.
 **/
void
e_make_widget_backing_stored  (GtkWidget *widget)
{
	gtk_signal_connect (GTK_OBJECT (widget), "realize",
			    GTK_SIGNAL_FUNC (widget_realize_callback_for_backing_store), NULL);
}
