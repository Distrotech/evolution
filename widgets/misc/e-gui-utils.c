/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * e-gui-utils.c
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

#include <config.h>

#include "e-gui-utils.h"

#include <gtk/gtkentry.h>
#include <gtk/gtksignal.h>
#include <libgnomeui/gnome-messagebox.h>
#include <libgnomeui/gnome-stock.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk-pixbuf/gnome-canvas-pixbuf.h>

void
e_notice (GtkWindow *window, const char *type, const char *format, ...)
{
	GtkWidget *dialog;
	va_list args;
	char *str;

	va_start (args, format);
	str = g_strdup_vprintf (format, args);
	dialog = gnome_message_box_new (str, type, GNOME_STOCK_BUTTON_OK, NULL);
	va_end (args);
	g_free (str);
	
	if (window)
		gnome_dialog_set_parent (GNOME_DIALOG (dialog), window);

	gnome_dialog_run (GNOME_DIALOG (dialog));
}

static void
kill_popup_menu (GtkWidget *widget, GtkMenu *menu)
{
	g_return_if_fail (menu != NULL);
	g_return_if_fail (GTK_IS_MENU (menu));

	gtk_object_unref (GTK_OBJECT (menu));
}

void
e_auto_kill_popup_menu_on_hide (GtkMenu *menu)
{
	g_return_if_fail (menu != NULL);
	g_return_if_fail (GTK_IS_MENU (menu));

	gtk_signal_connect (GTK_OBJECT (menu), "hide",
			    GTK_SIGNAL_FUNC (kill_popup_menu), menu);
}

void
e_popup_menu (GtkMenu *menu, GdkEvent *event)
{
	g_return_if_fail (menu != NULL);
	g_return_if_fail (GTK_IS_MENU (menu));

	e_auto_kill_popup_menu_on_hide (menu);

	if (event->type == GDK_KEY_PRESS)
		gtk_menu_popup (menu, NULL, NULL, 0, NULL, 0, event->key.time);
	else if ((event->type == GDK_BUTTON_PRESS) ||
		 (event->type == GDK_BUTTON_RELEASE) ||
		 (event->type == GDK_2BUTTON_PRESS) ||
		 (event->type == GDK_3BUTTON_PRESS)){
		gtk_menu_popup (menu, NULL, NULL, 0, NULL, event->button.button, event->button.time);
	} else
		gtk_menu_popup (menu, NULL, NULL, 0, NULL, 0, GDK_CURRENT_TIME);
}

typedef struct {
	GtkCallback callback;
	gpointer closure;
} CallbackClosure;

static void
e_container_foreach_leaf_callback(GtkWidget *widget, CallbackClosure *callback_closure)
{
	if (GTK_IS_CONTAINER(widget)) {
		e_container_foreach_leaf(GTK_CONTAINER(widget), callback_closure->callback, callback_closure->closure);
	} else {
		(*callback_closure->callback) (widget, callback_closure->closure);
	}
}

void
e_container_foreach_leaf(GtkContainer *container,
			 GtkCallback callback,
			 gpointer closure)
{
	CallbackClosure callback_closure;
	callback_closure.callback = callback;
	callback_closure.closure = closure;
	gtk_container_foreach(container, (GtkCallback) e_container_foreach_leaf_callback, &callback_closure);
}

static void
e_container_change_tab_order_destroy_notify(gpointer data)
{
	GList *list = data;
	g_list_foreach(list, (GFunc) gtk_object_unref, NULL);
	g_list_free(list);
}


static gint
e_container_change_tab_order_callback(GtkContainer *container,
				      GtkDirectionType direction,
				      GList *children)
{
	GtkWidget *focus_child;
	GtkWidget *child;

	if (direction != GTK_DIR_TAB_FORWARD &&
	    direction != GTK_DIR_TAB_BACKWARD)
		return FALSE;

	focus_child = container->focus_child;

	if (direction == GTK_DIR_TAB_BACKWARD) {
		children = g_list_last(children);
	}

	while (children) {
		child = children->data;
		if (direction == GTK_DIR_TAB_FORWARD)
			children = children->next;
		else
			children = children->prev;

		if (!child)
			continue;

		if (focus_child) {
			if (focus_child == child) {
				focus_child = NULL;

				if (GTK_WIDGET_DRAWABLE (child) &&
				    GTK_IS_CONTAINER (child) &&
				    !GTK_WIDGET_HAS_FOCUS (child))
					if (gtk_container_focus (GTK_CONTAINER (child), direction)) {
						gtk_signal_emit_stop_by_name(GTK_OBJECT(container), "focus");
						return TRUE;
					}
			}
		}
		else if (GTK_WIDGET_DRAWABLE (child)) {
			if (GTK_IS_CONTAINER (child)) {
				if (gtk_container_focus (GTK_CONTAINER (child), direction)) {
					gtk_signal_emit_stop_by_name(GTK_OBJECT(container), "focus");
					return TRUE;
				}
			}
			else if (GTK_WIDGET_CAN_FOCUS (child)) {
				gtk_widget_grab_focus (child);
				gtk_signal_emit_stop_by_name(GTK_OBJECT(container), "focus");
				return TRUE;
			}
		}
	}

	return FALSE;
}

gint
e_container_change_tab_order(GtkContainer *container, GList *widgets)
{
	GList *list;
	list = g_list_copy(widgets);
	g_list_foreach(list, (GFunc) gtk_object_ref, NULL);
	return gtk_signal_connect_full(GTK_OBJECT(container), "focus",
				       GTK_SIGNAL_FUNC(e_container_change_tab_order_callback),
				       NULL, list,
				       e_container_change_tab_order_destroy_notify,
				       FALSE, FALSE);
}

struct widgetandint {
	GtkWidget *widget;
	int count;
};

static void
nth_entry_callback(GtkWidget *widget, struct widgetandint *data)
{
	if (GTK_IS_ENTRY(widget)) {
		if (data->count > 1) {
			data->count --;
			data->widget = widget;
		} else if (data->count == 1) {
			data->count --;
			data->widget = NULL;
			gtk_widget_grab_focus(widget);
		}
	}
}

void
e_container_focus_nth_entry(GtkContainer *container, int n)
{
	struct widgetandint data;
	data.widget = NULL;
	data.count = n;
	e_container_foreach_leaf(container, (GtkCallback) nth_entry_callback, &data);
	if (data.widget)
		gtk_widget_grab_focus(data.widget);
}
