/* e-editor-actions.c
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

//#include "e-editor-private.h"

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gio/gio.h>
#include <glib/gi18n-lib.h>
#include <string.h>

#include "e-editor.h"
#include "e-editor-actions.h"
#include "e-editor-private.h"
#include "e-editor-widgets.h"
#include "e-emoticon-action.h"
#include "e-emoticon-chooser.h"
#include "e-image-chooser-dialog.h"

static void
insert_html_file_ready_cb (GFile *file,
                           GAsyncResult *result,
                           EEditor *editor)
{
	EEditorSelection *selection;
	gchar *contents = NULL;
	gsize length;
	GError *error = NULL;

	g_file_load_contents_finish (
		file, result, &contents, &length, NULL, &error);
	if (error != NULL) {
		GtkWidget *dialog;

		dialog = gtk_message_dialog_new (
			GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (editor))),
			0, GTK_MESSAGE_ERROR,
			GTK_BUTTONS_CLOSE, _("Failed to insert HTML file."));
		gtk_message_dialog_format_secondary_text (
			GTK_MESSAGE_DIALOG (dialog), "%s.", error->message);
		gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (dialog);

		g_clear_error (&error);
		g_object_unref (editor);
		return;
	}

	selection = e_editor_widget_get_selection (
			e_editor_get_editor_widget (editor));
	e_editor_selection_insert_html (selection, contents);
	g_free (contents);

	g_object_unref (editor);
}

static void
insert_text_file_ready_cb (GFile *file,
                           GAsyncResult *result,
                           EEditor *editor)
{
	EEditorSelection *selection;
	gchar *contents;
	gsize length;
	GError *error = NULL;

	g_file_load_contents_finish (
		file, result, &contents, &length, NULL, &error);
	if (error != NULL) {
		GtkWidget *dialog;

		dialog = gtk_message_dialog_new (
			GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (editor))),
			0, GTK_MESSAGE_ERROR,
			GTK_BUTTONS_CLOSE, _("Failed to insert text file."));
		gtk_message_dialog_format_secondary_text (
			GTK_MESSAGE_DIALOG (dialog), "%s.", error->message);
		gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (dialog);

		g_clear_error (&error);
		g_object_unref (editor);
		return;
	}

	selection = e_editor_widget_get_selection (
			e_editor_get_editor_widget (editor));
	e_editor_selection_insert_text (selection, contents);
	g_free (contents);

	g_object_unref (editor);
}

/*****************************************************************************
 * Action Callbacks
 *****************************************************************************/

static WebKitDOMNode *
find_parent_element_by_type (WebKitDOMNode *node, GType type)
{
	while (node) {

		if (g_type_is_a (type, webkit_dom_node_get_type ()))
			return node;

		node = webkit_dom_node_get_parent_node (node);
	}

	return NULL;
}

static void
action_context_delete_cell_cb (GtkAction *action,
                               EEditor *editor)
{
	WebKitDOMDocument *document;
	WebKitDOMDOMWindow *window;
	WebKitDOMDOMSelection *selection;
	WebKitDOMRange *range;
	WebKitDOMNode *start, *end, *cell;
	gboolean single_row;

	document = webkit_web_view_get_dom_document (
			WEBKIT_WEB_VIEW (e_editor_get_editor_widget (editor)));
	window = webkit_dom_document_get_default_view (document);
	selection = webkit_dom_dom_window_get_selection (window);
	if (webkit_dom_dom_selection_get_range_count (selection) < 1)
		return;

	range = webkit_dom_dom_selection_get_range_at (selection, 0, NULL);

	/* Find TD in which the selection starts */
	start = webkit_dom_range_get_start_container (range, NULL);
	if (!WEBKIT_DOM_IS_HTML_TABLE_CELL_ELEMENT (start)) {
		start = find_parent_element_by_type (
			start, WEBKIT_TYPE_DOM_HTML_TABLE_CELL_ELEMENT);
	}

	/* Find TD in which the selection ends */
	end = webkit_dom_range_get_end_container (range, NULL);
	if (!WEBKIT_DOM_IS_HTML_TABLE_CELL_ELEMENT (end)) {
		end = find_parent_element_by_type (
			end, WEBKIT_TYPE_DOM_HTML_TABLE_CELL_ELEMENT);
	}

	single_row = (webkit_dom_node_get_parent_node (start) ==
			webkit_dom_node_get_parent_node (end));

	cell = start;
	while (cell) {
		WebKitDOMNodeList *nodes;
		gulong length, i;

		/* Remove all child nodes in the cell */
		nodes = webkit_dom_node_get_child_nodes (cell);
		length = webkit_dom_node_list_get_length (nodes);
		for (i = 0; i < length; i++) {
			webkit_dom_node_remove_child (
				cell,
				webkit_dom_node_list_item (nodes, i),
				NULL);
		}

		if (cell == end)
			break;

		cell = webkit_dom_node_get_next_sibling (cell);

		if (!cell && !single_row) {
			cell = webkit_dom_node_get_first_child (
				webkit_dom_node_get_parent_node (cell));
		}
	}
}

static void
action_context_delete_column_cb (GtkAction *action,
                                 EEditor *editor)
{
	WebKitDOMDocument *document;
	WebKitDOMDOMWindow *window;
	WebKitDOMDOMSelection *selection;
	WebKitDOMRange *range;
	WebKitDOMNode *start, *end, *first_row;
	gulong index, count, i;

	document = webkit_web_view_get_dom_document (
			WEBKIT_WEB_VIEW (e_editor_get_editor_widget (editor)));
	window = webkit_dom_document_get_default_view (document);
	selection = webkit_dom_dom_window_get_selection (window);
	if (webkit_dom_dom_selection_get_range_count (selection) < 1) {
		return;
	}

	range = webkit_dom_dom_selection_get_range_at (selection, 0, NULL);

	/* Find TD in which the selection starts */
	start = webkit_dom_range_get_start_container (range, NULL);
	if (!WEBKIT_DOM_IS_HTML_TABLE_CELL_ELEMENT (start)) {
		start = find_parent_element_by_type (
			start, WEBKIT_TYPE_DOM_HTML_TABLE_CELL_ELEMENT);
	}

	/* Find TD in which the selection ends */
	end = webkit_dom_range_get_end_container (range, NULL);
	if (!WEBKIT_DOM_IS_HTML_TABLE_CELL_ELEMENT (end)) {
		end = find_parent_element_by_type (
			end, WEBKIT_TYPE_DOM_HTML_TABLE_CELL_ELEMENT);
	}

	first_row = find_parent_element_by_type (
		start, WEBKIT_TYPE_DOM_HTML_TABLE_ROW_ELEMENT);
	first_row = webkit_dom_node_get_first_child (
		webkit_dom_node_get_parent_node (first_row));

	index = webkit_dom_html_table_cell_element_get_cell_index (
			WEBKIT_DOM_HTML_TABLE_CELL_ELEMENT (start));
	count = webkit_dom_html_table_cell_element_get_cell_index (
			WEBKIT_DOM_HTML_TABLE_CELL_ELEMENT (end)) - index;

	for (i = 0; i < count; i++) {
		WebKitDOMNode *row = first_row;

		while (row) {
			webkit_dom_html_table_row_element_delete_cell (
				WEBKIT_DOM_HTML_TABLE_ROW_ELEMENT (row),
				index, NULL);

			row = webkit_dom_node_get_next_sibling (row);
		}
	}
}

