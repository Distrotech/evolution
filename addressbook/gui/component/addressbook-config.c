/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <config.h>

#include <glib.h>
#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-i18n.h>
#include <libgnomeui/gnome-dialog.h>
#include <libgnomeui/gnome-stock.h>
#include <glade/glade.h>
#include <gal/widgets/e-unicode.h>
#include <gtkhtml/gtkhtml.h>
#include <e-util/e-html-utils.h>
#include "addressbook-config.h"
#include "addressbook-storage.h"

typedef struct _AddressbookSourceDialog AddressbookSourceDialog;
typedef struct _AddressbookSourcePageItem  AddressbookSourcePageItem;

struct _AddressbookSourceDialog {
	GladeXML *gui;

	GtkWidget *dialog;

	GtkWidget *notebook;
	GtkWidget *basic_notebook;
	GtkWidget *advanced_notebook;

	GtkWidget *name;
	GtkWidget *host;

	GtkWidget *port;
	GtkWidget *rootdn;
	GtkWidget *scope_optionmenu;
	AddressbookLDAPScopeType ldap_scope;
	GtkWidget *auth_checkbutton;
#if the_ui_gods_smile_upon_me
	GtkWidget *remember_checkbutton;
#endif

	gint id; /* button we closed the dialog with */

	AddressbookSource *source; /* our result if the Ok button was clicked */
};

static void
addressbook_source_edit_changed (GtkWidget *item, AddressbookSourceDialog *dialog)
{
	char *data;
	gboolean complete = TRUE;

	if (complete) {
		data = e_utf8_gtk_editable_get_chars (GTK_EDITABLE (dialog->name), 0, -1);
		if (!data || !*data)
			complete = FALSE;
		g_free (data);
	}

	if (complete) {
		if (complete) {
			data = e_utf8_gtk_editable_get_chars (GTK_EDITABLE (dialog->host), 0, -1);
			if (!data || !*data)
				complete = FALSE;
			g_free (data);
		}

		if (complete) {
			data = e_utf8_gtk_editable_get_chars (GTK_EDITABLE (dialog->port), 0, -1);
			if (!data || !*data)
				complete = FALSE;
				/* XXX more validation on port here */
			g_free (data);
		}
	}

	gnome_dialog_set_sensitive (GNOME_DIALOG (dialog->dialog), 0, complete);
}

static void
auth_checkbutton_changed (GtkWidget *item, AddressbookSourceDialog *dialog)
{
	/* make sure the change is reflected by the state of the dialog's OK button */
	addressbook_source_edit_changed (item, dialog);

#if the_ui_gods_smile_upon_me
	gtk_widget_set_sensitive (dialog->remember_checkbutton,
				  gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON(dialog->auth_checkbutton)));
#endif
}

static void
option_menuitem_activated (GtkWidget *item, AddressbookSourceDialog *dialog)
{
	/* make sure the change is reflected by the state of the dialog's OK button */
	addressbook_source_edit_changed (item, dialog);

	dialog->ldap_scope = g_list_index (gtk_container_children (GTK_CONTAINER (item->parent)),
					   item);
}

typedef struct {
	GtkWidget *notebook;
	int page_num;
} FocusHelpClosure;

static void
focus_help (GtkWidget *w, GdkEventFocus *event, FocusHelpClosure *closure)
{
	gtk_notebook_set_page (GTK_NOTEBOOK(closure->notebook), closure->page_num);
}

static void
add_focus_handler (GtkWidget *widget, GtkWidget *notebook, int page_num)
{
	FocusHelpClosure *focus_closure = g_new0 (FocusHelpClosure, 1);
	focus_closure->notebook = notebook;
	focus_closure->page_num = page_num;

	gtk_signal_connect_full (GTK_OBJECT (widget),
				 "focus_in_event" /* XXX */,
				 (GtkSignalFunc) focus_help, NULL,
				 focus_closure,
				 (GtkDestroyNotify) g_free,
				 FALSE, FALSE);
}

