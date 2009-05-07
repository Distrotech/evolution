/*
 * e-book-shell-view-actions.c
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

#include "e-book-shell-view-private.h"

#include <e-util/e-error.h>
#include <e-util/e-util.h>
#include <filter/filter-rule.h>

#include <addressbook-config.h>

static void
action_address_book_copy_cb (GtkAction *action,
                             EBookShellView *book_shell_view)
{
	EBookShellContent *book_shell_content;
	EAddressbookView *view;

	book_shell_content = book_shell_view->priv->book_shell_content;
	view = e_book_shell_content_get_current_view (book_shell_content);
	g_return_if_fail (view != NULL);

	e_addressbook_view_copy_to_folder (view, TRUE);
}

static void
action_address_book_delete_cb (GtkAction *action,
                               EBookShellView *book_shell_view)
{
	EShellView *shell_view;
	EShellWindow *shell_window;
	EBookShellModule *book_shell_module;
	EBookShellSidebar *book_shell_sidebar;
	ESource *source;
	ESourceSelector *selector;
	ESourceGroup *source_group;
	ESourceList *source_list;
	EBook *book;
	gint response;
	GError *error = NULL;

	shell_view = E_SHELL_VIEW (book_shell_view);
	shell_window = e_shell_view_get_shell_window (shell_view);

	book_shell_module = book_shell_view->priv->book_shell_module;
	source_list = e_book_shell_module_get_source_list (book_shell_module);

	book_shell_sidebar = book_shell_view->priv->book_shell_sidebar;
	selector = e_book_shell_sidebar_get_selector (book_shell_sidebar);
	source = e_source_selector_peek_primary_selection (selector);
	g_return_if_fail (source != NULL);

	response = e_error_run (
		GTK_WINDOW (shell_window),
		"addressbook:ask-delete-addressbook",
		e_source_peek_name (source));

	if (response != GTK_RESPONSE_YES)
		return;

	book = e_book_new (source, &error);
	if (error != NULL) {
		g_warning ("Error removing addressbook: %s", error->message);
		g_error_free (error);
		return;
	}

	if (!e_book_remove (book, NULL)) {
		e_error_run (
			GTK_WINDOW (shell_window),
			"addressbook:remove-addressbook", NULL);
		g_object_unref (book);
		return;
	}

	if (e_source_selector_source_is_selected (selector, source))
		e_source_selector_unselect_source (selector, source);

	source_group = e_source_peek_group (source);
	e_source_group_remove_source (source_group, source);

	e_source_list_sync (source_list, NULL);

	g_object_unref (book);
}

static void
action_address_book_move_cb (GtkAction *action,
                             EBookShellView *book_shell_view)
{
	EBookShellContent *book_shell_content;
	EAddressbookView *view;

	book_shell_content = book_shell_view->priv->book_shell_content;
	view = e_book_shell_content_get_current_view (book_shell_content);
	g_return_if_fail (view != NULL);

	e_addressbook_view_move_to_folder (view, TRUE);
}

static void
action_address_book_new_cb (GtkAction *action,
                            EBookShellView *book_shell_view)
{
	EShellView *shell_view;
	EShellWindow *shell_window;

	shell_view = E_SHELL_VIEW (book_shell_view);
	shell_window = e_shell_view_get_shell_window (shell_view);

	addressbook_config_create_new_source (GTK_WIDGET (shell_window));
}

static void
action_address_book_properties_cb (GtkAction *action,
                                   EBookShellView *book_shell_view)
{
	EShellView *shell_view;
	EShellWindow *shell_window;
	EBookShellSidebar *book_shell_sidebar;
	ESource *source;
	ESourceSelector *selector;
	EditorUidClosure *closure;
	GHashTable *uid_to_editor;
	const gchar *uid;

	shell_view = E_SHELL_VIEW (book_shell_view);
	shell_window = e_shell_view_get_shell_window (shell_view);

	book_shell_sidebar = book_shell_view->priv->book_shell_sidebar;
	selector = e_book_shell_sidebar_get_selector (book_shell_sidebar);
	source = e_source_selector_peek_primary_selection (selector);
	g_return_if_fail (source != NULL);

	uid = e_source_peek_uid (source);
	uid_to_editor = book_shell_view->priv->uid_to_editor;

	closure = g_hash_table_lookup (uid_to_editor, uid);
	if (closure == NULL) {
		GtkWidget *editor;

		editor = addressbook_config_edit_source (
			GTK_WIDGET (shell_window), source);

		closure = g_new (EditorUidClosure, 1);
		closure->editor = editor;
		closure->uid = g_strdup (uid);
		closure->view = book_shell_view;

		g_hash_table_insert (uid_to_editor, closure->uid, closure);

		g_object_weak_ref (
			G_OBJECT (closure->editor), (GWeakNotify)
			e_book_shell_view_editor_weak_notify, closure);
	}

	gtk_window_present (GTK_WINDOW (closure->editor));
}

static void
action_address_book_rename_cb (GtkAction *action,
                               EBookShellView *book_shell_view)
{
	EBookShellSidebar *book_shell_sidebar;
	ESourceSelector *selector;

	book_shell_sidebar = book_shell_view->priv->book_shell_sidebar;
	selector = e_book_shell_sidebar_get_selector (book_shell_sidebar);

	e_source_selector_edit_primary_selection (selector);
}

static void
action_address_book_save_as_cb (GtkAction *action,
                                EBookShellView *book_shell_view)
{
	EBookShellContent *book_shell_content;
	EAddressbookView *view;

	book_shell_content = book_shell_view->priv->book_shell_content;
	view = e_book_shell_content_get_current_view (book_shell_content);
	g_return_if_fail (view != NULL);

	e_addressbook_view_save_as (view, TRUE);
}

static void
action_address_book_stop_cb (GtkAction *action,
                             EBookShellView *book_shell_view)
{
	EBookShellContent *book_shell_content;
	EAddressbookView *view;

	book_shell_content = book_shell_view->priv->book_shell_content;
	view = e_book_shell_content_get_current_view (book_shell_content);
	g_return_if_fail (view != NULL);

	e_addressbook_view_stop (view);
}

static void
action_contact_clipboard_copy_cb (GtkAction *action,
                                  EBookShellView *book_shell_view)
{
	EBookShellContent *book_shell_content;

	book_shell_content = book_shell_view->priv->book_shell_content;
	e_book_shell_content_clipboard_copy (book_shell_content);
}

static void
action_contact_clipboard_cut_cb (GtkAction *action,
                                 EBookShellView *book_shell_view)
{
	EBookShellContent *book_shell_content;
	EAddressbookView *view;

	book_shell_content = book_shell_view->priv->book_shell_content;
	view = e_book_shell_content_get_current_view (book_shell_content);
	g_return_if_fail (view != NULL);

	e_addressbook_view_cut (view);
}

static void
action_contact_clipboard_paste_cb (GtkAction *action,
                                   EBookShellView *book_shell_view)
{
	EBookShellContent *book_shell_content;
	EAddressbookView *view;

	book_shell_content = book_shell_view->priv->book_shell_content;
	view = e_book_shell_content_get_current_view (book_shell_content);
	g_return_if_fail (view != NULL);

	e_addressbook_view_paste (view);
}

static void
action_contact_copy_cb (GtkAction *action,
                        EBookShellView *book_shell_view)
{
	EBookShellContent *book_shell_content;
	EAddressbookView *view;

	book_shell_content = book_shell_view->priv->book_shell_content;
	view = e_book_shell_content_get_current_view (book_shell_content);
	g_return_if_fail (view != NULL);

	e_addressbook_view_copy_to_folder (view, FALSE);
}

static void
action_contact_delete_cb (GtkAction *action,
                          EBookShellView *book_shell_view)
{
	EBookShellContent *book_shell_content;
	EAddressbookView *view;

	book_shell_content = book_shell_view->priv->book_shell_content;
	view = e_book_shell_content_get_current_view (book_shell_content);
	g_return_if_fail (view != NULL);

	e_addressbook_view_delete_selection (view, TRUE);
}

static void
action_contact_forward_cb (GtkAction *action,
                           EBookShellView *book_shell_view)
{
	EBookShellContent *book_shell_content;
	EAddressbookView *view;
	GList *list, *iter;

	book_shell_content = book_shell_view->priv->book_shell_content;
	view = e_book_shell_content_get_current_view (book_shell_content);
	g_return_if_fail (view != NULL);

	list = e_addressbook_view_get_selected (view);
	g_return_if_fail (list != NULL);

	/* Convert the list of contacts to a list of destinations. */
	for (iter = list; iter != NULL; iter = iter->next) {
		EContact *contact = iter->data;
		EDestination *destination;

		destination = e_destination_new ();
		e_destination_set_contact (destination, contact, 0);
		g_object_unref (contact);

		iter->data = destination;
	}

	eab_send_as_attachment (list);
	g_list_foreach (list, (GFunc) g_object_unref, NULL);
	g_list_free (list);
}

