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

#include "eab-editor.h"
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
#include <libgnome/gnome-util.h>
#include <libgnome/gnome-i18n.h>
#include <libgnome/gnome-help.h>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gal/widgets/e-categories.h>
#include <gal/widgets/e-gui-utils.h>
#include <gal/e-text/e-entry.h>

#include <libebook/e-address-western.h>

#include <e-util/e-categories-master-list-wombat.h>

#include <camel/camel.h>

#include "addressbook/gui/component/addressbook.h"
#include "addressbook/printing/e-contact-print.h"
#include "addressbook/printing/e-contact-print-envelope.h"
#include "addressbook/gui/widgets/eab-gui-util.h"
#include "e-util/e-gui-utils.h"
#include "widgets/misc/e-dateedit.h"
#include "widgets/misc/e-image-chooser.h"
#include "widgets/misc/e-url-entry.h"
#include "widgets/misc/e-source-option-menu.h"
#include "shell/evolution-shell-component-utils.h"

#include "eab-contact-merging.h"

#include "e-contact-editor-address.h"
#include "e-contact-editor-im.h"
#include "e-contact-editor-fullname.h"
#include "e-contact-editor-marshal.h"

#define EMAIL_SLOTS   4
#define PHONE_SLOTS   4
#define IM_SLOTS      3
#define ADDRESS_SLOTS 3

#define EVOLUTION_UI_SLOT_PARAM "X-EVOLUTION-UI-SLOT"

/* IM columns */
enum {
	COLUMN_IM_ICON,
	COLUMN_IM_SERVICE,
	COLUMN_IM_SCREENNAME,
	COLUMN_IM_LOCATION,
	COLUMN_IM_LOCATION_TYPE,
	COLUMN_IM_SERVICE_FIELD,
	NUM_IM_COLUMNS
};


static void e_contact_editor_init		(EContactEditor		 *editor);
static void e_contact_editor_class_init	(EContactEditorClass	 *klass);
static void e_contact_editor_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void e_contact_editor_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static void e_contact_editor_dispose (GObject *object);

static void e_contact_editor_raise 	    (EABEditor *editor);
static void e_contact_editor_show  	    (EABEditor *editor);
static void e_contact_editor_save_contact   (EABEditor *editor, gboolean should_close);
static void e_contact_editor_close 	    (EABEditor *editor);
static gboolean e_contact_editor_is_valid   (EABEditor *editor);
static gboolean e_contact_editor_is_changed (EABEditor *editor);
static GtkWindow* e_contact_editor_get_window (EABEditor *editor);

static void save_contact (EContactEditor *ce, gboolean should_close);
static void entry_activated (EContactEditor *editor);
static void enable_writable_fields(EContactEditor *editor);
static void set_editable(EContactEditor *editor);
static void fill_in_info(EContactEditor *editor);
static void extract_info(EContactEditor *editor);
static void set_entry_text(EContactEditor *editor, GtkEntry *entry, const char *string);
static void set_fields(EContactEditor *editor);
static void command_state_changed (EContactEditor *ce);
static void widget_changed (GtkWidget *widget, EContactEditor *editor);
static void enable_widget (GtkWidget *widget, gboolean enabled);

static EABEditorClass *parent_class = NULL;

/* The arguments we take */
enum {
	PROP_0,
	PROP_SOURCE_BOOK,
	PROP_TARGET_BOOK,
	PROP_CONTACT,
	PROP_IS_NEW_CONTACT,
	PROP_EDITABLE,
	PROP_CHANGED,
	PROP_WRITABLE_FIELDS
};

enum {
	DYNAMIC_LIST_EMAIL,
	DYNAMIC_LIST_PHONE,
	DYNAMIC_LIST_ADDRESS
};

static struct {
	EContactField field_id;
	const gchar *type_1;
	const gchar *type_2;
}
phones [] = {
	{ E_CONTACT_PHONE_ASSISTANT,    EVC_X_ASSISTANT,       NULL    },
	{ E_CONTACT_PHONE_BUSINESS,     "WORK",                "VOICE" },
	{ E_CONTACT_PHONE_BUSINESS_FAX, "WORK",                "FAX"   },
	{ E_CONTACT_PHONE_CALLBACK,     EVC_X_CALLBACK,        NULL    },
	{ E_CONTACT_PHONE_CAR,          "CAR",                 NULL    },
	{ E_CONTACT_PHONE_COMPANY,      "X-EVOLUTION-COMPANY", NULL    },
	{ E_CONTACT_PHONE_HOME,         "HOME",                "VOICE" },
	{ E_CONTACT_PHONE_HOME_FAX,     "HOME",                "FAX"   },
	{ E_CONTACT_PHONE_ISDN,         "ISDN",                NULL    },
	{ E_CONTACT_PHONE_MOBILE,       "CELL",                NULL    },
	{ E_CONTACT_PHONE_OTHER,        "VOICE",               NULL    },
	{ E_CONTACT_PHONE_OTHER_FAX,    "FAX",                 NULL    },
	{ E_CONTACT_PHONE_PAGER,        "PAGER",               NULL    },
	{ E_CONTACT_PHONE_PRIMARY,      "PREF",                NULL    },
	{ E_CONTACT_PHONE_RADIO,        EVC_X_RADIO,           NULL    },
	{ E_CONTACT_PHONE_TELEX,        EVC_X_TELEX,           NULL    },
	{ E_CONTACT_PHONE_TTYTDD,       EVC_X_TTYTDD,          NULL    }
};

/* Defaults from the table above */
gint phones_default [] = { 1, 6, 9, 2 };

static EContactField addresses[] = {
	E_CONTACT_ADDRESS_WORK,
	E_CONTACT_ADDRESS_HOME,
	E_CONTACT_ADDRESS_OTHER
};

static gchar *address_name [] = {
	"work",
	"home",
	"other"
};

static struct {
	EContactField  field;
	gchar         *pretty_name;
}
im_service [] =
{
	{ E_CONTACT_IM_AIM,       N_ ("AIM")       },
	{ E_CONTACT_IM_JABBER,    N_ ("Jabber")    },
	{ E_CONTACT_IM_YAHOO,     N_ ("Yahoo")     },
	{ E_CONTACT_IM_MSN,       N_ ("MSN")       },
	{ E_CONTACT_IM_ICQ,       N_ ("ICQ")       },
	{ E_CONTACT_IM_GROUPWISE, N_ ("GroupWise") }
};

/* Defaults from the table above */
gint im_service_default [] = { 0, 2, 3 };

static struct {
	gchar *name;
	gchar *pretty_name;
}
common_location [] =
{
	{ "WORK",  N_ ("Work")  },
	{ "HOME",  N_ ("Home")  },
	{ "OTHER", N_ ("Other") }
};

/* Default from the table above */
gint email_default [] = { 0, 1, 2, 2 };

#define nonempty(x) ((x) && *(x))

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

		contact_editor_type = g_type_register_static (EAB_TYPE_EDITOR, "EContactEditor", &contact_editor_info, 0);
	}

	return contact_editor_type;
}