static void
addressbook_source_dialog_set_source (AddressbookSourceDialog *dialog, AddressbookSource *source)
{
	e_utf8_gtk_entry_set_text (GTK_ENTRY (dialog->name), source ? source->name : "");
	e_utf8_gtk_entry_set_text (GTK_ENTRY (dialog->host), source ? source->host : "");
	e_utf8_gtk_entry_set_text (GTK_ENTRY (dialog->port), source ? source->port : "389");
	e_utf8_gtk_entry_set_text (GTK_ENTRY (dialog->rootdn), source ? source->rootdn : "");

	gtk_option_menu_set_history (GTK_OPTION_MENU(dialog->scope_optionmenu), source ? source->scope : ADDRESSBOOK_LDAP_SCOPE_ONELEVEL);
	dialog->ldap_scope = source ? source->scope : ADDRESSBOOK_LDAP_SCOPE_ONELEVEL;

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(dialog->auth_checkbutton), source && source->auth == ADDRESSBOOK_LDAP_AUTH_SIMPLE);
#if the_ui_gods_smile_upon_me
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(dialog->remember_checkbutton), source && source->remember_passwd);
#endif
}

static AddressbookSource *
addressbook_source_dialog_get_source (AddressbookSourceDialog *dialog)
{
	AddressbookSource *source = g_new0 (AddressbookSource, 1);

	source->name   = e_utf8_gtk_entry_get_text (GTK_ENTRY (dialog->name));
	source->host   = e_utf8_gtk_entry_get_text (GTK_ENTRY (dialog->host));
	source->port   = e_utf8_gtk_entry_get_text (GTK_ENTRY (dialog->port));
	source->rootdn = e_utf8_gtk_entry_get_text (GTK_ENTRY (dialog->rootdn));
	source->scope  = dialog->ldap_scope;
	source->auth   = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (dialog->auth_checkbutton)) ? ADDRESSBOOK_LDAP_AUTH_SIMPLE : ADDRESSBOOK_LDAP_AUTH_NONE;
#if the_ui_gods_smile_upon_me
	source->remember_passwd   = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (dialog->remember_checkbutton));
#endif

	addressbook_storage_init_source_uri (source);

	return source;
}

static void
addressbook_source_dialog_ok_clicked (GtkWidget *widget, AddressbookSourceDialog *dialog)
{
	dialog->source = addressbook_source_dialog_get_source (dialog);
}

static void
add_activate_cb (GtkWidget *item, AddressbookSourceDialog *dialog)
{
	gtk_signal_connect (GTK_OBJECT (item), "activate",
			    GTK_SIGNAL_FUNC (option_menuitem_activated), dialog);
}

static AddressbookSourceDialog*
addressbook_source_dialog (GladeXML *gui, AddressbookSource *source, GtkWidget *parent)
{
	AddressbookSourceDialog *dialog = g_new0 (AddressbookSourceDialog, 1);
	GtkWidget *menu;

	dialog->gui = gui;

	dialog->dialog = glade_xml_get_widget (gui, "add_addressbook");

	if (source)
		gtk_window_set_title (GTK_WINDOW (dialog->dialog), _("Edit Addressbook"));

	gtk_window_set_modal (GTK_WINDOW (dialog->dialog), TRUE);
	gtk_window_set_policy (GTK_WINDOW (dialog->dialog), 
			       FALSE, TRUE, FALSE);

	gnome_dialog_set_parent (GNOME_DIALOG (dialog->dialog),
				 GTK_WINDOW (parent));

	dialog->notebook = glade_xml_get_widget (gui, "add-addressbook-notebook");
	dialog->basic_notebook = glade_xml_get_widget (gui, "basic-notebook");
	dialog->advanced_notebook = glade_xml_get_widget (gui, "advanced-notebook");

	/* BASIC STUFF */
	dialog->name = glade_xml_get_widget (gui, "account-name-entry");
	gtk_signal_connect (GTK_OBJECT (dialog->name), "changed",
			    GTK_SIGNAL_FUNC (addressbook_source_edit_changed), dialog);
	add_focus_handler (dialog->name, dialog->basic_notebook, 0);

	dialog->host = glade_xml_get_widget (gui, "server-name-entry");
	gtk_signal_connect (GTK_OBJECT (dialog->host), "changed",
			    GTK_SIGNAL_FUNC (addressbook_source_edit_changed), dialog);
	add_focus_handler (dialog->host, dialog->basic_notebook, 1);

	dialog->auth_checkbutton = glade_xml_get_widget (gui, "auth-checkbutton");
	add_focus_handler (dialog->auth_checkbutton, dialog->basic_notebook, 2);
	gtk_signal_connect (GTK_OBJECT (dialog->auth_checkbutton), "toggled",
			    GTK_SIGNAL_FUNC (auth_checkbutton_changed), dialog);

#if the_ui_gods_smile_upon_me
	dialog->remember_checkbutton = glade_xml_get_widget (gui, "remember-checkbutton");
	add_focus_handler (dialog->auth_checkbutton, dialog->basic_notebook, 3);
	gtk_signal_connect (GTK_OBJECT (dialog->remember_checkbutton), "toggled",
			    GTK_SIGNAL_FUNC (addressbook_source_edit_changed), dialog);
	gtk_widget_set_sensitive (dialog->remember_checkbutton,
				  gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON(dialog->auth_checkbutton)));
