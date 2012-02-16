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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "e-menu-tool-button.h"

enum {
	PROP_0,
	PROP_PREFER_ITEM
};

struct _EMenuToolButtonPrivate
{
	gchar *prefer_item;
};

G_DEFINE_TYPE (
	EMenuToolButton,
	e_menu_tool_button,
	GTK_TYPE_MENU_TOOL_BUTTON)

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
menu_tool_button_get_prefer_menu_item (GtkMenuToolButton *menu_tool_button)
{
	GtkWidget *menu;
	GtkMenuItem *item = NULL;
	GList *children;
	const gchar *prefer_item;

	menu = gtk_menu_tool_button_get_menu (menu_tool_button);
	if (!GTK_IS_MENU (menu))
		return NULL;

	children = gtk_container_get_children (GTK_CONTAINER (menu));
	if (children == NULL)
		return NULL;

	prefer_item = e_menu_tool_button_get_prefer_item (E_MENU_TOOL_BUTTON (menu_tool_button));
	if (prefer_item && *prefer_item) {
		GtkAction *action;
		GList *iter;

		for (iter = children; iter != NULL; iter = iter->next) {
			item = GTK_MENU_ITEM (iter->data);

			if (!item)
				continue;

			action = gtk_activatable_get_related_action (GTK_ACTIVATABLE (item));
			if (action && g_strcmp0 (gtk_action_get_name (action), prefer_item) == 0)
				break;
			else if (!action && g_strcmp0 (gtk_widget_get_name (GTK_WIDGET (item)), prefer_item) == 0)
				break;

			item = NULL;
		}
	}

	if (!item)
		item = GTK_MENU_ITEM (children->data);

	g_list_free (children);

	return item;
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
	menu_item = menu_tool_button_get_prefer_menu_item (menu_tool_button);
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
	action = gtk_activatable_get_related_action (
		GTK_ACTIVATABLE (menu_item));
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
	menu_item = menu_tool_button_get_prefer_menu_item (menu_tool_button);

	if (GTK_IS_MENU_ITEM (menu_item))
		gtk_menu_item_activate (menu_item);
}

static void
menu_tool_button_set_property (GObject *object,
                               guint property_id,
                               const GValue *value,
                               GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_PREFER_ITEM:
			e_menu_tool_button_set_prefer_item (
				E_MENU_TOOL_BUTTON (object),
				g_value_get_string (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
menu_tool_button_get_property (GObject *object,
                               guint property_id,
                               GValue *value,
                               GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_PREFER_ITEM:
			g_value_set_string (
				value, e_menu_tool_button_get_prefer_item (
				E_MENU_TOOL_BUTTON (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
menu_tool_button_dispose (GObject *object)
{
	EMenuToolButtonPrivate *priv = E_MENU_TOOL_BUTTON (object)->priv;

	if (priv->prefer_item) {
		g_free (priv->prefer_item);
		priv->prefer_item = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_menu_tool_button_parent_class)->dispose (object);
}

static void
e_menu_tool_button_class_init (EMenuToolButtonClass *class)
{
	GObjectClass *object_class;
	GtkToolButtonClass *tool_button_class;

	g_type_class_add_private (class, sizeof (EMenuToolButtonPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = menu_tool_button_set_property;
	object_class->get_property = menu_tool_button_get_property;
	object_class->dispose = menu_tool_button_dispose;

	tool_button_class = GTK_TOOL_BUTTON_CLASS (class);
	tool_button_class->clicked = menu_tool_button_clicked;

	g_object_class_install_property (
		object_class,
		PROP_PREFER_ITEM,
		g_param_spec_string (
			"prefer-item",
			"Prefer Item",
			"Name of an item to show instead of the first",
			NULL,
			G_PARAM_READWRITE));
}

static void
e_menu_tool_button_init (EMenuToolButton *button)
{
	button->priv = G_TYPE_INSTANCE_GET_PRIVATE (
		button, E_TYPE_MENU_TOOL_BUTTON, EMenuToolButtonPrivate);

	button->priv->prefer_item = NULL;

	g_signal_connect (
		button, "notify::menu",
		G_CALLBACK (menu_tool_button_update_button), NULL);
}

GtkToolItem *
e_menu_tool_button_new (const gchar *label)
{
	return g_object_new (E_TYPE_MENU_TOOL_BUTTON, "label", label, NULL);
}

void
e_menu_tool_button_set_prefer_item (EMenuToolButton *button,
				    const gchar *prefer_item)
{
	g_return_if_fail (button != NULL);
	g_return_if_fail (E_IS_MENU_TOOL_BUTTON (button));

	if (g_strcmp0 (button->priv->prefer_item, prefer_item) == 0)
		return;

	g_free (button->priv->prefer_item);
	button->priv->prefer_item = g_strdup (prefer_item);

	g_object_notify (G_OBJECT (button), "prefer-item");
}

const gchar *
e_menu_tool_button_get_prefer_item (EMenuToolButton *button)
{
	g_return_val_if_fail (button != NULL, NULL);
	g_return_val_if_fail (E_IS_MENU_TOOL_BUTTON (button), NULL);

	return button->priv->prefer_item;
}
