/*
 * anjal-shell-content.c
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

#include "anjal-shell-content.h"

#include <glib/gi18n.h>
#include <camel/camel-store.h>
#include <libedataserver/e-data-server-util.h>

#include "e-util/e-binding.h"
#include "e-util/gconf-bridge.h"
#include "widgets/misc/e-paned.h"

#include "em-search-context.h"
#include "em-utils.h"
#include "mail-config.h"
#include "mail-ops.h"

#include "e-mail-reader.h"
#include "e-mail-search-bar.h"
#include "anjal-shell-backend.h"
#include "anjal-shell-view-actions.h"
#include <misc/e-hinted-entry.h>

#define ANJAL_SHELL_CONTENT_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), ANJAL_TYPE_SHELL_CONTENT, AnjalShellContentPrivate))

#define STATE_KEY_SCROLLBAR_POSITION	"ScrollbarPosition"
#define STATE_KEY_SELECTED_MESSAGE	"SelectedMessage"

struct _AnjalShellContentPrivate {
	GtkWidget *view;
	GtkWidget *view_box;
	gboolean show_deleted;
	GtkWidget *search_entry;
};

enum {
	PROP_0,
	PROP_SHOW_DELETED
};

static gpointer parent_class;
static GType anjal_shell_content_type;


static void
anjal_shell_content_set_property (GObject *object,
                                 guint property_id,
                                 const GValue *value,
                                 GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_SHOW_DELETED:
			anjal_shell_content_set_show_deleted (
				ANJAL_SHELL_CONTENT (object),
				g_value_get_boolean (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
anjal_shell_content_get_property (GObject *object,
                                 guint property_id,
                                 GValue *value,
                                 GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_SHOW_DELETED:
			g_value_set_boolean (
				value,
				anjal_shell_content_get_show_deleted (
				ANJAL_SHELL_CONTENT (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
anjal_shell_content_dispose (GObject *object)
{
	AnjalShellContentPrivate *priv;

	priv = ANJAL_SHELL_CONTENT_GET_PRIVATE (object);

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
anjal_shell_content_constructed (GObject *object)
{
	AnjalShellContentPrivate *priv;
	EShellContent *shell_content;
	EShellBackend *shell_backend;
	EShellView *shell_view;
	EShellViewClass *shell_view_class;
	GtkWidget *container;

	priv = ANJAL_SHELL_CONTENT_GET_PRIVATE (object);

	/* Chain up to parent's constructed() method. */
	G_OBJECT_CLASS (parent_class)->constructed (object);

	shell_content = E_SHELL_CONTENT (object);
	shell_view = e_shell_content_get_shell_view (shell_content);
	shell_view_class = E_SHELL_VIEW_GET_CLASS (shell_view);
	shell_backend = e_shell_view_get_shell_backend (shell_view);

	/* Build content widgets. */

	container = GTK_WIDGET (object);

	//widget = e_paned_new (GTK_ORIENTATION_VERTICAL);
	//gtk_container_add (GTK_CONTAINER (container), widget);

	// Find a way to hook mail-view in here.
	priv->view_box = gtk_vbox_new (FALSE, 0);
	gtk_container_add (GTK_CONTAINER (container), priv->view_box);
	gtk_widget_show (priv->view_box);

}

void
anjal_shell_content_pack_view (EShellContent *shell_content, GtkWidget *view)
{
	AnjalShellContentPrivate *priv;
	priv = ANJAL_SHELL_CONTENT_GET_PRIVATE (shell_content);

	gtk_box_pack_start ((GtkBox *)priv->view_box, view, TRUE, TRUE, 0);
	priv->view = view;
}

static guint32
anjal_shell_content_check_state (EShellContent *shell_content)
{
	return 0;
}

static void
anjal_shell_construct_search_bar (EShellContent *shell_content)
{
	GtkWidget *widget = e_hinted_entry_new ();
	AnjalShellContentPrivate *priv;

	priv = ANJAL_SHELL_CONTENT_GET_PRIVATE (shell_content);


	priv->search_entry = widget;
	e_shell_content_set_search_entry (shell_content, (GtkEntry *)widget, FALSE);
	e_shell_content_show_search_bar (shell_content, FALSE);
}

