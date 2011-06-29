/*
 * e-mail-label-action.c
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

#include "e-mail-label-action.h"

struct _EMailLabelActionPrivate {
	gint placeholder;
};

static gpointer parent_class;

static void
mail_label_action_menu_item_realize_cb (GtkWidget *menu_item)
{
	GtkAction *action;
	GtkActivatable *activatable;
	GtkWidget *container;
	GtkWidget *widget;

	activatable = GTK_ACTIVATABLE (menu_item);
	action = gtk_activatable_get_related_action (activatable);
	g_return_if_fail (E_IS_MAIL_LABEL_ACTION (action));

	/* Prevent GtkMenuItem's sync_action_properties() method from
	 * destroying our hack.  Instead we use EBindings to keep the
	 * label and image in sync with the action. */
	gtk_activatable_set_use_action_appearance (activatable, FALSE);

	/* Remove the menu item's child widget. */
	widget = gtk_bin_get_child (GTK_BIN (menu_item));
	gtk_widget_destroy (widget);

	/* Now add our own child widget. */

	widget = gtk_hbox_new (FALSE, 3);
	gtk_container_add (GTK_CONTAINER (menu_item), widget);
	gtk_widget_show (widget);

	container = widget;

	widget = gtk_action_create_icon (action, GTK_ICON_SIZE_MENU);
	gtk_box_pack_start (GTK_BOX (container), widget, FALSE, FALSE, 0);
	gtk_widget_show (widget);

	widget = gtk_label_new (NULL);
	gtk_label_set_use_underline (GTK_LABEL (widget), TRUE);
	gtk_misc_set_alignment (GTK_MISC (widget), 0.0, 0.5);
	gtk_box_pack_start (GTK_BOX (container), widget, TRUE, TRUE, 0);
	gtk_widget_show (widget);

	g_object_bind_property (
		action, "label",
		widget, "label",
		G_BINDING_BIDIRECTIONAL |
		G_BINDING_SYNC_CREATE);
}

static GtkWidget *
mail_label_action_create_menu_item (GtkAction *action)
{
	GtkWidget *menu_item;

	menu_item = gtk_check_menu_item_new ();

	g_signal_connect (
		menu_item, "realize",
		G_CALLBACK (mail_label_action_menu_item_realize_cb), NULL);

	return menu_item;
}

static void
mail_label_action_class_init (EMailLabelActionClass *class)
{
	GtkActionClass *action_class;

	parent_class = g_type_class_peek_parent (class);
	g_type_class_add_private (class, sizeof (EMailLabelActionPrivate));

	action_class = GTK_ACTION_CLASS (class);
	action_class->create_menu_item = mail_label_action_create_menu_item;
}

static void
mail_label_action_init (EMailLabelAction *action)
{
	action->priv = G_TYPE_INSTANCE_GET_PRIVATE (
		action, E_TYPE_MAIL_LABEL_ACTION, EMailLabelActionPrivate);
}

GType
e_mail_label_action_get_type (void)
{
	static GType type = 0;

	if (G_UNLIKELY (type == 0)) {
		static const GTypeInfo type_info = {
			sizeof (EMailLabelActionClass),
			(GBaseInitFunc) NULL,
			(GBaseFinalizeFunc) NULL,
			(GClassInitFunc) mail_label_action_class_init,
			(GClassFinalizeFunc) NULL,
			NULL,  /* class_data */
			sizeof (EMailLabelAction),
			0,     /* n_preallocs */
			(GInstanceInitFunc) mail_label_action_init,
			NULL   /* value_table */
		};

		type = g_type_register_static (
			GTK_TYPE_TOGGLE_ACTION,
			"EMailLabelAction", &type_info, 0);
	}

	return type;
}

EMailLabelAction *
e_mail_label_action_new (const gchar *name,
                         const gchar *label,
                         const gchar *tooltip,
                         const gchar *stock_id)
{
	g_return_val_if_fail (name != NULL, NULL);

	return g_object_new (
		E_TYPE_MAIL_LABEL_ACTION,
		"name", name, "label", label,
		"tooltip", tooltip, "stock-id", stock_id, NULL);
}
