/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * e-table-field-chooser.c
 * Copyright (C) 2000  Ximian, Inc.
 * Author: Chris Lahey <clahey@ximian.com>
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

#include <gtk/gtk.h>

#include <libgnome/gnome-i18n.h>
#include <libgnome/gnome-util.h>
#include <gtk/gtkscrolledwindow.h>
#include <gal/e-table/e-table-scrolled.h>
#include <gal/e-table/e-table-model.h>
#include <gal/widgets/e-popup-menu.h>
#include <gal/widgets/e-gui-utils.h>
#include <gal/menus/gal-view-factory-etable.h>
#include <gal/menus/gal-view-etable.h>
#include <gal/util/e-xml-utils.h>
#include <libgnomeui/gnome-dialog-util.h>

#include <libgnomeprint/gnome-print.h>
#include <libgnomeprint/gnome-print-job.h>
#include <libgnomeprintui/gnome-print-dialog.h>
#include <libgnomeprintui/gnome-print-job-preview.h>

#include "addressbook/printing/e-contact-print.h"
#include "addressbook/printing/e-contact-print-envelope.h"

#ifdef WITH_ADDRESSBOOK_VIEW_TREEVIEW
#include <gal/widgets/e-treeview-selection-model.h>
#include "gal-view-factory-treeview.h"
#include "gal-view-treeview.h"
#endif

#include "e-addressbook-marshal.h"
#include "e-addressbook-view.h"
#include "e-addressbook-model.h"
#include "eab-gui-util.h"
#include "util/eab-book-util.h"
#include "e-addressbook-table-adapter.h"
#ifdef WITH_ADDRESSBOOK_VIEW_TREEVIEW
#include "e-addressbook-treeview-adapter.h"
#endif
#include "eab-contact-merging.h"

#include "e-contact-editor.h"
#include <gdk/gdkkeysyms.h>
#include <ctype.h>
#include <string.h>

#include <libxml/tree.h>
#include <libxml/parser.h>

#define SHOW_ALL_SEARCH "(contains \"x-evolution-any-field\" \"\")"

#define d(x)

static void eab_view_init		(EABView		 *card);
static void eab_view_class_init	(EABViewClass	 *klass);

static void eab_view_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void eab_view_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static void eab_view_dispose (GObject *object);
static void change_view_type (EABView *view, EABViewType view_type);

static void status_message     (GtkObject *object, const gchar *status, EABView *eav);
static void search_result      (GtkObject *object, EBookViewStatus status, EABView *eav);
static void folder_bar_message (GtkObject *object, const gchar *status, EABView *eav);
static void stop_state_changed (GtkObject *object, EABView *eav);
static void writable_status (GtkObject *object, gboolean writable, EABView *eav);
static void backend_died (GtkObject *object, EABView *eav);
static void command_state_change (EABView *eav);

static void selection_clear_event (GtkWidget *invisible, GdkEventSelection *event,
				   EABView *view);
static void selection_received (GtkWidget *invisible, GtkSelectionData *selection_data,
				guint time, EABView *view);
static void selection_get (GtkWidget *invisible, GtkSelectionData *selection_data,
			   guint info, guint time_stamp, EABView *view);
static void invisible_destroyed (gpointer data, GObject *where_object_was);

#define PARENT_TYPE GTK_TYPE_EVENT_BOX
static GtkEventBoxClass *parent_class = NULL;

/* The arguments we take */
enum {
	PROP_0,
	PROP_BOOK,
	PROP_QUERY,
	PROP_TYPE,
};

enum {
	STATUS_MESSAGE,
	SEARCH_RESULT,
	FOLDER_BAR_MESSAGE,
	COMMAND_STATE_CHANGE,
	LAST_SIGNAL
};

enum DndTargetType {
	DND_TARGET_TYPE_VCARD,
};
#define VCARD_TYPE "text/x-vcard"
static GtkTargetEntry drag_types[] = {
	{ VCARD_TYPE, 0, DND_TARGET_TYPE_VCARD },
};
static const int num_drag_types = sizeof (drag_types) / sizeof (drag_types[0]);

static guint eab_view_signals [LAST_SIGNAL] = {0, };

static GdkAtom clipboard_atom = GDK_NONE;

static GalViewCollection *collection = NULL;

GType
eab_view_get_type (void)
{
	static GType type = 0;

	if (!type) {
		static const GTypeInfo info =  {
			sizeof (EABViewClass),
			NULL,           /* base_init */
			NULL,           /* base_finalize */
			(GClassInitFunc) eab_view_class_init,
			NULL,           /* class_finalize */
			NULL,           /* class_data */
			sizeof (EABView),
			0,             /* n_preallocs */
			(GInstanceInitFunc) eab_view_init,
		};

		type = g_type_register_static (PARENT_TYPE, "EABView", &info, 0);
	}

	return type;
}

static void
eab_view_class_init (EABViewClass *klass)
{
	GObjectClass *object_class;
	GtkWidgetClass *widget_class;

	object_class = G_OBJECT_CLASS(klass);
	widget_class = GTK_WIDGET_CLASS(klass);

	parent_class = gtk_type_class (PARENT_TYPE);

	object_class->set_property = eab_view_set_property;
	object_class->get_property = eab_view_get_property;
	object_class->dispose = eab_view_dispose;

	g_object_class_install_property (object_class, PROP_BOOK, 
					 g_param_spec_object ("book",
							      _("Book"),
							      /*_( */"XXX blurb" /*)*/,
							      E_TYPE_BOOK,
							      G_PARAM_READWRITE));

	g_object_class_install_property (object_class, PROP_QUERY, 
					 g_param_spec_string ("query",
							      _("Query"),
							      /*_( */"XXX blurb" /*)*/,
							      NULL,
							      G_PARAM_READWRITE));

	g_object_class_install_property (object_class, PROP_TYPE, 
					 g_param_spec_int ("type",
							   _("Type"),
							   /*_( */"XXX blurb" /*)*/,
							   EAB_VIEW_NONE, 
							   EAB_VIEW_TABLE,
							   EAB_VIEW_NONE,
							   G_PARAM_READWRITE));

	eab_view_signals [STATUS_MESSAGE] =
		g_signal_new ("status_message",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (EABViewClass, status_message),
			      NULL, NULL,
			      eab_marshal_NONE__POINTER,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);

	eab_view_signals [SEARCH_RESULT] =
		g_signal_new ("search_result",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (EABViewClass, search_result),
			      NULL, NULL,
			      eab_marshal_NONE__INT,
			      G_TYPE_NONE, 1, G_TYPE_INT);

	eab_view_signals [FOLDER_BAR_MESSAGE] =
		g_signal_new ("folder_bar_message",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (EABViewClass, folder_bar_message),
			      NULL, NULL,
			      eab_marshal_NONE__POINTER,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);

	eab_view_signals [COMMAND_STATE_CHANGE] =
		g_signal_new ("command_state_change",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (EABViewClass, command_state_change),
			      NULL, NULL,
			      eab_marshal_NONE__NONE,
			      G_TYPE_NONE, 0);

	if (!clipboard_atom)
		clipboard_atom = gdk_atom_intern ("CLIPBOARD", FALSE);
}

