/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * e-contact-editor.c
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

#include "e-contact-editor.h"

#include <string.h>
#include <time.h>
#include <gtk/gtkcheckbutton.h>
#include <gtk/gtkcheckmenuitem.h>
#include <gtk/gtkcombo.h>
#include <gtk/gtktextview.h>
#include <gtk/gtkmessagedialog.h>
#include <gtk/gtkstock.h>
#include <gtk/gtkentry.h>
#include <gtk/gtklabel.h>
#include <libgnomeui/gnome-popup-menu.h>
#include <libgnomeui/gnome-window-icon.h>
#include <libgnome/gnome-i18n.h>

#include <bonobo/bonobo-ui-container.h>
#include <bonobo/bonobo-ui-util.h>
#include <bonobo/bonobo-window.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gal/widgets/e-categories.h>
#include <gal/widgets/e-gui-utils.h>
#include <gal/e-text/e-entry.h>

#include <e-util/e-categories-master-list-wombat.h>

#include "addressbook/printing/e-contact-print.h"
#include "addressbook/printing/e-contact-print-envelope.h"
#include "addressbook/gui/widgets/e-addressbook-util.h"
#include "e-util/e-gui-utils.h"
#include "widgets/misc/e-dateedit.h"
#include "widgets/misc/e-url-entry.h"
#include "shell/evolution-shell-component-utils.h"

#include "e-card-merging.h"

#include "e-contact-editor-address.h"
#include "e-contact-editor-fullname.h"
#include "e-contact-editor-marshal.h"
#include "e-contact-save-as.h"

/* Signal IDs */
enum {
	CARD_ADDED,
	CARD_MODIFIED,
	CARD_DELETED,
	EDITOR_CLOSED,
	LAST_SIGNAL
};

static void e_contact_editor_init		(EContactEditor		 *card);
static void e_contact_editor_class_init	(EContactEditorClass	 *klass);
static void e_contact_editor_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void e_contact_editor_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static void e_contact_editor_dispose (GObject *object);

static void _email_arrow_pressed (GtkWidget *widget, GdkEventButton *button, EContactEditor *editor);
static void _phone_arrow_pressed (GtkWidget *widget, GdkEventButton *button, EContactEditor *editor);
static void _address_arrow_pressed (GtkWidget *widget, GdkEventButton *button, EContactEditor *editor);
static void find_address_mailing (EContactEditor *editor);
static void enable_writable_fields(EContactEditor *editor);
static void set_editable(EContactEditor *editor);
static void fill_in_info(EContactEditor *editor);
static void extract_info(EContactEditor *editor);
static void set_fields(EContactEditor *editor);
static void set_address_field(EContactEditor *editor, int result);
static void add_field_callback(GtkWidget *widget, EContactEditor *editor);
static void command_state_changed (EContactEditor *ce);
static void widget_changed (GtkWidget *widget, EContactEditor *editor);
static void close_dialog (EContactEditor *ce);
static void enable_widget (GtkWidget *widget, gboolean enabled);

static GtkObjectClass *parent_class = NULL;

static guint contact_editor_signals[LAST_SIGNAL];

/* The arguments we take */
enum {
	PROP_0,
	PROP_BOOK,
	PROP_CARD,
	PROP_IS_NEW_CARD,
	PROP_EDITABLE,
	PROP_CHANGED,
	PROP_WRITABLE_FIELDS
};

enum {
	DYNAMIC_LIST_EMAIL,
	DYNAMIC_LIST_PHONE,
	DYNAMIC_LIST_ADDRESS
};

static GSList *all_contact_editors = NULL;

GType
e_contact_editor_get_type (void)
{
	static GType contact_editor_type = 0;

	if (!contact_editor_type) {
		static const GTypeInfo contact_editor_info =  {
			sizeof (EContactEditorClass),
			NULL,           /* base_init */
			NULL,           /* base_finalize */
			(GClassInitFunc) e_contact_editor_class_init,
			NULL,           /* class_finalize */
			NULL,           /* class_data */
			sizeof (EContactEditor),
			0,             /* n_preallocs */
			(GInstanceInitFunc) e_contact_editor_init,
		};

		contact_editor_type = g_type_register_static (GTK_TYPE_OBJECT, "EContactEditor", &contact_editor_info, 0);
	}

	return contact_editor_type;
}

static void
e_contact_editor_class_init (EContactEditorClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  parent_class = g_type_class_ref (GTK_TYPE_OBJECT);

  object_class->set_property = e_contact_editor_set_property;
  object_class->get_property = e_contact_editor_get_property;
  object_class->dispose = e_contact_editor_dispose;

  g_object_class_install_property (object_class, PROP_BOOK, 
				   g_param_spec_object ("book",
							_("Book"),
							/*_( */"XXX blurb" /*)*/,
							E_TYPE_BOOK,
							G_PARAM_READWRITE));

  g_object_class_install_property (object_class, PROP_CARD, 
				   g_param_spec_object ("card",
							_("Card"),
							/*_( */"XXX blurb" /*)*/,
							E_TYPE_CARD,
							G_PARAM_READWRITE));

  g_object_class_install_property (object_class, PROP_IS_NEW_CARD, 
				   g_param_spec_boolean ("is_new_card",
							 _("Is New Card"),
							 /*_( */"XXX blurb" /*)*/,
							 FALSE,
							 G_PARAM_READWRITE));

  g_object_class_install_property (object_class, PROP_WRITABLE_FIELDS, 
				   g_param_spec_object ("writable_fields",
							_("Writable Fields"),
							/*_( */"XXX blurb" /*)*/,
							E_TYPE_LIST,
							G_PARAM_READWRITE));

  g_object_class_install_property (object_class, PROP_EDITABLE, 
				   g_param_spec_boolean ("editable",
							 _("Editable"),
							 /*_( */"XXX blurb" /*)*/,
							 FALSE,
							 G_PARAM_READWRITE));

  g_object_class_install_property (object_class, PROP_CHANGED, 
				   g_param_spec_boolean ("changed",
							 _("Changed"),
							 /*_( */"XXX blurb" /*)*/,
							 FALSE,
							 G_PARAM_READWRITE));

  contact_editor_signals[CARD_ADDED] =
	  g_signal_new ("card_added",
			G_OBJECT_CLASS_TYPE (object_class),
			G_SIGNAL_RUN_FIRST,
			G_STRUCT_OFFSET (EContactEditorClass, card_added),
			NULL, NULL,
			ece_marshal_NONE__INT_OBJECT,
			G_TYPE_NONE, 2,
			G_TYPE_INT, G_TYPE_OBJECT);

  contact_editor_signals[CARD_MODIFIED] =
	  g_signal_new ("card_modified",
			G_OBJECT_CLASS_TYPE (object_class),
			G_SIGNAL_RUN_FIRST,
			G_STRUCT_OFFSET (EContactEditorClass, card_modified),
			NULL, NULL,
			ece_marshal_NONE__INT_OBJECT,
			G_TYPE_NONE, 2,
			G_TYPE_INT, G_TYPE_OBJECT);

  contact_editor_signals[CARD_DELETED] =
	  g_signal_new ("card_deleted",
			G_OBJECT_CLASS_TYPE (object_class),
			G_SIGNAL_RUN_FIRST,
			G_STRUCT_OFFSET (EContactEditorClass, card_deleted),
			NULL, NULL,
			ece_marshal_NONE__INT_OBJECT,
			G_TYPE_NONE, 2,
			G_TYPE_INT, G_TYPE_OBJECT);

  contact_editor_signals[EDITOR_CLOSED] =
	  g_signal_new ("editor_closed",
			G_OBJECT_CLASS_TYPE (object_class),
			G_SIGNAL_RUN_FIRST,
			G_STRUCT_OFFSET (EContactEditorClass, editor_closed),
			NULL, NULL,
			ece_marshal_NONE__NONE,
			G_TYPE_NONE, 0);
}

static void
connect_arrow_button_signal (EContactEditor *editor, gchar *button_xml, GCallback func)
{
	GladeXML *gui = editor->gui;
	GtkWidget *button = glade_xml_get_widget(gui, button_xml);
	if (button && GTK_IS_BUTTON(button)) {
		g_signal_connect(button, "button_press_event", func, editor);
	}
}

static void
connect_arrow_button_signals (EContactEditor *editor)
{
	connect_arrow_button_signal(editor, "button-phone1", G_CALLBACK (_phone_arrow_pressed));
	connect_arrow_button_signal(editor, "button-phone2", G_CALLBACK (_phone_arrow_pressed));
	connect_arrow_button_signal(editor, "button-phone3", G_CALLBACK (_phone_arrow_pressed));
	connect_arrow_button_signal(editor, "button-phone4", G_CALLBACK (_phone_arrow_pressed));
	connect_arrow_button_signal(editor, "button-address", G_CALLBACK (_address_arrow_pressed));
	connect_arrow_button_signal(editor, "button-email1", G_CALLBACK (_email_arrow_pressed));
}

static void
wants_html_changed (GtkWidget *widget, EContactEditor *editor)
{
	gboolean wants_html;
	g_object_get (widget,
		       "active", &wants_html,
		       NULL);
	g_object_set (editor->card,
		       "wants_html", wants_html,
		       NULL);

	widget_changed (widget, editor);
}

static void
phone_entry_changed (GtkWidget *widget, EContactEditor *editor)
{
	int which;
	GtkEntry *entry = GTK_ENTRY(widget);
	ECardPhone *phone;

	if ( widget == glade_xml_get_widget(editor->gui, "entry-phone1") ) {
		which = 1;
	} else if ( widget == glade_xml_get_widget(editor->gui, "entry-phone2") ) {
		which = 2;
	} else if ( widget == glade_xml_get_widget(editor->gui, "entry-phone3") ) {
		which = 3;
	} else if ( widget == glade_xml_get_widget(editor->gui, "entry-phone4") ) {
		which = 4;
	} else
		return;
	phone = e_card_phone_new();
	phone->number = g_strdup (gtk_entry_get_text(entry));
	e_card_simple_set_phone(editor->simple, editor->phone_choice[which - 1], phone);
	e_card_phone_unref(phone);
	set_fields(editor);

	widget_changed (widget, editor);
}

static void
email_entry_changed (GtkWidget *widget, EContactEditor *editor)
{
	const gchar *string;
	GtkEntry *entry = GTK_ENTRY(widget);

	string = gtk_entry_get_text(entry);

	e_card_simple_set_email(editor->simple, editor->email_choice, string);

	widget_changed (widget, editor);
}

static void
address_text_changed (GtkTextBuffer *buffer, EContactEditor *editor)
{
	ECardAddrLabel *address;
	GtkTextIter start_iter, end_iter;

	if (editor->address_choice == -1)
		return;

	address = e_card_address_label_new();

	if (editor->address_mailing == editor->address_choice || editor->address_mailing == -1) {
		GtkWidget *check;
		
		address->flags |= E_CARD_ADDR_DEFAULT;
		
		check = glade_xml_get_widget(editor->gui, "checkbutton-mailingaddress");
		if (check && GTK_IS_CHECK_BUTTON (check)) {
			g_signal_handlers_block_matched (check,
							 G_SIGNAL_MATCH_DATA,
							 0, 0,
							 NULL, NULL, editor);
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check), TRUE);
			g_signal_handlers_unblock_matched (check,
							   G_SIGNAL_MATCH_DATA,
							   0, 0,
							   NULL, NULL, editor);
		}
	}
	
	gtk_text_buffer_get_start_iter (GTK_TEXT_BUFFER (buffer), &start_iter);
	gtk_text_buffer_get_end_iter (GTK_TEXT_BUFFER (buffer), &end_iter);

	address->data = gtk_text_buffer_get_text (GTK_TEXT_BUFFER (buffer), &start_iter, &end_iter, FALSE);

	e_card_simple_set_address(editor->simple, editor->address_choice, address);
	e_card_address_label_unref(address);

	widget_changed (NULL, editor);
}