static void
action_contact_move_cb (GtkAction *action,
                        EBookShellView *book_shell_view)
{
	EBookShellContent *book_shell_content;
	EAddressbookView *view;

	book_shell_content = book_shell_view->priv->book_shell_content;
	view = e_book_shell_content_get_current_view (book_shell_content);
	g_return_if_fail (view != NULL);

	e_addressbook_view_move_to_folder (view, FALSE);
}

static void
action_contact_new_cb (GtkAction *action,
                       EBookShellView *book_shell_view)
{
	EBookShellContent *book_shell_content;
	EAddressbookView *view;
	EAddressbookModel *model;
	EContact *contact;
	GtkWidget *editor;
	EBook *book;

	book_shell_content = book_shell_view->priv->book_shell_content;
	view = e_book_shell_content_get_current_view (book_shell_content);
	g_return_if_fail (view != NULL);

	model = e_addressbook_view_get_model (view);
	book = e_addressbook_model_get_book (model);
	g_return_if_fail (book != NULL);

	contact = e_contact_new ();
	editor = e_contact_editor_new (book, contact, TRUE, TRUE);
	eab_editor_show (EAB_EDITOR (editor));
	g_object_unref (contact);
}

static void
action_contact_new_list_cb (GtkAction *action,
                            EBookShellView *book_shell_view)
{
	EBookShellContent *book_shell_content;
	EAddressbookView *view;
	EAddressbookModel *model;
	EContact *contact;
	GtkWidget *editor;
	EBook *book;

	book_shell_content = book_shell_view->priv->book_shell_content;
	view = e_book_shell_content_get_current_view (book_shell_content);
	g_return_if_fail (view != NULL);

	model = e_addressbook_view_get_model (view);
	book = e_addressbook_model_get_book (model);
	g_return_if_fail (book != NULL);

	contact = e_contact_new ();
	editor = e_contact_list_editor_new (book, contact, TRUE, TRUE);
	eab_editor_show (EAB_EDITOR (editor));
	g_object_unref (contact);
}

