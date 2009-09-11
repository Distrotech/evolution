/*
 * e-mail-shell-view-actions.c
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

#include "e-mail-shell-view-private.h"

static void
action_gal_save_custom_view_cb (GtkAction *action,
                                EMailShellView *mail_shell_view)
{
	EMailShellContent *mail_shell_content;
	EShellView *shell_view;
	GalViewInstance *view_instance;

	/* All shell views repond to the activation of this action,
	 * which is defined by EShellWindow.  But only the currently
	 * active shell view proceeds with saving the custom view. */
	shell_view = E_SHELL_VIEW (mail_shell_view);
	if (!e_shell_view_is_active (shell_view))
		return;

	mail_shell_content = mail_shell_view->priv->mail_shell_content;
	view_instance = e_mail_shell_content_get_view_instance (mail_shell_content);
	gal_view_instance_save_as (view_instance);
}

static void
action_mail_account_disable_cb (GtkAction *action,
                                EMailShellView *mail_shell_view)
{
	EMailShellSidebar *mail_shell_sidebar;
	EMFolderTree *folder_tree;
	EAccountList *account_list;
	EAccount *account;
	gchar *folder_uri;

	mail_shell_sidebar = mail_shell_view->priv->mail_shell_sidebar;

	folder_tree = e_mail_shell_sidebar_get_folder_tree (mail_shell_sidebar);
	folder_uri = em_folder_tree_get_selected_uri (folder_tree);
	g_return_if_fail (folder_uri != NULL);

	account_list = e_get_account_list ();
	account = mail_config_get_account_by_source_url (folder_uri);
	g_return_if_fail (account != NULL);

	if (e_account_list_account_has_proxies (account_list, account))
		e_account_list_remove_account_proxies (account_list, account);

	account->enabled = !account->enabled;
	e_account_list_change (account_list, account);
	e_mail_store_remove_by_uri (folder_uri);

	if (account->parent_uid != NULL)
		e_account_list_remove (account_list, account);

	e_account_list_save (account_list);

	g_free (folder_uri);
}

static void
action_mail_create_search_folder_cb (GtkAction *action,
                                     EMailShellView *mail_shell_view)
{
	EMailReader *reader;
	EShellView *shell_view;
	EShellContent *shell_content;
	MessageList *message_list;
	FilterRule *search_rule;
	EMVFolderRule *vfolder_rule;
	const gchar *folder_uri;
	const gchar *search_text;
	gchar *rule_name;

	vfolder_load_storage ();

	shell_view = E_SHELL_VIEW (mail_shell_view);
	shell_content = e_shell_view_get_shell_content (shell_view);
	search_rule = e_shell_content_get_search_rule (shell_content);
	search_text = e_shell_content_get_search_text (shell_content);

	g_return_if_fail (search_rule != NULL);

	if (search_text == NULL || *search_text == '\0')
		search_text = "''";

	reader = E_MAIL_READER (shell_content);
	message_list = e_mail_reader_get_message_list (reader);
	folder_uri = message_list->folder_uri;

	search_rule = vfolder_clone_rule (search_rule);
	rule_name = g_strdup_printf ("%s %s", search_rule->name, search_text);
	filter_rule_set_source (search_rule, FILTER_SOURCE_INCOMING);
	filter_rule_set_name (search_rule, rule_name);
	g_free (rule_name);

	vfolder_rule = EM_VFOLDER_RULE (search_rule);
	em_vfolder_rule_add_source (vfolder_rule, folder_uri);
	vfolder_gui_add_rule (vfolder_rule);
}

static void
action_mail_download_foreach_cb (CamelService *service)
{
	if (CAMEL_IS_DISCO_STORE (service) || CAMEL_IS_OFFLINE_STORE (service))
		mail_store_prepare_offline (CAMEL_STORE (service));
}

static void
action_mail_download_cb (GtkAction *action,
                         EMailShellView *mail_shell_view)
{
	e_mail_store_foreach ((GHFunc) action_mail_download_foreach_cb, NULL);
}

static void
action_mail_empty_trash_cb (GtkAction *action,
                            EMailShellView *mail_shell_view)
{
	EShellWindow *shell_window;
	EShellView *shell_view;

	shell_view = E_SHELL_VIEW (mail_shell_view);
	shell_window = e_shell_view_get_shell_window (shell_view);

	em_utils_empty_trash (GTK_WIDGET (shell_window));
}

static void
action_mail_flush_outbox_cb (GtkAction *action,
                             EMailShellView *mail_shell_view)
{
	mail_send ();
}

static void
action_mail_folder_copy_cb (GtkAction *action,
                            EMailShellView *mail_shell_view)
{
	EShellView *shell_view;
	EShellWindow *shell_window;
	EMailShellSidebar *mail_shell_sidebar;
	CamelFolderInfo *folder_info;
	EMFolderTree *folder_tree;

	shell_view = E_SHELL_VIEW (mail_shell_view);
	shell_window = e_shell_view_get_shell_window (shell_view);

	mail_shell_sidebar = mail_shell_view->priv->mail_shell_sidebar;
	folder_tree = e_mail_shell_sidebar_get_folder_tree (mail_shell_sidebar);
	folder_info = em_folder_tree_get_selected_folder_info (folder_tree);
	g_return_if_fail (folder_info != NULL);

	/* XXX Leaking folder_info? */
	em_folder_utils_copy_folder (
		GTK_WINDOW (shell_window), folder_info, FALSE);
}

static void
action_mail_folder_delete_cb (GtkAction *action,
                              EMailShellView *mail_shell_view)
{
	EMailShellSidebar *mail_shell_sidebar;
	EMFolderTree *folder_tree;
	CamelFolder *folder;

	mail_shell_sidebar = mail_shell_view->priv->mail_shell_sidebar;
	folder_tree = e_mail_shell_sidebar_get_folder_tree (mail_shell_sidebar);
	folder = em_folder_tree_get_selected_folder (folder_tree);
	g_return_if_fail (folder != NULL);

	em_folder_utils_delete_folder (folder);
}

static void
action_mail_folder_expunge_cb (GtkAction *action,
                               EMailShellView *mail_shell_view)
{
	EMailReader *reader;
	MessageList *message_list;
	EShellWindow *shell_window;
	EShellView *shell_view;

	shell_view = E_SHELL_VIEW (mail_shell_view);
	shell_window = e_shell_view_get_shell_window (shell_view);

	reader = E_MAIL_READER (mail_shell_view->priv->mail_shell_content);
	message_list = e_mail_reader_get_message_list (reader);
	g_return_if_fail (message_list->folder != NULL);

	em_utils_expunge_folder (
		GTK_WIDGET (shell_window), message_list->folder);
}

static void
action_mail_folder_mark_all_as_read_cb (GtkAction *action,
                                        EMailShellView *mail_shell_view)
{
	EMailReader *reader;
	MessageList *message_list;
	EShellWindow *shell_window;
	EShellView *shell_view;
	CamelFolder *folder;
	GtkWindow *parent;
	GPtrArray *uids;
	const gchar *key;
	const gchar *prompt;
	guint ii;

	shell_view = E_SHELL_VIEW (mail_shell_view);
	shell_window = e_shell_view_get_shell_window (shell_view);
	parent = GTK_WINDOW (shell_window);

	reader = E_MAIL_READER (mail_shell_view->priv->mail_shell_content);
	message_list = e_mail_reader_get_message_list (reader);
	folder = message_list->folder;
	g_return_if_fail (folder != NULL);

	key = "/apps/evolution/mail/prompts/mark_all_read";
	prompt = "mail:ask-mark-all-read";

	if (!em_utils_prompt_user (parent, key, prompt, NULL))
		return;

	uids = message_list_get_uids (message_list);

	camel_folder_freeze (folder);
	for (ii = 0; ii < uids->len; ii++)
		camel_folder_set_message_flags (
			folder, uids->pdata[ii],
			CAMEL_MESSAGE_SEEN, CAMEL_MESSAGE_SEEN);
	camel_folder_thaw (folder);

	message_list_free_uids (message_list, uids);
}