static void
address_mailing_changed (GtkWidget *widget, EContactEditor *editor)
{
	const ECardDeliveryAddress *curr;
	ECardDeliveryAddress *address;
	gboolean mailing_address;

	if (editor->address_choice == -1)
		return;

	mailing_address = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
	
	/* Mark the current address as the mailing address */
	curr = e_card_simple_get_delivery_address (editor->simple,
						   editor->address_choice);

	address = e_card_delivery_address_copy (curr);

	if (!address)
		address = e_card_delivery_address_new ();

	if (mailing_address)
		address->flags |= E_CARD_ADDR_DEFAULT;
	else
		address->flags &= ~E_CARD_ADDR_DEFAULT;

	e_card_simple_set_delivery_address(editor->simple, editor->address_choice, address);
	e_card_delivery_address_unref (address);

	/* Unset the previous mailing address flag */
	if (mailing_address && editor->address_mailing != -1) {
		const ECardDeliveryAddress *curr;

		curr = e_card_simple_get_delivery_address(editor->simple, 
							  editor->address_mailing);
		address = e_card_delivery_address_copy (curr);
		address->flags &= ~E_CARD_ADDR_DEFAULT;
		e_card_simple_set_delivery_address(editor->simple, 
						   editor->address_mailing,
						   address);
		e_card_delivery_address_unref (address);
	}

	/* Remember the new mailing address */
	if (mailing_address)
		editor->address_mailing = editor->address_choice;
	else
		editor->address_mailing = -1;

	widget_changed (widget, editor);
}


/* This function tells you whether name_to_style will make sense.  */
static gboolean
style_makes_sense(const ECardName *name, char *company, int style)
{
	switch (style) {
	case 0: /* Fall Through */
	case 1:
		return TRUE;
	case 2:
		if (company && *company)
			return TRUE;
		else
			return FALSE;
	case 3: /* Fall Through */
	case 4:
		if (company && *company && name && ((name->given && *name->given) || (name->family && *name->family)))
			return TRUE;
		else
			return FALSE;
	default:
		return FALSE;
	}
}

static char *
name_to_style(const ECardName *name, char *company, int style)
{
	char *string;
	char *strings[4], **stringptr;
	char *substring;
	switch (style) {
	case 0:
		stringptr = strings;
		if (name) {
			if (name->family && *name->family)
				*(stringptr++) = name->family;
			if (name->given && *name->given)
				*(stringptr++) = name->given;
		}
		*stringptr = NULL;
		string = g_strjoinv(", ", strings);
		break;
	case 1:
		stringptr = strings;
		if (name) {
			if (name->given && *name->given)
				*(stringptr++) = name->given;
			if (name->family && *name->family)
				*(stringptr++) = name->family;
		}
		*stringptr = NULL;
		string = g_strjoinv(" ", strings);
		break;
	case 2:
		string = g_strdup(company);
		break;
	case 3: /* Fall Through */
	case 4:
		stringptr = strings;
		if (name) {
			if (name->family && *name->family)
				*(stringptr++) = name->family;
			if (name->given && *name->given)
				*(stringptr++) = name->given;
		}
		*stringptr = NULL;
		substring = g_strjoinv(", ", strings);
		if (!(company && *company))
			company = "";
		if (style == 3)
			string = g_strdup_printf("%s (%s)", substring, company);
		else
			string = g_strdup_printf("%s (%s)", company, substring);
		g_free(substring);
		break;
	default:
		string = g_strdup("");
	}
	return string;
}

static int
file_as_get_style (EContactEditor *editor)
{
	GtkEntry *file_as = GTK_ENTRY(glade_xml_get_widget(editor->gui, "entry-file-as"));
	char *filestring;
	char *trystring;
	ECardName *name = editor->name;
	int i;
	int style;

	if (!(file_as && GTK_IS_ENTRY(file_as)))
		return -1;

	filestring = g_strdup (gtk_entry_get_text(file_as));

	style = -1;
	for (i = 0; i < 5; i++) {
		trystring = name_to_style(name, editor->company, i);
		if (!strcmp(trystring, filestring)) {
			g_free(trystring);
			g_free(filestring);
			return i;
		}
		g_free(trystring);
	}
	g_free (filestring);
	return -1;
}

static void
file_as_set_style(EContactEditor *editor, int style)
{
	char *string;
	int i;
	GList *strings = NULL;
	GtkEntry *file_as = GTK_ENTRY(glade_xml_get_widget(editor->gui, "entry-file-as"));
	GtkWidget *widget;
		

	if (!(file_as && GTK_IS_ENTRY(file_as)))
		return;

	if (style == -1) {
		string = g_strdup (gtk_entry_get_text(file_as));
		strings = g_list_append(strings, string);
	}

	widget = glade_xml_get_widget(editor->gui, "combo-file-as");

	for (i = 0; i < 5; i++) {
		if (style_makes_sense(editor->name, editor->company, i)) {
			char *u;
			u = name_to_style(editor->name, editor->company, i);
			if (u) strings = g_list_append(strings, u);
		}
	}

	if (widget && GTK_IS_COMBO(widget)) {
		GtkCombo *combo = GTK_COMBO(widget);
		gtk_combo_set_popdown_strings(combo, strings);
		g_list_foreach(strings, (GFunc) g_free, NULL);
		g_list_free(strings);
	}

	if (style != -1) {
		string = name_to_style(editor->name, editor->company, style);
		gtk_entry_set_text(file_as, string);
		g_free(string);
	}
}

static void
name_entry_changed (GtkWidget *widget, EContactEditor *editor)
{
	int style = 0;
	const char *string;

	style = file_as_get_style(editor);
	
	e_card_name_unref(editor->name);

	string = gtk_entry_get_text (GTK_ENTRY(widget));
	editor->name = e_card_name_from_string(string);
	
	file_as_set_style(editor, style);

	widget_changed (widget, editor);
}

static void
company_entry_changed (GtkWidget *widget, EContactEditor *editor)
{
	int style = 0;

	style = file_as_get_style(editor);
	
	g_free(editor->company);
	
	editor->company = g_strdup (gtk_entry_get_text(GTK_ENTRY(widget)));
	
	file_as_set_style(editor, style);

	widget_changed (widget, editor);
}

static void
field_changed (GtkWidget *widget, EContactEditor *editor)
{
	if (!editor->changed) {
		editor->changed = TRUE;
		command_state_changed (editor);
	}
}

static void
set_entry_changed_signal_phone(EContactEditor *editor, char *id)
{
	GtkWidget *widget = glade_xml_get_widget(editor->gui, id);
	if (widget && GTK_IS_ENTRY(widget))
		g_signal_connect(widget, "changed",
				 G_CALLBACK (phone_entry_changed), editor);
}

static void
set_entry_changed_signal_email(EContactEditor *editor, char *id)
{
	GtkWidget *widget = glade_xml_get_widget(editor->gui, id);
	if (widget && GTK_IS_ENTRY(widget))
		g_signal_connect(widget, "changed",
				 G_CALLBACK (email_entry_changed), editor);
}

static void
widget_changed (GtkWidget *widget, EContactEditor *editor)
{
	if (!editor->editable) {
		g_warning ("non-editable contact editor has an editable field in it.");
		return;
	}

	if (!editor->changed) {
		editor->changed = TRUE;
		command_state_changed (editor);
	}
}

static void
set_entry_changed_signal_field(EContactEditor *editor, char *id)
{
	GtkWidget *widget = glade_xml_get_widget(editor->gui, id);
	if (widget && GTK_IS_ENTRY(widget))
		g_signal_connect(widget, "changed",
				 G_CALLBACK (field_changed), editor);
}

static void
set_urlentry_changed_signal_field (EContactEditor *editor, char *id)
{
	GtkWidget *widget = glade_xml_get_widget(editor->gui, id);
	if (widget && E_IS_URL_ENTRY(widget)) {
		GtkWidget *entry = e_url_entry_get_entry (E_URL_ENTRY (widget));
		g_signal_connect (entry, "changed",
				  G_CALLBACK (field_changed), editor);
	}
}

static void
set_entry_changed_signals(EContactEditor *editor)
{
	GtkWidget *widget;
	set_entry_changed_signal_phone(editor, "entry-phone1");
	set_entry_changed_signal_phone(editor, "entry-phone2");
	set_entry_changed_signal_phone(editor, "entry-phone3");
	set_entry_changed_signal_phone(editor, "entry-phone4");

	set_entry_changed_signal_email(editor, "entry-email1");

	widget = glade_xml_get_widget(editor->gui, "text-address");
	if (widget && GTK_IS_TEXT_VIEW(widget)) {
		GtkTextBuffer *buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (widget));
		g_signal_connect(buffer, "changed",
				 G_CALLBACK (address_text_changed), editor);
	}

	widget = glade_xml_get_widget(editor->gui, "entry-fullname");
	if (widget && GTK_IS_ENTRY(widget)) {
		g_signal_connect (widget, "changed",
				  G_CALLBACK (name_entry_changed), editor);
	}

	widget = glade_xml_get_widget(editor->gui, "entry-company");
	if (widget && GTK_IS_ENTRY(widget)) {
		g_signal_connect (widget, "changed",
				  G_CALLBACK (company_entry_changed), editor);
	}

	set_urlentry_changed_signal_field (editor, "entry-web");
	set_urlentry_changed_signal_field (editor, "entry-caluri");
	set_urlentry_changed_signal_field (editor, "entry-fburl");

	set_entry_changed_signal_field(editor, "entry-categories");
	set_entry_changed_signal_field(editor, "entry-jobtitle");
	set_entry_changed_signal_field(editor, "entry-file-as");
	set_entry_changed_signal_field(editor, "entry-manager");
	set_entry_changed_signal_field(editor, "entry-assistant");
	set_entry_changed_signal_field(editor, "entry-office");
	set_entry_changed_signal_field(editor, "entry-department");
	set_entry_changed_signal_field(editor, "entry-profession");
	set_entry_changed_signal_field(editor, "entry-nickname");
	set_entry_changed_signal_field(editor, "entry-spouse");

	widget = glade_xml_get_widget(editor->gui, "text-comments");
	if (widget && GTK_IS_TEXT_VIEW(widget)) {
		GtkTextBuffer *buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (widget));
		g_signal_connect (buffer, "changed",
				  G_CALLBACK (widget_changed), editor);
	}
	widget = glade_xml_get_widget(editor->gui, "dateedit-birthday");
	if (widget && E_IS_DATE_EDIT(widget)) {
		g_signal_connect (widget, "changed",
				  G_CALLBACK (widget_changed), editor);
	}
	widget = glade_xml_get_widget(editor->gui, "dateedit-anniversary");
	if (widget && E_IS_DATE_EDIT(widget)) {
		g_signal_connect (widget, "changed",
				  G_CALLBACK (widget_changed), editor);
	}
}