static void
action_context_delete_row_cb (GtkAction *action,
                              EEditor *editor)
{
	WebKitDOMDocument *document;
	WebKitDOMDOMWindow *window;
	WebKitDOMDOMSelection *selection;
	WebKitDOMRange *range;
	WebKitDOMNode *start, *end, *table;
	gulong index, row_count, i;

	document = webkit_web_view_get_dom_document (
			WEBKIT_WEB_VIEW (e_editor_get_editor_widget (editor)));
	window = webkit_dom_document_get_default_view (document);
	selection = webkit_dom_dom_window_get_selection (window);
	if (webkit_dom_dom_selection_get_range_count (selection) < 1)
		return;

	range = webkit_dom_dom_selection_get_range_at (selection, 0, NULL);

	start = webkit_dom_range_get_start_container (range, NULL);
	if (!WEBKIT_DOM_IS_HTML_TABLE_ROW_ELEMENT (start)) {
		start = find_parent_element_by_type (
			start, WEBKIT_TYPE_DOM_HTML_TABLE_ROW_ELEMENT);
	}

	end = webkit_dom_range_get_end_container (range, NULL);
	if (!WEBKIT_DOM_IS_HTML_TABLE_ROW_ELEMENT (end)) {
		end = find_parent_element_by_type (
			end, WEBKIT_TYPE_DOM_HTML_TABLE_ROW_ELEMENT);
	}

	table = find_parent_element_by_type (
			start, WEBKIT_TYPE_DOM_HTML_TABLE_ELEMENT);

	index = webkit_dom_html_table_row_element_get_row_index (
			WEBKIT_DOM_HTML_TABLE_ROW_ELEMENT (start));
	row_count = webkit_dom_html_table_row_element_get_row_index (
			WEBKIT_DOM_HTML_TABLE_ROW_ELEMENT (end)) - index;

	for (i = 0; i < row_count; i++) {
		webkit_dom_html_table_element_delete_row (
			WEBKIT_DOM_HTML_TABLE_ELEMENT (table), index, NULL);
	}
}

static void
action_context_delete_table_cb (GtkAction *action,
    EEditor *editor)
{
	WebKitDOMDocument *document;
	WebKitDOMDOMWindow *window;
	WebKitDOMDOMSelection *selection;
	WebKitDOMRange *range;
	WebKitDOMNode *table;

	document = webkit_web_view_get_dom_document (
			WEBKIT_WEB_VIEW (e_editor_get_editor_widget (editor)));
	window = webkit_dom_document_get_default_view (document);
	selection = webkit_dom_dom_window_get_selection (window);
	if (webkit_dom_dom_selection_get_range_count (selection) < 1)
		return;

	range = webkit_dom_dom_selection_get_range_at (selection, 0, NULL);

	table = webkit_dom_range_get_start_container (range, NULL);
	if (!WEBKIT_DOM_IS_HTML_TABLE_ELEMENT (table)) {
		table = find_parent_element_by_type (
			table, WEBKIT_TYPE_DOM_HTML_TABLE_ELEMENT);
	}

	webkit_dom_node_remove_child (
		webkit_dom_node_get_parent_node (table), table, NULL);
}

static void
action_context_insert_column_after_cb (GtkAction *action,
                                       EEditor *editor)
{
	WebKitDOMDocument *document;
	WebKitDOMDOMWindow *window;
	WebKitDOMDOMSelection *selection;
	WebKitDOMRange *range;
	WebKitDOMNode *cell, *row;
	gulong index;

	document = webkit_web_view_get_dom_document (
			WEBKIT_WEB_VIEW (e_editor_get_editor_widget (editor)));
	window = webkit_dom_document_get_default_view (document);
	selection = webkit_dom_dom_window_get_selection (window);
	if (webkit_dom_dom_selection_get_range_count (selection) < 1)
		return;

	range = webkit_dom_dom_selection_get_range_at (selection, 0, NULL);

	cell = webkit_dom_range_get_end_container (range, NULL);
	if (!WEBKIT_DOM_IS_HTML_TABLE_CELL_ELEMENT (cell)) {
		cell = find_parent_element_by_type (
			cell, WEBKIT_TYPE_DOM_HTML_TABLE_CELL_ELEMENT);
	}

	row = find_parent_element_by_type (
		cell, WEBKIT_TYPE_DOM_HTML_TABLE_ROW_ELEMENT);
	/* Get the first row in the table */
	row = webkit_dom_node_get_first_child (
		webkit_dom_node_get_parent_node (row));

	index = webkit_dom_html_table_cell_element_get_cell_index (
			WEBKIT_DOM_HTML_TABLE_CELL_ELEMENT (cell));

	while (row) {
		webkit_dom_html_table_row_element_insert_cell (
			WEBKIT_DOM_HTML_TABLE_ROW_ELEMENT (row), index + 1, NULL);

		row = webkit_dom_node_get_next_sibling (row);
	}
}

static void
action_context_insert_column_before_cb (GtkAction *action,
                                        EEditor *editor)
{
	WebKitDOMDocument *document;
	WebKitDOMDOMWindow *window;
	WebKitDOMDOMSelection *selection;
	WebKitDOMRange *range;
	WebKitDOMNode *cell, *row;
	gulong index;

	document = webkit_web_view_get_dom_document (
			WEBKIT_WEB_VIEW (e_editor_get_editor_widget (editor)));
	window = webkit_dom_document_get_default_view (document);
	selection = webkit_dom_dom_window_get_selection (window);
	if (webkit_dom_dom_selection_get_range_count (selection) < 1)
		return;

	range = webkit_dom_dom_selection_get_range_at (selection, 0, NULL);

	cell = webkit_dom_range_get_start_container (range, NULL);
	if (!WEBKIT_DOM_IS_HTML_TABLE_CELL_ELEMENT (cell)) {
		cell = find_parent_element_by_type (
			cell, WEBKIT_TYPE_DOM_HTML_TABLE_CELL_ELEMENT);
	}

	row = find_parent_element_by_type (
		cell, WEBKIT_TYPE_DOM_HTML_TABLE_ROW_ELEMENT);
	/* Get the first row in the table */
	row = webkit_dom_node_get_first_child (
		webkit_dom_node_get_parent_node (row));

	index = webkit_dom_html_table_cell_element_get_cell_index (
			WEBKIT_DOM_HTML_TABLE_CELL_ELEMENT (cell));

	while (row) {
		webkit_dom_html_table_row_element_insert_cell (
			WEBKIT_DOM_HTML_TABLE_ROW_ELEMENT (row), index - 1, NULL);

		row = webkit_dom_node_get_next_sibling (row);
	}
}

static void
action_context_insert_row_above_cb (GtkAction *action,
                                    EEditor *editor)
{
	WebKitDOMDocument *document;
	WebKitDOMDOMWindow *window;
	WebKitDOMDOMSelection *selection;
	WebKitDOMRange *range;
	WebKitDOMNode *row, *table;
	gulong index;

	document = webkit_web_view_get_dom_document (
			WEBKIT_WEB_VIEW (e_editor_get_editor_widget (editor)));
	window = webkit_dom_document_get_default_view (document);
	selection = webkit_dom_dom_window_get_selection (window);
	if (webkit_dom_dom_selection_get_range_count (selection) < 1)
		return;

	range = webkit_dom_dom_selection_get_range_at (selection, 0, NULL);

	row = webkit_dom_range_get_start_container (range, NULL);
	if (!WEBKIT_DOM_IS_HTML_TABLE_CELL_ELEMENT (row)) {
		row = find_parent_element_by_type (
			row, WEBKIT_TYPE_DOM_HTML_TABLE_ROW_ELEMENT);
	}

	table = find_parent_element_by_type (
		row, WEBKIT_TYPE_DOM_HTML_TABLE_ELEMENT);

	index = webkit_dom_html_table_row_element_get_row_index (
			WEBKIT_DOM_HTML_TABLE_ROW_ELEMENT (row));

	webkit_dom_html_table_element_insert_row (
		WEBKIT_DOM_HTML_TABLE_ELEMENT (table), index - 1, NULL);
}

