/*
 * e-menu-tool-button.c
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

#include "e-menu-tool-button.h"

static gpointer parent_class;

static GtkWidget *
menu_tool_button_clone_image (GtkWidget *source)
{
	GtkIconSize size;
	GtkImageType image_type;
	const gchar *icon_name;

	/* XXX This isn't general purpose because it requires that the
	 *     source image be using a named icon.  Somewhat surprised
	 *     GTK+ doesn't offer something like this. */
	image_type = gtk_image_get_storage_type (GTK_IMAGE (source));
	g_return_val_if_fail (image_type == GTK_IMAGE_ICON_NAME, NULL);
	gtk_image_get_icon_name (GTK_IMAGE (source), &icon_name, &size);

	return gtk_image_new_from_icon_name (icon_name, size);
}

static GtkMenuItem *
menu_tool_button_get_first_menu_item (GtkMenuToolButton *menu_tool_button)
{
	GtkWidget *menu;
	GList *children;

	menu = gtk_menu_tool_button_get_menu (menu_tool_button);
	if (!GTK_IS_MENU (menu))
		return NULL;

	/* XXX GTK+ 2.12 provides no accessor function. */
	children = GTK_MENU_SHELL (menu)->children;
	if (children == NULL)
		return NULL;

	return GTK_MENU_ITEM (children->data);
}

static void
menu_tool_button_update_button (GtkToolButton *tool_button)
{
	GtkMenuItem *menu_item;
	GtkMenuToolButton *menu_tool_button;
	GtkImageMenuItem *image_menu_item;
	GtkAction *action;
	GtkWidget *image;
	gchar *tooltip = NULL;

	menu_tool_button = GTK_MENU_TOOL_BUTTON (tool_button);
	menu_item = menu_tool_button_get_first_menu_item (menu_tool_button);
	if (!GTK_IS_IMAGE_MENU_ITEM (menu_item))
		return;

	image_menu_item = GTK_IMAGE_MENU_ITEM (menu_item);
	image = gtk_image_menu_item_get_image (image_menu_item);
	if (!GTK_IS_IMAGE (image))
		return;

	image = menu_tool_button_clone_image (image);
	gtk_tool_button_set_icon_widget (tool_button, image);
	gtk_widget_show (image);

	/* If the menu item is a proxy for a GtkAction, extract
	 * the action's tooltip and use it as our own tooltip. */
	action = gtk_widget_get_action (GTK_WIDGET (menu_item));
	if (action != NULL)
		g_object_get (action, "tooltip", &tooltip, NULL);
	gtk_widget_set_tooltip_text (GTK_WIDGET (tool_button), tooltip);
	g_free (tooltip);
}

static void
menu_tool_button_clicked (GtkToolButton *tool_button)
{
	GtkMenuItem *menu_item;
	GtkMenuToolButton *menu_tool_button;

	menu_tool_button = GTK_MENU_TOOL_BUTTON (tool_button);
	menu_item = menu_tool_button_get_first_menu_item (menu_tool_button);

	if (GTK_IS_MENU_ITEM (menu_item))
		gtk_menu_item_activate (menu_item);
}

static void
menu_tool_button_class_init (EMenuToolButtonClass *class)
{
	GtkToolButtonClass *tool_button_class;

	parent_class = g_type_class_peek_parent (class);

	tool_button_class = GTK_TOOL_BUTTON_CLASS (class);
	tool_button_class->clicked = menu_tool_button_clicked;
}

static void
menu_tool_button_init (EMenuToolButton *button)
{
	g_signal_connect (
		button, "notify::menu",
		G_CALLBACK (menu_tool_button_update_button), NULL);
}

GType
e_menu_tool_button_get_type (void)
{
	static GType type = 0;

	if (G_UNLIKELY (type == 0)) {
		const GTypeInfo type_info = {
			sizeof (EMenuToolButtonClass),
			(GBaseInitFunc) NULL,
			(GBaseFinalizeFunc) NULL,
			(GClassInitFunc) menu_tool_button_class_init,
			(GClassFinalizeFunc) NULL,
			NULL,  /* class_data */
			sizeof (EMenuToolButton),
			0,     /* n_preallocs */
			(GInstanceInitFunc) menu_tool_button_init,
			NULL   /* value_table */
		};

		type = g_type_register_static (
			GTK_TYPE_MENU_TOOL_BUTTON, "EMenuToolButton",
			&type_info, 0);
	}

	return type;
}

GtkToolItem *
e_menu_tool_button_new (const gchar *label)
{
	return g_object_new (E_TYPE_MENU_TOOL_BUTTON, "label", label, NULL);
}