static void
eab_view_init (EABView *eav)
{
	eav->view_type = EAB_VIEW_NONE;

	eav->model = NULL;
	eav->object = NULL;
	eav->widget = NULL;
	eav->scrolled = NULL;
	eav->contact_display = NULL;

	eav->view_instance = NULL;
	eav->view_menus = NULL;
	eav->current_view = NULL;
	eav->uic = NULL;

	eav->book = NULL;
	eav->query = NULL;

	eav->invisible = NULL;
	eav->clipboard_contacts = NULL;
}

static void
eab_view_dispose (GObject *object)
{
	EABView *eav = EAB_VIEW(object);

	if (eav->model) {
		g_object_unref (eav->model);
		eav->model = NULL;
	}

	if (eav->book) {
		g_object_unref (eav->book);
		eav->book = NULL;
	}

	if (eav->query) {
		g_free(eav->query);
		eav->query = NULL;
	}

	eav->uic = NULL;

	if (eav->view_instance) {
		g_object_unref (eav->view_instance);
		eav->view_instance = NULL;
	}

	if (eav->view_menus) {
		g_object_unref (eav->view_menus);
		eav->view_menus = NULL;
	}

	if (eav->clipboard_contacts) {
		g_list_foreach (eav->clipboard_contacts, (GFunc)g_object_unref, NULL);
		g_list_free (eav->clipboard_contacts);
		eav->clipboard_contacts = NULL;
	}
		
	if (eav->invisible) {
		gtk_widget_destroy (eav->invisible);
		eav->invisible = NULL;
	}

	if (G_OBJECT_CLASS(parent_class)->dispose)
		G_OBJECT_CLASS(parent_class)->dispose(object);
}

GtkWidget*
eab_view_new (void)
{
	GtkWidget *widget = GTK_WIDGET (g_object_new (E_TYPE_AB_VIEW, NULL));
	EABView *eav = EAB_VIEW (widget);

	/* create our model */
	eav->model = eab_model_new ();

	g_signal_connect (eav->model, "status_message",
			  G_CALLBACK (status_message), eav);
	g_signal_connect (eav->model, "search_result",
			  G_CALLBACK (search_result), eav);
	g_signal_connect (eav->model, "folder_bar_message",
			  G_CALLBACK (folder_bar_message), eav);
	g_signal_connect (eav->model, "stop_state_changed",
			  G_CALLBACK (stop_state_changed), eav);
	g_signal_connect (eav->model, "writable_status",
			  G_CALLBACK (writable_status), eav);
	g_signal_connect (eav->model, "backend_died",
			  G_CALLBACK (backend_died), eav);

	eav->editable = FALSE;
	eav->query = g_strdup (SHOW_ALL_SEARCH);

	/* create the paned window and contact display */
	eav->paned = gtk_vpaned_new ();
	gtk_container_add (GTK_CONTAINER (eav), eav->paned);

	eav->widget = gtk_label_new ("empty label here");
	gtk_container_add (GTK_CONTAINER (eav->paned), eav->widget);
	gtk_widget_show (eav->widget);

	eav->scrolled = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (eav->scrolled), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (eav->scrolled), GTK_SHADOW_IN);
	eav->contact_display = eab_contact_display_new ();

	gtk_scrolled_window_add_with_viewport (GTK_SCROLLED_WINDOW (eav->scrolled), eav->contact_display);
	gtk_widget_show (eav->contact_display);

	gtk_container_add (GTK_CONTAINER (eav->paned), eav->scrolled);
	gtk_widget_show (eav->scrolled);
	gtk_widget_show (eav->paned);

	/* XXX hack */
	gtk_paned_set_position (GTK_PANED (eav->paned), 144);

	/* gtk selection crap */
	eav->invisible = gtk_invisible_new ();

	gtk_selection_add_target (eav->invisible,
				  clipboard_atom,
				  GDK_SELECTION_TYPE_STRING,
				  0);
		
	g_signal_connect (eav->invisible, "selection_get",
			  G_CALLBACK (selection_get), 
			  eav);
	g_signal_connect (eav->invisible, "selection_clear_event",
			  G_CALLBACK (selection_clear_event),
			  eav);
	g_signal_connect (eav->invisible, "selection_received",
			  G_CALLBACK (selection_received),
			  eav);
	g_object_weak_ref (G_OBJECT (eav->invisible), invisible_destroyed, eav);

	return widget;
}

static void
writable_status (GtkObject *object, gboolean writable, EABView *eav)
{
	eav->editable = writable;
	command_state_change (eav);
}

static void
init_collection (void)
{
	GalViewFactory *factory;
	ETableSpecification *spec;
	char *galview;

	if (collection == NULL) {
		collection = gal_view_collection_new();

		gal_view_collection_set_title (collection, _("Addressbook"));

		galview = gnome_util_prepend_user_home("/evolution/views/addressbook/");
		gal_view_collection_set_storage_directories
			(collection,
			 EVOLUTION_GALVIEWSDIR "/addressbook/",
			 galview);
		g_free(galview);

		spec = e_table_specification_new();
		e_table_specification_load_from_file (spec, EVOLUTION_ETSPECDIR "/e-addressbook-view.etspec");

		factory = gal_view_factory_etable_new (spec);
		g_object_unref (spec);
		gal_view_collection_add_factory (collection, factory);
		g_object_unref (factory);

#ifdef WITH_ADDRESSBOOK_VIEW_TREEVIEW
		factory = gal_view_factory_treeview_new ();
		gal_view_collection_add_factory (collection, factory);
		g_object_unref (factory);
#endif

		gal_view_collection_load(collection);
	}
}

static void
display_view(GalViewInstance *instance,
	     GalView *view,
	     gpointer data)
{
	EABView *address_view = data;
	if (GAL_IS_VIEW_ETABLE(view)) {
		change_view_type (address_view, EAB_VIEW_TABLE);
		gal_view_etable_attach_table (GAL_VIEW_ETABLE(view), e_table_scrolled_get_table(E_TABLE_SCROLLED(address_view->widget)));
	}
#ifdef WITH_ADDRESSBOOK_VIEW_TREEVIEW
	else if (GAL_IS_VIEW_TREEVIEW (view)) {
		change_view_type (address_view, EAB_VIEW_TREEVIEW);
		gal_view_treeview_attach (GAL_VIEW_TREEVIEW(view), GTK_TREE_VIEW (address_view->object));
	}
#endif
	address_view->current_view = view;
}

