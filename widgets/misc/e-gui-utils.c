/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * GUI utility functions
 *
 * Author:
 *   Miguel de Icaza (miguel@helixcode.com)
 *
 * (C) 1999 Miguel de Icaza
 * (C) 2000 Helix Code, Inc.
 */
#include <config.h>
#include <gtk/gtksignal.h>
#include <libgnomeui/gnome-messagebox.h>
#include <libgnomeui/gnome-stock.h>
#include <gnome.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk-pixbuf/gnome-canvas-pixbuf.h>
#include "e-gui-utils.h"

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
e_popup_menu (GtkMenu *menu, GdkEventButton *event)
{
	g_return_if_fail (menu != NULL);
	g_return_if_fail (GTK_IS_MENU (menu));

	e_auto_kill_popup_menu_on_hide (menu);
	gtk_menu_popup (menu, NULL, NULL, 0, NULL, event->button, event->time);
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
