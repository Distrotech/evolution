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

#include <gtk/gtkinvisible.h>

#include <libgnome/gnome-paper.h>
#include <libgnome/gnome-i18n.h>
#include <libgnome/gnome-util.h>
#include <gal/e-table/e-table-scrolled.h>
#include <gal/e-table/e-table-model.h>
#include <gal/widgets/e-scroll-frame.h>
#include <gal/widgets/e-popup-menu.h>
#include <gal/widgets/e-gui-utils.h>
#include <gal/widgets/e-unicode.h>
#include <gal/menus/gal-view-factory-etable.h>
#include <gal/menus/gal-view-etable.h>
#include <gal/util/e-unicode-i18n.h>
#include <gal/util/e-xml-utils.h>
#include <gal/unicode/gunicode.h>
#include <libgnomeui/gnome-dialog-util.h>
#include <libgnomeui/gnome-stock.h>

#include <libgnomeprint/gnome-print.h>
#include <libgnomeprint/gnome-print-dialog.h>
#include <libgnomeprint/gnome-print-master.h>
#include <libgnomeprint/gnome-print-master-preview.h>

#include "addressbook/printing/e-contact-print.h"
#include "addressbook/printing/e-contact-print-envelope.h"

#include "gal-view-factory-minicard.h"
#include "gal-view-minicard.h"

#include "e-addressbook-view.h"
#include "e-addressbook-model.h"
#include "e-addressbook-util.h"
#include "e-addressbook-table-adapter.h"
#include "e-addressbook-reflow-adapter.h"
#include "e-minicard-view-widget.h"
#include "e-contact-save-as.h"
#include "e-card-merging.h"

#include "e-contact-editor.h"
#include <gdk/gdkkeysyms.h>
#include <ctype.h>

#include <gnome-xml/tree.h>
#include <gnome-xml/parser.h>

#define SHOW_ALL_SEARCH "(contains \"x-evolution-any-field\" \"\")"

#define d(x)

static void e_addressbook_view_init		(EAddressbookView		 *card);
static void e_addressbook_view_class_init	(EAddressbookViewClass	 *klass);
static void e_addressbook_view_set_arg (GtkObject *o, GtkArg *arg, guint arg_id);
static void e_addressbook_view_get_arg (GtkObject *object, GtkArg *arg, guint arg_id);
static void e_addressbook_view_destroy (GtkObject *object);
static void change_view_type (EAddressbookView *view, EAddressbookViewType view_type);

static void status_message     (GtkObject *object, const gchar *status, EAddressbookView *eav);
static void search_result      (GtkObject *object, EBookViewStatus status, EAddressbookView *eav);
static void folder_bar_message (GtkObject *object, const gchar *status, EAddressbookView *eav);
static void stop_state_changed (GtkObject *object, EAddressbookView *eav);
static void writable_status (GtkObject *object, gboolean writable, EAddressbookView *eav);
static void backend_died (GtkObject *object, EAddressbookView *eav);
static void command_state_change (EAddressbookView *eav);
static void alphabet_state_change (EAddressbookView *eav, gunichar letter);

static void selection_clear_event (GtkWidget *invisible, GdkEventSelection *event,
				   EAddressbookView *view);
static void selection_received (GtkWidget *invisible, GtkSelectionData *selection_data,
				guint time, EAddressbookView *view);
static void selection_get (GtkWidget *invisible, GtkSelectionData *selection_data,
			   guint info, guint time_stamp, EAddressbookView *view);
static void invisible_destroyed (GtkWidget *invisible, EAddressbookView *view);

static GtkTableClass *parent_class = NULL;

/* The arguments we take */
enum {
	ARG_0,
	ARG_BOOK,
	ARG_QUERY,
	ARG_TYPE,
};