static void
action_mail_folder_move_cb (GtkAction *action,
                            EMailShellView *mail_shell_view)
{
	EShellView *shell_view;
	EShellWindow *shell_window;
	EMailShellSidebar *mail_shell_sidebar;
	CamelFolderInfo *folder_info;
	EMFolderTree *folder_tree;

	shell_view = E_SHELL_VIEW (mail_shell_view);
	shell_window = e_shell_view_get_shell_window (shell_view);

	mail_shell_sidebar = mail_shell_view->priv->mail_shell_sidebar;
	folder_tree = e_mail_shell_sidebar_get_folder_tree (mail_shell_sidebar);
	folder_info = em_folder_tree_get_selected_folder_info (folder_tree);
	g_return_if_fail (folder_info != NULL);

	/* XXX Leaking folder_info? */
	em_folder_utils_copy_folder (
		GTK_WINDOW (shell_window), folder_info, TRUE);
}

static void
action_mail_folder_new_cb (GtkAction *action,
                           EMailShellView *mail_shell_view)
{
	EShellView *shell_view;
	EShellWindow *shell_window;
	EMailShellSidebar *mail_shell_sidebar;
	CamelFolderInfo *folder_info;
	EMFolderTree *folder_tree;

	shell_view = E_SHELL_VIEW (mail_shell_view);
	shell_window = e_shell_view_get_shell_window (shell_view);

	mail_shell_sidebar = mail_shell_view->priv->mail_shell_sidebar;
	folder_tree = e_mail_shell_sidebar_get_folder_tree (mail_shell_sidebar);
	folder_info = em_folder_tree_get_selected_folder_info (folder_tree);
	g_return_if_fail (folder_info != NULL);

	em_folder_utils_create_folder (
		folder_info, folder_tree, GTK_WINDOW (shell_window));
	camel_folder_info_free (folder_info);
}

static void
action_mail_folder_properties_cb (GtkAction *action,
                                  EMailShellView *mail_shell_view)
{
	EMailShellSidebar *mail_shell_sidebar;
	EMFolderTree *folder_tree;
	EShellView *shell_view;
	GtkTreeSelection *selection;
	GtkTreeView *tree_view;
	GtkTreeModel *model;
	GtkTreeIter iter;
	gchar *uri;

	shell_view = E_SHELL_VIEW (mail_shell_view);
	mail_shell_sidebar = mail_shell_view->priv->mail_shell_sidebar;
	folder_tree = e_mail_shell_sidebar_get_folder_tree (mail_shell_sidebar);

	tree_view = GTK_TREE_VIEW (folder_tree);
	selection = gtk_tree_view_get_selection (tree_view);
	if (!gtk_tree_selection_get_selected (selection, &model, &iter))
		return;

	gtk_tree_model_get (model, &iter, COL_STRING_URI, &uri, -1);
	em_folder_properties_show (shell_view, NULL, uri);
	g_free (uri);
}

static void
action_mail_folder_refresh_cb (GtkAction *action,
                               EMailShellView *mail_shell_view)
{
	EMailShellSidebar *mail_shell_sidebar;
	EMFolderTree *folder_tree;
	CamelFolder *folder;

	mail_shell_sidebar = mail_shell_view->priv->mail_shell_sidebar;
	folder_tree = e_mail_shell_sidebar_get_folder_tree (mail_shell_sidebar);
	folder = em_folder_tree_get_selected_folder (folder_tree);
	g_return_if_fail (folder != NULL);

	mail_refresh_folder (folder, NULL, NULL);
}

static void
action_mail_folder_rename_cb (GtkAction *action,
                              EMailShellView *mail_shell_view)
{
	EMailShellSidebar *mail_shell_sidebar;
	EMFolderTree *folder_tree;
	CamelFolder *folder;

	mail_shell_sidebar = mail_shell_view->priv->mail_shell_sidebar;
	folder_tree = e_mail_shell_sidebar_get_folder_tree (mail_shell_sidebar);
	folder = em_folder_tree_get_selected_folder (folder_tree);
	g_return_if_fail (folder != NULL);

	em_folder_utils_rename_folder (folder);
}

/* Helper for action_mail_folder_select_all_cb() */
static gboolean
action_mail_folder_select_all_timeout_cb (MessageList *message_list)
{
	message_list_select_all (message_list);
	gtk_widget_grab_focus (GTK_WIDGET (message_list));

	return FALSE;
}

static void
action_mail_folder_select_all_cb (GtkAction *action,
                                  EMailShellView *mail_shell_view)
{
	EMailReader *reader;
	MessageList *message_list;
	EShellWindow *shell_window;
	EShellView *shell_view;

	shell_view = E_SHELL_VIEW (mail_shell_view);
	shell_window = e_shell_view_get_shell_window (shell_view);

	reader = E_MAIL_READER (mail_shell_view->priv->mail_shell_content);
	message_list = e_mail_reader_get_message_list (reader);

	if (message_list->threaded) {
		gtk_action_activate (ACTION (MAIL_THREADS_EXPAND_ALL));

		/* XXX The timeout below is added so that the execution
		 *     thread to expand all conversation threads would
		 *     have completed.  The timeout 505 is just to ensure
		 *     that the value is a small delta more than the
		 *     timeout value in mail_regen_list(). */
		g_timeout_add (
			505, (GSourceFunc)
			action_mail_folder_select_all_timeout_cb,
			message_list);
	} else
		/* If there is no threading, just select all immediately. */
		action_mail_folder_select_all_timeout_cb (message_list);
}

static void
action_mail_folder_select_thread_cb (GtkAction *action,
                                     EMailShellView *mail_shell_view)
{
	MessageList *message_list;
	EMailReader *reader;

	reader = E_MAIL_READER (mail_shell_view->priv->mail_shell_content);
	message_list = e_mail_reader_get_message_list (reader);

	message_list_select_thread (message_list);
}

static void
action_mail_folder_select_subthread_cb (GtkAction *action,
                                        EMailShellView *mail_shell_view)
{
	MessageList *message_list;
	EMailReader *reader;

	reader = E_MAIL_READER (mail_shell_view->priv->mail_shell_content);
	message_list = e_mail_reader_get_message_list (reader);

	message_list_select_subthread (message_list);
}

static void
action_mail_folder_unsubscribe_cb (GtkAction *action,
                                   EMailShellView *mail_shell_view)
{
	EMailShellSidebar *mail_shell_sidebar;
	EMFolderTree *folder_tree;
	gchar *folder_uri;

	mail_shell_sidebar = mail_shell_view->priv->mail_shell_sidebar;
	folder_tree = e_mail_shell_sidebar_get_folder_tree (mail_shell_sidebar);

	folder_uri = em_folder_tree_get_selected_uri (folder_tree);
	em_folder_utils_unsubscribe_folder (folder_uri);
	g_free (folder_uri);
}

static void
action_mail_hide_deleted_cb (GtkToggleAction *action,
                             EMailShellView *mail_shell_view)
{
	MessageList *message_list;
	EMailReader *reader;
	gboolean active;

	reader = E_MAIL_READER (mail_shell_view->priv->mail_shell_content);
	message_list = e_mail_reader_get_message_list (reader);

	active = gtk_toggle_action_get_active (action);
	message_list_set_hidedeleted (message_list, active);
}