static void
action_context_insert_row_below_cb (GtkAction *action,
                                    EEditor *editor)
{
	WebKitDOMDocument *document;
	WebKitDOMDOMWindow *window;
	WebKitDOMDOMSelection *selection;
	WebKitDOMRange *range;
	WebKitDOMNode *row, *table;
	gulong index;

	document = webkit_web_view_get_dom_document (
			WEBKIT_WEB_VIEW (e_editor_get_editor_widget (editor)));
	window = webkit_dom_document_get_default_view (document);
	selection = webkit_dom_dom_window_get_selection (window);
	if (webkit_dom_dom_selection_get_range_count (selection) < 1)
		return;

	range = webkit_dom_dom_selection_get_range_at (selection, 0, NULL);

	row = webkit_dom_range_get_end_container (range, NULL);
	if (!WEBKIT_DOM_IS_HTML_TABLE_CELL_ELEMENT (row)) {
		row = find_parent_element_by_type (
			row, WEBKIT_TYPE_DOM_HTML_TABLE_ROW_ELEMENT);
	}

	table = find_parent_element_by_type (
		row, WEBKIT_TYPE_DOM_HTML_TABLE_ELEMENT);

	index = webkit_dom_html_table_row_element_get_row_index (
			WEBKIT_DOM_HTML_TABLE_ROW_ELEMENT (row));

	webkit_dom_html_table_element_insert_row (
		WEBKIT_DOM_HTML_TABLE_ELEMENT (table), index + 1, NULL);
}

static void
action_context_remove_link_cb (GtkAction *action,
                               EEditor *editor)
{
	WebKitDOMDocument *document;

	document = webkit_web_view_get_dom_document (
			WEBKIT_WEB_VIEW (e_editor_get_editor_widget (editor)));
	webkit_dom_document_exec_command (document, "unlink", FALSE, "");
}

static void
action_context_spell_add_cb (GtkAction *action,
                             EEditor *editor)
{
	WebKitDOMDocument *document;
	WebKitDOMDOMWindow *window;
	WebKitDOMDOMSelection *selection;
	WebKitDOMRange *range;
	WebKitSpellChecker *spell_checker;
	gchar *word;

	document = webkit_web_view_get_dom_document (
			WEBKIT_WEB_VIEW (e_editor_get_editor_widget (editor)));
	window = webkit_dom_document_get_default_view (document);
	selection = webkit_dom_dom_window_get_selection (window);
	if (webkit_dom_dom_selection_get_range_count (selection) < 1)
		return;

	range = webkit_dom_dom_selection_get_range_at (selection, 0, NULL);
	word = webkit_dom_range_get_text (range);

	spell_checker = WEBKIT_SPELL_CHECKER (webkit_get_text_checker ());
	webkit_spell_checker_learn_word (spell_checker, word);

	g_free (word);
}

static void
action_context_spell_ignore_cb (GtkAction *action,
                                EEditor *editor)
{
	WebKitDOMDocument *document;
	WebKitDOMDOMWindow *window;
	WebKitDOMDOMSelection *selection;
	WebKitDOMRange *range;
	WebKitSpellChecker *spell_checker;
	gchar *word;

	document = webkit_web_view_get_dom_document (
			WEBKIT_WEB_VIEW (e_editor_get_editor_widget (editor)));
	window = webkit_dom_document_get_default_view (document);
	selection = webkit_dom_dom_window_get_selection (window);
	if (webkit_dom_dom_selection_get_range_count (selection) < 1)
		return;

	range = webkit_dom_dom_selection_get_range_at (selection, 0, NULL);
	word = webkit_dom_range_get_text (range);

	spell_checker = WEBKIT_SPELL_CHECKER (webkit_get_text_checker ());
	webkit_spell_checker_ignore_word (spell_checker, word);

	g_free (word);
}

static void
action_copy_cb (GtkAction *action,
                EEditor *editor)
{
	webkit_web_view_copy_clipboard (
		WEBKIT_WEB_VIEW (e_editor_get_editor_widget (editor)));
}

static void
action_cut_cb (GtkAction *action,
               EEditor *editor)
{
	webkit_web_view_cut_clipboard (
		WEBKIT_WEB_VIEW (e_editor_get_editor_widget (editor)));
}

static void
action_indent_cb (GtkAction *action,
                  EEditor *editor)
{
	e_editor_selection_indent (editor->priv->selection);
}

static void
action_insert_emoticon_cb (GtkAction *action,
			   EEditor *editor)
{
	EEditorWidget *widget;
	EEditorSelection *selection;
	EEmoticonChooser *chooser;
	EEmoticon *emoticon;
	gchar *uri = NULL;

	chooser = E_EMOTICON_CHOOSER (action);
	emoticon = e_emoticon_chooser_get_current_emoticon (chooser);
	g_return_if_fail (emoticon != NULL);

	uri = e_emoticon_get_uri (emoticon);
	widget = e_editor_get_editor_widget (editor);
	selection = e_editor_widget_get_selection (widget);
	e_editor_selection_insert_image (selection, uri);

	g_free (uri);
}

static void
action_insert_html_file_cb (GtkToggleAction *action,
                            EEditor *editor)
{
	GtkWidget *dialog;
	GtkFileFilter *filter;

	dialog = gtk_file_chooser_dialog_new (
			_("Insert HTML File"), NULL,
			GTK_FILE_CHOOSER_ACTION_OPEN,
			GTK_STOCK_OK, GTK_RESPONSE_ACCEPT,
			GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL, NULL);

	filter = gtk_file_filter_new ();
	gtk_file_filter_set_name (filter, _("HTML file"));
	gtk_file_filter_add_mime_type (filter, "text/html");
	gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter);

	if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT) {
		GFile *file = gtk_file_chooser_get_file (
					GTK_FILE_CHOOSER (dialog));

		/* XXX Need a way to cancel this. */
		g_file_load_contents_async (
			file, NULL, (GAsyncReadyCallback)
			insert_html_file_ready_cb,
			g_object_ref (editor));

		g_object_unref (file);
	}

	gtk_widget_destroy (dialog);
}

static void
action_insert_image_cb (GtkAction *action,
                        EEditor *editor)
{
	GtkWidget *dialog;

	dialog = e_image_chooser_dialog_new (
			_("Insert Image"), NULL);

	if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT) {
		gchar *uri;
		EEditorSelection *selection;

		 uri = gtk_file_chooser_get_uri (
				GTK_FILE_CHOOSER (dialog));

		 selection = e_editor_widget_get_selection (
				e_editor_get_editor_widget (editor));
		 e_editor_selection_insert_image (selection, uri);
		 g_free (uri);
	}

	gtk_widget_destroy (dialog);
}

static void
action_insert_link_cb (GtkAction *action,
                       EEditor *editor)
{
	if (editor->priv->link_dialog == NULL) {
		editor->priv->link_dialog =
			e_editor_link_dialog_new (editor);
	}

	gtk_window_present (GTK_WINDOW (editor->priv->link_dialog));
}

static void
action_insert_rule_cb (GtkAction *action,
                       EEditor *editor)
{
	if (editor->priv->hrule_dialog == NULL) {
		editor->priv->hrule_dialog =
			e_editor_hrule_dialog_new (editor);
	}

	gtk_window_present (GTK_WINDOW (editor->priv->hrule_dialog));
}

static void
action_insert_table_cb (GtkAction *action,
                        EEditor *editor)
{
	if (editor->priv->table_dialog == NULL) {
		editor->priv->table_dialog =
			e_editor_table_dialog_new (editor);
	}

	gtk_window_present (GTK_WINDOW (editor->priv->table_dialog));
}

static void
action_insert_text_file_cb (GtkAction *action,
                            EEditor *editor)
{
	GtkWidget *dialog;
	GtkFileFilter *filter;

	dialog = gtk_file_chooser_dialog_new (
			_("Insert text file"), NULL,
			GTK_FILE_CHOOSER_ACTION_OPEN,
			GTK_STOCK_OK, GTK_RESPONSE_ACCEPT,
			GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL, NULL);

	filter = gtk_file_filter_new ();
	gtk_file_filter_set_name (filter, _("Text file"));
	gtk_file_filter_add_mime_type (filter, "text/plain");
	gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter);

	if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT) {
		GFile *file = gtk_file_chooser_get_file (
					GTK_FILE_CHOOSER (dialog));

		/* XXX Need a way to cancel this. */
		g_file_load_contents_async (
			file, NULL, (GAsyncReadyCallback)
			insert_text_file_ready_cb,
			g_object_ref (editor));

		g_object_unref (file);
	}

	gtk_widget_destroy (dialog);
}

