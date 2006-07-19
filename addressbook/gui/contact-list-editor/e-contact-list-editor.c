/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* e-contact-list-editor.c
 * Copyright (C) 2001  Ximian, Inc.
 * Author: Chris Toshok <toshok@ximian.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <config.h>

#include "e-contact-list-editor.h"
#include <e-util/e-icon-factory.h>
#include <e-util/e-util-private.h>
#include <e-util/e-error.h>

#include <string.h>

#include <libgnome/gnome-i18n.h>
#include <libgnomeui/gnome-window-icon.h>
#include <gtk/gtkentry.h>
#include <gtk/gtktogglebutton.h>
#include <gtk/gtkdialog.h>

#include <libedataserverui/e-source-option-menu.h>

#include <table/e-table-scrolled.h>
#include <libgnomevfs/gnome-vfs-ops.h>

#include "shell/evolution-shell-component-utils.h"

#include "misc/e-image-chooser.h"

#include "addressbook/gui/component/addressbook.h"
#include "addressbook/gui/widgets/eab-gui-util.h"
#include "addressbook/util/eab-book-util.h"

#include "eab-editor.h"
#include "e-contact-editor.h"
#include "e-contact-list-model.h"
#include "e-contact-list-editor-marshal.h"
#include "eab-contact-merging.h"

static void e_contact_list_editor_init		(EContactListEditor		 *editor);
static void e_contact_list_editor_class_init	(EContactListEditorClass	 *klass);
static void e_contact_list_editor_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void e_contact_list_editor_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static void e_contact_list_editor_dispose (GObject *object);

static void     e_contact_list_editor_show       (EABEditor *editor);
static void     e_contact_list_editor_raise      (EABEditor *editor);
static void     e_contact_list_editor_close      (EABEditor *editor);
static void     e_contact_list_editor_save_contact (EABEditor *editor, gboolean should_close);
static gboolean e_contact_list_editor_is_valid   (EABEditor *editor);
static gboolean e_contact_list_editor_is_changed (EABEditor *editor);
static GtkWindow* e_contact_list_editor_get_window (EABEditor *editor);

static void set_editable (EContactListEditor *editor);
static void command_state_changed (EContactListEditor *editor);
static void extract_info(EContactListEditor *editor);
static void fill_in_info(EContactListEditor *editor);

static void add_email_cb (GtkWidget *w, EContactListEditor *editor);
static void remove_entry_cb (GtkWidget *w, EContactListEditor *editor);
static void select_cb (GtkWidget *w, EContactListEditor *editor);
static void list_name_changed_cb (GtkWidget *w, EContactListEditor *editor);
static void list_image_changed_cb (GtkWidget *w, EContactListEditor *editor);
static void visible_addrs_toggled_cb (GtkWidget *w, EContactListEditor *editor);
static void source_selected (GtkWidget *source_option_menu, ESource *source, EContactListEditor *editor);

static void close_cb (GtkWidget *widget, EContactListEditor *editor);
static void save_and_close_cb (GtkWidget *widget, EContactListEditor *editor);

static gint app_delete_event_cb (GtkWidget *widget, GdkEvent *event, gpointer data);
static gboolean table_drag_drop_cb (ETable *table, int row, int col, GdkDragContext *context,
				    gint x, gint y, guint time, EContactListEditor *editor);
static gboolean table_drag_motion_cb (ETable *table, int row, int col, GdkDragContext *context,
				      gint x, gint y, guint time, EContactListEditor *editor);
static void table_drag_data_received_cb (ETable *table, int row, int col,
					 GdkDragContext *context,
					 gint x, gint y,
					 GtkSelectionData *selection_data, guint info, guint time,
					 EContactListEditor *editor);

static EABEditorClass *parent_class = NULL;

enum DndTargetType {
	DND_TARGET_TYPE_VCARD,
};
#define VCARD_TYPE "text/x-vcard"

static GtkTargetEntry list_drag_types[] = {
	{ VCARD_TYPE, 0, DND_TARGET_TYPE_VCARD },
};
static const int num_list_drag_types = sizeof (list_drag_types) / sizeof (list_drag_types[0]);

/* The arguments we take */
enum {
	PROP_0,
	PROP_BOOK,
	PROP_CONTACT,
	PROP_IS_NEW_LIST,
	PROP_EDITABLE
};

GType
e_contact_list_editor_get_type (void)
{
	static GType cle_type = 0;

	if (!cle_type) {
		static const GTypeInfo cle_info =  {
			sizeof (EContactListEditorClass),
			NULL,           /* base_init */
			NULL,           /* base_finalize */
			(GClassInitFunc) e_contact_list_editor_class_init,
			NULL,           /* class_finalize */
			NULL,           /* class_data */
			sizeof (EContactListEditor),
			0,             /* n_preallocs */
			(GInstanceInitFunc) e_contact_list_editor_init,
		};

		cle_type = g_type_register_static (EAB_TYPE_EDITOR, "EContactListEditor", &cle_info, 0);
	}

	return cle_type;
}