static void
e_contact_editor_class_init (EContactEditorClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	EABEditorClass *editor_class = EAB_EDITOR_CLASS (klass);

	parent_class = g_type_class_ref (EAB_TYPE_EDITOR);

	object_class->set_property = e_contact_editor_set_property;
	object_class->get_property = e_contact_editor_get_property;
	object_class->dispose = e_contact_editor_dispose;

	editor_class->raise = e_contact_editor_raise;
	editor_class->show = e_contact_editor_show;
	editor_class->close = e_contact_editor_close;
	editor_class->is_valid = e_contact_editor_is_valid;
	editor_class->save_contact = e_contact_editor_save_contact;
	editor_class->is_changed = e_contact_editor_is_changed;
	editor_class->get_window = e_contact_editor_get_window;

	g_object_class_install_property (object_class, PROP_SOURCE_BOOK, 
					 g_param_spec_object ("source_book",
							      _("Source Book"),
							      /*_( */"XXX blurb" /*)*/,
							      E_TYPE_BOOK,
							      G_PARAM_READWRITE));

	g_object_class_install_property (object_class, PROP_TARGET_BOOK, 
					 g_param_spec_object ("target_book",
							      _("Target Book"),
							      /*_( */"XXX blurb" /*)*/,
							      E_TYPE_BOOK,
							      G_PARAM_READWRITE));

	g_object_class_install_property (object_class, PROP_CONTACT, 
					 g_param_spec_object ("contact",
							      _("Contact"),
							      /*_( */"XXX blurb" /*)*/,
							      E_TYPE_CONTACT,
							      G_PARAM_READWRITE));

	g_object_class_install_property (object_class, PROP_IS_NEW_CONTACT, 
					 g_param_spec_boolean ("is_new_contact",
							       _("Is New Contact"),
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
}

static void
init_email_record_location (EContactEditor *editor, gint record)
{
	GtkWidget *location_option_menu;
	GtkWidget *location_menu;
	GtkWidget *email_entry;
	gchar     *widget_name;
	gint       i;

	widget_name = g_strdup_printf ("entry-email-%d", record);
	email_entry = glade_xml_get_widget (editor->gui, widget_name);
	g_free (widget_name);

	widget_name = g_strdup_printf ("optionmenu-email-%d", record);
	location_option_menu = glade_xml_get_widget (editor->gui, widget_name);
	g_free (widget_name);

	g_signal_connect (location_option_menu, "changed", G_CALLBACK (widget_changed), editor);
	g_signal_connect (email_entry, "changed", G_CALLBACK (widget_changed), editor);
	g_signal_connect_swapped (email_entry, "activate", G_CALLBACK (entry_activated), editor);

	location_menu = gtk_menu_new ();

	for (i = 0; i < G_N_ELEMENTS (common_location); i++) {
		GtkWidget *item;

		item = gtk_menu_item_new_with_label (common_location [i].pretty_name);
		gtk_menu_shell_append (GTK_MENU_SHELL (location_menu), item);
	}

	gtk_widget_show_all (location_menu);
	gtk_option_menu_set_menu (GTK_OPTION_MENU (location_option_menu), location_menu);
}

static void
init_email (EContactEditor *editor)
{
	gint i;

	for (i = 1; i <= EMAIL_SLOTS; i++)
		init_email_record_location (editor, i);
}

static void
fill_in_email_record (EContactEditor *editor, gint record, const gchar *address, gint location)
{
	GtkWidget *location_option_menu;
	GtkWidget *email_entry;
	gchar     *widget_name;

	widget_name = g_strdup_printf ("optionmenu-email-%d", record);
	location_option_menu = glade_xml_get_widget (editor->gui, widget_name);
	g_free (widget_name);

	widget_name = g_strdup_printf ("entry-email-%d", record);
	email_entry = glade_xml_get_widget (editor->gui, widget_name);
	g_free (widget_name);

	gtk_option_menu_set_history (GTK_OPTION_MENU (location_option_menu),
				     location >= 0 ? location : email_default [record - 1]);
	set_entry_text (editor, GTK_ENTRY (email_entry), address ? address : "");
}

static void
extract_email_record (EContactEditor *editor, gint record, gchar **address, gint *location)
{
	GtkWidget *location_option_menu;
	GtkWidget *email_entry;
	gchar     *widget_name;

	widget_name = g_strdup_printf ("optionmenu-email-%d", record);
	location_option_menu = glade_xml_get_widget (editor->gui, widget_name);
	g_free (widget_name);

	widget_name = g_strdup_printf ("entry-email-%d", record);
	email_entry = glade_xml_get_widget (editor->gui, widget_name);
	g_free (widget_name);

	*address  = g_strdup (gtk_entry_get_text (GTK_ENTRY (email_entry)));
	*location = gtk_option_menu_get_history (GTK_OPTION_MENU (location_option_menu));
}

static const gchar *
email_index_to_location (gint index)
{
	return common_location [index].name;
}

static const gchar *
im_index_to_location (gint index)
{
	return common_location [index].name;
}

static void
phone_index_to_type (gint index, const gchar **type_1, const gchar **type_2)
{
	*type_1 = phones [index].type_1;
	*type_2 = phones [index].type_2;
}

static gint
get_email_location (EVCardAttribute *attr)
{
	gint i;

	for (i = 0; i < G_N_ELEMENTS (common_location); i++) {
		if (e_vcard_attribute_has_type (attr, common_location [i].name))
			return i;
	}

	return -1;
}

static gint
get_im_location (EVCardAttribute *attr)
{
	gint i;

	for (i = 0; i < G_N_ELEMENTS (common_location); i++) {
		if (e_vcard_attribute_has_type (attr, common_location [i].name))
			return i;
	}

	return -1;
}

static gint
get_phone_type (EVCardAttribute *attr)
{
	gint i;

	for (i = 0; i < G_N_ELEMENTS (phones); i++) {
		if (e_vcard_attribute_has_type (attr, phones [i].type_1) &&
		    (phones [i].type_2 == NULL || e_vcard_attribute_has_type (attr, phones [i].type_2)))
			return i;
	}

	return -1;
}

static EVCardAttributeParam *
get_ui_slot_param (EVCardAttribute *attr)
{
	EVCardAttributeParam *param = NULL;
	GList                *param_list;
	GList                *l;

	param_list = e_vcard_attribute_get_params (attr);

	for (l = param_list; l; l = g_list_next (l)) {
		const gchar *str;

		param = l->data;

		str = e_vcard_attribute_param_get_name (param);
		if (!strcasecmp (str, EVOLUTION_UI_SLOT_PARAM))
			break;

		param = NULL;
	}

	return param;
}

static gint
get_ui_slot (EVCardAttribute *attr)
{
	EVCardAttributeParam *param;
	gint                  slot = -1;

	param = get_ui_slot_param (attr);

	if (param) {
		GList *value_list;

		value_list = e_vcard_attribute_param_get_values (param);
		slot = atoi (value_list->data);
	}

	return slot;
}

static void
set_ui_slot (EVCardAttribute *attr, gint slot)
{
	EVCardAttributeParam *param;
	gchar                *slot_str;

	param = get_ui_slot_param (attr);
	if (!param) {
		param = e_vcard_attribute_param_new (EVOLUTION_UI_SLOT_PARAM);
		e_vcard_attribute_add_param (attr, param);
	}

	e_vcard_attribute_param_remove_values (param);

	slot_str = g_strdup_printf ("%d", slot);
	e_vcard_attribute_param_add_value (param, slot_str);
	g_free (slot_str);
}

static gint
alloc_ui_slot (EContactEditor *editor, const gchar *widget_base, gint preferred_slot, gint num_slots)
{
	gchar       *widget_name;
	GtkWidget   *widget;
	const gchar *entry_contents;
	gint         i;

	/* See if we can get the preferred slot */

	if (preferred_slot >= 1) {
		widget_name = g_strdup_printf ("%s-%d", widget_base, preferred_slot);
		widget = glade_xml_get_widget (editor->gui, widget_name);
		entry_contents = gtk_entry_get_text (GTK_ENTRY (widget));
		g_free (widget_name);

		if (!nonempty (entry_contents))
			return preferred_slot;
	}

	/* Find first empty slot */

	for (i = 1; i <= num_slots; i++) {
		widget_name = g_strdup_printf ("%s-%d", widget_base, i);
		widget = glade_xml_get_widget (editor->gui, widget_name);
		entry_contents = gtk_entry_get_text (GTK_ENTRY (widget));
		g_free (widget_name);

		if (!nonempty (entry_contents))
			return i;
	}

	return -1;
}

static void
free_attr_list (GList *attr_list)
{
	GList *l;

	for (l = attr_list; l; l = g_list_next (l)) {
		EVCardAttribute *attr = l->data;
		e_vcard_attribute_free (attr);
	}

	g_list_free (attr_list);
}

static void
fill_in_email (EContactEditor *editor)
{
	GList *email_attr_list;
	GList *l;
	gint   record_n;

	/* Clear */

	for (record_n = 1; record_n <= EMAIL_SLOTS; record_n++) {
		fill_in_email_record (editor, record_n, NULL, -1);
	}

	/* Fill in */

	email_attr_list = e_contact_get_attributes (editor->contact, E_CONTACT_EMAIL);

	for (record_n = 1, l = email_attr_list; l && record_n <= EMAIL_SLOTS; l = g_list_next (l)) {
		EVCardAttribute *attr = l->data;
		gchar           *email_address;
		gint             slot;

		email_address = e_vcard_attribute_get_value (attr);
		slot = alloc_ui_slot (editor, "entry-email", get_ui_slot (attr), EMAIL_SLOTS);
		if (slot < 1)
			break;

		fill_in_email_record (editor, slot, email_address,
				      get_email_location (attr));

		record_n++;
	}
}

static void
extract_email (EContactEditor *editor)
{
	GList *attr_list = NULL;
	GList *old_attr_list;
	GList *l, *l_next;
	gint   i;

	for (i = 1; i <= EMAIL_SLOTS; i++) {
		gchar *address;
		gint   location;

		extract_email_record (editor, i, &address, &location);

		if (nonempty (address)) {
			EVCardAttribute *attr;
			attr = e_vcard_attribute_new ("", e_contact_vcard_attribute (E_CONTACT_EMAIL));

			if (location >= 0)
				e_vcard_attribute_add_param_with_value (attr,
									e_vcard_attribute_param_new (EVC_TYPE),
									email_index_to_location (location));

			e_vcard_attribute_add_value (attr, address);
			set_ui_slot (attr, i);

			attr_list = g_list_append (attr_list, attr);
		}

		g_free (address);
	}

	/* Splice in the old attributes, minus the EMAIL_SLOTS first */

	old_attr_list = e_contact_get_attributes (editor->contact, E_CONTACT_EMAIL);
	for (l = old_attr_list, i = 1; l && i <= EMAIL_SLOTS; l = l_next, i++) {
		l_next = g_list_next (l);

		e_vcard_attribute_free (l->data);
		g_list_delete_link (l, l);
	}

	old_attr_list = l;
	attr_list = g_list_concat (attr_list, old_attr_list);

	e_contact_set_attributes (editor->contact, E_CONTACT_EMAIL, attr_list);

	free_attr_list (attr_list);
}

/* EContact can get attributes by field ID only, and there is none for TEL, so we need this */
static GList *
get_attributes_named (EVCard *vcard, const gchar *attr_name)
{
	GList *attr_list_in;
	GList *attr_list_out = NULL;
	GList *l;

	attr_list_in = e_vcard_get_attributes (vcard);

	for (l = attr_list_in; l; l = g_list_next (l)) {
		EVCardAttribute *attr = l->data;
		const gchar *name;

		name = e_vcard_attribute_get_name (attr);

		if (!strcasecmp (attr_name, name)) {
			attr_list_out = g_list_append (attr_list_out, e_vcard_attribute_copy (attr));
		}
	}

	return attr_list_out;
}

/* EContact can set attributes by field ID only, and there is none for TEL, so we need this */
static void
set_attributes_named (EVCard *vcard, const gchar *attr_name, GList *attr_list)
{
	GList *l;

	e_vcard_remove_attributes (vcard, NULL, attr_name);

	for (l = attr_list; l; l = g_list_next (l)) {
		EVCardAttribute *attr = l->data;

		e_vcard_add_attribute (vcard, e_vcard_attribute_copy (attr));
	}
}

static void
fill_in_phone_record (EContactEditor *editor, gint record, const gchar *phone, gint phone_type)
{
	GtkWidget *phone_type_option_menu;
	GtkWidget *phone_entry;
	gchar     *widget_name;

	widget_name = g_strdup_printf ("optionmenu-phone-%d", record);
	phone_type_option_menu = glade_xml_get_widget (editor->gui, widget_name);
	g_free (widget_name);

	widget_name = g_strdup_printf ("entry-phone-%d", record);
	phone_entry = glade_xml_get_widget (editor->gui, widget_name);
	g_free (widget_name);

	gtk_option_menu_set_history (GTK_OPTION_MENU (phone_type_option_menu),
				     phone_type >= 0 ? phone_type :
				     phones_default [record - 1]);
	set_entry_text (editor, GTK_ENTRY (phone_entry), phone ? phone : "");
}

static void
extract_phone_record (EContactEditor *editor, gint record, gchar **phone, gint *phone_type)
{
	GtkWidget *phone_type_option_menu;
	GtkWidget *phone_entry;
	gchar     *widget_name;

	widget_name = g_strdup_printf ("optionmenu-phone-%d", record);
	phone_type_option_menu = glade_xml_get_widget (editor->gui, widget_name);
	g_free (widget_name);

	widget_name = g_strdup_printf ("entry-phone-%d", record);
	phone_entry = glade_xml_get_widget (editor->gui, widget_name);
	g_free (widget_name);

	*phone      = g_strdup (gtk_entry_get_text (GTK_ENTRY (phone_entry)));
	*phone_type = gtk_option_menu_get_history (GTK_OPTION_MENU (phone_type_option_menu));
}

static void
fill_in_phone (EContactEditor *editor)
{
	GList *phone_attr_list;
	GList *l;
	gint   record_n;

	/* Clear */

	for (record_n = 1; record_n <= PHONE_SLOTS; record_n++) {
		fill_in_phone_record (editor, record_n, NULL, -1);
	}

	/* Fill in */

	phone_attr_list = get_attributes_named (E_VCARD (editor->contact), "TEL");

	for (record_n = 1, l = phone_attr_list; l && record_n <= PHONE_SLOTS; l = g_list_next (l)) {
		EVCardAttribute *attr = l->data;
		gchar           *phone;
		gint             slot;

		phone = e_vcard_attribute_get_value (attr);
		slot = alloc_ui_slot (editor, "entry-phone", get_ui_slot (attr), PHONE_SLOTS);
		if (slot < 1)
			break;

		fill_in_phone_record (editor, slot, phone,
				      get_phone_type (attr));

		record_n++;
	}
}

static void
extract_phone (EContactEditor *editor)
{
	GList *attr_list = NULL;
	GList *old_attr_list;
	GList *l, *l_next;
	gint   i;

	for (i = 1; i <= PHONE_SLOTS; i++) {
		gchar *phone;
		gint   phone_type;

		extract_phone_record (editor, i, &phone, &phone_type);

		if (nonempty (phone)) {
			EVCardAttribute *attr;

			attr = e_vcard_attribute_new ("", "TEL");

			if (phone_type >= 0) {
				const gchar *type_1;
				const gchar *type_2;

				phone_index_to_type (phone_type, &type_1, &type_2);

				e_vcard_attribute_add_param_with_value (
					attr, e_vcard_attribute_param_new (EVC_TYPE), type_1);

				if (type_2)
					e_vcard_attribute_add_param_with_value (
						attr, e_vcard_attribute_param_new (EVC_TYPE), type_2);

			}

			e_vcard_attribute_add_value (attr, phone);
			set_ui_slot (attr, i);

			attr_list = g_list_append (attr_list, attr);
		}

		g_free (phone);
	}

	/* Splice in the old attributes, minus the PHONE_SLOTS first */

	old_attr_list = get_attributes_named (E_VCARD (editor->contact), "TEL");
	for (l = old_attr_list, i = 1; l && i <= PHONE_SLOTS; l = l_next, i++) {
		l_next = g_list_next (l);

		e_vcard_attribute_free (l->data);
		g_list_delete_link (l, l);
	}

	old_attr_list = l;
	attr_list = g_list_concat (attr_list, old_attr_list);

	set_attributes_named (E_VCARD (editor->contact), "TEL", attr_list);

	free_attr_list (attr_list);
}

static void
init_phone_record_type (EContactEditor *editor, gint record)
{
	GtkWidget *phone_type_option_menu;
	GtkWidget *phone_type_menu;
	GtkWidget *phone_entry;
	gchar     *widget_name;
	gint       i;

	widget_name = g_strdup_printf ("entry-phone-%d", record);
	phone_entry = glade_xml_get_widget (editor->gui, widget_name);
	g_free (widget_name);

	widget_name = g_strdup_printf ("optionmenu-phone-%d", record);
	phone_type_option_menu = glade_xml_get_widget (editor->gui, widget_name);
	g_free (widget_name);

	g_signal_connect (phone_type_option_menu, "changed", G_CALLBACK (widget_changed), editor);
	g_signal_connect (phone_entry, "changed", G_CALLBACK (widget_changed), editor);
	g_signal_connect_swapped (phone_entry, "activate", G_CALLBACK (entry_activated), editor);

	phone_type_menu = gtk_menu_new ();

	for (i = 0; i < G_N_ELEMENTS (phones); i++) {
		GtkWidget *item;

		item = gtk_menu_item_new_with_label (e_contact_pretty_name (phones [i].field_id));
		gtk_menu_shell_append (GTK_MENU_SHELL (phone_type_menu), item);
	}

	gtk_widget_show_all (phone_type_menu);
	gtk_option_menu_set_menu (GTK_OPTION_MENU (phone_type_option_menu), phone_type_menu);
}

static void
init_phone (EContactEditor *editor)
{
	gint i;

	for (i = 1; i <= PHONE_SLOTS; i++)
		init_phone_record_type (editor, i);
}

static void
init_im_record_location (EContactEditor *editor, gint record)
{
	GtkWidget *location_option_menu;
	GtkWidget *location_menu;
	GtkWidget *name_entry;
	gchar     *widget_name;
	gint       i;

	widget_name = g_strdup_printf ("entry-im-name-%d", record);
	name_entry = glade_xml_get_widget (editor->gui, widget_name);
	g_free (widget_name);

	widget_name = g_strdup_printf ("optionmenu-im-location-%d", record);
	location_option_menu = glade_xml_get_widget (editor->gui, widget_name);
	g_free (widget_name);

	g_signal_connect (location_option_menu, "changed", G_CALLBACK (widget_changed), editor);
	g_signal_connect (name_entry, "changed", G_CALLBACK (widget_changed), editor);
	g_signal_connect_swapped (name_entry, "activate", G_CALLBACK (entry_activated), editor);

	location_menu = gtk_menu_new ();

	for (i = 0; i < G_N_ELEMENTS (common_location); i++) {
		GtkWidget *item;

		item = gtk_menu_item_new_with_label (common_location [i].pretty_name);
		gtk_menu_shell_append (GTK_MENU_SHELL (location_menu), item);
	}

	gtk_widget_show_all (location_menu);
	gtk_option_menu_set_menu (GTK_OPTION_MENU (location_option_menu), location_menu);
}

static void
init_im_record_service (EContactEditor *editor, gint record)
{
	GtkWidget *service_option_menu;
	GtkWidget *service_menu;
	gchar     *widget_name;
	gint       i;

	widget_name = g_strdup_printf ("optionmenu-im-service-%d", record);
	service_option_menu = glade_xml_get_widget (editor->gui, widget_name);
	g_free (widget_name);

	g_signal_connect (service_option_menu, "changed", G_CALLBACK (widget_changed), editor);

	service_menu = gtk_menu_new ();

	for (i = 0; i < G_N_ELEMENTS (im_service); i++) {
		GtkWidget *item;

		item = gtk_menu_item_new_with_label (im_service [i].pretty_name);
		gtk_menu_shell_append (GTK_MENU_SHELL (service_menu), item);
	}

	gtk_widget_show_all (service_menu);
	gtk_option_menu_set_menu (GTK_OPTION_MENU (service_option_menu), service_menu);
}

static void
init_im (EContactEditor *editor)
{
	gint i;

	for (i = 1; i <= IM_SLOTS; i++) {
		init_im_record_service  (editor, i);
		init_im_record_location (editor, i);
	}
}

static void
fill_in_im_record (EContactEditor *editor, gint record, gint service, const gchar *name, gint location)
{
	GtkWidget *service_option_menu;
	GtkWidget *location_option_menu;
	GtkWidget *name_entry;
	gchar     *widget_name;

	widget_name = g_strdup_printf ("optionmenu-im-service-%d", record);
	service_option_menu = glade_xml_get_widget (editor->gui, widget_name);
	g_free (widget_name);

	widget_name = g_strdup_printf ("optionmenu-im-location-%d", record);
	location_option_menu = glade_xml_get_widget (editor->gui, widget_name);
	g_free (widget_name);

	widget_name = g_strdup_printf ("entry-im-name-%d", record);
	name_entry = glade_xml_get_widget (editor->gui, widget_name);
	g_free (widget_name);

	gtk_option_menu_set_history (GTK_OPTION_MENU (location_option_menu),
				     location >= 0 ? location : 0);
	gtk_option_menu_set_history (GTK_OPTION_MENU (service_option_menu),
				     service >= 0 ? service : im_service_default [record - 1]);
	set_entry_text (editor, GTK_ENTRY (name_entry), name ? name : "");
}

static void
fill_in_im (EContactEditor *editor)
{
	GList *im_attr_list;
	GList *l;
	gint   record_n;
	gint   i;

	/* Clear */

	for (record_n = 1 ; record_n <= IM_SLOTS; record_n++) {
		fill_in_im_record (editor, record_n, -1, NULL, -1);
	}

	/* Fill in */

	for (record_n = 1, i = 0; i < G_N_ELEMENTS (im_service); i++) {
		im_attr_list = e_contact_get_attributes (editor->contact, im_service [i].field);

		for (l = im_attr_list; l && record_n <= IM_SLOTS; l = g_list_next (l)) {
			EVCardAttribute *attr = l->data;
			gchar           *im_name;
			gint             slot;

			im_name = e_vcard_attribute_get_value (attr);
			slot = alloc_ui_slot (editor, "entry-im-name", get_ui_slot (attr), IM_SLOTS);
			if (slot < 1)
				break;

			fill_in_im_record (editor, slot, i, im_name,
					   get_im_location (attr));

			record_n++;
		}
	}
}

static void
extract_im_record (EContactEditor *editor, gint record, gint *service, gchar **name, gint *location)
{
	GtkWidget *service_option_menu;
	GtkWidget *location_option_menu;
	GtkWidget *name_entry;
	gchar     *widget_name;

	widget_name = g_strdup_printf ("optionmenu-im-service-%d", record);
	service_option_menu = glade_xml_get_widget (editor->gui, widget_name);
	g_free (widget_name);

	widget_name = g_strdup_printf ("optionmenu-im-location-%d", record);
	location_option_menu = glade_xml_get_widget (editor->gui, widget_name);
	g_free (widget_name);

	widget_name = g_strdup_printf ("entry-im-name-%d", record);
	name_entry = glade_xml_get_widget (editor->gui, widget_name);
	g_free (widget_name);

	*name  = g_strdup (gtk_entry_get_text (GTK_ENTRY (name_entry)));
	*service = gtk_option_menu_get_history (GTK_OPTION_MENU (service_option_menu));
	*location = gtk_option_menu_get_history (GTK_OPTION_MENU (location_option_menu));
}

static void
extract_im (EContactEditor *editor)
{
	GList **service_attr_list;
	gint    remaining_slots = IM_SLOTS;
	gint    i;

	service_attr_list = g_new0 (GList *, G_N_ELEMENTS (im_service));

	for (i = 1; i <= IM_SLOTS; i++) {
		EVCardAttribute *attr;
		gchar           *name;
		gint             service;
		gint             location;

		extract_im_record (editor, i, &service, &name, &location);

		if (nonempty (name)) {
			attr = e_vcard_attribute_new ("", e_contact_vcard_attribute (im_service [service].field));

			if (location >= 0)
				e_vcard_attribute_add_param_with_value (attr,
									e_vcard_attribute_param_new (EVC_TYPE),
									im_index_to_location (location));

			e_vcard_attribute_add_value (attr, name);
			set_ui_slot (attr, i);

			service_attr_list [service] = g_list_append (service_attr_list [service], attr);
		}

		g_free (name);
	}

	for (i = 0; i < G_N_ELEMENTS (im_service); i++) {
		GList *old_service_attr_list;
		gint   filled_in_slots;
		GList *l, *l_next;
		gint   j;

		/* Splice in the old attributes, minus the filled_in_slots first */

		old_service_attr_list = e_contact_get_attributes (editor->contact, im_service [i].field);
		filled_in_slots = MIN (remaining_slots, g_list_length (old_service_attr_list));
		remaining_slots -= filled_in_slots;

		for (l = old_service_attr_list, j = 0; l && j < filled_in_slots; l = l_next, j++) {
			l_next = g_list_next (l);

			e_vcard_attribute_free (l->data);
			g_list_delete_link (l, l);
		}

		old_service_attr_list = l;
		service_attr_list [i] = g_list_concat (service_attr_list [i], old_service_attr_list);

		e_contact_set_attributes (editor->contact, im_service [i].field,
					  service_attr_list [i]);

		free_attr_list (service_attr_list [i]);
	}

	g_free (service_attr_list);
}

static void
init_address_textview (EContactEditor *editor, gint record)
{
	gchar         *textview_name;
	GtkWidget     *textview;
	GtkTextBuffer *text_buffer;

	textview_name = g_strdup_printf ("textview-%s-address", address_name [record]);
	textview = glade_xml_get_widget (editor->gui, textview_name);
	g_free (textview_name);

	text_buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (textview));
	g_signal_connect (text_buffer, "changed", G_CALLBACK (widget_changed), editor);
}

static void
init_address_field (EContactEditor *editor, gint record, const gchar *widget_field_name)
{
	gchar     *entry_name;
	GtkWidget *entry;

	entry_name = g_strdup_printf ("entry-%s-%s", address_name [record], widget_field_name);
	entry = glade_xml_get_widget (editor->gui, entry_name);
	g_free (entry_name);

	g_signal_connect (entry, "changed", G_CALLBACK (widget_changed), editor);
	g_signal_connect_swapped (entry, "activate", G_CALLBACK (entry_activated), editor);
}

static void
init_address_record (EContactEditor *editor, gint record)
{
	init_address_textview (editor, record);
	init_address_field (editor, record, "city");
	init_address_field (editor, record, "state");
	init_address_field (editor, record, "zip");
	init_address_field (editor, record, "country");
}

static void
init_address (EContactEditor *editor)
{
	gint i;

	for (i = 0; i < ADDRESS_SLOTS; i++)
		init_address_record (editor, i);
}

static void
fill_in_address_textview (EContactEditor *editor, gint record, EContactAddress *address)
{
	gchar         *textview_name;
	GtkWidget     *textview;
	GtkTextBuffer *text_buffer;
	GtkTextIter    iter;

	textview_name = g_strdup_printf ("textview-%s-address", address_name [record]);
	textview = glade_xml_get_widget (editor->gui, textview_name);
	g_free (textview_name);

	text_buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (textview));
	gtk_text_buffer_set_text (text_buffer, address->street ? address->street : "", -1);

	gtk_text_buffer_get_end_iter (text_buffer, &iter);
	gtk_text_buffer_insert (text_buffer, &iter, address->ext ? address->ext : "", -1);
}