#endif

	/* ADVANCED STUFF */
	dialog->port = glade_xml_get_widget (gui, "port-entry");
	gtk_signal_connect (GTK_OBJECT (dialog->port), "changed",
			    GTK_SIGNAL_FUNC (addressbook_source_edit_changed), dialog);
	add_focus_handler (dialog->port, dialog->advanced_notebook, 0);

	dialog->rootdn = glade_xml_get_widget (gui, "rootdn-entry");
	gtk_signal_connect (GTK_OBJECT (dialog->rootdn), "changed",
			    GTK_SIGNAL_FUNC (addressbook_source_edit_changed), dialog);
	add_focus_handler (dialog->rootdn, dialog->advanced_notebook, 1);

	dialog->scope_optionmenu = glade_xml_get_widget (gui, "scope-optionmenu");
	add_focus_handler (dialog->scope_optionmenu, dialog->advanced_notebook, 2);
	menu = gtk_option_menu_get_menu (GTK_OPTION_MENU(dialog->scope_optionmenu));
	gtk_container_foreach (GTK_CONTAINER (menu), (GtkCallback)add_activate_cb, dialog);

	/* fill in source info if there is some */
	addressbook_source_dialog_set_source (dialog, source);

	/* always start out on the first page. */
	gtk_notebook_set_page (GTK_NOTEBOOK (dialog->notebook), 0);

	gnome_dialog_set_sensitive (GNOME_DIALOG (dialog->dialog), 0, FALSE);
	
	gnome_dialog_button_connect( GNOME_DIALOG (dialog->dialog), 0,
				     GTK_SIGNAL_FUNC (addressbook_source_dialog_ok_clicked),
				     dialog);

	return dialog;
}

static AddressbookSourceDialog *
addressbook_config_source_with_gui (GladeXML *gui, AddressbookSource *source, GtkWidget *parent)
{
	AddressbookSourceDialog* dialog;

	dialog = addressbook_source_dialog (gui, source, parent);

	gnome_dialog_close_hides (GNOME_DIALOG(dialog->dialog), TRUE);

	dialog->id = gnome_dialog_run_and_close (GNOME_DIALOG (dialog->dialog));

	return dialog;
}

void
addressbook_create_new_source (char *new_source, GtkWidget *parent)
{
	AddressbookSourceDialog *dialog;
	GladeXML *gui;

	gui = glade_xml_new (EVOLUTION_GLADEDIR "/addressbook-config.glade", NULL);

	dialog = addressbook_source_dialog (gui, NULL, parent);

	e_utf8_gtk_entry_set_text (GTK_ENTRY (dialog->name), new_source);

	gnome_dialog_close_hides (GNOME_DIALOG(dialog->dialog), TRUE);

	dialog->id = gnome_dialog_run_and_close (GNOME_DIALOG (dialog->dialog));
	
	gtk_object_unref (GTK_OBJECT (dialog->gui));

	if (dialog->id == 0) {
		/* Ok was clicked */
		addressbook_storage_add_source (addressbook_source_copy(dialog->source));
		addressbook_storage_write_sources();
	}
}