static void
e_contact_list_editor_class_init (EContactListEditorClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	EABEditorClass *editor_class = EAB_EDITOR_CLASS (klass);

	parent_class = g_type_class_ref (EAB_TYPE_EDITOR);

	editor_class->show = e_contact_list_editor_show;
	editor_class->raise = e_contact_list_editor_raise;
	editor_class->close = e_contact_list_editor_close;
	editor_class->save_contact = e_contact_list_editor_save_contact;
	editor_class->is_valid = e_contact_list_editor_is_valid;
	editor_class->is_changed = e_contact_list_editor_is_changed;
	editor_class->get_window = e_contact_list_editor_get_window;

	object_class->set_property = e_contact_list_editor_set_property;
	object_class->get_property = e_contact_list_editor_get_property;
	object_class->dispose = e_contact_list_editor_dispose;

	g_object_class_install_property (object_class, PROP_BOOK, 
					 g_param_spec_object ("book",
							      _("Book"),
							      /*_( */"XXX blurb" /*)*/,
							      E_TYPE_BOOK,
							      G_PARAM_READWRITE));

	g_object_class_install_property (object_class, PROP_CONTACT, 
					 g_param_spec_object ("contact",
							      _("Contact"),
							      /*_( */"XXX blurb" /*)*/,
							      E_TYPE_CONTACT,
							      G_PARAM_READWRITE));

	g_object_class_install_property (object_class, PROP_IS_NEW_LIST, 
					 g_param_spec_boolean ("is_new_list",
							       _("Is New List"),
							       /*_( */"XXX blurb" /*)*/,
							       FALSE,
							       G_PARAM_READWRITE));

	g_object_class_install_property (object_class, PROP_EDITABLE, 
					 g_param_spec_boolean ("editable",
							       _("Editable"),
							       /*_( */"XXX blurb" /*)*/,
							       FALSE,
							       G_PARAM_READWRITE));
}

static void
e_contact_list_editor_init (EContactListEditor *editor)
{
	GladeXML *gui;
	GList *icon_list;
	char *gladefile;

	editor->contact = NULL;
	editor->changed = FALSE;
	editor->image_set = FALSE;
	editor->editable = TRUE;
	editor->allows_contact_lists = TRUE;
	editor->in_async_call = FALSE;
	editor->is_new_list = FALSE;

	editor->load_source_id = 0;
	editor->load_book = NULL;

	gladefile = g_build_filename (EVOLUTION_GLADEDIR,
				      "contact-list-editor.glade",
				      NULL);
	gui = glade_xml_new (gladefile, NULL, NULL);
	g_free (gladefile);

	editor->gui = gui;

	editor->app = glade_xml_get_widget (gui, "contact list editor");

	editor->table = glade_xml_get_widget (gui, "contact-list-table");
	editor->model = g_object_get_data (G_OBJECT(editor->table), "model");

	/* XXX need this for libglade-2 it seems */
	gtk_widget_show (editor->table);

	editor->add_button = glade_xml_get_widget (editor->gui, "add-email-button");
	editor->remove_button = glade_xml_get_widget (editor->gui, "remove-button");
	editor->select_button = glade_xml_get_widget (editor->gui, "select-button");

	editor->email_entry = glade_xml_get_widget (gui, "email-entry");
	editor->list_name_entry = glade_xml_get_widget (gui, "list-name-entry");
	editor->list_image = glade_xml_get_widget (gui, "list-image");
	editor->visible_addrs_checkbutton = glade_xml_get_widget (gui, "visible-addrs-checkbutton");
	editor->source_menu = glade_xml_get_widget (gui, "source-option-menu-source");

	editor->ok_button = glade_xml_get_widget (gui, "ok-button");
	editor->cancel_button = glade_xml_get_widget (gui, "cancel-button");

	/* connect signals */
	g_signal_connect (editor->add_button,
			  "clicked", G_CALLBACK(add_email_cb), editor);
	g_signal_connect (editor->email_entry,
			  "activate", G_CALLBACK(add_email_cb), editor);
	g_signal_connect (editor->remove_button,
			  "clicked", G_CALLBACK(remove_entry_cb), editor);
	g_signal_connect (editor->select_button,
			  "clicked", G_CALLBACK(select_cb), editor);
	g_signal_connect (editor->list_name_entry,
			  "changed", G_CALLBACK(list_name_changed_cb), editor);
	g_signal_connect (editor->visible_addrs_checkbutton,
			  "toggled", G_CALLBACK(visible_addrs_toggled_cb), editor);

	e_table_drag_dest_set (e_table_scrolled_get_table (E_TABLE_SCROLLED (editor->table)),
			       0, list_drag_types, num_list_drag_types, GDK_ACTION_LINK);

	g_signal_connect (e_table_scrolled_get_table (E_TABLE_SCROLLED (editor->table)),
			  "table_drag_motion", G_CALLBACK(table_drag_motion_cb), editor);
	g_signal_connect (e_table_scrolled_get_table (E_TABLE_SCROLLED (editor->table)),
			  "table_drag_drop", G_CALLBACK (table_drag_drop_cb), editor);
	g_signal_connect (e_table_scrolled_get_table (E_TABLE_SCROLLED (editor->table)),
			  "table_drag_data_received", G_CALLBACK(table_drag_data_received_cb), editor);

	g_signal_connect (editor->ok_button,
			  "clicked", G_CALLBACK(save_and_close_cb), editor);
	g_signal_connect (editor->cancel_button,
			  "clicked", G_CALLBACK(close_cb), editor);
	

	g_signal_connect (editor->list_image,
			  "changed", G_CALLBACK(list_image_changed_cb), editor);

	g_signal_connect (editor->source_menu,
			  "source_selected", G_CALLBACK (source_selected), editor);

	command_state_changed (editor);

	/* Connect to the deletion of the dialog */
	g_signal_connect (editor->app, "delete_event",
			  G_CALLBACK (app_delete_event_cb), editor);

	gtk_dialog_set_has_separator (GTK_DIALOG (editor->app), FALSE);

	/* set the icon */
	icon_list = e_icon_factory_get_icon_list ("stock_contact-list");
	if (icon_list) {
		gtk_window_set_icon_list (GTK_WINDOW (editor->app), icon_list);
		g_list_foreach (icon_list, (GFunc) g_object_unref, NULL);
		g_list_free (icon_list);
	}

	gtk_widget_show_all (editor->app);
}

