/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * e-table-field-chooser.c
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
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
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include <config.h>

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <libedataserver/e-data-server-util.h>
#include <libedataserverui/e-source-selector.h>
#include <e-util/e-util.h>
#include "eab-gui-util.h"
#include "util/eab-book-util.h"
#include <libebook/e-destination.h>
#include "e-util/e-error.h"
#include "e-util/e-html-utils.h"
#include "misc/e-image-chooser.h"
#include <e-util/e-icon-factory.h>
#include "eab-contact-merging.h"
#include <composer/e-msg-composer.h>
#include <mail/em-composer-utils.h>

/* we link to camel for decoding quoted printable email addresses */
#include <camel/camel-mime-utils.h>

#include "addressbook/gui/contact-editor/eab-editor.h"
#include "addressbook/gui/contact-editor/e-contact-editor.h"
#include "addressbook/gui/contact-list-editor/e-contact-list-editor.h"
#include "addressbook/gui/component/addressbook-component.h"
#include "addressbook/gui/component/addressbook.h"

/* the NULL's in this table correspond to the status codes
   that should *never* be generated by a backend */
static const char *status_to_string[] = {
	/* E_BOOK_ERROR_OK */                        		N_("Success"),
	/* E_BOOK_ERROR_INVALID_ARG */               		NULL,
	/* E_BOOK_ERROR_BUSY */                      		N_("Backend busy"),
	/* E_BOOK_ERROR_REPOSITORY_OFFLINE */        		N_("Repository offline"),
	/* E_BOOK_ERROR_NO_SUCH_BOOK */              		N_("Address Book does not exist"),
	/* E_BOOK_ERROR_NO_SELF_CONTACT */           		N_("No Self Contact defined"),
	/* E_BOOK_ERROR_URI_NOT_LOADED */            		NULL,
	/* E_BOOK_ERROR_URI_ALREADY_LOADED */        		NULL,
	/* E_BOOK_ERROR_PERMISSION_DENIED */         		N_("Permission denied"),
	/* E_BOOK_ERROR_CONTACT_NOT_FOUND */         		N_("Contact not found"),
	/* E_BOOK_ERROR_CONTACT_ID_ALREADY_EXISTS */ 		N_("Contact ID already exists"),
	/* E_BOOK_ERROR_PROTOCOL_NOT_SUPPORTED */    		N_("Protocol not supported"),
	/* E_BOOK_ERROR_CANCELLED */                 		N_("Canceled"),
	/* E_BOOK_ERROR_COULD_NOT_CANCEL */                     N_("Could not cancel"),
	/* E_BOOK_ERROR_AUTHENTICATION_FAILED */                N_("Authentication Failed"),
	/* E_BOOK_ERROR_AUTHENTICATION_REQUIRED */              N_("Authentication Required"),
	/* E_BOOK_ERROR_TLS_NOT_AVAILABLE */                    N_("TLS not Available"),
	/* E_BOOK_ERROR_CORBA_EXCEPTION */                      NULL,
	/* E_BOOK_ERROR_NO_SUCH_SOURCE */                       N_("No such source"),
	/* E_BOOK_ERROR_OFFLINE_UNAVAILABLE */			N_("Not available in offline mode"),
	/* E_BOOK_ERROR_OTHER_ERROR */                          N_("Other error"),
	/* E_BOOK_ERROR_INVALID_SERVER_VERSION */		N_("Invalid server version")
};

void
eab_error_dialog (const char *msg, EBookStatus status)
{
	const char *status_str = status_to_string [status];

	if (status_str)
		e_error_run (NULL, "addressbook:generic-error", msg, _(status_str), NULL);
}

