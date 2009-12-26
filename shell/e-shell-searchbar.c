/*
 * e-shell-searchbar.c
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

#include "e-shell-searchbar.h"

#include <config.h>
#include <glib/gi18n-lib.h>

#include "e-util/e-binding.h"
#include "widgets/misc/e-action-combo-box.h"
#include "widgets/misc/e-hinted-entry.h"

#include "e-shell-window-actions.h"

#define E_SHELL_SEARCHBAR_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_SHELL_SEARCHBAR, EShellSearchbarPrivate))

#define SEARCH_OPTION_ADVANCED		(-1)

#define STATE_KEY_SEARCH_FILTER		"SearchFilter"
#define STATE_KEY_SEARCH_SCOPE		"SearchScope"
#define STATE_KEY_SEARCH_TEXT		"SearchText"

struct _EShellSearchbarPrivate {

	gpointer shell_view;  /* weak pointer */

	GtkRadioAction *search_option;
	EFilterRule *search_rule;

	/* Child Widgets (not referenced) */
	GtkWidget *filter_combo_box;
	GtkWidget *search_entry;
	GtkWidget *scope_combo_box;

	guint filter_visible : 1;
	guint search_visible : 1;
	guint scope_visible  : 1;
	guint label_visible  : 1;
};

enum {
	PROP_0,
	PROP_FILTER_COMBO_BOX,
	PROP_FILTER_VISIBLE,
	PROP_SEARCH_HINT,
	PROP_SEARCH_OPTION,
	PROP_SEARCH_TEXT,
	PROP_SEARCH_VISIBLE,
	PROP_SCOPE_COMBO_BOX,
	PROP_SCOPE_VISIBLE,
	PROP_SHELL_VIEW,
	PROP_LABEL_VISIBLE,
};

static gpointer parent_class;

static void
shell_searchbar_update_search_widgets (EShellSearchbar *searchbar)
{
	EShellView *shell_view;
	EShellWindow *shell_window;
	GtkAction *action;
	GtkWidget *widget;
	const gchar *search_text;
	gboolean sensitive;

	/* EShellView subclasses are responsible for actually
	 * executing the search.  This is all cosmetic stuff. */

	widget = searchbar->priv->search_entry;
	shell_view = e_shell_searchbar_get_shell_view (searchbar);
	shell_window = e_shell_view_get_shell_window (shell_view);
	search_text = e_shell_searchbar_get_search_text (searchbar);

	sensitive =
		(search_text != NULL && *search_text != '\0') ||
		(e_shell_view_get_search_rule (shell_view) != NULL);

	if (sensitive) {
		GtkStyle *style;
		const GdkColor *color;

		style = gtk_widget_get_style (widget);
		color = &style->mid[GTK_STATE_SELECTED];

		gtk_widget_modify_base (widget, GTK_STATE_NORMAL, color);
		gtk_widget_modify_text (widget, GTK_STATE_NORMAL, NULL);
	} else {
		/* Text color will be updated when we move the focus. */
		gtk_widget_modify_base (widget, GTK_STATE_NORMAL, NULL);
	}

	action = E_SHELL_WINDOW_ACTION_SEARCH_CLEAR (shell_window);
	gtk_action_set_sensitive (action, sensitive);

	action = E_SHELL_WINDOW_ACTION_SEARCH_SAVE (shell_window);
	gtk_action_set_sensitive (action, sensitive);
}

static void
shell_searchbar_clear_search_cb (EShellView *shell_view,
                                 EShellSearchbar *searchbar)
{
	GtkRadioAction *search_option;
	gint current_value;

	e_shell_searchbar_set_search_text (searchbar, NULL);

	search_option = e_shell_searchbar_get_search_option (searchbar);
	if (search_option == NULL)
		return;

	/* Reset the search option if it's set to advanced search. */
	current_value = gtk_radio_action_get_current_value (search_option);
	if (current_value == SEARCH_OPTION_ADVANCED)
		gtk_radio_action_set_current_value (search_option, 0);
}