static void
action_contact_open_cb (GtkAction *action,
                        EBookShellView *book_shell_view)
{
	EBookShellContent *book_shell_content;
	EAddressbookView *view;

	book_shell_content = book_shell_view->priv->book_shell_content;
	view = e_book_shell_content_get_current_view (book_shell_content);
	g_return_if_fail (view != NULL);

	e_addressbook_view_view (view);
}

static void
action_contact_preview_cb (GtkToggleAction *action,
                           EBookShellView *book_shell_view)
{
	EBookShellContent *book_shell_content;
	gboolean visible;

	book_shell_content = book_shell_view->priv->book_shell_content;
	visible = gtk_toggle_action_get_active (action);
	e_book_shell_content_set_preview_visible (book_shell_content, visible);
}

static void
action_contact_print_cb (GtkAction *action,
                         EBookShellView *book_shell_view)
{
	EBookShellContent *book_shell_content;
	EAddressbookView *view;
	GtkPrintOperationAction print_action;

	book_shell_content = book_shell_view->priv->book_shell_content;
	view = e_book_shell_content_get_current_view (book_shell_content);
	g_return_if_fail (view != NULL);

	print_action = GTK_PRINT_OPERATION_ACTION_PRINT_DIALOG;
	e_addressbook_view_print (view, print_action);
}

static void
action_contact_print_preview_cb (GtkAction *action,
                                 EBookShellView *book_shell_view)
{
	EBookShellContent *book_shell_content;
	EAddressbookView *view;
	GtkPrintOperationAction print_action;

	book_shell_content = book_shell_view->priv->book_shell_content;
	view = e_book_shell_content_get_current_view (book_shell_content);
	g_return_if_fail (view != NULL);

	print_action = GTK_PRINT_OPERATION_ACTION_PREVIEW;
	e_addressbook_view_print (view, print_action);
}