void
eab_load_error_dialog (GtkWidget *parent, ESource *source, EBookStatus status)
{
	char *label_string, *label = NULL, *uri;
	GtkWidget *dialog;

	g_return_if_fail (source != NULL);

	uri = e_source_get_uri (source);

	if (status == E_BOOK_ERROR_OFFLINE_UNAVAILABLE) {
		label_string = _("We were unable to open this addressbook. This either means "
                                 "this book is not marked for offline usage or not yet downloaded "
                                 "for offline usage. Please load the addressbook once in online mode "
                                 "to download its contents");
	}

	else if (!strncmp (uri, "file:", 5)) {
		char *path = g_filename_from_uri (uri, NULL, NULL);
		label = g_strdup_printf (
			_("We were unable to open this addressbook.  Please check that the "
			  "path %s exists and that you have permission to access it."), path);
		g_free (path);
		label_string = label;
	}
	else if (!strncmp (uri, "ldap:", 5)) {
		/* special case for ldap: contact folders so we can tell the user about openldap */
#ifdef HAVE_LDAP
		label_string =
			_("We were unable to open this addressbook.  This either "
			  "means you have entered an incorrect URI, or the LDAP server "
			  "is unreachable.");
#else
		label_string =
			_("This version of Evolution does not have LDAP support "
			  "compiled in to it.  If you want to use LDAP in Evolution, "
			  "you must install an LDAP-enabled Evolution package.");
#endif
	} else {
		/* other network folders */
		label_string =
			_("We were unable to open this addressbook.  This either "
			  "means you have entered an incorrect URI, or the server "
			  "is unreachable.");
	}

	dialog  = e_error_new ((GtkWindow *) parent, "addressbook:load-error", label_string, NULL);
	g_signal_connect (dialog, "response", G_CALLBACK (gtk_widget_destroy), NULL);

	gtk_widget_show (dialog);
	g_free (label);
	g_free (uri);
}

void
eab_search_result_dialog      (GtkWidget *parent,
			       EBookViewStatus status)
{
	char *str = NULL;

	switch (status) {
	case E_BOOK_VIEW_STATUS_OK:
		return;
	case E_BOOK_VIEW_STATUS_SIZE_LIMIT_EXCEEDED:
		str = _("More cards matched this query than either the server is \n"
			"configured to return or Evolution is configured to display.\n"
			"Please make your search more specific or raise the result limit in\n"
			"the directory server preferences for this addressbook.");
		break;
	case E_BOOK_VIEW_STATUS_TIME_LIMIT_EXCEEDED:
		str = _("The time to execute this query exceeded the server limit or the limit\n"
			"you have configured for this addressbook.  Please make your search\n"
			"more specific or raise the time limit in the directory server\n"
			"preferences for this addressbook.");
		break;
	case E_BOOK_VIEW_ERROR_INVALID_QUERY:
		str = _("The backend for this addressbook was unable to parse this query.");
		break;
	case E_BOOK_VIEW_ERROR_QUERY_REFUSED:
		str = _("The backend for this addressbook refused to perform this query.");
		break;
	case E_BOOK_VIEW_ERROR_OTHER_ERROR:
		str = _("This query did not complete successfully.");
		break;
	default:
		g_return_if_reached ();
	}

	e_error_run ((GtkWindow *) parent, "addressbook:search-error", str, NULL);
}

gint
eab_prompt_save_dialog (GtkWindow *parent)
{
	return e_error_run (parent, "addressbook:prompt-save", NULL);
}

static void
added_cb (EBook* book, EBookStatus status, EContact *contact,
	  gpointer data)
{
	gboolean is_list = GPOINTER_TO_INT (data);

	if (status != E_BOOK_ERROR_OK && status != E_BOOK_ERROR_CANCELLED) {
		eab_error_dialog (is_list ? _("Error adding list") : _("Error adding contact"), status);
	}
}

static void
modified_cb (EBook* book, EBookStatus status, EContact *contact,
	     gpointer data)
{
	gboolean is_list = GPOINTER_TO_INT (data);

	if (status != E_BOOK_ERROR_OK && status != E_BOOK_ERROR_CANCELLED) {
		eab_error_dialog (is_list ? _("Error modifying list") : _("Error modifying contact"),
				  status);
	}
}

static void
deleted_cb (EBook* book, EBookStatus status, EContact *contact,
	    gpointer data)
{
	gboolean is_list = GPOINTER_TO_INT (data);

	if (status != E_BOOK_ERROR_OK) {
		eab_error_dialog (is_list ? _("Error removing list") : _("Error removing contact"),
				  status);
	}
}

static void
editor_closed_cb (GtkObject *editor, gpointer data)
{
	g_object_unref (editor);
}