enum {
	STATUS_MESSAGE,
	SEARCH_RESULT,
	FOLDER_BAR_MESSAGE,
	COMMAND_STATE_CHANGE,
	ALPHABET_STATE_CHANGE,
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

static guint e_addressbook_view_signals [LAST_SIGNAL] = {0, };

static GdkAtom clipboard_atom = GDK_NONE;

static GalViewCollection *collection = NULL;

GtkType
e_addressbook_view_get_type (void)
{
	static GtkType type = 0;

	if (!type) {
		static const GtkTypeInfo info =
		{
			"EAddressbookView",
			sizeof (EAddressbookView),
			sizeof (EAddressbookViewClass),
			(GtkClassInitFunc) e_addressbook_view_class_init,
			(GtkObjectInitFunc) e_addressbook_view_init,
			/* reserved_1 */ NULL,
		       	/* reserved_2 */ NULL,
			(GtkClassInitFunc) NULL,
		};
		
		type = gtk_type_unique (gtk_table_get_type (), &info);
	}

	return type;
}

static void
e_addressbook_view_class_init (EAddressbookViewClass *klass)
{
	GtkObjectClass *object_class;
	GtkWidgetClass *widget_class;

	object_class = GTK_OBJECT_CLASS(klass);
	widget_class = GTK_WIDGET_CLASS(klass);

	parent_class = gtk_type_class (gtk_table_get_type ());

	object_class->set_arg = e_addressbook_view_set_arg;
	object_class->get_arg = e_addressbook_view_get_arg;
	object_class->destroy = e_addressbook_view_destroy;

	gtk_object_add_arg_type ("EAddressbookView::book", GTK_TYPE_OBJECT,
				 GTK_ARG_READWRITE, ARG_BOOK);
	gtk_object_add_arg_type ("EAddressbookView::query", GTK_TYPE_STRING,
				 GTK_ARG_READWRITE, ARG_QUERY);
	gtk_object_add_arg_type ("EAddressbookView::type", GTK_TYPE_ENUM,
				 GTK_ARG_READWRITE, ARG_TYPE);

	e_addressbook_view_signals [STATUS_MESSAGE] =
		gtk_signal_new ("status_message",
				GTK_RUN_LAST,
				object_class->type,
				GTK_SIGNAL_OFFSET (EAddressbookViewClass, status_message),
				gtk_marshal_NONE__POINTER,
				GTK_TYPE_NONE, 1, GTK_TYPE_POINTER);

	e_addressbook_view_signals [SEARCH_RESULT] =
		gtk_signal_new ("search_result",
				GTK_RUN_LAST,
				object_class->type,
				GTK_SIGNAL_OFFSET (EAddressbookViewClass, search_result),
				gtk_marshal_NONE__ENUM,
				GTK_TYPE_NONE, 1, GTK_TYPE_ENUM);

	e_addressbook_view_signals [FOLDER_BAR_MESSAGE] =
		gtk_signal_new ("folder_bar_message",
				GTK_RUN_LAST,
				object_class->type,
				GTK_SIGNAL_OFFSET (EAddressbookViewClass, folder_bar_message),
				gtk_marshal_NONE__POINTER,
				GTK_TYPE_NONE, 1, GTK_TYPE_POINTER);

	e_addressbook_view_signals [COMMAND_STATE_CHANGE] =
		gtk_signal_new ("command_state_change",
				GTK_RUN_LAST,
				object_class->type,
				GTK_SIGNAL_OFFSET (EAddressbookViewClass, command_state_change),
				gtk_marshal_NONE__NONE,
				GTK_TYPE_NONE, 0);

	e_addressbook_view_signals [ALPHABET_STATE_CHANGE] =
		gtk_signal_new ("alphabet_state_change",
				GTK_RUN_LAST,
				object_class->type,
				GTK_SIGNAL_OFFSET (EAddressbookViewClass, alphabet_state_change),
				gtk_marshal_NONE__UINT,
				GTK_TYPE_NONE, 1, GTK_TYPE_UINT);

	gtk_object_class_add_signals (object_class, e_addressbook_view_signals, LAST_SIGNAL);

	if (!clipboard_atom)
		clipboard_atom = gdk_atom_intern ("CLIPBOARD", FALSE);
}

static void
e_addressbook_view_init (EAddressbookView *eav)
{
	eav->view_type = E_ADDRESSBOOK_VIEW_NONE;

	eav->model = e_addressbook_model_new ();

	gtk_signal_connect (GTK_OBJECT(eav->model),
			    "status_message",
			    GTK_SIGNAL_FUNC (status_message),
			    eav);

	gtk_signal_connect (GTK_OBJECT(eav->model),
			    "search_result",
			    GTK_SIGNAL_FUNC (search_result),
			    eav);

	gtk_signal_connect (GTK_OBJECT(eav->model),
			    "folder_bar_message",
			    GTK_SIGNAL_FUNC (folder_bar_message),
			    eav);

	gtk_signal_connect (GTK_OBJECT(eav->model),
			    "stop_state_changed",
			    GTK_SIGNAL_FUNC (stop_state_changed),
			    eav);

	gtk_signal_connect (GTK_OBJECT(eav->model),
			    "writable_status",
			    GTK_SIGNAL_FUNC (writable_status),
			    eav);

	gtk_signal_connect (GTK_OBJECT(eav->model),
			    "backend_died",
			    GTK_SIGNAL_FUNC (backend_died),
			    eav);

	eav->editable = FALSE;
	eav->book = NULL;
	eav->query = g_strdup (SHOW_ALL_SEARCH);

	eav->object = NULL;
	eav->widget = NULL;

	eav->view_instance = NULL;
	eav->view_menus = NULL;
	eav->uic = NULL;
	eav->current_alphabet_widget = NULL;

	eav->invisible = gtk_invisible_new ();

	gtk_selection_add_target (eav->invisible,
				  clipboard_atom,
				  GDK_SELECTION_TYPE_STRING,
				  0);
		
	gtk_signal_connect (GTK_OBJECT(eav->invisible), "selection_get",
			    GTK_SIGNAL_FUNC (selection_get), 
			    eav);
	gtk_signal_connect (GTK_OBJECT(eav->invisible), "selection_clear_event",
			    GTK_SIGNAL_FUNC (selection_clear_event),
			    eav);
	gtk_signal_connect (GTK_OBJECT(eav->invisible), "selection_received",
			    GTK_SIGNAL_FUNC (selection_received),
			    eav);
	gtk_signal_connect (GTK_OBJECT(eav->invisible), "destroy",
			    GTK_SIGNAL_FUNC (invisible_destroyed),
			    eav);
}

static void
e_addressbook_view_destroy (GtkObject *object)
{
	EAddressbookView *eav = E_ADDRESSBOOK_VIEW(object);

	if (eav->model) {
		gtk_object_unref(GTK_OBJECT(eav->model));
		eav->model = NULL;
	}

	if (eav->book) {
		gtk_object_unref(GTK_OBJECT(eav->book));
		eav->book = NULL;
	}

	g_free(eav->query);
	eav->query = NULL;

	eav->uic = NULL;

	if (eav->view_instance) {
		gtk_object_unref (GTK_OBJECT (eav->view_instance));
		eav->view_instance = NULL;
	}

	if (eav->view_menus) {
		gtk_object_unref (GTK_OBJECT (eav->view_menus));
		eav->view_menus = NULL;
	}

	if (eav->clipboard_cards) {
		g_list_foreach (eav->clipboard_cards, (GFunc)gtk_object_unref, NULL);
		g_list_free (eav->clipboard_cards);
		eav->clipboard_cards = NULL;
	}
		
	if (eav->invisible) {
		gtk_widget_destroy (eav->invisible);
		eav->invisible = NULL;
	}

	if (GTK_OBJECT_CLASS(parent_class)->destroy)
		GTK_OBJECT_CLASS(parent_class)->destroy(object);
}

GtkWidget*
e_addressbook_view_new (void)
{
	GtkWidget *widget = GTK_WIDGET (gtk_type_new (e_addressbook_view_get_type ()));
	return widget;
}

static void
writable_status (GtkObject *object, gboolean writable, EAddressbookView *eav)
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
			 EVOLUTION_DATADIR "/evolution/views/addressbook/",
			 galview);
		g_free(galview);

		spec = e_table_specification_new();
		e_table_specification_load_from_file (spec, EVOLUTION_ETSPECDIR "/e-addressbook-view.etspec");

		factory = gal_view_factory_etable_new (spec);
		gtk_object_unref (GTK_OBJECT (spec));
		gal_view_collection_add_factory (collection, factory);
		gtk_object_unref (GTK_OBJECT (factory));

		factory = gal_view_factory_minicard_new ();
		gal_view_collection_add_factory (collection, factory);
		gtk_object_unref (GTK_OBJECT (factory));

		gal_view_collection_load(collection);
	}
}

static void
display_view(GalViewInstance *instance,
	     GalView *view,
	     gpointer data)
{
	EAddressbookView *address_view = data;
	if (GAL_IS_VIEW_ETABLE(view)) {
		change_view_type (address_view, E_ADDRESSBOOK_VIEW_TABLE);
		gal_view_etable_attach_table (GAL_VIEW_ETABLE(view), e_table_scrolled_get_table(E_TABLE_SCROLLED(address_view->widget)));
	} else if (GAL_IS_VIEW_MINICARD(view)) {
		change_view_type (address_view, E_ADDRESSBOOK_VIEW_MINICARD);
		gal_view_minicard_attach (GAL_VIEW_MINICARD(view), E_MINICARD_VIEW_WIDGET (address_view->object));
	}
	address_view->current_view = view;
}

static void
setup_menus (EAddressbookView *view)
{
	if (view->book && view->view_instance == NULL) {
		init_collection ();
		view->view_instance = gal_view_instance_new (collection, e_book_get_uri (view->book));
	}

	if (view->view_instance && view->uic) {
		view->view_menus = gal_view_menus_new(view->view_instance);
		gal_view_menus_apply(view->view_menus, view->uic, NULL);

		display_view (view->view_instance, gal_view_instance_get_current_view (view->view_instance), view);

		gtk_signal_connect(GTK_OBJECT(view->view_instance), "display_view",
				   display_view, view);
	}
}

static void
e_addressbook_view_set_arg (GtkObject *object, GtkArg *arg, guint arg_id)
{
	EAddressbookView *eav = E_ADDRESSBOOK_VIEW(object);

	switch (arg_id){
	case ARG_BOOK:
		if (eav->book) {
			gtk_object_unref(GTK_OBJECT(eav->book));
		}
		if (GTK_VALUE_OBJECT(*arg)) {
			eav->book = E_BOOK(GTK_VALUE_OBJECT(*arg));
			gtk_object_ref(GTK_OBJECT(eav->book));
		}
		else
			eav->book = NULL;

		if (eav->view_instance) {
			gtk_object_unref (GTK_OBJECT (eav->view_instance));
			eav->view_instance = NULL;
		}

		gtk_object_set(GTK_OBJECT(eav->model),
			       "book", eav->book,
			       NULL);

		setup_menus (eav);

		break;
	case ARG_QUERY:
#if 0 /* This code will mess up ldap a bit.  We need to think about the ramifications of this more. */
		if ((GTK_VALUE_STRING (*arg) == NULL && !strcmp (eav->query, SHOW_ALL_SEARCH)) ||
		    (GTK_VALUE_STRING (*arg) != NULL && !strcmp (eav->query, GTK_VALUE_STRING (*arg))))
			break;
#endif
		g_free(eav->query);
		eav->query = g_strdup(GTK_VALUE_STRING(*arg));
		if (!eav->query)
			eav->query = g_strdup (SHOW_ALL_SEARCH);
		gtk_object_set(GTK_OBJECT(eav->model),
			       "query", eav->query,
			       NULL);
		if (eav->current_alphabet_widget != NULL) {
			GtkWidget *current = eav->current_alphabet_widget;

			eav->current_alphabet_widget = NULL;
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (current), FALSE);
		}
		break;
	case ARG_TYPE:
		change_view_type(eav, GTK_VALUE_ENUM(*arg));
		break;
	default:
		break;
	}
}