static void
action_mail_hide_read_cb (GtkAction *action,
                          EMailShellView *mail_shell_view)
{
	MessageList *message_list;
	EMailReader *reader;

	reader = E_MAIL_READER (mail_shell_view->priv->mail_shell_content);
	message_list = e_mail_reader_get_message_list (reader);

	message_list_hide_add (
		message_list,
		"(match-all (system-flag \"seen\"))",
		ML_HIDE_SAME, ML_HIDE_SAME);
}

static void
action_mail_hide_selected_cb (GtkAction *action,
                              EMailShellView *mail_shell_view)
{
	MessageList *message_list;
	EMailReader *reader;
	GPtrArray *uids;

	reader = E_MAIL_READER (mail_shell_view->priv->mail_shell_content);
	message_list = e_mail_reader_get_message_list (reader);

	uids = message_list_get_selected (message_list);
	message_list_hide_uids (message_list, uids);
	message_list_free_uids (message_list, uids);
}

static void
action_mail_label_cb (GtkToggleAction *action,
                      EMailShellView *mail_shell_view)
{
	EMailReader *reader;
	MessageList *message_list;
	CamelFolder *folder;
	GPtrArray *uids;
	const gchar *tag;
	gint ii;

	tag = g_object_get_data (G_OBJECT (action), "tag");
	g_return_if_fail (tag != NULL);

	reader = E_MAIL_READER (mail_shell_view->priv->mail_shell_content);
	message_list = e_mail_reader_get_message_list (reader);
	folder = message_list->folder;

	uids = message_list_get_selected (message_list);

	for (ii = 0; ii < uids->len; ii++) {
		if (gtk_toggle_action_get_active (action))
			camel_folder_set_message_user_flag (
				folder, uids->pdata[ii], tag, TRUE);
		else {
			camel_folder_set_message_user_flag (
				folder, uids->pdata[ii], tag, FALSE);
			camel_folder_set_message_user_tag (
				folder, uids->pdata[ii], "label", NULL);
		}
	}

	message_list_free_uids (message_list, uids);
}

static void
action_mail_label_new_cb (GtkAction *action,
                          EMailShellView *mail_shell_view)
{
	EShell *shell;
	EShellSettings *shell_settings;
	EShellWindow *shell_window;
	EShellView *shell_view;
	EMailLabelDialog *label_dialog;
	EMailLabelListStore *store;
	EMailReader *reader;
	MessageList *message_list;
	CamelFolder *folder;
	GtkTreeModel *model;
	GtkTreeIter iter;
	GtkWidget *dialog;
	GPtrArray *uids;
	GdkColor label_color;
	const gchar *property_name;
	const gchar *label_name;
	gchar *label_tag;
	gint n_children;
	guint ii;

	shell_view = E_SHELL_VIEW (mail_shell_view);
	shell_window = e_shell_view_get_shell_window (shell_view);

	dialog = e_mail_label_dialog_new (GTK_WINDOW (shell_window));

	gtk_window_set_title (GTK_WINDOW (dialog), _("Add Label"));

	if (gtk_dialog_run (GTK_DIALOG (dialog)) != GTK_RESPONSE_OK)
		goto exit;

	shell = e_shell_window_get_shell (shell_window);
	shell_settings = e_shell_get_shell_settings (shell);

	label_dialog = E_MAIL_LABEL_DIALOG (dialog);
	label_name = e_mail_label_dialog_get_label_name (label_dialog);
	e_mail_label_dialog_get_label_color (label_dialog, &label_color);

	property_name = "mail-label-list-store";
	store = e_shell_settings_get_object (shell_settings, property_name);
	e_mail_label_list_store_set (store, NULL, label_name, &label_color);
	g_object_unref (store);

	/* XXX This is awkward.  We've added a new label to the list store
	 *     but we don't have the new label's tag nor an iterator to use
	 *     to fetch it.  We know the label was appended to the store,
	 *     so we have to dig it out manually.  EMailLabelListStore API
	 *     probably needs some rethinking. */
	model = GTK_TREE_MODEL (store);
	n_children = gtk_tree_model_iter_n_children (model, NULL);
	gtk_tree_model_iter_nth_child (model, &iter, NULL, n_children - 1);
	label_tag = e_mail_label_list_store_get_tag (store, &iter);

	reader = E_MAIL_READER (mail_shell_view->priv->mail_shell_content);
	message_list = e_mail_reader_get_message_list (reader);
	folder = message_list->folder;

	uids = message_list_get_selected (message_list);

	for (ii = 0; ii < uids->len; ii++)
		camel_folder_set_message_user_flag (
			folder, uids->pdata[ii], label_tag, TRUE);

	message_list_free_uids (message_list, uids);

	g_free (label_tag);

exit:
	gtk_widget_destroy (dialog);
}

static void
action_mail_label_none_cb (GtkAction *action,
                           EMailShellView *mail_shell_view)
{
	EShell *shell;
	EShellView *shell_view;
	EShellSettings *shell_settings;
	EShellWindow *shell_window;
	EMailReader *reader;
	MessageList *message_list;
	GtkTreeModel *tree_model;
	CamelFolder *folder;
	GtkTreeIter iter;
	GPtrArray *uids;
	gboolean valid;
	guint ii;

	shell_view = E_SHELL_VIEW (mail_shell_view);
	shell_window = e_shell_view_get_shell_window (shell_view);
	shell = e_shell_window_get_shell (shell_window);
	shell_settings = e_shell_get_shell_settings (shell);

	tree_model = e_shell_settings_get_object (
		shell_settings, "mail-label-list-store");

	reader = E_MAIL_READER (mail_shell_view->priv->mail_shell_content);
	message_list = e_mail_reader_get_message_list (reader);
	uids = message_list_get_selected (message_list);
	folder = message_list->folder;

	valid = gtk_tree_model_get_iter_first (tree_model, &iter);

	while (valid) {
		gchar *tag;

		tag = e_mail_label_list_store_get_tag (
			E_MAIL_LABEL_LIST_STORE (tree_model), &iter);

		for (ii = 0; ii < uids->len; ii++) {
			camel_folder_set_message_user_flag (
				folder, uids->pdata[ii], tag, FALSE);
			camel_folder_set_message_user_tag (
				folder, uids->pdata[ii], "label", NULL);
		}

		g_free (tag);

		valid = gtk_tree_model_iter_next (tree_model, &iter);
	}

	message_list_free_uids (message_list, uids);
}

static void
action_mail_search_cb (GtkRadioAction *action,
                       GtkRadioAction *current,
                       EMailShellView *mail_shell_view)
{
	EShellView *shell_view;
	EShellContent *shell_content;
	const gchar *search_hint;

	/* XXX Figure out a way to handle this in EShellContent
	 *     instead of every shell view having to handle it.
	 *     The problem is EShellContent does not know what
	 *     the search option actions are for this view.  It
	 *     would have to dig up the popup menu and retrieve
	 *     the action for each menu item.  Seems messy. */

	shell_view = E_SHELL_VIEW (mail_shell_view);
	shell_content = e_shell_view_get_shell_content (shell_view);

	search_hint = gtk_action_get_label (GTK_ACTION (current));
	e_shell_content_set_search_hint (shell_content, search_hint);
}

static void
action_mail_show_hidden_cb (GtkAction *action,
                            EMailShellView *mail_shell_view)
{
	MessageList *message_list;
	EMailReader *reader;

	reader = E_MAIL_READER (mail_shell_view->priv->mail_shell_content);
	message_list = e_mail_reader_get_message_list (reader);

	message_list_hide_clear (message_list);
}