EContactEditor *
eab_show_contact_editor (EBook *book, EContact *contact,
			 gboolean is_new_contact,
			 gboolean editable)
{
	EContactEditor *ce;

	ce = e_contact_editor_new (book, contact, is_new_contact, editable);

	g_signal_connect (ce, "contact_added",
			  G_CALLBACK (added_cb), GINT_TO_POINTER (FALSE));
	g_signal_connect (ce, "contact_modified",
			  G_CALLBACK (modified_cb), GINT_TO_POINTER (FALSE));
	g_signal_connect (ce, "contact_deleted",
			  G_CALLBACK (deleted_cb), GINT_TO_POINTER (FALSE));
	g_signal_connect (ce, "editor_closed",
			  G_CALLBACK (editor_closed_cb), NULL);

	return ce;
}

EContactListEditor *
eab_show_contact_list_editor (EBook *book, EContact *contact,
			      gboolean is_new_contact,
			      gboolean editable)
{
	EContactListEditor *ce;

	ce = e_contact_list_editor_new (book, contact, is_new_contact, editable);

	g_signal_connect (ce, "contact_added",
			  G_CALLBACK (added_cb), GINT_TO_POINTER (TRUE));
	g_signal_connect (ce, "contact_modified",
			  G_CALLBACK (modified_cb), GINT_TO_POINTER (TRUE));
	g_signal_connect (ce, "contact_deleted",
			  G_CALLBACK (deleted_cb), GINT_TO_POINTER (TRUE));
	g_signal_connect (ce, "editor_closed",
			  G_CALLBACK (editor_closed_cb), GINT_TO_POINTER (TRUE));

	eab_editor_show (EAB_EDITOR (ce));

	return ce;
}

static void
view_contacts (EBook *book, GList *list, gboolean editable)
{
	for (; list; list = list->next) {
		EContact *contact = list->data;
		if (e_contact_get (contact, E_CONTACT_IS_LIST))
			eab_show_contact_list_editor (book, contact, FALSE, editable);
		else
			eab_show_contact_editor (book, contact, FALSE, editable);
	}
}

void
eab_show_multiple_contacts (EBook *book,
			    GList *list,
			    gboolean editable)
{
	if (list) {
		int length = g_list_length (list);
		if (length > 5) {
			GtkWidget *dialog;
			gint response;

			dialog = gtk_message_dialog_new (NULL,
							 0,
							 GTK_MESSAGE_QUESTION,
							 GTK_BUTTONS_NONE,
							 ngettext("Opening %d contact will open %d new window as well.\n"
								  "Do you really want to display this contact?",
								  "Opening %d contacts will open %d new windows as well.\n"
								  "Do you really want to display all of these contacts?",
								  length),
							 length,
							 length);
			gtk_dialog_add_buttons (GTK_DIALOG (dialog),
						_("_Don't Display"), GTK_RESPONSE_NO,
						_("Display _All Contacts"), GTK_RESPONSE_YES,
						NULL);
			response = gtk_dialog_run (GTK_DIALOG (dialog));
			gtk_widget_destroy (dialog);
			if (response == GTK_RESPONSE_YES)
				view_contacts (book, list, editable);
		} else {
			view_contacts (book, list, editable);
		}
	}
}


static gint
file_exists(GtkWindow *window, const char *filename)
{
	GtkWidget *dialog;
	gint response;
	char * utf8_filename;

	utf8_filename = g_filename_to_utf8 (filename, -1, NULL, NULL, NULL);
	dialog = gtk_message_dialog_new (window,
					 0,
					 GTK_MESSAGE_QUESTION,
					 GTK_BUTTONS_NONE,
					 /* For Translators only: "it" refers to the filename %s. */
					 _("%s already exists\nDo you want to overwrite it?"), utf8_filename);
	g_free (utf8_filename);
	gtk_dialog_add_buttons (GTK_DIALOG (dialog),
				GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
				_("Overwrite"), GTK_RESPONSE_ACCEPT,
				NULL);

	response = gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);
	return response;
}

typedef struct {
	GtkWidget *filesel;
	char *vcard;
	gboolean has_multiple_contacts;
} SaveAsInfo;