static void
e_addressbook_view_get_arg (GtkObject *object, GtkArg *arg, guint arg_id)
{
	EAddressbookView *eav = E_ADDRESSBOOK_VIEW(object);

	switch (arg_id) {
	case ARG_BOOK:
		if (eav->book)
			GTK_VALUE_OBJECT (*arg) = GTK_OBJECT(eav->book);
		else
			GTK_VALUE_OBJECT (*arg) = NULL;
		break;
	case ARG_QUERY:
		GTK_VALUE_STRING (*arg) = eav->query;
		break;
	case ARG_TYPE:
		GTK_VALUE_ENUM (*arg) = eav->view_type;
		break;
	default:
		arg->type = GTK_TYPE_INVALID;
		break;
	}
}

static ESelectionModel*
get_selection_model (EAddressbookView *view)
{
	if (view->view_type == E_ADDRESSBOOK_VIEW_MINICARD)
		return e_minicard_view_widget_get_selection_model (E_MINICARD_VIEW_WIDGET(view->object));
	else
		return e_table_get_selection_model (e_table_scrolled_get_table (E_TABLE_SCROLLED(view->widget)));
}

/* Popup menu stuff */
typedef struct {
	EAddressbookView *view;
	EPopupMenu *submenu;
	gpointer closure;
} CardAndBook;

static ESelectionModel*
card_and_book_get_selection_model (CardAndBook *card_and_book)
{
	return get_selection_model (card_and_book->view);
}

static void
card_and_book_free (CardAndBook *card_and_book)
{
	EAddressbookView *view = card_and_book->view;
	ESelectionModel *selection;

	if (card_and_book->submenu)
		gal_view_instance_free_popup_menu (view->view_instance,
						   card_and_book->submenu);

	selection = card_and_book_get_selection_model (card_and_book);
	if (selection)
		e_selection_model_right_click_up(selection);

	gtk_object_unref(GTK_OBJECT(view));
}

static void
get_card_list_1(gint model_row,
		      gpointer closure)
{
	CardAndBook *card_and_book;
	GList **list;
	EAddressbookView *view;
	ECard *card;

	card_and_book = closure;
	list = card_and_book->closure;
	view = card_and_book->view;

	card = e_addressbook_model_get_card(view->model, model_row);
	*list = g_list_prepend(*list, card);
}

static GList *
get_card_list (CardAndBook *card_and_book)
{
	GList *list = NULL;
	ESelectionModel *selection;

	selection = card_and_book_get_selection_model (card_and_book);

	if (selection) {
		card_and_book->closure = &list;
		e_selection_model_foreach (selection, get_card_list_1, card_and_book);
	}

	return list;
}

static void
has_email_address_1(gint model_row,
			  gpointer closure)
{
	CardAndBook *card_and_book;
	gboolean *has_email;
	EAddressbookView *view;
	const ECard *card;
	EList *email;

	card_and_book = closure;
	has_email = card_and_book->closure;
	view = card_and_book->view;

	if (*has_email)
		return;

	card = e_addressbook_model_peek_card(view->model, model_row);

	gtk_object_get (GTK_OBJECT (card),
			"email", &email,
			NULL);

	if (e_list_length (email) > 0)
		*has_email = TRUE;
}

static gboolean
get_has_email_address (CardAndBook *card_and_book)
{
	ESelectionModel *selection;
	gboolean has_email = FALSE;

	selection = card_and_book_get_selection_model (card_and_book);

	if (selection) {
		card_and_book->closure = &has_email;
		e_selection_model_foreach (selection, has_email_address_1, card_and_book);
	}

	return has_email;
}

static void
save_as (GtkWidget *widget, CardAndBook *card_and_book)
{
	GList *cards = get_card_list (card_and_book);
	if (cards) {
		e_contact_list_save_as(_("Save as VCard"), cards, NULL);
		e_free_object_list(cards);
	}
}

static void
send_as (GtkWidget *widget, CardAndBook *card_and_book)
{
	GList *cards = get_card_list (card_and_book);
	if (cards) {
		e_card_list_send(cards, E_CARD_DISPOSITION_AS_ATTACHMENT);
		e_free_object_list(cards);
	}
}

static void
send_to (GtkWidget *widget, CardAndBook *card_and_book)

{
	GList *cards = get_card_list (card_and_book);

	if (cards) {
		e_card_list_send(cards, E_CARD_DISPOSITION_AS_TO);
		e_free_object_list(cards);
	}
}

static void
print (GtkWidget *widget, CardAndBook *card_and_book)
{
	GList *cards = get_card_list (card_and_book);
	if (cards) {
		if (cards->next)
			gtk_widget_show(e_contact_print_card_list_dialog_new(cards));
		else
			gtk_widget_show(e_contact_print_card_dialog_new(cards->data));
		e_free_object_list(cards);
	}
}

#if 0 /* Envelope printing is disabled for Evolution 1.0. */
static void
print_envelope (GtkWidget *widget, CardAndBook *card_and_book)
{
	GList *cards = get_card_list (card_and_book);
	if (cards) {
		gtk_widget_show(e_contact_list_print_envelope_dialog_new(card_and_book->card));
		e_free_object_list(cards);
	}
}
#endif

static void
copy (GtkWidget *widget, CardAndBook *card_and_book)
{
	e_addressbook_view_copy (card_and_book->view);
}

static void
paste (GtkWidget *widget, CardAndBook *card_and_book)
{
	e_addressbook_view_paste (card_and_book->view);
}

static void
cut (GtkWidget *widget, CardAndBook *card_and_book)
{
	e_addressbook_view_cut (card_and_book->view);
}

static void
delete (GtkWidget *widget, CardAndBook *card_and_book)
{
	if (e_contact_editor_confirm_delete(GTK_WINDOW(gtk_widget_get_toplevel(card_and_book->view->widget)))) {
		EBook *book;

		GList *list = get_card_list(card_and_book);
		GList *iterator;

		gtk_object_get(GTK_OBJECT(card_and_book->view->model),
			       "book", &book,
			       NULL);

		for (iterator = list; iterator; iterator = iterator->next) {
			ECard *card = iterator->data;
			/* Remove the card. */
			e_book_remove_card (book,
					    card,
					    NULL,
					    NULL);
		}
		e_free_object_list(list);
	}
}

static void
copy_to_folder (GtkWidget *widget, CardAndBook *card_and_book)
{
	e_addressbook_view_copy_to_folder (card_and_book->view);
}

static void
move_to_folder (GtkWidget *widget, CardAndBook *card_and_book)
{
	e_addressbook_view_move_to_folder (card_and_book->view);
}

static void
free_popup_info (GtkWidget *w, CardAndBook *card_and_book)
{
	card_and_book_free (card_and_book);
}

static void
new_card (GtkWidget *widget, CardAndBook *card_and_book)
{
	EBook *book;

	gtk_object_get(GTK_OBJECT(card_and_book->view->model),
		       "book", &book,
		       NULL);
	e_addressbook_show_contact_editor (book, e_card_new(""), TRUE, TRUE);
}