static void
full_name_clicked(GtkWidget *button, EContactEditor *editor)
{
	GtkDialog *dialog = GTK_DIALOG(e_contact_editor_fullname_new(editor->name));
	int result;

	g_object_set (dialog,
			"editable", editor->fullname_editable,
			NULL);
	gtk_widget_show(GTK_WIDGET(dialog));
	result = gtk_dialog_run (dialog);
	gtk_widget_hide (GTK_WIDGET (dialog));

	if (editor->fullname_editable && result == GTK_RESPONSE_OK) {
		ECardName *name;
		GtkWidget *fname_widget;
		int style = 0;

		g_object_get (dialog,
			       "name", &name,
			       NULL);

		style = file_as_get_style(editor);

		fname_widget = glade_xml_get_widget(editor->gui, "entry-fullname");
		if (fname_widget && GTK_IS_ENTRY(fname_widget)) {
			char *full_name = e_card_name_to_string(name);
			gtk_entry_set_text(GTK_ENTRY(fname_widget), full_name);
			g_free(full_name);
		}

		e_card_name_unref(editor->name);
		editor->name = e_card_name_ref(name);

		file_as_set_style(editor, style);
	}
	gtk_widget_destroy (GTK_WIDGET (dialog));
}

static void
full_addr_clicked(GtkWidget *button, EContactEditor *editor)
{
	GtkDialog *dialog;
	int result;
	const ECardDeliveryAddress *address;

	address = e_card_simple_get_delivery_address(editor->simple, editor->address_choice);

	dialog = GTK_DIALOG(e_contact_editor_address_new(address));
	g_object_set (dialog,
		      "editable", editor->address_editable[editor->address_choice],
		      NULL);
	gtk_widget_show(GTK_WIDGET(dialog));

	result = gtk_dialog_run (dialog);

	gtk_widget_hide (GTK_WIDGET (dialog));

	if (editor->address_editable[editor->address_choice] && result == GTK_RESPONSE_OK) {
		ECardDeliveryAddress *new_address;
		GtkWidget *address_widget;
		int saved_choice = editor->address_choice;

		editor->address_choice = -1;

		g_object_get (dialog,
			       "address", &new_address,
			       NULL);

		address_widget = glade_xml_get_widget(editor->gui, "text-address");
		if (address_widget && GTK_IS_TEXT_VIEW(address_widget)) {
			GtkTextBuffer *buffer;
			char *string = e_card_delivery_address_to_string(new_address);

			buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (address_widget));

			gtk_text_buffer_set_text (buffer, string, strlen (string));

			g_free(string);
		}

		editor->address_choice = saved_choice;

		e_card_simple_set_delivery_address(editor->simple, editor->address_choice, new_address);

		e_card_delivery_address_unref(new_address);

		widget_changed (NULL, editor);
	}
	gtk_widget_destroy (GTK_WIDGET (dialog));
}

static void
categories_clicked(GtkWidget *button, EContactEditor *editor)
{
	char *categories = NULL;
	GtkDialog *dialog;
	int result;
	GtkWidget *entry = glade_xml_get_widget(editor->gui, "entry-categories");
	ECategoriesMasterList *ecml;
	if (entry && GTK_IS_ENTRY(entry))
		categories = g_strdup (gtk_entry_get_text(GTK_ENTRY(entry)));
	else if (editor->card)
		g_object_get (editor->card,
			       "categories", &categories,
			       NULL);
	dialog = GTK_DIALOG(e_categories_new(categories));

	if (dialog == NULL) {
		GtkWidget *uh_oh = gtk_message_dialog_new (NULL,
							   0, GTK_MESSAGE_ERROR,
							   GTK_RESPONSE_OK,
							   _("Category editor not available."));
		g_free (categories);
		gtk_widget_show (uh_oh);
		return;
	}

	ecml = e_categories_master_list_wombat_new ();
	g_object_set (dialog,
		       "header", _("This contact belongs to these categories:"),
		       "ecml", ecml,
		       NULL);
	g_object_unref (ecml);
	gtk_widget_show(GTK_WIDGET(dialog));
	result = gtk_dialog_run (dialog);
	g_free (categories);
	if (result == GTK_RESPONSE_OK) {
		g_object_get (dialog,
			       "categories", &categories,
			       NULL);
		if (entry && GTK_IS_ENTRY(entry))
			gtk_entry_set_text(GTK_ENTRY(entry), categories);
		else
			g_object_set (editor->card,
				       "categories", categories,
				       NULL);
		g_free(categories);
	}
	gtk_widget_destroy(GTK_WIDGET(dialog));
}

typedef struct {
	EContactEditor *ce;
	gboolean should_close;
} EditorCloseStruct;

static void
card_added_cb (EBook *book, EBookStatus status, const char *id, EditorCloseStruct *ecs)
{
	EContactEditor *ce = ecs->ce;
	gboolean should_close = ecs->should_close;

	gtk_widget_set_sensitive (ce->app, TRUE);
	ce->in_async_call = FALSE;

	e_card_set_id (ce->card, id);

	g_signal_emit (ce, contact_editor_signals[CARD_ADDED], 0,
		       status, ce->card);

	if (status == E_BOOK_STATUS_SUCCESS) {
		ce->is_new_card = FALSE;

		if (should_close) {
			close_dialog (ce);
		}
		else {
			ce->changed = FALSE;
			command_state_changed (ce);
		}
	}

	g_object_unref (ce);
	g_free (ecs);
}

static void
card_modified_cb (EBook *book, EBookStatus status, EditorCloseStruct *ecs)
{
	EContactEditor *ce = ecs->ce;
	gboolean should_close = ecs->should_close;

	gtk_widget_set_sensitive (ce->app, TRUE);
	ce->in_async_call = FALSE;

	g_signal_emit (ce, contact_editor_signals[CARD_MODIFIED], 0,
		       status, ce->card);

	if (status == E_BOOK_STATUS_SUCCESS) {
		if (should_close) {
			close_dialog (ce);
		}
		else {
			ce->changed = FALSE;
			command_state_changed (ce);
		}
	}

	g_object_unref (ce);
	g_free (ecs);
}

/* Emits the signal to request saving a card */
static void
save_card (EContactEditor *ce, gboolean should_close)
{
	extract_info (ce);
	e_card_simple_sync_card (ce->simple);

	if (ce->book) {
		EditorCloseStruct *ecs = g_new(EditorCloseStruct, 1);
		
		ecs->ce = ce;
		g_object_ref (ecs->ce);

		ecs->should_close = should_close;

		gtk_widget_set_sensitive (ce->app, FALSE);
		ce->in_async_call = TRUE;

		if (ce->is_new_card)
			e_card_merging_book_add_card (ce->book, ce->card, (EBookIdCallback)card_added_cb, ecs);
		else
			e_card_merging_book_commit_card (ce->book, ce->card, (EBookCallback)card_modified_cb, ecs);
	}
}

/* Closes the dialog box and emits the appropriate signals */
static void
close_dialog (EContactEditor *ce)
{
	if (ce->app != NULL) {
		gtk_widget_destroy (ce->app);
		ce->app = NULL;
		g_signal_emit (ce, contact_editor_signals[EDITOR_CLOSED], 0);
	}
}

static gboolean
prompt_to_save_changes (EContactEditor *editor)
{
	if (!editor->changed)
		return TRUE;

	switch (e_addressbook_prompt_save_dialog (GTK_WINDOW(editor->app))) {
	case GTK_RESPONSE_YES:
		save_card (editor, FALSE);
		return TRUE;
	case GTK_RESPONSE_NO:
		return TRUE;
	case GTK_RESPONSE_CANCEL:
	default:
		return FALSE;
	}
}

/* Menu callbacks */

/* File/Save callback */
static void
file_save_cb (GtkWidget *widget, gpointer data)
{
	EContactEditor *ce;

	ce = E_CONTACT_EDITOR (data);
	save_card (ce, FALSE);
}

/* File/Close callback */
static void
file_close_cb (GtkWidget *widget, gpointer data)
{
	EContactEditor *ce;

	ce = E_CONTACT_EDITOR (data);
	if (!prompt_to_save_changes (ce))
		return;

	close_dialog (ce);
}

static void
file_save_as_cb (GtkWidget *widget, gpointer data)
{
	EContactEditor *ce;
	ECard *card;

	ce = E_CONTACT_EDITOR (data);

	extract_info (ce);
	e_card_simple_sync_card (ce->simple);

	card = ce->card;
	e_contact_save_as(_("Save Contact as VCard"), card, GTK_WINDOW (ce->app));
}

static void
file_send_as_cb (GtkWidget *widget, gpointer data)
{
	EContactEditor *ce;
	ECard *card;

	ce = E_CONTACT_EDITOR (data);

	extract_info (ce);
	e_card_simple_sync_card (ce->simple);

	card = ce->card;
	e_addressbook_send_card(card, E_ADDRESSBOOK_DISPOSITION_AS_ATTACHMENT);
}

static void
file_send_to_cb (GtkWidget *widget, gpointer data)
{
	EContactEditor *ce;
	ECard *card;

	ce = E_CONTACT_EDITOR (data);

	extract_info (ce);
	e_card_simple_sync_card (ce->simple);

	card = ce->card;
	e_addressbook_send_card(card, E_ADDRESSBOOK_DISPOSITION_AS_TO);
}

gboolean
e_contact_editor_confirm_delete (GtkWindow *parent)
{
	GtkWidget *dialog;
	gint result;

	dialog = gtk_message_dialog_new (parent,
					 0,
					 GTK_MESSAGE_QUESTION,
					 GTK_BUTTONS_NONE,
#if notyet
					 /* XXX we really need to handle the plural case here.. */
					 (plural
					  ? _("Are you sure you want\n"
					      "to delete these contacts?"))
#endif
					  _("Are you sure you want\n"
					    "to delete this contact?"));

	gtk_dialog_add_buttons (GTK_DIALOG (dialog),
				GTK_STOCK_CANCEL, GTK_RESPONSE_REJECT,
				GTK_STOCK_DELETE, GTK_RESPONSE_ACCEPT,
				NULL);

	result = gtk_dialog_run(GTK_DIALOG (dialog));

	gtk_widget_destroy (dialog);

	return (result == GTK_RESPONSE_ACCEPT);
}

static void
card_deleted_cb (EBook *book, EBookStatus status, EContactEditor *ce)
{
	gtk_widget_set_sensitive (ce->app, TRUE);
	ce->in_async_call = FALSE;

	g_signal_emit (ce, contact_editor_signals[CARD_DELETED], 0,
		       status, ce->card);

	/* always close the dialog after we successfully delete a card */
	if (status == E_BOOK_STATUS_SUCCESS)
		close_dialog (ce);
}

static void
delete_cb (GtkWidget *widget, gpointer data)
{
	EContactEditor *ce = E_CONTACT_EDITOR (data);
	ECard *card = ce->card;
	ECardSimple *simple = ce->simple;

	g_object_ref(card);
	g_object_ref(simple);

	if (e_contact_editor_confirm_delete(GTK_WINDOW(ce->app))) {

		extract_info (ce);
		e_card_simple_sync_card (simple);
		
		if (!ce->is_new_card && ce->book) {
			gtk_widget_set_sensitive (ce->app, FALSE);
			ce->in_async_call = TRUE;

			e_book_remove_card (ce->book, card, (EBookCallback)card_deleted_cb, ce);
		}
	}

	g_object_unref(card);
	g_object_unref(simple);
}

