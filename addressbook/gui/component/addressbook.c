/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * addressbook.c: 
 *
 * Author:
 *   Chris Lahey (clahey@ximian.com)
 *
 * (C) 2000 Ximian, Inc.
 */

#include <config.h>

#include <glib.h>
#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-i18n.h>
#include <libgnomeui/gnome-app.h>
#include <libgnomeui/gnome-stock.h>
#include <libgnomeui/gnome-uidefs.h>
#include <bonobo/bonobo-generic-factory.h>
#include <bonobo/bonobo-ui-util.h>
#include <gal/util/e-util.h>
#include <gal/widgets/e-unicode.h>

#include "select-names/e-select-names.h"
#include "select-names/e-select-names-manager.h"

#include "evolution-shell-component-utils.h"
#include "e-contact-editor.h"
#include "e-contact-save-as.h"
#include "addressbook-config.h"
#include "addressbook.h"
#include "addressbook/gui/search/e-addressbook-search-dialog.h"
#include "addressbook/gui/widgets/e-addressbook-view.h"
#include "addressbook/gui/widgets/e-addressbook-util.h"
#include "addressbook/printing/e-contact-print.h"

#include <ebook/e-book.h>
#include <widgets/misc/e-search-bar.h>
#include <widgets/misc/e-filter-bar.h>

#define PROPERTY_FOLDER_URI          "folder_uri"

#define PROPERTY_FOLDER_URI_IDX      1

typedef struct {
	EAddressbookView *view;
	ESearchBar *search;
	GtkWidget *vbox;
	BonoboControl *control;
	BonoboPropertyBag *properties;
	char *uri;
	char *passwd;
} AddressbookView;

static void
new_contact_cb (BonoboUIComponent *uih, void *user_data, const char *path)
{
	EBook *book;
	AddressbookView *view = (AddressbookView *) user_data;

	gtk_object_get(GTK_OBJECT(view->view),
		       "book", &book,
		       NULL);

	g_assert (E_IS_BOOK (book));

	e_addressbook_show_contact_editor (book, e_card_new(""), TRUE, e_addressbook_view_can_create(view->view));
}

static void
new_contact_list_cb (BonoboUIComponent *uih, void *user_data, const char *path)
{
	EBook *book;
	AddressbookView *view = (AddressbookView *) user_data;

	gtk_object_get(GTK_OBJECT(view->view),
		       "book", &book,
		       NULL);

	g_assert (E_IS_BOOK (book));

	e_addressbook_show_contact_list_editor (book, e_card_new(""), TRUE, e_addressbook_view_can_create(view->view));
}

static void
save_contact_cb (BonoboUIComponent *uih, void *user_data, const char *path)
{
	AddressbookView *view = (AddressbookView *) user_data;
	e_addressbook_view_save_as(view->view);
}

static void
config_cb (BonoboUIComponent *uih, void *user_data, const char *path)
{
	addressbook_config (NULL /* XXX */);
}

static void
search_cb (BonoboUIComponent *uih, void *user_data, const char *path)
{
	EBook *book;
	AddressbookView *view = (AddressbookView *) user_data;

	gtk_object_get(GTK_OBJECT(view->view),
		       "book", &book,
		       NULL);
	g_assert (E_IS_BOOK (book));

	gtk_widget_show(e_addressbook_search_dialog_new(book));
}

static void
delete_contact_cb (BonoboUIComponent *uih, void *user_data, const char *path)
{
	AddressbookView *view = (AddressbookView *) user_data;
	e_addressbook_view_delete_selection(view->view);
}

static void
print_cb (BonoboUIComponent *uih, void *user_data, const char *path)
{
	AddressbookView *view = (AddressbookView *) user_data;
	e_addressbook_view_print(view->view);
}

static void
stop_loading_cb (BonoboUIComponent *uih, void *user_data, const char *path)
{
	AddressbookView *view = (AddressbookView *) user_data;
	e_addressbook_view_stop(view->view);
}

static void
cut_contacts_cb (BonoboUIComponent *uih, void *user_data, const char *path)
{
	AddressbookView *view = (AddressbookView *) user_data;
	e_addressbook_view_cut(view->view);
}

static void
copy_contacts_cb (BonoboUIComponent *uih, void *user_data, const char *path)
{
	AddressbookView *view = (AddressbookView *) user_data;
	e_addressbook_view_copy(view->view);
}