static void
new_list (GtkWidget *widget, CardAndBook *card_and_book)
{
	EBook *book;

	gtk_object_get(GTK_OBJECT(card_and_book->view->model),
		       "book", &book,
		       NULL);
	e_addressbook_show_contact_list_editor (book, e_card_new(""), TRUE, TRUE);
}

#if 0
static void
sources (GtkWidget *widget, CardAndBook *card_and_book)
{
	BonoboControl *control;
	GNOME_Evolution_ShellView shell_view;
	CORBA_Environment ev;

	control = gtk_object_get_data (GTK_OBJECT (gcal), "control");
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
do_popup_menu(EAddressbookView *view, GdkEvent *event)
{
	CardAndBook *card_and_book;
	GtkMenu *popup;
	EPopupMenu *submenu = NULL;
	ESelectionModel *selection_model;
	gboolean selection = FALSE;

	EPopupMenu menu[] = {
		E_POPUP_ITEM (N_("New Contact..."), GTK_SIGNAL_FUNC(new_card), POPUP_READONLY_MASK),
		E_POPUP_ITEM (N_("New Contact List..."), GTK_SIGNAL_FUNC(new_list), POPUP_READONLY_MASK),
		E_POPUP_SEPARATOR,
#if 0
		E_POPUP_ITEM (N_("Go to Folder..."), GTK_SIGNAL_FUNC (goto_folder), 0),
		E_POPUP_ITEM (N_("Import..."), GTK_SIGNAL_FUNC (import), POPUP_READONLY_MASK),
		E_POPUP_SEPARATOR,
		E_POPUP_ITEM (N_("Search for Contacts..."), GTK_SIGNAL_FUNC (search), 0),
		E_POPUP_ITEM (N_("Addressbook Sources..."), GTK_SIGNAL_FUNC (sources), 0),
		E_POPUP_SEPARATOR,
		E_POPUP_ITEM (N_("Pilot Settings..."), GTK_SIGNAL_FUNC (pilot_settings), 0),
#endif
		E_POPUP_SEPARATOR,
		E_POPUP_ITEM (N_("Save as VCard"), GTK_SIGNAL_FUNC(save_as), POPUP_NOSELECTION_MASK),
		E_POPUP_ITEM (N_("Forward Contact"), GTK_SIGNAL_FUNC(send_as), POPUP_NOSELECTION_MASK),
		E_POPUP_ITEM (N_("Send Message to Contact"), GTK_SIGNAL_FUNC(send_to), POPUP_NOSELECTION_MASK | POPUP_NOEMAIL_MASK),
		E_POPUP_ITEM (N_("Print"), GTK_SIGNAL_FUNC(print), POPUP_NOSELECTION_MASK),
#if 0 /* Envelope printing is disabled for Evolution 1.0. */
		E_POPUP_ITEM (N_("Print Envelope"), GTK_SIGNAL_FUNC(print_envelope), POPUP_NOSELECTION_MASK),
#endif
		E_POPUP_SEPARATOR,

		E_POPUP_ITEM (N_("Copy to folder..."), GTK_SIGNAL_FUNC(copy_to_folder), POPUP_NOSELECTION_MASK), 
		E_POPUP_ITEM (N_("Move to folder..."), GTK_SIGNAL_FUNC(move_to_folder), POPUP_READONLY_MASK | POPUP_NOSELECTION_MASK),
		E_POPUP_SEPARATOR,

		E_POPUP_ITEM (N_("Cut"), GTK_SIGNAL_FUNC (cut), POPUP_READONLY_MASK | POPUP_NOSELECTION_MASK),
		E_POPUP_ITEM (N_("Copy"), GTK_SIGNAL_FUNC (copy), POPUP_NOSELECTION_MASK),
		E_POPUP_ITEM (N_("Paste"), GTK_SIGNAL_FUNC (paste), POPUP_READONLY_MASK),
		E_POPUP_ITEM (N_("Delete"), GTK_SIGNAL_FUNC(delete), POPUP_READONLY_MASK | POPUP_NOSELECTION_MASK),
		E_POPUP_SEPARATOR,

#if 0
		E_POPUP_SUBMENU (N_("Current View"), submenu = gal_view_instance_get_popup_menu (view->view_instance), 0),
#endif
		E_POPUP_TERMINATOR
	};

	card_and_book = g_new(CardAndBook, 1);
	card_and_book->view = view;
	card_and_book->submenu = submenu;

	gtk_object_ref(GTK_OBJECT(card_and_book->view));

	selection_model = card_and_book_get_selection_model (card_and_book);
	if (selection_model)
		selection = e_selection_model_selected_count (selection_model) > 0;

	popup = e_popup_menu_create (menu,
				     0,
				     (e_addressbook_model_editable (view->model) ? 0 : POPUP_READONLY_MASK) +
				     (selection ? 0 : POPUP_NOSELECTION_MASK) +
				     (get_has_email_address (card_and_book) ? 0 : POPUP_NOEMAIL_MASK),
				     card_and_book);

	gtk_signal_connect (GTK_OBJECT (popup), "selection-done",
			    GTK_SIGNAL_FUNC (free_popup_info), card_and_book);
	e_popup_menu (popup, event);

}


/* Minicard view stuff */

/* Translators: put here a list of labels you want to see on buttons in
   addressbook. You may use any character to separate labels but it must
   also be placed at the begining ot the string */
const char *button_labels = N_(",123,a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z");
/* Translators: put here a list of characters that correspond to buttons
   in addressbook. You may use any character to separate labels but it
   must also be placed at the begining ot the string.
   Use lower case letters if possible. */
const char *button_letters = N_(",0,a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z");

typedef struct {
	EAddressbookView *view;
	GtkWidget *button;
	GtkWidget *vbox;
	gchar *letters;
} LetterClosure;

static char **
e_utf8_split (const char *utf8_str, gunichar delim)
{
	GSList *str_list = NULL, *sl;
	int n = 0;
	const char *str, *s;
	char **str_array;

	g_return_val_if_fail (utf8_str != NULL, NULL);

	str = utf8_str;
	while (*str != '\0') {
		int len;
		char *new_str;

		for (s = str; *s != '\0' && g_utf8_get_char (s) != delim; s = g_utf8_next_char (s))
			;
		len = s - str;
		new_str = g_new (char, len + 1);
		if (len > 0) {
			memcpy (new_str, str, len);
		}
		new_str[len] = '\0';
		str_list = g_slist_prepend (str_list, new_str);
		n++;
		if (*s != '\0') {
			str = g_utf8_next_char (s);
		} else {
			str = s;
		}		
	}

	str_array = g_new (char *, n + 1);
	str_array[n--] = NULL;
	for (sl = str_list; sl != NULL; sl = sl->next) {
		str_array[n--] = sl->data;
	}
	g_slist_free (str_list);

	return str_array;
}

static void
jump_to_letters (EAddressbookView *view, gchar* l)
{
	char *query;
	char *s;
	char buf[6 + 1];

	if (g_unichar_isdigit (g_utf8_get_char(l))) {
		const char *letters = U_(button_letters);
		char **letter_v;
		GString *gstr;
		char **p;

		letter_v = e_utf8_split (g_utf8_next_char (letters),
		                         g_utf8_get_char (letters));
		g_assert (letter_v != NULL && letter_v[0] != NULL);
		gstr = g_string_new ("(not (or ");
		for (p = letter_v + 1; *p != NULL; p++) {
			for (s = *p; *s != '\0'; s = g_utf8_next_char (s)) {
				buf [g_unichar_to_utf8 (g_utf8_get_char(s), buf)] = '\0';
				g_string_sprintfa (gstr, "(beginswith \"file_as\" \"%s\")", buf);
			}
		}
		g_string_append (gstr, "))");
		query = gstr->str;
		g_strfreev (letter_v);
		g_string_free (gstr, FALSE);
	} else {
		GString *gstr;

		gstr = g_string_new ("(or ");

		for (s = l; *s != '\0'; s = g_utf8_next_char (s)) {
			buf [g_unichar_to_utf8 (g_utf8_get_char(s), buf)] = '\0';
			g_string_sprintfa (gstr, "(beginswith \"file_as\" \"%s\")", buf);
		}

		g_string_append (gstr, ")");
		query = gstr->str;
		g_string_free (gstr, FALSE);
	}
	gtk_object_set (GTK_OBJECT (view),
			"query", query,
			NULL);
	g_free (query);
}

static void
button_toggled(GtkWidget *button, LetterClosure *closure)
{
	EAddressbookView *view = closure->view;

	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button))) {
		GtkWidget *current = view->current_alphabet_widget;

		view->current_alphabet_widget = NULL;
		if (current && current != button)
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (current), FALSE);
		jump_to_letters (view, closure->letters);
		view->current_alphabet_widget = button;
		alphabet_state_change (view, g_utf8_get_char(closure->letters));
	} else {
		if (view->current_alphabet_widget != NULL &&
		    view->current_alphabet_widget == button) {
			view->current_alphabet_widget = NULL;
			gtk_object_set (GTK_OBJECT (view),
					"query", NULL,
					NULL);
			alphabet_state_change (view, 0);
		}
	}
}

