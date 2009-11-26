/*
 * e-mail-shell-view-private.c
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

#include "anjal-shell-view-private.h"

static void
mail_shell_view_folder_tree_selected_cb (AnjalShellView *mail_shell_view,
                                         const gchar *full_name,
                                         const gchar *uri,
                                         guint32 flags,
                                         EMFolderTree *folder_tree)
{
	EShellView *shell_view;
	gboolean folder_selected;

	shell_view = E_SHELL_VIEW (mail_shell_view);

	folder_selected =
		!(flags & CAMEL_FOLDER_NOSELECT) &&
		full_name != NULL;
	
	anjal_mail_view_set_folder_uri (mail_shell_view->priv->view, uri);
	e_shell_view_update_actions (shell_view);
}

static gboolean
mail_shell_view_folder_tree_key_press_event_cb (AnjalShellView *mail_shell_view,
                                                GdkEventKey *event)
{
	gboolean handled = FALSE;

	if ((event->state & GDK_CONTROL_MASK) != 0)
		goto ctrl;

	/* <keyval> alone */
	switch (event->keyval) {
		case GDK_period:
		case GDK_comma:
		case GDK_bracketleft:
		case GDK_bracketright:
			goto emit;

		default:
			goto exit;
	}

ctrl:
	/* Ctrl + <keyval> */
	switch (event->keyval) {
		case GDK_period:
		case GDK_comma:
			goto emit;

		default:
			goto exit;
	}

	/* All branches jump past this. */
	g_return_val_if_reached (FALSE);

emit:
	/* Forward the key press to the EMailReader interface. */
	;
exit:
	return handled;
}

static void
mail_shell_view_folder_tree_selection_done_cb (AnjalShellView *mail_shell_view,
                                               GtkWidget *menu)
{
	EMailShellSidebar *mail_shell_sidebar;
	EMFolderTree *folder_tree;
	const gchar *list_uri=NULL;
	gchar *tree_uri;

	mail_shell_sidebar = mail_shell_view->priv->mail_shell_sidebar;
	folder_tree = e_mail_shell_sidebar_get_folder_tree (mail_shell_sidebar);

	tree_uri = em_folder_tree_get_selected_uri (folder_tree);

	/* If the folder tree and message list disagree on the current
	 * folder, reset the folder tree to match the message list. */
	if (g_strcmp0 (tree_uri, list_uri) != 0)
		em_folder_tree_set_selected (folder_tree, list_uri, FALSE);

	g_free (tree_uri);

	/* Disconnect from the "selection-done" signal. */
	g_signal_handlers_disconnect_by_func (
		menu, mail_shell_view_folder_tree_selection_done_cb,
		mail_shell_view);
}

static void
mail_shell_view_folder_tree_popup_event_cb (EShellView *shell_view,
                                            GdkEventButton *event)
{
	GtkWidget *menu;
	const gchar *widget_path;

	widget_path = "/mail-folder-popup";
	menu = e_shell_view_show_popup_menu (shell_view, widget_path, event);

	g_signal_connect_swapped (
		menu, "selection-done",
		G_CALLBACK (mail_shell_view_folder_tree_selection_done_cb),
		shell_view);
}

void
anjal_shell_view_set_mail_view (AnjalShellView *mail_shell_view, 
				AnjalMailView *mail_view)
{
	AnjalShellViewPrivate *priv = mail_shell_view->priv;
	EShellView *shell_view;
	EShellSidebar *shell_sidebar;
	EMailShellSidebar *mail_shell_sidebar;
	EMFolderTree *folder_tree;
	
	priv->view = mail_view;

	shell_view = E_SHELL_VIEW (mail_shell_view);
	shell_sidebar = e_shell_view_get_shell_sidebar (shell_view);
	mail_shell_sidebar = E_MAIL_SHELL_SIDEBAR (shell_sidebar);
	folder_tree = e_mail_shell_sidebar_get_folder_tree (mail_shell_sidebar);
	anjal_mail_view_set_folder_tree (mail_shell_view->priv->view, (GtkWidget *)folder_tree);
}

void
anjal_shell_view_private_init (AnjalShellView *mail_shell_view,
                                EShellViewClass *shell_view_class)
{

}