typedef struct {
	GladeXML *gui;
	GNOME_Evolution_Shell shell;
	GtkWidget *dialog;
	GtkWidget *clistSources;
	GtkWidget *addSource;
	GtkWidget *editSource;
	GtkWidget *deleteSource;
	gint      source_row;
} AddressbookDialog;

static void
update_sensitivity (AddressbookDialog *dialog)
{
	gboolean sensitive = dialog->source_row != -1;

	gtk_widget_set_sensitive (dialog->editSource, sensitive);
	gtk_widget_set_sensitive (dialog->deleteSource, sensitive);
}

static void
add_source_clicked (GtkWidget *widget, AddressbookDialog *dialog)
{
	AddressbookSourceDialog *sdialog;

	sdialog = addressbook_config_source_with_gui (dialog->gui, NULL, dialog->dialog);
	if (sdialog->id == 0) {
		/* Ok was clicked */
		AddressbookSource *source = addressbook_source_copy(sdialog->source);
		gint row;
		gchar *text[2];

		text[0] = source->name;
		text[1] = source->host;

		row = e_utf8_gtk_clist_append (GTK_CLIST(dialog->clistSources), text);
		gtk_clist_set_row_data_full (GTK_CLIST(dialog->clistSources), row, source, (GtkDestroyNotify) addressbook_source_free);
		gnome_dialog_set_sensitive (GNOME_DIALOG(dialog->dialog), 0, TRUE);
		gnome_dialog_set_sensitive (GNOME_DIALOG(dialog->dialog), 1, TRUE);
		update_sensitivity (dialog);
	}
}

static void
edit_source_clicked (GtkWidget *widget, AddressbookDialog *dialog)
{
	AddressbookSource *source;
	AddressbookSourceDialog *sdialog;

	source = gtk_clist_get_row_data (GTK_CLIST (dialog->clistSources), dialog->source_row);

	sdialog = addressbook_config_source_with_gui (dialog->gui, source, dialog->dialog);
	if (sdialog->id == 0) {
		/* Ok was clicked */
		source = addressbook_source_copy(sdialog->source);

		e_utf8_gtk_clist_set_text (GTK_CLIST (dialog->clistSources), dialog->source_row, 0, source->name);
		e_utf8_gtk_clist_set_text (GTK_CLIST (dialog->clistSources), dialog->source_row, 1, source->host);
		gtk_clist_set_row_data (GTK_CLIST (dialog->clistSources), dialog->source_row, source);
		gnome_dialog_set_sensitive (GNOME_DIALOG(dialog->dialog), 0, TRUE);
		gnome_dialog_set_sensitive (GNOME_DIALOG(dialog->dialog), 1, TRUE);
		update_sensitivity (dialog);
	}
}

static void
delete_source_clicked (GtkWidget *widget, AddressbookDialog *dialog)
{
	gtk_clist_remove (GTK_CLIST (dialog->clistSources), dialog->source_row);
	dialog->source_row = -1;
	gnome_dialog_set_sensitive (GNOME_DIALOG(dialog->dialog), 0, TRUE);
	gnome_dialog_set_sensitive (GNOME_DIALOG(dialog->dialog), 1, TRUE);
	update_sensitivity (dialog);
}

static void
sources_select_row (GtkWidget *widget, gint row, gint column,
		    GdkEventButton *event, AddressbookDialog *dialog)
{
	dialog->source_row = row;

	update_sensitivity (dialog);
}

static void
addressbook_dialog_close (GtkWidget *w, AddressbookDialog *dialog)
{
	gtk_widget_destroy (dialog->dialog);
	gtk_object_unref (GTK_OBJECT (dialog->gui));
	g_free (dialog);
}