static void
action_contact_save_as_cb (GtkAction *action,
                           EBookShellView *book_shell_view)
{
	EBookShellContent *book_shell_content;
	EAddressbookView *view;

	book_shell_content = book_shell_view->priv->book_shell_content;
	view = e_book_shell_content_get_current_view (book_shell_content);
	g_return_if_fail (view != NULL);

	e_addressbook_view_save_as (view, FALSE);
}

static void
action_contact_select_all_cb (GtkAction *action,
                              EBookShellView *book_shell_view)
{
	EBookShellContent *book_shell_content;
	EAddressbookView *view;

	book_shell_content = book_shell_view->priv->book_shell_content;
	view = e_book_shell_content_get_current_view (book_shell_content);
	g_return_if_fail (view != NULL);

	e_addressbook_view_select_all (view);
}

static void
action_contact_send_message_cb (GtkAction *action,
                                EBookShellView *book_shell_view)
{
	EBookShellContent *book_shell_content;
	EAddressbookView *view;
	GList *list, *iter;

	book_shell_content = book_shell_view->priv->book_shell_content;
	view = e_book_shell_content_get_current_view (book_shell_content);
	g_return_if_fail (view != NULL);

	list = e_addressbook_view_get_selected (view);
	g_return_if_fail (list != NULL);

	/* Convert the list of contacts to a list of destinations. */
	for (iter = list; iter != NULL; iter = iter->next) {
		EContact *contact = iter->data;
		EDestination *destination;

		destination = e_destination_new ();
		e_destination_set_contact (destination, contact, 0);
		g_object_unref (contact);

		iter->data = destination;
	}

	eab_send_as_to (list);
	g_list_foreach (list, (GFunc) g_object_unref, NULL);
	g_list_free (list);
}

static void
action_gal_save_custom_view_cb (GtkAction *action,
                                EBookShellView *book_shell_view)
{
	EBookShellContent *book_shell_content;
	EShellView *shell_view;
	EAddressbookView *address_view;
	GalViewInstance *view_instance;

	/* All shell views respond to the activation of this action,
	 * which is defined by EShellWindow.  But only the currently
	 * active shell view proceeds with saving the custom view. */
	shell_view = E_SHELL_VIEW (book_shell_view);
	if (!e_shell_view_is_active (shell_view))
		return;

	book_shell_content = book_shell_view->priv->book_shell_content;
	address_view = e_book_shell_content_get_current_view (book_shell_content);
	view_instance = e_addressbook_view_get_view_instance (address_view);
	gal_view_instance_save_as (view_instance);
}

static void
action_search_execute_cb (GtkAction *action,
                          EBookShellView *book_shell_view)
{
	EShellView *shell_view;

	/* All shell views respond to the activation of this action,
	 * which is defined by EShellWindow.  But only the currently
	 * active shell view proceeds with executing the search. */
	shell_view = E_SHELL_VIEW (book_shell_view);
	if (!e_shell_view_is_active (shell_view))
		return;

	e_book_shell_view_execute_search (book_shell_view);
}

static void
action_search_filter_cb (GtkRadioAction *action,
                         GtkRadioAction *current,
                         EBookShellView *book_shell_view)
{
	e_book_shell_view_execute_search (book_shell_view);
}