static void
action_language_cb (GtkToggleAction *action,
                    EEditor *editor)
{
	/* FIXME WEBKIT */
	/*
	const GtkhtmlSpellLanguage *language;
	GtkhtmlSpellChecker *checker;
	const gchar *language_code;
	GtkAction *add_action;
	GtkHTML *html;
	GList *list;
	guint length;
	gchar *action_name;
	gboolean active;

	active = gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (action));
	language_code = gtk_action_get_name (GTK_ACTION (action));
	language = gtkhtml_spell_language_lookup (language_code);

	checker = g_hash_table_lookup (
		editor->priv->available_spell_checkers, language);
	g_return_if_fail (checker != NULL);

	//Update the list of active spell checkers.
	list = editor->priv->active_spell_checkers;
	if (active)
		list = g_list_insert_sorted (
			list, g_object_ref (checker),
			(GCompareFunc) gtkhtml_spell_checker_compare);
	else {
		GList *link;

		link = g_list_find (list, checker);
		g_return_if_fail (link != NULL);
		list = g_list_delete_link (list, link);
		g_object_unref (checker);
	}
	editor->priv->active_spell_checkers = list;
	length = g_list_length (list);

	// Update "Add Word To" context menu item visibility.
	action_name = g_strdup_printf ("context-spell-add-%s", language_code);
	add_action = gtkhtml_editor_get_action (editor, action_name);
	gtk_action_set_visible (add_action, active);
	g_free (action_name);

	gtk_action_set_visible (ACTION (CONTEXT_SPELL_ADD), length == 1);
	gtk_action_set_visible (ACTION (CONTEXT_SPELL_ADD_MENU), length > 1);
	gtk_action_set_visible (ACTION (CONTEXT_SPELL_IGNORE), length > 0);

	gtk_action_set_sensitive (ACTION (SPELL_CHECK), length > 0);

	html = gtkhtml_editor_get_html (editor);
	html_engine_spell_check (html->engine);

	gtkthtml_editor_emit_spell_languages_changed (editor);
	*/
}

struct _ModeChanged {
	GtkRadioAction *action;
	EEditor *editor;
};

static gboolean
mode_changed (struct _ModeChanged *data)
{
	GtkActionGroup *action_group;
	EEditor *editor = data->editor;
	EEditorWidget *widget;
	EEditorWidgetMode mode;
	gboolean is_html;

	widget = e_editor_get_editor_widget (editor);
	mode = gtk_radio_action_get_current_value (data->action);
	is_html = (mode == E_EDITOR_WIDGET_MODE_HTML);

	if (mode == e_editor_widget_get_mode (widget)) {
		goto exit;
	}

	/* If switching from HTML to plain text */
	if (!is_html) {
		GtkWidget *dialog, *parent;

		parent = gtk_widget_get_toplevel (GTK_WIDGET (editor));
		if (!GTK_IS_WINDOW (parent)) {
			parent = NULL;
		}

		dialog = gtk_message_dialog_new (
				parent ? GTK_WINDOW (parent) : NULL,
				GTK_DIALOG_DESTROY_WITH_PARENT,
				GTK_MESSAGE_WARNING,
				GTK_BUTTONS_OK_CANCEL,
				_("Turning HTML mode off will cause the text "
				  "to loose all formatting. Do you want to continue?"));
		if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_CANCEL) {
			gtk_radio_action_set_current_value (
				data->action, E_EDITOR_WIDGET_MODE_HTML);
			gtk_widget_destroy (dialog);
			goto exit;
		}

		gtk_widget_destroy (dialog);
	}

	action_group = editor->priv->html_actions;
	gtk_action_group_set_sensitive (action_group, is_html);

	action_group = editor->priv->html_context_actions;
	gtk_action_group_set_visible (action_group, is_html);

	gtk_widget_set_sensitive (editor->priv->color_combo_box, is_html);

	if (is_html) {
		gtk_widget_show (editor->priv->html_toolbar);
	} else {
		gtk_widget_hide (editor->priv->html_toolbar);
	}

	/* Certain paragraph styles are HTML-only. */
	gtk_action_set_sensitive (ACTION (STYLE_H1), is_html);
	gtk_action_set_sensitive (ACTION (STYLE_H2), is_html);
	gtk_action_set_sensitive (ACTION (STYLE_H3), is_html);
	gtk_action_set_sensitive (ACTION (STYLE_H4), is_html);
	gtk_action_set_sensitive (ACTION (STYLE_H5), is_html);
	gtk_action_set_sensitive (ACTION (STYLE_H6), is_html);
	gtk_action_set_sensitive (ACTION (STYLE_ADDRESS), is_html);

	e_editor_widget_set_mode (
		e_editor_get_editor_widget (editor), mode);

 exit:
	g_clear_object (&data->editor);
	g_clear_object (&data->action);
	g_free (data);

	return FALSE;
}

static void
action_mode_cb (GtkRadioAction *action,
                GtkRadioAction *current,
                EEditor *editor)
{
	struct _ModeChanged *data;

	data = g_new0 (struct _ModeChanged, 1);
	data->action = g_object_ref (current);
	data->editor = g_object_ref (editor);

	/* We can't change group current value from this callback, so
	 * let's do it all from an idle callback */
	g_idle_add ((GSourceFunc) mode_changed, data);
}

static void
action_paste_cb (GtkAction *action,
                 EEditor *editor)
{
	webkit_web_view_paste_clipboard (
		WEBKIT_WEB_VIEW (e_editor_get_editor_widget (editor)));
}

static void
action_paste_quote_cb (GtkAction *action,
                       EEditor *editor)
{
	e_editor_widget_paste_clipboard_quoted (
		e_editor_get_editor_widget (editor));
}

static void
action_properties_cell_cb (GtkAction *action,
                           EEditor *editor)
{
	gtk_window_present (GTK_WINDOW (WIDGET (CELL_PROPERTIES_WINDOW)));
}

static void
action_properties_image_cb (GtkAction *action,
                            EEditor *editor)
{
	gtk_window_present (GTK_WINDOW (WIDGET (IMAGE_PROPERTIES_WINDOW)));
}

static void
action_properties_link_cb (GtkAction *action,
                           EEditor *editor)
{
	if (editor->priv->link_dialog == NULL) {
		editor->priv->link_dialog =
			e_editor_link_dialog_new (editor);
	}

	gtk_window_present (GTK_WINDOW (editor->priv->link_dialog));
}

static void
action_properties_page_cb (GtkAction *action,
                           EEditor *editor)
{
	gtk_window_present (GTK_WINDOW (WIDGET (PAGE_PROPERTIES_WINDOW)));
}

static void
action_properties_paragraph_cb (GtkAction *action,
                                EEditor *editor)
{
	gtk_window_present (GTK_WINDOW (WIDGET (PARAGRAPH_PROPERTIES_WINDOW)));
}

static void
action_properties_rule_cb (GtkAction *action,
                           EEditor *editor)
{
	if (editor->priv->hrule_dialog == NULL) {
		editor->priv->hrule_dialog =
			e_editor_hrule_dialog_new (editor);
	}

	gtk_window_present (GTK_WINDOW (editor->priv->hrule_dialog));
}

static void
action_properties_table_cb (GtkAction *action,
                            EEditor *editor)
{
	if (editor->priv->table_dialog == NULL) {
		editor->priv->table_dialog =
			e_editor_table_dialog_new (editor);
	}

	gtk_window_present (GTK_WINDOW (editor->priv->table_dialog));
}

static void
action_properties_text_cb (GtkAction *action,
                           EEditor *editor)
{
	gtk_window_present (GTK_WINDOW (WIDGET (TEXT_PROPERTIES_WINDOW)));
}

static void
action_style_cb (GtkRadioAction *action,
                 GtkRadioAction *current,
                 EEditor *editor)
{
	EEditorSelection *selection;

	selection = e_editor_widget_get_selection (
			e_editor_get_editor_widget (editor));
	e_editor_selection_set_block_format (
		selection, gtk_radio_action_get_current_value (current));
}

static void
action_redo_cb (GtkAction *action,
                EEditor *editor)
{
	webkit_web_view_redo (
		WEBKIT_WEB_VIEW (e_editor_get_editor_widget (editor)));
}