static void
free_closure(GtkWidget *button, LetterClosure *closure)
{
	if (button != NULL &&
	    button == closure->view->current_alphabet_widget) {
		closure->view->current_alphabet_widget = NULL;
	}
	g_free (closure->letters);
	g_free (closure);
}

static GtkWidget *
create_alphabet (EAddressbookView *view)
{
	GtkWidget *widget, *viewport, *vbox;
	const char *labels, *letters;
	char **label_v, **letter_v;
	char **pl, **pc;
	gunichar sep;

	widget = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (widget),
	                                GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

	viewport = gtk_viewport_new (NULL, NULL);
	gtk_container_add (GTK_CONTAINER (widget), viewport);
	gtk_container_set_border_width (GTK_CONTAINER (viewport), 4);
	gtk_viewport_set_shadow_type (GTK_VIEWPORT (viewport), GTK_SHADOW_NONE);

	vbox = gtk_vbox_new (FALSE, 4);
	gtk_container_add (GTK_CONTAINER (viewport), vbox);
	gtk_widget_set_usize (vbox, 27, 0);

	labels = U_(button_labels);
	sep = g_utf8_get_char (labels);
	label_v = e_utf8_split (g_utf8_next_char (labels), sep);
	letters = U_(button_letters);
	sep = g_utf8_get_char (letters);
	letter_v = e_utf8_split (g_utf8_next_char (letters), sep);
	g_assert (label_v != NULL && letter_v != NULL);
	for (pl = label_v, pc = letter_v; *pl != NULL && *pc != NULL; pl++, pc++) {
		GtkWidget *button;
		LetterClosure *closure;
		char *label;

		label = e_utf8_to_locale_string (*pl);
		button = gtk_toggle_button_new_with_label (label);
		g_free (label);
		gtk_box_pack_start (GTK_BOX (vbox), button, FALSE, FALSE, 0);

		closure = g_new (LetterClosure, 1);
		closure->view = view;
		closure->letters = g_strdup (*pc);
		closure->button = button;
		closure->vbox = vbox;
		gtk_signal_connect(GTK_OBJECT(button), "toggled",
		                   GTK_SIGNAL_FUNC (button_toggled), closure);
		gtk_signal_connect(GTK_OBJECT(button), "destroy",
		                   GTK_SIGNAL_FUNC (free_closure), closure);

	}
	g_strfreev (label_v);
	g_strfreev (letter_v);

	gtk_widget_show_all (widget);

	return widget;
}

static void
minicard_selection_change (EMinicardViewWidget *widget, EAddressbookView *view)
{
	command_state_change (view);
}

static void
minicard_button_press (GtkWidget *widget, GdkEventButton *event, EAddressbookView *view)
{
	d(g_print ("Button %d pressed with event type %d\n", event->button, event->type));
}

static void
minicard_right_click (EMinicardView *minicard_view_item, GdkEvent *event, EAddressbookView *view)
{
	do_popup_menu(view, event);
}

static void
create_minicard_view (EAddressbookView *view)
{
	GtkWidget *scrollframe;
	GtkWidget *alphabet;
	GtkWidget *minicard_view;
	GtkWidget *minicard_hbox;
	EAddressbookReflowAdapter *adapter;

	gtk_widget_push_visual (gdk_rgb_get_visual ());
	gtk_widget_push_colormap (gdk_rgb_get_cmap ());

	minicard_hbox = gtk_hbox_new(FALSE, 0);

	adapter = E_ADDRESSBOOK_REFLOW_ADAPTER(e_addressbook_reflow_adapter_new (view->model));
	minicard_view = e_minicard_view_widget_new(adapter);

	/* A hack */
	gtk_object_set_data (GTK_OBJECT (adapter), "view", view);

	gtk_signal_connect(GTK_OBJECT(minicard_view), "selection_change",
			   GTK_SIGNAL_FUNC(minicard_selection_change), view);

	gtk_signal_connect(GTK_OBJECT(minicard_view), "button_press_event",
			   GTK_SIGNAL_FUNC(minicard_button_press), view);

	gtk_signal_connect(GTK_OBJECT(minicard_view), "right_click",
			   GTK_SIGNAL_FUNC(minicard_right_click), view);


	view->object = GTK_OBJECT(minicard_view);
	view->widget = minicard_hbox;

	scrollframe = e_scroll_frame_new (NULL, NULL);
	e_scroll_frame_set_policy (E_SCROLL_FRAME (scrollframe),
				   GTK_POLICY_AUTOMATIC,
				   GTK_POLICY_AUTOMATIC);

	gtk_container_add (GTK_CONTAINER (scrollframe), minicard_view);


	gtk_box_pack_start(GTK_BOX(minicard_hbox), scrollframe, TRUE, TRUE, 0);

	alphabet = create_alphabet(view);
	if (alphabet) {
		gtk_object_ref(GTK_OBJECT(alphabet));
		gtk_widget_unparent(alphabet);
		gtk_box_pack_start(GTK_BOX(minicard_hbox), alphabet, FALSE, FALSE, 0);
		gtk_object_unref(GTK_OBJECT(alphabet));
	}

	gtk_table_attach(GTK_TABLE(view), minicard_hbox,
			 0, 1,
			 0, 1,
			 GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_EXPAND,
			 0, 0);

	gtk_widget_show_all( GTK_WIDGET(minicard_hbox) );

	gtk_widget_pop_visual ();
	gtk_widget_pop_colormap ();

	e_reflow_model_changed (E_REFLOW_MODEL (adapter));

	gtk_object_unref (GTK_OBJECT (adapter));
}

static void
table_double_click(ETableScrolled *table, gint row, gint col, GdkEvent *event, EAddressbookView *view)
{
	if (E_IS_ADDRESSBOOK_TABLE_ADAPTER(view->object)) {
		EAddressbookModel *model = view->model;
		ECard *card = e_addressbook_model_get_card(model, row);
		EBook *book;

		gtk_object_get(GTK_OBJECT(model),
			       "book", &book,
			       NULL);
		
		g_assert (E_IS_BOOK (book));

		if (e_card_evolution_list (card))
			e_addressbook_show_contact_list_editor (book, card, FALSE, view->editable);
		else
			e_addressbook_show_contact_editor (book, card, FALSE, view->editable);
	}
}

