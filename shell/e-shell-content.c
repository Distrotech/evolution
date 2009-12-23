/*
 * e-shell-content.c
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

#include "e-shell-content.h"

#include <glib/gi18n.h>

#include "e-util/e-binding.h"
#include "e-util/e-util.h"
#include "e-util/e-alert-dialog.h"
#include "filter/e-rule-editor.h"
#include "widgets/misc/e-action-combo-box.h"
#include "widgets/misc/e-hinted-entry.h"

#include "e-shell-backend.h"
#include "e-shell-searchbar.h"
#include "e-shell-view.h"
#include "e-shell-window-actions.h"

#define E_SHELL_CONTENT_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_SHELL_CONTENT, EShellContentPrivate))

struct _EShellContentPrivate {

	gpointer shell_view;  /* weak pointer */

	GtkWidget *searchbar;

	/* Custom search rules. */
	gchar *user_filename;
};

enum {
	PROP_0,
	PROP_SHELL_VIEW
};

static gpointer parent_class;

static void
shell_content_dialog_rule_changed (GtkWidget *dialog,
                                   EFilterRule *rule)
{
	gboolean sensitive;

	sensitive = (rule != NULL) && (rule->parts != NULL);

	gtk_dialog_set_response_sensitive (
		GTK_DIALOG (dialog), GTK_RESPONSE_OK, sensitive);
	gtk_dialog_set_response_sensitive (
		GTK_DIALOG (dialog), GTK_RESPONSE_APPLY, sensitive);
}

static void
shell_content_set_shell_view (EShellContent *shell_content,
                              EShellView *shell_view)
{
	g_return_if_fail (shell_content->priv->shell_view == NULL);

	shell_content->priv->shell_view = shell_view;

	g_object_add_weak_pointer (
		G_OBJECT (shell_view),
		&shell_content->priv->shell_view);
}