static GtkActionEntry contact_entries[] = {

	{ "address-book-copy",
	  GTK_STOCK_COPY,
	  N_("Co_py All Contacts To..."),
	  NULL,
	  N_("Copy the contacts of the selected address book to another"),
	  G_CALLBACK (action_address_book_copy_cb) },

	{ "address-book-delete",
	  GTK_STOCK_DELETE,
	  N_("Del_ete Address Book"),
	  NULL,
	  N_("Delete the selected address book"),
	  G_CALLBACK (action_address_book_delete_cb) },

	{ "address-book-move",
	  "folder-move",
	  N_("Mo_ve All Contacts To..."),
	  NULL,
	  N_("Move the contacts of the selected address book to another"),
	  G_CALLBACK (action_address_book_move_cb) },

	{ "address-book-new",
	  "address-book-new",
	  N_("_New Address Book"),
	  NULL,
	  N_("Create a new address book"),
	  G_CALLBACK (action_address_book_new_cb) },

	{ "address-book-properties",
	  GTK_STOCK_PROPERTIES,
	  N_("Address _Book Properties"),
	  NULL,
	  N_("Show properties of the selected address book"),
	  G_CALLBACK (action_address_book_properties_cb) },

	{ "address-book-rename",
	  NULL,
	  N_("_Rename..."),
	  "F2",
	  N_("Rename the selected address book"),
	  G_CALLBACK (action_address_book_rename_cb) },

	{ "address-book-save-as",
	  GTK_STOCK_SAVE_AS,
	  N_("S_ave Address Book as vCard"),
	  NULL,
	  N_("Save the contacts of the selected address book as a vCard"),
	  G_CALLBACK (action_address_book_save_as_cb) },

	{ "address-book-stop",
	  GTK_STOCK_STOP,
	  NULL,
	  NULL,
	  N_("Stop loading"),
	  G_CALLBACK (action_address_book_stop_cb) },

	{ "contact-clipboard-copy",
	  GTK_STOCK_COPY,
	  NULL,
	  NULL,
	  N_("Copy the selection"),
	  G_CALLBACK (action_contact_clipboard_copy_cb) },

	{ "contact-clipboard-cut",
	  GTK_STOCK_CUT,
	  NULL,
	  NULL,
	  N_("Cut the selection"),
	  G_CALLBACK (action_contact_clipboard_cut_cb) },

	{ "contact-clipboard-paste",
	  GTK_STOCK_PASTE,
	  NULL,
	  NULL,
	  N_("Paste the clipboard"),
	  G_CALLBACK (action_contact_clipboard_paste_cb) },

	{ "contact-copy",
	  NULL,
	  N_("_Copy Contact To..."),
	  "<Control><Shift>y",
	  N_("Copy selected contacts to another address book"),
	  G_CALLBACK (action_contact_copy_cb) },

	{ "contact-delete",
	  GTK_STOCK_DELETE,
	  N_("_Delete Contact"),
	  "<Control>d",
	  N_("Delete selected contacts"),
	  G_CALLBACK (action_contact_delete_cb) },

	{ "contact-forward",
	  "mail-forward",
	  N_("_Forward Contact..."),
	  NULL,
	  N_("Send selected contacts to another person"),
	  G_CALLBACK (action_contact_forward_cb) },

	{ "contact-move",
	  NULL,
	  N_("_Move Contact To..."),
	  "<Control><Shift>v",
	  N_("Move selected contacts to another address book"),
	  G_CALLBACK (action_contact_move_cb) },

	{ "contact-new",
	  "contact-new",
	  N_("_New Contact..."),
	  NULL,
	  N_("Create a new contact"),
	  G_CALLBACK (action_contact_new_cb) },

	{ "contact-new-list",
	  "stock_contact-list",
	  N_("New Contact _List..."),
	  NULL,
	  N_("Create a new contact list"),
	  G_CALLBACK (action_contact_new_list_cb) },

	{ "contact-open",
	  NULL,
	  N_("_Open"),
	  "<Control>o",
	  N_("View the current contact"),
	  G_CALLBACK (action_contact_open_cb) },

	{ "contact-save-as",
	  GTK_STOCK_SAVE_AS,
	  N_("Save as vCard..."),
	  NULL,
	  N_("Save selected contacts as a vCard"),
	  G_CALLBACK (action_contact_save_as_cb) },

	{ "contact-select-all",
	  GTK_STOCK_SELECT_ALL,
	  NULL,
	  NULL,
	  N_("Select all contacts"),
	  G_CALLBACK (action_contact_select_all_cb) },

	{ "contact-send-message",
	  "mail-message-new",
	  N_("_Send Message to Contact..."),
	  NULL,
	  N_("Send a message to the selected contacts"),
	  G_CALLBACK (action_contact_send_message_cb) },

	/*** Menus ***/

	{ "actions-menu",
	  NULL,
	  N_("_Actions"),
	  NULL,
	  NULL,
	  NULL }
};