static void
save_it(GtkWidget *widget, SaveAsInfo *info)
{
	const char *filename;
	char *uri;
	gint response = 0;


	filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (info->filesel));
	uri = gtk_file_chooser_get_uri (GTK_FILE_CHOOSER (info->filesel));

	if (filename && g_file_test (filename, G_FILE_TEST_EXISTS)) {
		response = file_exists(GTK_WINDOW (info->filesel), filename);
		switch (response) {
			case GTK_RESPONSE_ACCEPT : /* Overwrite */
				break;
			case GTK_RESPONSE_CANCEL : /* cancel */
				return;
		}
	}

	if (!e_write_file_uri (uri, info->vcard)) {
		char *err_str_ext;
		if (info->has_multiple_contacts) {
			/* more than one, finding the total number of contacts might
			 * hit performance while saving large number of contacts
			 */
			err_str_ext = ngettext ("contact", "contacts", 2);
		}
		else {
			err_str_ext = ngettext ("contact", "contacts", 1);
		}

		/* translators: Arguments, err_str_ext (item to be saved: "contact"/"contacts"),
		 * destination file name, and error code will fill the placeholders
		 * {0}, {1} and {2}, respectively in the error message formed
		 */
		e_error_run (GTK_WINDOW (info->filesel), "addressbook:save-error",
					 err_str_ext, filename, g_strerror (errno));
		gtk_widget_destroy(GTK_WIDGET(info->filesel));
		return;
	}

	gtk_widget_destroy(GTK_WIDGET(info->filesel));
}

static void
close_it(GtkWidget *widget, SaveAsInfo *info)
{
	gtk_widget_destroy (GTK_WIDGET (info->filesel));
}

static void
destroy_it(void *data, GObject *where_the_object_was)
{
	SaveAsInfo *info = data;
	g_free (info->vcard);
	g_free (info);
}

static void
filechooser_response (GtkWidget *widget, gint response_id, SaveAsInfo *info)
{
	if (response_id == GTK_RESPONSE_ACCEPT)
		save_it  (widget, info);
	else
		close_it (widget, info);
}

static char *
make_safe_filename (char *name)
{
	char *safe;

	if (!name) {
		/* This is a filename. Translators take note. */
		name = _("card.vcf");
	}

	if (!g_strrstr (name, ".vcf"))
		safe = g_strdup_printf ("%s%s", name, ".vcf");
	else
		safe = g_strdup (name);

	e_filename_make_safe (safe);

	return safe;
}

static void
source_selection_changed_cb (GtkWidget *selector, GtkWidget *ok_button)
{
	gtk_widget_set_sensitive (ok_button,
				  e_source_selector_peek_primary_selection (E_SOURCE_SELECTOR (selector)) ?
				  TRUE : FALSE);
}

ESource *
eab_select_source (const gchar *title, const gchar *message, const gchar *select_uid, GtkWindow *parent)
{
	ESource *source;
	ESourceList *source_list;
	GtkWidget *dialog;
	GtkWidget *ok_button;
	GtkWidget *cancel_button;
	/* GtkWidget *label; */
	GtkWidget *selector;
	GtkWidget *scrolled_window;
	gint response;

	if (!e_book_get_addressbooks (&source_list, NULL))
		return NULL;

	dialog = gtk_dialog_new_with_buttons (_("Select Address Book"), parent,
					      GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
					      NULL);
	gtk_window_set_default_size (GTK_WINDOW (dialog), 350, 300);

	cancel_button = gtk_dialog_add_button (GTK_DIALOG (dialog), GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
	ok_button = gtk_dialog_add_button (GTK_DIALOG (dialog), GTK_STOCK_OK, GTK_RESPONSE_ACCEPT);
	gtk_widget_set_sensitive (ok_button, FALSE);

	/* label = gtk_label_new (message); */

	selector = e_source_selector_new (source_list);
	e_source_selector_show_selection (E_SOURCE_SELECTOR (selector), FALSE);
	g_signal_connect (selector, "primary_selection_changed",
			  G_CALLBACK (source_selection_changed_cb), ok_button);

	if (select_uid) {
		source = e_source_list_peek_source_by_uid (source_list, select_uid);
		if (source)
			e_source_selector_set_primary_selection (E_SOURCE_SELECTOR (selector), source);
	}

	scrolled_window = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolled_window), GTK_SHADOW_IN);
	gtk_container_add (GTK_CONTAINER (scrolled_window), selector);

	/* gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), label, FALSE, FALSE, 4); */
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), scrolled_window, TRUE, TRUE, 4);

	gtk_widget_show_all (dialog);
	response = gtk_dialog_run (GTK_DIALOG (dialog));

	if (response == GTK_RESPONSE_ACCEPT)
		source = e_source_selector_peek_primary_selection (E_SOURCE_SELECTOR (selector));
	else
		source = NULL;

	gtk_widget_destroy (dialog);
	return source;
}