static void
action_mail_smart_backward_cb (GtkAction *action,
                               EMailShellView *mail_shell_view)
{
	EShell *shell;
	EShellView *shell_view;
	EShellWindow *shell_window;
	EShellSettings *shell_settings;
	EMailShellSidebar *mail_shell_sidebar;
	EMFolderTree *folder_tree;
	EMFormatHTMLDisplay *html_display;
	EMailReader *reader;
	MessageList *message_list;
	GtkToggleAction *toggle_action;
	GtkHTML *html;
	gboolean caret_mode;
	gboolean magic_spacebar;

	/* This implements the so-called "Magic Backspace". */

	shell_view = E_SHELL_VIEW (mail_shell_view);
	shell_window = e_shell_view_get_shell_window (shell_view);
	shell = e_shell_window_get_shell (shell_window);
	shell_settings = e_shell_get_shell_settings (shell);

	mail_shell_sidebar = mail_shell_view->priv->mail_shell_sidebar;
	folder_tree = e_mail_shell_sidebar_get_folder_tree (mail_shell_sidebar);

	reader = E_MAIL_READER (mail_shell_view->priv->mail_shell_content);
	html_display = e_mail_reader_get_html_display (reader);
	message_list = e_mail_reader_get_message_list (reader);

	magic_spacebar = e_shell_settings_get_boolean (
		shell_settings, "mail-magic-spacebar");

	toggle_action = GTK_TOGGLE_ACTION (ACTION (MAIL_CARET_MODE));
	caret_mode = gtk_toggle_action_get_active (toggle_action);

	html = EM_FORMAT_HTML (html_display)->html;

	if (e_web_view_scroll_backward (E_WEB_VIEW (html)))
		return;

	if (caret_mode || !magic_spacebar)
		return;

	/* XXX Are two separate calls really necessary? */

	if (message_list_select (
		message_list, MESSAGE_LIST_SELECT_PREVIOUS,
		0, CAMEL_MESSAGE_SEEN))
		return;

	if (message_list_select (
		message_list, MESSAGE_LIST_SELECT_PREVIOUS |
		MESSAGE_LIST_SELECT_WRAP, 0, CAMEL_MESSAGE_SEEN))
		return;

	em_folder_tree_select_prev_path (folder_tree, TRUE);

	gtk_widget_grab_focus (GTK_WIDGET (message_list));
}

static void
action_mail_smart_forward_cb (GtkAction *action,
                              EMailShellView *mail_shell_view)
{
	EShell *shell;
	EShellView *shell_view;
	EShellWindow *shell_window;
	EShellSettings *shell_settings;
	EMailShellSidebar *mail_shell_sidebar;
	EMFolderTree *folder_tree;
	EMFormatHTMLDisplay *html_display;
	EMailReader *reader;
	MessageList *message_list;
	GtkToggleAction *toggle_action;
	GtkHTML *html;
	gboolean caret_mode;
	gboolean magic_spacebar;

	/* This implements the so-called "Magic Spacebar". */

	shell_view = E_SHELL_VIEW (mail_shell_view);
	shell_window = e_shell_view_get_shell_window (shell_view);
	shell = e_shell_window_get_shell (shell_window);
	shell_settings = e_shell_get_shell_settings (shell);

	mail_shell_sidebar = mail_shell_view->priv->mail_shell_sidebar;
	folder_tree = e_mail_shell_sidebar_get_folder_tree (mail_shell_sidebar);

	reader = E_MAIL_READER (mail_shell_view->priv->mail_shell_content);
	html_display = e_mail_reader_get_html_display (reader);
	message_list = e_mail_reader_get_message_list (reader);

	magic_spacebar = e_shell_settings_get_boolean (
		shell_settings, "mail-magic-spacebar");

	toggle_action = GTK_TOGGLE_ACTION (ACTION (MAIL_CARET_MODE));
	caret_mode = gtk_toggle_action_get_active (toggle_action);

	html = EM_FORMAT_HTML (html_display)->html;

	if (e_web_view_scroll_forward (E_WEB_VIEW (html)))
		return;

	if (caret_mode || !magic_spacebar)
		return;

	/* XXX Are two separate calls really necessary? */

	if (message_list_select (
		message_list, MESSAGE_LIST_SELECT_NEXT,
		0, CAMEL_MESSAGE_SEEN))
		return;

	if (message_list_select (
		message_list, MESSAGE_LIST_SELECT_NEXT |
		MESSAGE_LIST_SELECT_WRAP, 0, CAMEL_MESSAGE_SEEN))
		return;

	em_folder_tree_select_next_path (folder_tree, TRUE);

	gtk_widget_grab_focus (GTK_WIDGET (message_list));
}

static void
action_mail_stop_cb (GtkAction *action,
                     EMailShellView *mail_shell_view)
{
	mail_cancel_all ();
}

static void
action_mail_threads_collapse_all_cb (GtkAction *action,
                                     EMailShellView *mail_shell_view)
{
	MessageList *message_list;
	EMailReader *reader;

	reader = E_MAIL_READER (mail_shell_view->priv->mail_shell_content);
	message_list = e_mail_reader_get_message_list (reader);

	message_list_set_threaded_collapse_all (message_list);
}

static void
action_mail_threads_expand_all_cb (GtkAction *action,
                                   EMailShellView *mail_shell_view)
{
	MessageList *message_list;
	EMailReader *reader;

	reader = E_MAIL_READER (mail_shell_view->priv->mail_shell_content);
	message_list = e_mail_reader_get_message_list (reader);

	message_list_set_threaded_expand_all (message_list);
}

static void
action_mail_threads_group_by_cb (GtkToggleAction *action,
                                 EMailShellView *mail_shell_view)
{
	EMailShellContent *mail_shell_content;
	MessageList *message_list;
	EMailReader *reader;
	gboolean active;

	mail_shell_content = mail_shell_view->priv->mail_shell_content;
	active = gtk_toggle_action_get_active (action);

	reader = E_MAIL_READER (mail_shell_content);
	message_list = e_mail_reader_get_message_list (reader);

	message_list_set_threaded (message_list, active);
}

static void
action_mail_tools_filters_cb (GtkAction *action,
                              EMailShellView *mail_shell_view)
{
	EShellWindow *shell_window;
	EShellView *shell_view;

	shell_view = E_SHELL_VIEW (mail_shell_view);
	shell_window = e_shell_view_get_shell_window (shell_view);

	em_utils_edit_filters (GTK_WIDGET (shell_window));
}

static void
action_mail_tools_search_folders_cb (GtkAction *action,
                                     EMailShellView *mail_shell_view)
{
	vfolder_edit (E_SHELL_VIEW (mail_shell_view));
}

static void
action_mail_tools_subscriptions_cb (GtkAction *action,
                                    EMailShellView *mail_shell_view)
{
	EShellWindow *shell_window;
	EShellView *shell_view;
	GtkWidget *dialog;

	shell_view = E_SHELL_VIEW (mail_shell_view);
	shell_window = e_shell_view_get_shell_window (shell_view);

	dialog = em_subscribe_editor_new ();
	gtk_window_set_transient_for (
		GTK_WINDOW (dialog), GTK_WINDOW (shell_window));
	gtk_dialog_run (GTK_DIALOG (dialog));
	/* XXX Dialog destroys itself. */
}

static void
action_mail_view_cb (GtkRadioAction *action,
                     GtkRadioAction *current,
                     EMailShellView *mail_shell_view)
{
	EMailShellContent *mail_shell_content;
	GtkOrientable *orientable;
	GtkOrientation orientation;

	mail_shell_content = mail_shell_view->priv->mail_shell_content;
	orientable = GTK_ORIENTABLE (mail_shell_content);

	switch (gtk_radio_action_get_current_value (action)) {
		case 0:
			orientation = GTK_ORIENTATION_VERTICAL;
			break;
		case 1:
			orientation = GTK_ORIENTATION_HORIZONTAL;
			break;
		default:
			g_return_if_reached ();
	}

	gtk_orientable_set_orientation (orientable, orientation);
}