static gint
table_right_click(ETableScrolled *table, gint row, gint col, GdkEvent *event, EAddressbookView *view)
{
	do_popup_menu(view, event);
	return TRUE;
}

static gint
table_white_space_event(ETableScrolled *table, GdkEvent *event, EAddressbookView *view)
{
	if (event->type == GDK_BUTTON_PRESS && ((GdkEventButton *)event)->button == 3) {
		do_popup_menu(view, event);
		return TRUE;
	} else {
		return FALSE;
	}
}

static void
table_selection_change(ETableScrolled *table, EAddressbookView *view)
{
	command_state_change (view);
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
	EAddressbookView *view = user_data;

	if (!E_IS_ADDRESSBOOK_TABLE_ADAPTER(view->object))
		return;

	switch (info) {
	case DND_TARGET_TYPE_VCARD: {
		char *value;

		row = e_table_view_to_model_row (table, row);
		value = e_card_get_vcard(view->model->data[row]);

		gtk_selection_data_set (selection_data,
					selection_data->target,
					8,
					value, strlen (value));
		break;
	}
	}
}

static void
emit_status_message (EAddressbookView *eav, const gchar *status)
{
	gtk_signal_emit (GTK_OBJECT (eav),
			 e_addressbook_view_signals [STATUS_MESSAGE],
			 status);
}

static void
emit_search_result (EAddressbookView *eav, EBookViewStatus status)
{
	gtk_signal_emit (GTK_OBJECT (eav),
			 e_addressbook_view_signals [SEARCH_RESULT],
			 status);
}

static void
emit_folder_bar_message (EAddressbookView *eav, const gchar *message)
{
	gtk_signal_emit (GTK_OBJECT (eav),
			 e_addressbook_view_signals [FOLDER_BAR_MESSAGE],
			 message);
}

static void
status_message (GtkObject *object, const gchar *status, EAddressbookView *eav)
{
	emit_status_message (eav, status);
}

static void
search_result (GtkObject *object, EBookViewStatus status, EAddressbookView *eav)
{
	emit_search_result (eav, status);
}

static void
folder_bar_message (GtkObject *object, const gchar *status, EAddressbookView *eav)
{
	emit_folder_bar_message (eav, status);
}

static void
stop_state_changed (GtkObject *object, EAddressbookView *eav)
{
	command_state_change (eav);
}

static void
command_state_change (EAddressbookView *eav)
{
	/* Reffing during emission is unnecessary.  Gtk automatically refs during an emission. */
	gtk_signal_emit (GTK_OBJECT (eav), e_addressbook_view_signals [COMMAND_STATE_CHANGE]);
}

static void
alphabet_state_change (EAddressbookView *eav, gunichar letter)
{
	gtk_signal_emit (GTK_OBJECT (eav), e_addressbook_view_signals [ALPHABET_STATE_CHANGE], letter);
}

static void
backend_died (GtkObject *object, EAddressbookView *eav)
{
	char *message = g_strdup_printf (_("The addressbook backend for\n%s\nhas crashed. "
					   "You will have to restart Evolution in order "
					   "to use it again"),
					 e_book_get_uri (eav->book));
        gnome_error_dialog_parented (message, GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (eav))));
        g_free (message);
}

static void
create_table_view (EAddressbookView *view)
{
	ETableModel *adapter;
	ECardSimple *simple;
	GtkWidget *table;
	
	simple = e_card_simple_new(NULL);

	adapter = e_addressbook_table_adapter_new(view->model);

	/* Here we create the table.  We give it the three pieces of
	   the table we've created, the header, the model, and the
	   initial layout.  It does the rest.  */
	table = e_table_scrolled_new_from_spec_file (adapter, NULL, EVOLUTION_ETSPECDIR "/e-addressbook-view.etspec", NULL);

	view->object = GTK_OBJECT(adapter);
	view->widget = table;

	gtk_signal_connect(GTK_OBJECT(e_table_scrolled_get_table(E_TABLE_SCROLLED(table))), "double_click",
			   GTK_SIGNAL_FUNC(table_double_click), view);
	gtk_signal_connect(GTK_OBJECT(e_table_scrolled_get_table(E_TABLE_SCROLLED(table))), "right_click",
			   GTK_SIGNAL_FUNC(table_right_click), view);
	gtk_signal_connect(GTK_OBJECT(e_table_scrolled_get_table(E_TABLE_SCROLLED(table))), "white_space_event",
			   GTK_SIGNAL_FUNC(table_white_space_event), view);
	gtk_signal_connect(GTK_OBJECT(e_table_scrolled_get_table(E_TABLE_SCROLLED(table))), "selection_change",
			   GTK_SIGNAL_FUNC(table_selection_change), view);

	/* drag & drop signals */
	e_table_drag_source_set (E_TABLE(E_TABLE_SCROLLED(table)->table), GDK_BUTTON1_MASK,
				 drag_types, num_drag_types, GDK_ACTION_MOVE);
	
	gtk_signal_connect (GTK_OBJECT (E_TABLE_SCROLLED(table)->table),
			    "table_drag_data_get",
			    GTK_SIGNAL_FUNC (table_drag_data_get),
			    view);

	gtk_table_attach(GTK_TABLE(view), table,
			 0, 1,
			 0, 1,
			 GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_EXPAND,
			 0, 0);

	gtk_widget_show( GTK_WIDGET(table) );

	gtk_object_unref(GTK_OBJECT(simple));
}


static void
change_view_type (EAddressbookView *view, EAddressbookViewType view_type)
{
	if (view_type == view->view_type)
		return;

	if (view->widget) {
		gtk_widget_destroy (view->widget);
		view->widget = NULL;
	}
	view->object = NULL;

	switch (view_type) {
	case E_ADDRESSBOOK_VIEW_MINICARD:
		create_minicard_view (view);
		break;
	case E_ADDRESSBOOK_VIEW_TABLE:
		create_table_view (view);
		break;
	default:
		g_warning ("view_type must be either TABLE or MINICARD\n");
		return;
	}

	view->view_type = view_type;

	command_state_change (view);
}

static void
e_contact_print_destroy(GnomeDialog *dialog, gpointer data)
{
	ETableScrolled *table = gtk_object_get_data(GTK_OBJECT(dialog), "table");
	EPrintable *printable = gtk_object_get_data(GTK_OBJECT(dialog), "printable");
	gtk_object_unref(GTK_OBJECT(printable));
	gtk_object_unref(GTK_OBJECT(table));
}

static void
e_contact_print_button(GnomeDialog *dialog, gint button, gpointer data)
{
	GnomePrintMaster *master;
	GnomePrintContext *pc;
	EPrintable *printable = gtk_object_get_data(GTK_OBJECT(dialog), "printable");
	GtkWidget *preview;
	switch( button ) {
	case GNOME_PRINT_PRINT:
		master = gnome_print_master_new_from_dialog( GNOME_PRINT_DIALOG(dialog) );
		pc = gnome_print_master_get_context( master );
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
		gnome_print_master_close(master);
		gnome_print_master_print(master);
		gtk_object_unref(GTK_OBJECT(master));
		gnome_dialog_close(dialog);
		break;
	case GNOME_PRINT_PREVIEW:
		master = gnome_print_master_new_from_dialog( GNOME_PRINT_DIALOG(dialog) );
		pc = gnome_print_master_get_context( master );
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
		gnome_print_master_close(master);
		preview = GTK_WIDGET(gnome_print_master_preview_new(master, "Print Preview"));
		gtk_widget_show_all(preview);
		gtk_object_unref(GTK_OBJECT(master));
		break;
	case GNOME_PRINT_CANCEL:
		gnome_dialog_close(dialog);
		break;
	}
}