void
eab_contact_save (char *title, EContact *contact, GtkWindow *parent_window)
{
	GtkWidget *filesel;
	char *file;
	char *name;
	SaveAsInfo *info = g_new(SaveAsInfo, 1);

	name = e_contact_get (contact, E_CONTACT_FILE_AS);
	file = make_safe_filename (name);

	info->has_multiple_contacts = FALSE;

	filesel = gtk_file_chooser_dialog_new (title,
					       parent_window,
					       GTK_FILE_CHOOSER_ACTION_SAVE,
					       GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
					       GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT,
					       NULL);
	gtk_dialog_set_default_response (GTK_DIALOG (filesel), GTK_RESPONSE_ACCEPT);

	gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (filesel), g_get_home_dir ());
	gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (filesel), file);
	gtk_file_chooser_set_local_only (GTK_FILE_CHOOSER (filesel), FALSE);

	info->filesel = filesel;
	info->vcard = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);

	g_signal_connect (G_OBJECT (filesel), "response",
			  G_CALLBACK (filechooser_response), info);
	g_object_weak_ref (G_OBJECT (filesel), destroy_it, info);

	if (parent_window) {
		gtk_window_set_transient_for (GTK_WINDOW (filesel),
					      parent_window);
		gtk_window_set_modal (GTK_WINDOW (filesel), TRUE);
	}

	gtk_widget_show(GTK_WIDGET(filesel));
	g_free (file);
}

void
eab_contact_list_save (char *title, GList *list, GtkWindow *parent_window)
{
	GtkWidget *filesel;
	SaveAsInfo *info = g_new(SaveAsInfo, 1);
	char *file;

	filesel = gtk_file_chooser_dialog_new (title,
					       parent_window,
					       GTK_FILE_CHOOSER_ACTION_SAVE,
					       GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
					       GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT,
					       NULL);
	gtk_dialog_set_default_response (GTK_DIALOG (filesel), GTK_RESPONSE_ACCEPT);
	gtk_file_chooser_set_local_only (GTK_FILE_CHOOSER (filesel), FALSE);

	/* Check if the list has more than one contact */
	if (g_list_next (list))
		info->has_multiple_contacts = TRUE;
	else
		info->has_multiple_contacts = FALSE;

	/* This is a filename. Translators take note. */
	if (list && list->data && list->next == NULL) {
		char *name;
		name = e_contact_get (E_CONTACT (list->data), E_CONTACT_FILE_AS);
		if (!name)
			name = e_contact_get (E_CONTACT (list->data), E_CONTACT_FULL_NAME);

		file = make_safe_filename (name);
	} else {
		file = make_safe_filename (_("list"));
	}

	gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (filesel), g_get_home_dir ());
	gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (filesel), file);

	info->filesel = filesel;
	info->vcard = eab_contact_list_to_string (list);

	g_signal_connect (G_OBJECT (filesel), "response",
			  G_CALLBACK (filechooser_response), info);
	g_object_weak_ref (G_OBJECT (filesel), destroy_it, info);

	if (parent_window) {
		gtk_window_set_transient_for (GTK_WINDOW (filesel),
					      parent_window);
		gtk_window_set_modal (GTK_WINDOW (filesel), TRUE);
	}

	gtk_widget_show(GTK_WIDGET(filesel));
	g_free (file);
}

typedef struct ContactCopyProcess_ ContactCopyProcess;

typedef void (*ContactCopyDone) (ContactCopyProcess *process);

struct ContactCopyProcess_ {
	int count;
	gboolean book_status;
	GList *contacts;
	EBook *source;
	EBook *destination;
	ContactCopyDone done_cb;
};

#if 0
static void
contact_deleted_cb (EBook* book, EBookStatus status, gpointer user_data)
{
	if (status != E_BOOK_ERROR_OK) {
		eab_error_dialog (_("Error removing contact"), status);
	}
}
#endif

