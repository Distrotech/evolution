/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-memo-shell-view.c
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
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
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "e-memo-shell-view-private.h"

enum {
	PROP_0,
	PROP_SOURCE_LIST
};

GType e_memo_shell_view_type = 0;
static gpointer parent_class;

static void
memo_shell_view_get_property (GObject *object,
                              guint property_id,
                              GValue *value,
                              GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_SOURCE_LIST:
			g_value_set_object (
				value, e_memo_shell_view_get_source_list (
				E_MEMO_SHELL_VIEW (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
memo_shell_view_dispose (GObject *object)
{
	e_memo_shell_view_private_dispose (E_MEMO_SHELL_VIEW (object));

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
memo_shell_view_finalize (GObject *object)
{
	e_memo_shell_view_private_finalize (E_MEMO_SHELL_VIEW (object));

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
memo_shell_view_constructed (GObject *object)
{
	/* Chain up to parent's constructed() method. */
	G_OBJECT_CLASS (parent_class)->constructed (object);

	e_memo_shell_view_private_constructed (E_MEMO_SHELL_VIEW (object));
}

static void
memo_shell_view_changed (EShellView *shell_view)
{
	EMemoShellViewPrivate *priv;
	GtkActionGroup *action_group;
	gboolean visible;

	priv = E_MEMO_SHELL_VIEW_GET_PRIVATE (shell_view);

	action_group = priv->memo_actions;
	visible = e_shell_view_is_selected (shell_view);
	gtk_action_group_set_visible (action_group, visible);
}

static void
memo_shell_view_class_init (EMemoShellView *class,
                            GTypeModule *type_module)
{
	GObjectClass *object_class;
	EShellViewClass *shell_view_class;

	parent_class = g_type_class_peek_parent (class);
	g_type_class_add_private (class, sizeof (EMemoShellViewPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->get_property = memo_shell_view_get_property;
	object_class->dispose = memo_shell_view_dispose;
	object_class->finalize = memo_shell_view_finalize;
	object_class->constructed = memo_shell_view_constructed;

	shell_view_class = E_SHELL_VIEW_CLASS (class);
	shell_view_class->label = N_("Memos");
	shell_view_class->icon_name = "evolution-memos";
	shell_view_class->type_module = type_module;
	shell_view_class->changed = memo_shell_view_changed;
	shell_view_class->new_shell_sidebar = e_memo_shell_sidebar_new;

	g_object_class_install_property (
		object_class,
		PROP_SOURCE_LIST,
		g_param_spec_object (
			"source-list",
			_("Source List"),
			_("The registry of memo lists"),
			E_TYPE_SOURCE_LIST,
			G_PARAM_READABLE));
}

static void
memo_shell_view_init (EMemoShellView *memo_shell_view,
                      EShellViewClass *shell_view_class)
{
	memo_shell_view->priv =
		E_MEMO_SHELL_VIEW_GET_PRIVATE (memo_shell_view);

	e_memo_shell_view_private_init (memo_shell_view, shell_view_class);
}

GType
e_memo_shell_view_get_type (GTypeModule *type_module)
{
	if (e_memo_shell_view_type == 0) {
		const GTypeInfo type_info = {
			sizeof (EMemoShellViewClass),
			(GBaseInitFunc) NULL,
			(GBaseFinalizeFunc) NULL,
			(GClassInitFunc) memo_shell_view_class_init,
			(GClassFinalizeFunc) NULL,
			type_module,
			sizeof (EMemoShellView),
			0,    /* n_preallocs */
			(GInstanceInitFunc) memo_shell_view_init,
			NULL  /* value_table */
		};

		e_memo_shell_view_type =
			g_type_module_register_type (
				type_module, E_TYPE_SHELL_VIEW,
				"EMemoShellView", &type_info, 0);
	}

	return e_memo_shell_view_type;
}

ESourceList *
e_memo_shell_view_get_source_list (EMemoShellView *memo_shell_view)
{
	g_return_val_if_fail (E_IS_MEMO_SHELL_VIEW (memo_shell_view), NULL);

	return memo_shell_view->priv->source_list;
}