static void
paste_contacts_cb (BonoboUIComponent *uih, void *user_data, const char *path)
{
	AddressbookView *view = (AddressbookView *) user_data;
	e_addressbook_view_paste(view->view);
}

static void
select_all_contacts_cb (BonoboUIComponent *uih, void *user_data, const char *path)
{
	AddressbookView *view = (AddressbookView *) user_data;
	e_addressbook_view_select_all (view->view);
}

static void
send_contact_cb (BonoboUIComponent *uih, void *user_data, const char *path)
{
	AddressbookView *view = (AddressbookView *) user_data;
	e_addressbook_view_send(view->view);
}

static void
send_contact_to_cb (BonoboUIComponent *uih, void *user_data, const char *path)
{
	AddressbookView *view = (AddressbookView *) user_data;
	e_addressbook_view_send_to(view->view);
}

static void
update_command_state (EAddressbookView *eav, AddressbookView *view)
{
	BonoboUIComponent *uic = bonobo_control_get_ui_component (view->control);

	/* New Contact */
	bonobo_ui_component_set_prop (uic,
				      "/commands/ContactNew",
				      "sensitive",
				      e_addressbook_view_can_create (view->view) ? "1" : "0", NULL);
	bonobo_ui_component_set_prop (uic,
				      "/commands/ContactNewList",
				      "sensitive",
				      e_addressbook_view_can_create (view->view) ? "1" : "0", NULL);

	bonobo_ui_component_set_prop (uic,
				      "/commands/ContactsSaveAsVCard",
				      "sensitive",
				      e_addressbook_view_can_save_as (view->view) ? "1" : "0", NULL);

	/* Print Contact */
	bonobo_ui_component_set_prop (uic,
				      "/commands/ContactsPrint",
				      "sensitive",
				      e_addressbook_view_can_print (view->view) ? "1" : "0", NULL);

	/* Delete Contact */
	bonobo_ui_component_set_prop (uic,
				      "/commands/ContactDelete",
				      "sensitive",
				      e_addressbook_view_can_delete (view->view) ? "1" : "0", NULL);

	bonobo_ui_component_set_prop (uic,
				      "/commands/ContactsCut",
				      "sensitive",
				      e_addressbook_view_can_cut (view->view) ? "1" : "0", NULL);
	bonobo_ui_component_set_prop (uic,
				      "/commands/ContactsCopy",
				      "sensitive",
				      e_addressbook_view_can_copy (view->view) ? "1" : "0", NULL);
	bonobo_ui_component_set_prop (uic,
				      "/commands/ContactsPaste",
				      "sensitive",
				      e_addressbook_view_can_paste (view->view) ? "1" : "0", NULL);
	bonobo_ui_component_set_prop (uic,
				      "/commands/ContactsSelectAll",
				      "sensitive",
				      e_addressbook_view_can_select_all (view->view) ? "1" : "0", NULL);

	bonobo_ui_component_set_prop (uic,
				      "/commands/ContactsSendContactToOther",
				      "sensitive",
				      e_addressbook_view_can_send (view->view) ? "1" : "0", NULL);

	bonobo_ui_component_set_prop (uic,
				      "/commands/ContactsSendMessageToContact",
				      "sensitive",
				      e_addressbook_view_can_send_to (view->view) ? "1" : "0", NULL);

	
	/* Stop */
	bonobo_ui_component_set_prop (uic,
				      "/commands/ContactStop",
				      "sensitive",
				      e_addressbook_view_can_stop (view->view) ? "1" : "0", NULL);
}

static void
change_view_type (AddressbookView *view, EAddressbookViewType view_type)
{
	gtk_object_set (GTK_OBJECT (view->view), "type", view_type, NULL);
}