/* Emits the signal to request printing a card */
static void
print_cb (BonoboUIComponent *uih, void *data, const char *path)
{
	EContactEditor *ce;

	ce = E_CONTACT_EDITOR (data);

	extract_info (ce);
	e_card_simple_sync_card (ce->simple);

	gtk_widget_show(e_contact_print_card_dialog_new(ce->card));
}

#if 0 /* Envelope printing is disabled for Evolution 1.0. */
/* Emits the signal to request printing a card */
static void
print_envelope_cb (BonoboUIComponent *uih, void *data, const char *path)
{
	EContactEditor *ce;

	ce = E_CONTACT_EDITOR (data);

	extract_info (ce);
	e_card_simple_sync_card (ce->simple);

	gtk_widget_show(e_contact_print_envelope_dialog_new(ce->card));
}
#endif

/* Toolbar/Save and Close callback */
static void
tb_save_and_close_cb (BonoboUIComponent *uih, void *data, const char *path)
{
	EContactEditor *ce;

	ce = E_CONTACT_EDITOR (data);
	save_card (ce, TRUE);
}

static
BonoboUIVerb verbs [] = {
	BONOBO_UI_UNSAFE_VERB ("ContactEditorSave", file_save_cb),
	BONOBO_UI_UNSAFE_VERB ("ContactEditorSaveAs", file_save_as_cb),
	BONOBO_UI_UNSAFE_VERB ("ContactEditorSaveClose", tb_save_and_close_cb),
	BONOBO_UI_UNSAFE_VERB ("ContactEditorSendAs", file_send_as_cb),
	BONOBO_UI_UNSAFE_VERB ("ContactEditorSendTo", file_send_to_cb),
	BONOBO_UI_UNSAFE_VERB ("ContactEditorDelete", delete_cb),
	BONOBO_UI_UNSAFE_VERB ("ContactEditorPrint", print_cb),
#if 0 /* Envelope printing is disabled for Evolution 1.0. */
	BONOBO_UI_UNSAFE_VERB ("ContactEditorPrintEnvelope", print_envelope_cb),
#endif
	/*	BONOBO_UI_UNSAFE_VERB ("ContactEditorPageSetup", file_page_setup_menu), */
	BONOBO_UI_UNSAFE_VERB ("ContactEditorClose", file_close_cb),
	
	BONOBO_UI_VERB_END
};

EPixmap pixmaps[] = {
	E_PIXMAP ("/commands/ContactEditorSave", "save-16.png"),
	E_PIXMAP ("/commands/ContactEditorSaveClose", "save-16.png"),
	E_PIXMAP ("/commands/ContactEditorSaveAs", "save-as-16.png"),
	E_PIXMAP ("/commands/ContactEditorDelete", "evolution-trash-mini.png"),
	E_PIXMAP ("/commands/ContactEditorPrint", "print.xpm"),
#if 0 /* Envelope printing is disabled for Evolution 1.0. */
	E_PIXMAP ("/commands/ContactEditorPrintEnvelope", "print.xpm"),
#endif
	E_PIXMAP ("/Toolbar/ContactEditorSaveClose", "buttons/save-24.png"),
	E_PIXMAP ("/Toolbar/ContactEditorDelete", "buttons/delete-message.png"),
	E_PIXMAP ("/Toolbar/ContactEditorPrint", "buttons/print.png"),

	E_PIXMAP_END
};

static void
create_ui (EContactEditor *ce)
{
	bonobo_ui_component_add_verb_list_with_data (ce->uic, verbs, ce);

	bonobo_ui_util_set_ui (ce->uic, PREFIX,
			       EVOLUTION_UIDIR "/evolution-contact-editor.xml",
			       "evolution-contact-editor", NULL);

	e_pixmaps_update (ce->uic, pixmaps);
}

/* Callback used when the dialog box is destroyed */
static gint
app_delete_event_cb (GtkWidget *widget, GdkEvent *event, gpointer data)
{
	EContactEditor *ce;

	ce = E_CONTACT_EDITOR (data);

	/* if we're saving, don't allow the dialog to close */
	if (ce->in_async_call)
		return TRUE;

	if (!prompt_to_save_changes (ce))
		return TRUE;

	close_dialog (ce);
	return TRUE;
}

static GList *
add_to_tab_order(GList *list, GladeXML *gui, char *name)
{
	GtkWidget *widget = glade_xml_get_widget(gui, name);
	return g_list_prepend(list, widget);
}

static void
setup_tab_order(GladeXML *gui)
{
	GtkWidget *container;
	GList *list = NULL;

	container = glade_xml_get_widget(gui, "table-contact-editor-general");

	if (container) {
		list = add_to_tab_order(list, gui, "entry-fullname");
		list = add_to_tab_order(list, gui, "entry-jobtitle");
		list = add_to_tab_order(list, gui, "entry-company");
		list = add_to_tab_order(list, gui, "combo-file-as");
		list = add_to_tab_order(list, gui, "entry-phone1");
		list = add_to_tab_order(list, gui, "entry-phone2");
		list = add_to_tab_order(list, gui, "entry-phone3");
		list = add_to_tab_order(list, gui, "entry-phone4");

		list = add_to_tab_order(list, gui, "entry-email1");
		list = add_to_tab_order(list, gui, "alignment-htmlmail");
		list = add_to_tab_order(list, gui, "entry-web");
		list = add_to_tab_order(list, gui, "button-fulladdr");
		list = add_to_tab_order(list, gui, "text-address");
		list = g_list_reverse(list);
		e_container_change_tab_order(GTK_CONTAINER(container), list);
		g_list_free(list);
	}
}

static void
e_contact_editor_init (EContactEditor *e_contact_editor)
{
	GladeXML *gui;
	GtkWidget *widget;
	GtkWidget *bonobo_win;
	GtkWidget *wants_html;
	BonoboUIContainer *container;
	char *icon_path;

	e_contact_editor->email_info = NULL;
	e_contact_editor->phone_info = NULL;
	e_contact_editor->address_info = NULL;
	e_contact_editor->email_popup = NULL;
	e_contact_editor->phone_popup = NULL;
	e_contact_editor->address_popup = NULL;
	e_contact_editor->email_list = NULL;
	e_contact_editor->phone_list = NULL;
	e_contact_editor->address_list = NULL;
	e_contact_editor->name = e_card_name_new();
	e_contact_editor->company = g_strdup("");
	
	e_contact_editor->email_choice = 0;
	e_contact_editor->phone_choice[0] = E_CARD_SIMPLE_PHONE_ID_BUSINESS;
	e_contact_editor->phone_choice[1] = E_CARD_SIMPLE_PHONE_ID_HOME;
	e_contact_editor->phone_choice[2] = E_CARD_SIMPLE_PHONE_ID_BUSINESS_FAX;
	e_contact_editor->phone_choice[3] = E_CARD_SIMPLE_PHONE_ID_MOBILE;
	e_contact_editor->address_choice = 0;
	e_contact_editor->address_mailing = -1;

	e_contact_editor->arbitrary_fields = NULL;
	
	e_contact_editor->simple = e_card_simple_new(NULL);

	e_contact_editor->card = NULL;
	e_contact_editor->changed = FALSE;
	e_contact_editor->in_async_call = FALSE;
	e_contact_editor->editable = TRUE;

	gui = glade_xml_new (EVOLUTION_GLADEDIR "/contact-editor.glade", NULL, NULL);
	e_contact_editor->gui = gui;

	setup_tab_order(gui);

	e_contact_editor->app = glade_xml_get_widget (gui, "contact editor");

	e_container_foreach_leaf (GTK_CONTAINER (e_contact_editor->app),
				  (GtkCallback) add_field_callback,
				  e_contact_editor);

	connect_arrow_button_signals(e_contact_editor);
	set_entry_changed_signals(e_contact_editor);

	wants_html = glade_xml_get_widget(e_contact_editor->gui, "checkbutton-htmlmail");
	if (wants_html && GTK_IS_TOGGLE_BUTTON(wants_html))
		g_signal_connect (wants_html, "toggled",
				  G_CALLBACK (wants_html_changed), e_contact_editor);

 	widget = glade_xml_get_widget(e_contact_editor->gui, "checkbutton-mailingaddress");
	if (widget && GTK_IS_TOGGLE_BUTTON(widget))
		g_signal_connect (widget, "toggled",
				  G_CALLBACK (address_mailing_changed), e_contact_editor);
	
	widget = glade_xml_get_widget(e_contact_editor->gui, "button-fullname");
	if (widget && GTK_IS_BUTTON(widget))
		g_signal_connect (widget, "clicked",
				  G_CALLBACK (full_name_clicked), e_contact_editor);

	widget = glade_xml_get_widget(e_contact_editor->gui, "button-fulladdr");
	if (widget && GTK_IS_BUTTON(widget))
		g_signal_connect (widget, "clicked",
				  G_CALLBACK (full_addr_clicked), e_contact_editor);

	widget = glade_xml_get_widget(e_contact_editor->gui, "button-categories");
	if (widget && GTK_IS_BUTTON(widget))
		g_signal_connect (widget, "clicked",
				  G_CALLBACK (categories_clicked), e_contact_editor);

	/* Construct the app */
	bonobo_win = bonobo_window_new ("contact-editor-dialog", _("Contact Editor"));

	/* FIXME: The sucking bit */
	{
		GtkWidget *contents;

		contents = bonobo_dock_get_client_area (gnome_app_get_dock (GNOME_APP(e_contact_editor->app)));

		if (!contents) {
			g_message ("contact_editor_construct(): Could not get contents");
			return;
		}
		g_object_ref (contents);
		gtk_container_remove (GTK_CONTAINER (contents->parent), contents);
		bonobo_window_set_contents (BONOBO_WINDOW (bonobo_win), contents);
		gtk_widget_destroy (e_contact_editor->app);
		e_contact_editor->app = bonobo_win;
	}

	/* Build the menu and toolbar */
	container = bonobo_window_get_ui_container (BONOBO_WINDOW (e_contact_editor->app));

	e_contact_editor->uic = bonobo_ui_component_new_default ();
	if (!e_contact_editor->uic) {
		g_message ("e_contact_editor_init(): eeeeek, could not create the UI handler!");
		return;
	}
	bonobo_ui_component_set_container (e_contact_editor->uic,
					   bonobo_object_corba_objref (BONOBO_OBJECT (container)),
					   NULL);

	create_ui (e_contact_editor);

	widget = glade_xml_get_widget(e_contact_editor->gui, "entry-fullname");
	if (widget)
		gtk_widget_grab_focus (widget);

	/* Connect to the deletion of the dialog */

	g_signal_connect (e_contact_editor->app, "delete_event",
			    GTK_SIGNAL_FUNC (app_delete_event_cb), e_contact_editor);

	/* set the icon */
	icon_path = g_build_filename (EVOLUTION_IMAGESDIR, "evolution-contacts-mini.png", NULL);
	gnome_window_icon_set_from_file (GTK_WINDOW (e_contact_editor->app), icon_path);
	g_free (icon_path);
}