static void
action_search_filter_cb (GtkRadioAction *action,
                         GtkRadioAction *current,
                         EShellView *shell_view)
{
	EShellContent *shell_content;
	EShellWindow *shell_window;
	EMailReader *reader;
	MessageList *message_list;
	GKeyFile *key_file;
	const gchar *folder_uri;

	shell_content = e_shell_view_get_shell_content (shell_view);
	shell_window = e_shell_view_get_shell_window (shell_view);
	key_file = e_shell_view_get_state_key_file (shell_view);

	reader = E_MAIL_READER (shell_content);
	message_list = e_mail_reader_get_message_list (reader);
	folder_uri = message_list->folder_uri;

	if (folder_uri != NULL) {
		const gchar *key;
		const gchar *string;
		gchar *group_name;

		key = STATE_KEY_SEARCH_FILTER;
		string = gtk_action_get_name (GTK_ACTION (current));
		group_name = g_strdup_printf ("Folder %s", folder_uri);

		g_key_file_set_string (key_file, group_name, key, string);
		e_shell_view_set_state_dirty (shell_view);

		g_free (group_name);
	}

	e_shell_view_execute_search (shell_view);
}

static void
action_search_quick_cb (GtkAction *action,
                        EMailShellView *mail_shell_view)
{
	EShellView *shell_view;
	EShellWindow *shell_window;
	EShellContent *shell_content;
	FilterRule *search_rule;
	gint value;

	/* Set the search rule in EShellContent so that "Create
	 * Search Folder from Search" works for quick searches. */

	shell_view = E_SHELL_VIEW (mail_shell_view);
	shell_window = e_shell_view_get_shell_window (shell_view);
	shell_content = e_shell_view_get_shell_content (shell_view);

	action = ACTION (MAIL_SEARCH_SUBJECT_OR_ADDRESSES_CONTAIN);
	value = gtk_radio_action_get_current_value (GTK_RADIO_ACTION (action));
	g_return_if_fail (value >= 0 && value < MAIL_NUM_SEARCH_RULES);
	search_rule = mail_shell_view->priv->search_rules[value];

	e_shell_content_set_search_rule (shell_content, search_rule);
}

static void
action_search_scope_cb (GtkRadioAction *action,
                        GtkRadioAction *current,
                        EShellView *shell_view)
{
	EShellContent *shell_content;
	EShellWindow *shell_window;
	EMailReader *reader;
	MessageList *message_list;
	GKeyFile *key_file;
	const gchar *folder_uri;

	shell_content = e_shell_view_get_shell_content (shell_view);
	shell_window = e_shell_view_get_shell_window (shell_view);
	key_file = e_shell_view_get_state_key_file (shell_view);

	reader = E_MAIL_READER (shell_content);
	message_list = e_mail_reader_get_message_list (reader);
	folder_uri = message_list->folder_uri;

	if (folder_uri != NULL) {
		const gchar *key;
		const gchar *string;
		gchar *group_name;

		key = STATE_KEY_SEARCH_SCOPE;
		string = gtk_action_get_name (GTK_ACTION (current));
		group_name = g_strdup_printf ("Folder %s", folder_uri);

		g_key_file_set_string (key_file, group_name, key, string);
		e_shell_view_set_state_dirty (shell_view);

		g_free (group_name);
	}

	e_shell_view_execute_search (shell_view);
}

static GtkActionEntry mail_entries[] = {

	{ "mail-account-disable",
	  NULL,
	  N_("_Disable Account"),
	  NULL,
	  N_("Disable this account"),
	  G_CALLBACK (action_mail_account_disable_cb) },

	{ "mail-create-search-folder",
	  NULL,
	  N_("C_reate Search Folder From Search..."),
	  NULL,
	  NULL,  /* XXX Add a tooltip! */
	  G_CALLBACK (action_mail_create_search_folder_cb) },

	{ "mail-download",
	  NULL,
	  N_("_Download Messages for Offline Usage"),
	  NULL,
	  N_("Download messages of accounts and folders marked for offline"),
	  G_CALLBACK (action_mail_download_cb) },

	{ "mail-empty-trash",
	  NULL,
	  N_("Empty _Trash"),
	  NULL,
	  N_("Permanently remove all the deleted messages from all folders"),
	  G_CALLBACK (action_mail_empty_trash_cb) },

	{ "mail-flush-outbox",
	  "mail-send",
	  N_("Fl_ush Outbox"),
	  NULL,
	  NULL,  /* XXX Add a tooltip! */
	  G_CALLBACK (action_mail_flush_outbox_cb) },

	{ "mail-folder-copy",
	  "folder-copy",
	  N_("_Copy Folder To..."),
	  NULL,
	  N_("Copy the selected folder into another folder"),
	  G_CALLBACK (action_mail_folder_copy_cb) },

	{ "mail-folder-delete",
	  GTK_STOCK_DELETE,
	  NULL,
	  NULL,
	  N_("Permanently remove this folder"),
	  G_CALLBACK (action_mail_folder_delete_cb) },

	{ "mail-folder-expunge",
	  NULL,
	  N_("E_xpunge"),
	  "<Control>e",
	  N_("Permanently remove all deleted messages from this folder"),
	  G_CALLBACK (action_mail_folder_expunge_cb) },

	{ "mail-folder-mark-all-as-read",
	  "mail-read",
	  N_("Mar_k All Messages as Read"),
	  NULL,
	  N_("Mark all messages in the folder as read"),
	  G_CALLBACK (action_mail_folder_mark_all_as_read_cb) },

	{ "mail-folder-move",
	  "folder-move",
	  N_("_Move Folder To..."),
	  NULL,
	  N_("Move the selected folder into another folder"),
	  G_CALLBACK (action_mail_folder_move_cb) },

	{ "mail-folder-new",
	  "folder-new",
	  N_("_New..."),
	  NULL,
	  N_("Create a new folder for storing mail"),
	  G_CALLBACK (action_mail_folder_new_cb) },

	{ "mail-folder-properties",
	  GTK_STOCK_PROPERTIES,
	  NULL,
	  NULL,
	  N_("Change the properties of this folder"),
	  G_CALLBACK (action_mail_folder_properties_cb) },

	{ "mail-folder-refresh",
	  GTK_STOCK_REFRESH,
	  NULL,
	  "F5",
	  N_("Refresh the folder"),
	  G_CALLBACK (action_mail_folder_refresh_cb) },

	{ "mail-folder-rename",
	  NULL,
	  N_("_Rename..."),
	  "F2",
	  N_("Change the name of this folder"),
	  G_CALLBACK (action_mail_folder_rename_cb) },

	{ "mail-folder-select-all",
	  NULL,
	  N_("Select _All Messages"),
	  "<Control>a",
	  N_("Select all visible messages"),
	  G_CALLBACK (action_mail_folder_select_all_cb) },

	{ "mail-folder-select-thread",
	  NULL,
	  N_("Select Message _Thread"),
	  "<Control>h",
	  N_("Select all messages in the same thread as the selected message"),
	  G_CALLBACK (action_mail_folder_select_thread_cb) },

	{ "mail-folder-select-subthread",
	  NULL,
	  N_("Select Message S_ubthread"),
	  "<Shift><Control>h",
	  N_("Select all replies to the currently selected message"),
	  G_CALLBACK (action_mail_folder_select_subthread_cb) },

	{ "mail-folder-unsubscribe",
	  NULL,
	  N_("_Unsubscribe"),
	  NULL,
	  N_("Unsubscribe from the selected folder"),
	  G_CALLBACK (action_mail_folder_unsubscribe_cb) },

	{ "mail-label-new",
	  NULL,
	  N_("_New Label"),
	  NULL,
	  NULL,  /* XXX Add a tooltip! */
	  G_CALLBACK (action_mail_label_new_cb) },

	{ "mail-label-none",
	  NULL,
	  N_("N_one"),
	  NULL,
	  NULL,  /* XXX Add a tooltip! */
	  G_CALLBACK (action_mail_label_none_cb) },

	{ "mail-hide-read",
	  NULL,
	  N_("Hide _Read Messages"),
	  NULL,
	  N_("Temporarily hide all messages that have already been read"),
	  G_CALLBACK (action_mail_hide_read_cb) },

	{ "mail-hide-selected",
	  NULL,
	  N_("Hide S_elected Messages"),
	  NULL,
	  N_("Temporarily hide the selected messages"),
	  G_CALLBACK (action_mail_hide_selected_cb) },

	{ "mail-show-hidden",
	  NULL,
	  N_("Show Hidde_n Messages"),
	  NULL,
	  N_("Show messages that have been temporarily hidden"),
	  G_CALLBACK (action_mail_show_hidden_cb) },

	{ "mail-smart-backward",
	  NULL,
	  NULL,  /* No menu item; key press only */
	  NULL,
	  NULL,
	  G_CALLBACK (action_mail_smart_backward_cb) },

	{ "mail-smart-forward",
	  NULL,
	  NULL,  /* No menu item; key press only */
	  NULL,
	  NULL,
	  G_CALLBACK (action_mail_smart_forward_cb) },

	{ "mail-stop",
	  GTK_STOCK_STOP,
	  N_("Cancel"),
	  NULL,
	  N_("Cancel the current mail operation"),
	  G_CALLBACK (action_mail_stop_cb) },

	{ "mail-threads-collapse-all",
	  NULL,
	  N_("Collapse All _Threads"),
	  "<Shift><Control>b",
	  N_("Collapse all message threads"),
	  G_CALLBACK (action_mail_threads_collapse_all_cb) },

	{ "mail-threads-expand-all",
	  NULL,
	  N_("E_xpand All Threads"),
	  NULL,
	  N_("Expand all message threads"),
	  G_CALLBACK (action_mail_threads_expand_all_cb) },

	{ "mail-tools-filters",
	  NULL,
	  N_("_Message Filters"),
	  NULL,
	  N_("Create or edit rules for filtering new mail"),
	  G_CALLBACK (action_mail_tools_filters_cb) },

	{ "mail-tools-search-folders",
	  NULL,
	  N_("Search F_olders"),
	  NULL,
	  N_("Create or edit search folder definitions"),
	  G_CALLBACK (action_mail_tools_search_folders_cb) },

	{ "mail-tools-subscriptions",
	  NULL,
	  N_("_Subscriptions..."),
	  NULL,
	  N_("Subscribe or unsubscribe to folders on remote servers"),
	  G_CALLBACK (action_mail_tools_subscriptions_cb) },

	/*** Menus ***/

	{ "mail-folder-menu",
	  NULL,
	  N_("F_older"),
	  NULL,
	  NULL,
	  NULL },

	{ "mail-label-menu",
	  NULL,
	  N_("_Label"),
	  NULL,
	  NULL,
	  NULL },

	{ "mail-preview-menu",
	  NULL,
	  N_("_Preview"),
	  NULL,
	  NULL,
	  NULL }
};

