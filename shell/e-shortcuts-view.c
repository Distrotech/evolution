/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-shortcuts-view.c
 *
 * Copyright (C) 2000, 2001 Ximian, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
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

#include <glib.h>
#include <gtk/gtkcheckmenuitem.h>
#include <gtk/gtkentry.h>
#include <gtk/gtklabel.h>
#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-i18n.h>
#include <libgnomeui/gnome-app.h>
#include <libgnomeui/gnome-app-helper.h>
#include <libgnomeui/gnome-dialog.h>
#include <libgnomeui/gnome-messagebox.h>
#include <libgnomeui/gnome-popup-menu.h>
#include <libgnomeui/gnome-stock.h>
#include <libgnomeui/gnome-uidefs.h>
#include <gal/util/e-util.h>
#include <gal/widgets/e-unicode.h>

#include "e-util/e-request.h"

#include "e-shell-constants.h"

#include "e-shortcuts-view-model.h"

#include "e-shortcuts-view.h"


#define PARENT_TYPE E_TYPE_SHORTCUT_BAR
static EShortcutBarClass *parent_class = NULL;

struct _EShortcutsViewPrivate {
	EShortcuts *shortcuts;
};

enum {
	ACTIVATE_SHORTCUT,
	HIDE_REQUESTED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };


static void
show_new_group_dialog (EShortcutsView *view)
{
	char *group_name;

	group_name = e_request_string (GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (view))),
				       _("Create new shortcut group"),
				       _("Group name:"),
				       NULL);

	if (group_name == NULL)
		return;

	e_shortcuts_add_group (view->priv->shortcuts, -1, group_name);

	g_free (group_name);
}


/* Shortcut bar right-click menu.  */

struct _RightClickMenuData {
	EShortcutsView *shortcuts_view;
	int group_num;
};
typedef struct _RightClickMenuData RightClickMenuData;

static void
toggle_large_icons_cb (GtkWidget *widget,
		       void *data)
{
	RightClickMenuData *menu_data;

	menu_data = (RightClickMenuData *) data;

	if (menu_data == NULL)
		return;

	if (! GTK_CHECK_MENU_ITEM (widget)->active)
		return;

	e_shortcut_bar_set_view_type (E_SHORTCUT_BAR (menu_data->shortcuts_view),
				      menu_data->group_num,
				      E_ICON_BAR_LARGE_ICONS);
}

static void
toggle_small_icons_cb (GtkWidget *widget,
		       void *data)
{
	RightClickMenuData *menu_data;

	menu_data = (RightClickMenuData *) data;
	if (menu_data == NULL)
		return;

	if (! GTK_CHECK_MENU_ITEM (widget)->active)
		return;

	e_shortcut_bar_set_view_type (E_SHORTCUT_BAR (menu_data->shortcuts_view),
				      menu_data->group_num,
				      E_ICON_BAR_SMALL_ICONS);
}

static void
hide_shortcut_bar_cb (GtkWidget *widget,
		      void *data)
{
	RightClickMenuData *menu_data;
	EShortcutsView *shortcut_view;

	menu_data = (RightClickMenuData *) data;

	shortcut_view = E_SHORTCUTS_VIEW (menu_data->shortcuts_view);

	gtk_signal_emit (GTK_OBJECT (shortcut_view), signals[HIDE_REQUESTED]);
}

static void
create_new_group_cb (GtkWidget *widget,
		     void *data)
{
	RightClickMenuData *menu_data;

	menu_data = (RightClickMenuData *) data;

	show_new_group_dialog (menu_data->shortcuts_view);
}

static void
destroy_group_cb (GtkWidget *widget,
		  void *data)
{
	RightClickMenuData *menu_data;
	EShortcuts *shortcuts;
	EShortcutsView *shortcuts_view;
	EShortcutsViewPrivate *priv;
	GtkWidget *message_box;
	char *question, *title;

	menu_data = (RightClickMenuData *) data;
	shortcuts_view = menu_data->shortcuts_view;
	priv = shortcuts_view->priv;
	shortcuts = priv->shortcuts;

	title = e_utf8_to_locale_string (e_shortcuts_get_group_title (
	                                 shortcuts, menu_data->group_num));
	question = g_strdup_printf (_("Do you really want to remove group\n"
	                              "`%s' from the shortcut bar?"), title);
	g_free (title);

	message_box = gnome_message_box_new (question, GNOME_MESSAGE_BOX_QUESTION,
					     _("Remove"), _("Don't remove"), NULL);
	gnome_dialog_set_parent (GNOME_DIALOG (message_box),
				 GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (shortcuts_view))));
	g_free (question);

	if (gnome_dialog_run_and_close (GNOME_DIALOG (message_box)) != 0)
		return;

	e_shortcuts_remove_group (shortcuts, menu_data->group_num);
}