BonoboUIVerb verbs [] = {
	BONOBO_UI_UNSAFE_VERB ("ContactsPrint", print_cb),
	BONOBO_UI_UNSAFE_VERB ("ContactsSaveAsVCard", save_contact_cb),
	BONOBO_UI_UNSAFE_VERB ("ToolSearch", search_cb),

	BONOBO_UI_UNSAFE_VERB ("AddressbookConfig", config_cb),

	BONOBO_UI_UNSAFE_VERB ("ContactNew", new_contact_cb),
	BONOBO_UI_UNSAFE_VERB ("ContactNewList", new_contact_list_cb),
	BONOBO_UI_UNSAFE_VERB ("ContactDelete", delete_contact_cb),
	BONOBO_UI_UNSAFE_VERB ("ContactStop", stop_loading_cb),

	BONOBO_UI_UNSAFE_VERB ("ContactsCut", cut_contacts_cb),
	BONOBO_UI_UNSAFE_VERB ("ContactsCopy", copy_contacts_cb),
	BONOBO_UI_UNSAFE_VERB ("ContactsPaste", paste_contacts_cb),
	BONOBO_UI_UNSAFE_VERB ("ContactsSelectAll", select_all_contacts_cb),

	BONOBO_UI_UNSAFE_VERB ("ContactsSendContactToOther", send_contact_cb),
	BONOBO_UI_UNSAFE_VERB ("ContactsSendMessageToContact", send_contact_to_cb),
	
	BONOBO_UI_VERB_END
};

static EPixmap pixmaps [] = {
	E_PIXMAP ("/menu/File/New/NewFirstItem/ContactNew", "evolution-contacts-mini.png"),
	E_PIXMAP ("/menu/File/Print/ContactsPrint", "print.xpm"),
	E_PIXMAP ("/menu/File/Print/ContactsPrintPreview", "print-preview.xpm"),
	E_PIXMAP ("/menu/Tools/Component/AddressbookConfig", "configure_16_addressbook.xpm"),

	E_PIXMAP ("/Toolbar/ContactNew", "new_contact.xpm"),
	E_PIXMAP ("/Toolbar/ContactNewList", "all_contacts.xpm"),

	E_PIXMAP_END
};

static void
control_activate (BonoboControl     *control,
		  BonoboUIComponent *uic,
		  AddressbookView   *view)
{
	Bonobo_UIContainer remote_ui_container;

	remote_ui_container = bonobo_control_get_remote_ui_container (control);
	bonobo_ui_component_set_container (uic, remote_ui_container);
	bonobo_object_release_unref (remote_ui_container, NULL);

	bonobo_ui_component_add_verb_list_with_data (
		uic, verbs, view);
	
	bonobo_ui_component_freeze (uic, NULL);

	bonobo_ui_util_set_ui (uic, EVOLUTION_DATADIR,
			       "evolution-addressbook.xml",
			       "evolution-addressbook");

	e_addressbook_view_setup_menus (view->view, uic);

	e_pixmaps_update (uic, pixmaps);

	bonobo_ui_component_thaw (uic, NULL);

	update_command_state (view->view, view);
}

static void
control_activate_cb (BonoboControl *control, 
		     gboolean activate, 
		     AddressbookView *view)
{
	BonoboUIComponent *uic;

	uic = bonobo_control_get_ui_component (control);
	g_assert (uic != NULL);
	
	if (activate)
		control_activate (control, uic, view);
	else {
		bonobo_ui_component_unset_container (uic);
		e_addressbook_view_discard_menus (view->view);
	}
}

static void
addressbook_view_free(AddressbookView *view)
{
	EBook *book;
	
	gtk_object_get(GTK_OBJECT(view->view),
		       "book", &book,
		       NULL);
	if (view->uri)
		gtk_object_unref (GTK_OBJECT (book));
	
	if (view->properties)
		bonobo_object_unref(BONOBO_OBJECT(view->properties));
	g_free(view->passwd);
	g_free(view->uri);
	g_free(view);
}

static void
book_auth_cb (EBook *book, EBookStatus status, gpointer closure)
{
	AddressbookView *view = closure;
	if (status == E_BOOK_STATUS_SUCCESS) {
		gtk_object_set(GTK_OBJECT(view->view),
			       "book", book,
			       NULL);
	}
	else {
		/* pop up a nice dialog, or redo the authentication
                   bit some number of times. */
	}
}

static void
passwd_cb (gchar *string, gpointer data)
{
	AddressbookView *view = (AddressbookView*)data;

	view->passwd = g_strdup (string);
}