static void
setup_menus (EABView *view)
{
	if (view->book && view->view_instance == NULL) {
		init_collection ();
		view->view_instance = gal_view_instance_new (collection, e_book_get_uri (view->book));
	}

	if (view->view_instance && view->uic) {
		view->view_menus = gal_view_menus_new(view->view_instance);
		gal_view_menus_apply(view->view_menus, view->uic, NULL);

		display_view (view->view_instance, gal_view_instance_get_current_view (view->view_instance), view);

		g_signal_connect(view->view_instance, "display_view",
				 G_CALLBACK (display_view), view);
	}
}

static void
eab_view_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	EABView *eav = EAB_VIEW(object);

	switch (prop_id){
	case PROP_BOOK:
		if (eav->book) {
			g_object_unref (eav->book);
		}
		if (g_value_get_object (value)) {
			eav->book = E_BOOK(g_value_get_object (value));
			g_object_ref (eav->book);
		}
		else
			eav->book = NULL;

		if (eav->view_instance) {
			g_object_unref (eav->view_instance);
			eav->view_instance = NULL;
		}

		g_object_set(eav->model,
			     "book", eav->book,
			     NULL);

		setup_menus (eav);

		break;
	case PROP_QUERY:
#if 0 /* This code will mess up ldap a bit.  We need to think about the ramifications of this more. */
		if ((g_value_get_string (value) == NULL && !strcmp (eav->query, SHOW_ALL_SEARCH)) ||
		    (g_value_get_string (value) != NULL && !strcmp (eav->query, g_value_get_string (value))))
			break;
#endif
		g_free(eav->query);
		eav->query = g_strdup(g_value_get_string (value));
		if (!eav->query)
			eav->query = g_strdup (SHOW_ALL_SEARCH);
		g_object_set(eav->model,
			     "query", eav->query,
			     NULL);
		break;
	case PROP_TYPE:
		change_view_type(eav, g_value_get_int (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
eab_view_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	EABView *eav = EAB_VIEW(object);

	switch (prop_id) {
	case PROP_BOOK:
		if (eav->book)
			g_value_set_object (value, eav->book);
		else
			g_value_set_object (value, NULL);
		break;
	case PROP_QUERY:
		g_value_set_string (value, eav->query);
		break;
	case PROP_TYPE:
		g_value_set_int (value, eav->view_type);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static ESelectionModel*
get_selection_model (EABView *view)
{
	if (view->view_type == EAB_VIEW_TABLE)
		return e_table_get_selection_model (e_table_scrolled_get_table (E_TABLE_SCROLLED(view->widget)));
#ifdef WITH_ADDRESSBOOK_VIEW_TREEVIEW
	else if (view->view_type == EAB_VIEW_TREEVIEW) {
		return e_treeview_get_selection_model (GTK_TREE_VIEW (view->object));
	}
#endif
	g_return_val_if_reached (NULL);
}

/* Popup menu stuff */
typedef struct {
	EABView *view;
	EPopupMenu *submenu;
	gpointer closure;
} ContactAndBook;

static ESelectionModel*
contact_and_book_get_selection_model (ContactAndBook *contact_and_book)
{
	return get_selection_model (contact_and_book->view);
}

static void
contact_and_book_free (ContactAndBook *contact_and_book)
{
	EABView *view = contact_and_book->view;
	ESelectionModel *selection;

	if (contact_and_book->submenu)
		gal_view_instance_free_popup_menu (view->view_instance,
						   contact_and_book->submenu);

	selection = contact_and_book_get_selection_model (contact_and_book);
	if (selection)
		e_selection_model_right_click_up(selection);

	g_object_unref (view);
}

static void
get_contact_list_1(gint model_row,
		   gpointer closure)
{
	ContactAndBook *contact_and_book;
	GList **list;
	EABView *view;
	EContact *contact;

	contact_and_book = closure;
	list = contact_and_book->closure;
	view = contact_and_book->view;

	contact = eab_model_get_contact(view->model, model_row);
	*list = g_list_prepend(*list, contact);
}

static GList *
get_contact_list (ContactAndBook *contact_and_book)
{
	GList *list = NULL;
	ESelectionModel *selection;

	selection = contact_and_book_get_selection_model (contact_and_book);

	if (selection) {
		contact_and_book->closure = &list;
		e_selection_model_foreach (selection, get_contact_list_1, contact_and_book);
	}

	return list;
}

static void
has_email_address_1(gint model_row,
			  gpointer closure)
{
	ContactAndBook *contact_and_book;
	gboolean *has_email;
	EABView *view;
	const EContact *contact;
	GList *email;

	contact_and_book = closure;
	has_email = contact_and_book->closure;
	view = contact_and_book->view;

	if (*has_email)
		return;

	contact = eab_model_contact_at(view->model, model_row);

	email = e_contact_get (E_CONTACT (contact), E_CONTACT_EMAIL);

	if (g_list_length (email) > 0)
		*has_email = TRUE;

	g_list_foreach (email, (GFunc)g_free, NULL);
	g_list_free (email);
}

static gboolean
get_has_email_address (ContactAndBook *contact_and_book)
{
	ESelectionModel *selection;
	gboolean has_email = FALSE;

	selection = contact_and_book_get_selection_model (contact_and_book);

	if (selection) {
		contact_and_book->closure = &has_email;
		e_selection_model_foreach (selection, has_email_address_1, contact_and_book);
	}

	return has_email;
}

static void
save_as (GtkWidget *widget, ContactAndBook *contact_and_book)
{
	GList *contacts = get_contact_list (contact_and_book);
	if (contacts) {
		eab_contact_list_save(_("Save as VCard"), contacts, NULL);
		e_free_object_list(contacts);
	}
}

static void
send_as (GtkWidget *widget, ContactAndBook *contact_and_book)
{
	GList *contacts = get_contact_list (contact_and_book);
	if (contacts) {
		eab_send_contact_list(contacts, EAB_DISPOSITION_AS_ATTACHMENT);
		e_free_object_list(contacts);
	}
}

static void
send_to (GtkWidget *widget, ContactAndBook *contact_and_book)

{
	GList *contacts = get_contact_list (contact_and_book);

	if (contacts) {
		eab_send_contact_list(contacts, EAB_DISPOSITION_AS_TO);
		e_free_object_list(contacts);
	}
}

static void
print (GtkWidget *widget, ContactAndBook *contact_and_book)
{
	GList *contacts = get_contact_list (contact_and_book);
	if (contacts) {
		if (contacts->next)
			gtk_widget_show(e_contact_print_contact_list_dialog_new(contacts));
		else
			gtk_widget_show(e_contact_print_contact_dialog_new(contacts->data));
		e_free_object_list(contacts);
	}
}

#if 0 /* Envelope printing is disabled for Evolution 1.0. */
static void
print_envelope (GtkWidget *widget, ContactAndBook *contact_and_book)
{
	GList *cards = get_card_list (contact_and_book);
	if (cards) {
		gtk_widget_show(e_contact_list_print_envelope_dialog_new(contact_and_book->card));
		e_free_object_list(cards);
	}
}
#endif

static void
copy (GtkWidget *widget, ContactAndBook *contact_and_book)
{
	eab_view_copy (contact_and_book->view);
}

static void
paste (GtkWidget *widget, ContactAndBook *contact_and_book)
{
	eab_view_paste (contact_and_book->view);
}

static void
cut (GtkWidget *widget, ContactAndBook *contact_and_book)
{
	eab_view_cut (contact_and_book->view);
}

static void
delete (GtkWidget *widget, ContactAndBook *contact_and_book)
{
	if (e_contact_editor_confirm_delete(GTK_WINDOW(gtk_widget_get_toplevel(contact_and_book->view->widget)))) {
		EBook *book;
		GList *list = get_contact_list(contact_and_book);
		GList *iterator;
		gboolean bulk_remove = FALSE;

		bulk_remove = e_book_check_static_capability (contact_and_book->view->model->book,
							      "bulk-remove");

		g_object_get(contact_and_book->view->model,
			     "book", &book,
			     NULL);

		if (bulk_remove) {
			GList *ids = NULL;

			for (iterator = list; iterator; iterator = iterator->next) {
				EContact *contact = iterator->data;
				ids = g_list_prepend (ids, (char*)e_contact_get_const (contact, E_CONTACT_UID));
			}

			/* Remove the cards all at once. */
			/* XXX no callback specified... ugh */
			e_book_async_remove_contacts (book,
						      ids,
						      NULL,
						      NULL);
			
			g_list_free (ids);
		}
		else {
			for (iterator = list; iterator; iterator = iterator->next) {
				EContact *contact = iterator->data;
				/* Remove the card. */
				/* XXX no callback specified... ugh */
				e_book_async_remove_contact (book,
							     e_contact_get_const (contact, E_CONTACT_UID),
							     NULL,
							     NULL);
			}
		}
		e_free_object_list(list);
	}
}

static void
copy_to_folder (GtkWidget *widget, ContactAndBook *contact_and_book)
{
	eab_view_copy_to_folder (contact_and_book->view);
}

static void
move_to_folder (GtkWidget *widget, ContactAndBook *contact_and_book)
{
	eab_view_move_to_folder (contact_and_book->view);
}

static void
free_popup_info (GtkWidget *w, ContactAndBook *contact_and_book)
{
	contact_and_book_free (contact_and_book);
}

static void
new_card (GtkWidget *widget, ContactAndBook *contact_and_book)
{
	EBook *book;

	g_object_get(contact_and_book->view->model,
		     "book", &book,
		     NULL);
	eab_show_contact_editor (book, e_contact_new(), TRUE, TRUE);
}

static void
new_list (GtkWidget *widget, ContactAndBook *contact_and_book)
{
	EBook *book;

	g_object_get(contact_and_book->view->model,
		     "book", &book,
		     NULL);
	eab_show_contact_list_editor (book, e_contact_new(), TRUE, TRUE);
}

#if 0
static void
sources (GtkWidget *widget, ContactAndBook *contact_and_book)
{
	BonoboControl *control;
	GNOME_Evolution_ShellView shell_view;
	CORBA_Environment ev;

	control = g_object_get_data (G_OBJECT (gcal), "control");
	if (control == NULL)
		return;

	shell_view = get_shell_view_interface (control);
	if (shell_view == CORBA_OBJECT_NIL)
		return;

	CORBA_exception_init (&ev);
	
	GNOME_Evolution_ShellView_showSettings (shell_view, &ev);
	
	if (BONOBO_EX (&ev))
		g_message ("control_util_show_settings(): Could not show settings");

	CORBA_exception_free (&ev);
}
#endif

#define POPUP_READONLY_MASK 0x1
#define POPUP_NOSELECTION_MASK 0x2
#define POPUP_NOEMAIL_MASK 0x4

static void
do_popup_menu(EABView *view, GdkEvent *event)
{
	ContactAndBook *contact_and_book;
	GtkMenu *popup;
	EPopupMenu *submenu = NULL;
	ESelectionModel *selection_model;
	gboolean selection = FALSE;

	EPopupMenu menu[] = {
		E_POPUP_ITEM (N_("New Contact..."), G_CALLBACK(new_card), POPUP_READONLY_MASK),
		E_POPUP_ITEM (N_("New Contact List..."), G_CALLBACK(new_list), POPUP_READONLY_MASK),
		E_POPUP_SEPARATOR,
#if 0
		E_POPUP_ITEM (N_("Go to Folder..."), G_CALLBACK (goto_folder), 0),
		E_POPUP_ITEM (N_("Import..."), G_CALLBACK (import), POPUP_READONLY_MASK),
		E_POPUP_SEPARATOR,
		E_POPUP_ITEM (N_("Search for Contacts..."), G_CALLBACK (search), 0),
		E_POPUP_ITEM (N_("Addressbook Sources..."), G_CALLBACK (sources), 0),
		E_POPUP_SEPARATOR,
		E_POPUP_ITEM (N_("Pilot Settings..."), G_CALLBACK (pilot_settings), 0),
#endif
		E_POPUP_SEPARATOR,
		E_POPUP_ITEM (N_("Save as VCard"), G_CALLBACK(save_as), POPUP_NOSELECTION_MASK),
		E_POPUP_ITEM (N_("Forward Contact"), G_CALLBACK(send_as), POPUP_NOSELECTION_MASK),
		E_POPUP_ITEM (N_("Send Message to Contact"), G_CALLBACK(send_to), POPUP_NOSELECTION_MASK | POPUP_NOEMAIL_MASK),
		E_POPUP_ITEM (N_("Print"), G_CALLBACK(print), POPUP_NOSELECTION_MASK),
#if 0 /* Envelope printing is disabled for Evolution 1.0. */
		E_POPUP_ITEM (N_("Print Envelope"), G_CALLBACK(print_envelope), POPUP_NOSELECTION_MASK),
#endif
		E_POPUP_SEPARATOR,

		E_POPUP_ITEM (N_("Copy to folder..."), G_CALLBACK(copy_to_folder), POPUP_NOSELECTION_MASK), 
		E_POPUP_ITEM (N_("Move to folder..."), G_CALLBACK(move_to_folder), POPUP_READONLY_MASK | POPUP_NOSELECTION_MASK),
		E_POPUP_SEPARATOR,

		E_POPUP_ITEM (N_("Cut"), G_CALLBACK (cut), POPUP_READONLY_MASK | POPUP_NOSELECTION_MASK),
		E_POPUP_ITEM (N_("Copy"), G_CALLBACK (copy), POPUP_NOSELECTION_MASK),
		E_POPUP_ITEM (N_("Paste"), G_CALLBACK (paste), POPUP_READONLY_MASK),
		E_POPUP_ITEM (N_("Delete"), G_CALLBACK(delete), POPUP_READONLY_MASK | POPUP_NOSELECTION_MASK),
		E_POPUP_SEPARATOR,

#if 0
		E_POPUP_SUBMENU (N_("Current View"), submenu = gal_view_instance_get_popup_menu (view->view_instance), 0),
#endif
		E_POPUP_TERMINATOR
	};

	contact_and_book = g_new(ContactAndBook, 1);
	contact_and_book->view = view;
	contact_and_book->submenu = submenu;

	g_object_ref (contact_and_book->view);

	selection_model = contact_and_book_get_selection_model (contact_and_book);
	if (selection_model)
		selection = e_selection_model_selected_count (selection_model) > 0;

	popup = e_popup_menu_create (menu,
				     0,
				     (eab_model_editable (view->model) ? 0 : POPUP_READONLY_MASK) +
				     (selection ? 0 : POPUP_NOSELECTION_MASK) +
				     (get_has_email_address (contact_and_book) ? 0 : POPUP_NOEMAIL_MASK),
				     contact_and_book);

	g_signal_connect (popup, "selection-done",
			  G_CALLBACK (free_popup_info), contact_and_book);
	e_popup_menu (popup, event);

}

static void
render_contact (int row, EABView *view)
{
	EContact *contact = eab_model_get_contact (view->model, row);

	eab_contact_display_render (EAB_CONTACT_DISPLAY (view->contact_display), contact,
				    EAB_CONTACT_DISPLAY_RENDER_NORMAL);
}

static void
selection_changed (GObject *o, EABView *view)
{
	ESelectionModel *selection_model;

	command_state_change (view);

	selection_model = get_selection_model (view);

	if (e_selection_model_selected_count (selection_model) == 1)
		e_selection_model_foreach (selection_model,
					   (EForeachFunc)render_contact, view);
	else
		eab_contact_display_render (EAB_CONTACT_DISPLAY (view->contact_display), NULL,
					    EAB_CONTACT_DISPLAY_RENDER_NORMAL);
					    
}

static void
table_double_click(ETableScrolled *table, gint row, gint col, GdkEvent *event, EABView *view)
{
	if (E_IS_ADDRESSBOOK_TABLE_ADAPTER(view->object)) {
		EABModel *model = view->model;
		EContact *contact = eab_model_get_contact (model, row);
		EBook *book;

		g_object_get(model,
			     "book", &book,
			     NULL);
		
		g_assert (E_IS_BOOK (book));

		if (e_contact_get (contact, E_CONTACT_IS_LIST))
			eab_show_contact_list_editor (book, contact, FALSE, view->editable);
		else
			eab_show_contact_editor (book, contact, FALSE, view->editable);
	}
}

static gint
table_right_click(ETableScrolled *table, gint row, gint col, GdkEvent *event, EABView *view)
{
	do_popup_menu(view, event);
	return TRUE;
}

static gint
table_white_space_event(ETableScrolled *table, GdkEvent *event, EABView *view)
{
	if (event->type == GDK_BUTTON_PRESS && ((GdkEventButton *)event)->button == 3) {
		do_popup_menu(view, event);
		return TRUE;
	} else {
		return FALSE;
	}
}

static void
table_drag_data_get (ETable             *table,
		     int                 row,
		     int                 col,
		     GdkDragContext     *context,
		     GtkSelectionData   *selection_data,
		     guint               info,
		     guint               time,
		     gpointer            user_data)
{
	EABView *view = user_data;

	if (!E_IS_ADDRESSBOOK_TABLE_ADAPTER(view->object))
		return;

	switch (info) {
	case DND_TARGET_TYPE_VCARD: {
		char *value;

		value = e_vcard_to_string (E_VCARD (view->model->data[row]), EVC_FORMAT_VCARD_30);

		gtk_selection_data_set (selection_data,
					selection_data->target,
					8,
					value, strlen (value));
		break;
	}
	}
}

static void
emit_status_message (EABView *eav, const gchar *status)
{
	g_signal_emit (eav,
		       eab_view_signals [STATUS_MESSAGE], 0,
		       status);
}

static void
emit_search_result (EABView *eav, EBookViewStatus status)
{
	g_signal_emit (eav,
		       eab_view_signals [SEARCH_RESULT], 0,
		       status);
}

static void
emit_folder_bar_message (EABView *eav, const gchar *message)
{
	g_signal_emit (eav,
		       eab_view_signals [FOLDER_BAR_MESSAGE], 0,
		       message);
}

static void
status_message (GtkObject *object, const gchar *status, EABView *eav)
{
	emit_status_message (eav, status);
}

static void
search_result (GtkObject *object, EBookViewStatus status, EABView *eav)
{
	emit_search_result (eav, status);
}

static void
folder_bar_message (GtkObject *object, const gchar *status, EABView *eav)
{
	emit_folder_bar_message (eav, status);
}

static void
stop_state_changed (GtkObject *object, EABView *eav)
{
	command_state_change (eav);
}

static void
command_state_change (EABView *eav)
{
	/* Reffing during emission is unnecessary.  Gtk automatically refs during an emission. */
	g_signal_emit (eav, eab_view_signals [COMMAND_STATE_CHANGE], 0);
}

static void
backend_died (GtkObject *object, EABView *eav)
{
	char *message = g_strdup_printf (_("The addressbook backend for\n%s\nhas crashed. "
					   "You will have to restart Evolution in order "
					   "to use it again"),
					 e_book_get_uri (eav->book));
        gnome_error_dialog_parented (message, GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (eav))));
        g_free (message);
}

static void
create_table_view (EABView *view)
{
	ETableModel *adapter;
	GtkWidget *table;
	
	adapter = eab_table_adapter_new(view->model);

	/* Here we create the table.  We give it the three pieces of
	   the table we've created, the header, the model, and the
	   initial layout.  It does the rest.  */
	table = e_table_scrolled_new_from_spec_file (adapter, NULL, EVOLUTION_ETSPECDIR "/e-addressbook-view.etspec", NULL);

	view->object = G_OBJECT(adapter);
	view->widget = table;

	g_signal_connect(e_table_scrolled_get_table(E_TABLE_SCROLLED(table)), "double_click",
			 G_CALLBACK(table_double_click), view);
	g_signal_connect(e_table_scrolled_get_table(E_TABLE_SCROLLED(table)), "right_click",
			 G_CALLBACK(table_right_click), view);
	g_signal_connect(e_table_scrolled_get_table(E_TABLE_SCROLLED(table)), "white_space_event",
			 G_CALLBACK(table_white_space_event), view);
	g_signal_connect(e_table_scrolled_get_table(E_TABLE_SCROLLED(table)), "selection_change",
			 G_CALLBACK(selection_changed), view);

	/* drag & drop signals */
	e_table_drag_source_set (E_TABLE(E_TABLE_SCROLLED(table)->table), GDK_BUTTON1_MASK,
				 drag_types, num_drag_types, GDK_ACTION_MOVE);
	
	g_signal_connect (E_TABLE_SCROLLED(table)->table,
			  "table_drag_data_get",
			  G_CALLBACK (table_drag_data_get),
			  view);

	gtk_paned_add1 (GTK_PANED (view->paned), table);

	gtk_widget_show( GTK_WIDGET(table) );
}

#ifdef WITH_ADDRESSBOOK_VIEW_TREEVIEW
static void
treeview_row_activated(GtkTreeView *treeview,
		       GtkTreePath *path, GtkTreeViewColumn *column,
		       EABView *view)
{
	EABModel *model = view->model;
	int row = gtk_tree_path_get_indices (path)[0];
	ECard *card = eab_model_get_card(model, row);
	EBook *book;

	g_object_get(model,
		     "book", &book,
		     NULL);
		
	g_assert (E_IS_BOOK (book));

	if (e_card_evolution_list (card))
		eab_show_contact_list_editor (book, card, FALSE, view->editable);
	else
		eab_show_contact_editor (book, card, FALSE, view->editable);
}

static void
create_treeview_view (EABView *view)
{
	GtkTreeModel *adapter;
	ECardSimple *simple;
	GtkWidget *treeview;
	GtkWidget *scrolled;
	int i;

	simple = e_card_simple_new(NULL);

	adapter = eab_treeview_adapter_new(view->model);

	scrolled = gtk_scrolled_window_new (NULL, NULL);
	treeview = gtk_tree_view_new_with_model (adapter);

	gtk_widget_show (treeview);

	gtk_container_add (GTK_CONTAINER (scrolled), treeview);

	for (i = 0; i < 15; i ++) {
		GtkTreeViewColumn *column =
			gtk_tree_view_column_new_with_attributes (e_card_simple_get_name (simple, i),
								  gtk_cell_renderer_text_new (),
								  "text", i,
								  NULL);

		gtk_tree_view_append_column (GTK_TREE_VIEW (treeview), column);
	}

	view->object = G_OBJECT(treeview);
	view->widget = scrolled;

	gtk_tree_selection_set_mode (gtk_tree_view_get_selection (GTK_TREE_VIEW (treeview)), GTK_SELECTION_MULTIPLE);
	gtk_tree_view_enable_model_drag_source (GTK_TREE_VIEW (treeview), 
						GDK_BUTTON1_MASK,
						drag_types,
						num_drag_types,
						GDK_ACTION_MOVE);

	g_signal_connect(treeview, "row_activated",
			 G_CALLBACK (treeview_row_activated), view);
#if 0
	g_signal_connect(e_table_scrolled_get_table(E_TABLE_SCROLLED(table)), "right_click",
			 G_CALLBACK(table_right_click), view);

	/* drag & drop signals */
	e_table_drag_source_set (E_TABLE(E_TABLE_SCROLLED(table)->table), GDK_BUTTON1_MASK,
				 drag_types, num_drag_types, GDK_ACTION_MOVE);
	
	g_signal_connect (E_TABLE_SCROLLED(table)->table,
			  "table_drag_data_get",
			  G_CALLBACK (table_drag_data_get),
			  view);
#endif


	g_signal_connect(e_treeview_get_selection_model (GTK_TREE_VIEW (treeview)), "selection_changed",
			 G_CALLBACK(selection_changed), view);

	gtk_paned_add1 (GTK_PANED (view->paned), scrolled);

	gtk_widget_show( GTK_WIDGET(scrolled) );

	g_object_unref (simple);
}
#endif

static void
change_view_type (EABView *view, EABViewType view_type)
{
	if (view_type == view->view_type)
		return;

	if (view->widget) {
		gtk_container_remove (GTK_CONTAINER (view->paned), view->widget);
		gtk_widget_destroy (view->widget);
		view->widget = NULL;
	}
	view->object = NULL;

	switch (view_type) {
	case EAB_VIEW_TABLE:
		create_table_view (view);
		break;
#ifdef WITH_ADDRESSBOOK_VIEW_TREEVIEW
	case EAB_VIEW_TREEVIEW:
		create_treeview_view (view);
		break;
#endif
	default:
		g_warning ("view_type not recognized.");
		return;
	}

	view->view_type = view_type;

	command_state_change (view);
}

typedef struct {
	GtkWidget *table;
	GObject *printable;
} EContactPrintDialogWeakData;

static void
e_contact_print_destroy(gpointer data, GObject *where_object_was)
{
	EContactPrintDialogWeakData *weak_data = data;
	g_object_unref (weak_data->printable);
	g_object_unref (weak_data->table);
	g_free (weak_data);
}

static void
e_contact_print_button(GtkDialog *dialog, gint response, gpointer data)
{
	GnomePrintJob *master;
	GnomePrintContext *pc;
	EPrintable *printable = g_object_get_data(G_OBJECT(dialog), "printable");
	GtkWidget *preview;
	switch( response ) {
	case GNOME_PRINT_DIALOG_RESPONSE_PRINT:
		master = gnome_print_job_new(gnome_print_dialog_get_config ( GNOME_PRINT_DIALOG(dialog) ));
		pc = gnome_print_job_get_context( master );
		e_printable_reset(printable);
		while (e_printable_data_left(printable)) {
			if (gnome_print_gsave(pc) == -1)
				/* FIXME */;
			if (gnome_print_translate(pc, 72, 72) == -1)
				/* FIXME */;
			e_printable_print_page(printable,
					       pc,
					       6.5 * 72,
					       5 * 72,
					       TRUE);
			if (gnome_print_grestore(pc) == -1)
				/* FIXME */;
			if (gnome_print_showpage(pc) == -1)
				/* FIXME */;
		}
		gnome_print_job_close(master);
		gnome_print_job_print(master);
		g_object_unref (master);
		gtk_widget_destroy((GtkWidget *)dialog);
		break;
	case GNOME_PRINT_DIALOG_RESPONSE_PREVIEW:
		master = gnome_print_job_new (gnome_print_dialog_get_config ( GNOME_PRINT_DIALOG(dialog) ));
		pc = gnome_print_job_get_context( master );
		e_printable_reset(printable);
		while (e_printable_data_left(printable)) {
			if (gnome_print_gsave(pc) == -1)
				/* FIXME */;
			if (gnome_print_translate(pc, 72, 72) == -1)
				/* FIXME */;
			e_printable_print_page(printable,
					       pc,
					       6.5 * 72,
					       9 * 72,
					       TRUE);
			if (gnome_print_grestore(pc) == -1)
				/* FIXME */;
			if (gnome_print_showpage(pc) == -1)
				/* FIXME */;
		}
		gnome_print_job_close(master);
		preview = GTK_WIDGET(gnome_print_job_preview_new(master, "Print Preview"));
		gtk_widget_show_all(preview);
		g_object_unref (master);
		break;
	case GNOME_PRINT_DIALOG_RESPONSE_CANCEL:
	default:
		gtk_widget_destroy((GtkWidget *)dialog);
		break;
	}
}

void
eab_view_setup_menus (EABView *view,
				BonoboUIComponent *uic)
{

	g_return_if_fail (view != NULL);
	g_return_if_fail (E_IS_ADDRESSBOOK_VIEW (view));
	g_return_if_fail (uic != NULL);
	g_return_if_fail (BONOBO_IS_UI_COMPONENT (uic));

	init_collection ();

	view->uic = uic;

	setup_menus (view);
}

/**
 * eab_view_discard_menus:
 * @view: An addressbook view.
 * 
 * Makes an addressbook view discard its GAL view menus and its views instance
 * objects.  This should be called when the corresponding Bonobo component is
 * deactivated.
 **/
void
eab_view_discard_menus (EABView *view)
{
	g_return_if_fail (view != NULL);
	g_return_if_fail (E_IS_ADDRESSBOOK_VIEW (view));
	g_return_if_fail (view->view_instance);

	if (view->view_menus) {
		gal_view_menus_unmerge (view->view_menus, NULL);

		g_object_unref (view->view_menus);
		view->view_menus = NULL;
	}

	if (view->view_instance) {
		g_object_unref (view->view_instance);
		view->view_instance = NULL;
	}

	view->uic = NULL;
}

void
eab_view_print(EABView *view)
{
	if (view->view_type == EAB_VIEW_TABLE) {
		GtkWidget *dialog;
		EPrintable *printable;
		ETable *etable;
		EContactPrintDialogWeakData *weak_data;

		dialog = gnome_print_dialog_new(NULL, "Print cards", GNOME_PRINT_DIALOG_RANGE | GNOME_PRINT_DIALOG_COPIES);
		gnome_print_dialog_construct_range_any(GNOME_PRINT_DIALOG(dialog), GNOME_PRINT_RANGE_ALL | GNOME_PRINT_RANGE_SELECTION,
						       NULL, NULL, NULL);

		g_object_get(view->widget, "table", &etable, NULL);
		printable = e_table_get_printable(etable);

		g_object_ref (view->widget);

		g_object_set_data (G_OBJECT (dialog), "table", view->widget);
		g_object_set_data (G_OBJECT (dialog), "printable", printable);
		
		g_signal_connect(dialog,
				 "response", G_CALLBACK(e_contact_print_button), NULL);

		weak_data = g_new (EContactPrintDialogWeakData, 1);

		weak_data->table = view->widget;
		weak_data->printable = G_OBJECT (printable);

		g_object_weak_ref (G_OBJECT (dialog), e_contact_print_destroy, weak_data);

		gtk_widget_show(dialog);
	}
#ifdef WITH_ADDRESSBOOK_VIEW_TREEVIEW
	else if (view->view_type == EAB_VIEW_TREEVIEW) {
		/* XXX */
	}
#endif
}

void
eab_view_print_preview(EABView *view)
{
	if (view->view_type == EAB_VIEW_TABLE) {
		EPrintable *printable;
		ETable *etable;
		GnomePrintJob *master;
		GnomePrintContext *pc;
		GnomePrintConfig *config;
		GtkWidget *preview;

		g_object_get(view->widget, "table", &etable, NULL);
		printable = e_table_get_printable(etable);

		master = gnome_print_job_new(NULL);
		config = gnome_print_job_get_config (master);
		gnome_print_config_set_int (config, GNOME_PRINT_KEY_NUM_COPIES, 1);
		pc = gnome_print_job_get_context( master );
		e_printable_reset(printable);
		while (e_printable_data_left(printable)) {
			if (gnome_print_gsave(pc) == -1)
				/* FIXME */;
			if (gnome_print_translate(pc, 72, 72) == -1)
				/* FIXME */;
			e_printable_print_page(printable,
					       pc,
					       6.5 * 72,
					       9 * 72,
					       TRUE);
			if (gnome_print_grestore(pc) == -1)
				/* FIXME */;
			if (gnome_print_showpage(pc) == -1)
				/* FIXME */;
		}
		gnome_print_job_close(master);
		preview = GTK_WIDGET(gnome_print_job_preview_new(master, "Print Preview"));
		gtk_widget_show_all(preview);
		g_object_unref (master);
		g_object_unref (printable);
	}
#ifdef WITH_ADDRESSBOOK_VIEW_TREEVIEW
	else if (view->view_type == EAB_VIEW_TREEVIEW) {
		/* XXX */
	}
#endif
}

void
eab_view_delete_selection(EABView *view)
{
	ContactAndBook contact_and_book;

	memset (&contact_and_book, 0, sizeof (contact_and_book));
	contact_and_book.view = view;

	delete (GTK_WIDGET (view), &contact_and_book);
}

static void
invisible_destroyed (gpointer data, GObject *where_object_was)
{
	EABView *view = data;
	view->invisible = NULL;
}

static void
selection_get (GtkWidget *invisible,
	       GtkSelectionData *selection_data,
	       guint info,
	       guint time_stamp,
	       EABView *view)
{
	char *value;

	value = eab_contact_list_to_string (view->clipboard_contacts);

	gtk_selection_data_set (selection_data, GDK_SELECTION_TYPE_STRING,
				8, value, strlen (value));
				
}

static void
selection_clear_event (GtkWidget *invisible,
		       GdkEventSelection *event,
		       EABView *view)
{
	if (view->clipboard_contacts) {
		g_list_foreach (view->clipboard_contacts, (GFunc)g_object_unref, NULL);
		g_list_free (view->clipboard_contacts);
		view->clipboard_contacts = NULL;
	}
}

static void
selection_received (GtkWidget *invisible,
		    GtkSelectionData *selection_data,
		    guint time,
		    EABView *view)
{
	if (selection_data->length < 0 || selection_data->type != GDK_SELECTION_TYPE_STRING) {
		return;
	}
	else {
		/* XXX make sure selection_data->data = \0 terminated */
		GList *contact_list = eab_contact_list_from_string (selection_data->data);
		GList *l;
		
		for (l = contact_list; l; l = l->next) {
			EContact *contact = l->data;

			/* XXX NULL for a callback /sigh */
			eab_merging_book_add_contact (view->book, contact, NULL /* XXX */, NULL);
		}

		g_list_foreach (contact_list, (GFunc)g_object_unref, NULL);
		g_list_free (contact_list);
	}
}

static void
add_to_list (int model_row, gpointer closure)
{
	GList **list = closure;
	*list = g_list_prepend (*list, GINT_TO_POINTER (model_row));
}

static GList *
get_selected_contacts (EABView *view)
{
	GList *list;
	GList *iterator;
	ESelectionModel *selection = get_selection_model (view);

	list = NULL;
	e_selection_model_foreach (selection, add_to_list, &list);

	for (iterator = list; iterator; iterator = iterator->next) {
		iterator->data = eab_model_get_contact (view->model, GPOINTER_TO_INT (iterator->data));
	}
	list = g_list_reverse (list);
	return list;
}

void
eab_view_save_as (EABView *view)
{
	GList *list = get_selected_contacts (view);
	if (list)
		eab_contact_list_save (_("Save as VCard"), list, NULL);
	e_free_object_list(list);
}

void
eab_view_view (EABView *view)
{
	GList *list = get_selected_contacts (view);
	eab_show_multiple_contacts (view->book, list, view->editable);
	e_free_object_list(list);
}

void
eab_view_send (EABView *view)
{
	GList *list = get_selected_contacts (view);
	if (list)
		eab_send_contact_list (list, EAB_DISPOSITION_AS_ATTACHMENT);
	e_free_object_list(list);
}

void
eab_view_send_to (EABView *view)
{
	GList *list = get_selected_contacts (view);
	if (list)
		eab_send_contact_list (list, EAB_DISPOSITION_AS_TO);
	e_free_object_list(list);
}

void
eab_view_cut (EABView *view)
{
	eab_view_copy (view);
	eab_view_delete_selection (view);
}

void
eab_view_copy (EABView *view)
{
	view->clipboard_contacts = get_selected_contacts (view);

	gtk_selection_owner_set (view->invisible, clipboard_atom, GDK_CURRENT_TIME);
}

void
eab_view_paste (EABView *view)
{
	gtk_selection_convert (view->invisible, clipboard_atom,
			       GDK_SELECTION_TYPE_STRING,
			       GDK_CURRENT_TIME);
}

void
eab_view_select_all (EABView *view)
{
	ESelectionModel *model = get_selection_model (view);

	g_return_if_fail (model);

	e_selection_model_select_all (model);
}

void
eab_view_show_all(EABView *view)
{
	g_object_set(view,
		     "query", NULL,
		     NULL);
}

void
eab_view_stop(EABView *view)
{
	if (view)
		eab_model_stop (view->model);
}

static void
view_transfer_contacts (EABView *view, gboolean delete_from_source)
{
	EBook *book;
	GList *contacts;
	GtkWindow *parent_window;

	g_object_get(view->model, 
		     "book", &book,
		     NULL);
	contacts = get_selected_contacts (view);
	parent_window = GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (view)));

	eab_transfer_contacts (book, contacts, delete_from_source, parent_window);
}