static void
shell_searchbar_custom_search_cb (EShellView *shell_view,
                                  EFilterRule *custom_rule,
                                  EShellSearchbar *searchbar)
{
	GtkRadioAction *search_option;
	gint value = SEARCH_OPTION_ADVANCED;

	e_shell_searchbar_set_search_text (searchbar, NULL);

	search_option = e_shell_searchbar_get_search_option (searchbar);
	if (search_option != NULL)
		gtk_radio_action_set_current_value (search_option, value);
}

static void
shell_searchbar_execute_search_cb (EShellView *shell_view,
                                   EShellSearchbar *searchbar)
{
	GtkWidget *widget;

	shell_searchbar_update_search_widgets (searchbar);

	if (!e_shell_view_is_active (shell_view))
		return;

	/* Direct the focus away from the search entry, so that a
	 * focus-in event is required before the text can be changed.
	 * This will reset the entry to the appropriate visual state. */
	widget = searchbar->priv->search_entry;
	gtk_widget_child_focus (widget, GTK_DIR_TAB_FORWARD);
}

static void
shell_searchbar_entry_activate_cb (EShellSearchbar *searchbar)
{
	EShellView *shell_view;
	EShellWindow *shell_window;
	GtkAction *action;
	const gchar *search_text;

	shell_view = e_shell_searchbar_get_shell_view (searchbar);
	shell_window = e_shell_view_get_shell_window (shell_view);

	search_text = e_shell_searchbar_get_search_text (searchbar);
	if (search_text != NULL && *search_text != '\0')
		action = E_SHELL_WINDOW_ACTION_SEARCH_QUICK (shell_window);
	else
		action = E_SHELL_WINDOW_ACTION_SEARCH_CLEAR (shell_window);

	gtk_action_activate (action);
}

static void
shell_searchbar_entry_changed_cb (EShellSearchbar *searchbar)
{
	EShellView *shell_view;
	EShellWindow *shell_window;
	GtkAction *action;
	const gchar *search_text;
	gboolean sensitive;

	shell_view = e_shell_searchbar_get_shell_view (searchbar);
	shell_window = e_shell_view_get_shell_window (shell_view);

	search_text = e_shell_searchbar_get_search_text (searchbar);
	sensitive = (search_text != NULL && *search_text != '\0');

	action = E_SHELL_WINDOW_ACTION_SEARCH_QUICK (shell_window);
	gtk_action_set_sensitive (action, sensitive);
}

static void
shell_searchbar_entry_icon_press_cb (EShellSearchbar *searchbar,
                                     GtkEntryIconPosition icon_pos,
                                     GdkEvent *event)
{
	EShellView *shell_view;
	EShellWindow *shell_window;
	GtkAction *action;

	/* Show the search options menu when the icon is pressed. */

	if (icon_pos != GTK_ENTRY_ICON_PRIMARY)
		return;

	shell_view = e_shell_searchbar_get_shell_view (searchbar);
	shell_window = e_shell_view_get_shell_window (shell_view);

	action = E_SHELL_WINDOW_ACTION_SEARCH_OPTIONS (shell_window);
	gtk_action_activate (action);
}

static void
shell_searchbar_entry_icon_release_cb (EShellSearchbar *searchbar,
                                       GtkEntryIconPosition icon_pos,
                                       GdkEvent *event)
{
	EShellView *shell_view;
	EShellWindow *shell_window;
	GtkAction *action;

	/* Clear the search when the icon is pressed and released. */

	if (icon_pos != GTK_ENTRY_ICON_SECONDARY)
		return;

	shell_view = e_shell_searchbar_get_shell_view (searchbar);
	shell_window = e_shell_view_get_shell_window (shell_view);

	action = E_SHELL_WINDOW_ACTION_SEARCH_CLEAR (shell_window);
	gtk_action_activate (action);
}

static gboolean
shell_searchbar_entry_key_press_cb (EShellSearchbar *searchbar,
                                    GdkEventKey *key_event,
                                    GtkWindow *entry)
{
	EShellView *shell_view;
	EShellWindow *shell_window;
	GtkAction *action;
	guint mask;

	mask = gtk_accelerator_get_default_mod_mask ();
	if ((key_event->state & mask) != GDK_MOD1_MASK)
		return FALSE;

	if (key_event->keyval != GDK_Down)
		return FALSE;

	shell_view = e_shell_searchbar_get_shell_view (searchbar);
	shell_window = e_shell_view_get_shell_window (shell_view);

	action = E_SHELL_WINDOW_ACTION_SEARCH_OPTIONS (shell_window);
	gtk_action_activate (action);

	return TRUE;
}