static void
action_select_all_cb (GtkAction *action,
                      EEditor *editor)
{
	webkit_web_view_select_all (
		WEBKIT_WEB_VIEW (e_editor_get_editor_widget (editor)));
}

static void
action_show_find_cb (GtkAction *action,
                     EEditor *editor)
{
	if (editor->priv->find_dialog == NULL) {
		editor->priv->find_dialog = e_editor_find_dialog_new (editor);
		gtk_action_set_sensitive (ACTION (FIND_AGAIN), TRUE);
	}

	gtk_window_present (GTK_WINDOW (editor->priv->find_dialog));
}

static void
action_find_again_cb (GtkAction *action,
                      EEditor *editor)
{
	if (editor->priv->find_dialog == NULL) {
		return;
	}

	e_editor_find_dialog_find_next (
		E_EDITOR_FIND_DIALOG (editor->priv->find_dialog));
}

static void
action_show_replace_cb (GtkAction *action,
                        EEditor *editor)
{
	if (editor->priv->replace_dialog == NULL) {
		editor->priv->replace_dialog =
			e_editor_replace_dialog_new (editor);
	}

	gtk_window_present (GTK_WINDOW (editor->priv->replace_dialog));
}

static void
action_spell_check_cb (GtkAction *action,
                       EEditor *editor)
{
	/* FIXME WEBKIT
	e_editor_widget_spell_check (editor);
	*/
}

static void
action_undo_cb (GtkAction *action,
                EEditor *editor)
{
	webkit_web_view_undo (
		WEBKIT_WEB_VIEW (e_editor_get_editor_widget (editor)));
}

static void
action_unindent_cb (GtkAction *action,
                    EEditor *editor)
{
	e_editor_selection_unindent (editor->priv->selection);
}

static void
action_wrap_lines_cb (GtkAction *action,
                      EEditor *editor)
{
	e_editor_selection_wrap_lines (editor->priv->selection);
}

/*****************************************************************************
 * Core Actions
 *
 * These actions are always enabled.
 *****************************************************************************/

static GtkActionEntry core_entries[] = {

	{ "copy",
	  GTK_STOCK_COPY,
	  N_("_Copy"),
	  "<Control>c",
	  NULL,
	  G_CALLBACK (action_copy_cb) },

	{ "cut",
	  GTK_STOCK_CUT,
	  N_("Cu_t"),
	  "<Control>x",
	  NULL,
	  G_CALLBACK (action_cut_cb) },

	{ "indent",
	  GTK_STOCK_INDENT,
	  N_("_Increase Indent"),
	  "<Control>bracketright",
	  N_("Increase Indent"),
	  G_CALLBACK (action_indent_cb) },

	{ "insert-html-file",
	  NULL,
	  N_("_HTML File..."),
	  NULL,
	  NULL,
	  G_CALLBACK (action_insert_html_file_cb) },

	{ "insert-text-file",
	  NULL,
	  N_("Te_xt File..."),
	  NULL,
	  NULL,
	  G_CALLBACK (action_insert_text_file_cb) },

	{ "paste",
	  GTK_STOCK_PASTE,
	  N_("_Paste"),
	  "<Control>v",
	  NULL,
	  G_CALLBACK (action_paste_cb) },

	{ "paste-quote",
	  NULL,
	  N_("Paste _Quotation"),
	  "<Shift><Control>v",
	  NULL,
	  G_CALLBACK (action_paste_quote_cb) },

	{ "redo",
	  GTK_STOCK_REDO,
	  N_("_Redo"),
	  "<Shift><Control>z",
	  NULL,
	  G_CALLBACK (action_redo_cb) },

	{ "select-all",
	  GTK_STOCK_SELECT_ALL,
	  N_("Select _All"),
	  "<Control>a",
	  NULL,
	  G_CALLBACK (action_select_all_cb) },

	{ "show-find",
	  GTK_STOCK_FIND,
	  N_("_Find..."),
	  "<Control>f",
	  NULL,
	  G_CALLBACK (action_show_find_cb) },

	{ "find-again",
	  NULL,
	  N_("Find A_gain"),
	  "<Control>g",
	  NULL,
	  G_CALLBACK (action_find_again_cb) },

	{ "show-replace",
	  GTK_STOCK_FIND_AND_REPLACE,
	  N_("Re_place..."),
	  "<Control>h",
	  NULL,
	  G_CALLBACK (action_show_replace_cb) },

	{ "spell-check",
	  GTK_STOCK_SPELL_CHECK,
	  N_("Check _Spelling..."),
	  "F7",
	  NULL,
	  G_CALLBACK (action_spell_check_cb) },

	{ "undo",
	  GTK_STOCK_UNDO,
	  N_("_Undo"),
	  "<Control>z",
	  NULL,
	  G_CALLBACK (action_undo_cb) },

	{ "unindent",
	  GTK_STOCK_UNINDENT,
	  N_("_Decrease Indent"),
	  "<Control>bracketleft",
	  N_("Decrease Indent"),
	  G_CALLBACK (action_unindent_cb) },

	{ "wrap-lines",
	  NULL,
	  N_("_Wrap Lines"),
	  "<Control>k",
	  NULL,
	  G_CALLBACK (action_wrap_lines_cb) },

	/* Menus */

	{ "edit-menu",
	  NULL,
	  N_("_Edit"),
	  NULL,
	  NULL,
	  NULL },

	{ "file-menu",
	  NULL,
	  N_("_File"),
	  NULL,
	  NULL,
	  NULL },

	{ "format-menu",
	  NULL,
	  N_("For_mat"),
	  NULL,
	  NULL,
	  NULL },

	{ "paragraph-style-menu",
	  NULL,
	  N_("_Paragraph Style"),
	  NULL,
	  NULL,
	  NULL },

	{ "insert-menu",
	  NULL,
	  N_("_Insert"),
	  NULL,
	  NULL,
	  NULL },

	{ "justify-menu",
	  NULL,
	  N_("_Alignment"),
	  NULL,
	  NULL,
	  NULL },

	{ "language-menu",
	  NULL,
	  N_("Current _Languages"),
	  NULL,
	  NULL,
	  NULL },

	{ "view-menu",
	  NULL,
	  N_("_View"),
	  NULL,
	  NULL,
	  NULL }
};

static GtkRadioActionEntry core_justify_entries[] = {

	{ "justify-center",
	  GTK_STOCK_JUSTIFY_CENTER,
	  N_("_Center"),
	  "<Control>e",
	  N_("Center Alignment"),
	  E_EDITOR_SELECTION_ALIGNMENT_CENTER },

	{ "justify-left",
	  GTK_STOCK_JUSTIFY_LEFT,
	  N_("_Left"),
	  "<Control>l",
	  N_("Left Alignment"),
	  E_EDITOR_SELECTION_ALIGNMENT_LEFT },

	{ "justify-right",
	  GTK_STOCK_JUSTIFY_RIGHT,
	  N_("_Right"),
	  "<Control>r",
	  N_("Right Alignment"),
	  E_EDITOR_SELECTION_ALIGNMENT_RIGHT }
};

static GtkRadioActionEntry core_mode_entries[] = {

	{ "mode-html",
	  NULL,
	  N_("_HTML"),
	  NULL,
	  N_("HTML editing mode"),
	  E_EDITOR_WIDGET_MODE_HTML },

	{ "mode-plain",
	  NULL,
	  N_("Plain _Text"),
	  NULL,
	  N_("Plain text editing mode"),
	  E_EDITOR_WIDGET_MODE_PLAIN_TEXT }
};