static EPopupActionEntry mail_popup_entries[] = {

	{ "mail-popup-account-disable",
	  NULL,
	  "mail-account-disable" },

	{ "mail-popup-empty-trash",
	  NULL,
	  "mail-empty-trash" },

	{ "mail-popup-flush-outbox",
	  NULL,
	  "mail-flush-outbox" },

	{ "mail-popup-folder-copy",
	  NULL,
	  "mail-folder-copy" },

	{ "mail-popup-folder-delete",
	  NULL,
	  "mail-folder-delete" },

	{ "mail-popup-folder-move",
	  NULL,
	  "mail-folder-move" },

	{ "mail-popup-folder-new",
	  N_("_New Folder..."),
	  "mail-folder-new" },

	{ "mail-popup-folder-properties",
	  NULL,
	  "mail-folder-properties" },

	{ "mail-popup-folder-refresh",
	  NULL,
	  "mail-folder-refresh" },

	{ "mail-popup-folder-rename",
	  NULL,
	  "mail-folder-rename" },

	{ "mail-popup-folder-unsubscribe",
	  NULL,
	  "mail-folder-unsubscribe" }
};

static GtkToggleActionEntry mail_toggle_entries[] = {

	{ "mail-hide-deleted",
	  NULL,
	  N_("Hide _Deleted Messages"),
	  NULL,
	  N_("Hide deleted messages rather than displaying "
	     "them with a line through them"),
	  G_CALLBACK (action_mail_hide_deleted_cb),
	  TRUE },

	{ "mail-preview",
	  NULL,
	  N_("Show Message _Preview"),
	  "<Control>m",
	  N_("Show message preview pane"),
	  NULL,  /* Handled by property bindings */
	  TRUE },

	{ "mail-threads-group-by",
	  NULL,
	  N_("_Group By Threads"),
	  "<Control>t",
	  N_("Threaded message list"),
	  G_CALLBACK (action_mail_threads_group_by_cb),
	  FALSE }
};

static GtkRadioActionEntry mail_view_entries[] = {

	/* This action represents the initial active mail view.
	 * It should not be visible in the UI, nor should it be
	 * possible to switch to it from another shell view. */
	{ "mail-view-initial",
	  NULL,
	  NULL,
	  NULL,
	  NULL,
	  -1 },

	{ "mail-view-classic",
	  NULL,
	  N_("_Classic View"),
	  NULL,
	  N_("Show message preview below the message list"),
	  0 },

	{ "mail-view-vertical",
	  NULL,
	  N_("_Vertical View"),
	  NULL,
	  N_("Show message preview alongside the message list"),
	  1 }
};

static GtkRadioActionEntry mail_filter_entries[] = {

	{ "mail-filter-all-messages",
	  NULL,
	  N_("All Messages"),
	  NULL,
	  NULL,  /* XXX Add a tooltip! */
	  MAIL_FILTER_ALL_MESSAGES },

	{ "mail-filter-important-messages",
	  "emblem-important",
	  N_("Important Messages"),
	  NULL,
	  NULL,  /* XXX Add a tooltip! */
	  MAIL_FILTER_IMPORTANT_MESSAGES },

	{ "mail-filter-last-5-days-messages",
	  NULL,
	  N_("Last 5 Days' Messages"),
	  NULL,
	  NULL,  /* XXX Add a tooltip! */
	  MAIL_FILTER_LAST_5_DAYS_MESSAGES },

	{ "mail-filter-messages-not-junk",
	  "mail-mark-notjunk",
	  N_("Messages Not Junk"),
	  NULL,
	  NULL,  /* XXX Add a tooltip! */
	  MAIL_FILTER_MESSAGES_NOT_JUNK },

	{ "mail-filter-messages-with-attachments",
	  "mail-attachment",
	  N_("Messages with Attachments"),
	  NULL,
	  NULL,  /* XXX Add a tooltip! */
	  MAIL_FILTER_MESSAGES_WITH_ATTACHMENTS },

	{ "mail-filter-no-label",
	  NULL,
	  N_("No Label"),
	  NULL,
	  NULL,  /* XXX Add a tooltip! */
	  MAIL_FILTER_NO_LABEL },

	{ "mail-filter-read-messages",
	  "mail-read",
	  N_("Read Messages"),
	  NULL,
	  NULL,  /* XXX Add a tooltip! */
	  MAIL_FILTER_READ_MESSAGES },

	{ "mail-filter-recent-messages",
	  NULL,
	  N_("Recent Messages"),
	  NULL,
	  NULL,  /* XXX Add a tooltip! */
	  MAIL_FILTER_RECENT_MESSAGES },

	{ "mail-filter-unread-messages",
	  "mail-unread",
	  N_("Unread Messages"),
	  NULL,
	  NULL,  /* XXX Add a tooltip! */
	  MAIL_FILTER_UNREAD_MESSAGES }
};