static EPopupActionEntry contact_popup_entries[] = {

	{ "address-book-popup-delete",
	  N_("_Delete"),
	  "address-book-delete" },

	{ "address-book-popup-properties",
	  N_("_Properties"),
	  "address-book-properties" },

	{ "address-book-popup-rename",
	  NULL,
	  "address-book-rename" },

	{ "address-book-popup-save-as",
	  N_("_Save as vCard..."),
	  "address-book-save-as" },

	{ "contact-popup-clipboard-copy",
	  NULL,
	  "contact-clipboard-copy" },

	{ "contact-popup-clipboard-cut",
	  NULL,
	  "contact-clipboard-cut" },

	{ "contact-popup-clipboard-paste",
	  NULL,
	  "contact-clipboard-paste" },

	{ "contact-popup-copy",
	  NULL,
	  "contact-copy" },

	{ "contact-popup-delete",
	  NULL,
	  "contact-delete" },

	{ "contact-popup-forward",
	  NULL,
	  "contact-forward" },

	{ "contact-popup-move",
	  NULL,
	  "contact-move" },

	{ "contact-popup-open",
	  NULL,
	  "contact-open" },

	{ "contact-popup-save-as",
	  NULL,
	  "contact-save-as" },

	{ "contact-popup-send-message",
	  NULL,
	  "contact-send-message" },
};

static GtkToggleActionEntry contact_toggle_entries[] = {

	{ "contact-preview",
	  NULL,
	  N_("Contact _Preview"),
	  "<Control>m",
	  N_("Show contact preview window"),
	  G_CALLBACK (action_contact_preview_cb),
	  TRUE }
};

static GtkRadioActionEntry contact_filter_entries[] = {

	{ "contact-filter-any-category",
	  NULL,
	  N_("Any Category"),
	  NULL,
	  NULL,
	  CONTACT_FILTER_ANY_CATEGORY },

	{ "contact-filter-unmatched",
	  NULL,
	  N_("Unmatched"),
	  NULL,
	  NULL,
	  CONTACT_FILTER_UNMATCHED }
};

static GtkRadioActionEntry contact_search_entries[] = {

	{ "contact-search-any-field-contains",
	  NULL,
	  N_("Any field contains"),
	  NULL,
	  NULL,  /* XXX Add a tooltip! */
	  CONTACT_SEARCH_ANY_FIELD_CONTAINS },

	{ "contact-search-email-begins-with",
	  NULL,
	  N_("Email begins with"),
	  NULL,
	  NULL,  /* XXX Add a tooltip! */
	  CONTACT_SEARCH_EMAIL_BEGINS_WITH },

	{ "contact-search-name-contains",
	  NULL,
	  N_("Name contains"),
	  NULL,
	  NULL,  /* XXX Add a tooltip! */
	  CONTACT_SEARCH_NAME_CONTAINS }
};

static GtkActionEntry lockdown_printing_entries[] = {

	{ "contact-print",
	  GTK_STOCK_PRINT,
	  NULL,
	  "<Control>p",
	  N_("Print selected contacts"),
	  G_CALLBACK (action_contact_print_cb) },

	{ "contact-print-preview",
	  GTK_STOCK_PRINT_PREVIEW,
	  NULL,
	  NULL,
	  N_("Preview the contacts to be printed"),
	  G_CALLBACK (action_contact_print_preview_cb) }
};

static EPopupActionEntry lockdown_printing_popup_entries[] = {

	{ "contact-popup-print",
	  NULL,
	  "contact-print" }
};