static void
do_delete (gpointer data, gpointer user_data)
{
	EBook *book = user_data;
	EContact *contact = data;
	const char *id;

	id = e_contact_get_const (contact, E_CONTACT_UID);
	e_book_remove_contact(book, id, NULL);
}

static void
delete_contacts (ContactCopyProcess *process)
{
	if (process->book_status == TRUE) {
		g_list_foreach (process->contacts,
				do_delete,
				process->source);
	}
}

static void
process_unref (ContactCopyProcess *process)
{
	process->count --;
	if (process->count == 0) {
		if (process->done_cb)
			process->done_cb (process);
		g_list_foreach (
			process->contacts,
			(GFunc) g_object_unref, NULL);
		g_list_free (process->contacts);
		g_object_unref (process->source);
		g_object_unref (process->destination);
		g_free (process);
	}
}

static void
contact_added_cb (EBook* book, EBookStatus status, const char *id, gpointer user_data)
{
	ContactCopyProcess *process = user_data;

	if (status != E_BOOK_ERROR_OK && status != E_BOOK_ERROR_CANCELLED) {
		process->book_status = FALSE;
		eab_error_dialog (_("Error adding contact"), status);
	}
	else if (status == E_BOOK_ERROR_CANCELLED) {
		process->book_status = FALSE;
	}
	else {
		/* success */
		process->book_status = TRUE;
	}
	process_unref (process);
}

static void
do_copy (gpointer data, gpointer user_data)
{
	EBook *book;
	EContact *contact;
	ContactCopyProcess *process;

	process = user_data;
	contact = data;

	book = process->destination;

	process->count ++;
	eab_merging_book_add_contact(book, contact, contact_added_cb, process);
}

static void
got_book_cb (EBook *book, EBookStatus status, gpointer closure)
{
	ContactCopyProcess *process;
	process = closure;
	if (status == E_BOOK_ERROR_OK) {
		process->destination = book;
		process->book_status = TRUE;
		g_object_ref (book);
		g_list_foreach (process->contacts,
				do_copy,
				process);
	}
	process_unref (process);
}

void
eab_transfer_contacts (EBook *source, GList *contacts /* adopted */, gboolean delete_from_source, GtkWindow *parent_window)
{
	EBook *dest;
	ESource *destination_source;
	static char *last_uid = NULL;
	ContactCopyProcess *process;
	char *desc;

	if (contacts == NULL)
		return;

	if (last_uid == NULL)
		last_uid = g_strdup ("");

	if (contacts->next == NULL) {
		if (delete_from_source)
			desc = _("Move contact to");
		else
			desc = _("Copy contact to");
	} else {
		if (delete_from_source)
			desc = _("Move contacts to");
		else
			desc = _("Copy contacts to");
	}

	destination_source = eab_select_source (desc, NULL,
						last_uid, parent_window);

	if (!destination_source)
		return;

	if (strcmp (last_uid, e_source_peek_uid (destination_source)) != 0) {
		g_free (last_uid);
		last_uid = g_strdup (e_source_peek_uid (destination_source));
	}

	process = g_new (ContactCopyProcess, 1);
	process->count = 1;
	process->book_status = FALSE;
	process->source = source;
	g_object_ref (source);
	process->contacts = contacts;
	process->destination = NULL;

	if (delete_from_source)
		process->done_cb = delete_contacts;
	else
		process->done_cb = NULL;

	dest = e_book_new (destination_source, NULL);
	addressbook_load (dest, got_book_cb, process);
}

typedef struct {
	EContact *contact;
	int email_num; /* if the contact is a person (not a list), the email address to use */
} ContactAndEmailNum;