static GtkRadioActionEntry mail_search_entries[] = {

	{ "mail-search-body-contains",
	  NULL,
	  N_("Body contains"),
	  NULL,
	  NULL,  /* XXX Add a tooltip! */
	  MAIL_SEARCH_BODY_CONTAINS },

	{ "mail-search-message-contains",
	  NULL,
	  N_("Message contains"),
	  NULL,
	  NULL,  /* XXX Add a tooltip! */
	  MAIL_SEARCH_MESSAGE_CONTAINS },

	{ "mail-search-recipients-contain",
	  NULL,
	  N_("Recipients contain"),
	  NULL,
	  NULL,  /* XXX Add a tooltip! */
	  MAIL_SEARCH_RECIPIENTS_CONTAIN },

	{ "mail-search-sender-contains",
	  NULL,
	  N_("Sender contains"),
	  NULL,
	  NULL,  /* XXX Add a tooltip! */
	  MAIL_SEARCH_SENDER_CONTAINS },

	{ "mail-search-subject-contains",
	  NULL,
	  N_("Subject contains"),
	  NULL,
	  NULL,  /* XXX Add a tooltip! */
	  MAIL_SEARCH_SUBJECT_CONTAINS },

	{ "mail-search-subject-or-addresses-contain",
	  NULL,
	  N_("Subject or Addresses contain"),
	  NULL,
	  NULL,  /* XXX Add a tooltip! */
	  MAIL_SEARCH_SUBJECT_OR_ADDRESSES_CONTAIN }
};

static GtkRadioActionEntry mail_scope_entries[] = {

	{ "mail-scope-all-accounts",
	  NULL,
	  N_("All Accounts"),
	  NULL,
	  NULL,  /* XXX Add a tooltip! */
	  MAIL_SCOPE_ALL_ACCOUNTS },

	{ "mail-scope-current-account",
	  NULL,
	  N_("Current Account"),
	  NULL,
	  NULL,  /* XXX Add a tooltip! */
	  MAIL_SCOPE_CURRENT_ACCOUNT },

	{ "mail-scope-current-folder",
	  NULL,
	  N_("Current Folder"),
	  NULL,
	  NULL,  /* XXX Add a tooltip! */
	  MAIL_SCOPE_CURRENT_FOLDER }
};

void
e_mail_shell_view_actions_init (EMailShellView *mail_shell_view)
{
	EShellView *shell_view;
	EShellWindow *shell_window;
	EShellContent *shell_content;
	GtkActionGroup *action_group;
	GtkRadioAction *radio_action;
	GConfBridge *bridge;
	GObject *object;
	const gchar *key;

	g_return_if_fail (E_IS_MAIL_SHELL_VIEW (mail_shell_view));

	shell_view = E_SHELL_VIEW (mail_shell_view);
	shell_window = e_shell_view_get_shell_window (shell_view);
	shell_content = e_shell_view_get_shell_content (shell_view);

	/* Mail Actions */
	action_group = ACTION_GROUP (MAIL);
	gtk_action_group_add_actions (
		action_group, mail_entries,
		G_N_ELEMENTS (mail_entries), mail_shell_view);
	e_action_group_add_popup_actions (
		action_group, mail_popup_entries,
		G_N_ELEMENTS (mail_popup_entries));
	gtk_action_group_add_toggle_actions (
		action_group, mail_toggle_entries,
		G_N_ELEMENTS (mail_toggle_entries), mail_shell_view);
	gtk_action_group_add_radio_actions (
		action_group, mail_view_entries,
		G_N_ELEMENTS (mail_view_entries), -1,
		G_CALLBACK (action_mail_view_cb), mail_shell_view);
	gtk_action_group_add_radio_actions (
		action_group, mail_search_entries,
		G_N_ELEMENTS (mail_search_entries),
		MAIL_SEARCH_SUBJECT_OR_ADDRESSES_CONTAIN,
		G_CALLBACK (action_mail_search_cb), mail_shell_view);
	gtk_action_group_add_radio_actions (
		action_group, mail_scope_entries,
		G_N_ELEMENTS (mail_scope_entries),
		MAIL_SCOPE_CURRENT_FOLDER,
		G_CALLBACK (action_search_scope_cb), mail_shell_view);

	radio_action = GTK_RADIO_ACTION (ACTION (MAIL_SCOPE_ALL_ACCOUNTS));
	e_shell_content_set_scope_action (shell_content, radio_action);
	e_shell_content_set_scope_visible (shell_content, TRUE);

	/* Bind GObject properties for GConf keys. */

	bridge = gconf_bridge_get ();

	object = G_OBJECT (ACTION (MAIL_PREVIEW));
	key = "/apps/evolution/mail/display/show_preview";
	gconf_bridge_bind_property (bridge, key, object, "active");

	object = G_OBJECT (ACTION (MAIL_THREADS_GROUP_BY));
	key = "/apps/evolution/mail/display/thread_list";
	gconf_bridge_bind_property (bridge, key, object, "active");

	object = G_OBJECT (ACTION (MAIL_VIEW_VERTICAL));
	key = "/apps/evolution/mail/display/layout";
	gconf_bridge_bind_property (bridge, key, object, "current-value");

	/* Fine tuning. */

	e_binding_new (
		ACTION (MAIL_THREADS_GROUP_BY), "active",
		ACTION (MAIL_FOLDER_SELECT_THREAD), "sensitive");

	e_binding_new (
		ACTION (MAIL_THREADS_GROUP_BY), "active",
		ACTION (MAIL_FOLDER_SELECT_SUBTHREAD), "sensitive");

	e_binding_new (
		ACTION (MAIL_THREADS_GROUP_BY), "active",
		ACTION (MAIL_THREADS_COLLAPSE_ALL), "sensitive");

	e_binding_new (
		ACTION (MAIL_THREADS_GROUP_BY), "active",
		ACTION (MAIL_THREADS_EXPAND_ALL), "sensitive");

	e_mutual_binding_new (
		ACTION (MAIL_PREVIEW), "active",
		shell_content, "preview-visible");

	e_binding_new (
		ACTION (MAIL_PREVIEW), "active",
		ACTION (MAIL_VIEW_CLASSIC), "sensitive");

	e_binding_new (
		ACTION (MAIL_PREVIEW), "active",
		ACTION (MAIL_VIEW_VERTICAL), "sensitive");

	/* XXX The boolean sense of the GConf key is the inverse of
	 *     the menu item, so we have to maintain two properties. */
	e_mutual_binding_new_with_negation (
		shell_content, "show-deleted",
		ACTION (MAIL_HIDE_DELETED), "active");

	/* Keep the sensitivity of "Create Search Folder from Search"
	 * in sync with "Save Search" so that its only selectable when
	 * showing search results. */
	e_binding_new (
		ACTION (SEARCH_SAVE), "sensitive",
		ACTION (MAIL_CREATE_SEARCH_FOLDER), "sensitive");

	g_signal_connect (
		ACTION (GAL_SAVE_CUSTOM_VIEW), "activate",
		G_CALLBACK (action_gal_save_custom_view_cb), mail_shell_view);

	g_signal_connect (
		ACTION (SEARCH_QUICK), "activate",
		G_CALLBACK (action_search_quick_cb), mail_shell_view);
}