static void
addressbook_dialog_apply (GtkWidget *w, AddressbookDialog *dialog)
{
	int i;

	addressbook_storage_clear_sources();

	for (i = 0; i < GTK_CLIST(dialog->clistSources)->rows; i ++) {
		AddressbookSource *source = (AddressbookSource*)gtk_clist_get_row_data (GTK_CLIST (dialog->clistSources), i);
		addressbook_storage_add_source (addressbook_source_copy (source));
	}

	addressbook_storage_write_sources();

	gnome_dialog_set_sensitive (GNOME_DIALOG(dialog->dialog), 0, FALSE);
	gnome_dialog_set_sensitive (GNOME_DIALOG(dialog->dialog), 1, FALSE);
}

static void
addressbook_dialog_ok (GtkWidget *w, AddressbookDialog *dialog)
{
	addressbook_dialog_apply(w, dialog);
	addressbook_dialog_close(w, dialog);
}

void
addressbook_config (GNOME_Evolution_Shell shell)
{
	AddressbookDialog *dialog;
	GladeXML *gui;
	GtkWidget *clist;
	GList *l;

	dialog = g_new0 (AddressbookDialog, 1);

	dialog->source_row = -1;

	gui = glade_xml_new (EVOLUTION_GLADEDIR "/addressbook-config.glade", NULL);
	dialog->gui = gui;
	dialog->shell = shell;

	dialog->dialog = glade_xml_get_widget (gui, "addressbook_sources");

	clist = glade_xml_get_widget (gui, "clistSources");
	dialog->clistSources = clist;
	
	gtk_clist_column_titles_passive (GTK_CLIST (clist));
	gtk_clist_set_column_width (GTK_CLIST (clist), 0, 80);

	dialog->addSource = glade_xml_get_widget (gui, "addSource");
	gtk_signal_connect (GTK_OBJECT(dialog->addSource), "clicked",
			    GTK_SIGNAL_FUNC (add_source_clicked),
			    dialog);

	dialog->editSource = glade_xml_get_widget (gui, "editSource");
	gtk_signal_connect (GTK_OBJECT(dialog->editSource), "clicked",
			    GTK_SIGNAL_FUNC (edit_source_clicked),
			    dialog);

	dialog->deleteSource = glade_xml_get_widget (gui, "deleteSource");
	gtk_signal_connect (GTK_OBJECT(dialog->deleteSource), "clicked",
			    GTK_SIGNAL_FUNC (delete_source_clicked),
			    dialog);

	gnome_dialog_set_sensitive (GNOME_DIALOG(dialog->dialog), 0, FALSE);
	gnome_dialog_set_sensitive (GNOME_DIALOG(dialog->dialog), 1, FALSE);
	update_sensitivity (dialog);

	l = addressbook_storage_get_sources ();
	for (; l != NULL; l = l->next) {
		AddressbookSource *source;
		gint row;
		gchar *text[2];

		source = addressbook_source_copy ((AddressbookSource*)l->data);

		text[0] = source->name;
		text[1] = source->host;

		row = e_utf8_gtk_clist_append (GTK_CLIST(clist), text);
		gtk_clist_set_row_data_full (GTK_CLIST(clist), row, source, (GtkDestroyNotify) addressbook_source_free);
	}

	gtk_signal_connect (GTK_OBJECT (clist), "select_row",
			    GTK_SIGNAL_FUNC (sources_select_row),
			    dialog);

	gnome_dialog_button_connect (GNOME_DIALOG(dialog->dialog),
				     0 /* OK */, addressbook_dialog_ok, dialog);

	gnome_dialog_button_connect (GNOME_DIALOG(dialog->dialog),
				     1 /* APPLY */, addressbook_dialog_apply, dialog);

	gnome_dialog_button_connect (GNOME_DIALOG(dialog->dialog),
				     2 /* CLOSE */, addressbook_dialog_close, dialog);

	gnome_dialog_run (GNOME_DIALOG(dialog->dialog));
}