static GtkRadioActionEntry core_style_entries[] = {

	{ "style-normal",
	  NULL,
	  N_("_Normal"),
	  "<Control>0",
	  NULL,
	  E_EDITOR_SELECTION_BLOCK_FORMAT_PARAGRAPH },

	{ "style-h1",
	  NULL,
	  N_("Header _1"),
	  "<Control>1",
	  NULL,
	  E_EDITOR_SELECTION_BLOCK_FORMAT_H1 },

	{ "style-h2",
	  NULL,
	  N_("Header _2"),
	  "<Control>2",
	  NULL,
	  E_EDITOR_SELECTION_BLOCK_FORMAT_H2 },

	{ "style-h3",
	  NULL,
	  N_("Header _3"),
	  "<Control>3",
	  NULL,
	  E_EDITOR_SELECTION_BLOCK_FORMAT_H3 },

	{ "style-h4",
	  NULL,
	  N_("Header _4"),
	  "<Control>4",
	  NULL,
	  E_EDITOR_SELECTION_BLOCK_FORMAT_H4 },

	{ "style-h5",
	  NULL,
	  N_("Header _5"),
	  "<Control>5",
	  NULL,
	  E_EDITOR_SELECTION_BLOCK_FORMAT_H5 },

	{ "style-h6",
	  NULL,
	  N_("Header _6"),
	  "<Control>6",
	  NULL,
	  E_EDITOR_SELECTION_BLOCK_FORMAT_H6 },

	{ "style-address",
	  NULL,
	  N_("A_ddress"),
	  "<Control>8",
	  NULL,
	  E_EDITOR_SELECTION_BLOCK_FORMAT_ADDRESS },

	{ "style-preformat",
	  NULL,
	  N_("_Preformatted"),
	  "<Control>7",
	  NULL,
	  E_EDITOR_SELECTION_BLOCK_FORMAT_PRE },

	{ "style-list-bullet",
	  NULL,
	  N_("_Bulleted List"),
	  NULL,
	  NULL,
	  E_EDITOR_SELECTION_BLOCK_FORMAT_UNORDERED_LIST },

	{ "style-list-roman",
	  NULL,
	  N_("_Roman Numeral List"),
	  NULL,
	  NULL,
	  E_EDITOR_SELECTION_BLOCK_FORMAT_ORDERED_LIST_ROMAN },

	{ "style-list-number",
	  NULL,
	  N_("Numbered _List"),
	  NULL,
	  NULL,
	  E_EDITOR_SELECTION_BLOCK_FORMAT_ORDERED_LIST },

	{ "style-list-alpha",
	  NULL,
	  N_("_Alphabetical List"),
	  NULL,
	  NULL,
	  E_EDITOR_SELECTION_BLOCK_FORMAT_ORDERED_LIST_ALPHA }
};

/*****************************************************************************
 * Core Actions (HTML only)
 *
 * These actions are only enabled in HTML mode.
 *****************************************************************************/

static GtkActionEntry html_entries[] = {

	{ "insert-image",
	  "insert-image",
	  N_("_Image..."),
	  NULL,
	  N_("Insert Image"),
	  G_CALLBACK (action_insert_image_cb) },

	{ "insert-link",
	  "insert-link",
	  N_("_Link..."),
	  NULL,
	  N_("Insert Link"),
	  G_CALLBACK (action_insert_link_cb) },

	{ "insert-rule",
	  "stock_insert-rule",
	  /* Translators: 'Rule' here means a horizontal line in an HTML text */
	  N_("_Rule..."),
	  NULL,
	  /* Translators: 'Rule' here means a horizontal line in an HTML text */
	  N_("Insert Rule"),
	  G_CALLBACK (action_insert_rule_cb) },

	{ "insert-table",
	  "stock_insert-table",
	  N_("_Table..."),
	  NULL,
	  N_("Insert Table"),
	  G_CALLBACK (action_insert_table_cb) },

	{ "properties-cell",
	  NULL,
	  N_("_Cell..."),
	  NULL,
	  NULL,
	  G_CALLBACK (action_properties_cell_cb) },

	{ "properties-image",
	  NULL,
	  N_("_Image..."),
	  NULL,
	  NULL,
	  G_CALLBACK (action_properties_image_cb) },

	{ "properties-link",
	  NULL,
	  N_("_Link..."),
	  NULL,
	  NULL,
	  G_CALLBACK (action_properties_link_cb) },

	{ "properties-page",
	  NULL,
	  N_("Pa_ge..."),
	  NULL,
	  NULL,
	  G_CALLBACK (action_properties_page_cb) },

	{ "properties-rule",
	  NULL,
	  /* Translators: 'Rule' here means a horizontal line in an HTML text */
	  N_("_Rule..."),
	  NULL,
	  NULL,
	  G_CALLBACK (action_properties_rule_cb) },

	{ "properties-table",
	  NULL,
	  N_("_Table..."),
	  NULL,
	  NULL,
	  G_CALLBACK (action_properties_table_cb) },

	/* Menus */

	{ "font-size-menu",
	  NULL,
	  N_("Font _Size"),
	  NULL,
	  NULL,
	  NULL },

	{ "font-style-menu",
	  NULL,
	  N_("_Font Style"),
	  NULL,
	  NULL,
	  NULL },
};

static GtkToggleActionEntry html_toggle_entries[] = {

	{ "bold",
	  GTK_STOCK_BOLD,
	  N_("_Bold"),
	  "<Control>b",
	  N_("Bold"),
	  NULL,
	  FALSE },

	{ "italic",
	  GTK_STOCK_ITALIC,
	  N_("_Italic"),
	  "<Control>i",
	  N_("Italic"),
	  NULL,
	  FALSE },

	{ "monospaced",
	  "stock_text-monospaced",
	  N_("_Plain Text"),
	  "<Control>t",
	  N_("Plain Text"),
	  NULL,
	  FALSE },

	{ "strikethrough",
	  GTK_STOCK_STRIKETHROUGH,
	  N_("_Strikethrough"),
	  NULL,
	  N_("Strikethrough"),
	  NULL,
	  FALSE },

	{ "underline",
	  GTK_STOCK_UNDERLINE,
	  N_("_Underline"),
	  "<Control>u",
	  N_("Underline"),
	  NULL,
	  FALSE }
};

static GtkRadioActionEntry html_size_entries[] = {

	{ "size-minus-two",
	  NULL,
	  /* Translators: This is a font size level. It is shown on a tool bar. Please keep it as short as possible. */
	  N_("-2"),
	  NULL,
	  NULL,
	  E_EDITOR_SELECTION_FONT_SIZE_TINY },

	{ "size-minus-one",
	  NULL,
	  /* Translators: This is a font size level. It is shown on a tool bar. Please keep it as short as possible. */
	  N_("-1"),
	  NULL,
	  NULL,
	  E_EDITOR_SELECTION_FONT_SIZE_SMALL },

	{ "size-plus-zero",
	  NULL,
	  /* Translators: This is a font size level. It is shown on a tool bar. Please keep it as short as possible. */
	  N_("+0"),
	  NULL,
	  NULL,
	  E_EDITOR_SELECTION_FONT_SIZE_NORMAL },

	{ "size-plus-one",
	  NULL,
	  /* Translators: This is a font size level. It is shown on a tool bar. Please keep it as short as possible. */
	  N_("+1"),
	  NULL,
	  NULL,
	  E_EDITOR_SELECTION_FONT_SIZE_BIG },

	{ "size-plus-two",
	  NULL,
	  /* Translators: This is a font size level. It is shown on a tool bar. Please keep it as short as possible. */
	  N_("+2"),
	  NULL,
	  NULL,
	  E_EDITOR_SELECTION_FONT_SIZE_BIGGER },

	{ "size-plus-three",
	  NULL,
	  /* Translators: This is a font size level. It is shown on a tool bar. Please keep it as short as possible. */
	  N_("+3"),
	  NULL,
	  NULL,
	  E_EDITOR_SELECTION_FONT_SIZE_LARGE },

	{ "size-plus-four",
	  NULL,
	  /* Translators: This is a font size level. It is shown on a tool bar. Please keep it as short as possible. */
	  N_("+4"),
	  NULL,
	  NULL,
	  E_EDITOR_SELECTION_FONT_SIZE_VERY_LARGE }
};

/*****************************************************************************
 * Context Menu Actions
 *
 * These require separate action entries so we can toggle their visiblity
 * rather than their sensitivity as we do with main menu / toolbar actions.
 * Note that some of these actions use the same callback function as their
 * non-context sensitive counterparts.
 *****************************************************************************/