static void
shell_searchbar_option_changed_cb (GtkRadioAction *action,
                                   GtkRadioAction *current,
                                   EShellSearchbar *searchbar)
{
	EShellView *shell_view;
	const gchar *search_text;
	const gchar *label;
	gint current_value;

	shell_view = e_shell_searchbar_get_shell_view (searchbar);

	label = gtk_action_get_label (GTK_ACTION (current));
	e_shell_searchbar_set_search_hint (searchbar, label);

	current_value = gtk_radio_action_get_current_value (current);
	search_text = e_shell_searchbar_get_search_text (searchbar);

	if (current_value != SEARCH_OPTION_ADVANCED) {
		e_shell_view_set_search_rule (shell_view, NULL);
		e_shell_searchbar_set_search_text (searchbar, search_text);
		if (search_text != NULL && *search_text != '\0')
			e_shell_view_execute_search (shell_view);
	} else if (search_text != NULL)
		e_shell_searchbar_set_search_text (searchbar, NULL);
}

static void
shell_searchbar_set_shell_view (EShellSearchbar *searchbar,
                                EShellView *shell_view)
{
	g_return_if_fail (searchbar->priv->shell_view == NULL);

	searchbar->priv->shell_view = shell_view;

	g_object_add_weak_pointer (
		G_OBJECT (shell_view),
		&searchbar->priv->shell_view);
}