void
e_book_shell_view_actions_init (EBookShellView *book_shell_view)
{
	EShellView *shell_view;
	EShellWindow *shell_window;
	GtkActionGroup *action_group;
	GConfBridge *bridge;
	GtkAction *action;
	GObject *object;
	const gchar *key;

	shell_view = E_SHELL_VIEW (book_shell_view);
	shell_window = e_shell_view_get_shell_window (shell_view);

	/* Contact Actions */
	action_group = ACTION_GROUP (CONTACTS);
	gtk_action_group_add_actions (
		action_group, contact_entries,
		G_N_ELEMENTS (contact_entries), book_shell_view);
	e_action_group_add_popup_actions (
		action_group, contact_popup_entries,
		G_N_ELEMENTS (contact_popup_entries));
	gtk_action_group_add_toggle_actions (
		action_group, contact_toggle_entries,
		G_N_ELEMENTS (contact_toggle_entries), book_shell_view);
	gtk_action_group_add_radio_actions (
		action_group, contact_search_entries,
		G_N_ELEMENTS (contact_search_entries),
		CONTACT_SEARCH_NAME_CONTAINS,
		NULL, NULL);

	/* Lockdown Printing Actions */
	action_group = ACTION_GROUP (LOCKDOWN_PRINTING);
	gtk_action_group_add_actions (
		action_group, lockdown_printing_entries,
		G_N_ELEMENTS (lockdown_printing_entries), book_shell_view);
	e_action_group_add_popup_actions (
		action_group, lockdown_printing_popup_entries,
		G_N_ELEMENTS (lockdown_printing_popup_entries));

	/* Bind GObject properties to GConf keys. */

	bridge = gconf_bridge_get ();

	object = G_OBJECT (ACTION (CONTACT_PREVIEW));
	key = "/apps/evolution/addressbook/display/show_preview";
	gconf_bridge_bind_property (bridge, key, object, "active");

	/* Fine tuning. */

	action = ACTION (CONTACT_DELETE);
	g_object_set (action, "short-label", _("Delete"), NULL);

	g_signal_connect (
		ACTION (GAL_SAVE_CUSTOM_VIEW), "activate",
		G_CALLBACK (action_gal_save_custom_view_cb), book_shell_view);

	g_signal_connect (
		ACTION (SEARCH_EXECUTE), "activate",
		G_CALLBACK (action_search_execute_cb), book_shell_view);
}

void
e_book_shell_view_update_search_filter (EBookShellView *book_shell_view)
{
	EShellView *shell_view;
	EShellContent *shell_content;
	EShellWindow *shell_window;
	GtkActionGroup *action_group;
	GtkRadioAction *radio_action;
	GList *list, *iter;
	GSList *group;
	gint ii;

	shell_view = E_SHELL_VIEW (book_shell_view);
	shell_content = e_shell_view_get_shell_content (shell_view);
	shell_window = e_shell_view_get_shell_window (shell_view);

	action_group = ACTION_GROUP (CONTACTS_FILTER);
	e_action_group_remove_all_actions (action_group);

	/* Add the standard filter actions. */
	gtk_action_group_add_radio_actions (
		action_group, contact_filter_entries,
		G_N_ELEMENTS (contact_filter_entries),
		CONTACT_FILTER_ANY_CATEGORY,
		G_CALLBACK (action_search_filter_cb),
		book_shell_view);

	/* Retrieve the radio group from an action we just added. */
	list = gtk_action_group_list_actions (action_group);
	radio_action = GTK_RADIO_ACTION (list->data);
	group = gtk_radio_action_get_group (radio_action);
	g_list_free (list);

	/* Build the category actions. */

	list = e_categories_get_list ();
	for (iter = list, ii = 0; iter != NULL; iter = iter->next, ii++) {
		const gchar *category_name = iter->data;
		const gchar *filename;
		GtkAction *action;
		gchar *action_name;

		action_name = g_strdup_printf (
			"contact-filter-category-%d", ii);
		radio_action = gtk_radio_action_new (
			action_name, category_name, NULL, NULL, ii);
		g_free (action_name);

		/* Convert the category icon file to a themed icon name. */
		filename = e_categories_get_icon_file_for (category_name);
		if (filename != NULL && *filename != '\0') {
			gchar *basename;
			gchar *cp;

			basename = g_path_get_basename (filename);

			/* Lose the file extension. */
			if ((cp = strrchr (basename, '.')) != NULL)
				*cp = '\0';

			g_object_set (
				radio_action, "icon-name", basename, NULL);

			g_free (basename);
		}

		gtk_radio_action_set_group (radio_action, group);
		group = gtk_radio_action_get_group (radio_action);

		/* The action group takes ownership of the action. */
		action = GTK_ACTION (radio_action);
		gtk_action_group_add_action (action_group, action);
		g_object_unref (radio_action);
	}
	g_list_free (list);

	/* Use any action in the group; doesn't matter which. */
	e_shell_content_set_filter_action (shell_content, radio_action);

	ii = CONTACT_FILTER_UNMATCHED;
	e_shell_content_add_filter_separator_after (shell_content, ii);
}