static GtkActionEntry context_entries[] = {

	{ "context-delete-cell",
	  NULL,
	  N_("Cell Contents"),
	  NULL,
	  NULL,
	  G_CALLBACK (action_context_delete_cell_cb) },

	{ "context-delete-column",
	  NULL,
	  N_("Column"),
	  NULL,
	  NULL,
	  G_CALLBACK (action_context_delete_column_cb) },

	{ "context-delete-row",
	  NULL,
	  N_("Row"),
	  NULL,
	  NULL,
	  G_CALLBACK (action_context_delete_row_cb) },

	{ "context-delete-table",
	  NULL,
	  N_("Table"),
	  NULL,
	  NULL,
	  G_CALLBACK (action_context_delete_table_cb) },

	/* Menus */

	{ "context-delete-table-menu",
	  NULL,
	  /* Translators: Popup menu item caption, containing all the Delete options for a table */
	  N_("Table Delete"),
	  NULL,
	  NULL,
	  NULL },

	{ "context-input-methods-menu",
	  NULL,
	  N_("Input Methods"),
	  NULL,
	  NULL,
	  NULL },

	{ "context-insert-table-menu",
	  NULL,
	  /* Translators: Popup menu item caption, containing all the Insert options for a table */
	  N_("Table Insert"),
	  NULL,
	  NULL,
	  NULL },

	{ "context-properties-menu",
	  NULL,
	  N_("Properties"),
	  NULL,
	  NULL,
	  NULL },
};

/*****************************************************************************
 * Context Menu Actions (HTML only)
 *
 * These actions are never visible in plain-text mode.  Note that some
 * of them use the same callback function as their non-context sensitive
 * counterparts.
 *****************************************************************************/

static GtkActionEntry html_context_entries[] = {

	{ "context-insert-column-after",
	  NULL,
	  N_("Column After"),
	  NULL,
	  NULL,
	  G_CALLBACK (action_context_insert_column_after_cb) },

	{ "context-insert-column-before",
	  NULL,
	  N_("Column Before"),
	  NULL,
	  NULL,
	  G_CALLBACK (action_context_insert_column_before_cb) },

	{ "context-insert-link",
	  NULL,
	  N_("Insert _Link"),
	  NULL,
	  NULL,
	  G_CALLBACK (action_insert_link_cb) },

	{ "context-insert-row-above",
	  NULL,
	  N_("Row Above"),
	  NULL,
	  NULL,
	  G_CALLBACK (action_context_insert_row_above_cb) },

	{ "context-insert-row-below",
	  NULL,
	  N_("Row Below"),
	  NULL,
	  NULL,
	  G_CALLBACK (action_context_insert_row_below_cb) },

	{ "context-insert-table",
	  NULL,
	  N_("Table"),
	  NULL,
	  NULL,
	  G_CALLBACK (action_insert_table_cb) },

	{ "context-properties-cell",
	  NULL,
	  N_("Cell..."),
	  NULL,
	  NULL,
	  G_CALLBACK (action_properties_cell_cb) },

	{ "context-properties-image",
	  NULL,
	  N_("Image..."),
	  NULL,
	  NULL,
	  G_CALLBACK (action_properties_image_cb) },

	{ "context-properties-link",
	  NULL,
	  N_("Link..."),
	  NULL,
	  NULL,
	  G_CALLBACK (action_properties_link_cb) },

	{ "context-properties-page",
	  NULL,
	  N_("Page..."),
	  NULL,
	  NULL,
	  G_CALLBACK (action_properties_page_cb) },

	{ "context-properties-paragraph",
	  NULL,
	  N_("Paragraph..."),
	  NULL,
	  NULL,
	  G_CALLBACK (action_properties_paragraph_cb) },

	{ "context-properties-rule",
	  NULL,
	  /* Translators: 'Rule' here means a horizontal line in an HTML text */
	  N_("Rule..."),
	  NULL,
	  NULL,
	  G_CALLBACK (action_properties_rule_cb) },

	{ "context-properties-table",
	  NULL,
	  N_("Table..."),
	  NULL,
	  NULL,
	  G_CALLBACK (action_properties_table_cb) },

	{ "context-properties-text",
	  NULL,
	  N_("Text..."),
	  NULL,
	  NULL,
	  G_CALLBACK (action_properties_text_cb) },

	{ "context-remove-link",
	  NULL,
	  N_("Remove Link"),
	  NULL,
	  NULL,
	  G_CALLBACK (action_context_remove_link_cb) }
};

/*****************************************************************************
 * Context Menu Actions (spell checking only)
 *
 * These actions are only visible when the word underneath the cursor is
 * misspelled.
 *****************************************************************************/

static GtkActionEntry spell_context_entries[] = {

	{ "context-spell-add",
	  NULL,
	  N_("Add Word to Dictionary"),
	  NULL,
	  NULL,
          G_CALLBACK (action_context_spell_add_cb) },

	{ "context-spell-ignore",
	  NULL,
	  N_("Ignore Misspelled Word"),
	  NULL,
	  NULL,
	  G_CALLBACK (action_context_spell_ignore_cb) },

	{ "context-spell-add-menu",
	  NULL,
	  N_("Add Word To"),
	  NULL,
	  NULL,
	  NULL },

	/* Menus */

	{ "context-more-suggestions-menu",
	  NULL,
	  N_("More Suggestions"),
	  NULL,
	  NULL,
	  NULL }
};

static void
editor_actions_setup_languages_menu (EEditor *editor)
{
	/* FIXME WEBKIT
	GtkUIManager *manager;
	GtkActionGroup *action_group;
	const GList *available_languages;
	guint merge_id;

	manager = editor->priv->manager;
	action_group = editor->priv->language_actions;
	available_languages = gtkhtml_spell_language_get_available ();
	merge_id = gtk_ui_manager_new_merge_id (manager);

	while (available_languages != NULL) {
		GtkhtmlSpellLanguage *language = available_languages->data;
		GtkhtmlSpellChecker *checker;
		GtkToggleAction *action;

		checker = gtkhtml_spell_checker_new (language);

		g_hash_table_insert (
			editor->priv->available_spell_checkers,
			language, checker);

		action = gtk_toggle_action_new (
			gtkhtml_spell_language_get_code (language),
			gtkhtml_spell_language_get_name (language),
			NULL, NULL);

		g_signal_connect (
			action, "toggled",
			G_CALLBACK (action_language_cb), editor);

		gtk_action_group_add_action (
			action_group, GTK_ACTION (action));

		g_object_unref (action);

		gtk_ui_manager_add_ui (
			manager, merge_id,
			"/main-menu/edit-menu/language-menu",
			gtkhtml_spell_language_get_code (language),
			gtkhtml_spell_language_get_code (language),
			GTK_UI_MANAGER_AUTO, FALSE);

		available_languages = g_list_next (available_languages);
	}
	*/
}

static void
editor_actions_setup_spell_check_menu (EEditor *editor)
{
	/*
	GtkUIManager *manager;
	GtkActionGroup *action_group;
	const GList *available_languages;
	guint merge_id;

	manager = editor->priv->manager;
	action_group = editor->priv->spell_check_actions;;
	available_languages = gtkhtml_spell_language_get_available ();
	merge_id = gtk_ui_manager_new_merge_id (manager);

	while (available_languages != NULL) {
		GtkhtmlSpellLanguage *language = available_languages->data;
		GtkAction *action;
		const gchar *code;
		const gchar *name;
		gchar *action_label;
		gchar *action_name;

		code = gtkhtml_spell_language_get_code (language);
		name = gtkhtml_spell_language_get_name (language);

		// Add a suggestion menu. 

		action_name = g_strdup_printf (
			"context-spell-suggest-%s-menu", code);

		action = gtk_action_new (action_name, name, NULL, NULL);
		gtk_action_group_add_action (action_group, action);
		g_object_unref (action);

		gtk_ui_manager_add_ui (
			manager, merge_id,
			"/context-menu/context-spell-suggest",
			action_name, action_name,
			GTK_UI_MANAGER_MENU, FALSE);

		g_free (action_name);

		// Add an item to the "Add Word To" menu.

		action_name = g_strdup_printf ("context-spell-add-%s", code);
		// Translators: %s will be replaced with the actual dictionary name,
		//where a user can add a word to. This is part of an "Add Word To" submenu.
		action_label = g_strdup_printf (_("%s Dictionary"), name);

		action = gtk_action_new (
			action_name, action_label, NULL, NULL);

		g_signal_connect (
			action, "activate",
			G_CALLBACK (action_context_spell_add_cb), editor);

		// Visibility is dependent on whether the
		//corresponding language action is active.
		gtk_action_set_visible (action, FALSE);

		gtk_action_group_add_action (action_group, action);

		g_object_unref (action);

		gtk_ui_manager_add_ui (
			manager, merge_id,
			"/context-menu/context-spell-add-menu",
			action_name, action_name,
			GTK_UI_MANAGER_AUTO, FALSE);

		g_free (action_label);
		g_free (action_name);

		available_languages = g_list_next (available_languages);
	}
	*/
}