static void
shell_searchbar_set_property (GObject *object,
                              guint property_id,
                              const GValue *value,
                              GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_LABEL_VISIBLE:
			e_shell_searchbar_set_label_visible (
				E_SHELL_SEARCHBAR (object),
				g_value_get_boolean (value));
			return;
		case PROP_FILTER_VISIBLE:
			e_shell_searchbar_set_filter_visible (
				E_SHELL_SEARCHBAR (object),
				g_value_get_boolean (value));
			return;

		case PROP_SEARCH_HINT:
			e_shell_searchbar_set_search_hint (
				E_SHELL_SEARCHBAR (object),
				g_value_get_string (value));
			return;

		case PROP_SEARCH_OPTION:
			e_shell_searchbar_set_search_option (
				E_SHELL_SEARCHBAR (object),
				g_value_get_object (value));
			return;

		case PROP_SEARCH_TEXT:
			e_shell_searchbar_set_search_text (
				E_SHELL_SEARCHBAR (object),
				g_value_get_string (value));
			return;

		case PROP_SEARCH_VISIBLE:
			e_shell_searchbar_set_search_visible (
				E_SHELL_SEARCHBAR (object),
				g_value_get_boolean (value));
			return;

		case PROP_SCOPE_VISIBLE:
			e_shell_searchbar_set_scope_visible (
				E_SHELL_SEARCHBAR (object),
				g_value_get_boolean (value));
			return;

		case PROP_SHELL_VIEW:
			shell_searchbar_set_shell_view (
				E_SHELL_SEARCHBAR (object),
				g_value_get_object (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
shell_searchbar_get_property (GObject *object,
                              guint property_id,
                              GValue *value,
                              GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_FILTER_COMBO_BOX:
			g_value_set_object (
				value, e_shell_searchbar_get_filter_combo_box (
				E_SHELL_SEARCHBAR (object)));
			return;

		case PROP_LABEL_VISIBLE:
			g_value_set_boolean (
				value, e_shell_searchbar_get_label_visible (
				E_SHELL_SEARCHBAR (object)));
			return;

		case PROP_FILTER_VISIBLE:
			g_value_set_boolean (
				value, e_shell_searchbar_get_filter_visible (
				E_SHELL_SEARCHBAR (object)));
			return;

		case PROP_SEARCH_HINT:
			g_value_set_string (
				value, e_shell_searchbar_get_search_hint (
				E_SHELL_SEARCHBAR (object)));
			return;

		case PROP_SEARCH_OPTION:
			g_value_set_object (
				value, e_shell_searchbar_get_search_option (
				E_SHELL_SEARCHBAR (object)));
			return;

		case PROP_SEARCH_TEXT:
			g_value_set_string (
				value, e_shell_searchbar_get_search_text (
				E_SHELL_SEARCHBAR (object)));
			return;

		case PROP_SEARCH_VISIBLE:
			g_value_set_boolean (
				value, e_shell_searchbar_get_search_visible (
				E_SHELL_SEARCHBAR (object)));
			return;

		case PROP_SCOPE_COMBO_BOX:
			g_value_set_object (
				value, e_shell_searchbar_get_scope_combo_box (
				E_SHELL_SEARCHBAR (object)));
			return;

		case PROP_SCOPE_VISIBLE:
			g_value_set_boolean (
				value, e_shell_searchbar_get_scope_visible (
				E_SHELL_SEARCHBAR (object)));
			return;

		case PROP_SHELL_VIEW:
			g_value_set_object (
				value, e_shell_searchbar_get_shell_view (
				E_SHELL_SEARCHBAR (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
shell_searchbar_dispose (GObject *object)
{
	EShellSearchbarPrivate *priv;

	priv = E_SHELL_SEARCHBAR_GET_PRIVATE (object);

	if (priv->shell_view != NULL) {
		g_object_remove_weak_pointer (
			G_OBJECT (priv->shell_view), &priv->shell_view);
		priv->shell_view = NULL;
	}

	if (priv->search_option != NULL) {
		g_signal_handlers_disconnect_matched (
			priv->search_option, G_SIGNAL_MATCH_DATA,
			0, 0, NULL, NULL, object);
		g_object_unref (priv->search_option);
		priv->search_option = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
shell_searchbar_constructed (GObject *object)
{
	EShellView *shell_view;
	EShellWindow *shell_window;
	EShellSearchbar *searchbar;
	GtkSizeGroup *size_group;
	GtkAction *action;
	GtkWidget *widget;

	searchbar = E_SHELL_SEARCHBAR (object);
	shell_view = e_shell_searchbar_get_shell_view (searchbar);
	shell_window = e_shell_view_get_shell_window (shell_view);
	size_group = e_shell_view_get_size_group (shell_view);

	g_signal_connect (
		shell_view, "clear-search",
		G_CALLBACK (shell_searchbar_clear_search_cb),
		searchbar);

	g_signal_connect (
		shell_view, "custom-search",
		G_CALLBACK (shell_searchbar_custom_search_cb),
		searchbar);

	g_signal_connect_after (
		shell_view, "execute-search",
		G_CALLBACK (shell_searchbar_execute_search_cb),
		searchbar);

	widget = searchbar->priv->search_entry;

	action = E_SHELL_WINDOW_ACTION_SEARCH_CLEAR (shell_window);

	e_binding_new (
		action, "sensitive",
		widget, "secondary-icon-sensitive");
	e_binding_new (
		action, "stock-id",
		widget, "secondary-icon-stock");
	e_binding_new (
		action, "tooltip",
		widget, "secondary-icon-tooltip-text");

	action = E_SHELL_WINDOW_ACTION_SEARCH_OPTIONS (shell_window);

	e_binding_new (
		action, "sensitive",
		widget, "primary-icon-sensitive");
	e_binding_new (
		action, "stock-id",
		widget, "primary-icon-stock");
	e_binding_new (
		action, "tooltip",
		widget, "primary-icon-tooltip-text");

	widget = GTK_WIDGET (searchbar);
	gtk_size_group_add_widget (size_group, widget);
}

static void
shell_searchbar_class_init (EShellSearchbarClass *class)
{
	GObjectClass *object_class;

	parent_class = g_type_class_peek_parent (class);
	g_type_class_add_private (class, sizeof (EShellSearchbarPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = shell_searchbar_set_property;
	object_class->get_property = shell_searchbar_get_property;
	object_class->dispose = shell_searchbar_dispose;
	object_class->constructed = shell_searchbar_constructed;

	g_object_class_install_property (
		object_class,
		PROP_FILTER_COMBO_BOX,
		g_param_spec_object (
			"filter-combo-box",
			NULL,
			NULL,
			E_TYPE_ACTION_COMBO_BOX,
			G_PARAM_READABLE));

	g_object_class_install_property (
		object_class,
		PROP_LABEL_VISIBLE,
		g_param_spec_boolean (
			"label-visible",
			NULL,
			NULL,
			TRUE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT));

	g_object_class_install_property (
		object_class,
		PROP_FILTER_VISIBLE,
		g_param_spec_boolean (
			"filter-visible",
			NULL,
			NULL,
			TRUE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT));

	g_object_class_install_property (
		object_class,
		PROP_SEARCH_HINT,
		g_param_spec_string (
			"search-hint",
			NULL,
			NULL,
			NULL,
			G_PARAM_READWRITE));

	g_object_class_install_property (
		object_class,
		PROP_SEARCH_OPTION,
		g_param_spec_object (
			"search-option",
			NULL,
			NULL,
			GTK_TYPE_RADIO_ACTION,
			G_PARAM_READWRITE));

	g_object_class_install_property (
		object_class,
		PROP_SEARCH_TEXT,
		g_param_spec_string (
			"search-text",
			NULL,
			NULL,
			NULL,
			G_PARAM_READWRITE));

	g_object_class_install_property (
		object_class,
		PROP_SEARCH_VISIBLE,
		g_param_spec_boolean (
			"search-visible",
			NULL,
			NULL,
			TRUE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT));

	g_object_class_install_property (
		object_class,
		PROP_SCOPE_COMBO_BOX,
		g_param_spec_object (
			"scope-combo-box",
			NULL,
			NULL,
			E_TYPE_ACTION_COMBO_BOX,
			G_PARAM_READABLE));

	g_object_class_install_property (
		object_class,
		PROP_SCOPE_VISIBLE,
		g_param_spec_boolean (
			"scope-visible",
			NULL,
			NULL,
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT));

	/**
	 * EShellContent:shell-view
	 *
	 * The #EShellView to which the searchbar widget belongs.
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
shell_searchbar_init (EShellSearchbar *searchbar)
{
	GtkBox *box;
	GtkLabel *label;
	GtkWidget *widget;

	searchbar->priv = E_SHELL_SEARCHBAR_GET_PRIVATE (searchbar);

	gtk_box_set_spacing (GTK_BOX (searchbar), 24);

	/* Filter Combo Widgets */

	box = GTK_BOX (searchbar);

	widget = gtk_hbox_new (FALSE, 3);
	gtk_box_pack_start (box, widget, FALSE, FALSE, 0);

	e_binding_new (
		searchbar, "filter-visible",
		widget, "visible");

	box = GTK_BOX (widget);

	/* Translators: The "Show:" label precedes a combo box that
	 * allows the user to filter the current view.  Examples of
	 * items that appear in the combo box are "Unread Messages",
	 * "Important Messages", or "Active Appointments". */
	widget = gtk_label_new_with_mnemonic (_("Sho_w:"));
	gtk_box_pack_start (box, widget, FALSE, FALSE, 0);
	gtk_widget_show (widget);

	label = GTK_LABEL (widget);

	widget = e_action_combo_box_new ();
	gtk_label_set_mnemonic_widget (label, widget);
	gtk_box_pack_start (box, widget, FALSE, FALSE, 0);
	searchbar->priv->filter_combo_box = widget;
	gtk_widget_show (widget);

	/* Search Entry Widgets */

	box = GTK_BOX (searchbar);

	widget = gtk_hbox_new (FALSE, 3);
	gtk_box_pack_start (box, widget, TRUE, TRUE, 0);

	e_binding_new (
		searchbar, "search-visible",
		widget, "visible");

	box = GTK_BOX (widget);

	/* Translators: This is part of the quick search interface.
	 * example: Search: [_______________] in [ Current Folder ] */
	widget = gtk_label_new_with_mnemonic (_("Sear_ch:"));
	gtk_box_pack_start (box, widget, FALSE, FALSE, 0);
	gtk_widget_show (widget);

	label = GTK_LABEL (widget);
	e_binding_new (
		searchbar, "label-visible",
		widget, "visible");

	widget = e_hinted_entry_new ();
	gtk_label_set_mnemonic_widget (label, widget);
	gtk_box_pack_start (box, widget, TRUE, TRUE, 0);
	searchbar->priv->search_entry = widget;
	gtk_widget_show (widget);

	g_signal_connect_swapped (
		widget, "activate",
		G_CALLBACK (shell_searchbar_entry_activate_cb),
		searchbar);

	g_signal_connect_swapped (
		widget, "changed",
		G_CALLBACK (shell_searchbar_entry_changed_cb),
		searchbar);

	g_signal_connect_swapped (
		widget, "icon-press",
		G_CALLBACK (shell_searchbar_entry_icon_press_cb),
		searchbar);

	g_signal_connect_swapped (
		widget, "icon-release",
		G_CALLBACK (shell_searchbar_entry_icon_release_cb),
		searchbar);

	g_signal_connect_swapped (
		widget, "key-press-event",
		G_CALLBACK (shell_searchbar_entry_key_press_cb),
		searchbar);

	/* Scope Combo Widgets */

	box = GTK_BOX (searchbar);

	widget = gtk_hbox_new (FALSE, 3);
	gtk_box_pack_start (box, widget, FALSE, FALSE, 0);

	e_binding_new (
		searchbar, "scope-visible",
		widget, "visible");

	box = GTK_BOX (widget);

	/* Translators: This is part of the quick search interface.
	 * example: Search: [_______________] in [ Current Folder ] */
	widget = gtk_label_new_with_mnemonic (_("i_n"));
	gtk_box_pack_start (box, widget, FALSE, FALSE, 0);
	gtk_widget_show (widget);

	label = GTK_LABEL (widget);

	widget = e_action_combo_box_new ();
	gtk_label_set_mnemonic_widget (label, widget);
	gtk_box_pack_start (box, widget, FALSE, FALSE, 0);
	searchbar->priv->scope_combo_box = widget;
	gtk_widget_show (widget);
}

GType
e_shell_searchbar_get_type (void)
{
	static GType type = 0;

	if (G_UNLIKELY (type == 0)) {
		static const GTypeInfo type_info = {
			sizeof (EShellSearchbarClass),
			(GBaseInitFunc) NULL,
			(GBaseFinalizeFunc) NULL,
			(GClassInitFunc) shell_searchbar_class_init,
			(GClassFinalizeFunc) NULL,
			NULL,  /* class_data */
			sizeof (EShellSearchbar),
			0,     /* n_preallocs */
			(GInstanceInitFunc) shell_searchbar_init,
			NULL   /* value_table */
		};

		type = g_type_register_static (
			GTK_TYPE_BOX, "EShellSearchbar", &type_info, 0);
	}

	return type;
}

/**
 * e_shell_searchbar_new:
 * @shell_view: an #EShellView
 *
 * Creates a new #EShellSearchbar instance.
 *
 * Returns: a new #EShellSearchbar instance
 **/
GtkWidget *
e_shell_searchbar_new (EShellView *shell_view)
{
	g_return_val_if_fail (E_IS_SHELL_VIEW (shell_view), NULL);

	return g_object_new (
		E_TYPE_SHELL_SEARCHBAR, "shell-view", shell_view, NULL);
}

/**
 * e_shell_searchbar_get_shell_view:
 * @searchbar: an #EShellSearchbar
 *
 * Returns the #EShellView that was passed to e_shell_searchbar_new().
 *
 * Returns: the #EShellView to which @searchbar belongs
 **/
EShellView *
e_shell_searchbar_get_shell_view (EShellSearchbar *searchbar)
{
	g_return_val_if_fail (E_IS_SHELL_SEARCHBAR (searchbar), NULL);

	return E_SHELL_VIEW (searchbar->priv->shell_view);
}

EActionComboBox *
e_shell_searchbar_get_filter_combo_box (EShellSearchbar *searchbar)
{
	g_return_val_if_fail (E_IS_SHELL_SEARCHBAR (searchbar), NULL);

	return E_ACTION_COMBO_BOX (searchbar->priv->filter_combo_box);
}

gboolean
e_shell_searchbar_get_label_visible (EShellSearchbar *searchbar)
{
	g_return_val_if_fail (E_IS_SHELL_SEARCHBAR (searchbar), FALSE);

	return searchbar->priv->label_visible;
}

void
e_shell_searchbar_set_label_visible (EShellSearchbar *searchbar,
                                      gboolean label_visible)
{
	g_return_if_fail (E_IS_SHELL_SEARCHBAR (searchbar));

	searchbar->priv->label_visible = label_visible;

	g_object_notify (G_OBJECT (searchbar), "label-visible");
}

gboolean
e_shell_searchbar_get_filter_visible (EShellSearchbar *searchbar)
{
	g_return_val_if_fail (E_IS_SHELL_SEARCHBAR (searchbar), FALSE);

	return searchbar->priv->filter_visible;
}

void
e_shell_searchbar_set_filter_visible (EShellSearchbar *searchbar,
                                      gboolean filter_visible)
{
	g_return_if_fail (E_IS_SHELL_SEARCHBAR (searchbar));

	searchbar->priv->filter_visible = filter_visible;

	g_object_notify (G_OBJECT (searchbar), "filter-visible");
}

const gchar *
e_shell_searchbar_get_search_hint (EShellSearchbar *searchbar)
{
	EHintedEntry *entry;

	g_return_val_if_fail (E_IS_SHELL_SEARCHBAR (searchbar), NULL);

	entry = E_HINTED_ENTRY (searchbar->priv->search_entry);

	return e_hinted_entry_get_hint (entry);
}

void
e_shell_searchbar_set_search_hint (EShellSearchbar *searchbar,
                                   const gchar *search_hint)
{
	EHintedEntry *entry;

	g_return_if_fail (E_IS_SHELL_SEARCHBAR (searchbar));

	entry = E_HINTED_ENTRY (searchbar->priv->search_entry);

	e_hinted_entry_set_hint (entry, search_hint);

	g_object_notify (G_OBJECT (searchbar), "search-hint");
}

GtkRadioAction *
e_shell_searchbar_get_search_option (EShellSearchbar *searchbar)
{
	g_return_val_if_fail (E_IS_SHELL_SEARCHBAR (searchbar), NULL);

	return searchbar->priv->search_option;
}

void
e_shell_searchbar_set_search_option (EShellSearchbar *searchbar,
                                     GtkRadioAction *search_option)
{
	g_return_if_fail (E_IS_SHELL_SEARCHBAR (searchbar));

	if (search_option != NULL) {
		g_return_if_fail (GTK_IS_RADIO_ACTION (search_option));
		g_object_ref (search_option);
	}

	if (searchbar->priv->search_option != NULL) {
		g_signal_handlers_disconnect_matched (
			searchbar->priv->search_option,
			G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL,
			searchbar);
		g_object_unref (searchbar->priv->search_option);
	}

	searchbar->priv->search_option = search_option;

	if (search_option != NULL)
		g_signal_connect (
			search_option, "changed",
			G_CALLBACK (shell_searchbar_option_changed_cb),
			searchbar);

	g_object_notify (G_OBJECT (searchbar), "search-option");
}

const gchar *
e_shell_searchbar_get_search_text (EShellSearchbar *searchbar)
{
	EHintedEntry *entry;

	g_return_val_if_fail (E_IS_SHELL_SEARCHBAR (searchbar), NULL);

	entry = E_HINTED_ENTRY (searchbar->priv->search_entry);

	return e_hinted_entry_get_text (entry);
}

void
e_shell_searchbar_set_search_text (EShellSearchbar *searchbar,
                                   const gchar *search_text)
{
	EHintedEntry *entry;

	g_return_if_fail (E_IS_SHELL_SEARCHBAR (searchbar));

	entry = E_HINTED_ENTRY (searchbar->priv->search_entry);

	e_hinted_entry_set_text (entry, search_text);

	shell_searchbar_update_search_widgets (searchbar);

	g_object_notify (G_OBJECT (searchbar), "search-text");
}

gboolean
e_shell_searchbar_get_search_visible (EShellSearchbar *searchbar)
{
	g_return_val_if_fail (E_IS_SHELL_SEARCHBAR (searchbar), FALSE);

	return searchbar->priv->search_visible;
}

void
e_shell_searchbar_set_search_visible (EShellSearchbar *searchbar,
                                      gboolean search_visible)
{
	g_return_if_fail (E_IS_SHELL_SEARCHBAR (searchbar));

	searchbar->priv->search_visible = search_visible;

	g_object_notify (G_OBJECT (searchbar), "search-visible");
}

EActionComboBox *
e_shell_searchbar_get_scope_combo_box (EShellSearchbar *searchbar)
{
	g_return_val_if_fail (E_IS_SHELL_SEARCHBAR (searchbar), NULL);

	return E_ACTION_COMBO_BOX (searchbar->priv->scope_combo_box);
}

gboolean
e_shell_searchbar_get_scope_visible (EShellSearchbar *searchbar)
{
	g_return_val_if_fail (E_IS_SHELL_SEARCHBAR (searchbar), FALSE);

	return searchbar->priv->scope_visible;
}

void
e_shell_searchbar_set_scope_visible (EShellSearchbar *searchbar,
                                      gboolean scope_visible)
{
	g_return_if_fail (E_IS_SHELL_SEARCHBAR (searchbar));

	searchbar->priv->scope_visible = scope_visible;

	g_object_notify (G_OBJECT (searchbar), "scope-visible");
}

void
e_shell_searchbar_restore_state (EShellSearchbar *searchbar,
                                 const gchar *group_name)
{
	EShellView *shell_view;
	EShellWindow *shell_window;
	GKeyFile *key_file;
	GtkAction *action;
	GtkWidget *widget;
	const gchar *search_text;
	const gchar *key;
	gchar *string;

	g_return_if_fail (E_IS_SHELL_SEARCHBAR (searchbar));
	g_return_if_fail (group_name != NULL);

	shell_view = e_shell_searchbar_get_shell_view (searchbar);
	shell_window = e_shell_view_get_shell_window (shell_view);
	key_file = e_shell_view_get_state_key_file (shell_view);

	/* Changing the combo boxes triggers searches, so block
	 * the search action until the state is fully restored. */
	action = E_SHELL_WINDOW_ACTION_SEARCH_QUICK (shell_window);
	gtk_action_block_activate (action);

	e_shell_view_block_execute_search (shell_view);

	key = STATE_KEY_SEARCH_FILTER;
	string = g_key_file_get_string (key_file, group_name, key, NULL);
	if (string != NULL && *string != '\0')
		action = e_shell_window_get_action (shell_window, string);
	else
		action = NULL;
	if (action != NULL)
		gtk_action_activate (action);
	else {
		/* Pick the first combo box item. */
		widget = searchbar->priv->filter_combo_box;
		gtk_combo_box_set_active (GTK_COMBO_BOX (widget), 0);
	}
	g_free (string);

	key = STATE_KEY_SEARCH_SCOPE;
	string = g_key_file_get_string (key_file, group_name, key, NULL);
	if (string != NULL && *string != '\0')
		action = e_shell_window_get_action (shell_window, string);
	else
		action = NULL;
	if (action != NULL)
		gtk_action_activate (action);
	else {
		/* Pick the first combo box item. */
		widget = searchbar->priv->scope_combo_box;
		gtk_combo_box_set_active (GTK_COMBO_BOX (widget), 0);
	}
	g_free (string);

	key = STATE_KEY_SEARCH_TEXT;
	string = g_key_file_get_string (key_file, group_name, key, NULL);
	search_text = e_shell_searchbar_get_search_text (searchbar);
	if (search_text != NULL && *search_text == '\0')
		search_text = NULL;
	if (g_strcmp0 (string, search_text) != 0)
		e_shell_searchbar_set_search_text (searchbar, string);
	g_free (string);

	e_shell_view_unblock_execute_search (shell_view);

	action = E_SHELL_WINDOW_ACTION_SEARCH_QUICK (shell_window);
	gtk_action_unblock_activate (action);

	/* Now execute the search. */
	e_shell_view_execute_search (shell_view);
}
