/*
 * anjal-shell-view-actions.c
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
action_mail_account_disable_cb (GtkAction *action,
                                AnjalShellView *mail_shell_view)
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
action_mail_download_foreach_cb (CamelService *service)
{
	if (CAMEL_IS_DISCO_STORE (service) || CAMEL_IS_OFFLINE_STORE (service))
		mail_store_prepare_offline (CAMEL_STORE (service));
}

static void
action_mail_download_cb (GtkAction *action,
                         AnjalShellView *mail_shell_view)
{
	e_mail_store_foreach ((GHFunc) action_mail_download_foreach_cb, NULL);
}

static void
action_mail_empty_trash_cb (GtkAction *action,
                            AnjalShellView *mail_shell_view)
{
	EShellWindow *shell_window;
	EShellView *shell_view;

	shell_view = E_SHELL_VIEW (mail_shell_view);
	shell_window = e_shell_view_get_shell_window (shell_view);

	em_utils_empty_trash (GTK_WIDGET (shell_window));
}

static void
action_mail_flush_outbox_cb (GtkAction *action,
                             AnjalShellView *mail_shell_view)
{
	mail_send ();
}

static void
action_mail_folder_copy_cb (GtkAction *action,
                            AnjalShellView *mail_shell_view)
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
                              AnjalShellView *mail_shell_view)
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
                               AnjalShellView *mail_shell_view)
{
	EShellWindow *shell_window;
	EShellView *shell_view;
	EMFolderTree *folder_tree;
	CamelFolder *folder;
	EMailShellSidebar * mail_shell_sidebar;

	shell_view = E_SHELL_VIEW (mail_shell_view);
	shell_window = e_shell_view_get_shell_window (shell_view);
	mail_shell_sidebar = mail_shell_view->priv->mail_shell_sidebar;
	folder_tree = e_mail_shell_sidebar_get_folder_tree (mail_shell_sidebar);
	folder = em_folder_tree_get_selected_folder (folder_tree);
	g_return_if_fail (folder != NULL);

	em_utils_expunge_folder (
		GTK_WIDGET (shell_window), folder);
}


static void
action_mail_folder_move_cb (GtkAction *action,
                            AnjalShellView *mail_shell_view)
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
                           AnjalShellView *mail_shell_view)
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
                                  AnjalShellView *mail_shell_view)
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
                               AnjalShellView *mail_shell_view)
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
                              AnjalShellView *mail_shell_view)
{
	EMailShellSidebar *mail_shell_sidebar;
	EMFolderTree *folder_tree;

	mail_shell_sidebar = mail_shell_view->priv->mail_shell_sidebar;
	folder_tree = e_mail_shell_sidebar_get_folder_tree (mail_shell_sidebar);

	em_folder_tree_edit_selected (folder_tree);
}


static void
action_mail_folder_unsubscribe_cb (GtkAction *action,
                                   AnjalShellView *mail_shell_view)
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
action_mail_search_cb (GtkRadioAction *action,
                       GtkRadioAction *current,
                       AnjalShellView *mail_shell_view)
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
action_mail_stop_cb (GtkAction *action,
                     AnjalShellView *mail_shell_view)
{
	mail_cancel_all ();
}


static void
action_mail_tools_filters_cb (GtkAction *action,
                              AnjalShellView *mail_shell_view)
{
	EShellWindow *shell_window;
	EShellView *shell_view;

	shell_view = E_SHELL_VIEW (mail_shell_view);
	shell_window = e_shell_view_get_shell_window (shell_view);

	em_utils_edit_filters (GTK_WIDGET (shell_window));
}

static void
action_mail_tools_search_folders_cb (GtkAction *action,
                                     AnjalShellView *mail_shell_view)
{
	vfolder_edit (E_SHELL_VIEW (mail_shell_view));
}

static void
action_mail_tools_subscriptions_cb (GtkAction *action,
                                    AnjalShellView *mail_shell_view)
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

#if 0
static void
action_search_filter_cb (GtkRadioAction *action,
                         GtkRadioAction *current,
                         EShellView *shell_view)
{
	EShellContent *shell_content;
	EShellWindow *shell_window;
	GKeyFile *key_file;
	const gchar *folder_uri = NULL;

	shell_content = e_shell_view_get_shell_content (shell_view);
	shell_window = e_shell_view_get_shell_window (shell_view);
	key_file = e_shell_view_get_state_key_file (shell_view);

	folder_uri = NULL; /* ANJAL: Get from folder tree */

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
#endif