void
editor_actions_init (EEditor *editor)
{
	GtkAction *action;
	GtkActionGroup *action_group;
	GtkUIManager *manager;
	const gchar *domain;
	EEditorWidget *editor_widget;

	g_return_if_fail (E_IS_EDITOR (editor));

	manager = e_editor_get_ui_manager (editor);
	domain = GETTEXT_PACKAGE;
	editor_widget = e_editor_get_editor_widget (editor);

	/* Core Actions */
	action_group = editor->priv->core_actions;
	gtk_action_group_set_translation_domain (action_group, domain);
	gtk_action_group_add_actions (
		action_group, core_entries,
		G_N_ELEMENTS (core_entries), editor);
	gtk_action_group_add_radio_actions (
		action_group, core_justify_entries,
		G_N_ELEMENTS (core_justify_entries),
		E_EDITOR_SELECTION_ALIGNMENT_LEFT,
		NULL, NULL);
	gtk_action_group_add_radio_actions (
		action_group, core_mode_entries,
		G_N_ELEMENTS (core_mode_entries),
		E_EDITOR_WIDGET_MODE_HTML,
		G_CALLBACK (action_mode_cb), editor);
	gtk_action_group_add_radio_actions (
		action_group, core_style_entries,
		G_N_ELEMENTS (core_style_entries),
		E_EDITOR_SELECTION_BLOCK_FORMAT_PARAGRAPH,
		G_CALLBACK (action_style_cb), editor);
	gtk_ui_manager_insert_action_group (manager, action_group, 0);

	/* Synchronize wiget mode with the button */
	e_editor_widget_set_mode (editor_widget, E_EDITOR_WIDGET_MODE_HTML);

	/* Face Action */
	action = e_emoticon_action_new (
		"insert-face", _("_Emoticon"),
		_("Insert Emoticon"), NULL);
	g_object_set (action, "icon-name", "face-smile", NULL);
	g_signal_connect (
		action, "item-activated",
		G_CALLBACK (action_insert_emoticon_cb), editor);
	gtk_action_group_add_action (action_group, action);

	/* Core Actions (HTML only) */
	action_group = editor->priv->html_actions;
	gtk_action_group_set_translation_domain (action_group, domain);
	gtk_action_group_add_actions (
		action_group, html_entries,
		G_N_ELEMENTS (html_entries), editor);
	gtk_action_group_add_toggle_actions (
		action_group, html_toggle_entries,
		G_N_ELEMENTS (html_toggle_entries), editor);
	gtk_action_group_add_radio_actions (
		action_group, html_size_entries,
		G_N_ELEMENTS (html_size_entries),
		E_EDITOR_SELECTION_FONT_SIZE_NORMAL,
		NULL, NULL);
	gtk_ui_manager_insert_action_group (manager, action_group, 0);

	/* Context Menu Actions */
	action_group = editor->priv->context_actions;
	gtk_action_group_set_translation_domain (action_group, domain);
	gtk_action_group_add_actions (
		action_group, context_entries,
		G_N_ELEMENTS (context_entries), editor);
	gtk_ui_manager_insert_action_group (manager, action_group, 0);

	/* Context Menu Actions (HTML only) */
	action_group = editor->priv->html_context_actions;
	gtk_action_group_set_translation_domain (action_group, domain);
	gtk_action_group_add_actions (
		action_group, html_context_entries,
		G_N_ELEMENTS (html_context_entries), editor);
	gtk_ui_manager_insert_action_group (manager, action_group, 0);

	/* Context Menu Actions (spell check only) */
	action_group = editor->priv->spell_check_actions;
	gtk_action_group_set_translation_domain (action_group, domain);
	gtk_action_group_add_actions (
		action_group, spell_context_entries,
		G_N_ELEMENTS (spell_context_entries), editor);
	gtk_ui_manager_insert_action_group (manager, action_group, 0);

	/* Language actions are generated dynamically. */
	editor_actions_setup_languages_menu (editor);
	action_group = editor->priv->language_actions;
	gtk_ui_manager_insert_action_group (manager, action_group, 0);

	/* Some spell check actions are generated dynamically. */
	action_group = editor->priv->suggestion_actions;
	editor_actions_setup_spell_check_menu (editor);
	gtk_ui_manager_insert_action_group (manager, action_group, 0);

	/* Fine Tuning */

	g_object_set (
		G_OBJECT (ACTION (SHOW_FIND)),
		"short-label", _("_Find"), NULL);
	g_object_set (
		G_OBJECT (ACTION (SHOW_REPLACE)),
		"short-label", _("Re_place"), NULL);
	g_object_set (
		G_OBJECT (ACTION (INSERT_IMAGE)),
		"short-label", _("_Image"), NULL);
	g_object_set (
		G_OBJECT (ACTION (INSERT_LINK)),
		"short-label", _("_Link"), NULL);
	g_object_set (
		G_OBJECT (ACTION (INSERT_RULE)),
		/* Translators: 'Rule' here means a horizontal line in an HTML text */
		"short-label", _("_Rule"), NULL);
	g_object_set (
		G_OBJECT (ACTION (INSERT_TABLE)),
		"short-label", _("_Table"), NULL);

	gtk_action_set_sensitive (ACTION (UNINDENT), FALSE);
	gtk_action_set_sensitive (ACTION (FIND_AGAIN), FALSE);

	g_object_bind_property (
		editor_widget, "can-redo",
		ACTION (REDO), "sensitive",
		G_BINDING_SYNC_CREATE);
	g_object_bind_property (
		editor_widget, "can-undo",
		ACTION (UNDO), "sensitive",
		G_BINDING_SYNC_CREATE);
	g_object_bind_property (
		editor_widget, "can-copy",
		ACTION (COPY), "sensitive",
		G_BINDING_SYNC_CREATE);
	g_object_bind_property (
		editor_widget, "can-cut",
		ACTION (CUT), "sensitive",
		G_BINDING_SYNC_CREATE);
	g_object_bind_property (
		editor_widget, "can-paste",
		ACTION (PASTE), "sensitive",
		G_BINDING_SYNC_CREATE);

	/* This is connected to JUSTIFY_LEFT action only, but
	 * it automatically applies on all actions in the group. */
	g_object_bind_property (
		editor->priv->selection, "alignment",
		ACTION (JUSTIFY_LEFT), "current-value",
		G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
	g_object_bind_property (
		editor->priv->selection, "bold",
		ACTION (BOLD), "active",
		G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
	g_object_bind_property (
		editor->priv->selection, "font-size",
		ACTION (FONT_SIZE_GROUP), "current-value",
		G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
	g_object_bind_property (
		editor->priv->selection, "indented",
		ACTION (UNINDENT), "sensitive",
		G_BINDING_SYNC_CREATE);
	g_object_bind_property (
		editor->priv->selection, "italic",
		ACTION (ITALIC), "active",
		G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
	g_object_bind_property (
		editor->priv->selection, "monospaced",
		ACTION (MONOSPACED), "active",
		G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
	g_object_bind_property (
		editor->priv->selection, "strike-through",
		ACTION (STRIKETHROUGH), "active",
		G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
	g_object_bind_property (
		editor->priv->selection, "underline",
		ACTION (UNDERLINE), "active",
		G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);

}