static void
new_target_cb (EBook *new_book, EBookStatus status, EContactListEditor *editor)
{
	editor->load_source_id = 0;
	editor->load_book      = NULL;

	if (status != E_BOOK_ERROR_OK || new_book == NULL) {
		eab_load_error_dialog (NULL, e_book_get_source (new_book), status);

		e_source_option_menu_select (E_SOURCE_OPTION_MENU (editor->source_menu),
					     e_book_get_source (editor->book));

		if (new_book)
			g_object_unref (new_book);
		return;
	}

	g_object_set (editor, "book", new_book, NULL);
	g_object_unref (new_book);
}

static void
cancel_load (EContactListEditor *editor)
{
	if (editor->load_source_id) {
		addressbook_load_cancel (editor->load_source_id);
		editor->load_source_id = 0;

		g_object_unref (editor->load_book);
		editor->load_book = NULL;
	}
}

static void
source_selected (GtkWidget *source_option_menu, ESource *source, EContactListEditor *editor)
{
	cancel_load (editor);

	if (e_source_equal (e_book_get_source (editor->book), source))
		return;

	editor->load_book = e_book_new (source, NULL);
	editor->load_source_id = addressbook_load (editor->load_book,
						   (EBookCallback) new_target_cb, editor);
}

static void
e_contact_list_editor_dispose (GObject *object)
{
	EContactListEditor *editor = E_CONTACT_LIST_EDITOR (object);

	cancel_load (editor);

	if (editor->name_selector) {
		g_object_unref (editor->name_selector);
		editor->name_selector = NULL;
	}

	if (G_OBJECT_CLASS (parent_class)->dispose)
		(* G_OBJECT_CLASS (parent_class)->dispose) (object);
}

typedef struct {
	EContactListEditor *cle;
	gboolean should_close;
} EditorCloseStruct;

static void
list_added_cb (EBook *book, EBookStatus status, const char *id, EditorCloseStruct *ecs)
{
	EContactListEditor *cle = ecs->cle;
	gboolean should_close = ecs->should_close;

	if (cle->app)
		gtk_widget_set_sensitive (cle->app, TRUE);
	cle->in_async_call = FALSE;

	e_contact_set (cle->contact, E_CONTACT_UID, (char*)id);

	eab_editor_contact_added (EAB_EDITOR (cle), status, cle->contact);

	if (status == E_BOOK_ERROR_OK) {
		cle->is_new_list = FALSE;

		if (should_close)
			eab_editor_close (EAB_EDITOR (cle));
		else
			command_state_changed (cle);
	}

	g_object_unref (cle);
	g_free (ecs);
}

static void
list_modified_cb (EBook *book, EBookStatus status, EditorCloseStruct *ecs)
{
	EContactListEditor *cle = ecs->cle;
	gboolean should_close = ecs->should_close;

	if (cle->app)
		gtk_widget_set_sensitive (cle->app, TRUE);
	cle->in_async_call = FALSE;

	eab_editor_contact_modified (EAB_EDITOR (cle), status, cle->contact);

	if (status == E_BOOK_ERROR_OK) {
		if (should_close)
			eab_editor_close (EAB_EDITOR (cle));
	}

	g_object_unref (cle); /* release ref held for ebook callback */
	g_free (ecs);
}