static void
book_open_cb (EBook *book, EBookStatus status, gpointer closure)
{
	AddressbookView *view = closure;
	AddressbookSource *source;
	source = addressbook_storage_get_source_by_uri (view->uri);

	if (status == E_BOOK_STATUS_SUCCESS) {
		/* check if the addressbook needs authentication */

		if (source &&
		    source->type == ADDRESSBOOK_SOURCE_LDAP &&
		    source->ldap.auth == ADDRESSBOOK_LDAP_AUTH_SIMPLE) {
			int button;
			char *msg = g_strdup_printf (_("Enter password for %s"), source->ldap.binddn);
			/* give a password prompt for the binddn */
			GtkWidget *dialog = gnome_request_dialog (TRUE, msg, NULL,
								  0, passwd_cb, view, NULL); 
			
			button = gnome_dialog_run_and_close (GNOME_DIALOG (dialog));

			if (button == 0 && *(view->passwd)) {
				e_book_authenticate_user (book, source->ldap.binddn, view->passwd,
							  book_auth_cb, closure);
				memset (view->passwd, 0, strlen (view->passwd)); /* clear out the passwd */
				g_free (view->passwd);
				view->passwd = NULL;
				return;
			}
		}


		/* if they either didn't configure the source to use
                   authentication, or they canceled the dialog,
                   proceed without authenticating */
		gtk_object_set(GTK_OBJECT(view->view),
			       "book", book,
			       NULL);

	} else {
		GtkWidget *warning_dialog, *label;
        	warning_dialog = gnome_dialog_new (
        		_("Unable to open addressbook"),
			GNOME_STOCK_BUTTON_CLOSE,
        		NULL);

		if (source->type == ADDRESSBOOK_SOURCE_LDAP) {
#if HAVE_LDAP
			label = gtk_label_new (
					       _("We were unable to open this addressbook.  This either\n"
						 "means you have entered an incorrect URI, or the LDAP server\n"
						 "is down"));
#else
			label = gtk_label_new (
					       _("This version of Evolution does not have LDAP support\n"
						 "compiled in to it.  If you want to use LDAP in Evolution\n"
						 "you must compile the program from the CVS sources after\n"
						 "retrieving OpenLDAP from the link below.\n"));
#endif
		}
		else {
			label = gtk_label_new (
					       _("We were unable to open this addressbook.  Please check that the\n"
						 "path exists and that you have permission to access it."));
		}

		gtk_misc_set_alignment(GTK_MISC(label),
				       0, .5);
		gtk_label_set_justify(GTK_LABEL(label),
				      GTK_JUSTIFY_LEFT);

		gtk_box_pack_start (GTK_BOX (GNOME_DIALOG (warning_dialog)->vbox), 
				    label, TRUE, TRUE, 0);
		gtk_widget_show (label);

#ifndef HAVE_LDAP
		if (source->type == ADDRESSBOOK_SOURCE_LDAP) {
			GtkWidget *href;
			href = gnome_href_new ("http://www.openldap.org/", "OpenLDAP at http://www.openldap.org/");
			gtk_box_pack_start (GTK_BOX (GNOME_DIALOG (warning_dialog)->vbox), 
					    href, FALSE, FALSE, 0);
			gtk_widget_show (href);
		}
#endif
		gnome_dialog_run (GNOME_DIALOG (warning_dialog));
		
		gtk_object_destroy (GTK_OBJECT (warning_dialog));
	}
}

static void destroy_callback(GtkWidget *widget, gpointer data)
{
	AddressbookView *view = data;
	addressbook_view_free(view);
}

static void
get_prop (BonoboPropertyBag *bag,
	  BonoboArg         *arg,
	  guint              arg_id,
	  CORBA_Environment *ev,
	  gpointer           user_data)
{
	AddressbookView *view = user_data;

	switch (arg_id) {

	case PROPERTY_FOLDER_URI_IDX:
		if (view && view->uri)
			BONOBO_ARG_SET_STRING (arg, view->uri);
		else
			BONOBO_ARG_SET_STRING (arg, "");
		break;

	default:
		g_warning ("Unhandled arg %d\n", arg_id);
	}
}

char *
addressbook_expand_uri (const char *uri)
{
	char *new_uri;

	if (!strncmp (uri, "file:", 5)) {
		if (strlen (uri + 7) > 3
		    && !strcmp (uri + strlen(uri) - 3, ".db")) {
			/* it's a .db file */
			new_uri = g_strdup (uri);
		}
		else {
			char *file_name;
			/* we assume it's a dir and glom addressbook.db onto the end. */
			file_name = g_concat_dir_and_file(uri + 7, "addressbook.db");
			new_uri = g_strdup_printf("file://%s", file_name);
			g_free(file_name);
		}
	}
	else {
		new_uri = g_strdup (uri);
	}

	return new_uri;
}