void
anjal_shell_view_private_constructed (AnjalShellView *mail_shell_view)
{
	AnjalShellViewPrivate *priv = mail_shell_view->priv;
	EMailShellSidebar *mail_shell_sidebar;
	EShell *shell;
	EShellView *shell_view;
	EShellBackend *shell_backend;
	EShellContent *shell_content;
	EShellSettings *shell_settings;
	EShellSidebar *shell_sidebar;
	EShellWindow *shell_window;
	EMFolderTree *folder_tree;
	ERuleContext *context;
	EFilterRule *rule = NULL;
	GtkTreeSelection *selection;
	GtkTreeModel *tree_model;
	GtkUIManager *ui_manager;
	const gchar *source;
	guint merge_id;
	gint ii = 0;

	shell_view = E_SHELL_VIEW (mail_shell_view);
	shell_backend = e_shell_view_get_shell_backend (shell_view);
	shell_content = e_shell_view_get_shell_content (shell_view);
	shell_sidebar = e_shell_view_get_shell_sidebar (shell_view);
	shell_window = e_shell_view_get_shell_window (shell_view);
	ui_manager = e_shell_window_get_ui_manager (shell_window);

	shell = e_shell_window_get_shell (shell_window);
	shell_settings = e_shell_get_shell_settings (shell);

	tree_model = e_shell_settings_get_object (
		shell_settings, "mail-label-list-store");

	e_shell_window_add_action_group (shell_window, "mail");
	e_shell_window_add_action_group (shell_window, "mail-filter");
	e_shell_window_add_action_group (shell_window, "mail-label");

	merge_id = gtk_ui_manager_new_merge_id (ui_manager);
	priv->label_merge_id = merge_id;

	/* Cache these to avoid lots of awkward casting. */
	priv->mail_shell_backend = g_object_ref (shell_backend);
	priv->mail_shell_content = g_object_ref (shell_content);
	priv->mail_shell_sidebar = g_object_ref (shell_sidebar);

	mail_shell_sidebar = E_MAIL_SHELL_SIDEBAR (shell_sidebar);
	folder_tree = e_mail_shell_sidebar_get_folder_tree (mail_shell_sidebar);
	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (folder_tree));

	g_signal_connect_swapped (
		folder_tree, "folder-selected",
		G_CALLBACK (mail_shell_view_folder_tree_selected_cb),
		mail_shell_view);

	g_signal_connect_swapped (
		folder_tree, "key-press-event",
		G_CALLBACK (mail_shell_view_folder_tree_key_press_event_cb),
		mail_shell_view);

	g_signal_connect_swapped (
		folder_tree, "popup-event",
		G_CALLBACK (mail_shell_view_folder_tree_popup_event_cb),
		mail_shell_view);

	g_signal_connect_swapped (
		tree_model, "row-changed",
		G_CALLBACK (anjal_shell_view_update_search_filter),
		mail_shell_view);

	g_signal_connect_swapped (
		tree_model, "row-deleted",
		G_CALLBACK (anjal_shell_view_update_search_filter),
		mail_shell_view);

	g_signal_connect_swapped (
		tree_model, "row-inserted",
		G_CALLBACK (anjal_shell_view_update_search_filter),
		mail_shell_view);

	anjal_shell_view_actions_init (mail_shell_view);
	anjal_shell_view_update_search_filter (mail_shell_view);

	/* Populate built-in rules for search entry popup menu.
	 * Keep the assertions, please.  If the conditions aren't
	 * met we're going to crash anyway, just more mysteriously. */
	context = e_shell_content_get_search_context (shell_content);
	source = E_FILTER_SOURCE_DEMAND;
	while ((rule = e_rule_context_next_rule (context, rule, source))) {
		g_assert (ii < MAIL_NUM_SEARCH_RULES);
		priv->search_rules[ii++] = g_object_ref (rule);
	}
	g_assert (ii == MAIL_NUM_SEARCH_RULES);

	/* Now that we're all set up, simulate selecting a folder. */
	g_signal_emit_by_name (selection, "changed");
}

void
anjal_shell_view_private_dispose (AnjalShellView *mail_shell_view)
{
	AnjalShellViewPrivate *priv = mail_shell_view->priv;
	gint ii;

	DISPOSE (priv->mail_shell_backend);
	DISPOSE (priv->mail_shell_content);
	DISPOSE (priv->mail_shell_sidebar);

	for (ii = 0; ii < MAIL_NUM_SEARCH_RULES; ii++)
		DISPOSE (priv->search_rules[ii]);
}

void
anjal_shell_view_private_finalize (AnjalShellView *mail_shell_view)
{
	/* XXX Nothing to do? */
}

void
anjal_shell_view_restore_state (AnjalShellView *mail_shell_view, const char *folder_uri)
{
	EShellView *shell_view;
	EShellContent *shell_content;
	gchar *group_name;

	/* XXX Move this to EMailShellContent. */

	g_return_if_fail (E_IS_SHELL_VIEW (mail_shell_view));

	shell_view = E_SHELL_VIEW (mail_shell_view);
	shell_content = e_shell_view_get_shell_content (shell_view);

	group_name = g_strdup_printf ("Folder %s", folder_uri);
	e_shell_content_restore_state (shell_content, group_name);
	g_free (group_name);
}

