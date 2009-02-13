/*
 * e-shell-sidebar.c
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

#include "e-shell-sidebar.h"

#include <e-shell-view.h>

#define E_SHELL_SIDEBAR_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_SHELL_SIDEBAR, EShellSidebarPrivate))

struct _EShellSidebarPrivate {

	gpointer shell_view;  /* weak pointer */

	GtkWidget *event_box;
	GtkWidget *image;
	GtkWidget *primary_label;
	GtkWidget *secondary_label;
	gchar *primary_text;
	gchar *secondary_text;
};

enum {
	PROP_0,
	PROP_ICON_NAME,
	PROP_PRIMARY_TEXT,
	PROP_SECONDARY_TEXT,
	PROP_SHELL_VIEW
};

static gpointer parent_class;

static void
shell_sidebar_set_shell_view (EShellSidebar *shell_sidebar,
                              EShellView *shell_view)
{
	g_return_if_fail (shell_sidebar->priv->shell_view == NULL);

	shell_sidebar->priv->shell_view = shell_view;

	g_object_add_weak_pointer (
		G_OBJECT (shell_view),
		&shell_sidebar->priv->shell_view);
}

static void
shell_sidebar_set_property (GObject *object,
                            guint property_id,
                            const GValue *value,
                            GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_ICON_NAME:
			e_shell_sidebar_set_icon_name (
				E_SHELL_SIDEBAR (object),
				g_value_get_string (value));
			return;

		case PROP_PRIMARY_TEXT:
			e_shell_sidebar_set_primary_text (
				E_SHELL_SIDEBAR (object),
				g_value_get_string (value));
			return;

		case PROP_SECONDARY_TEXT:
			e_shell_sidebar_set_secondary_text (
				E_SHELL_SIDEBAR (object),
				g_value_get_string (value));
			return;

		case PROP_SHELL_VIEW:
			shell_sidebar_set_shell_view (
				E_SHELL_SIDEBAR (object),
				g_value_get_object (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
shell_sidebar_get_property (GObject *object,
                            guint property_id,
                            GValue *value,
                            GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_ICON_NAME:
			g_value_set_string (
				value, e_shell_sidebar_get_icon_name (
				E_SHELL_SIDEBAR (object)));
			return;

		case PROP_PRIMARY_TEXT:
			g_value_set_string (
				value, e_shell_sidebar_get_primary_text (
				E_SHELL_SIDEBAR (object)));
			return;

		case PROP_SECONDARY_TEXT:
			g_value_set_string (
				value, e_shell_sidebar_get_secondary_text (
				E_SHELL_SIDEBAR (object)));
			return;

		case PROP_SHELL_VIEW:
			g_value_set_object (
				value, e_shell_sidebar_get_shell_view (
				E_SHELL_SIDEBAR (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
shell_sidebar_dispose (GObject *object)
{
	EShellSidebarPrivate *priv;

	priv = E_SHELL_SIDEBAR_GET_PRIVATE (object);

	if (priv->shell_view != NULL) {
		g_object_remove_weak_pointer (
			G_OBJECT (priv->shell_view), &priv->shell_view);
		priv->shell_view = NULL;
	}

	if (priv->image != NULL) {
		g_object_unref (priv->image);
		priv->image = NULL;
	}

	if (priv->event_box != NULL) {
		g_object_unref (priv->event_box);
		priv->event_box = NULL;
	}

	if (priv->primary_label != NULL) {
		g_object_unref (priv->primary_label);
		priv->primary_label = NULL;
	}

	if (priv->secondary_label != NULL) {
		g_object_unref (priv->secondary_label);
		priv->secondary_label = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
shell_sidebar_finalize (GObject *object)
{
	EShellSidebarPrivate *priv;

	priv = E_SHELL_SIDEBAR_GET_PRIVATE (object);

	g_free (priv->primary_text);
	g_free (priv->secondary_text);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
shell_sidebar_constructed (GObject *object)
{
	EShellView *shell_view;
	EShellSidebar *shell_sidebar;
	GtkSizeGroup *size_group;
	GtkAction *action;
	GtkWidget *container;
	GtkWidget *widget;
	gchar *label;
	gchar *icon_name;

	shell_sidebar = E_SHELL_SIDEBAR (object);
	shell_view = e_shell_sidebar_get_shell_view (shell_sidebar);
	size_group = e_shell_view_get_size_group (shell_view);
	action = e_shell_view_get_action (shell_view);

	widget = shell_sidebar->priv->event_box;
	gtk_size_group_add_widget (size_group, widget);

	container = widget;

	widget = gtk_hbox_new (FALSE, 6);
	gtk_container_set_border_width (GTK_CONTAINER (widget), 6);
	gtk_container_add (GTK_CONTAINER (container), widget);
	gtk_widget_show (widget);

	container = widget;

	widget = gtk_image_new ();
	gtk_box_pack_start (GTK_BOX (container), widget, FALSE, FALSE, 0);
	shell_sidebar->priv->image = g_object_ref (widget);
	gtk_widget_show (widget);

	widget = gtk_label_new (NULL);
	gtk_label_set_ellipsize (GTK_LABEL (widget), PANGO_ELLIPSIZE_END);
	gtk_misc_set_alignment (GTK_MISC (widget), 0.0, 0.5);
	gtk_box_pack_start (GTK_BOX (container), widget, TRUE, TRUE, 0);
	shell_sidebar->priv->primary_label = g_object_ref (widget);
	gtk_widget_show (widget);

	widget = gtk_label_new (NULL);
	gtk_misc_set_alignment (GTK_MISC (widget), 1.0, 0.5);
	gtk_box_pack_start (GTK_BOX (container), widget, FALSE, FALSE, 0);
	shell_sidebar->priv->secondary_label = g_object_ref (widget);
	gtk_widget_show (widget);

	g_object_get (action, "icon-name", &icon_name, NULL);
	e_shell_sidebar_set_icon_name (shell_sidebar, icon_name);
	g_free (icon_name);

	g_object_get (action, "label", &label, NULL);
	e_shell_sidebar_set_primary_text (shell_sidebar, label);
	g_free (label);
}

static void
shell_sidebar_size_request (GtkWidget *widget,
                            GtkRequisition *requisition)
{
	EShellSidebarPrivate *priv;
	GtkRequisition child_requisition;
	GtkWidget *child;

	priv = E_SHELL_SIDEBAR_GET_PRIVATE (widget);

	requisition->width = 0;
	requisition->height = 0;

	child = gtk_bin_get_child (GTK_BIN (widget));
	gtk_widget_size_request (child, requisition);

	child = priv->event_box;
	gtk_widget_size_request (child, &child_requisition);
	requisition->width = MAX (requisition->width, child_requisition.width);
	requisition->height += child_requisition.height;
}

static void
shell_sidebar_size_allocate (GtkWidget *widget,
                             GtkAllocation *allocation)
{
	EShellSidebarPrivate *priv;
	GtkAllocation child_allocation;
	GtkRequisition child_requisition;
	GtkWidget *child;

	priv = E_SHELL_SIDEBAR_GET_PRIVATE (widget);

	widget->allocation = *allocation;

	child = priv->event_box;
	gtk_widget_size_request (child, &child_requisition);

	child_allocation.x = allocation->x;
	child_allocation.y = allocation->y;
	child_allocation.width = allocation->width;
	child_allocation.height = child_requisition.height;

	gtk_widget_size_allocate (child, &child_allocation);

	child_allocation.y += child_requisition.height;
	child_allocation.height =
		allocation->height - child_requisition.height;

	child = gtk_bin_get_child (GTK_BIN (widget));
	if (child != NULL)
		gtk_widget_size_allocate (child, &child_allocation);
}

static void
shell_sidebar_remove (GtkContainer *container,
                      GtkWidget *widget)
{
	EShellSidebarPrivate *priv;

	priv = E_SHELL_SIDEBAR_GET_PRIVATE (container);

	/* Look in the internal widgets first. */

	if (widget == priv->event_box) {
		gtk_widget_unparent (priv->event_box);
		gtk_widget_queue_resize (GTK_WIDGET (container));
		return;
	}

	/* Chain up to parent's remove() method. */
	GTK_CONTAINER_CLASS (parent_class)->remove (container, widget);
}

static void
shell_sidebar_forall (GtkContainer *container,
                      gboolean include_internals,
                      GtkCallback callback,
                      gpointer callback_data)
{
	EShellSidebarPrivate *priv;

	priv = E_SHELL_SIDEBAR_GET_PRIVATE (container);

	if (include_internals)
		callback (priv->event_box, callback_data);

	/* Chain up to parent's forall() method. */
	GTK_CONTAINER_CLASS (parent_class)->forall (
		container, include_internals, callback, callback_data);
}

static void
shell_sidebar_class_init (EShellSidebarClass *class)
{
	GObjectClass *object_class;
	GtkWidgetClass *widget_class;
	GtkContainerClass *container_class;

	parent_class = g_type_class_peek_parent (class);
	g_type_class_add_private (class, sizeof (EShellSidebarPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = shell_sidebar_set_property;
	object_class->get_property = shell_sidebar_get_property;
	object_class->dispose = shell_sidebar_dispose;
	object_class->finalize = shell_sidebar_finalize;
	object_class->constructed = shell_sidebar_constructed;

	widget_class = GTK_WIDGET_CLASS (class);
	widget_class->size_request = shell_sidebar_size_request;
	widget_class->size_allocate = shell_sidebar_size_allocate;

	container_class = GTK_CONTAINER_CLASS (class);
	container_class->remove = shell_sidebar_remove;
	container_class->forall = shell_sidebar_forall;

	/**
	 * EShellSidebar:icon-name
	 *
	 * The named icon is displayed at the top of the sidebar.
	 */
	g_object_class_install_property (
		object_class,
		PROP_ICON_NAME,
		g_param_spec_string (
			"icon-name",
			NULL,
			NULL,
			NULL,
			G_PARAM_READWRITE));

	/**
	 * EShellSidebar:primary-text
	 *
	 * The primary text is displayed in bold at the top of the sidebar.
	 **/
	g_object_class_install_property (
		object_class,
		PROP_PRIMARY_TEXT,
		g_param_spec_string (
			"primary-text",
			NULL,
			NULL,
			NULL,
			G_PARAM_READWRITE));

	/**
	 * EShellSidebar:secondary-text
	 *
	 * The secondary text is displayed in a smaller font at the top of
	 * the sidebar.
	 **/
	g_object_class_install_property (
		object_class,
		PROP_SECONDARY_TEXT,
		g_param_spec_string (
			"secondary-text",
			NULL,
			NULL,
			NULL,
			G_PARAM_READWRITE));

	/**
	 * EShellSidebar:shell-view
	 *
	 * The #EShellView to which the sidebar widget belongs.
	 **/
	g_object_class_install_property (
		object_class,
		PROP_SHELL_VIEW,
		g_param_spec_object (
			"shell-view",
			NULL,
			NULL,
			E_TYPE_SHELL_VIEW,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY));
}

static void
shell_sidebar_init (EShellSidebar *shell_sidebar)
{
	GtkStyle *style;
	GtkWidget *widget;
	const GdkColor *color;

	shell_sidebar->priv = E_SHELL_SIDEBAR_GET_PRIVATE (shell_sidebar);

	GTK_WIDGET_SET_FLAGS (shell_sidebar, GTK_NO_WINDOW);

	widget = gtk_event_box_new ();
	style = gtk_widget_get_style (widget);
	color = &style->bg[GTK_STATE_ACTIVE];
	gtk_widget_modify_bg (widget, GTK_STATE_NORMAL, color);
	gtk_widget_set_parent (widget, GTK_WIDGET (shell_sidebar));
	shell_sidebar->priv->event_box = g_object_ref (widget);
	gtk_widget_show (widget);

	/* Finish initialization once we have a shell view. */
}

GType
e_shell_sidebar_get_type (void)
{
	static GType type = 0;

	if (G_UNLIKELY (type == 0)) {
		static const GTypeInfo type_info = {
			sizeof (EShellSidebarClass),
			(GBaseInitFunc) NULL,
			(GBaseFinalizeFunc) NULL,
			(GClassInitFunc) shell_sidebar_class_init,
			(GClassFinalizeFunc) NULL,
			NULL,  /* class_data */
			sizeof (EShellSidebar),
			0,     /* n_preallocs */
			(GInstanceInitFunc) shell_sidebar_init,
			NULL   /* value_table */
		};

		type = g_type_register_static (
			GTK_TYPE_BIN, "EShellSidebar", &type_info, 0);
	}

	return type;
}

/**
 * e_shell_sidebar_new:
 * @shell_view: an #EShellView
 *
 * Creates a new #EShellSidebar instance belonging to @shell_view.
 *
 * Returns: a new #EShellSidebar instance
 **/
GtkWidget *
e_shell_sidebar_new (EShellView *shell_view)
{
	g_return_val_if_fail (E_IS_SHELL_VIEW (shell_view), NULL);

	return g_object_new (
		E_TYPE_SHELL_SIDEBAR,
		"shell-view", shell_view, NULL);
}

/**
 * e_shell_sidebar_check_state:
 * @shell_sidebar: an #EShellSidebar
 *
 * #EShellSidebar subclasses should implement the
 * <structfield>check_state</structfield> method in #EShellSidebarClass
 * to return a set of flags describing the current sidebar selection.
 * Subclasses are responsible for defining their own flags.  This is
 * primarily used to assist shell views with updating actions (see
 * e_shell_view_update_actions()).
 *
 * Returns: a set of flags describing the current @shell_sidebar selection
 **/
guint32
e_shell_sidebar_check_state (EShellSidebar *shell_sidebar)
{
	EShellSidebarClass *shell_sidebar_class;

	g_return_val_if_fail (E_IS_SHELL_SIDEBAR (shell_sidebar), 0);

	shell_sidebar_class = E_SHELL_SIDEBAR_GET_CLASS (shell_sidebar);
	g_return_val_if_fail (shell_sidebar_class->check_state != NULL, 0);

	return shell_sidebar_class->check_state (shell_sidebar);
}

/**
 * e_shell_sidebar_get_shell_view:
 * @shell_sidebar: an #EShellSidebar
 *
 * Returns the #EShellView that was passed to e_shell_sidebar_new().
 *
 * Returns: the #EShellView to which @shell_sidebar belongs
 **/
EShellView *
e_shell_sidebar_get_shell_view (EShellSidebar *shell_sidebar)
{
	g_return_val_if_fail (E_IS_SHELL_SIDEBAR (shell_sidebar), NULL);

	return E_SHELL_VIEW (shell_sidebar->priv->shell_view);
}

/**
 * e_shell_sidebar_get_icon_name:
 * @shell_sidebar: an #EShellSidebar
 *
 * Returns the icon name displayed at the top of the sidebar.
 *
 * Returns: the icon name for @shell_sidebar
 **/
const gchar *
e_shell_sidebar_get_icon_name (EShellSidebar *shell_sidebar)
{
	GtkImage *image;
	const gchar *icon_name;

	g_return_val_if_fail (E_IS_SHELL_SIDEBAR (shell_sidebar), NULL);

	image = GTK_IMAGE (shell_sidebar->priv->image);
	gtk_image_get_icon_name (image, &icon_name, NULL);

	return icon_name;
}

/**
 * e_shell_sidebar_set_icon_name:
 *
 * Sets the icon name displayed at the top of the sidebar.
 **/
void
e_shell_sidebar_set_icon_name (EShellSidebar *shell_sidebar,
                               const gchar *icon_name)
{
	GtkImage *image;

	g_return_if_fail (E_IS_SHELL_SIDEBAR (shell_sidebar));

	image = GTK_IMAGE (shell_sidebar->priv->image);
	gtk_image_set_from_icon_name (image, icon_name, GTK_ICON_SIZE_MENU);

	g_object_notify (G_OBJECT (shell_sidebar), "icon-name");
}

/**
 * e_shell_sidebar_get_primary_text:
 * @shell_sidebar: an #EShellSidebar
 *
 * Returns the primary text for @shell_sidebar.
 *
 * The primary text is displayed in bold at the top of the sidebar.  It
 * defaults to the shell view's label (as seen on the switcher button),
 * but typically shows the name of the selected item in the sidebar.
 *
 * Returns: the primary text for @shell_sidebar
 **/
const gchar *
e_shell_sidebar_get_primary_text (EShellSidebar *shell_sidebar)
{
	g_return_val_if_fail (E_IS_SHELL_SIDEBAR (shell_sidebar), NULL);

	return shell_sidebar->priv->primary_text;
}

/**
 * e_shell_sidebar_set_primary_text:
 * @shell_sidebar: an #EShellSidebar
 * @primary_text: text to be displayed in a bold font
 *
 * Sets the primary text for @shell_sidebar.
 *
 * The primary text is displayed in bold at the top of the sidebar.  It
 * defaults to the shell view's label (as seen on the switcher button),
 * but typically shows the name of the selected item in the sidebar.
 **/
void
e_shell_sidebar_set_primary_text (EShellSidebar *shell_sidebar,
                                  const gchar *primary_text)
{
	GtkLabel *label;
	gchar *markup;

	g_return_if_fail (E_IS_SHELL_SIDEBAR (shell_sidebar));

	g_free (shell_sidebar->priv->primary_text);
	shell_sidebar->priv->primary_text = g_strdup (primary_text);

	if (primary_text == NULL)
		primary_text = "";

	label = GTK_LABEL (shell_sidebar->priv->primary_label);
	markup = g_markup_printf_escaped ("<b>%s</b>", primary_text);
	gtk_label_set_markup (label, markup);
	g_free (markup);

	gtk_widget_queue_resize (GTK_WIDGET (shell_sidebar));
	g_object_notify (G_OBJECT (shell_sidebar), "primary-text");
}

/**
 * e_shell_sidebar_get_secondary_text:
 * @shell_sidebar: an #EShellSidebar
 *
 * Returns the secondary text for @shell_sidebar.
 *
 * The secondary text is displayed in a smaller font at the top of the
 * sidebar.  It typically shows information about the contents of the
 * selected sidebar item, such as total number of items, number of
 * selected items, etc.
 *
 * Returns: the secondary text for @shell_sidebar
 **/ 
const gchar *
e_shell_sidebar_get_secondary_text (EShellSidebar *shell_sidebar)
{
	g_return_val_if_fail (E_IS_SHELL_SIDEBAR (shell_sidebar), NULL);

	return shell_sidebar->priv->secondary_text;
}

/**
 * e_shell_sidebar_set_secondary_text:
 * @shell_sidebar: an #EShellSidebar
 * @secondary_text: text to be displayed in a smaller font
 *
 * Sets the secondary text for @shell_sidebar.
 *
 * The secondary text is displayed in a smaller font at the top of the
 * sidebar.  It typically shows information about the contents of the
 * selected sidebar item, such as total number of items, number of
 * selected items, etc.
 **/
void
e_shell_sidebar_set_secondary_text (EShellSidebar *shell_sidebar,
                                    const gchar *secondary_text)
{
	GtkLabel *label;
	gchar *markup;

	g_return_if_fail (E_IS_SHELL_SIDEBAR (shell_sidebar));

	g_free (shell_sidebar->priv->secondary_text);
	shell_sidebar->priv->secondary_text = g_strdup (secondary_text);

	if (secondary_text == NULL)
		secondary_text = "";

	label = GTK_LABEL (shell_sidebar->priv->secondary_label);
	markup = g_markup_printf_escaped ("<small>%s</small>", secondary_text);
	gtk_label_set_markup (label, markup);
	g_free (markup);

	gtk_widget_queue_resize (GTK_WIDGET (shell_sidebar));
	g_object_notify (G_OBJECT (shell_sidebar), "secondary-text");
}