void
e_contact_editor_dispose (GObject *object) {
	EContactEditor *e_contact_editor = E_CONTACT_EDITOR(object);

	if (e_contact_editor->writable_fields) {
		g_object_unref(e_contact_editor->writable_fields);
		e_contact_editor->writable_fields = NULL;
	}
	if (e_contact_editor->email_list) {
		g_list_foreach(e_contact_editor->email_list, (GFunc) g_free, NULL);
		g_list_free(e_contact_editor->email_list);
		e_contact_editor->email_list = NULL;
	}
	if (e_contact_editor->email_info) {
		g_free(e_contact_editor->email_info);
		e_contact_editor->email_info = NULL;
	}
	if (e_contact_editor->email_popup) {
		g_object_unref(e_contact_editor->email_popup);
		e_contact_editor->email_popup = NULL;
	}
	
	if (e_contact_editor->phone_list) {
		g_list_foreach(e_contact_editor->phone_list, (GFunc) g_free, NULL);
		g_list_free(e_contact_editor->phone_list);
		e_contact_editor->phone_list = NULL;
	}
	if (e_contact_editor->phone_info) {
		g_free(e_contact_editor->phone_info);
		e_contact_editor->phone_info = NULL;
	}
	if (e_contact_editor->phone_popup) {
		g_object_unref(e_contact_editor->phone_popup);
		e_contact_editor->phone_popup = NULL;
	}
	
	if (e_contact_editor->address_list) {
		g_list_foreach(e_contact_editor->address_list, (GFunc) g_free, NULL);
		g_list_free(e_contact_editor->address_list);
		e_contact_editor->address_list = NULL;
	}
	if (e_contact_editor->address_info) {
		g_free(e_contact_editor->address_info);
		e_contact_editor->address_info = NULL;
	}
	if (e_contact_editor->address_popup) {
		g_object_unref(e_contact_editor->address_popup);
		e_contact_editor->address_popup = NULL;
	}
	
	if (e_contact_editor->simple) {
		g_object_unref(e_contact_editor->simple);
		e_contact_editor->simple = NULL;
	}
	
	if (e_contact_editor->card) {
		g_object_unref(e_contact_editor->card);
		e_contact_editor->card = NULL;
	}

	if (e_contact_editor->book) {
		g_object_unref(e_contact_editor->book);
		e_contact_editor->book = NULL;
	}

	if (e_contact_editor->name) {
		e_card_name_unref(e_contact_editor->name);
		e_contact_editor->name = NULL;
	}

	if (e_contact_editor->company) {
		g_free (e_contact_editor->company);
		e_contact_editor->company = NULL;
	}

	if (e_contact_editor->gui) {
		g_object_unref(e_contact_editor->gui);
		e_contact_editor->gui = NULL;
	}
}

static void
command_state_changed (EContactEditor *ce)
{
	bonobo_ui_component_set_prop (ce->uic,
				      "/commands/ContactEditorSaveClose",
				      "sensitive",
				      ce->changed ? "1" : "0", NULL);
	bonobo_ui_component_set_prop (ce->uic,
				      "/commands/ContactEditorSave",
				      "sensitive",
				      ce->changed ? "1" : "0", NULL);
	bonobo_ui_component_set_prop (ce->uic,
				      "/commands/ContactEditorDelete",
				      "sensitive",
				      (ce->editable && !ce->is_new_card) ? "1" : "0", NULL);
}

static void
supported_fields_cb (EBook *book, EBookStatus status,
		     EList *fields, EContactEditor *ce)
{
	if (!g_slist_find (all_contact_editors, ce)) {
		g_warning ("supported_fields_cb called for book that's still around, but contact editor that's been destroyed.");
		return;
	}

	g_object_set (ce,
			"writable_fields", fields,
			NULL);

	e_contact_editor_show (ce);

	command_state_changed (ce);
}

static void
contact_editor_destroy_notify (void *data,
			       GObject *where_the_object_was)
{
	EContactEditor *ce = E_CONTACT_EDITOR (data);

	all_contact_editors = g_slist_remove (all_contact_editors, ce);
}

EContactEditor *
e_contact_editor_new (EBook *book,
		      ECard *card,
		      gboolean is_new_card,
		      gboolean editable)
{
	EContactEditor *ce;

	g_return_val_if_fail (E_IS_BOOK (book), NULL);
	g_return_val_if_fail (E_IS_CARD (card), NULL);

	ce = g_object_new (E_TYPE_CONTACT_EDITOR, NULL);

	all_contact_editors = g_slist_prepend (all_contact_editors, ce);
	g_object_weak_ref (G_OBJECT (ce), contact_editor_destroy_notify, ce);

	g_object_ref (ce);
	gtk_object_sink (GTK_OBJECT (ce));

	g_object_set (ce,
		      "book", book,
		      "card", card,
		      "is_new_card", is_new_card,
		      "editable", editable,
		      NULL);

	if (book)
		e_book_get_supported_fields (book, (EBookFieldsCallback)supported_fields_cb, ce);

	return ce;
}

static void
e_contact_editor_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	EContactEditor *editor;

	editor = E_CONTACT_EDITOR (object);
	
	switch (prop_id){
	case PROP_BOOK:
		if (editor->book)
			g_object_unref(editor->book);
		editor->book = E_BOOK(g_value_get_object (value));
		g_object_ref (editor->book);
		/* XXX more here about editable/etc. */
		break;
	case PROP_CARD:
		if (editor->card)
			g_object_unref(editor->card);
		editor->card = e_card_duplicate(E_CARD(g_value_get_object (value)));
		g_object_set(editor->simple,
			     "card", editor->card,
			     NULL);
		fill_in_info(editor);
		editor->changed = FALSE;
		break;

	case PROP_IS_NEW_CARD:
		editor->is_new_card = g_value_get_boolean (value) ? TRUE : FALSE;
		break;

	case PROP_EDITABLE: {
		gboolean new_value = g_value_get_boolean (value) ? TRUE : FALSE;
		gboolean changed = (editor->editable != new_value);

		editor->editable = new_value;

		if (changed) {
			set_editable (editor);
			command_state_changed (editor);
		}
		break;
	}

	case PROP_CHANGED: {
		gboolean new_value = g_value_get_boolean (value) ? TRUE : FALSE;
		gboolean changed = (editor->changed != new_value);

		editor->changed = new_value;

		if (changed)
			command_state_changed (editor);
		break;
	}
	case PROP_WRITABLE_FIELDS:
		if (editor->writable_fields)
			g_object_unref(editor->writable_fields);
		editor->writable_fields = g_value_get_object (value);
		if (editor->writable_fields)
			g_object_ref (editor->writable_fields);
		else
			editor->writable_fields = e_list_new(NULL, NULL, NULL);
		enable_writable_fields (editor);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
e_contact_editor_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	EContactEditor *e_contact_editor;

	e_contact_editor = E_CONTACT_EDITOR (object);

	switch (prop_id) {
	case PROP_BOOK:
		g_value_set_object (value, e_contact_editor->book);
		break;

	case PROP_CARD:
		e_card_simple_sync_card(e_contact_editor->simple);
		extract_info(e_contact_editor);
		g_value_set_object (value, e_contact_editor->card);
		break;

	case PROP_IS_NEW_CARD:
		g_value_set_boolean (value, e_contact_editor->is_new_card ? TRUE : FALSE);
		break;

	case PROP_EDITABLE:
		g_value_set_boolean (value, e_contact_editor->editable ? TRUE : FALSE);
		break;

	case PROP_CHANGED:
		g_value_set_boolean (value, e_contact_editor->changed ? TRUE : FALSE);
		break;

	case PROP_WRITABLE_FIELDS:
		if (e_contact_editor->writable_fields)
			g_value_set_object (value, e_list_duplicate (e_contact_editor->writable_fields));
		else
			g_value_set_object (value, NULL);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
_popup_position(GtkMenu *menu,
		gint *x,
		gint *y,
		gboolean *push_in,
		gpointer data)
{
	GtkWidget *button = GTK_WIDGET(data);
	GtkRequisition request;
	int mh, mw;
	gdk_window_get_origin (button->window, x, y);
	*x += button->allocation.x;
	*y += button->allocation.y;

	gtk_widget_size_request(GTK_WIDGET(menu), &request);

	mh = request.height;
	mw = request.width;

	*x -= mw;
	if (*x < 0)
		*x = 0;
	
	if (*y < 0)
		*y = 0;
	
	if ((*x + mw) > gdk_screen_width ())
		*x = gdk_screen_width () - mw;
	
	if ((*y + mh) > gdk_screen_height ())
		*y = gdk_screen_height () - mh;

	*push_in = FALSE;
}

static gint
_arrow_pressed (GtkWidget *widget, GdkEventButton *button, EContactEditor *editor, GtkWidget *popup, GList **list, GnomeUIInfo **info, gchar *label)
{
	gint menu_item;

	g_signal_stop_emission_by_name (widget, "button_press_event");

	gtk_widget_realize(popup);
	menu_item = gnome_popup_menu_do_popup_modal(popup, _popup_position, widget, button, editor, widget);
	if ( menu_item != -1 ) {
		GtkWidget *label_widget = glade_xml_get_widget(editor->gui, label);
		if (label_widget && GTK_IS_LABEL(label_widget)) {
			g_object_set (label_widget,
				      "label", _(g_list_nth_data(*list, menu_item)),
				      NULL);
		}
	}
	return menu_item;
}

static void
e_contact_editor_build_ui_info(GList *list, GnomeUIInfo **infop)
{
	GnomeUIInfo *info;
	GnomeUIInfo singleton = { GNOME_APP_UI_TOGGLEITEM, NULL, NULL, NULL, NULL, NULL, GNOME_APP_PIXMAP_NONE, 0, 0, 0, NULL };
	GnomeUIInfo end = GNOMEUIINFO_END;
	int length;
	int i;

	info = *infop;

	if ( info )
		g_free(info);
	length = g_list_length( list );
	info = g_new(GnomeUIInfo, length + 2);
	for (i = 0; i < length; i++) {
		info[i] = singleton;
		info[i].label = _(list->data);
		list = list->next;
	}
	info[i] = end;

	*infop = info;
}

static void
e_contact_editor_build_phone_ui (EContactEditor *editor)
{
	int i;

	if (editor->phone_list == NULL) {
		static char *info[] = {
			N_("Assistant"),
			N_("Business"),
			N_("Business 2"),
			N_("Business Fax"),
			N_("Callback"),
			N_("Car"),
			N_("Company"),
			N_("Home"),
			N_("Home 2"),
			N_("Home Fax"),
			N_("ISDN"),
			N_("Mobile"),
			N_("Other"),
			N_("Other Fax"),
			N_("Pager"),
			N_("Primary"),
			N_("Radio"),
			N_("Telex"),
			N_("TTY/TDD")
		};
		
		for (i = 0; i < sizeof(info) / sizeof(info[0]); i++) {
			editor->phone_list = g_list_append(editor->phone_list, g_strdup(info[i]));
		}
	}
	if (editor->phone_info == NULL) {
		e_contact_editor_build_ui_info(editor->phone_list, &editor->phone_info);
		
		if ( editor->phone_popup )
			g_object_unref(editor->phone_popup);
		
		editor->phone_popup = gnome_popup_menu_new(editor->phone_info);
		g_object_ref (editor->phone_popup);
		gtk_object_sink (GTK_OBJECT (editor->phone_popup));
	}
}

static void
e_contact_editor_build_email_ui (EContactEditor *editor)
{
	int i;

	if (editor->email_list == NULL) {
		static char *info[] = {
			N_("Primary Email"),
			N_("Email 2"),
			N_("Email 3")
		};
		
		for (i = 0; i < sizeof(info) / sizeof(info[0]); i++) {
			editor->email_list = g_list_append(editor->email_list, g_strdup(info[i]));
		}
	}
	if (editor->email_info == NULL) {
		e_contact_editor_build_ui_info(editor->email_list, &editor->email_info);

		if ( editor->email_popup )
			g_object_unref(editor->email_popup);
		
		editor->email_popup = gnome_popup_menu_new(editor->email_info);
		g_object_ref (editor->email_popup);
		gtk_object_sink (GTK_OBJECT (editor->email_popup));
	}
}

static void
e_contact_editor_build_address_ui (EContactEditor *editor)
{
	int i;

	if (editor->address_list == NULL) {
		static char *info[] = {
			N_("Business"),
			N_("Home"),
			N_("Other")
		};
		
		for (i = 0; i < sizeof(info) / sizeof(info[0]); i++) {
			editor->address_list = g_list_append(editor->address_list, g_strdup(info[i]));
		}
	}
	if (editor->address_info == NULL) {
		e_contact_editor_build_ui_info(editor->address_list, &editor->address_info);

		if ( editor->address_popup )
			g_object_unref(editor->address_popup);
		
		editor->address_popup = gnome_popup_menu_new(editor->address_info);
		g_object_ref (editor->address_popup);
		gtk_object_sink (GTK_OBJECT (editor->address_popup));
	}
}

static void
_phone_arrow_pressed (GtkWidget *widget, GdkEventButton *button, EContactEditor *editor)
{
	int which;
	int i;
	gchar *label;
	gchar *entry;
	int result;
	if ( widget == glade_xml_get_widget(editor->gui, "button-phone1") ) {
		which = 1;
	} else if ( widget == glade_xml_get_widget(editor->gui, "button-phone2") ) {
		which = 2;
	} else if ( widget == glade_xml_get_widget(editor->gui, "button-phone3") ) {
		which = 3;
	} else if ( widget == glade_xml_get_widget(editor->gui, "button-phone4") ) {
		which = 4;
	} else
		return;
	
	label = g_strdup_printf("label-phone%d", which);
	entry = g_strdup_printf("entry-phone%d", which);

	e_contact_editor_build_phone_ui (editor);
	
	for(i = 0; i < E_CARD_SIMPLE_PHONE_ID_LAST; i++) {
		const ECardPhone *phone = e_card_simple_get_phone(editor->simple, i);
		gboolean checked;
		checked = phone && phone->number && *phone->number;
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(editor->phone_info[i].widget),
					       checked);
	}
	
	result = _arrow_pressed (widget, button, editor, editor->phone_popup, &editor->phone_list, &editor->phone_info, label);
	
	if (result != -1) {
		editor->phone_choice[which - 1] = result;
		set_fields(editor);
		enable_widget (glade_xml_get_widget (editor->gui, label), TRUE);
		enable_widget (glade_xml_get_widget (editor->gui, entry), editor->editable);
	}

	g_free(label);
	g_free(entry);
}

static void
_email_arrow_pressed (GtkWidget *widget, GdkEventButton *button, EContactEditor *editor)
{
	int i;
	int result;

	e_contact_editor_build_email_ui (editor);

	for(i = 0; i < E_CARD_SIMPLE_EMAIL_ID_LAST; i++) {
		const char *string = e_card_simple_get_email(editor->simple, i);
		gboolean checked;
		checked = string && *string;
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(editor->email_info[i].widget),
					       checked);
	}
	
	result = _arrow_pressed (widget, button, editor, editor->email_popup, &editor->email_list, &editor->email_info, "label-email1");
	
	if (result != -1) {
		editor->email_choice = result;
		set_fields(editor);

		/* make sure the buttons/entry is/are sensitive */
		enable_widget (glade_xml_get_widget (editor->gui, "label-email1"), TRUE);
		enable_widget (glade_xml_get_widget (editor->gui, "entry-email1"), editor->editable);
		enable_widget (glade_xml_get_widget (editor->gui, "checkbutton-htmlmail"), editor->editable);
	}
}