void
e_addressbook_view_setup_menus (EAddressbookView *view,
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
 * e_addressbook_view_discard_menus:
 * @view: An addressbook view.
 * 
 * Makes an addressbook view discard its GAL view menus and its views instance
 * objects.  This should be called when the corresponding Bonobo component is
 * deactivated.
 **/
void
e_addressbook_view_discard_menus (EAddressbookView *view)
{
	g_return_if_fail (view != NULL);
	g_return_if_fail (E_IS_ADDRESSBOOK_VIEW (view));
	g_return_if_fail (view->view_instance);

	if (view->view_menus) {
		gal_view_menus_unmerge (view->view_menus, NULL);

		gtk_object_unref (GTK_OBJECT (view->view_menus));
		view->view_menus = NULL;
	}

	if (view->view_instance) {
		gtk_object_unref (GTK_OBJECT (view->view_instance));
		view->view_instance = NULL;
	}

	view->uic = NULL;
}

void
e_addressbook_view_print(EAddressbookView *view)
{
	if (view->view_type == E_ADDRESSBOOK_VIEW_MINICARD) {
		char *query;
		EBook *book;
		GtkWidget *print;

		gtk_object_get (GTK_OBJECT(view->model),
				"query", &query,
				"book", &book,
				NULL);
		print = e_contact_print_dialog_new(book, query);
		g_free(query);
		gtk_widget_show_all(print);
	} else if (view->view_type == E_ADDRESSBOOK_VIEW_TABLE) {
		GtkWidget *dialog;
		EPrintable *printable;
		ETable *etable;

		dialog = gnome_print_dialog_new("Print cards", GNOME_PRINT_DIALOG_RANGE | GNOME_PRINT_DIALOG_COPIES);
		gnome_print_dialog_construct_range_any(GNOME_PRINT_DIALOG(dialog), GNOME_PRINT_RANGE_ALL | GNOME_PRINT_RANGE_SELECTION,
						       NULL, NULL, NULL);

		gtk_object_get(GTK_OBJECT(view->widget), "table", &etable, NULL);
		printable = e_table_get_printable(etable);

		gtk_object_ref(GTK_OBJECT(view->widget));

		gtk_object_set_data(GTK_OBJECT(dialog), "table", view->widget);
		gtk_object_set_data(GTK_OBJECT(dialog), "printable", printable);
		
		gtk_signal_connect(GTK_OBJECT(dialog),
				   "clicked", GTK_SIGNAL_FUNC(e_contact_print_button), NULL);
		gtk_signal_connect(GTK_OBJECT(dialog),
				   "destroy", GTK_SIGNAL_FUNC(e_contact_print_destroy), NULL);
		gtk_widget_show(dialog);
	}
}

void
e_addressbook_view_print_preview(EAddressbookView *view)
{
	if (view->view_type == E_ADDRESSBOOK_VIEW_MINICARD) {
		char *query;
		EBook *book;

		gtk_object_get (GTK_OBJECT(view->model),
				"query", &query,
				"book", &book,
				NULL);
		e_contact_print_preview(book, query);
		g_free(query);
	} else if (view->view_type == E_ADDRESSBOOK_VIEW_TABLE) {
		EPrintable *printable;
		ETable *etable;
		GnomePrintMaster *master;
		GnomePrintContext *pc;
		GtkWidget *preview;

		gtk_object_get(GTK_OBJECT(view->widget), "table", &etable, NULL);
		printable = e_table_get_printable(etable);

		master = gnome_print_master_new();
		gnome_print_master_set_copies (master, 1, FALSE);
		pc = gnome_print_master_get_context( master );
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
		gnome_print_master_close(master);
		preview = GTK_WIDGET(gnome_print_master_preview_new(master, "Print Preview"));
		gtk_widget_show_all(preview);
		gtk_object_unref(GTK_OBJECT(master));
		gtk_object_unref(GTK_OBJECT(printable));
	}
}

static void
card_deleted_cb (EBook* book, EBookStatus status, gpointer user_data)
{
	if (status != E_BOOK_STATUS_SUCCESS) {
		e_addressbook_error_dialog (_("Error removing card"), status);
	}
}

static void
do_remove (int i, gpointer user_data)
{
	EBook *book;
	ECard *card;
	EAddressbookView *view = user_data;

	gtk_object_get (GTK_OBJECT(view->model),
			"book", &book,
			NULL);

	card = e_addressbook_model_get_card (view->model, i);

	e_book_remove_card(book, card, card_deleted_cb, view);

	gtk_object_unref (GTK_OBJECT (card));
}

void
e_addressbook_view_delete_selection(EAddressbookView *view)
{
	ESelectionModel *model = get_selection_model (view);

	g_return_if_fail (model);

	e_selection_model_foreach (model,
				   do_remove,
				   view);
}

static void
invisible_destroyed (GtkWidget *invisible, EAddressbookView *view)
{
	view->invisible = NULL;
}

static void
selection_get (GtkWidget *invisible,
	       GtkSelectionData *selection_data,
	       guint info,
	       guint time_stamp,
	       EAddressbookView *view)
{
	char *value;

	value = e_card_list_get_vcard(view->clipboard_cards);

	gtk_selection_data_set (selection_data, GDK_SELECTION_TYPE_STRING,
				8, value, strlen (value));
				
}

static void
selection_clear_event (GtkWidget *invisible,
		       GdkEventSelection *event,
		       EAddressbookView *view)
{
	if (view->clipboard_cards) {
		g_list_foreach (view->clipboard_cards, (GFunc)gtk_object_unref, NULL);
		g_list_free (view->clipboard_cards);
		view->clipboard_cards = NULL;
	}
}

static void
selection_received (GtkWidget *invisible,
		    GtkSelectionData *selection_data,
		    guint time,
		    EAddressbookView *view)
{
	if (selection_data->length < 0 || selection_data->type != GDK_SELECTION_TYPE_STRING) {
		return;
	}
	else {
		/* XXX make sure selection_data->data = \0 terminated */
		GList *card_list = e_card_load_cards_from_string_with_default_charset (selection_data->data, "ISO-8859-1");
		GList *l;
		
		if (!card_list /* it wasn't a vcard list */)
			return;

		for (l = card_list; l; l = l->next) {
			ECard *card = l->data;

			e_card_merging_book_add_card (view->book, card, NULL /* XXX */, NULL);
		}

		g_list_foreach (card_list, (GFunc)gtk_object_unref, NULL);
		g_list_free (card_list);
	}
}

static void
add_to_list (int model_row, gpointer closure)
{
	GList **list = closure;
	*list = g_list_prepend (*list, GINT_TO_POINTER (model_row));
}

static GList *
get_selected_cards (EAddressbookView *view)
{
	GList *list;
	GList *iterator;
	ESelectionModel *selection = get_selection_model (view);

	list = NULL;
	e_selection_model_foreach (selection, add_to_list, &list);

	for (iterator = list; iterator; iterator = iterator->next) {
		iterator->data = e_addressbook_model_card_at (view->model, GPOINTER_TO_INT (iterator->data));
		if (iterator->data)
			gtk_object_ref (iterator->data);
	}
	list = g_list_reverse (list);
	return list;
}

#if 0
void
e_addressbook_view_save_state (EAddressbookView *view, const char *filename)
{
	xmlDoc *doc;
	xmlNode *node;

	doc = xmlNewDoc ("1.0");
	node = xmlNewDocNode (doc, NULL, "addressbook-view", NULL);
	xmlDocSetRootElement (doc, node);

	switch (view->view_type) {
	case E_ADDRESSBOOK_VIEW_MINICARD: {
		int column_width;
		e_xml_set_string_prop_by_name (node, "style", "minicard");
		gtk_object_get (GTK_OBJECT (view->object),
				"column_width", &column_width,
				NULL);
		e_xml_set_integer_prop_by_name (node, "column-width", column_width);
		break;	
	}
	case E_ADDRESSBOOK_VIEW_TABLE: {
		ETableState *state;
		state = e_table_get_state_object (E_TABLE (view->widget));

		e_xml_set_string_prop_by_name (node, "style", "table");
		e_table_state_save_to_node (state, node);
		gtk_object_unref (GTK_OBJECT (state));
		break;
	}
	default:
		xmlFreeDoc(doc);
		return;
	}
	xmlSaveFile (filename, doc);
	xmlFreeDoc(doc);
}

void
e_addressbook_view_load_state (EAddressbookView *view, const char *filename)
{
	xmlDoc *doc;

	doc = xmlParseFile (filename);
	if (doc) {
		xmlNode *node;
		char *type;

		node = xmlDocGetRootElement (doc);
		type = e_xml_get_string_prop_by_name (node, "style");

		if (!strcmp (type, "minicard")) {
			int column_width;

			change_view_type (view, E_ADDRESSBOOK_VIEW_MINICARD);

			column_width = e_xml_get_integer_prop_by_name (node, "column-width");
			gtk_object_set (GTK_OBJECT (view->object),
					"column_width", column_width,
					NULL);
		} else if (!strcmp (type, "table")) {
			ETableState *state;

			change_view_type (view, E_ADDRESSBOOK_VIEW_TABLE);

			state = e_table_state_new();
			e_table_state_load_from_node (state, node->xmlChildrenNode);
			e_table_set_state_object (E_TABLE (view->widget), state);
			gtk_object_unref (GTK_OBJECT (state));
		}
		xmlFreeDoc(doc);
	}
}
#endif

void
e_addressbook_view_save_as (EAddressbookView *view)
{
	GList *list = get_selected_cards (view);
	if (list)
		e_contact_list_save_as (_("Save as VCard"), list, NULL);
	e_free_object_list(list);
}

void
e_addressbook_view_view (EAddressbookView *view)
{
	GList *list = get_selected_cards (view);
	e_addressbook_show_multiple_cards (view->book, list, view->editable);
	e_free_object_list(list);
}

void
e_addressbook_view_send (EAddressbookView *view)
{
	GList *list = get_selected_cards (view);
	if (list)
		e_card_list_send (list, E_CARD_DISPOSITION_AS_ATTACHMENT);
	e_free_object_list(list);
}

void
e_addressbook_view_send_to (EAddressbookView *view)
{
	GList *list = get_selected_cards (view);
	if (list)
		e_card_list_send (list, E_CARD_DISPOSITION_AS_TO);
	e_free_object_list(list);
}

void
e_addressbook_view_cut (EAddressbookView *view)
{
	e_addressbook_view_copy (view);
	e_addressbook_view_delete_selection (view);
}

void
e_addressbook_view_copy (EAddressbookView *view)
{
	view->clipboard_cards = get_selected_cards (view);

	gtk_selection_owner_set (view->invisible, clipboard_atom, GDK_CURRENT_TIME);
}

void
e_addressbook_view_paste (EAddressbookView *view)
{
	gtk_selection_convert (view->invisible, clipboard_atom,
			       GDK_SELECTION_TYPE_STRING,
			       GDK_CURRENT_TIME);
}

void
e_addressbook_view_select_all (EAddressbookView *view)
{
	ESelectionModel *model = get_selection_model (view);

	g_return_if_fail (model);

	e_selection_model_select_all (model);
}

void
e_addressbook_view_show_all(EAddressbookView *view)
{
	gtk_object_set(GTK_OBJECT(view),
		       "query", NULL,
		       NULL);
}

void
e_addressbook_view_stop(EAddressbookView *view)
{
	if (view)
		e_addressbook_model_stop (view->model);
}

static void
view_transfer_cards (EAddressbookView *view, gboolean delete_from_source)
{
	EBook *book;
	GList *cards;
	GtkWindow *parent_window;

	gtk_object_get(GTK_OBJECT(view->model), 
		       "book", &book,
		       NULL);
	cards = get_selected_cards (view);
	parent_window = GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (view)));

	e_addressbook_transfer_cards (book, cards, delete_from_source, parent_window);
}