static void
fill_in_address_field (EContactEditor *editor, gint record, const gchar *widget_field_name,
		       const gchar *string)
{
	gchar     *entry_name;
	GtkWidget *entry;

	entry_name = g_strdup_printf ("entry-%s-%s", address_name [record], widget_field_name);
	entry = glade_xml_get_widget (editor->gui, entry_name);
	g_free (entry_name);

	gtk_entry_set_text (GTK_ENTRY (entry), string);
}

static void
fill_in_address_record (EContactEditor *editor, gint record)
{
	EContactAddress *address;

	address = e_contact_get (editor->contact, addresses [record]);
	if (!address)
		return;

	fill_in_address_textview (editor, record, address);
	fill_in_address_field (editor, record, "city", address->locality);
	fill_in_address_field (editor, record, "state", address->region);
	fill_in_address_field (editor, record, "zip", address->code);
	fill_in_address_field (editor, record, "country", address->country);

	g_boxed_free (e_contact_address_get_type (), address);
}

static void
fill_in_address (EContactEditor *editor)
{
	gint i;

	for (i = 0; i < ADDRESS_SLOTS; i++)
		fill_in_address_record (editor, i);
}

static void
extract_address_textview (EContactEditor *editor, gint record, EContactAddress *address)
{
	gchar         *textview_name;
	GtkWidget     *textview;
	GtkTextBuffer *text_buffer;
	GtkTextIter    iter_1, iter_2;

	textview_name = g_strdup_printf ("textview-%s-address", address_name [record]);
	textview = glade_xml_get_widget (editor->gui, textview_name);
	g_free (textview_name);

	text_buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (textview));
	gtk_text_buffer_get_start_iter (text_buffer, &iter_1);

	/* Skip blank lines */
	while (gtk_text_iter_get_chars_in_line (&iter_1) < 1 &&
	       !gtk_text_iter_is_end (&iter_1))
		gtk_text_iter_forward_line (&iter_1);

	if (gtk_text_iter_is_end (&iter_1))
		return;

	iter_2 = iter_1;
	gtk_text_iter_forward_line (&iter_2);

	/* Extract street (first line of text) */
	address->street = gtk_text_iter_get_text (&iter_1, &iter_2);

	iter_1 = iter_2;
	if (gtk_text_iter_is_end (&iter_1))
		return;

	gtk_text_iter_forward_to_end (&iter_2);

	/* Extract extended address (remaining lines of text) */
	address->ext = gtk_text_iter_get_text (&iter_1, &iter_2);
}