static void
_address_arrow_pressed (GtkWidget *widget, GdkEventButton *button, EContactEditor *editor)
{
	int i;
	int result;

	e_contact_editor_build_address_ui (editor);

	for(i = 0; i < E_CARD_SIMPLE_ADDRESS_ID_LAST; i++) {
		const ECardAddrLabel *address = e_card_simple_get_address(editor->simple, i);
		gboolean checked;
		checked = address && address->data && *address->data;
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(editor->address_info[i].widget),
					       checked);
	}

	result = _arrow_pressed (widget, button, editor, editor->address_popup, &editor->address_list, &editor->address_info, "label-address");

	if (result != -1) {
		set_address_field(editor, result);

		/* make sure the buttons/entry is/are sensitive */
		enable_widget (glade_xml_get_widget (editor->gui, "label-address"), TRUE);
		enable_widget (glade_xml_get_widget (editor->gui, "text-address"), editor->address_editable[result]);
		enable_widget (glade_xml_get_widget (editor->gui, "checkbutton-mailingaddress"), editor->address_editable[result]);
	}
}

static void
find_address_mailing (EContactEditor *editor)
{
	const ECardDeliveryAddress *address;
	int i;
	
	editor->address_mailing = -1;
	for (i = 0; i < E_CARD_SIMPLE_ADDRESS_ID_LAST; i++) {
		address = e_card_simple_get_delivery_address(editor->simple, i);
		if (address && (address->flags & E_CARD_ADDR_DEFAULT)) {
			if (editor->address_mailing == -1) {
				editor->address_mailing = i;
			} else {
				ECardDeliveryAddress *new;
				
				new = e_card_delivery_address_copy (address);
				new->flags &= ~E_CARD_ADDR_DEFAULT;
				e_card_simple_set_delivery_address(editor->simple, i, new);
				e_card_delivery_address_unref (new);
			}
		}
	}
}

static void
set_field(EContactEditor *editor, GtkEntry *entry, const char *string)
{
	const char *oldstring = gtk_entry_get_text(entry);
	if (!string)
		string = "";
	if (strcmp(string, oldstring)) {
		g_signal_handlers_block_matched (entry,
						 G_SIGNAL_MATCH_DATA,
						 0, 0, NULL, NULL, editor);
		gtk_entry_set_text(entry, string);
		g_signal_handlers_unblock_matched (entry,
						   G_SIGNAL_MATCH_DATA,
						   0, 0, NULL, NULL, editor);
	}
}

static void
set_phone_field(EContactEditor *editor, GtkWidget *entry, const ECardPhone *phone)
{
	set_field(editor, GTK_ENTRY(entry), phone ? phone->number : "");
}

static void
set_fields(EContactEditor *editor)
{
	GtkWidget *entry;
	GtkWidget *label_widget;
	int i;

	entry = glade_xml_get_widget(editor->gui, "entry-phone1");
	if (entry && GTK_IS_ENTRY(entry))
		set_phone_field(editor, entry, e_card_simple_get_phone(editor->simple, editor->phone_choice[0]));

	entry = glade_xml_get_widget(editor->gui, "entry-phone2");
	if (entry && GTK_IS_ENTRY(entry))
		set_phone_field(editor, entry, e_card_simple_get_phone(editor->simple, editor->phone_choice[1]));

	entry = glade_xml_get_widget(editor->gui, "entry-phone3");
	if (entry && GTK_IS_ENTRY(entry))
		set_phone_field(editor, entry, e_card_simple_get_phone(editor->simple, editor->phone_choice[2]));

	entry = glade_xml_get_widget(editor->gui, "entry-phone4");
	if (entry && GTK_IS_ENTRY(entry))
		set_phone_field(editor, entry, e_card_simple_get_phone(editor->simple, editor->phone_choice[3]));
	
	entry = glade_xml_get_widget(editor->gui, "entry-email1");
	if (entry && GTK_IS_ENTRY(entry))
		set_field(editor, GTK_ENTRY(entry), e_card_simple_get_email(editor->simple, editor->email_choice));



	e_contact_editor_build_address_ui (editor);

	for (i = 0; i < E_CARD_SIMPLE_ADDRESS_ID_LAST; i++) {
		const ECardAddrLabel *address = e_card_simple_get_address(editor->simple, i);

		if (address && address->data && *address->data)
			break;
	}
	if (i == E_CARD_SIMPLE_ADDRESS_ID_LAST)
		i = 0;

	label_widget = glade_xml_get_widget(editor->gui, "label-address");
	if (label_widget && GTK_IS_LABEL(label_widget)) {
		g_object_set (label_widget,
			       "label", _(g_list_nth_data(editor->address_list, i)),
			       NULL);
	}

	set_address_field(editor, i);
}

static void
set_address_field(EContactEditor *editor, int result)
{
	GtkWidget *text, *check;
	
	text = glade_xml_get_widget(editor->gui, "text-address");

	if (text && GTK_IS_TEXT_VIEW(text)) {
		GtkTextBuffer *buffer;
		GtkTextIter start_iter, end_iter;
		const ECardAddrLabel *address;

		if (result == -1)
			result = editor->address_choice;
		editor->address_choice = -1;

		buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (text));

		gtk_text_buffer_get_start_iter (buffer, &start_iter);
		gtk_text_buffer_get_end_iter (buffer, &end_iter);

		gtk_text_buffer_delete (buffer, &start_iter, &end_iter);

		address = e_card_simple_get_address(editor->simple, result);
		if (address && address->data)
			gtk_text_buffer_insert (buffer, &start_iter, address->data, strlen (address->data));

		check = glade_xml_get_widget(editor->gui, "checkbutton-mailingaddress");
		if (check && GTK_IS_CHECK_BUTTON (check)) {
			if (address && address->data)
				gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check), 
							      address->flags & E_CARD_ADDR_DEFAULT);
			else
				gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check), FALSE);
		}
		
		editor->address_choice = result;
	}
}

static void
add_field_callback(GtkWidget *widget, EContactEditor *editor)
{
	const char *name;
	int i;
	static const char *builtins[] = {
		"entry-fullname",
		"entry-web",
		"entry-company",
		"entry-department",
		"entry-office",
		"entry-jobtitle",
		"entry-profession",
		"entry-manager",
		"entry-assistant",
		"entry-nickname",
		"entry-spouse",
		"text-comments",
		"entry-categories",
		"entry-file-as",
		"dateedit-anniversary",
		"dateedit-birthday",
		"entry-phone1",
		"entry-phone2",
		"entry-phone3",
		"entry-phone4",
		"entry-email1",
		"text-address",
		"checkbutton-mailingaddress",
		"checkbutton-htmlmail",
		"entry-caluri",
		"entry-fburl",
		NULL
	};
	name = glade_get_widget_name(widget);
	if (name) {
		for (i = 0; builtins[i]; i++) {
			if (!strcmp(name, builtins[i]))
				return;
		}
		if (GTK_IS_ENTRY(widget) || GTK_IS_TEXT_VIEW(widget)) {
			editor->arbitrary_fields = g_list_prepend(editor->arbitrary_fields, g_strdup(name));
		}
	}
}

static struct {
	char *id;
	char *key;
} field_mapping [] = {
	{ "entry-fullname", "full_name" },
	{ "entry-web", "url" },
	{ "entry-company", "org" },
	{ "entry-department", "org_unit" },
	{ "entry-office", "office" },
	{ "entry-jobtitle", "title" },
	{ "entry-profession", "role" },
	{ "entry-manager", "manager" },
	{ "entry-assistant", "assistant" },
	{ "entry-nickname", "nickname" },
	{ "entry-spouse", "spouse" },
	{ "text-comments", "note" },
	{ "entry-categories", "categories" },
	{ "entry-caluri", "caluri" },
	{ "entry-fburl", "fburl" },
};