static void
eab_send_to_contact_and_email_num_list (GList *contact_list)
{
	EMsgComposer *composer;
	EComposerHeaderTable *table;
	GPtrArray *to_array;
	GPtrArray *bcc_array;

	union {
		gpointer *pdata;
		EDestination **destinations;
	} convert;

	if (contact_list == NULL)
		return;

	composer = e_msg_composer_new ();
	table = e_msg_composer_get_header_table (composer);
	em_composer_utils_setup_default_callbacks (composer);

	to_array = g_ptr_array_new ();
	bcc_array = g_ptr_array_new ();

	/* Sort contacts into "To" and "Bcc" destinations. */
	while (contact_list != NULL) {
		ContactAndEmailNum *ce = contact_list->data;
		EContact *contact = ce->contact;
		EDestination *destination;

		destination = e_destination_new ();
		e_destination_set_contact (destination, contact, 0);

		if (e_destination_is_evolution_list (destination)) {
			if (e_destination_list_show_addresses (destination))
				g_ptr_array_add (to_array, destination);
			else
				g_ptr_array_add (bcc_array, destination);
		} else
			g_ptr_array_add (to_array, destination);

		contact_list = g_list_next (contact_list);
	}

	/* Add sentinels to each array. */
	g_ptr_array_add (to_array, NULL);
	g_ptr_array_add (bcc_array, NULL);

	/* XXX Acrobatics like this make me question whether NULL-terminated
	 *     arrays are really the best argument type for passing a list of
	 *     destinations to the header table. */

	/* Add "To" destinations. */
	convert.pdata = to_array->pdata;
	e_composer_header_table_set_destinations_to (
		table, convert.destinations);
	g_ptr_array_free (to_array, FALSE);
	e_destination_freev (convert.destinations);

	/* Add "Bcc" destinations. */
	convert.pdata = bcc_array->pdata;
	e_composer_header_table_set_destinations_bcc (
		table, convert.destinations);
	g_ptr_array_free (bcc_array, FALSE);
	e_destination_freev (convert.destinations);

	gtk_widget_show (GTK_WIDGET (composer));
}

static const char *
get_email (EContact *contact, EContactField field_id, gchar **to_free)
{
	char *name = NULL, *mail = NULL;
	const char *value = e_contact_get_const (contact, field_id);

	*to_free = NULL;

	if (eab_parse_qp_email (value, &name, &mail)) {
		*to_free = g_strdup_printf ("%s <%s>", name, mail);
		value = *to_free;
	}

	g_free (name);
	g_free (mail);

	return value;
}

static void
eab_send_contact_list_as_attachment (GList *contacts)
{
	EMsgComposer *composer;
	EComposerHeaderTable *table;
	CamelMimePart *attachment;
	gchar *data;

	if (contacts == NULL)
		return;

	composer = e_msg_composer_new ();
	table = e_msg_composer_get_header_table (composer);

	attachment = camel_mime_part_new ();
	data = eab_contact_list_to_string (contacts);

	camel_mime_part_set_content (
		attachment, data, strlen (data), "text/x-vcard");

	if (contacts->next != NULL)
		camel_mime_part_set_description (
			attachment, _("Multiple vCards"));
	else {
		EContact *contact = contacts->data;
		const gchar *file_as;
		gchar *description;

		file_as = e_contact_get_const (contact, E_CONTACT_FILE_AS);
		description = g_strdup_printf (_("vCard for %s"), file_as);
		camel_mime_part_set_description (attachment, description);
		g_free (description);
	}

	camel_mime_part_set_disposition (attachment, "attachment");

	e_msg_composer_attach (composer, attachment);
	camel_object_unref (attachment);

	if (contacts->next != NULL)
		e_composer_header_table_set_subject (
			table, _("Contact information"));
	else {
		EContact *contact = contacts->data;
		gchar *tempstr;
		const gchar *tempstr2;
		gchar *tempfree = NULL;

		tempstr2 = e_contact_get_const (contact, E_CONTACT_FILE_AS);
		if (!tempstr2 || !*tempstr2)
			tempstr2 = e_contact_get_const (contact, E_CONTACT_FULL_NAME);
		if (!tempstr2 || !*tempstr2)
			tempstr2 = e_contact_get_const (contact, E_CONTACT_ORG);
		if (!tempstr2 || !*tempstr2) {
			g_free (tempfree);
			tempstr2 = get_email (contact, E_CONTACT_EMAIL_1, &tempfree);
		}
		if (!tempstr2 || !*tempstr2) {
			g_free (tempfree);
			tempstr2 = get_email (contact, E_CONTACT_EMAIL_2, &tempfree);
		}
		if (!tempstr2 || !*tempstr2) {
			g_free (tempfree);
			tempstr2 = get_email (contact, E_CONTACT_EMAIL_3, &tempfree);
		}

		if (!tempstr2 || !*tempstr2)
			tempstr = g_strdup_printf (_("Contact information"));
		else
			tempstr = g_strdup_printf (_("Contact information for %s"), tempstr2);

		e_composer_header_table_set_subject (table, tempstr);

		g_free (tempstr);
		g_free (tempfree);
	}

	gtk_widget_show (GTK_WIDGET (composer));
}