static void
save_contact (EContactListEditor *cle, gboolean should_close)
{
	extract_info (cle);

	if (cle->book) {
		EditorCloseStruct *ecs = g_new(EditorCloseStruct, 1);
		
		ecs->cle = cle;
		g_object_ref (cle);
		ecs->should_close = should_close;

		if (cle->app)
			gtk_widget_set_sensitive (cle->app, FALSE);
		cle->in_async_call = TRUE;

		if (cle->is_new_list)
			eab_merging_book_add_contact (cle->book, cle->contact, (EBookIdCallback)list_added_cb, ecs);
		else
			eab_merging_book_commit_contact (cle->book, cle->contact, (EBookCallback)list_modified_cb, ecs);

		cle->changed = FALSE;
	}
}

static gboolean
is_named (EContactListEditor *editor)
{
	char *string = gtk_editable_get_chars(GTK_EDITABLE (editor->list_name_entry), 0, -1);
	gboolean named = FALSE;

	if (string && *string) {
		named = TRUE;
	}

	g_free (string);

	return named;
}

static void
e_contact_list_editor_save_contact (EABEditor *editor, gboolean should_close)
{
	EContactListEditor *cle = E_CONTACT_LIST_EDITOR (editor);

	save_contact (cle, should_close);
}

static gboolean
e_contact_list_editor_is_valid (EABEditor *editor)
{
	EContactListEditor *cle = E_CONTACT_LIST_EDITOR (editor);

	return is_named (cle);
}

static gboolean
e_contact_list_editor_is_changed (EABEditor *editor)
{
	EContactListEditor *cle = E_CONTACT_LIST_EDITOR (editor);

	return cle->changed;
}

static GtkWindow*
e_contact_list_editor_get_window (EABEditor *editor)
{
	return GTK_WINDOW (E_CONTACT_LIST_EDITOR (editor)->app);
}

static void
close_cb (GtkWidget *widget, EContactListEditor *cle)
{
	eab_editor_prompt_to_save_changes (EAB_EDITOR (cle), GTK_WINDOW (cle->app));
}

static void
save_and_close_cb (GtkWidget *widget, EContactListEditor *cle)
{
	EABEditor *editor = EAB_EDITOR (cle);
	if (! (cle->editable  &&  cle->allows_contact_lists))
		eab_editor_close(editor);
	else
		save_contact (cle, TRUE);
}

static void
contact_list_editor_destroy_notify (gpointer data,
				    GObject *where_the_object_was)
{
	eab_editor_remove (EAB_EDITOR (data));
}

EContactListEditor *
e_contact_list_editor_new (EBook *book,
			   EContact *list_contact,
			   gboolean is_new_list,
			   gboolean editable)
{
	EContactListEditor *ce = g_object_new (E_TYPE_CONTACT_LIST_EDITOR, NULL);

	eab_editor_add (EAB_EDITOR (ce));
	g_object_weak_ref (G_OBJECT (ce), contact_list_editor_destroy_notify, ce);

	g_object_set (ce,
		      "book", book,
		      "contact", list_contact,
		      "is_new_list", is_new_list,
		      "editable", editable,
		      NULL);

	return ce;
}