static void
fill_in_field(EContactEditor *editor, char *id, char *value)
{
	GtkWidget *widget = glade_xml_get_widget(editor->gui, id);

	if (!widget)
		return;

	if (E_IS_URL_ENTRY (widget))
		widget = e_url_entry_get_entry (E_URL_ENTRY (widget));

	if (GTK_IS_TEXT_VIEW (widget)) {
		if (value)
			gtk_text_buffer_set_text (gtk_text_view_get_buffer (GTK_TEXT_VIEW (widget)),
						  value, strlen (value));
	}
	else if (GTK_IS_EDITABLE(widget)) {
		int position = 0;
		GtkEditable *editable = GTK_EDITABLE(widget);
		gtk_editable_delete_text(editable, 0, -1);
		if (value)
			gtk_editable_insert_text(editable, value, strlen(value), &position);
	}
}

static void
fill_in_card_field(EContactEditor *editor, ECard *card, char *id, char *key)
{
	char *string;
	g_object_get (card,
		       key, &string,
		       NULL);
	fill_in_field(editor, id, string);
	g_free (string);
}

static void
fill_in_single_field(EContactEditor *editor, char *name)
{
	ECardSimple *simple = editor->simple;
	GtkWidget *widget = glade_xml_get_widget(editor->gui, name);

	if (widget && GTK_IS_EDITABLE(widget)) {
		int position = 0;
		GtkEditable *editable = GTK_EDITABLE(widget);
		const ECardArbitrary *arbitrary;

		gtk_editable_delete_text(editable, 0, -1);
		arbitrary = e_card_simple_get_arbitrary(simple,
							name);
		if (arbitrary && arbitrary->value)
			gtk_editable_insert_text(editable, arbitrary->value, strlen(arbitrary->value), &position);
	}
}

static void
disable_widget_foreach (char *key, GtkWidget *widget, gpointer closure)
{
	enable_widget (widget, FALSE);
}

static struct {
	char *widget_name;
	ECardSimpleField field_id;
	gboolean desensitize_for_read_only;
} widget_field_mappings[] = {
	{ "entry-web", E_CARD_SIMPLE_FIELD_URL, TRUE },
	{ "accellabel-web", E_CARD_SIMPLE_FIELD_URL },

	{ "entry-jobtitle", E_CARD_SIMPLE_FIELD_TITLE, TRUE },
	{ "label-jobtitle", E_CARD_SIMPLE_FIELD_TITLE },

	{ "entry-company", E_CARD_SIMPLE_FIELD_ORG, TRUE },
	{ "label-company", E_CARD_SIMPLE_FIELD_ORG },

	{ "combo-file-as", E_CARD_SIMPLE_FIELD_FILE_AS, TRUE },
	{ "entry-file-as", E_CARD_SIMPLE_FIELD_FILE_AS, TRUE },
	{ "accellabel-fileas", E_CARD_SIMPLE_FIELD_FILE_AS },

	{ "label-department", E_CARD_SIMPLE_FIELD_ORG_UNIT },
	{ "entry-department", E_CARD_SIMPLE_FIELD_ORG_UNIT, TRUE },

	{ "label-office", E_CARD_SIMPLE_FIELD_OFFICE },
	{ "entry-office", E_CARD_SIMPLE_FIELD_OFFICE, TRUE },

	{ "label-profession", E_CARD_SIMPLE_FIELD_ROLE },
	{ "entry-profession", E_CARD_SIMPLE_FIELD_ROLE, TRUE },

	{ "label-manager", E_CARD_SIMPLE_FIELD_MANAGER },
	{ "entry-manager", E_CARD_SIMPLE_FIELD_MANAGER, TRUE },

	{ "label-assistant", E_CARD_SIMPLE_FIELD_ASSISTANT },
	{ "entry-assistant", E_CARD_SIMPLE_FIELD_ASSISTANT, TRUE },

	{ "label-nickname", E_CARD_SIMPLE_FIELD_NICKNAME },
	{ "entry-nickname", E_CARD_SIMPLE_FIELD_NICKNAME, TRUE },

	{ "label-spouse", E_CARD_SIMPLE_FIELD_SPOUSE },
	{ "entry-spouse", E_CARD_SIMPLE_FIELD_SPOUSE, TRUE },

	{ "label-birthday", E_CARD_SIMPLE_FIELD_BIRTH_DATE },
	{ "dateedit-birthday", E_CARD_SIMPLE_FIELD_BIRTH_DATE, TRUE },

	{ "label-anniversary", E_CARD_SIMPLE_FIELD_ANNIVERSARY },
	{ "dateedit-anniversary", E_CARD_SIMPLE_FIELD_ANNIVERSARY, TRUE },

	{ "label-comments", E_CARD_SIMPLE_FIELD_NOTE },
	{ "text-comments", E_CARD_SIMPLE_FIELD_NOTE, TRUE },

	{ "entry-fullname", E_CARD_SIMPLE_FIELD_FULL_NAME, TRUE },

	{ "button-categories", E_CARD_SIMPLE_FIELD_CATEGORIES, TRUE },
	{ "entry-categories", E_CARD_SIMPLE_FIELD_CATEGORIES, TRUE },

	{ "label-caluri", E_CARD_SIMPLE_FIELD_CALURI },
	{ "entry-caluri", E_CARD_SIMPLE_FIELD_CALURI, TRUE },

	{ "label-fburl", E_CARD_SIMPLE_FIELD_FBURL },
	{ "entry-fburl", E_CARD_SIMPLE_FIELD_FBURL, TRUE }
};
static int num_widget_field_mappings = sizeof(widget_field_mappings) / sizeof (widget_field_mappings[0]);

static void
enable_writable_fields(EContactEditor *editor)
{
	EList *fields = editor->writable_fields;
	EIterator *iter;
	GHashTable *dropdown_hash, *supported_hash;
	int i;
	ECardSimple *simple;
	ECard *card;
	char *widget_name;

	if (!fields)
		return;

	card = e_card_new ("");
	simple = e_card_simple_new (card);

	dropdown_hash = g_hash_table_new (g_str_hash, g_str_equal);
	supported_hash = g_hash_table_new (g_str_hash, g_str_equal);

	/* build our hashtable of the drop down menu items */
	e_contact_editor_build_phone_ui (editor);
	for (i = 0; i < E_CARD_SIMPLE_PHONE_ID_LAST; i ++)
		g_hash_table_insert (dropdown_hash,
				     (char*)e_card_simple_get_ecard_field(simple, e_card_simple_map_phone_to_field (i)),
				     editor->phone_info[i].widget);
	e_contact_editor_build_email_ui (editor);
	for (i = 0; i < E_CARD_SIMPLE_EMAIL_ID_LAST; i ++)
		g_hash_table_insert (dropdown_hash,
				     (char*)e_card_simple_get_ecard_field(simple, e_card_simple_map_email_to_field (i)),
				     editor->email_info[i].widget);
	e_contact_editor_build_address_ui (editor);
	for (i = 0; i < E_CARD_SIMPLE_ADDRESS_ID_LAST; i ++)
		g_hash_table_insert (dropdown_hash,
				     (char*)e_card_simple_get_ecard_field(simple, e_card_simple_map_address_to_field (i)),
				     editor->address_info[i].widget);

	/* then disable them all */
	g_hash_table_foreach (dropdown_hash, (GHFunc)disable_widget_foreach, NULL);

	/* disable the label widgets for the dropdowns (4 phone, 1
           email and the toggle button, and 1 address and one for
           the full address button */
	for (i = 0; i < 4; i ++) {
		widget_name = g_strdup_printf ("label-phone%d", i+1);
		enable_widget (glade_xml_get_widget (editor->gui, widget_name), FALSE);
		g_free (widget_name);
		widget_name = g_strdup_printf ("entry-phone%d", i+1);
		enable_widget (glade_xml_get_widget (editor->gui, widget_name), FALSE);
		g_free (widget_name);
	}
	enable_widget (glade_xml_get_widget (editor->gui, "label-email1"), FALSE);
	enable_widget (glade_xml_get_widget (editor->gui, "entry-email1"), FALSE);
	enable_widget (glade_xml_get_widget (editor->gui, "checkbutton-htmlmail"), FALSE);
	enable_widget (glade_xml_get_widget (editor->gui, "checkbutton-mailingaddress"), FALSE);
	enable_widget (glade_xml_get_widget (editor->gui, "label-address"), FALSE);
	enable_widget (glade_xml_get_widget (editor->gui, "text-address"), FALSE);

	editor->fullname_editable = FALSE;

	/* enable widgets that map directly from a field to a widget (the drop down items) */
	iter = e_list_get_iterator (fields);
	for (; e_iterator_is_valid (iter); e_iterator_next (iter)) {
		char *field = (char*)e_iterator_get (iter);
		GtkWidget *widget = g_hash_table_lookup (dropdown_hash, field);
		int i;

		if (widget) {
			enable_widget (widget, TRUE);
		}
		else {
			/* if it's not a field that's handled by the
                           dropdown items, add it to the has to be
                           used in the second step */
			g_hash_table_insert (supported_hash, field, field);
		}

		for (i = 0; i < E_CARD_SIMPLE_ADDRESS_ID_LAST; i ++) {
			if (!strcmp (field, e_card_simple_get_ecard_field (simple, e_card_simple_map_address_to_field(i)))) {
				editor->address_editable [i] = TRUE;
			}
		}

		/* ugh - this is needed to make sure we don't have a
                   disabled label next to a drop down when the item in
                   the menu (the one reflected in the label) is
                   enabled. */
		if (!strcmp (field, e_card_simple_get_ecard_field (simple, e_card_simple_map_email_to_field(editor->email_choice)))) {
			enable_widget (glade_xml_get_widget (editor->gui, "label-email1"), TRUE);
			enable_widget (glade_xml_get_widget (editor->gui, "entry-email1"), editor->editable);
			enable_widget (glade_xml_get_widget (editor->gui, "checkbutton-htmlmail"), editor->editable);
		}
		else if (!strcmp (field, e_card_simple_get_ecard_field (simple, e_card_simple_map_address_to_field(editor->address_choice)))) {
			enable_widget (glade_xml_get_widget (editor->gui, "label-address"), TRUE);
			enable_widget (glade_xml_get_widget (editor->gui, "checkbutton-mailingaddress"), editor->editable);
			enable_widget (glade_xml_get_widget (editor->gui, "text-address"), editor->editable);
		}
		else for (i = 0; i < 4; i ++) {
			if (!strcmp (field, e_card_simple_get_ecard_field (simple, e_card_simple_map_phone_to_field(editor->phone_choice[i])))) {
				widget_name = g_strdup_printf ("label-phone%d", i+1);
				enable_widget (glade_xml_get_widget (editor->gui, widget_name), TRUE);
				g_free (widget_name);
				widget_name = g_strdup_printf ("entry-phone%d", i+1);
				enable_widget (glade_xml_get_widget (editor->gui, widget_name), editor->editable);
				g_free (widget_name);
			}
		}
	}

	/* handle the label next to the dropdown widgets */

	for (i = 0; i < num_widget_field_mappings; i ++) {
		gboolean enabled;
		GtkWidget *w;
		const char *field;

		w = glade_xml_get_widget(editor->gui, widget_field_mappings[i].widget_name);
		if (!w) {
			g_warning (_("Could not find widget for a field: `%s'"),
				   widget_field_mappings[i].widget_name);
			continue;
		}
		field = e_card_simple_get_ecard_field (simple,
						       widget_field_mappings[i].field_id);

		enabled = (g_hash_table_lookup (supported_hash, field) != NULL);

		if (widget_field_mappings[i].desensitize_for_read_only && !editor->editable) {
			enabled = FALSE;
		}

		enable_widget (w, enabled);
	}

	editor->fullname_editable = (g_hash_table_lookup (supported_hash, "full_name") != NULL);

	g_hash_table_destroy (dropdown_hash);
	g_hash_table_destroy (supported_hash);
	g_object_unref (simple);
	g_object_unref (card);
}