void
eab_view_copy_to_folder (EABView *view)
{
	view_transfer_contacts (view, FALSE);
}

void
eab_view_move_to_folder (EABView *view)
{
	view_transfer_contacts (view, TRUE);
}


static gboolean
eab_view_selection_nonempty (EABView  *view)
{
	ESelectionModel *selection_model;

	selection_model = get_selection_model (view);
	if (selection_model == NULL)
		return FALSE;

	return e_selection_model_selected_count (selection_model) != 0;
}

gboolean
eab_view_can_create (EABView  *view)
{
	return view ? eab_model_editable (view->model) : FALSE;
}

gboolean
eab_view_can_print (EABView  *view)
{
	return view && view->model ? eab_model_contact_count (view->model) : FALSE;
}

gboolean
eab_view_can_save_as (EABView  *view)
{
	return view ? eab_view_selection_nonempty (view) : FALSE;
}

gboolean
eab_view_can_view (EABView  *view)
{
	return view ? eab_view_selection_nonempty (view) : FALSE;
}

gboolean 
eab_view_can_send (EABView  *view)
{
	return view ? eab_view_selection_nonempty (view) : FALSE;
}

gboolean   
eab_view_can_send_to (EABView  *view)
{
	return view ? eab_view_selection_nonempty (view) : FALSE;
}

gboolean
eab_view_can_delete (EABView  *view)
{
	return view ? eab_view_selection_nonempty (view) && eab_model_editable (view->model) : FALSE;
}

gboolean
eab_view_can_cut (EABView *view)
{
	return view ? eab_view_selection_nonempty (view) && eab_model_editable (view->model) : FALSE;
}

gboolean
eab_view_can_copy (EABView *view)
{
	return view ? eab_view_selection_nonempty (view) : FALSE;
}

gboolean
eab_view_can_paste (EABView *view)
{
	return view ? eab_model_editable (view->model) : FALSE;
}

gboolean
eab_view_can_select_all (EABView *view)
{
	return view ? eab_model_contact_count (view->model) != 0 : FALSE;
}

gboolean
eab_view_can_stop (EABView  *view)
{
	return view ? eab_model_can_stop (view->model) : FALSE;
}

gboolean
eab_view_can_copy_to_folder (EABView *view)
{
	return view ? eab_view_selection_nonempty (view) : FALSE;
}

gboolean
eab_view_can_move_to_folder (EABView *view)
{
	return view ? eab_view_selection_nonempty (view) && eab_model_editable (view->model) : FALSE;
}