static gchar *
extract_address_field (EContactEditor *editor, gint record, const gchar *widget_field_name)
{
	gchar     *entry_name;
	GtkWidget *entry;

	entry_name = g_strdup_printf ("entry-%s-%s", address_name [record], widget_field_name);
	entry = glade_xml_get_widget (editor->gui, entry_name);
	g_free (entry_name);

	return g_strdup (gtk_entry_get_text (GTK_ENTRY (entry)));
}

static void
extract_address_record (EContactEditor *editor, gint record)
{
	EContactAddress *address;

	address = g_new0 (EContactAddress, 1);

	extract_address_textview (editor, record, address);
	address->locality = extract_address_field (editor, record, "city");
	address->region   = extract_address_field (editor, record, "state");
	address->code     = extract_address_field (editor, record, "zip");
	address->country  = extract_address_field (editor, record, "country");

	if (nonempty (address->street) ||
	    nonempty (address->ext) ||
	    nonempty (address->locality) ||
	    nonempty (address->region) ||
	    nonempty (address->code) ||
	    nonempty (address->country))
		e_contact_set (editor->contact, addresses [record], address);
	else
		e_contact_set (editor->contact, addresses [record], NULL);

	g_boxed_free (e_contact_address_get_type (), address);
}

static void
extract_address (EContactEditor *editor)
{
	gint i;

	for (i = 0; i < ADDRESS_SLOTS; i++)
		extract_address_record (editor, i);
}