#if 0
static void
action_search_scope_cb (GtkRadioAction *action,
                        GtkRadioAction *current,
                        EShellView *shell_view)
{
	EShellContent *shell_content;
	EShellWindow *shell_window;
	GKeyFile *key_file;
	const gchar *folder_uri;

	shell_content = e_shell_view_get_shell_content (shell_view);
	shell_window = e_shell_view_get_shell_window (shell_view);
	key_file = e_shell_view_get_state_key_file (shell_view);

	folder_uri = NULL; /* ANJAL: Get from folder tree */

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

#endif
static GtkActionEntry mail_entries[] = {

	{ "mail-account-disable",
	  NULL,
	  N_("_Disable Account"),
	  NULL,
	  N_("Disable this account"),
	  G_CALLBACK (action_mail_account_disable_cb) },

	{ "mail-download",
	  NULL,
	  N_("_Download Messages for Offline Usage"),
	  NULL,
	  N_("Download messages of accounts and folders marked for offline"),
	  G_CALLBACK (action_mail_download_cb) },

	{ "mail-empty-trashes", /* this is File->Empty Trash action */
	  NULL,
	  N_("Empty _Trash"),
	  NULL,
	  N_("Permanently remove all the deleted messages from all folders"),
	  G_CALLBACK (action_mail_empty_trash_cb) },

	{ "mail-empty-trash", /* this is a popup action over the trash folder */
	  NULL,
	  N_("_Empty Trash"),
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

	{ "mail-folder-unsubscribe",
	  NULL,
	  N_("_Unsubscribe"),
	  NULL,
	  N_("Unsubscribe from the selected folder"),
	  G_CALLBACK (action_mail_folder_unsubscribe_cb) },

	{ "mail-stop",
	  GTK_STOCK_STOP,
	  N_("Cancel"),
	  NULL,
	  N_("Cancel the current mail operation"),
	  G_CALLBACK (action_mail_stop_cb) },

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

	{ "mail-scope-current-folder",
	  NULL,
	  N_("Current Folder"),
	  NULL,
	  NULL,  /* XXX Add a tooltip! */
	  MAIL_SCOPE_CURRENT_FOLDER }
};

void
anjal_shell_view_actions_init (AnjalShellView *mail_shell_view)
{
	EShellView *shell_view;
	EShellWindow *shell_window;
	EShellContent *shell_content;
	GtkActionGroup *action_group;
	GtkRadioAction *radio_action;

	g_return_if_fail (ANJAL_IS_SHELL_VIEW (mail_shell_view));

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
	gtk_action_group_add_radio_actions (
		action_group, mail_search_entries,
		G_N_ELEMENTS (mail_search_entries),
		MAIL_SEARCH_SUBJECT_OR_ADDRESSES_CONTAIN,
		G_CALLBACK (action_mail_search_cb), mail_shell_view);
#if 0	
	gtk_action_group_add_radio_actions (
		action_group, mail_scope_entries,
		G_N_ELEMENTS (mail_scope_entries),
		MAIL_SCOPE_CURRENT_FOLDER,
		G_CALLBACK (action_search_scope_cb), mail_shell_view);
#endif

	radio_action = GTK_RADIO_ACTION (ACTION (MAIL_SCOPE_CURRENT_FOLDER));
	e_shell_content_set_scope_action (shell_content, radio_action);
	e_shell_content_set_scope_visible (shell_content, FALSE);

}


void
anjal_shell_view_update_popup_labels (AnjalShellView *mail_shell_view)
{

}

void
anjal_shell_view_update_search_filter (AnjalShellView *mail_shell_view)
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

	g_return_if_fail (ANJAL_IS_SHELL_VIEW (mail_shell_view));

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
#if 0	
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
#endif
	g_object_unref (tree_model);
}
