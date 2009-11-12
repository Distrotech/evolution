/*
 * e-book-shell-view.c
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

static gpointer parent_class;
static GType book_shell_view_type;

static void
book_shell_view_source_list_changed_cb (EBookShellView *book_shell_view,
                                        ESourceList *source_list)
{
	EBookShellViewPrivate *priv = book_shell_view->priv;
	EBookShellContent *book_shell_content;
	EShellView *shell_view;
	GList *keys, *iter;

	shell_view = E_SHELL_VIEW (book_shell_view);
	book_shell_content = book_shell_view->priv->book_shell_content;

	keys = g_hash_table_get_keys (priv->uid_to_view);
	for (iter = keys; iter != NULL; iter = iter->next) {
		gchar *uid = iter->data;
		EAddressbookView *view;

		/* If the source still exists, move on. */
		if (e_source_list_peek_source_by_uid (source_list, uid))
			continue;

		/* Remove the view for the deleted source. */
		view = g_hash_table_lookup (priv->uid_to_view, uid);
		e_book_shell_content_remove_view (book_shell_content, view);
		g_hash_table_remove (priv->uid_to_view, uid);
	}
	g_list_free (keys);

	keys = g_hash_table_get_keys (priv->uid_to_editor);
	for (iter = keys; iter != NULL; iter = iter->next) {
		gchar *uid = iter->data;
		EditorUidClosure *closure;

		/* If the source still exists, move on. */
		if (e_source_list_peek_source_by_uid (source_list, uid))
			continue;

		/* Remove the editor for the deleted source. */
		closure = g_hash_table_lookup (priv->uid_to_editor, uid);
		g_object_weak_unref (
			G_OBJECT (closure->editor), (GWeakNotify)
			e_book_shell_view_editor_weak_notify, closure);
		gtk_widget_destroy (closure->editor);
		g_hash_table_remove (priv->uid_to_editor, uid);
	}
	g_list_free (keys);

	e_shell_view_update_actions (shell_view);
}