static void
wants_html_changed (GtkWidget *widget, EContactEditor *editor)
{
	gboolean wants_html;
	g_object_get (widget,
		      "active", &wants_html,
		      NULL);

	e_contact_set (editor->contact, E_CONTACT_WANTS_HTML, GINT_TO_POINTER (wants_html));

	widget_changed (widget, editor);
}

static void
new_target_cb (EBook *new_book, EBookStatus status, EContactEditor *editor)
{
	editor->load_source_id = 0;
	editor->load_book      = NULL;

	if (status != E_BOOK_ERROR_OK || new_book == NULL) {
		GtkWidget *source_option_menu;

		eab_load_error_dialog (NULL, e_book_get_source (new_book), status);

		source_option_menu = glade_xml_get_widget (editor->gui, "source-option-menu-source");
		e_source_option_menu_select (E_SOURCE_OPTION_MENU (source_option_menu),
					     e_book_get_source (editor->target_book));

		if (new_book)
			g_object_unref (new_book);
		return;
	}

	g_object_set (editor, "target_book", new_book, NULL);
	g_object_unref (new_book);
	widget_changed (NULL, editor);
}

static void
cancel_load (EContactEditor *editor)
{
	if (editor->load_source_id) {
		addressbook_load_source_cancel (editor->load_source_id);
		editor->load_source_id = 0;

		g_object_unref (editor->load_book);
		editor->load_book = NULL;
	}
}

static void
source_selected (GtkWidget *source_option_menu, ESource *source, EContactEditor *editor)
{
	cancel_load (editor);

	if (e_source_equal (e_book_get_source (editor->target_book), source))
		return;

	if (e_source_equal (e_book_get_source (editor->source_book), source)) {
		g_object_set (editor, "target_book", editor->source_book, NULL);
		return;
	}

	editor->load_book = e_book_new ();
	editor->load_source_id = addressbook_load_source (editor->load_book, source,
							  (EBookCallback) new_target_cb, editor);
}

/* This function tells you whether name_to_style will make sense.  */
static gboolean
style_makes_sense(const EContactName *name, char *company, int style)
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
name_to_style(const EContactName *name, char *company, int style)
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
	EContactName *name = editor->name;
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

	e_contact_name_free (editor->name);

	string = gtk_entry_get_text (GTK_ENTRY(widget));

	editor->name = e_contact_name_from_string(string);
	
	file_as_set_style(editor, style);

	widget_changed (widget, editor);
}

static void
file_as_entry_changed (GtkWidget *widget, EContactEditor *editor)
{
	char *string = gtk_editable_get_chars(GTK_EDITABLE (widget), 0, -1);
	char *title;

	if (string && *string)
		title = string;
	else
		title = _("Contact Editor");

	gtk_window_set_title (GTK_WINDOW (editor->app), title);

	g_free (string);

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
image_chooser_changed (GtkWidget *widget, EContactEditor *editor)
{
	editor->image_set = TRUE;

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
widget_changed (GtkWidget *widget, EContactEditor *editor)
{
	if (!editor->target_editable) {
		g_warning ("non-editable contact editor has an editable field in it.");
		return;
	}

	if (!editor->changed) {
		editor->changed = TRUE;
		command_state_changed (editor);
	}
}

static void
set_entry_changed_signal_field (EContactEditor *editor, char *id)
{
	GtkWidget *widget = glade_xml_get_widget(editor->gui, id);

	g_signal_connect (widget, "changed", G_CALLBACK (field_changed), editor);
}

static void
set_urlentry_changed_signal_field (EContactEditor *editor, char *id)
{
	GtkWidget *widget = glade_xml_get_widget (editor->gui, id);
	GtkWidget *entry  = e_url_entry_get_entry (E_URL_ENTRY (widget));

	g_signal_connect (entry, "changed", G_CALLBACK (field_changed), editor);
}

static void
entry_activated (EContactEditor *editor)
{
	save_contact (editor, TRUE);
}

static void
set_entry_activate_signal_field (EContactEditor *editor, char *id)
{
	GtkWidget *widget = glade_xml_get_widget(editor->gui, id);

	g_signal_connect_swapped (widget, "activate", G_CALLBACK (entry_activated), editor);
}

static void
set_urlentry_activate_signal_field (EContactEditor *editor, char *id)
{
	GtkWidget *widget = glade_xml_get_widget (editor->gui, id);
	GtkWidget *entry  = e_url_entry_get_entry (E_URL_ENTRY (widget));

	g_signal_connect_swapped (entry, "activate", G_CALLBACK (entry_activated), editor);
}

/* This sort of sucks. We should really maintain a list of general entry widgets
 * and special case just the ones that don't fit the pattern */

static void
set_entry_activate_signals (EContactEditor *editor)
{
	set_urlentry_activate_signal_field (editor, "entry-homepage");
	set_urlentry_activate_signal_field (editor, "entry-caluri");
	set_urlentry_activate_signal_field (editor, "entry-fburl");
	set_urlentry_activate_signal_field (editor, "entry-videourl");

	set_entry_activate_signal_field (editor, "entry-fullname");
	set_entry_activate_signal_field (editor, "entry-company");
	set_entry_activate_signal_field (editor, "entry-categories");
	set_entry_activate_signal_field (editor, "entry-jobtitle");
	set_entry_activate_signal_field (editor, "entry-manager");
	set_entry_activate_signal_field (editor, "entry-assistant");
	set_entry_activate_signal_field (editor, "entry-department");
	set_entry_activate_signal_field (editor, "entry-profession");
	set_entry_activate_signal_field (editor, "entry-nickname");
}

static void
set_entry_changed_signals (EContactEditor *editor)
{
	GtkWidget *widget;

	widget = glade_xml_get_widget (editor->gui, "entry-fullname");
	g_signal_connect (widget, "changed", G_CALLBACK (name_entry_changed), editor);
	widget = glade_xml_get_widget (editor->gui, "entry-file-as");
	g_signal_connect (widget, "changed", G_CALLBACK (file_as_entry_changed), editor);
	widget = glade_xml_get_widget (editor->gui, "entry-company");
	g_signal_connect (widget, "changed", G_CALLBACK (company_entry_changed), editor);

	set_urlentry_changed_signal_field (editor, "entry-homepage");
	set_urlentry_changed_signal_field (editor, "entry-caluri");
	set_urlentry_changed_signal_field (editor, "entry-fburl");
	set_urlentry_changed_signal_field (editor, "entry-videourl");

	set_entry_changed_signal_field (editor, "entry-categories");
	set_entry_changed_signal_field (editor, "entry-jobtitle");
	set_entry_changed_signal_field (editor, "entry-manager");
	set_entry_changed_signal_field (editor, "entry-assistant");
	set_entry_changed_signal_field (editor, "entry-department");
	set_entry_changed_signal_field (editor, "entry-profession");
	set_entry_changed_signal_field (editor, "entry-nickname");

	widget = glade_xml_get_widget(editor->gui, "text-comments");
	{
		GtkTextBuffer *buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (widget));
		g_signal_connect (buffer, "changed", G_CALLBACK (widget_changed), editor);
	}
	widget = glade_xml_get_widget (editor->gui, "dateedit-birthday");
	g_signal_connect (widget, "changed", G_CALLBACK (widget_changed), editor);
	widget = glade_xml_get_widget (editor->gui, "dateedit-anniversary");
	g_signal_connect (widget, "changed", G_CALLBACK (widget_changed), editor);
	widget = glade_xml_get_widget (editor->gui, "image-chooser");
	g_signal_connect (widget, "changed", G_CALLBACK (image_chooser_changed), editor);
}

static void
full_name_clicked (GtkWidget *button, EContactEditor *editor)
{
	GtkDialog *dialog = GTK_DIALOG(e_contact_editor_fullname_new(editor->name));
	int result;

	g_object_set (dialog,
		      "editable", editor->fullname_editable,
		      NULL);
	gtk_widget_show (GTK_WIDGET(dialog));
	result = gtk_dialog_run (dialog);
	gtk_widget_hide (GTK_WIDGET (dialog));

	if (editor->fullname_editable && result == GTK_RESPONSE_OK) {
		EContactName *name;
		GtkWidget *fname_widget;
		int style = 0;

		g_object_get (dialog,
			      "name", &name,
			      NULL);

		style = file_as_get_style(editor);

		fname_widget = glade_xml_get_widget(editor->gui, "entry-fullname");
		if (fname_widget && GTK_IS_ENTRY(fname_widget)) {
			char *full_name = e_contact_name_to_string(name);
			gtk_entry_set_text(GTK_ENTRY(fname_widget), full_name);
			g_free(full_name);
		}

		e_contact_name_free(editor->name);
		editor->name = name;

		file_as_set_style(editor, style);
	}
	gtk_widget_destroy (GTK_WIDGET (dialog));
}

static void
categories_clicked (GtkWidget *button, EContactEditor *editor)
{
	char *categories = NULL;
	GtkDialog *dialog;
	int result;
	GtkWidget *entry = glade_xml_get_widget(editor->gui, "entry-categories");
	ECategoriesMasterList *ecml;
	if (entry && GTK_IS_ENTRY(entry))
		categories = g_strdup (gtk_entry_get_text(GTK_ENTRY(entry)));
	else if (editor->contact)
		categories = e_contact_get (editor->contact, E_CONTACT_CATEGORIES);

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
			e_contact_set (editor->contact, E_CONTACT_CATEGORIES, categories);

		g_free(categories);
	}
	gtk_widget_destroy(GTK_WIDGET(dialog));
}