static void
set_prop (BonoboPropertyBag *bag,
	  const BonoboArg   *arg,
	  guint              arg_id,
	  CORBA_Environment *ev,
	  gpointer           user_data)
{
	AddressbookView *view = user_data;

	char *uri_data;
	EBook *book;
	
	switch (arg_id) {

	case PROPERTY_FOLDER_URI_IDX:
		gtk_object_get(GTK_OBJECT(view->view),
			       "book", &book,
			       NULL);
		if (view->uri) {
			/* we've already had a uri set on this view, so unload it */
			e_book_unload_uri (book);
			g_free (view->uri);
		} else {
			book = e_book_new ();
		}

		view->uri = g_strdup(BONOBO_ARG_GET_STRING (arg));
		
		uri_data = addressbook_expand_uri (view->uri);

		if (! e_book_load_uri (book, uri_data, book_open_cb, view))
			printf ("error calling load_uri!\n");

		g_free(uri_data);

		break;
		
	default:
		g_warning ("Unhandled arg %d\n", arg_id);
		break;
	}
}

static ESearchBarItem addressbook_search_menu_items[] = {
	E_FILTERBAR_RESET,
	{ NULL, 0 },
	E_FILTERBAR_ADVANCED,
	{ NULL, -1 }
};

static void
addressbook_menu_activated (ESearchBar *esb, int id, AddressbookView *view)
{
	EBook *book;
	switch (id) {
	case E_FILTERBAR_RESET_ID:
		e_addressbook_view_show_all(view->view);
		break;
	case E_FILTERBAR_ADVANCED_ID:
		gtk_object_get(GTK_OBJECT(view->view),
			       "book", &book,
			       NULL);
		g_assert (E_IS_BOOK (book));

		gtk_widget_show(e_addressbook_search_dialog_new(book));
		break;
	}
}

enum {
	ESB_ANY,
	ESB_FULL_NAME,
	ESB_EMAIL,
	ESB_CATEGORY,
};

static ESearchBarItem addressbook_search_option_items[] = {
	{ N_("Any field contains"), ESB_ANY },
	{ N_("Name contains"), ESB_FULL_NAME },
	{ N_("Email contains"), ESB_EMAIL },
	{ N_("Category contains"), ESB_CATEGORY },
	{ NULL, -1 }
};

static void
addressbook_query_changed (ESearchBar *esb, AddressbookView *view)
{
	char *search_word, *search_query;
	int search_type;

	gtk_object_get(GTK_OBJECT(esb),
		       "text", &search_word,
		       "option_choice", &search_type,
		       NULL);

	if (search_word && strlen (search_word)) {
		switch (search_type) {
		case ESB_ANY:
			search_query = g_strdup_printf ("(contains \"x-evolution-any-field\" \"%s\")",
							search_word);
			break;
		case ESB_FULL_NAME:
			search_query = g_strdup_printf ("(contains \"full_name\" \"%s\")",
							search_word);
			break;
		case ESB_EMAIL:
			search_query = g_strdup_printf ("(contains \"email\" \"%s\")",
							search_word);
			break;
		case ESB_CATEGORY:
			search_query = g_strdup_printf ("(contains \"category\" \"%s\")",
							search_word);
			break;
		default:
			search_query = g_strdup ("(contains \"full_name\" \"\")");
			break;
		}
	} else
		search_query = g_strdup ("(contains \"full_name\" \"\")");

	gtk_object_set (GTK_OBJECT(view->view),
			"query", search_query,
			NULL);

	g_free (search_query);
	g_free (search_word);
}

static GNOME_Evolution_ShellView
retrieve_shell_view_interface_from_control (BonoboControl *control)
{
	Bonobo_ControlFrame control_frame;
	GNOME_Evolution_ShellView shell_view_interface;
	CORBA_Environment ev;

	shell_view_interface = gtk_object_get_data (GTK_OBJECT (control),
						    "shell_view_interface");

	if (shell_view_interface)
		return shell_view_interface;

	control_frame = bonobo_control_get_control_frame (control);

	if (control_frame == NULL)
		return CORBA_OBJECT_NIL;

	CORBA_exception_init (&ev);
	shell_view_interface = Bonobo_Unknown_queryInterface (control_frame,
							       "IDL:GNOME/Evolution/ShellView:1.0",
							       &ev);
	CORBA_exception_free (&ev);

	if (shell_view_interface != CORBA_OBJECT_NIL)
		gtk_object_set_data (GTK_OBJECT (control),
				     "shell_view_interface",
				     shell_view_interface);
	else
		g_warning ("Control frame doesn't have Evolution/ShellView.");

	return shell_view_interface;
}