static void
rename_group_cb (GtkWidget *widget,
		 void *data)
{
	RightClickMenuData *menu_data;
	EShortcuts *shortcuts;
	EShortcutsView *shortcuts_view;
	const char *old_name;
	char *new_name;
	int group;

	menu_data = (RightClickMenuData *) data;
	shortcuts_view = menu_data->shortcuts_view;
	shortcuts = shortcuts_view->priv->shortcuts;

	old_name = e_shortcuts_get_group_title (shortcuts, menu_data->group_num);

	new_name = e_request_string (GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (shortcuts_view))),
				     _("Rename Shortcut Group"),
				     _("Rename selected shortcut group to:"),
				     old_name);

	if (new_name == NULL)
		return;

	/* Remember the group and flip back to it */
	group = e_group_bar_get_current_group_num (E_GROUP_BAR (E_SHORTCUT_BAR (shortcuts_view)));
	e_shortcuts_rename_group (shortcuts, menu_data->group_num, new_name);
	g_free (new_name);
	e_group_bar_set_current_group_num (E_GROUP_BAR (E_SHORTCUT_BAR (shortcuts_view)), group, FALSE);
}

static GnomeUIInfo icon_size_radio_group_uiinfo[] = {
	{ GNOME_APP_UI_ITEM, N_("_Small Icons"),
	  N_("Show the shortcuts as small icons"), toggle_small_icons_cb, NULL,
	  NULL, 0, 0, 0, 0 },
	{ GNOME_APP_UI_ITEM, N_("_Large Icons"),
	  N_("Show the shortcuts as large icons"), toggle_large_icons_cb, NULL,
	  NULL, 0, 0, 0, 0 },

	GNOMEUIINFO_END
};

static GnomeUIInfo right_click_menu_uiinfo[] = {
	GNOMEUIINFO_RADIOLIST (icon_size_radio_group_uiinfo),

	GNOMEUIINFO_SEPARATOR,

	{ GNOME_APP_UI_ITEM, N_("_New Group..."),
	  N_("Create a new shortcut group"), create_new_group_cb, NULL,
	  NULL, 0, 0, 0, 0 },
	{ GNOME_APP_UI_ITEM, N_("_Remove this Group..."),
	  N_("Remove this shortcut group"), destroy_group_cb, NULL,
	  NULL, 0, 0, 0, 0 },
	{ GNOME_APP_UI_ITEM, N_("Re_name this Group..."),
	  N_("Rename this shortcut group"), rename_group_cb, NULL,
	  NULL, 0, 0, 0, 0 },

	GNOMEUIINFO_SEPARATOR,

	{ GNOME_APP_UI_ITEM, N_("_Hide the Shortcut Bar"), 
	  N_("Hide the shortcut bar"), hide_shortcut_bar_cb, NULL,
	  NULL, 0, 0, 0, 0 },

	GNOMEUIINFO_END
};

static void
pop_up_right_click_menu_for_group (EShortcutsView *shortcuts_view,
				   GdkEventButton *event,
				   int group_num)
{
	RightClickMenuData *menu_data;
	GtkWidget *popup_menu;

	menu_data = g_new (RightClickMenuData, 1);
	menu_data->shortcuts_view = shortcuts_view;
	menu_data->group_num      = group_num;

	popup_menu = gnome_popup_menu_new (right_click_menu_uiinfo);

	if (e_shortcut_bar_get_view_type (E_SHORTCUT_BAR (shortcuts_view), group_num)
	    == E_ICON_BAR_SMALL_ICONS)
		gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (icon_size_radio_group_uiinfo[0].widget),
						TRUE);
	else
		gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (icon_size_radio_group_uiinfo[1].widget),
						TRUE);

	if (group_num == 0)
		gtk_widget_set_sensitive (right_click_menu_uiinfo[3].widget, FALSE);

	gnome_popup_menu_do_popup_modal (popup_menu, NULL, NULL, event, menu_data);

	g_free (menu_data);
	gtk_widget_unref (popup_menu);
}