void
eab_send_contact_list (GList *contacts, EABDisposition disposition)
{
	switch (disposition) {
	case EAB_DISPOSITION_AS_TO: {
		GList *list = NULL, *l;

		for (l = contacts; l; l = l->next) {
			ContactAndEmailNum *ce = g_new (ContactAndEmailNum, 1);
			ce->contact = l->data;
			ce->email_num = 0; /* hardcode this */

			list = g_list_append (list, ce);
		}

		eab_send_to_contact_and_email_num_list (list);

		g_list_foreach (list, (GFunc)g_free, NULL);
		g_list_free (list);
		break;
	}
	case EAB_DISPOSITION_AS_ATTACHMENT:
		eab_send_contact_list_as_attachment (contacts);
		break;
	}
}

void
eab_send_contact (EContact *contact, int email_num, EABDisposition disposition)
{
	GList *list = NULL;

	switch (disposition) {
	case EAB_DISPOSITION_AS_TO: {
		ContactAndEmailNum ce;

		ce.contact = contact;
		ce.email_num = email_num;

		list = g_list_prepend (NULL, &ce);
		eab_send_to_contact_and_email_num_list (list);
		break;
	}
	case EAB_DISPOSITION_AS_ATTACHMENT: {
		list = g_list_prepend (NULL, contact);
		eab_send_contact_list_as_attachment (list);
		break;
	}
	}

	g_list_free (list);
}

GtkWidget *
eab_create_image_chooser_widget(gchar *name,
				gchar *string1, gchar *string2,
				gint int1, gint int2)
{
	char *filename;
	GtkWidget *w = NULL;

	w = e_image_chooser_new ();
	gtk_widget_show_all (w);

	if (string1) {
		filename = e_icon_factory_get_icon_filename (string1, E_ICON_SIZE_DIALOG);

		e_image_chooser_set_from_file (E_IMAGE_CHOOSER (w), filename);

		g_free (filename);
	}

	return w;
}

/* To parse something like...
=?UTF-8?Q?=E0=A4=95=E0=A4=95=E0=A4=AC=E0=A5=82=E0=A5=8B=E0=A5=87?=\t\n=?UTF-8?Q?=E0=A4=B0?=\t\n<aa@aa.ccom>
and return the decoded representation of name & email parts.
*/
gboolean
eab_parse_qp_email (const gchar *string, gchar **name, gchar **email)
{
	struct _camel_header_address *address;
	gboolean res = FALSE;

	address = camel_header_address_decode (string, "UTF-8");

	if (!address)
		return FALSE;

	/* report success only when we have filled both name and email address */
	if (address->type == CAMEL_HEADER_ADDRESS_NAME  && address->name && *address->name && address->v.addr && *address->v.addr) {
		*name = g_strdup (address->name);
		*email = g_strdup (address->v.addr);
		res = TRUE;
	}

	camel_header_address_unref (address);

	return res;
}

/* This is only wrapper to parse_qp_mail, it decodes string and if returned TRUE,
   then makes one string and returns it, otherwise returns NULL.
   Returned string is usable to place directly into GtkHtml stream.
   Returned value should be freed with g_free. */
char *
eab_parse_qp_email_to_html (const gchar *string)
{
	char *name = NULL, *mail = NULL;
	char *html_name, *html_mail;
	char *value;

	if (!eab_parse_qp_email (string, &name, &mail))
		return NULL;

	html_name = e_text_to_html (name, 0);
	html_mail = e_text_to_html (mail, E_TEXT_TO_HTML_CONVERT_ADDRESSES);

	value = g_strdup_printf ("%s &lt;%s&gt;", html_name, html_mail);

	g_free (html_name);
	g_free (html_mail);
	g_free (name);
	g_free (mail);

	return value;
}