static void
shell_content_set_property (GObject *object,
                            guint property_id,
                            const GValue *value,
                            GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_SHELL_VIEW:
			shell_content_set_shell_view (
				E_SHELL_CONTENT (object),
				g_value_get_object (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
shell_content_get_property (GObject *object,
                            guint property_id,
                            GValue *value,
                            GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_SHELL_VIEW:
			g_value_set_object (
				value, e_shell_content_get_shell_view (
				E_SHELL_CONTENT (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
shell_content_dispose (GObject *object)
{
	EShellContentPrivate *priv;

	priv = E_SHELL_CONTENT_GET_PRIVATE (object);

	if (priv->shell_view != NULL) {
		g_object_remove_weak_pointer (
			G_OBJECT (priv->shell_view), &priv->shell_view);
		priv->shell_view = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
shell_content_constructed (GObject *object)
{
	EShellContentClass *class;
	EShellContent *shell_content;
	EShellBackend *shell_backend;
	EShellView *shell_view;
	GtkWidget *widget;
	const gchar *data_dir;

	shell_content = E_SHELL_CONTENT (object);
	shell_view = e_shell_content_get_shell_view (shell_content);
	shell_backend = e_shell_view_get_shell_backend (shell_view);

	/* XXX Regenerate the filename for custom saved search as done
	 *     in shell_view_init_search_context().  ERuleContext ought
	 *     to remember the filename when loading rules so you don't
	 *     have to keep passing it in when saving rules. */
	data_dir = e_shell_backend_get_data_dir (shell_backend);
	shell_content->priv->user_filename =
		g_build_filename (data_dir, "searches.xml", NULL);

	class = E_SHELL_CONTENT_GET_CLASS (shell_content);
	if (class->construct_searchbar != NULL)
		widget = class->construct_searchbar (shell_content);
	else
		widget = NULL;
	if (widget != NULL) {
		gtk_widget_set_parent (widget, GTK_WIDGET (shell_content));
		shell_content->priv->searchbar = g_object_ref (widget);
		gtk_widget_show (widget);
	}
}

static void
shell_content_destroy (GtkObject *gtk_object)
{
	EShellContentPrivate *priv;

	priv = E_SHELL_CONTENT_GET_PRIVATE (gtk_object);

	/* Unparent the widget before destroying it to avoid
	 * writing a custom GtkContainer::remove() method. */

	if (priv->searchbar != NULL) {
		gtk_widget_unparent (priv->searchbar);
		gtk_widget_destroy (priv->searchbar);
		g_object_unref (priv->searchbar);
		priv->searchbar = NULL;
	}

	/* Chain up to parent's destroy() method. */
	GTK_OBJECT_CLASS (parent_class)->destroy (gtk_object);
}

static void
shell_content_size_request (GtkWidget *widget,
                            GtkRequisition *requisition)
{
	EShellContentPrivate *priv;
	GtkRequisition child_requisition;
	GtkWidget *child;

	priv = E_SHELL_CONTENT_GET_PRIVATE (widget);

	requisition->width = 0;
	requisition->height = 0;

	child = gtk_bin_get_child (GTK_BIN (widget));
	gtk_widget_size_request (child, requisition);

	if (priv->searchbar == NULL)
		return;

	gtk_widget_size_request (priv->searchbar, &child_requisition);
	requisition->width = MAX (requisition->width, child_requisition.width);
	requisition->height += child_requisition.height;
}

static void
shell_content_size_allocate (GtkWidget *widget,
                             GtkAllocation *allocation)
{
	EShellContentPrivate *priv;
	GtkAllocation child_allocation;
	GtkRequisition child_requisition;
	GtkWidget *child;

	priv = E_SHELL_CONTENT_GET_PRIVATE (widget);

	widget->allocation = *allocation;

	child = priv->searchbar;

	if (child == NULL)
		child_requisition.height = 0;
	else
		gtk_widget_size_request (child, &child_requisition);

	child_allocation.x = allocation->x;
	child_allocation.y = allocation->y;
	child_allocation.width = allocation->width;
	child_allocation.height = child_requisition.height;

	if (child != NULL)
		gtk_widget_size_allocate (child, &child_allocation);

	child_allocation.y += child_requisition.height;
	child_allocation.height =
		allocation->height - child_requisition.height;

	child = gtk_bin_get_child (GTK_BIN (widget));
	if (child != NULL)
		gtk_widget_size_allocate (child, &child_allocation);
}

static void
shell_content_forall (GtkContainer *container,
                      gboolean include_internals,
                      GtkCallback callback,
                      gpointer callback_data)
{
	EShellContentPrivate *priv;

	priv = E_SHELL_CONTENT_GET_PRIVATE (container);

	if (include_internals && priv->searchbar != NULL && container == priv->searchbar->parent)
		callback (priv->searchbar, callback_data);

	/* Chain up to parent's forall() method. */
	GTK_CONTAINER_CLASS (parent_class)->forall (
		container, include_internals, callback, callback_data);
}

static gchar *
shell_content_get_search_name (EShellContent *shell_content)
{
	EShellSearchbar *searchbar;
	EShellView *shell_view;
	EFilterRule *rule;
	const gchar *search_text;

	shell_view = e_shell_content_get_shell_view (shell_content);

	rule = e_shell_view_get_search_rule (shell_view);
	g_return_val_if_fail (E_IS_FILTER_RULE (rule), NULL);

	searchbar = E_SHELL_SEARCHBAR (shell_content->priv->searchbar);
	search_text = e_shell_searchbar_get_search_text (searchbar);

	if (search_text == NULL || *search_text == '\0')
		search_text = "''";

	return g_strdup_printf ("%s %s", rule->name, search_text);
}

static GtkWidget *
shell_content_construct_searchbar (EShellContent *shell_content)
{
	EShellView *shell_view;

	shell_view = e_shell_content_get_shell_view (shell_content);

	return e_shell_searchbar_new (shell_view);
}

static void
shell_content_class_init (EShellContentClass *class)
{
	GObjectClass *object_class;
	GtkObjectClass *gtk_object_class;
	GtkWidgetClass *widget_class;
	GtkContainerClass *container_class;

	parent_class = g_type_class_peek_parent (class);
	g_type_class_add_private (class, sizeof (EShellContentPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = shell_content_set_property;
	object_class->get_property = shell_content_get_property;
	object_class->dispose = shell_content_dispose;
	object_class->constructed = shell_content_constructed;

	gtk_object_class = GTK_OBJECT_CLASS (class);
	gtk_object_class->destroy = shell_content_destroy;

	widget_class = GTK_WIDGET_CLASS (class);
	widget_class->size_request = shell_content_size_request;
	widget_class->size_allocate = shell_content_size_allocate;

	container_class = GTK_CONTAINER_CLASS (class);
	container_class->forall = shell_content_forall;

	class->get_search_name = shell_content_get_search_name;
	class->construct_searchbar = shell_content_construct_searchbar;

	/**
	 * EShellContent:shell-view
	 *
	 * The #EShellView to which the content widget belongs.
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
shell_content_init (EShellContent *shell_content)
{
	shell_content->priv = E_SHELL_CONTENT_GET_PRIVATE (shell_content);

	GTK_WIDGET_SET_FLAGS (shell_content, GTK_NO_WINDOW);
}

GType
e_shell_content_get_type (void)
{
	static GType type = 0;

	if (G_UNLIKELY (type == 0)) {
		static const GTypeInfo type_info = {
			sizeof (EShellContentClass),
			(GBaseInitFunc) NULL,
			(GBaseFinalizeFunc) NULL,
			(GClassInitFunc) shell_content_class_init,
			(GClassFinalizeFunc) NULL,
			NULL,  /* class_data */
			sizeof (EShellContent),
			0,     /* n_preallocs */
			(GInstanceInitFunc) shell_content_init,
			NULL   /* value_table */
		};

		type = g_type_register_static (
			GTK_TYPE_BIN, "EShellContent", &type_info, 0);
	}

	return type;
}

/**
 * e_shell_content_new:
 * @shell_view: an #EShellView
 *
 * Creates a new #EShellContent instance belonging to @shell_view.
 *
 * Returns: a new #EShellContent instance
 **/
GtkWidget *
e_shell_content_new (EShellView *shell_view)
{
	g_return_val_if_fail (E_IS_SHELL_VIEW (shell_view), NULL);

	return g_object_new (
		E_TYPE_SHELL_CONTENT, "shell-view", shell_view, NULL);
}

/**
 * e_shell_content_get_searchbar:
 * @shell_view: an #EShellView
 *
 * Returns the search bar widget returned by the
 * <structfield>construct_searchbar</structfield> method in
 * #EShellContentClass.
 *
 * Returns: the search bar widget
 **/
GtkWidget *
e_shell_content_get_searchbar (EShellContent *shell_content)
{
	g_return_val_if_fail (E_IS_SHELL_CONTENT (shell_content), NULL);

	return shell_content->priv->searchbar;
}

/**
 * e_shell_content_check_state:
 * @shell_content: an #EShellContent
 *
 * #EShellContent subclasses should implement the
 * <structfield>check_state</structfield> method in #EShellContentClass
 * to return a set of flags describing the current content selection.
 * Subclasses are responsible for defining their own flags.  This is
 * primarily used to assist shell views with updating actions (see
 * e_shell_view_update_actions()).
 *
 * Returns: a set of flags describing the current @shell_content selection
 **/
guint32
e_shell_content_check_state (EShellContent *shell_content)
{
	EShellContentClass *shell_content_class;

	g_return_val_if_fail (E_IS_SHELL_CONTENT (shell_content), 0);

	shell_content_class = E_SHELL_CONTENT_GET_CLASS (shell_content);
	g_return_val_if_fail (shell_content_class->check_state != NULL, 0);

	return shell_content_class->check_state (shell_content);
}

/**
 * e_shell_content_get_shell_view:
 * @shell_content: an #EShellContent
 *
 * Returns the #EShellView that was passed to e_shell_content_new().
 *
 * Returns: the #EShellView to which @shell_content belongs
 **/
EShellView *
e_shell_content_get_shell_view (EShellContent *shell_content)
{
	g_return_val_if_fail (E_IS_SHELL_CONTENT (shell_content), NULL);

	return E_SHELL_VIEW (shell_content->priv->shell_view);
}

/**
 * e_shell_content_get_search_name:
 * @shell_content: an #EShellContent
 *
 * Returns a newly-allocated string containing a suitable name for the
 * current search criteria.  This is used as the suggested name in the
 * Save Search dialog.  Free the returned string with g_free().
 *
 * Returns: a name for the current search criteria
 **/
gchar *
e_shell_content_get_search_name (EShellContent *shell_content)
{
	EShellContentClass *class;

	g_return_val_if_fail (E_IS_SHELL_CONTENT (shell_content), NULL);

	class = E_SHELL_CONTENT_GET_CLASS (shell_content);
	g_return_val_if_fail (class->get_search_name != NULL, NULL);

	return class->get_search_name (shell_content);
}

void
e_shell_content_run_advanced_search_dialog (EShellContent *shell_content)
{
	EShellView *shell_view;
	EShellWindow *shell_window;
	GtkWidget *dialog;
	GtkWidget *widget;
	EFilterRule *rule;
	ERuleContext *context;
	const gchar *user_filename;
	gint response;
	EAlert *alert = NULL;

	g_return_if_fail (E_IS_SHELL_CONTENT (shell_content));

	shell_view = e_shell_content_get_shell_view (shell_content);
	shell_window = e_shell_view_get_shell_window (shell_view);
	user_filename = shell_content->priv->user_filename;

	rule = e_shell_view_get_search_rule (shell_view);

	if (rule == NULL)
		rule = e_filter_rule_new ();
	else
		rule = e_filter_rule_clone (rule);

	context = E_SHELL_VIEW_GET_CLASS (shell_view)->search_context;
	widget = e_filter_rule_get_widget (rule, context);
	e_filter_rule_set_source (rule, E_FILTER_SOURCE_INCOMING);

	dialog = gtk_dialog_new_with_buttons (
		_("Advanced Search"), GTK_WINDOW (shell_window),
		GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_NO_SEPARATOR,
		GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
		GTK_STOCK_SAVE, GTK_RESPONSE_APPLY,
		GTK_STOCK_OK, GTK_RESPONSE_OK, NULL);

	gtk_container_set_border_width (GTK_CONTAINER (dialog), 7);
	gtk_container_set_border_width (GTK_CONTAINER (widget), 3);
	gtk_window_set_default_size (GTK_WINDOW (dialog), 600, 300);

	gtk_box_pack_start (
		GTK_BOX (GTK_DIALOG (dialog)->vbox), widget, TRUE, TRUE, 0);

	g_signal_connect_swapped (
		rule, "changed", G_CALLBACK (
		shell_content_dialog_rule_changed), dialog);

	shell_content_dialog_rule_changed (dialog, rule);

run:
	response = gtk_dialog_run (GTK_DIALOG (dialog));

	if (response != GTK_RESPONSE_OK && response != GTK_RESPONSE_APPLY)
		goto exit;

	if (!e_filter_rule_validate (rule, &alert)) {
		e_alert_run_dialog (GTK_WINDOW (dialog), alert);
		g_object_unref (alert);
		alert = NULL;
		goto run;
	}

	e_shell_view_custom_search (shell_view, rule);

	if (response == GTK_RESPONSE_APPLY) {
		if (!e_rule_context_find_rule (context, rule->name, rule->source))
			e_rule_context_add_rule (context, rule);
		e_rule_context_save (context, user_filename);
		goto run;
	}

exit:
	g_object_unref (rule);
	gtk_widget_destroy (dialog);
}

void
e_shell_content_run_edit_searches_dialog (EShellContent *shell_content)
{
	EShellView *shell_view;
	ERuleContext *context;
	ERuleEditor *editor;
	const gchar *user_filename;

	g_return_if_fail (E_IS_SHELL_CONTENT (shell_content));

	shell_view = e_shell_content_get_shell_view (shell_content);
	context = E_SHELL_VIEW_GET_CLASS (shell_view)->search_context;
	user_filename = shell_content->priv->user_filename;

	editor = e_rule_editor_new (
		context, E_FILTER_SOURCE_INCOMING, _("Searches"));
	gtk_window_set_title (GTK_WINDOW (editor), _("Searches"));

	if (gtk_dialog_run (GTK_DIALOG (editor)) == GTK_RESPONSE_OK)
		e_rule_context_save (context, user_filename);

	gtk_widget_destroy (GTK_WIDGET (editor));
}

void
e_shell_content_run_save_search_dialog (EShellContent *shell_content)
{
	EShellView *shell_view;
	EShellWindow *shell_window;
	GtkWidget *dialog;
	GtkWidget *widget;
	EFilterRule *rule;
	ERuleContext *context;
	const gchar *user_filename;
	gchar *search_name;
	gint response;
	EAlert *alert = NULL;

	g_return_if_fail (E_IS_SHELL_CONTENT (shell_content));

	shell_view = e_shell_content_get_shell_view (shell_content);
	shell_window = e_shell_view_get_shell_window (shell_view);
	user_filename = shell_content->priv->user_filename;

	rule = e_shell_view_get_search_rule (shell_view);
	g_return_if_fail (E_IS_FILTER_RULE (rule));
	rule = e_filter_rule_clone (rule);

	search_name = e_shell_content_get_search_name (shell_content);
	e_filter_rule_set_name (rule, search_name);
	g_free (search_name);

	context = E_SHELL_VIEW_GET_CLASS (shell_view)->search_context;
	widget = e_filter_rule_get_widget (rule, context);
	e_filter_rule_set_source (rule, E_FILTER_SOURCE_INCOMING);

	dialog = gtk_dialog_new_with_buttons (
		_("Save Search"), GTK_WINDOW (shell_window),
		GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_NO_SEPARATOR,
		GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
		GTK_STOCK_OK, GTK_RESPONSE_OK, NULL);

	gtk_container_set_border_width (GTK_CONTAINER (dialog), 7);
	gtk_container_set_border_width (GTK_CONTAINER (widget), 3);
	gtk_window_set_default_size (GTK_WINDOW (dialog), 500, 300);

	gtk_box_pack_start (
		GTK_BOX (GTK_DIALOG (dialog)->vbox), widget, TRUE, TRUE, 0);

	g_signal_connect_swapped (
		rule, "changed", G_CALLBACK (
		shell_content_dialog_rule_changed), dialog);

	shell_content_dialog_rule_changed (dialog, rule);

run:
	response = gtk_dialog_run (GTK_DIALOG (dialog));

	if (response != GTK_RESPONSE_OK)
		goto exit;

	if (!e_filter_rule_validate (rule, &alert)) {
		e_alert_run_dialog (GTK_WINDOW (dialog), alert);
		g_object_unref (alert);
		alert = NULL;
		goto run;
	}

	/* XXX This function steals the rule reference, so
	 *     counteract that by referencing it again. */
	e_rule_context_add_rule (context, g_object_ref (rule));

	e_rule_context_save (context, user_filename);

exit:
	g_object_unref (rule);
	gtk_widget_destroy (dialog);
}