static void
set_status_message (EAddressbookView *eav, const char *message, AddressbookView *view)
{
	CORBA_Environment ev;
	GNOME_Evolution_ShellView shell_view_interface;

	CORBA_exception_init (&ev);

	shell_view_interface = retrieve_shell_view_interface_from_control (view->control);
	if (!shell_view_interface) {
		CORBA_exception_free (&ev);
		return;
	}

	if (message == NULL || message[0] == 0) {
		GNOME_Evolution_ShellView_unsetMessage (shell_view_interface, &ev);
	}
	else {
		GNOME_Evolution_ShellView_setMessage (shell_view_interface,
						      message,
						      e_addressbook_view_can_stop (view->view), &ev);
	}

	CORBA_exception_free (&ev);
}

BonoboControl *
addressbook_factory_new_control (void)
{
	AddressbookView *view;
	GtkFrame *frame;

	frame = gtk_frame_new (NULL);
	gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_IN);

	view = g_new0 (AddressbookView, 1);

	view->vbox = gtk_vbox_new (FALSE, 0);

	gtk_signal_connect (GTK_OBJECT (view->vbox), "destroy",
			    GTK_SIGNAL_FUNC (destroy_callback),
			    (gpointer) view);

	/* Create the control. */
	view->control = bonobo_control_new (view->vbox);

	view->search = E_SEARCH_BAR(e_search_bar_new(addressbook_search_menu_items,
						     addressbook_search_option_items));
	gtk_box_pack_start (GTK_BOX (view->vbox), GTK_WIDGET (view->search),
			    FALSE, FALSE, 0);
	gtk_signal_connect (GTK_OBJECT (view->search), "query_changed",
			    GTK_SIGNAL_FUNC (addressbook_query_changed), view);
	gtk_signal_connect (GTK_OBJECT (view->search), "menu_activated",
			    GTK_SIGNAL_FUNC (addressbook_menu_activated), view);

	view->view = E_ADDRESSBOOK_VIEW(e_addressbook_view_new());
	gtk_container_add (GTK_CONTAINER (frame), view->view);
	gtk_box_pack_start (GTK_BOX (view->vbox), GTK_WIDGET(frame),
			    TRUE, TRUE, 0);

	/* create the initial view */
	change_view_type (view, E_ADDRESSBOOK_VIEW_MINICARD);

	gtk_widget_show (frame);
	gtk_widget_show (view->vbox);
	gtk_widget_show (GTK_WIDGET(view->view));
	gtk_widget_show (GTK_WIDGET(view->search));

	view->properties = bonobo_property_bag_new (get_prop, set_prop, view);

	bonobo_property_bag_add (view->properties,
				 PROPERTY_FOLDER_URI, PROPERTY_FOLDER_URI_IDX,
				 BONOBO_ARG_STRING, NULL, _("The URI that the Folder Browser will display"), 0);

	bonobo_control_set_properties (view->control,
				       view->properties);

	gtk_signal_connect (GTK_OBJECT (view->view),
			    "status_message",
			    GTK_SIGNAL_FUNC(set_status_message),
			    view);

	gtk_signal_connect (GTK_OBJECT (view->view),
			    "command_state_change",
			    GTK_SIGNAL_FUNC(update_command_state),
			    view);
	
	view->uri = NULL;

	gtk_signal_connect (GTK_OBJECT (view->control), "activate",
			    control_activate_cb, view);

	return view->control;
}

static BonoboObject *
addressbook_factory (BonoboGenericFactory *Factory, void *closure)
{
	return BONOBO_OBJECT (addressbook_factory_new_control ());
}

void
addressbook_factory_init (void)
{
	static BonoboGenericFactory *addressbook_control_factory = NULL;

	if (addressbook_control_factory != NULL)
		return;

	addressbook_control_factory = bonobo_generic_factory_new (
		"OAFIID:GNOME_Evolution_Addressbook_ControlFactory",
		addressbook_factory, NULL);

	if (addressbook_control_factory == NULL) {
		g_error ("I could not register a Addressbook factory.");
	}
}