static void
set_editable (EContactEditor *editor)
{
	int i;
	char *entry;
	/* set the sensitivity of all the non-dropdown entry/texts/dateedits */
	for (i = 0; i < num_widget_field_mappings; i ++) {
		if (widget_field_mappings[i].desensitize_for_read_only) {
			GtkWidget *widget = glade_xml_get_widget(editor->gui, widget_field_mappings[i].widget_name);
			enable_widget (widget, editor->editable);
		}
	}

	/* handle the phone dropdown entries */
	for (i = 0; i < 4; i ++) {
		entry = g_strdup_printf ("entry-phone%d", i+1);

		enable_widget (glade_xml_get_widget(editor->gui, entry),
			       editor->editable);

		g_free (entry);
	}

	/* handle the email dropdown entry */
	entry = "entry-email1";
	enable_widget (glade_xml_get_widget(editor->gui, entry),
		       editor->editable);
	enable_widget (glade_xml_get_widget(editor->gui, "checkbutton-htmlmail"),
		       editor->editable);

	/* handle the address dropdown entry */
	entry = "text-address";
	enable_widget (glade_xml_get_widget(editor->gui, entry),
		       editor->editable);
}

static void
fill_in_info(EContactEditor *editor)
{
	ECard *card = editor->card;
	if (card) {
		char *file_as;
		ECardName *name;
		const ECardDate *anniversary;
		const ECardDate *bday;
		int i;
		GtkWidget *widget;
		GList *list;
		gboolean wants_html, wants_html_set;

		g_object_get (card,
			       "file_as",          &file_as,
			       "name",             &name,
			       "anniversary",      &anniversary,
			       "birth_date",       &bday,
			       "wants_html_set",   &wants_html_set,
			       "wants_html",       &wants_html,
			       NULL);
	
		for (i = 0; i < sizeof(field_mapping) / sizeof(field_mapping[0]); i++) {
			fill_in_card_field(editor, card, field_mapping[i].id, field_mapping[i].key);
		}

		for (list = editor->arbitrary_fields; list; list = list->next) {
			fill_in_single_field(editor, list->data);
		}

		find_address_mailing (editor);
		
		if (wants_html_set) {
			GtkWidget *widget = glade_xml_get_widget(editor->gui, "checkbutton-htmlmail");
			if (widget && GTK_IS_CHECK_BUTTON(widget)) {
				g_object_set (widget,
					       "active", wants_html,
					       NULL);
			}
		}

		/* File as has to come after company and name or else it'll get messed up when setting them. */
		fill_in_field(editor, "entry-file-as", file_as);
		g_free (file_as);

		e_card_name_unref(editor->name);
		editor->name = e_card_name_ref(name);

		widget = glade_xml_get_widget(editor->gui, "dateedit-anniversary");
		if (widget && E_IS_DATE_EDIT(widget)) {
			EDateEdit *dateedit;
			dateedit = E_DATE_EDIT(widget);
			if (anniversary)
				e_date_edit_set_date (dateedit,
						      anniversary->year,
						      anniversary->month,
						      anniversary->day);
			else
				e_date_edit_set_time (dateedit, -1);
		}

		widget = glade_xml_get_widget(editor->gui, "dateedit-birthday");
		if (widget && E_IS_DATE_EDIT(widget)) {
			EDateEdit *dateedit;
			dateedit = E_DATE_EDIT(widget);
			if (bday)
				e_date_edit_set_date (dateedit,
						      bday->year,
						      bday->month,
						      bday->day);
			else
				e_date_edit_set_time (dateedit, -1);
		}

		set_fields(editor);
	}
}

static void
extract_field(EContactEditor *editor, ECard *card, char *editable_id, char *key)
{
	GtkWidget *widget = glade_xml_get_widget(editor->gui, editable_id);
	char *string = NULL;

	if (!widget)
		return;

	if (E_IS_URL_ENTRY (widget))
		widget = e_url_entry_get_entry (E_URL_ENTRY (widget));

	if (GTK_IS_EDITABLE (widget))
		string = gtk_editable_get_chars(GTK_EDITABLE (widget), 0, -1);
	else if (GTK_IS_TEXT_VIEW (widget)) {
		GtkTextIter start, end;
		GtkTextBuffer *buffer;

		buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (widget));
		gtk_text_buffer_get_start_iter (buffer, &start);
		gtk_text_buffer_get_end_iter (buffer, &end);

		string = gtk_text_buffer_get_text (buffer, &start, &end, TRUE);
	}

	if (string && *string)
		g_object_set (card,
			      key, string,
			      NULL);
	else
		g_object_set (card,
			      key, NULL,
			      NULL);

	if (string) g_free(string);
}

static void
extract_single_field(EContactEditor *editor, char *name)
{
	GtkWidget *widget = glade_xml_get_widget(editor->gui, name);
	ECardSimple *simple = editor->simple;
	if (widget && GTK_IS_EDITABLE(widget)) {
		GtkEditable *editable = GTK_EDITABLE(widget);
		char *string = gtk_editable_get_chars(editable, 0, -1);

		if (string && *string)
			e_card_simple_set_arbitrary(simple,
						    name,
						    NULL,
						    string);
		else
			e_card_simple_set_arbitrary(simple,
						    name,
						    NULL,
						    NULL);
		if (string) g_free(string);
	}
}

static void
extract_info(EContactEditor *editor)
{
	ECard *card = editor->card;
	if (card) {
		ECardDate anniversary;
		ECardDate bday;
		int i;
		GtkWidget *widget;
		GList *list;

		widget = glade_xml_get_widget(editor->gui, "entry-file-as");
		if (widget && GTK_IS_EDITABLE(widget)) {
			GtkEditable *editable = GTK_EDITABLE(widget);
			char *string = gtk_editable_get_chars(editable, 0, -1);

			if (string && *string)
				g_object_set (card,
					       "file_as", string,
					       NULL);

			if (string) g_free(string);
		}

		for (i = 0; i < sizeof(field_mapping) / sizeof(field_mapping[0]); i++) {
			extract_field(editor, card, field_mapping[i].id, field_mapping[i].key);
		}

		for (list = editor->arbitrary_fields; list; list = list->next) {
			extract_single_field(editor, list->data);
		}

		if (editor->name)
			g_object_set (card,
				       "name", editor->name,
				       NULL);

		widget = glade_xml_get_widget(editor->gui, "dateedit-anniversary");
		if (widget && E_IS_DATE_EDIT(widget)) {
			if (e_date_edit_get_date (E_DATE_EDIT (widget),
						  &anniversary.year,
						  &anniversary.month,
						  &anniversary.day)) {
				/* g_print ("%d %d %d\n", anniversary.year, anniversary.month, anniversary.day); */
				g_object_set (card,
					       "anniversary", &anniversary,
					       NULL);
			} else
				g_object_set (card,
					       "anniversary", NULL,
					       NULL);
		}

		widget = glade_xml_get_widget(editor->gui, "dateedit-birthday");
		if (widget && E_IS_DATE_EDIT(widget)) {
			if (e_date_edit_get_date (E_DATE_EDIT (widget),
						  &bday.year,
						  &bday.month,
						  &bday.day)) {
				/* g_print ("%d %d %d\n", bday.year, bday.month, bday.day); */
				g_object_set (card,
					       "birth_date", &bday,
					       NULL);
			} else
				g_object_set (card,
					       "birth_date", NULL,
					       NULL);
		}
	}
}

/**
 * e_contact_editor_raise:
 * @config: The %EContactEditor object.
 *
 * Raises the dialog associated with this %EContactEditor object.
 */
void
e_contact_editor_raise (EContactEditor *editor)
{
	/* FIXME: perhaps we should raise at realize time */
	if (GTK_WIDGET (editor->app)->window)
		gdk_window_raise (GTK_WIDGET (editor->app)->window);
}

/**
 * e_contact_editor_show:
 * @ce: The %EContactEditor object.
 *
 * Shows the dialog associated with this %EContactEditor object.
 */
void
e_contact_editor_show (EContactEditor *ce)
{
	gtk_widget_show (ce->app);
}

GtkWidget *
e_contact_editor_create_date(gchar *name,
			     gchar *string1, gchar *string2,
			     gint int1, gint int2);

GtkWidget *
e_contact_editor_create_date(gchar *name,
			     gchar *string1, gchar *string2,
			     gint int1, gint int2)
{
	GtkWidget *widget = e_date_edit_new ();
	e_date_edit_set_allow_no_date_set (E_DATE_EDIT (widget),
					   TRUE);
	e_date_edit_set_show_time (E_DATE_EDIT (widget), FALSE);
	e_date_edit_set_time (E_DATE_EDIT (widget), -1);
	gtk_widget_show (widget);
	return widget;
}

GtkWidget *
e_contact_editor_create_web(gchar *name,
			    gchar *string1, gchar *string2,
			    gint int1, gint int2);

GtkWidget *
e_contact_editor_create_web(gchar *name,
			    gchar *string1, gchar *string2,
			    gint int1, gint int2)
{
	GtkWidget *widget = e_url_entry_new ();
	gtk_widget_show (widget);
	return widget;
}

static void
enable_widget (GtkWidget *widget, gboolean enabled)
{
	if (GTK_IS_ENTRY (widget)) {
		gtk_editable_set_editable (GTK_EDITABLE (widget), enabled);
	}
	else if (GTK_IS_TEXT_VIEW (widget)) {
		gtk_text_view_set_editable (GTK_TEXT_VIEW (widget), enabled);
	}
	else if (GTK_IS_COMBO (widget)) {
		gtk_editable_set_editable (GTK_EDITABLE (GTK_COMBO (widget)->entry),
					   enabled);
		gtk_widget_set_sensitive (GTK_COMBO (widget)->button, enabled);
	}
	else if (E_IS_URL_ENTRY (widget)) {
		GtkWidget *e = e_url_entry_get_entry (E_URL_ENTRY (widget));
		gtk_editable_set_editable (GTK_EDITABLE (e), enabled);
	}
	else if (E_IS_DATE_EDIT (widget)) {
		e_date_edit_set_editable (E_DATE_EDIT (widget), enabled);
	}
	else
		gtk_widget_set_sensitive (widget, enabled);
}


gboolean
e_contact_editor_request_close_all (void)
{
	GSList *p;
	GSList *pnext;
	gboolean retval;

	retval = TRUE;
	for (p = all_contact_editors; p != NULL; p = pnext) {
		pnext = p->next;

		e_contact_editor_raise (E_CONTACT_EDITOR (p->data));
		if (! prompt_to_save_changes (E_CONTACT_EDITOR (p->data))) {
			retval = FALSE;
			break;
		}

		close_dialog (E_CONTACT_EDITOR (p->data));
	}

	return retval;
}