/* Helper for e_mail_shell_view_update_popup_labels() */
static void
mail_shell_view_update_label_action (GtkToggleAction *action,
                                     MessageList *message_list,
                                     GPtrArray *uids,
                                     const gchar *label_tag)
{
	CamelFolder *folder;
	gboolean exists = FALSE;
	gboolean not_exists = FALSE;
	gboolean sensitive;
	guint ii;

	folder = message_list->folder;

	/* Figure out the proper label action state for the selected
	 * messages.  If all the selected messages have the given label,
	 * make the toggle action active.  If all the selected message
	 * DO NOT have the given label, make the toggle action inactive.
	 * If some do and some don't, make the action insensitive. */

	for (ii = 0; ii < uids->len && (!exists || !not_exists); ii++) {
		const gchar *old_label;
		gchar *new_label;

		/* Check for new-style labels. */
		if (camel_folder_get_message_user_flag (
			folder, uids->pdata[ii], label_tag)) {
			exists = TRUE;
			continue;
		}

		/* Check for old-style labels. */
		old_label = camel_folder_get_message_user_tag (
			folder, uids->pdata[ii], "label");
		if (old_label == NULL) {
			not_exists = TRUE;
			continue;
		}

		/* Convert old-style labels ("<name>") to "$Label<name>". */
		new_label = g_alloca (strlen (old_label) + 10);
		g_stpcpy (g_stpcpy (new_label, "$Label"), old_label);

		if (strcmp (new_label, label_tag) == 0)
			exists = TRUE;
		else
			not_exists = TRUE;
	}

	sensitive = !(exists && not_exists);
	gtk_toggle_action_set_active (action, exists);
	gtk_action_set_sensitive (GTK_ACTION (action), sensitive);
}

void
e_mail_shell_view_update_popup_labels (EMailShellView *mail_shell_view)
{
	EShell *shell;
	EShellSettings *shell_settings;
	EShellWindow *shell_window;
	EShellView *shell_view;
	EMailReader *reader;
	MessageList *message_list;
	GtkUIManager *ui_manager;
	GtkActionGroup *action_group;
	GtkTreeModel *tree_model;
	GtkTreeIter iter;
	GPtrArray *uids;
	const gchar *path;
	gboolean valid;
	guint merge_id;
	gint ii = 0;

	g_return_if_fail (E_IS_MAIL_SHELL_VIEW (mail_shell_view));

	shell_view = E_SHELL_VIEW (mail_shell_view);
	shell_window = e_shell_view_get_shell_window (shell_view);
	ui_manager = e_shell_window_get_ui_manager (shell_window);

	shell = e_shell_window_get_shell (shell_window);
	shell_settings = e_shell_get_shell_settings (shell);

	tree_model = e_shell_settings_get_object (
		shell_settings, "mail-label-list-store");

	action_group = ACTION_GROUP (MAIL_LABEL);
	merge_id = mail_shell_view->priv->label_merge_id;
	path = "/mail-message-popup/mail-label-menu/mail-label-actions";

	/* Unmerge the previous menu items. */
	gtk_ui_manager_remove_ui (ui_manager, merge_id);
	e_action_group_remove_all_actions (action_group);

	reader = E_MAIL_READER (mail_shell_view->priv->mail_shell_content);
	message_list = e_mail_reader_get_message_list (reader);
	uids = message_list_get_selected (message_list);

	valid = gtk_tree_model_get_iter_first (tree_model, &iter);

	while (valid) {
		GtkToggleAction *toggle_action;
		GtkAction *action;
		gchar *action_name;
		gchar *stock_id;
		gchar *label;
		gchar *tag;

		label = e_mail_label_list_store_get_name (
			E_MAIL_LABEL_LIST_STORE (tree_model), &iter);
		stock_id = e_mail_label_list_store_get_stock_id (
			E_MAIL_LABEL_LIST_STORE (tree_model), &iter);
		tag = e_mail_label_list_store_get_tag (
			E_MAIL_LABEL_LIST_STORE (tree_model), &iter);
		action_name = g_strdup_printf ("mail-label-%d", ii);

		/* XXX Add a tooltip! */
		toggle_action = gtk_toggle_action_new (
			action_name, label, NULL, stock_id);

		g_object_set_data_full (
			G_OBJECT (toggle_action), "tag",
			tag, (GDestroyNotify) g_free);

		/* Configure the action before we connect to signals. */
		mail_shell_view_update_label_action (
			toggle_action, message_list, uids, tag);

		g_signal_connect (
			toggle_action, "toggled",
			G_CALLBACK (action_mail_label_cb), mail_shell_view);

		/* The action group takes ownership of the action. */
		action = GTK_ACTION (toggle_action);
		gtk_action_group_add_action (action_group, action);
		g_object_unref (toggle_action);

		gtk_ui_manager_add_ui (
			ui_manager, merge_id, path, action_name,
			action_name, GTK_UI_MANAGER_AUTO, FALSE);

		g_free (label);
		g_free (stock_id);
		g_free (action_name);

		valid = gtk_tree_model_iter_next (tree_model, &iter);
		ii++;
	}

	message_list_free_uids (message_list, uids);

	g_object_unref (tree_model);
}

void
e_mail_shell_view_update_search_filter (EMailShellView *mail_shell_view)
{
	EShell *shell;
	EShellContent *shell_content;
	EShellSettings *shell_settings;
	EShellWindow *shell_window;
	EShellView *shell_view;
	GtkActionGroup *action_group;
	GtkRadioAction *radio_action;
	GtkTreeModel *tree_model;
	GtkTreeIter iter;
	GList *list;
	GSList *group;
	gboolean valid;
	gint ii = 0;

	g_return_if_fail (E_IS_MAIL_SHELL_VIEW (mail_shell_view));

	shell_view = E_SHELL_VIEW (mail_shell_view);
	shell_content = e_shell_view_get_shell_content (shell_view);
	shell_window = e_shell_view_get_shell_window (shell_view);

	shell = e_shell_window_get_shell (shell_window);
	shell_settings = e_shell_get_shell_settings (shell);

	tree_model = e_shell_settings_get_object (
		shell_settings, "mail-label-list-store");

	action_group = ACTION_GROUP (MAIL_FILTER);
	e_action_group_remove_all_actions (action_group);

	/* Add the standard filter actions. */
	gtk_action_group_add_radio_actions (
		action_group, mail_filter_entries,
		G_N_ELEMENTS (mail_filter_entries),
		MAIL_FILTER_ALL_MESSAGES,
		G_CALLBACK (action_search_filter_cb),
		mail_shell_view);

	/* Retrieve the radio group from an action we just added. */
	list = gtk_action_group_list_actions (action_group);
	radio_action = GTK_RADIO_ACTION (list->data);
	group = gtk_radio_action_get_group (radio_action);
	g_list_free (list);

	valid = gtk_tree_model_get_iter_first (tree_model, &iter);

	while (valid) {
		GtkAction *action;
		gchar *action_name;
		gchar *stock_id;
		gchar *label;

		label = e_mail_label_list_store_get_name (
			E_MAIL_LABEL_LIST_STORE (tree_model), &iter);
		stock_id = e_mail_label_list_store_get_stock_id (
			E_MAIL_LABEL_LIST_STORE (tree_model), &iter);

		action_name = g_strdup_printf ("mail-filter-label-%d", ii);
		radio_action = gtk_radio_action_new (
			action_name, label, NULL, stock_id, ii);
		g_free (action_name);

		gtk_radio_action_set_group (radio_action, group);
		group = gtk_radio_action_get_group (radio_action);

		/* The action group takes ownership of the action. */
		action = GTK_ACTION (radio_action);
		gtk_action_group_add_action (action_group, action);
		g_object_unref (radio_action);

		g_free (label);
		g_free (stock_id);

		valid = gtk_tree_model_iter_next (tree_model, &iter);
		ii++;
	}

	/* Use any action in the group; doesn't matter which. */
	e_shell_content_set_filter_action (shell_content, radio_action);

	ii = MAIL_FILTER_UNREAD_MESSAGES;
	e_shell_content_add_filter_separator_after (shell_content, ii);

	ii = MAIL_FILTER_READ_MESSAGES;
	e_shell_content_add_filter_separator_before (shell_content, ii);

	g_object_unref (tree_model);
}