/* Data to be passed around for the shortcut right-click menu items.  */

struct _ShortcutRightClickMenuData {
	EShortcutsView *shortcuts_view;
	int group_num;
	int item_num;
};
typedef struct _ShortcutRightClickMenuData ShortcutRightClickMenuData;


/* "Open Shortcut" and "Open Shortcut in New Window" commands.  */

static void
open_shortcut_helper (ShortcutRightClickMenuData *menu_data,
		      gboolean in_new_window)
{
	EShortcutsView *shortcuts_view;
	EShortcuts *shortcuts;
	const EShortcutItem *shortcut_item;

	shortcuts_view = menu_data->shortcuts_view;
	shortcuts = shortcuts_view->priv->shortcuts;

	shortcut_item = e_shortcuts_get_shortcut (shortcuts, menu_data->group_num, menu_data->item_num);
	if (shortcut_item == NULL)
		return;

	gtk_signal_emit (GTK_OBJECT (shortcuts_view), signals[ACTIVATE_SHORTCUT],
			 shortcuts, shortcut_item->uri, in_new_window);
}

static void
open_shortcut_cb (GtkWidget *widget,
		  void *data)
{
	open_shortcut_helper ((ShortcutRightClickMenuData *) data, FALSE);
}

static void
open_shortcut_in_new_window_cb (GtkWidget *widget,
				void *data)
{
	open_shortcut_helper ((ShortcutRightClickMenuData *) data, TRUE);
}


static void
remove_shortcut_cb (GtkWidget *widget,
		    void *data)
{
	ShortcutRightClickMenuData *menu_data;
	EShortcutsView *shortcuts_view;
	EShortcuts *shortcuts;

	menu_data = (ShortcutRightClickMenuData *) data;
	shortcuts_view = menu_data->shortcuts_view;
	shortcuts = shortcuts_view->priv->shortcuts;

	e_shortcuts_remove_shortcut (shortcuts, menu_data->group_num, menu_data->item_num);
}


/* "Rename Shortcut"  command.  */

static void
rename_shortcut_cb (GtkWidget *widget,
		    void *data)
{
	ShortcutRightClickMenuData *menu_data;
	EShortcutsView *shortcuts_view;
	EShortcuts *shortcuts;
	const EShortcutItem *shortcut_item;
	char *new_name;

	menu_data = (ShortcutRightClickMenuData *) data;
	shortcuts_view = menu_data->shortcuts_view;
	shortcuts = shortcuts_view->priv->shortcuts;

	shortcut_item = e_shortcuts_get_shortcut (shortcuts, menu_data->group_num, menu_data->item_num);

	new_name = e_request_string (GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (shortcuts_view))),
				     _("Rename shortcut"),
				     _("Rename selected shortcut to:"),
				     shortcut_item->name);

	if (new_name == NULL)
		return;

	e_shortcuts_update_shortcut (shortcuts, menu_data->group_num, menu_data->item_num,
				     shortcut_item->uri, new_name, shortcut_item->unread_count, shortcut_item->type);
	g_free (new_name);
}

static GnomeUIInfo shortcut_right_click_menu_uiinfo[] = {
	GNOMEUIINFO_ITEM_STOCK (N_("_Open"), N_("Open the folder linked to this shortcut"),
				open_shortcut_cb, GNOME_STOCK_MENU_OPEN), 
	GNOMEUIINFO_ITEM_NONE  (N_("Open in New _Window"), N_("Open the folder linked to this shortcut in a new window"),
				open_shortcut_in_new_window_cb),
	GNOMEUIINFO_SEPARATOR,
	GNOMEUIINFO_ITEM_NONE (N_("_Rename"), N_("Rename this shortcut"),
				rename_shortcut_cb),
	GNOMEUIINFO_ITEM_STOCK (N_("Re_move"), N_("Remove this shortcut from the shortcut bar"),
				remove_shortcut_cb, GNOME_STOCK_MENU_TRASH),
	GNOMEUIINFO_END
};