void
e_addressbook_view_copy_to_folder (EAddressbookView *view)
{
	view_transfer_cards (view, FALSE);
}

void
e_addressbook_view_move_to_folder (EAddressbookView *view)
{
	view_transfer_cards (view, TRUE);
}


static gboolean
e_addressbook_view_selection_nonempty (EAddressbookView  *view)
{
	ESelectionModel *selection_model;

	selection_model = get_selection_model (view);
	if (selection_model == NULL)
		return FALSE;

	return e_selection_model_selected_count (selection_model) != 0;
}

gboolean
e_addressbook_view_can_create (EAddressbookView  *view)
{
	return view ? e_addressbook_model_editable (view->model) : FALSE;
}

gboolean
e_addressbook_view_can_print (EAddressbookView  *view)
{
	return view && view->model ? e_addressbook_model_card_count (view->model) : FALSE;
}

gboolean
e_addressbook_view_can_save_as (EAddressbookView  *view)
{
	return view ? e_addressbook_view_selection_nonempty (view) : FALSE;
}

gboolean
e_addressbook_view_can_view (EAddressbookView  *view)
{
	return view ? e_addressbook_view_selection_nonempty (view) : FALSE;
}

gboolean 
e_addressbook_view_can_send (EAddressbookView  *view)
{
	return view ? e_addressbook_view_selection_nonempty (view) : FALSE;
}

gboolean   
e_addressbook_view_can_send_to (EAddressbookView  *view)
{
	return view ? e_addressbook_view_selection_nonempty (view) : FALSE;
}

gboolean
e_addressbook_view_can_delete (EAddressbookView  *view)
{
	return view ? e_addressbook_view_selection_nonempty (view) && e_addressbook_model_editable (view->model) : FALSE;
}

gboolean
e_addressbook_view_can_cut (EAddressbookView *view)
{
	return view ? e_addressbook_view_selection_nonempty (view) && e_addressbook_model_editable (view->model) : FALSE;
}

gboolean
e_addressbook_view_can_copy (EAddressbookView *view)
{
	return view ? e_addressbook_view_selection_nonempty (view) : FALSE;
}

gboolean
e_addressbook_view_can_paste (EAddressbookView *view)
{
	return view ? e_addressbook_model_editable (view->model) : FALSE;
}

gboolean
e_addressbook_view_can_select_all (EAddressbookView *view)
{
	return view ? e_addressbook_model_card_count (view->model) != 0 : FALSE;
}

gboolean
e_addressbook_view_can_stop (EAddressbookView  *view)
{
	return view ? e_addressbook_model_can_stop (view->model) : FALSE;
}

gboolean
e_addressbook_view_can_copy_to_folder (EAddressbookView *view)
{
	return view ? e_addressbook_view_selection_nonempty (view) : FALSE;
}

gboolean
e_addressbook_view_can_move_to_folder (EAddressbookView *view)
{
	return view ? e_addressbook_view_selection_nonempty (view) && e_addressbook_model_editable (view->model) : FALSE;
}