static void
e_contact_list_editor_set_property (GObject *object, guint prop_id,
				    const GValue *value, GParamSpec *pspec)
{
	EContactListEditor *editor;

	editor = E_CONTACT_LIST_EDITOR (object);
	
	switch (prop_id){
	case PROP_BOOK: {
		gboolean changed;

		if (editor->book)
			g_object_unref (editor->book);
		editor->book = E_BOOK(g_value_get_object (value));
		g_object_ref (editor->book);
		/* XXX more here about editable/etc. */

		changed = (editor->allows_contact_lists != e_book_check_static_capability (editor->book,
											   "contact-lists"));
		editor->allows_contact_lists = e_book_check_static_capability (editor->book,
									       "contact-lists");

		if (changed) {
			set_editable (editor);
			command_state_changed (editor);
		}
		break;
	}
	case PROP_CONTACT:
		if (editor->contact)
			g_object_unref (editor->contact);
		editor->contact = e_contact_duplicate(E_CONTACT(g_value_get_object (value)));
		fill_in_info(editor);
		editor->changed = FALSE;
		command_state_changed (editor);
		break;
	case PROP_IS_NEW_LIST: {
		gboolean new_value = g_value_get_boolean (value);
		gboolean changed = (editor->is_new_list != new_value);

		editor->is_new_list = new_value;
		
		if (changed)
			command_state_changed (editor);
		break;
	}
	case PROP_EDITABLE: {
		gboolean new_value = g_value_get_boolean (value);
		gboolean changed = (editor->editable != new_value);

		editor->editable = new_value;

		if (changed) {
			set_editable (editor);
			command_state_changed (editor);
		}
		break;
	}
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
e_contact_list_editor_get_property (GObject *object, guint prop_id,
				    GValue *value, GParamSpec *pspec)
{
	EContactListEditor *editor;

	editor = E_CONTACT_LIST_EDITOR (object);

	switch (prop_id) {
	case PROP_BOOK:
		g_value_set_object (value, editor->book);
		break;

	case PROP_CONTACT:
		extract_info(editor);
		g_value_set_object (value, editor->contact);
		break;

	case PROP_IS_NEW_LIST:
		g_value_set_boolean (value, editor->is_new_list);
		break;

	case PROP_EDITABLE:
		g_value_set_boolean (value, editor->editable);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
e_contact_list_editor_show (EABEditor *editor)
{
	EContactListEditor *cle = E_CONTACT_LIST_EDITOR (editor);
	gtk_widget_show (cle->app);
}

static void
e_contact_list_editor_raise (EABEditor *editor)
{
	EContactListEditor *cle = E_CONTACT_LIST_EDITOR (editor);
	gdk_window_raise (GTK_WIDGET (cle->app)->window);
}

static void
e_contact_list_editor_close (EABEditor *editor)
{
	EContactListEditor *cle = E_CONTACT_LIST_EDITOR (editor);
	g_assert (cle->app != NULL);

	gtk_widget_destroy (cle->app);
	cle->app = NULL;

	eab_editor_closed (EAB_EDITOR (cle));
}

GtkWidget *
e_contact_list_editor_create_table(gchar *name,
				   gchar *string1, gchar *string2,
				   gint int1, gint int2);

GtkWidget *
e_contact_list_editor_create_table(gchar *name,
				   gchar *string1, gchar *string2,
				   gint int1, gint int2)
{
	
	ETableModel *model;
	GtkWidget *table;
	char *etspecfile;

	model = e_contact_list_model_new ();

	etspecfile = g_build_filename (EVOLUTION_ETSPECDIR,
				       "e-contact-list-editor.etspec",
				       NULL);
	table = e_table_scrolled_new_from_spec_file (model,
				      NULL,
				      etspecfile,
				      NULL);
	g_free (etspecfile);

	g_object_set_data(G_OBJECT(table), "model", model);

	return table;
}

static gboolean 
contact_already_exists (EContactListModel *model, const char *email) 
{
	int row_count;
	int row;
	char *list_email;
	row_count = e_table_model_row_count (E_TABLE_MODEL (model));
	for (row = 0; row < row_count; row ++) {
		list_email = (char *) e_destination_get_email(e_contact_list_model_get_destination (model, row));
		 if (strcmp (list_email, email) == 0) {
			 if (e_error_run (NULL, "addressbook:ask-list-add-exists",
						 email) != GTK_RESPONSE_YES)
				 return TRUE;
			 else
				 return FALSE;
			 break;
		 }
	}
	return FALSE;
}

static void
add_to_model (EContactListEditor *editor, GList *destinations)
{
	GList *l;

	for (l = destinations; l; l = g_list_next (l)) {
		EDestination *destination = l->data;
		if (e_destination_get_email (destination)) {
			if (! contact_already_exists (E_CONTACT_LIST_MODEL (editor->model)
					, e_destination_get_email (destination))) {
				e_contact_list_model_add_destination (E_CONTACT_LIST_MODEL (editor->model),
								  destination);
			}
		}
	}
}

static void
select_names_ok_cb (GtkWidget *widget, gint response, gpointer data)
{
	EContactListEditor  *ce;
	ENameSelectorDialog *name_selector_dialog;
	ENameSelectorModel  *name_selector_model;
	EDestinationStore   *destination_store;
	GList               *destinations;

	ce = E_CONTACT_LIST_EDITOR (data);

	name_selector_dialog = e_name_selector_peek_dialog (ce->name_selector);
	gtk_widget_hide (GTK_WIDGET (name_selector_dialog));

	name_selector_model = e_name_selector_peek_model (ce->name_selector);
	e_name_selector_model_peek_section (name_selector_model, "Members", NULL, &destination_store);
	destinations = e_destination_store_list_destinations (destination_store);

	add_to_model (ce, destinations);

	g_list_free (destinations);

	ce->changed = TRUE;
	command_state_changed (ce);
}

static void
setup_name_selector (EContactListEditor *editor)
{
	ENameSelectorModel  *name_selector_model;
	ENameSelectorDialog *name_selector_dialog;

	if (editor->name_selector)
		return;

	editor->name_selector = e_name_selector_new ();

	name_selector_model = e_name_selector_peek_model (editor->name_selector);
	e_name_selector_model_add_section (name_selector_model, "Members", gettext ("_Members"), NULL);

	name_selector_dialog = e_name_selector_peek_dialog (editor->name_selector);
	gtk_window_set_title (GTK_WINDOW (name_selector_dialog), _("Contact List Members"));
	g_signal_connect (name_selector_dialog, "response",
			  G_CALLBACK (select_names_ok_cb), editor);
}

static void
select_cb (GtkWidget *w, EContactListEditor *editor)
{
	ENameSelectorModel  *name_selector_model;
	ENameSelectorDialog *name_selector_dialog;
	EDestinationStore   *destination_store;
	GList               *destinations;
	GList               *l;

	setup_name_selector (editor);

	/* We need to empty out the destination store, since we copy its contents every time.
	 * This sucks, we should really be wired directly to the EDestinationStore that
	 * the name selector uses in true MVC fashion. */

	name_selector_model = e_name_selector_peek_model (editor->name_selector);
	e_name_selector_model_peek_section (name_selector_model, "Members", NULL, &destination_store);
	destinations = e_destination_store_list_destinations (destination_store);

	for (l = destinations; l; l = g_list_next (l)) {
		EDestination *destination = l->data;
		e_destination_store_remove_destination (destination_store, destination);
	}

	g_list_free (destinations);

	name_selector_dialog = e_name_selector_peek_dialog (editor->name_selector);
	gtk_widget_show (GTK_WIDGET (name_selector_dialog));
}

GtkWidget *
e_contact_list_editor_create_source_option_menu (gchar *name,
						 gchar *string1, gchar *string2,
						 gint int1, gint int2);

GtkWidget *
e_contact_list_editor_create_source_option_menu (gchar *name,
						 gchar *string1, gchar *string2,
						 gint int1, gint int2)
{

	GtkWidget   *menu;
	GConfClient *gconf_client;
	ESourceList *source_list;

	gconf_client = gconf_client_get_default ();
	source_list = e_source_list_new_for_gconf (gconf_client, "/apps/evolution/addressbook/sources");

	menu = e_source_option_menu_new (source_list);
	g_object_unref (source_list);

	gtk_widget_show (menu);
	return menu;
}

static void
add_email_cb (GtkWidget *w, EContactListEditor *editor)
{
	GtkAdjustment *adj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (editor->table));
	const char *text = gtk_entry_get_text (GTK_ENTRY(editor->email_entry));

	if (text && *text) {
		e_contact_list_model_add_email (E_CONTACT_LIST_MODEL(editor->model), text);

		/* Skip to the end of the list */
		if (adj->upper - adj->lower > adj->page_size)
			gtk_adjustment_set_value (adj, adj->upper);
		
		editor->changed = TRUE;

	}

	gtk_entry_set_text (GTK_ENTRY(editor->email_entry), "");
	command_state_changed (editor);
}

static void
prepend_selected_rows (int model_row, GList **list)
{
	int *idx = g_new (int, 1);
	*idx = model_row;
	*list = g_list_append (*list, idx);
}

static void
remove_entry_cb (GtkWidget *w, EContactListEditor *editor)
{
	int *idx = NULL;
	GList *list = NULL;
	int num_rows_deleted = 0;
	e_table_selected_row_foreach (e_table_scrolled_get_table(E_TABLE_SCROLLED(editor->table)),
				      (EForeachFunc)prepend_selected_rows, &list);

	if (!list) return ;

	for(; list; list=list->next, num_rows_deleted++) {
		idx = (int *)(list->data);
		e_contact_list_model_remove_row (E_CONTACT_LIST_MODEL (editor->model), (*idx - num_rows_deleted));
		g_free (idx);
		list->data = NULL;
	}

	list = g_list_first (list);
	g_list_free (list);
	editor->changed = TRUE;
	command_state_changed (editor);
}

static void
list_name_changed_cb (GtkWidget *w, EContactListEditor *editor)
{
	char *string = gtk_editable_get_chars(GTK_EDITABLE (w), 0, -1);
	char *title;

	editor->changed = TRUE;

	if (string && *string)
		title = string;
	else
		title = _("Contact List Editor");

	gtk_window_set_title (GTK_WINDOW (editor->app), title);

	g_free (string);

	command_state_changed (editor);
}

static void
list_image_changed_cb (GtkWidget *w, EContactListEditor *editor)
{
	editor->image_set = TRUE;
	editor->changed = TRUE;
	command_state_changed (editor);
}

static void
visible_addrs_toggled_cb (GtkWidget *w, EContactListEditor *editor)
{
	editor->changed = TRUE;
	command_state_changed (editor);
}

static void
set_editable (EContactListEditor *editor)
{
	gtk_widget_set_sensitive (editor->email_entry, editor->editable && editor->allows_contact_lists);
	gtk_widget_set_sensitive (editor->list_name_entry, editor->editable && editor->allows_contact_lists);
	gtk_widget_set_sensitive (editor->add_button, editor->editable && editor->allows_contact_lists);
	gtk_widget_set_sensitive (editor->remove_button, editor->editable && editor->allows_contact_lists);
	gtk_widget_set_sensitive (editor->select_button, editor->editable && editor->allows_contact_lists);
	gtk_widget_set_sensitive (editor->cancel_button, editor->editable && editor->allows_contact_lists);
	gtk_widget_set_sensitive (editor->visible_addrs_checkbutton, editor->editable && editor->allows_contact_lists);
}

/* Callback used when the editor is destroyed */
static gint
app_delete_event_cb (GtkWidget *widget, GdkEvent *event, gpointer data)
{
	EContactListEditor *ce;

	ce = E_CONTACT_LIST_EDITOR (data);

	/* if we're in an async call, don't allow the dialog to close */
	if (ce->in_async_call)
		return TRUE;

	eab_editor_prompt_to_save_changes (EAB_EDITOR (ce), GTK_WINDOW (ce->app));
	return TRUE;
}

static gboolean
table_drag_motion_cb (ETable *table, int row, int col,
		      GdkDragContext *context,
		      gint x, gint y, guint time, EContactListEditor *editor)
{
	GList *p;

	for (p = context->targets; p != NULL; p = p->next) {
		char *possible_type;

		possible_type = gdk_atom_name (GDK_POINTER_TO_ATOM (p->data));
		if (!strcmp (possible_type, VCARD_TYPE)) {
			g_free (possible_type);
			gdk_drag_status (context, GDK_ACTION_LINK, time);
			return TRUE;
		}

		g_free (possible_type);
	}

	return FALSE;
}

static gboolean
table_drag_drop_cb (ETable *table, int row, int col,
		    GdkDragContext *context,
		    gint x, gint y, guint time, EContactListEditor *editor)
{
	GList *p;

	for (p = context->targets; p != NULL; p = p->next) {
		char *possible_type;

		possible_type = gdk_atom_name (GDK_POINTER_TO_ATOM (p->data));
		if (!strcmp (possible_type, VCARD_TYPE)) {
			g_free (possible_type);
			break;
		}

		g_free (possible_type);
	}


	if (p) {
		gtk_drag_get_data (GTK_WIDGET (table), context,
				   GDK_POINTER_TO_ATOM (p->data),
				   time);
		return TRUE;
	}

	return FALSE;
}

static void
table_drag_data_received_cb (ETable *table, int row, int col,
			     GdkDragContext *context,
			     gint x, gint y,
			     GtkSelectionData *selection_data,
			     guint info, guint time, EContactListEditor *editor)
{
	GtkAdjustment *adj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (editor->table));
	char *target_type;
	gboolean changed = FALSE;
	gboolean handled = FALSE;

	target_type = gdk_atom_name (selection_data->target);

	if (!strcmp (target_type, VCARD_TYPE)) {

		GList *contact_list = eab_contact_list_from_string (selection_data->data);
		GList *c;

		if (contact_list)
			handled = TRUE;

		for (c = contact_list; c; c = c->next) {
			EContact *contact = c->data;

			if (!e_contact_get (contact, E_CONTACT_IS_LIST)) { 
				if (e_contact_get (contact, E_CONTACT_EMAIL_1)) {
					if (! contact_already_exists (E_CONTACT_LIST_MODEL (editor->model)
							, e_contact_get (contact, E_CONTACT_EMAIL_1))) {
						e_contact_list_model_add_contact (E_CONTACT_LIST_MODEL (editor->model),
										  contact,
									  0  /* Hard-wired for default e-mail */);
		
						changed = TRUE;
					}
				}
				else
					g_warning ("Contact with no email-ids listed can't be added to a Contact-List");
			}
		}
		g_list_foreach (contact_list, (GFunc)g_object_unref, NULL);
		g_list_free (contact_list);

		/* Skip to the end of the list */
		if (adj->upper - adj->lower > adj->page_size)
			gtk_adjustment_set_value (adj, adj->upper);

		if (changed) {
			editor->changed = TRUE;
			command_state_changed (editor);
		}
	}

	gtk_drag_finish (context, handled, FALSE, time);
}

static void
command_state_changed (EContactListEditor *editor)
{
	gboolean valid = eab_editor_is_valid (EAB_EDITOR (editor));

	/* FIXME set the ok button to ok */
	gtk_widget_set_sensitive (editor->ok_button, valid && editor->allows_contact_lists);
	gtk_widget_set_sensitive (editor->source_menu, editor->is_new_list);
	gtk_widget_set_sensitive (glade_xml_get_widget (editor->gui, "source-label"), editor->is_new_list);
}

static void
extract_info(EContactListEditor *editor)
{
	EContact *contact = editor->contact;
	if (contact) {
		int i;
		char *image_data;
		gsize image_data_len;
		char *string = gtk_editable_get_chars(GTK_EDITABLE (editor->list_name_entry), 0, -1);

		if (string && *string) {
			e_contact_set (contact, E_CONTACT_FILE_AS, string);
			e_contact_set (contact, E_CONTACT_FULL_NAME, string);
		}

		g_free (string);

		e_contact_set (contact, E_CONTACT_IS_LIST, GINT_TO_POINTER (TRUE));
		e_contact_set (contact, E_CONTACT_LIST_SHOW_ADDRESSES,
			       GINT_TO_POINTER (!gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON(editor->visible_addrs_checkbutton))));

		e_vcard_remove_attributes (E_VCARD (contact), "", EVC_EMAIL);

		/* then refill it from the contact list model */
		for (i = 0; i < e_table_model_row_count (editor->model); i ++) {
			const EDestination *dest = e_contact_list_model_get_destination (E_CONTACT_LIST_MODEL (editor->model), i);
			EVCardAttribute *attr;

			attr = e_vcard_attribute_new (NULL, EVC_EMAIL);

			e_vcard_add_attribute (E_VCARD (contact), attr);

			e_destination_export_to_vcard_attribute ((EDestination*)dest, attr);
		}

		if (editor->image_set
		    && e_image_chooser_get_image_data (E_IMAGE_CHOOSER (editor->list_image),
						       &image_data,
						       &image_data_len)) {
			EContactPhoto photo;

			photo.type = E_CONTACT_PHOTO_TYPE_INLINED;
			photo.data.inlined.mime_type = NULL;
			photo.data.inlined.data = image_data;
			photo.data.inlined.length = image_data_len;

			e_contact_set (contact, E_CONTACT_LOGO, &photo);
			g_free (image_data);
		}
		else {
			e_contact_set (contact, E_CONTACT_LOGO, NULL);
		}
	}
}

static void
fill_in_info(EContactListEditor *editor)
{
	if (editor->contact) {
		EContactPhoto *photo;
		const char *file_as;
		gboolean show_addresses = FALSE;
		GList *email_list;
		GList *iter;

		file_as = e_contact_get_const (editor->contact, E_CONTACT_FILE_AS);
		email_list = e_contact_get_attributes (editor->contact, E_CONTACT_EMAIL);
		show_addresses = GPOINTER_TO_INT (e_contact_get (editor->contact, E_CONTACT_LIST_SHOW_ADDRESSES));

		gtk_editable_delete_text (GTK_EDITABLE (editor->list_name_entry), 0, -1);
		if (file_as) {
			int position = 0;

			gtk_editable_insert_text (GTK_EDITABLE (editor->list_name_entry), file_as, strlen (file_as), &position);
		}

		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(editor->visible_addrs_checkbutton), !show_addresses);

		e_contact_list_model_remove_all (E_CONTACT_LIST_MODEL (editor->model));

		for (iter = email_list; iter; iter = iter->next) {
			EVCardAttribute *attr = iter->data;
			GList *p;
			EDestination *list_dest = e_destination_new ();
			char *contact_uid = NULL;
			char *email = NULL;
			char *name = NULL;
			int email_num = -1;
			gboolean html_pref = FALSE;

			for (p = e_vcard_attribute_get_params (attr); p; p = p->next) {
				EVCardAttributeParam *param = p->data;
				const char *param_name = e_vcard_attribute_param_get_name (param);
				if (!g_ascii_strcasecmp (param_name,
							 EVC_X_DEST_CONTACT_UID)) {
					GList *v = e_vcard_attribute_param_get_values (param);
					contact_uid = v ? v->data : NULL;
				}
				else if (!g_ascii_strcasecmp (param_name,
							      EVC_X_DEST_EMAIL_NUM)) {
					GList *v = e_vcard_attribute_param_get_values (param);
					email_num = v ? atoi (v->data) : -1;
				}
				else if (!g_ascii_strcasecmp (param_name,
							      EVC_X_DEST_NAME)) {
					GList *v = e_vcard_attribute_param_get_values (param);
					name = v ? v->data : NULL;
				}
				else if (!g_ascii_strcasecmp (param_name,
							      EVC_X_DEST_EMAIL)) {
					GList *v = e_vcard_attribute_param_get_values (param);
					email = v ? v->data : NULL;
				}
				else if (!g_ascii_strcasecmp (param_name,
							      EVC_X_DEST_HTML_MAIL)) {
					GList *v = e_vcard_attribute_param_get_values (param);
					html_pref = v ? !g_ascii_strcasecmp (v->data, "true") : FALSE;
				}
			}

			if (contact_uid) e_destination_set_contact_uid (list_dest, contact_uid, email_num);
			if (name) e_destination_set_name (list_dest, name);
			if (email) e_destination_set_email (list_dest, email);
			e_destination_set_html_mail_pref (list_dest, html_pref);
			
			e_contact_list_model_add_destination (E_CONTACT_LIST_MODEL (editor->model), list_dest);
		}

		g_list_foreach (email_list, (GFunc) e_vcard_attribute_free, NULL);
		g_list_free (email_list);

		photo = e_contact_get (editor->contact, E_CONTACT_LOGO);
		if (photo && photo->type == E_CONTACT_PHOTO_TYPE_INLINED) {
			e_image_chooser_set_image_data (E_IMAGE_CHOOSER (editor->list_image), photo->data.inlined.data, photo->data.inlined.length);
			e_contact_photo_free (photo);
		}
	}
	
	if (editor->book) {
		ESource   *source;

		source = e_book_get_source (editor->book);		
		e_source_option_menu_select (E_SOURCE_OPTION_MENU (editor->source_menu), source);
		gtk_widget_set_sensitive (editor->source_menu, editor->is_new_list);
		gtk_widget_set_sensitive (glade_xml_get_widget (editor->gui, "source-label"), editor->is_new_list);
	}
}