static void
pop_up_right_click_menu_for_shortcut (EShortcutsView *shortcuts_view,
				      GdkEventButton *event,
				      int group_num,
				      int item_num)
{
	ShortcutRightClickMenuData *menu_data;
	GtkWidget *popup_menu;

	menu_data = g_new (ShortcutRightClickMenuData, 1);
	menu_data->shortcuts_view = shortcuts_view;
	menu_data->group_num 	  = group_num;
	menu_data->item_num  	  = item_num;

	popup_menu = gnome_popup_menu_new (shortcut_right_click_menu_uiinfo);

	gnome_popup_menu_do_popup_modal (popup_menu, NULL, NULL, event, menu_data);

	g_free (menu_data);
	gtk_widget_destroy (popup_menu);
}


/* GtkObject methods.  */

static void
destroy (GtkObject *object)
{
	EShortcutsViewPrivate *priv;
	EShortcutsView *shortcuts_view;

	shortcuts_view = E_SHORTCUTS_VIEW (object);

	priv = shortcuts_view->priv;

	gtk_object_unref (GTK_OBJECT (priv->shortcuts));

	g_free (priv);

	(* GTK_OBJECT_CLASS (parent_class)->destroy) (object);
}


/* EShortcutBar methods.  */

static void
item_selected (EShortcutBar *shortcut_bar,
	       GdkEvent *event,
	       int group_num,
	       int item_num)
{
	EShortcuts *shortcuts;
	EShortcutsView *shortcuts_view;
	const EShortcutItem *shortcut_item;

	shortcuts_view = E_SHORTCUTS_VIEW (shortcut_bar);
	shortcuts = shortcuts_view->priv->shortcuts;

	if (event->button.button == 3) {
		if (item_num < 0)
			pop_up_right_click_menu_for_group (shortcuts_view, &event->button,
							   group_num);
		else
			pop_up_right_click_menu_for_shortcut (shortcuts_view, &event->button,
							      group_num, item_num);
		return;
	} else if (event->button.button != 1) {
		return;
	}

	if (item_num < 0)
		return;

	shortcut_item = e_shortcuts_get_shortcut (shortcuts, group_num, item_num);
	if (shortcut_item == NULL)
		return;

	gtk_signal_emit (GTK_OBJECT (shortcuts_view), signals[ACTIVATE_SHORTCUT],
			 shortcuts, shortcut_item->uri, FALSE);
}

static void
get_shortcut_info (EShortcutsView *shortcuts_view,
		   const char *item_url,
		   int *unread_count_return,
		   const char **type_return)
{
	EShortcutsViewPrivate *priv;
	EStorageSet *storage_set;
	EStorage *storage;
	EFolder *folder;
	const char *path;

	priv = shortcuts_view->priv;

	if (strncmp (item_url, E_SHELL_URI_PREFIX, E_SHELL_URI_PREFIX_LEN) != 0) {
		*unread_count_return = 0;
		*type_return = NULL;
		return;
	}

	path = strchr (item_url, G_DIR_SEPARATOR);
	storage_set = e_shortcuts_get_storage_set (priv->shortcuts);

	folder = e_storage_set_get_folder (storage_set, path);
	if (folder != NULL) {
		*unread_count_return = e_folder_get_unread_count (folder);
		*type_return = e_folder_get_type_string (folder);
		return;
	}

	storage = e_storage_set_get_storage (storage_set, path + 1);
	if (storage != NULL) {
		*unread_count_return = 0;
		*type_return = e_storage_get_toplevel_node_type (storage);
		return;
	}

	*unread_count_return = 0;
	*type_return = NULL;
}