static void
book_shell_view_dispose (GObject *object)
{
	EBookShellView *book_shell_view;

	book_shell_view = E_BOOK_SHELL_VIEW (object);
	e_book_shell_view_private_dispose (book_shell_view);

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
book_shell_view_finalize (GObject *object)
{
	EBookShellView *book_shell_view;

	book_shell_view = E_BOOK_SHELL_VIEW (object);
	e_book_shell_view_private_finalize (book_shell_view);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
book_shell_view_constructed (GObject *object)
{
	EBookShellView *book_shell_view;
	EBookShellBackend *book_shell_backend;
	ESourceList *source_list;

	/* Chain up to parent's constructed() method. */
	G_OBJECT_CLASS (parent_class)->constructed (object);

	book_shell_view = E_BOOK_SHELL_VIEW (object);
	e_book_shell_view_private_constructed (book_shell_view);

	book_shell_backend = book_shell_view->priv->book_shell_backend;
	source_list = e_book_shell_backend_get_source_list (book_shell_backend);

	g_signal_connect_swapped (
		source_list, "changed",
		G_CALLBACK (book_shell_view_source_list_changed_cb),
		book_shell_view);
}

static void
book_shell_view_execute_search (EShellView *shell_view)
{
	EBookShellViewPrivate *priv;
	EBookShellContent *book_shell_content;
	EShellWindow *shell_window;
	EShellContent *shell_content;
	GtkRadioAction *action;
	GString *string;
	EAddressbookView *view;
	EAddressbookModel *model;
	const gchar *format;
	const gchar *text;
	gchar *query;
	gchar *temp;
	gint value;

	priv = E_BOOK_SHELL_VIEW_GET_PRIVATE (shell_view);

	shell_content = e_shell_view_get_shell_content (shell_view);
	text = e_shell_content_get_search_text (shell_content);

	shell_window = e_shell_view_get_shell_window (shell_view);
	action = GTK_RADIO_ACTION (ACTION (CONTACT_SEARCH_ANY_FIELD_CONTAINS));
	value = gtk_radio_action_get_current_value (action);

	if (text == NULL || *text == '\0') {
		text = "";
		value = CONTACT_SEARCH_ANY_FIELD_CONTAINS;
	}

	switch (value) {
		case CONTACT_SEARCH_NAME_CONTAINS:
			format = "(contains \"full_name\" %s)";
			break;

		case CONTACT_SEARCH_EMAIL_BEGINS_WITH:
			format = "(beginswith \"email\" %s)";
			break;

		default:
			text = "";
			/* fall through */

		case CONTACT_SEARCH_ANY_FIELD_CONTAINS:
			format = "(contains \"x-evolution-any-field\" %s)";
			break;
	}

	/* Build the query. */
	string = g_string_new ("");
	e_sexp_encode_string (string, text);
	query = g_strdup_printf (format, string->str);
	g_string_free (string, TRUE);

	/* Apply selected filter. */
	value = e_shell_content_get_filter_value (shell_content);
	switch (value) {
		case CONTACT_FILTER_ANY_CATEGORY:
			break;

		case CONTACT_FILTER_UNMATCHED:
			temp = g_strdup_printf (
				"(and (not (and (exists \"CATEGORIES\") "
				"(not (is \"CATEGORIES\" \"\")))) %s)",
				query);
			g_free (query);
			query = temp;
			break;

		default:
		{
			GList *categories;
			const gchar *category_name;

			categories = e_categories_get_list ();
			category_name = g_list_nth_data (categories, value);
			g_list_free (categories);

			temp = g_strdup_printf (
				"(and (is \"category_list\" \"%s\") %s)",
				category_name, query);
			g_free (query);
			query = temp;
		}
	}

	/* XXX This is wrong.  We need to programmatically construct an
	 *     EFilterRule, tell it to build code, and pass the resulting
	 *     expression string to EAddressbookModel. */
	e_shell_content_set_search_rule (shell_content, NULL);

	/* Submit the query. */
	book_shell_content = E_BOOK_SHELL_CONTENT (shell_content);
	view = e_book_shell_content_get_current_view (book_shell_content);
	model = e_addressbook_view_get_model (view);
	e_addressbook_model_set_query (model, query);
	g_free (query);

	e_book_shell_content_set_preview_contact (book_shell_content, NULL);
	priv->preview_index = -1;
}

static void
book_shell_view_update_actions (EShellView *shell_view)
{
	EBookShellViewPrivate *priv;
	EShellContent *shell_content;
	EShellSidebar *shell_sidebar;
	EShellWindow *shell_window;
	GtkAction *action;
	const gchar *label;
	gboolean sensitive;
	guint32 state;

	/* Be descriptive. */
	gboolean any_contacts_selected;
	gboolean has_primary_source;
	gboolean multiple_contacts_selected;
	gboolean primary_source_is_system;
	gboolean single_contact_selected;
	gboolean selection_is_contact_list;
	gboolean selection_has_email;
	gboolean source_is_busy;
	gboolean source_is_editable;
	gboolean source_is_empty;
	gboolean clipboard_has_directory;

	priv = E_BOOK_SHELL_VIEW_GET_PRIVATE (shell_view);

	shell_window = e_shell_view_get_shell_window (shell_view);

	shell_content = e_shell_view_get_shell_content (shell_view);
	state = e_shell_content_check_state (shell_content);

	single_contact_selected =
		(state & E_BOOK_SHELL_CONTENT_SELECTION_SINGLE);
	multiple_contacts_selected =
		(state & E_BOOK_SHELL_CONTENT_SELECTION_MULTIPLE);
	selection_has_email =
		(state & E_BOOK_SHELL_CONTENT_SELECTION_HAS_EMAIL);
	selection_is_contact_list =
		(state & E_BOOK_SHELL_CONTENT_SELECTION_IS_CONTACT_LIST);
	source_is_busy =
		(state & E_BOOK_SHELL_CONTENT_SOURCE_IS_BUSY);
	source_is_editable =
		(state & E_BOOK_SHELL_CONTENT_SOURCE_IS_EDITABLE);
	source_is_empty =
		(state & E_BOOK_SHELL_CONTENT_SOURCE_IS_EMPTY);
	clipboard_has_directory =
		(state & E_BOOK_SHELL_CONTENT_CLIPBOARD_HAS_DIRECTORY);

	shell_sidebar = e_shell_view_get_shell_sidebar (shell_view);
	state = e_shell_sidebar_check_state (shell_sidebar);

	has_primary_source =
		(state & E_BOOK_SHELL_SIDEBAR_HAS_PRIMARY_SOURCE);
	primary_source_is_system =
		(state & E_BOOK_SHELL_SIDEBAR_PRIMARY_SOURCE_IS_SYSTEM);

	any_contacts_selected =
		(single_contact_selected || multiple_contacts_selected);

	action = ACTION (ADDRESS_BOOK_DELETE);
	sensitive = has_primary_source && !primary_source_is_system;
	gtk_action_set_sensitive (action, sensitive);

	action = ACTION (ADDRESS_BOOK_RENAME);
	sensitive = has_primary_source;
	gtk_action_set_sensitive (action, sensitive);

	action = ACTION (ADDRESS_BOOK_STOP);
	sensitive = source_is_busy;
	gtk_action_set_sensitive (action, sensitive);

	action = ACTION (CONTACT_CLIPBOARD_COPY);
	sensitive = any_contacts_selected;
	gtk_action_set_sensitive (action, sensitive);

	action = ACTION (CONTACT_CLIPBOARD_CUT);
	sensitive = source_is_editable && any_contacts_selected;
	gtk_action_set_sensitive (action, sensitive);

	action = ACTION (CONTACT_CLIPBOARD_PASTE);
	sensitive = source_is_editable && clipboard_has_directory;
	gtk_action_set_sensitive (action, sensitive);

	action = ACTION (CONTACT_COPY);
	sensitive = any_contacts_selected;
	gtk_action_set_sensitive (action, sensitive);

	action = ACTION (CONTACT_DELETE);
	sensitive = source_is_editable && any_contacts_selected;
	gtk_action_set_sensitive (action, sensitive);

	action = ACTION (CONTACT_FORWARD);
	sensitive = any_contacts_selected;
	gtk_action_set_sensitive (action, sensitive);
	if (multiple_contacts_selected)
		label = _("_Forward Contacts");
	else
		label = _("_Forward Contact");
	g_object_set (action, "label", label, NULL);

	action = ACTION (CONTACT_MOVE);
	sensitive = source_is_editable && any_contacts_selected;
	gtk_action_set_sensitive (action, sensitive);

	action = ACTION (CONTACT_OPEN);
	sensitive = any_contacts_selected;
	gtk_action_set_sensitive (action, sensitive);

	action = ACTION (CONTACT_PRINT);
	sensitive = any_contacts_selected;
	gtk_action_set_sensitive (action, sensitive);

	action = ACTION (CONTACT_PRINT_PREVIEW);
	sensitive = any_contacts_selected;
	gtk_action_set_sensitive (action, sensitive);

	action = ACTION (CONTACT_SAVE_AS);
	sensitive = any_contacts_selected;
	gtk_action_set_sensitive (action, sensitive);

	action = ACTION (CONTACT_SELECT_ALL);
	sensitive = !(source_is_empty);
	gtk_action_set_sensitive (action, sensitive);

	action = ACTION (CONTACT_SEND_MESSAGE);
	sensitive = any_contacts_selected && selection_has_email;
	gtk_action_set_sensitive (action, sensitive);
	if (multiple_contacts_selected)
		label = _("_Send Message to Contacts");
	else if (selection_is_contact_list)
		label = _("_Send Message to List");
	else
		label = _("_Send Message to Contact");
	g_object_set (action, "label", label, NULL);
}

static void
book_shell_view_class_init (EBookShellViewClass *class)
{
	GObjectClass *object_class;
	EShellViewClass *shell_view_class;

	parent_class = g_type_class_peek_parent (class);
	g_type_class_add_private (class, sizeof (EBookShellViewPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = book_shell_view_dispose;
	object_class->finalize = book_shell_view_finalize;
	object_class->constructed = book_shell_view_constructed;

	shell_view_class = E_SHELL_VIEW_CLASS (class);
	shell_view_class->label = _("Contacts");
	shell_view_class->icon_name = "x-office-address-book";
	shell_view_class->ui_definition = "evolution-contacts.ui";
	shell_view_class->ui_manager_id = "org.gnome.evolution.contacts";
	shell_view_class->search_options = "/contact-search-options";
	shell_view_class->search_rules = "addresstypes.xml";
	shell_view_class->new_shell_content = e_book_shell_content_new;
	shell_view_class->new_shell_sidebar = e_book_shell_sidebar_new;
	shell_view_class->execute_search = book_shell_view_execute_search;
	shell_view_class->update_actions = book_shell_view_update_actions;
}

static void
book_shell_view_init (EBookShellView *book_shell_view,
                      EShellViewClass *shell_view_class)
{
	book_shell_view->priv =
		E_BOOK_SHELL_VIEW_GET_PRIVATE (book_shell_view);

	e_book_shell_view_private_init (book_shell_view, shell_view_class);
}

GType
e_book_shell_view_get_type (void)
{
	return book_shell_view_type;
}

void
e_book_shell_view_register_type (GTypeModule *type_module)
{
	const GTypeInfo type_info = {
		sizeof (EBookShellViewClass),
		(GBaseInitFunc) NULL,
		(GBaseFinalizeFunc) NULL,
		(GClassInitFunc) book_shell_view_class_init,
		(GClassFinalizeFunc) NULL,
		NULL,  /* class_data */
		sizeof (EBookShellView),
		0,     /* n_preallocs */
		(GInstanceInitFunc) book_shell_view_init,
		NULL   /* value_table */
	};

	book_shell_view_type = g_type_module_register_type (
		type_module, E_TYPE_SHELL_VIEW,
		"EBookShellView", &type_info, 0);
}