typedef struct {
	EContactEditor *ce;
	gboolean should_close;
	gchar *new_id;
} EditorCloseStruct;

static void
contact_moved_cb (EBook *book, EBookStatus status, EditorCloseStruct *ecs)
{
	EContactEditor *ce = ecs->ce;
	gboolean should_close = ecs->should_close;

	gtk_widget_set_sensitive (ce->app, TRUE);
	ce->in_async_call = FALSE;

	e_contact_set (ce->contact, E_CONTACT_UID, ecs->new_id);

	eab_editor_contact_deleted (EAB_EDITOR (ce), status, ce->contact);

	ce->is_new_contact = FALSE;

	if (should_close) {
		eab_editor_close (EAB_EDITOR (ce));
	}
	else {
		ce->changed = FALSE;

		g_object_ref (ce->target_book);
		g_object_unref (ce->source_book);
		ce->source_book = ce->target_book;

		command_state_changed (ce);
	}

	g_object_unref (ce);
	g_free (ecs->new_id);
	g_free (ecs);
}

static void
contact_added_cb (EBook *book, EBookStatus status, const char *id, EditorCloseStruct *ecs)
{
	EContactEditor *ce = ecs->ce;
	gboolean should_close = ecs->should_close;

	if (ce->source_book != ce->target_book && ce->source_editable &&
	    status == E_BOOK_ERROR_OK && ce->is_new_contact == FALSE) {
		ecs->new_id = g_strdup (id);
		e_book_async_remove_contact (ce->source_book, ce->contact,
					     (EBookCallback) contact_moved_cb, ecs);
		return;
	}

	gtk_widget_set_sensitive (ce->app, TRUE);
	ce->in_async_call = FALSE;

	e_contact_set (ce->contact, E_CONTACT_UID, (char *) id);

	eab_editor_contact_added (EAB_EDITOR (ce), status, ce->contact);

	if (status == E_BOOK_ERROR_OK) {
		ce->is_new_contact = FALSE;

		if (should_close) {
			eab_editor_close (EAB_EDITOR (ce));
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
contact_modified_cb (EBook *book, EBookStatus status, EditorCloseStruct *ecs)
{
	EContactEditor *ce = ecs->ce;
	gboolean should_close = ecs->should_close;

	gtk_widget_set_sensitive (ce->app, TRUE);
	ce->in_async_call = FALSE;

	eab_editor_contact_modified (EAB_EDITOR (ce), status, ce->contact);

	if (status == E_BOOK_ERROR_OK) {
		if (should_close) {
			eab_editor_close (EAB_EDITOR (ce));
		}
		else {
			ce->changed = FALSE;
			command_state_changed (ce);
		}
	}

	g_object_unref (ce);
	g_free (ecs);
}

/* Emits the signal to request saving a contact */
static void
save_contact (EContactEditor *ce, gboolean should_close)
{
	EditorCloseStruct *ecs = g_new(EditorCloseStruct, 1);

	extract_info (ce);
	if (!ce->target_book)
		return;

	ecs->ce = ce;
	g_object_ref (ecs->ce);

	ecs->should_close = should_close;

	gtk_widget_set_sensitive (ce->app, FALSE);
	ce->in_async_call = TRUE;

	if (ce->source_book != ce->target_book) {
		/* Two-step move; add to target, then remove from source */
		eab_merging_book_add_contact (ce->target_book, ce->contact,
					      (EBookIdCallback) contact_added_cb, ecs);
	} else {
		if (ce->is_new_contact)
			eab_merging_book_add_contact (ce->target_book, ce->contact,
						      (EBookIdCallback) contact_added_cb, ecs);
		else
			eab_merging_book_commit_contact (ce->target_book, ce->contact,
							 (EBookCallback) contact_modified_cb, ecs);
	}
}

static void
e_contact_editor_save_contact (EABEditor *editor, gboolean should_close)
{
	save_contact (E_CONTACT_EDITOR (editor), should_close);
}

/* Closes the dialog box and emits the appropriate signals */
static void
e_contact_editor_close (EABEditor *editor)
{
	EContactEditor *ce = E_CONTACT_EDITOR (editor);

	if (ce->app != NULL) {
		gtk_widget_destroy (ce->app);
		ce->app = NULL;
		eab_editor_closed (editor);
	}
}

static gboolean
e_contact_editor_is_valid (EABEditor *editor)
{
	/* insert checks here (date format, for instance, etc.) */
	return TRUE;
}

static gboolean
e_contact_editor_is_changed (EABEditor *editor)
{
	return E_CONTACT_EDITOR (editor)->changed;
}

static GtkWindow*
e_contact_editor_get_window (EABEditor *editor)
{
	return GTK_WINDOW (E_CONTACT_EDITOR (editor)->app);
}


static void
file_save_and_close_cb (GtkWidget *widget, gpointer data)
{
	EContactEditor *ce;

	ce = E_CONTACT_EDITOR (data);
	save_contact (ce, TRUE);
}

static void
file_cancel_cb (GtkWidget *widget, gpointer data)
{
	EContactEditor *ce = E_CONTACT_EDITOR (data);

	eab_editor_close (EAB_EDITOR (ce));
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

	if (!eab_editor_prompt_to_save_changes (EAB_EDITOR (ce), GTK_WINDOW (ce->app)))
		return TRUE;

	eab_editor_close (EAB_EDITOR (ce));
	return TRUE;
}

static void
show_help_cb (GtkWidget *widget, gpointer data)
{
	GError *error = NULL;

	gnome_help_display_desktop (NULL,
				    "evolution-" BASE_VERSION,
				    "usage-contact.xml",
				    "usage-contact-cards",
				    &error);
	if (error != NULL)
		g_warning ("%s", error->message);
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
		list = add_to_tab_order(list, gui, "entry-phone-1");
		list = add_to_tab_order(list, gui, "entry-phone-2");
		list = add_to_tab_order(list, gui, "entry-phone-3");
		list = add_to_tab_order(list, gui, "entry-phone-4");

		list = add_to_tab_order(list, gui, "entry-email1");
		list = add_to_tab_order(list, gui, "alignment-htmlmail");
		list = add_to_tab_order(list, gui, "entry-web");
		list = add_to_tab_order(list, gui, "entry-homepage");
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
	GtkWidget *wants_html;
	char *icon_path;

	e_contact_editor->name = e_contact_name_new();
	e_contact_editor->company = g_strdup("");
	
	e_contact_editor->contact = NULL;
	e_contact_editor->changed = FALSE;
	e_contact_editor->image_set = FALSE;
	e_contact_editor->in_async_call = FALSE;
	e_contact_editor->source_editable = TRUE;
	e_contact_editor->target_editable = TRUE;

	e_contact_editor->load_source_id = 0;
	e_contact_editor->load_book = NULL;

	gui = glade_xml_new (EVOLUTION_GLADEDIR "/contact-editor.glade", NULL, NULL);
	e_contact_editor->gui = gui;

	setup_tab_order(gui);

	e_contact_editor->app = glade_xml_get_widget (gui, "contact editor");
	widget = e_contact_editor->app;

	gtk_container_set_border_width (GTK_CONTAINER (GTK_DIALOG (widget)->vbox), 0);
	gtk_container_set_border_width (GTK_CONTAINER (GTK_DIALOG (widget)->action_area), 12);

	set_entry_changed_signals  (e_contact_editor);
	set_entry_activate_signals (e_contact_editor);

	init_email (e_contact_editor);
	init_phone (e_contact_editor);
	init_im (e_contact_editor);
	init_address (e_contact_editor);

	wants_html = glade_xml_get_widget(e_contact_editor->gui, "checkbutton-htmlmail");
	g_signal_connect (wants_html, "toggled", G_CALLBACK (wants_html_changed), e_contact_editor);
	widget = glade_xml_get_widget(e_contact_editor->gui, "button-fullname");
	g_signal_connect (widget, "clicked", G_CALLBACK (full_name_clicked), e_contact_editor);
	widget = glade_xml_get_widget(e_contact_editor->gui, "button-categories");
	g_signal_connect (widget, "clicked", G_CALLBACK (categories_clicked), e_contact_editor);
	widget = glade_xml_get_widget (e_contact_editor->gui, "source-option-menu-source");
	g_signal_connect (widget, "source_selected", G_CALLBACK (source_selected), e_contact_editor);
	widget = glade_xml_get_widget (e_contact_editor->gui, "button-ok");
	g_signal_connect (widget, "clicked", G_CALLBACK (file_save_and_close_cb), e_contact_editor);
	widget = glade_xml_get_widget (e_contact_editor->gui, "button-cancel");
	g_signal_connect (widget, "clicked", G_CALLBACK (file_cancel_cb), e_contact_editor);
	widget = glade_xml_get_widget (e_contact_editor->gui, "button-help");
	g_signal_connect (widget, "clicked", G_CALLBACK (show_help_cb), e_contact_editor);

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
e_contact_editor_dispose (GObject *object)
{
	EContactEditor *e_contact_editor = E_CONTACT_EDITOR(object);

	if (e_contact_editor->writable_fields) {
		g_object_unref(e_contact_editor->writable_fields);
		e_contact_editor->writable_fields = NULL;
	}
	
	if (e_contact_editor->contact) {
		g_object_unref(e_contact_editor->contact);
		e_contact_editor->contact = NULL;
	}
	
	if (e_contact_editor->source_book) {
		g_object_unref(e_contact_editor->source_book);
		e_contact_editor->source_book = NULL;
	}

	if (e_contact_editor->target_book) {
		g_object_unref(e_contact_editor->target_book);
		e_contact_editor->target_book = NULL;
	}

	if (e_contact_editor->name) {
		e_contact_name_free(e_contact_editor->name);
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

	cancel_load (e_contact_editor);

	if (G_OBJECT_CLASS (parent_class)->dispose)
		(* G_OBJECT_CLASS (parent_class)->dispose) (object);
}

static void
command_state_changed (EContactEditor *ce)
{
	GtkWidget *widget;
	gboolean   allow_save;

	allow_save = ce->target_editable && ce->changed ? TRUE : FALSE;

	widget = glade_xml_get_widget (ce->gui, "button-ok");
	gtk_widget_set_sensitive (widget, allow_save);
}

static void
supported_fields_cb (EBook *book, EBookStatus status,
		     EList *fields, EContactEditor *ce)
{
	if (!g_slist_find ((GSList*)eab_editor_get_all_editors (), ce)) {
		g_warning ("supported_fields_cb called for book that's still around, but contact editor that's been destroyed.");
		return;
	}

	g_object_set (ce,
		      "writable_fields", fields,
		      NULL);

	eab_editor_show (EAB_EDITOR (ce));

	command_state_changed (ce);
}

static void
contact_editor_destroy_notify (void *data,
			       GObject *where_the_object_was)
{
	eab_editor_remove (EAB_EDITOR (data));
}

EContactEditor *
e_contact_editor_new (EBook *book,
		      EContact *contact,
		      gboolean is_new_contact,
		      gboolean editable)
{
	EContactEditor *ce;

	g_return_val_if_fail (E_IS_BOOK (book), NULL);
	g_return_val_if_fail (E_IS_CONTACT (contact), NULL);

	ce = g_object_new (E_TYPE_CONTACT_EDITOR, NULL);

	eab_editor_add (EAB_EDITOR (ce));
	g_object_weak_ref (G_OBJECT (ce), contact_editor_destroy_notify, ce);

	g_object_set (ce,
		      "source_book", book,
		      "contact", contact,
		      "is_new_contact", is_new_contact,
		      "editable", editable,
		      NULL);

	if (book)
		e_book_async_get_supported_fields (book, (EBookFieldsCallback)supported_fields_cb, ce);

	return ce;
}

static void
e_contact_editor_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	EContactEditor *editor;

	editor = E_CONTACT_EDITOR (object);
	
	switch (prop_id){
	case PROP_SOURCE_BOOK: {
		gboolean writable;
		gboolean changed = FALSE;

		if (editor->source_book)
			g_object_unref(editor->source_book);
		editor->source_book = E_BOOK (g_value_get_object (value));
		g_object_ref (editor->source_book);

		if (!editor->target_book) {
			editor->target_book = editor->source_book;
			g_object_ref (editor->target_book);

			e_book_async_get_supported_fields (editor->target_book,
							   (EBookFieldsCallback) supported_fields_cb, editor);
		}

		writable = e_book_is_writable (editor->source_book);
		if (writable != editor->source_editable) {
			editor->source_editable = writable;
			changed = TRUE;
		}

		writable = e_book_is_writable (editor->target_book);
		if (writable != editor->target_editable) {
			editor->target_editable = writable;
			changed = TRUE;
		}

		if (changed) {
			set_editable (editor);
			command_state_changed (editor);
		}

		/* XXX more here about editable/etc. */
		break;
	}

	case PROP_TARGET_BOOK: {
		gboolean writable;
		gboolean changed = FALSE;

		if (editor->target_book)
			g_object_unref(editor->target_book);
		editor->target_book = E_BOOK (g_value_get_object (value));
		g_object_ref (editor->target_book);

		e_book_async_get_supported_fields (editor->target_book,
						   (EBookFieldsCallback) supported_fields_cb, editor);

		if (!editor->changed && !editor->is_new_contact) {
			editor->changed = TRUE;
			changed = TRUE;
		}

		writable = e_book_is_writable (editor->target_book);
		if (writable != editor->target_editable) {
			editor->target_editable = writable;
			set_editable (editor);
			changed = TRUE;
		}

		if (changed)
			command_state_changed (editor);

		/* If we're trying to load a new target book, cancel that here. */
		cancel_load (editor);

		/* XXX more here about editable/etc. */
		break;
	}

	case PROP_CONTACT:
		if (editor->contact)
			g_object_unref(editor->contact);
		editor->contact = e_contact_duplicate(E_CONTACT(g_value_get_object (value)));
		fill_in_info(editor);
		editor->changed = FALSE;
		break;

	case PROP_IS_NEW_CONTACT:
		editor->is_new_contact = g_value_get_boolean (value) ? TRUE : FALSE;
		break;

	case PROP_EDITABLE: {
		gboolean new_value = g_value_get_boolean (value) ? TRUE : FALSE;
		gboolean changed = (editor->target_editable != new_value);

		editor->target_editable = new_value;

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
	case PROP_SOURCE_BOOK:
		g_value_set_object (value, e_contact_editor->source_book);
		break;

	case PROP_TARGET_BOOK:
		g_value_set_object (value, e_contact_editor->target_book);
		break;

	case PROP_CONTACT:
		extract_info(e_contact_editor);
		g_value_set_object (value, e_contact_editor->contact);
		break;

	case PROP_IS_NEW_CONTACT:
		g_value_set_boolean (value, e_contact_editor->is_new_contact ? TRUE : FALSE);
		break;

	case PROP_EDITABLE:
		g_value_set_boolean (value, e_contact_editor->target_editable ? TRUE : FALSE);
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
set_entry_text (EContactEditor *editor, GtkEntry *entry, const gchar *string)
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
set_source_field (EContactEditor *editor)
{
	GtkWidget *source_menu;
	ESource   *source;

	if (!editor->target_book)
		return;

	source_menu = glade_xml_get_widget (editor->gui, "source-option-menu-source");
	source = e_book_get_source (editor->target_book);

	e_source_option_menu_select (E_SOURCE_OPTION_MENU (source_menu), source);
}

static void
set_fields(EContactEditor *editor)
{
	fill_in_email (editor);
	fill_in_phone (editor);
	fill_in_im (editor);
	fill_in_address (editor);
	set_source_field (editor);
}

static struct {
	char *id;
	EContactField field;
} field_mapping [] = {
	{ "entry-fullname", E_CONTACT_FULL_NAME },
	{ "entry-homepage", E_CONTACT_HOMEPAGE_URL },
	{ "entry-company", E_CONTACT_ORG },
	{ "entry-department", E_CONTACT_ORG_UNIT },
	{ "entry-jobtitle", E_CONTACT_TITLE },
	{ "entry-profession", E_CONTACT_ROLE },
	{ "entry-manager", E_CONTACT_MANAGER },
	{ "entry-assistant", E_CONTACT_ASSISTANT },
	{ "entry-nickname", E_CONTACT_NICKNAME },
	{ "text-comments", E_CONTACT_NOTE },
	{ "entry-categories", E_CONTACT_CATEGORIES },
	{ "entry-caluri", E_CONTACT_CALENDAR_URI },
	{ "entry-fburl", E_CONTACT_FREEBUSY_URL },
	{ "entry-videourl", E_CONTACT_VIDEO_URL },
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
disable_widget_foreach (char *key, GtkWidget *widget, gpointer closure)
{
	enable_widget (widget, FALSE);
}

static struct {
	char *widget_name;
	EContactField field_id;
	gboolean desensitize_for_read_only;
} widget_field_mappings[] = {
	{ "entry-homepage", E_CONTACT_HOMEPAGE_URL, TRUE },
	{ "accellabel-homepage", E_CONTACT_HOMEPAGE_URL },

	{ "entry-jobtitle", E_CONTACT_TITLE, TRUE },
	{ "label-jobtitle", E_CONTACT_TITLE },

	{ "entry-company", E_CONTACT_ORG, TRUE },
	{ "label-company", E_CONTACT_ORG },

	{ "combo-file-as", E_CONTACT_FILE_AS, TRUE },
	{ "entry-file-as", E_CONTACT_FILE_AS, TRUE },
	{ "accellabel-fileas", E_CONTACT_FILE_AS },

	{ "label-department", E_CONTACT_ORG_UNIT },
	{ "entry-department", E_CONTACT_ORG_UNIT, TRUE },

	{ "label-profession", E_CONTACT_ROLE },
	{ "entry-profession", E_CONTACT_ROLE, TRUE },

	{ "label-manager", E_CONTACT_MANAGER },
	{ "entry-manager", E_CONTACT_MANAGER, TRUE },

	{ "label-assistant", E_CONTACT_ASSISTANT },
	{ "entry-assistant", E_CONTACT_ASSISTANT, TRUE },

	{ "label-nickname", E_CONTACT_NICKNAME },
	{ "entry-nickname", E_CONTACT_NICKNAME, TRUE },

	{ "label-birthday", E_CONTACT_BIRTH_DATE },
	{ "dateedit-birthday", E_CONTACT_BIRTH_DATE, TRUE },

	{ "label-anniversary", E_CONTACT_ANNIVERSARY },
	{ "dateedit-anniversary", E_CONTACT_ANNIVERSARY, TRUE },

	{ "label-comments", E_CONTACT_NOTE },
	{ "text-comments", E_CONTACT_NOTE, TRUE },

	{ "entry-fullname", E_CONTACT_FULL_NAME, TRUE },

	{ "button-categories", E_CONTACT_CATEGORIES, TRUE },
	{ "entry-categories", E_CONTACT_CATEGORIES, TRUE },

	{ "label-caluri", E_CONTACT_CALENDAR_URI },
	{ "entry-caluri", E_CONTACT_CALENDAR_URI, TRUE },

	{ "label-fburl", E_CONTACT_FREEBUSY_URL },
	{ "entry-fburl", E_CONTACT_FREEBUSY_URL, TRUE },

	{ "label-videourl", E_CONTACT_VIDEO_URL },
	{ "entry-videourl", E_CONTACT_VIDEO_URL, TRUE }
};
static int num_widget_field_mappings = sizeof(widget_field_mappings) / sizeof (widget_field_mappings[0]);

static void
enable_writable_fields(EContactEditor *editor)
{
	EList *fields = editor->writable_fields;
	EIterator *iter;
	GHashTable *dropdown_hash, *supported_hash;
	int i;

	if (!fields)
		return;

	dropdown_hash = g_hash_table_new (g_str_hash, g_str_equal);
	supported_hash = g_hash_table_new (g_str_hash, g_str_equal);

	/* then disable them all */
	g_hash_table_foreach (dropdown_hash, (GHFunc)disable_widget_foreach, NULL);

	enable_widget (glade_xml_get_widget (editor->gui, "checkbutton-htmlmail"), FALSE);

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

		for (i = 0; i < G_N_ELEMENTS (addresses); i ++) {
			if (!strcmp (field, e_contact_field_name (addresses[i]))) {
				editor->address_editable [i] = TRUE;
			}
		}
	}

	enable_widget (glade_xml_get_widget (editor->gui, "checkbutton-htmlmail"), editor->target_editable);

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
		field = e_contact_field_name (widget_field_mappings[i].field_id);

		enabled = (g_hash_table_lookup (supported_hash, field) != NULL);

		if (widget_field_mappings[i].desensitize_for_read_only && !editor->target_editable) {
			enabled = FALSE;
		}

		enable_widget (w, enabled);
	}

	editor->fullname_editable = (g_hash_table_lookup (supported_hash, "full_name") != NULL);

	g_hash_table_destroy (dropdown_hash);
	g_hash_table_destroy (supported_hash);
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
			enable_widget (widget, editor->target_editable);
		}
	}

	/* handle the phone dropdown entries */
	for (i = 0; i < 4; i ++) {
		entry = g_strdup_printf ("entry-phone-%d", i+1);

		enable_widget (glade_xml_get_widget(editor->gui, entry),
			       editor->target_editable);

		g_free (entry);
	}

	enable_widget (glade_xml_get_widget(editor->gui, "checkbutton-htmlmail"),
		       editor->target_editable);

	entry = "image-chooser";
	enable_widget (glade_xml_get_widget(editor->gui, entry),
		       editor->target_editable);
}

static void
fill_in_info(EContactEditor *editor)
{
	EContact *contact = editor->contact;
	if (contact) {
		char *file_as;
		EContactName *name;
		EContactDate *anniversary;
		EContactDate *bday;
		EContactPhoto *photo;
		int i;
		GtkWidget *widget;
		gboolean wants_html;

		g_object_get (contact,
			      "file_as",          &file_as,
			      "name",             &name,
			      "anniversary",      &anniversary,
			      "birth_date",       &bday,
			      "wants_html",       &wants_html,
			      "photo",            &photo,
			      NULL);

		for (i = 0; i < sizeof(field_mapping) / sizeof(field_mapping[0]); i++) {
			char *string = e_contact_get (contact, field_mapping[i].field);
			fill_in_field(editor, field_mapping[i].id, string);
			g_free (string);
		}

		widget = glade_xml_get_widget(editor->gui, "checkbutton-htmlmail");
		if (widget && GTK_IS_CHECK_BUTTON(widget)) {
			g_object_set (widget,
				      "active", wants_html,
				      NULL);
		}

		/* File as has to come after company and name or else it'll get messed up when setting them. */
		fill_in_field(editor, "entry-file-as", file_as);

		g_free (file_as);
		if (editor->name)
			e_contact_name_free(editor->name);
		editor->name = name;

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

		if (photo) {
			widget = glade_xml_get_widget(editor->gui, "image-chooser");
			if (widget && E_IS_IMAGE_CHOOSER(widget))
				e_image_chooser_set_image_data (E_IMAGE_CHOOSER (widget), photo->data, photo->length);
		}

		e_contact_date_free (anniversary);
		e_contact_date_free (bday);
		e_contact_photo_free (photo);

		set_fields(editor);
	}
}

static void
extract_field(EContactEditor *editor, EContact *contact, char *editable_id, EContactField field)
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
		e_contact_set (contact, field, string);
	else
		e_contact_set (contact, field, NULL);

	if (string) g_free(string);
}

static void
extract_info(EContactEditor *editor)
{
	EContact *contact = editor->contact;
	if (contact) {
		EContactDate anniversary;
		EContactDate bday;
		int i;
		GtkWidget *widget;

		widget = glade_xml_get_widget(editor->gui, "entry-file-as");
		if (widget && GTK_IS_EDITABLE(widget)) {
			GtkEditable *editable = GTK_EDITABLE(widget);
			char *string = gtk_editable_get_chars(editable, 0, -1);

			if (string && *string)
				e_contact_set (contact, E_CONTACT_FILE_AS, string);

			g_free(string);
		}

		for (i = 0; i < sizeof(field_mapping) / sizeof(field_mapping[0]); i++) {
			extract_field(editor, contact, field_mapping[i].id, field_mapping[i].field);
		}

		if (editor->name)
			e_contact_set (contact, E_CONTACT_NAME, editor->name);

		widget = glade_xml_get_widget(editor->gui, "dateedit-anniversary");
		if (widget && E_IS_DATE_EDIT(widget)) {
			if (e_date_edit_get_date (E_DATE_EDIT (widget),
						  &anniversary.year,
						  &anniversary.month,
						  &anniversary.day)) {
				/* g_print ("%d %d %d\n", anniversary.year, anniversary.month, anniversary.day); */
				e_contact_set (contact, E_CONTACT_ANNIVERSARY, &anniversary);
			} else
				e_contact_set (contact, E_CONTACT_ANNIVERSARY, NULL);
		}

		widget = glade_xml_get_widget(editor->gui, "dateedit-birthday");
		if (widget && E_IS_DATE_EDIT(widget)) {
			if (e_date_edit_get_date (E_DATE_EDIT (widget),
						  &bday.year,
						  &bday.month,
						  &bday.day)) {
				/* g_print ("%d %d %d\n", bday.year, bday.month, bday.day); */
				e_contact_set (contact, E_CONTACT_BIRTH_DATE, &bday);
			} else
				e_contact_set (contact, E_CONTACT_BIRTH_DATE, NULL);
		}

		widget = glade_xml_get_widget (editor->gui, "image-chooser");
		if (widget && E_IS_IMAGE_CHOOSER (widget)) {
			char *image_data;
			gsize image_data_len;

			if (editor->image_set
			    && e_image_chooser_get_image_data (E_IMAGE_CHOOSER (widget),
							       &image_data,
							       &image_data_len)) {
				EContactPhoto photo;

				photo.data = image_data;
				photo.length = image_data_len;

				e_contact_set (contact, E_CONTACT_PHOTO, &photo);
				g_free (image_data);
			}
			else {
				e_contact_set (contact, E_CONTACT_PHOTO, NULL);
			}
		}

		extract_email (editor);
		extract_phone (editor);
		extract_im (editor);
		extract_address (editor);
	}
}

/**
 * e_contact_editor_raise:
 * @config: The %EContactEditor object.
 *
 * Raises the dialog associated with this %EContactEditor object.
 */
static void
e_contact_editor_raise (EABEditor *editor)
{
	EContactEditor *ce = E_CONTACT_EDITOR (editor);

	if (GTK_WIDGET (ce->app)->window)
		gdk_window_raise (GTK_WIDGET (ce->app)->window);
}

/**
 * e_contact_editor_show:
 * @ce: The %EContactEditor object.
 *
 * Shows the dialog associated with this %EContactEditor object.
 */
static void
e_contact_editor_show (EABEditor *editor)
{
	EContactEditor *ce = E_CONTACT_EDITOR (editor);
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

GtkWidget *
e_contact_editor_create_source_option_menu (gchar *name,
					    gchar *string1, gchar *string2,
					    gint int1, gint int2);

GtkWidget *
e_contact_editor_create_source_option_menu (gchar *name,
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
	else if (E_IS_IMAGE_CHOOSER (widget)) {
		e_image_chooser_set_editable (E_IMAGE_CHOOSER (widget), enabled);
	}
	else
		gtk_widget_set_sensitive (widget, enabled);
}