GtkWidget *
anjal_shell_content_get_search_entry (EShellContent *shell_content)
{	
	AnjalShellContentPrivate *priv;

	priv = ANJAL_SHELL_CONTENT_GET_PRIVATE (shell_content);

	return priv->search_entry;
}

static void
anjal_content_size_allocate (GtkWidget *widget,
                             GtkAllocation *allocation)
{
	GtkAllocation child_allocation;
	GtkWidget *child;

	widget->allocation = *allocation;

	child_allocation.x = allocation->x;
	child_allocation.y = allocation->y;
	child_allocation.width = allocation->width;
	child_allocation.height = allocation->height;

	child = gtk_bin_get_child (GTK_BIN (widget));
	if (child != NULL)
		gtk_widget_size_allocate (child, &child_allocation);

}
static void
anjal_shell_content_class_init (AnjalShellContentClass *class)
{
	GObjectClass *object_class;
	EShellContentClass *shell_content_class;
	GtkWidgetClass *widget_class;
	
	parent_class = g_type_class_peek_parent (class);
	g_type_class_add_private (class, sizeof (AnjalShellContentPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = anjal_shell_content_set_property;
	object_class->get_property = anjal_shell_content_get_property;
	object_class->dispose = anjal_shell_content_dispose;
	object_class->constructed = anjal_shell_content_constructed;

	widget_class = GTK_WIDGET_CLASS (class);	
	widget_class->size_allocate = anjal_content_size_allocate;

	shell_content_class = E_SHELL_CONTENT_CLASS (class);
	shell_content_class->new_search_context = em_search_context_new;
	shell_content_class->check_state = anjal_shell_content_check_state;
	E_SHELL_CONTENT_CLASS(parent_class)->construct_search_bar = anjal_shell_construct_search_bar;

	g_object_class_install_property (
		object_class,
		PROP_SHOW_DELETED,
		g_param_spec_boolean (
			"show-deleted",
			"Show Deleted",
			NULL,
			FALSE,
			G_PARAM_READWRITE));

}

static void
anjal_shell_content_init (AnjalShellContent *mail_shell_content)
{
	mail_shell_content->priv =
		ANJAL_SHELL_CONTENT_GET_PRIVATE (mail_shell_content);

	/* Postpone widget construction until we have a shell view. */
}

GType
anjal_shell_content_get_type (void)
{
	return anjal_shell_content_type;
}

void
anjal_shell_content_register_type (GTypeModule *type_module)
{
	static const GTypeInfo type_info = {
		sizeof (AnjalShellContentClass),
		(GBaseInitFunc) NULL,
		(GBaseFinalizeFunc) NULL,
		(GClassInitFunc) anjal_shell_content_class_init,
		(GClassFinalizeFunc) NULL,
		NULL,  /* class_data */
		sizeof (AnjalShellContent),
		0,     /* n_preallocs */
		(GInstanceInitFunc) anjal_shell_content_init,
		NULL   /* value_table */
	};

	anjal_shell_content_type = g_type_module_register_type (
		type_module, E_TYPE_SHELL_CONTENT,
		"AnjalShellContent", &type_info, 0);

}

GtkWidget *
anjal_shell_content_new (EShellView *shell_view)
{
	g_return_val_if_fail (E_IS_SHELL_VIEW (shell_view), NULL);

	return g_object_new (
		ANJAL_TYPE_SHELL_CONTENT,
		"shell-view", shell_view, NULL);
}

gboolean
anjal_shell_content_get_show_deleted (AnjalShellContent *mail_shell_content)
{
	g_return_val_if_fail (
		ANJAL_IS_SHELL_CONTENT (mail_shell_content), FALSE);

	return mail_shell_content->priv->show_deleted;
}

void
anjal_shell_content_set_show_deleted (AnjalShellContent *mail_shell_content,
                                       gboolean show_deleted)
{
	g_return_if_fail (ANJAL_IS_SHELL_CONTENT (mail_shell_content));

	mail_shell_content->priv->show_deleted = show_deleted;

	g_object_notify (G_OBJECT (mail_shell_content), "show-deleted");
}