static void
impl_shortcut_dropped (EShortcutBar *shortcut_bar,
		       int group_num,
		       int position,
		       const char *item_url,
		       const char *item_name)
{
	EShortcutsView *shortcuts_view;
	EShortcutsViewPrivate *priv;
	int unread_count;
	const char *type;
	char *tmp;
	char *tp;
	char *name_without_unread;

	shortcuts_view = E_SHORTCUTS_VIEW (shortcut_bar);
	priv = shortcuts_view->priv;

	get_shortcut_info (shortcuts_view, item_url, &unread_count, &type);

	/* Looks funny, but keeps it from adding the unread count
           repeatedly when dragging folders around */
	tmp = g_strdup_printf (" (%d)", unread_count);
	if ((tp = strstr (item_name, tmp)) != NULL)
		name_without_unread = g_strndup (item_name, strlen (item_name) - strlen (tp));
	else
		name_without_unread = g_strdup (item_name);

	e_shortcuts_add_shortcut (priv->shortcuts,
				  group_num, position,
				  item_url, name_without_unread, unread_count, type);

	g_free (tmp);
	g_free (name_without_unread);
}

static void
impl_shortcut_dragged (EShortcutBar *shortcut_bar,
		       gint group_num,
		       gint item_num)
{
	EShortcutsView *shortcuts_view;
	EShortcutsViewPrivate *priv;

	shortcuts_view = E_SHORTCUTS_VIEW (shortcut_bar);
	priv = shortcuts_view->priv;

	e_shortcuts_remove_shortcut (priv->shortcuts, group_num, item_num);
}


static void
class_init (EShortcutsViewClass *klass)
{
	GtkObjectClass *object_class;
	EShortcutBarClass *shortcut_bar_class;

	object_class = GTK_OBJECT_CLASS (klass);
	object_class->destroy = destroy;

	shortcut_bar_class = E_SHORTCUT_BAR_CLASS (klass);
	shortcut_bar_class->item_selected    = item_selected;
	shortcut_bar_class->shortcut_dropped = impl_shortcut_dropped;
	shortcut_bar_class->shortcut_dragged = impl_shortcut_dragged;

	parent_class = gtk_type_class (e_shortcut_bar_get_type ());

	signals[ACTIVATE_SHORTCUT] =
		gtk_signal_new ("activate_shortcut",
				GTK_RUN_LAST | GTK_RUN_ACTION,
				object_class->type,
				GTK_SIGNAL_OFFSET (EShortcutsViewClass, activate_shortcut),
				e_marshal_NONE__POINTER_POINTER_INT,
				GTK_TYPE_NONE, 3,
				GTK_TYPE_POINTER,
				GTK_TYPE_STRING,
				GTK_TYPE_BOOL);

	signals[HIDE_REQUESTED] =
		gtk_signal_new ("hide_requested",
				GTK_RUN_LAST,
				object_class->type,
				GTK_SIGNAL_OFFSET (EShortcutsViewClass,
						   hide_requested),
				gtk_marshal_NONE__NONE,
				GTK_TYPE_NONE, 0);

	gtk_object_class_add_signals (object_class, signals, LAST_SIGNAL);
}

static void
init (EShortcutsView *shortcuts_view)
{
	EShortcutsViewPrivate *priv;

	priv = g_new (EShortcutsViewPrivate, 1);
	priv->shortcuts = NULL;

	shortcuts_view->priv = priv;
}


void
e_shortcuts_view_construct (EShortcutsView *shortcuts_view,
			    EShortcuts *shortcuts)
{
	EShortcutsViewPrivate *priv;

	g_return_if_fail (shortcuts != NULL);
	g_return_if_fail (E_IS_SHORTCUTS (shortcuts));

	priv = shortcuts_view->priv;

	priv->shortcuts = shortcuts;
	gtk_object_ref (GTK_OBJECT (priv->shortcuts));

	e_shortcut_bar_set_model (E_SHORTCUT_BAR (shortcuts_view),
				  E_SHORTCUT_MODEL (e_shortcuts_view_model_new (shortcuts)));
}

GtkWidget *
e_shortcuts_view_new (EShortcuts *shortcuts)
{
	GtkWidget *new;

	g_return_val_if_fail (shortcuts != NULL, NULL);
	g_return_val_if_fail (E_IS_SHORTCUTS (shortcuts), NULL);

	new = gtk_type_new (e_shortcuts_view_get_type ());
	e_shortcuts_view_construct (E_SHORTCUTS_VIEW (new), shortcuts);

	return new;
}


E_MAKE_TYPE (e_shortcuts_view, "EShortcutsView", EShortcutsView, class_init, init, PARENT_TYPE)
