/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* itip-model.c
 *
 * Copyright (C) 2001  Ximian, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: JP Rosevear
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib.h>
#include <liboaf/liboaf.h>
#include <bonobo/bonobo-control.h>
#include <bonobo/bonobo-widget.h>
#include <bonobo/bonobo-exception.h>
#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-i18n.h>
#include <libgnome/gnome-util.h>
#include <libgnomevfs/gnome-vfs.h>
#include <gal/e-table/e-table-without.h>
#include <gal/e-table/e-cell-text.h>
#include <gal/e-table/e-cell-popup.h>
#include <gal/e-table/e-cell-combo.h>
#include <gal/util/e-unicode-i18n.h>
#include <e-book.h>
#include <e-card-types.h>
#include <e-card-cursor.h>
#include <e-card.h>
#include <e-card-simple.h>
#include <e-destination.h>
#include <cal-util/cal-component.h>
#include <cal-util/cal-util.h>
#include <cal-util/timeutil.h>
#include "Evolution-Addressbook-SelectNames.h"
#include "calendar-config.h"
#include "itip-utils.h"
#include "e-meeting-attendee.h"
#include "e-meeting-model.h"

#define SELECT_NAMES_OAFID "OAFIID:GNOME_Evolution_Addressbook_SelectNames"

enum columns {
	ITIP_ADDRESS_COL,
	ITIP_MEMBER_COL,
	ITIP_TYPE_COL,
	ITIP_ROLE_COL,
	ITIP_RSVP_COL,
	ITIP_DELTO_COL,
	ITIP_DELFROM_COL,
	ITIP_STATUS_COL,
	ITIP_CN_COL,
	ITIP_LANGUAGE_COL,
	ITIP_COLUMN_COUNT
};

struct _EMeetingModelPrivate 
{
	GPtrArray *attendees;
	ETableWithout *without;
	
	CalClient *client;
	icaltimezone *zone;
	
	EBook *ebook;
	gboolean book_loaded;
	gboolean book_load_wait;

	GList *refresh_callbacks;
	GList *refresh_data;
	gint refresh_count;
	gboolean refreshing;

	/* For invite others dialogs */
        GNOME_Evolution_Addressbook_SelectNames corba_select_names;
};

#define BUF_SIZE 1024

static char *sections[] = {N_("Chair Persons"), 
			   N_("Required Participants"), 
			   N_("Optional Participants"), 
			   N_("Non-Participants"),
			   NULL};
static icalparameter_role roles[] = {ICAL_ROLE_CHAIR,
				     ICAL_ROLE_REQPARTICIPANT,
				     ICAL_ROLE_OPTPARTICIPANT,
				     ICAL_ROLE_NONPARTICIPANT,
				     ICAL_ROLE_NONE};

typedef struct _EMeetingModelAttendeeRefreshData EMeetingModelAttendeeRefreshData;
struct _EMeetingModelAttendeeRefreshData {
	char buffer[BUF_SIZE];
	GString *string;

	EMeetingAttendee *ia;
};

typedef struct _EMeetingModelRefreshData EMeetingModelRefreshData;
struct _EMeetingModelRefreshData {
	EMeetingModel *im;
	
	EMeetingModelAttendeeRefreshData attendee_data;
};


static void class_init	(EMeetingModelClass	 *klass);
static void init	(EMeetingModel		 *model);
static void destroy	(GtkObject *obj);

static void attendee_changed_cb (EMeetingAttendee *ia, gpointer data);
static void select_names_ok_cb (BonoboListener    *listener,
				char              *event_name,
				CORBA_any         *arg,
				CORBA_Environment *ev,
				gpointer           data);

static void table_destroy_cb (ETableScrolled *etable, gpointer data);

static ETableModelClass *parent_class = NULL;

GtkType
e_meeting_model_get_type (void)
{
	static GtkType type = 0;

	if (type == 0)
	{
		static const GtkTypeInfo info =
		{
			"EMeetingModel",
			sizeof (EMeetingModel),
			sizeof (EMeetingModelClass),
			(GtkClassInitFunc) class_init,
			(GtkObjectInitFunc) init,
			/* reserved_1 */ NULL,
			/* reserved_2 */ NULL,
			(GtkClassInitFunc) NULL,
		};

		type = gtk_type_unique (e_table_model_get_type (), &info);
	}

	return type;
}

static void
book_open_cb (EBook *book, EBookStatus status, gpointer data)
{
	EMeetingModel *im = E_MEETING_MODEL (data);
	EMeetingModelPrivate *priv;
	
	priv = im->priv;

	if (status == E_BOOK_STATUS_SUCCESS)
		priv->book_loaded = TRUE;
	else
		g_warning ("Book not loaded");
	
	if (priv->book_load_wait) {
		priv->book_load_wait = FALSE;
		gtk_main_quit ();
	}
}

static void
start_addressbook_server (EMeetingModel *im)
{
	EMeetingModelPrivate *priv;
	gchar *uri, *path;

	priv = im->priv;
	
	priv->ebook = e_book_new ();

	path = g_concat_dir_and_file (g_get_home_dir (),
				      "evolution/local/Contacts/addressbook.db");
	uri = g_strdup_printf ("file://%s", path);
	g_free (path);

	e_book_load_uri (priv->ebook, uri, book_open_cb, im);

	g_free (uri);
}

static EMeetingAttendee *
find_match (EMeetingModel *im, const char *address, int *pos)
{
	EMeetingModelPrivate *priv;
	EMeetingAttendee *ia;
	const gchar *ia_address;
	int i;
	
	priv = im->priv;
	
	if (address == NULL)
		return NULL;
	
	/* Make sure we can add the new delegatee person */
	for (i = 0; i < priv->attendees->len; i++) {
		ia = g_ptr_array_index (priv->attendees, i);

		ia_address = e_meeting_attendee_get_address (ia);
		if (ia_address != NULL && !g_strcasecmp (itip_strip_mailto (ia_address), itip_strip_mailto (address))) {
			if (pos != NULL)
				*pos = i;
			return ia;
		}
	}

	return NULL;
}

static icalparameter_cutype
text_to_type (const char *type)
{
	if (!g_strcasecmp (type, _("Individual")))
		return ICAL_CUTYPE_INDIVIDUAL;
	else if (!g_strcasecmp (type, _("Group")))
		return ICAL_CUTYPE_GROUP;
	else if (!g_strcasecmp (type, _("Resource")))
		return ICAL_CUTYPE_RESOURCE;
	else if (!g_strcasecmp (type, _("Room")))
		return ICAL_CUTYPE_ROOM;
	else
		return ICAL_CUTYPE_NONE;
}

static char *
type_to_text (icalparameter_cutype type)
{
	switch (type) {
	case ICAL_CUTYPE_INDIVIDUAL:
		return _("Individual");
	case ICAL_CUTYPE_GROUP:
		return _("Group");
	case ICAL_CUTYPE_RESOURCE:
		return _("Resource");
	case ICAL_CUTYPE_ROOM:
		return _("Room");
	default:
		return _("Unknown");
	}

	return NULL;

}

static icalparameter_role
text_to_role (const char *role)
{
	if (!g_strcasecmp (role, _("Chair")))
		return ICAL_ROLE_CHAIR;
	else if (!g_strcasecmp (role, _("Required Participant")))
		return ICAL_ROLE_REQPARTICIPANT;
	else if (!g_strcasecmp (role, _("Optional Participant")))
		return ICAL_ROLE_OPTPARTICIPANT;
	else if (!g_strcasecmp (role, _("Non-Participant")))
		return ICAL_ROLE_NONPARTICIPANT;
	else
		return ICAL_ROLE_NONE;
}

static char *
role_to_text (icalparameter_role role) 
{
	switch (role) {
	case ICAL_ROLE_CHAIR:
		return _("Chair");
	case ICAL_ROLE_REQPARTICIPANT:
		return _("Required Participant");
	case ICAL_ROLE_OPTPARTICIPANT:
		return _("Optional Participant");
	case ICAL_ROLE_NONPARTICIPANT:
		return _("Non-Participant");
	default:
		return _("Unknown");
	}

	return NULL;
}

static gboolean
text_to_boolean (const char *role)
{
	if (!g_strcasecmp (role, _("Yes")))
		return TRUE;
	else
		return FALSE;
}

static char *
boolean_to_text (gboolean b) 
{
	if (b)
		return _("Yes");
	else
		return _("No");
}

static icalparameter_partstat
text_to_partstat (const char *partstat)
{
	if (!g_strcasecmp (partstat, _("Needs Action")))
		return ICAL_PARTSTAT_NEEDSACTION;
	else if (!g_strcasecmp (partstat, _("Accepted")))
		return ICAL_PARTSTAT_ACCEPTED;
	else if (!g_strcasecmp (partstat, _("Declined")))
		return ICAL_PARTSTAT_DECLINED;
	else if (!g_strcasecmp (partstat, _("Tentative")))
		return ICAL_PARTSTAT_TENTATIVE;
	else if (!g_strcasecmp (partstat, _("Delegated")))
		return ICAL_PARTSTAT_DELEGATED;
	else if (!g_strcasecmp (partstat, _("Completed")))
		return ICAL_PARTSTAT_COMPLETED;
	else if (!g_strcasecmp (partstat, _("In Process")))
		return ICAL_PARTSTAT_INPROCESS;
	else
		return ICAL_PARTSTAT_NONE;
}

static char *
partstat_to_text (icalparameter_partstat partstat) 
{
	switch (partstat) {
	case ICAL_PARTSTAT_NEEDSACTION:
		return _("Needs Action");
	case ICAL_PARTSTAT_ACCEPTED:
		return _("Accepted");
	case ICAL_PARTSTAT_DECLINED:
		return _("Declined");
	case ICAL_PARTSTAT_TENTATIVE:
		return _("Tentative");
	case ICAL_PARTSTAT_DELEGATED:
		return _("Delegated");
	case ICAL_PARTSTAT_COMPLETED:
		return _("Completed");
	case ICAL_PARTSTAT_INPROCESS:
		return _("In Process");
	case ICAL_PARTSTAT_NONE:
	default:
		return _("Unknown");
	}

	return NULL;
}

static int
column_count (ETableModel *etm)
{
	return ITIP_COLUMN_COUNT;
}

static int
row_count (ETableModel *etm)
{
	EMeetingModel *im;
	EMeetingModelPrivate *priv;

	im = E_MEETING_MODEL (etm);	
	priv = im->priv;

	return (priv->attendees->len);
}

static void
append_row (ETableModel *etm, ETableModel *source, int row)
{	
	EMeetingModel *im;
	EMeetingModelPrivate *priv;
	EMeetingAttendee *ia;
	char *address;
	
	im = E_MEETING_MODEL (etm);	
	priv = im->priv;
	
	address = (char *) e_table_model_value_at (source, ITIP_ADDRESS_COL, row);
	if (find_match (im, address, NULL) != NULL) {
		return;
	}
	
	ia = E_MEETING_ATTENDEE (e_meeting_attendee_new ());
	
	e_meeting_attendee_set_address (ia, g_strdup_printf ("MAILTO:%s", address));
	e_meeting_attendee_set_member (ia, g_strdup (e_table_model_value_at (source, ITIP_MEMBER_COL, row)));
	e_meeting_attendee_set_cutype (ia, text_to_type (e_table_model_value_at (source, ITIP_TYPE_COL, row)));
	e_meeting_attendee_set_role (ia, text_to_role (e_table_model_value_at (source, ITIP_ROLE_COL, row)));
	e_meeting_attendee_set_rsvp (ia, text_to_boolean (e_table_model_value_at (source, ITIP_RSVP_COL, row)));
	e_meeting_attendee_set_delto (ia, g_strdup (e_table_model_value_at (source, ITIP_DELTO_COL, row)));
	e_meeting_attendee_set_delfrom (ia, g_strdup (e_table_model_value_at (source, ITIP_DELFROM_COL, row)));
	e_meeting_attendee_set_status (ia, text_to_partstat (e_table_model_value_at (source, ITIP_STATUS_COL, row)));
	e_meeting_attendee_set_cn (ia, g_strdup (e_table_model_value_at (source, ITIP_CN_COL, row)));
	e_meeting_attendee_set_language (ia, g_strdup (e_table_model_value_at (source, ITIP_LANGUAGE_COL, row)));

	e_meeting_model_add_attendee (E_MEETING_MODEL (etm), ia);
}

static void *
value_at (ETableModel *etm, int col, int row)
{
	EMeetingModel *im;
	EMeetingModelPrivate *priv;
	EMeetingAttendee *ia;

	im = E_MEETING_MODEL (etm);	
	priv = im->priv;

	ia = g_ptr_array_index (priv->attendees, row);
	
	switch (col) {
	case ITIP_ADDRESS_COL:
		return (void *)itip_strip_mailto (e_meeting_attendee_get_address (ia));
	case ITIP_MEMBER_COL:
		return (void *)e_meeting_attendee_get_member (ia);
	case ITIP_TYPE_COL:
		return type_to_text (e_meeting_attendee_get_cutype (ia));
	case ITIP_ROLE_COL:
		return role_to_text (e_meeting_attendee_get_role (ia));
	case ITIP_RSVP_COL:
		return boolean_to_text (e_meeting_attendee_get_rsvp (ia));
	case ITIP_DELTO_COL:
		return (void *)itip_strip_mailto (e_meeting_attendee_get_delto (ia));
	case ITIP_DELFROM_COL:
		return (void *)itip_strip_mailto (e_meeting_attendee_get_delfrom (ia));
	case ITIP_STATUS_COL:
		return partstat_to_text (e_meeting_attendee_get_status (ia));
	case ITIP_CN_COL:
		return (void *)e_meeting_attendee_get_cn (ia);
	case ITIP_LANGUAGE_COL:
		return (void *)e_meeting_attendee_get_language (ia);
	}
	
	return NULL;
}

static void
set_value_at (ETableModel *etm, int col, int row, const void *val)
{
	EMeetingModel *im;
	EMeetingModelPrivate *priv;
	EMeetingAttendee *ia;

	im = E_MEETING_MODEL (etm);	
	priv = im->priv;

	ia = g_ptr_array_index (priv->attendees, row);
	
	switch (col) {
	case ITIP_ADDRESS_COL:
		e_meeting_attendee_set_address (ia, g_strdup_printf ("MAILTO:%s", (char *) val));
		break;
	case ITIP_MEMBER_COL:
		e_meeting_attendee_set_member (ia, g_strdup (val));
		break;
	case ITIP_TYPE_COL:
		e_meeting_attendee_set_cutype (ia, text_to_type (val));
		break;
	case ITIP_ROLE_COL:
		e_meeting_attendee_set_role (ia, text_to_role (val));
		break;
	case ITIP_RSVP_COL:
		e_meeting_attendee_set_rsvp (ia, text_to_boolean (val));
		break;
	case ITIP_DELTO_COL:
		e_meeting_attendee_set_delto (ia, g_strdup (val));
		break;
	case ITIP_DELFROM_COL:
		e_meeting_attendee_set_delfrom (ia, g_strdup (val));
		break;
	case ITIP_STATUS_COL:
		e_meeting_attendee_set_status (ia, text_to_partstat (val));
		break;
	case ITIP_CN_COL:
		e_meeting_attendee_set_cn (ia, g_strdup (val));
		break;
	case ITIP_LANGUAGE_COL:
		e_meeting_attendee_set_language (ia, g_strdup (val));
		break;
	}
}

static gboolean
is_cell_editable (ETableModel *etm, int col, int row)
{
	switch (col) {
	case ITIP_DELTO_COL:
	case ITIP_DELFROM_COL:
		return FALSE;

	default:
	}

	return TRUE;
}

static void *
duplicate_value (ETableModel *etm, int col, const void *val)
{
	return g_strdup (val);
}

static void
free_value (ETableModel *etm, int col, void *val)
{
	g_free (val);
}

static void *
init_value (ETableModel *etm, int col)
{
	switch (col) {
	case ITIP_ADDRESS_COL:
		return g_strdup ("");
	case ITIP_MEMBER_COL:
		return g_strdup ("");
	case ITIP_TYPE_COL:
		return g_strdup (_("Individual"));
	case ITIP_ROLE_COL:
		return g_strdup (_("Required Participant"));
	case ITIP_RSVP_COL:
		return g_strdup (_("Yes"));
	case ITIP_DELTO_COL:
		return g_strdup ("");
	case ITIP_DELFROM_COL:
		return g_strdup ("");
	case ITIP_STATUS_COL:
		return g_strdup (_("Needs Action"));
	case ITIP_CN_COL:
		return g_strdup ("");
	case ITIP_LANGUAGE_COL:
		return g_strdup ("en");
	}
	
	return g_strdup ("");
}

static gboolean
value_is_empty (ETableModel *etm, int col, const void *val)
{
	
	switch (col) {
	case ITIP_ADDRESS_COL:
	case ITIP_MEMBER_COL:
	case ITIP_DELTO_COL:
	case ITIP_DELFROM_COL:
	case ITIP_CN_COL:
		if (val && !g_strcasecmp (val, ""))
			return TRUE;
		else
			return FALSE;
	default:
	}
	
	return TRUE;
}

static char *
value_to_string (ETableModel *etm, int col, const void *val)
{
	return g_strdup (val);
}

static void *
get_key (ETableModel *source, int row, gpointer data) 
{
	EMeetingModel *im;
	EMeetingModelPrivate *priv;
	char *str;
	
	im = E_MEETING_MODEL (source);
	priv = im->priv;

	str = value_at (source, ITIP_DELTO_COL, row);
	if (str && *str)
		return g_strdup ("delegator");

	return g_strdup ("none");
}

static void *
duplicate_key (const void *key, gpointer data) 
{
	return g_strdup (key);
}

static void
free_gotten_key (void *key, gpointer data)
{
	g_free (key);
}

static void
free_duplicated_key (void *key, gpointer data)
{
	g_free (key);
}

static void
class_init (EMeetingModelClass *klass)
{
	GtkObjectClass *object_class;
	ETableModelClass *etm_class;
	
	object_class = GTK_OBJECT_CLASS (klass);
	etm_class = E_TABLE_MODEL_CLASS (klass);

	parent_class = gtk_type_class (E_TABLE_MODEL_TYPE);

	object_class->destroy = destroy;

	etm_class->column_count = column_count;
	etm_class->row_count = row_count;
	etm_class->value_at = value_at;
	etm_class->set_value_at = set_value_at;
	etm_class->is_cell_editable = is_cell_editable;
	etm_class->append_row = append_row;
	etm_class->duplicate_value = duplicate_value;
	etm_class->free_value = free_value;
	etm_class->initialize_value = init_value;
	etm_class->value_is_empty = value_is_empty;
	etm_class->value_to_string = value_to_string;
}


static void
init (EMeetingModel *im)
{
	EMeetingModelPrivate *priv;

	priv = g_new0 (EMeetingModelPrivate, 1);

	im->priv = priv;

	priv->attendees = g_ptr_array_new ();

	priv->without = E_TABLE_WITHOUT (e_table_without_new (E_TABLE_MODEL (im),
							      g_str_hash,
							      g_str_equal,
							      get_key,
							      duplicate_key,
							      free_gotten_key,
							      free_duplicated_key,
							      NULL));
	e_table_without_hide (priv->without, g_strdup ("delegator"));
	
	priv->client = NULL;
	priv->zone = icaltimezone_get_builtin_timezone (calendar_config_get_timezone ());
	
	priv->ebook = NULL;
	priv->book_loaded = FALSE;
	priv->book_load_wait = FALSE;
	
	priv->refreshing = FALSE;
	
	start_addressbook_server (im);
}

static void
destroy (GtkObject *obj)
{
	EMeetingModel *model = E_MEETING_MODEL (obj);
	EMeetingModelPrivate *priv;
	gint i;
	
	priv = model->priv;

	for (i = 0; i < priv->attendees->len; i++)
		gtk_object_unref (GTK_OBJECT (g_ptr_array_index(priv->attendees, i)));
	g_ptr_array_free (priv->attendees, FALSE);
	
	if (priv->client != NULL)
		gtk_object_unref (GTK_OBJECT (priv->client));

	if (priv->ebook != NULL)
		gtk_object_unref (GTK_OBJECT (priv->ebook));
	
	g_free (priv);
}

GtkObject *
e_meeting_model_new (void)
{
	return gtk_type_new (E_TYPE_MEETING_MODEL);
}


CalClient *
e_meeting_model_get_cal_client (EMeetingModel *im)
{
	EMeetingModelPrivate *priv;
	
	priv = im->priv;

	return priv->client;
}

void
e_meeting_model_set_cal_client (EMeetingModel *im, CalClient *client)
{
	EMeetingModelPrivate *priv;
	
	priv = im->priv;

	if (priv->client != NULL)
		gtk_object_unref (GTK_OBJECT (priv->client));
	
	if (client != NULL)
		gtk_object_ref (GTK_OBJECT (client));
	priv->client = client;
}

icaltimezone *
e_meeting_model_get_zone (EMeetingModel *im)
{
	EMeetingModelPrivate *priv;
	
	g_return_val_if_fail (im != NULL, NULL);
	g_return_val_if_fail (E_IS_MEETING_MODEL (im), NULL);

	priv = im->priv;
	
	return priv->zone;
}

void
e_meeting_model_set_zone (EMeetingModel *im, icaltimezone *zone)
{
	EMeetingModelPrivate *priv;
	
	g_return_if_fail (im != NULL);
	g_return_if_fail (E_IS_MEETING_MODEL (im));

	priv = im->priv;
	
	priv->zone = zone;
}

static ETableScrolled *
build_etable (ETableModel *model, const gchar *spec_file, const gchar *state_file)
{
	GtkWidget *etable;
	ETable *real_table;
	ETableExtras *extras;
	GList *strings;
	ECell *popup_cell, *cell;
	
	extras = e_table_extras_new ();

	/* For type */
	cell = e_cell_text_new (NULL, GTK_JUSTIFY_LEFT);
	popup_cell = e_cell_combo_new ();
	e_cell_popup_set_child (E_CELL_POPUP (popup_cell), cell);
	gtk_object_unref (GTK_OBJECT (cell));
	
	strings = NULL;
	strings = g_list_append (strings, (char*) U_("Individual"));
	strings = g_list_append (strings, (char*) U_("Group"));
	strings = g_list_append (strings, (char*) U_("Resource"));
	strings = g_list_append (strings, (char*) U_("Room"));
	strings = g_list_append (strings, (char*) U_("Unknown"));

	e_cell_combo_set_popdown_strings (E_CELL_COMBO (popup_cell), strings);
	e_table_extras_add_cell (extras, "typeedit", popup_cell);
	
	/* For role */
	cell = e_cell_text_new (NULL, GTK_JUSTIFY_LEFT);
	popup_cell = e_cell_combo_new ();
	e_cell_popup_set_child (E_CELL_POPUP (popup_cell), cell);
	gtk_object_unref (GTK_OBJECT (cell));
	
	strings = NULL;
	strings = g_list_append (strings, (char*) U_("Chair"));
	strings = g_list_append (strings, (char*) U_("Required Participant"));
	strings = g_list_append (strings, (char*) U_("Optional Participant"));
	strings = g_list_append (strings, (char*) U_("Non-Participant"));
	strings = g_list_append (strings, (char*) U_("Unknown"));

	e_cell_combo_set_popdown_strings (E_CELL_COMBO (popup_cell), strings);
	e_table_extras_add_cell (extras, "roleedit", popup_cell);

	/* For rsvp */
	cell = e_cell_text_new (NULL, GTK_JUSTIFY_LEFT);
	popup_cell = e_cell_combo_new ();
	e_cell_popup_set_child (E_CELL_POPUP (popup_cell), cell);
	gtk_object_unref (GTK_OBJECT (cell));

	strings = NULL;
	strings = g_list_append (strings, (char*) U_("Yes"));
	strings = g_list_append (strings, (char*) U_("No"));

	e_cell_combo_set_popdown_strings (E_CELL_COMBO (popup_cell), strings);
	e_table_extras_add_cell (extras, "rsvpedit", popup_cell);

	/* For status */
	cell = e_cell_text_new (NULL, GTK_JUSTIFY_LEFT);
	popup_cell = e_cell_combo_new ();
	e_cell_popup_set_child (E_CELL_POPUP (popup_cell), cell);
	gtk_object_unref (GTK_OBJECT (cell));

	strings = NULL;
	strings = g_list_append (strings, (char*) U_("Needs Action"));
	strings = g_list_append (strings, (char*) U_("Accepted"));
	strings = g_list_append (strings, (char*) U_("Declined"));
	strings = g_list_append (strings, (char*) U_("Tentative"));
	strings = g_list_append (strings, (char*) U_("Delegated"));

	e_cell_combo_set_popdown_strings (E_CELL_COMBO (popup_cell), strings);
	e_table_extras_add_cell (extras, "statusedit", popup_cell);

	etable = e_table_scrolled_new_from_spec_file (model, extras, spec_file, NULL);
	real_table = e_table_scrolled_get_table (E_TABLE_SCROLLED (etable));
	gtk_object_set (GTK_OBJECT (real_table), "uniform_row_height", TRUE, NULL);
	e_table_load_state (real_table, state_file);

#if 0
	gtk_signal_connect (GTK_OBJECT (real_table),
			    "right_click", GTK_SIGNAL_FUNC (right_click_cb), mpage);
#endif

	gtk_signal_connect (GTK_OBJECT (etable), "destroy", 
			    GTK_SIGNAL_FUNC (table_destroy_cb), g_strdup (state_file));

	gtk_object_unref (GTK_OBJECT (extras));
	
	return E_TABLE_SCROLLED (etable);
}

void
e_meeting_model_add_attendee (EMeetingModel *im, EMeetingAttendee *ia)
{
	EMeetingModelPrivate *priv;
	
	priv = im->priv;
	
	gtk_object_ref (GTK_OBJECT (ia));
	g_ptr_array_add (priv->attendees, ia);
	
	gtk_signal_connect (GTK_OBJECT (ia), "changed",
			    GTK_SIGNAL_FUNC (attendee_changed_cb), im);

	e_table_model_row_inserted (E_TABLE_MODEL (im), row_count (E_TABLE_MODEL (im)) - 1);
}

EMeetingAttendee *
e_meeting_model_add_attendee_with_defaults (EMeetingModel *im)
{
	EMeetingAttendee *ia;
	char *str;
	
	ia = E_MEETING_ATTENDEE (e_meeting_attendee_new ());

	e_meeting_attendee_set_address (ia, init_value (E_TABLE_MODEL (im), ITIP_ADDRESS_COL));
	e_meeting_attendee_set_member (ia, init_value (E_TABLE_MODEL (im), ITIP_MEMBER_COL));

	str = init_value (E_TABLE_MODEL (im), ITIP_TYPE_COL);
	e_meeting_attendee_set_cutype (ia, text_to_type (str));
	g_free (str);
	str = init_value (E_TABLE_MODEL (im), ITIP_ROLE_COL);
	e_meeting_attendee_set_role (ia, text_to_role (str));
	g_free (str);	
	str = init_value (E_TABLE_MODEL (im), ITIP_RSVP_COL);
	e_meeting_attendee_set_role (ia, text_to_boolean (str));
	g_free (str);
	
	e_meeting_attendee_set_delto (ia, init_value (E_TABLE_MODEL (im), ITIP_DELTO_COL));
	e_meeting_attendee_set_delfrom (ia, init_value (E_TABLE_MODEL (im), ITIP_DELFROM_COL));

	str = init_value (E_TABLE_MODEL (im), ITIP_STATUS_COL);
	e_meeting_attendee_set_status (ia, text_to_partstat (str));
	g_free (str);

	e_meeting_attendee_set_cn (ia, init_value (E_TABLE_MODEL (im), ITIP_CN_COL));
	e_meeting_attendee_set_language (ia, init_value (E_TABLE_MODEL (im), ITIP_LANGUAGE_COL));

	e_meeting_model_add_attendee (im, ia);

	return ia;
}

void
e_meeting_model_remove_attendee (EMeetingModel *im, EMeetingAttendee *ia)
{
	EMeetingModelPrivate *priv;
	gint i, row = -1;
	
	priv = im->priv;
	
	for (i = 0; i < priv->attendees->len; i++) {
		if (ia == g_ptr_array_index (priv->attendees, i)) {
			row = i;
			break;
		}
	}
	
	if (row != -1) {
		g_ptr_array_remove_index (priv->attendees, row);		
		gtk_object_unref (GTK_OBJECT (ia));

		e_table_model_row_deleted (E_TABLE_MODEL (im), row);
	}
}

void
e_meeting_model_remove_all_attendees (EMeetingModel *im)
{
	EMeetingModelPrivate *priv;
	gint i;
	
	priv = im->priv;
	
	for (i = 0; i < priv->attendees->len; i++) {
		EMeetingAttendee *ia = g_ptr_array_index (priv->attendees, i);
		gtk_object_unref (GTK_OBJECT (ia));
	}

	e_table_model_rows_deleted (E_TABLE_MODEL (im), 0, priv->attendees->len);
	g_ptr_array_set_size (priv->attendees, 0);
}

EMeetingAttendee *
e_meeting_model_find_attendee (EMeetingModel *im, const gchar *address, gint *row)
{
	EMeetingModelPrivate *priv;
	EMeetingAttendee *ia;
	int i;
	
	priv = im->priv;
	
	if (address == NULL)
		return NULL;
	
	for (i = 0; i < priv->attendees->len; i++) {
		const gchar *ia_address;
		
		ia = g_ptr_array_index (priv->attendees, i);
			
		ia_address = e_meeting_attendee_get_address (ia);
		if (ia_address && !g_strcasecmp (itip_strip_mailto (ia_address), itip_strip_mailto (address))) {
			if (row != NULL)
				*row = i;

			return ia;
		}
	}

	return NULL;
}

EMeetingAttendee *
e_meeting_model_find_attendee_at_row (EMeetingModel *im, gint row)
{
	EMeetingModelPrivate *priv;

	g_return_val_if_fail (im != NULL, NULL);
	g_return_val_if_fail (E_IS_MEETING_MODEL (im), NULL);
	g_return_val_if_fail (row >= 0, NULL);

	priv = im->priv;
	g_return_val_if_fail (row < priv->attendees->len, NULL);

	return g_ptr_array_index (priv->attendees, row);
}

gint 
e_meeting_model_count_actual_attendees (EMeetingModel *im)
{
	EMeetingModelPrivate *priv;
	
	priv = im->priv;

	return e_table_model_row_count (E_TABLE_MODEL (priv->without));	
}

const GPtrArray *
e_meeting_model_get_attendees (EMeetingModel *im)
{
	EMeetingModelPrivate *priv;
	
	priv = im->priv;
	
	return priv->attendees;
}

static icaltimezone *
find_zone (icalproperty *ip, icalcomponent *tz_top_level)
{
	icalparameter *param;
	icalcomponent *sub_comp;
	const char *tzid;
	icalcompiter iter;

	if (tz_top_level == NULL)
		return NULL;
	
	param = icalproperty_get_first_parameter (ip, ICAL_TZID_PARAMETER);
	tzid = icalparameter_get_tzid (param);

	iter = icalcomponent_begin_component (tz_top_level, ICAL_VTIMEZONE_COMPONENT);
	while ((sub_comp = icalcompiter_deref (&iter)) != NULL) {
		icalcomponent *clone;
		const char *tz_tzid;
		
		tz_tzid = icalproperty_get_tzid (sub_comp);
		if (!strcmp (tzid, tz_tzid)) {
			icaltimezone *zone;

			zone = icaltimezone_new ();
			clone = icalcomponent_new_clone (sub_comp);
			icaltimezone_set_component (zone, clone);
			
			return zone;
		}
		
		icalcompiter_next (&iter);
	}

	return NULL;
}

static struct icaltimetype
convert_time (struct icaltimetype itt, icaltimezone *from, icaltimezone *to)
{
	if (from == NULL)
		from = icaltimezone_get_utc_timezone ();

 	icaltimezone_convert_time (&itt, from, to);

	return itt;
}

static void
process_free_busy_comp (EMeetingAttendee *ia,
			icalcomponent *fb_comp,
			icaltimezone *zone,
			icalcomponent *tz_top_level)
{
	icalproperty *ip;
	
	ip = icalcomponent_get_first_property (fb_comp, ICAL_DTSTART_PROPERTY);
	if (ip != NULL) {
		struct icaltimetype dtstart;
		icaltimezone *ds_zone = NULL;
		
		dtstart = icalproperty_get_dtstart (ip);
		if (!dtstart.is_utc) {
			ds_zone = find_zone (ip, tz_top_level);
			if (ds_zone != NULL)
				dtstart = convert_time (dtstart, ds_zone, zone);
 		}
			
		e_meeting_attendee_set_start_busy_range (ia,
							 dtstart.year,
							 dtstart.month,
							 dtstart.day,
							 dtstart.hour,
							 dtstart.minute);
	}
	
	ip = icalcomponent_get_first_property (fb_comp, ICAL_DTEND_PROPERTY);
	if (ip != NULL) {
		struct icaltimetype dtend;
		icaltimezone *de_zone = NULL;
		
		dtend = icalproperty_get_dtend (ip);
		if (!dtend.is_utc) {
			de_zone = find_zone (ip, tz_top_level);
			if (de_zone != NULL)
				dtend = convert_time (dtend, de_zone, zone);
		}

		e_meeting_attendee_set_end_busy_range (ia,
						       dtend.year,
						       dtend.month,
						       dtend.day,
						       dtend.hour,
						       dtend.minute);
	}
	
	ip = icalcomponent_get_first_property (fb_comp, ICAL_FREEBUSY_PROPERTY);
	while (ip != NULL) {
		icalparameter *param;
		struct icalperiodtype fb;
		EMeetingFreeBusyType busy_type = E_MEETING_FREE_BUSY_LAST;
		icalparameter_fbtype fbtype = ICAL_FBTYPE_BUSY;
			
		fb = icalproperty_get_freebusy (ip);
		param = icalproperty_get_first_parameter (ip, ICAL_FBTYPE_PARAMETER);
		if (param != NULL)
			fbtype =  icalparameter_get_fbtype (param);
			
		switch (fbtype) {
		case ICAL_FBTYPE_BUSY:
			busy_type = E_MEETING_FREE_BUSY_BUSY;
			break;

		case ICAL_FBTYPE_BUSYUNAVAILABLE:
			busy_type = E_MEETING_FREE_BUSY_OUT_OF_OFFICE;
			break;

		case ICAL_FBTYPE_BUSYTENTATIVE:
			busy_type = E_MEETING_FREE_BUSY_TENTATIVE;
			break;

		default:
		}
			
		if (busy_type != E_MEETING_FREE_BUSY_LAST) {
			fb.start = convert_time (fb.start, NULL, zone);
			fb.end = convert_time (fb.end, NULL, zone);
			e_meeting_attendee_add_busy_period (ia,
							    fb.start.year,
							    fb.start.month,
							    fb.start.day,
							    fb.start.hour,
							    fb.start.minute,
							    fb.end.year,
							    fb.end.month,
							    fb.end.day,
							    fb.end.hour,
							    fb.end.minute,
							    busy_type);
		}
		
		ip = icalcomponent_get_next_property (fb_comp, ICAL_FREEBUSY_PROPERTY);
	}
}

static void
process_callbacks (EMeetingModel *im) 
{
	EMeetingModelPrivate *priv;	
	GList *l, *m;	

	priv = im->priv;

	for (l = priv->refresh_callbacks, m = priv->refresh_data; l != NULL; l = l->next, m = m->next) {
		EMeetingModelRefreshCallback cb = l->data;
			
		cb (m->data);
	}

	g_list_free (priv->refresh_callbacks);
	g_list_free (priv->refresh_data);
	priv->refresh_callbacks = NULL;
	priv->refresh_data = NULL;
	
	priv->refreshing = FALSE;
}

static void
process_free_busy (EMeetingModel *im, EMeetingAttendee *ia, char *text)
{
	EMeetingModelPrivate *priv;	
	icalcomponent *main_comp;
	icalcomponent_kind kind = ICAL_NO_COMPONENT;

	priv = im->priv;

	main_comp = icalparser_parse_string (text);
	if (main_comp == NULL)
		return;

	kind = icalcomponent_isa (main_comp);
	if (kind == ICAL_VCALENDAR_COMPONENT) {	
		icalcompiter iter;
		icalcomponent *tz_top_level, *sub_comp;

		tz_top_level = cal_util_new_top_level ();
		
		iter = icalcomponent_begin_component (main_comp, ICAL_VTIMEZONE_COMPONENT);
		while ((sub_comp = icalcompiter_deref (&iter)) != NULL) {
			icalcomponent *clone;
			
			clone = icalcomponent_new_clone (sub_comp);
			icalcomponent_add_component (tz_top_level, clone);
			
			icalcompiter_next (&iter);
		}

		iter = icalcomponent_begin_component (main_comp, ICAL_VFREEBUSY_COMPONENT);
		while ((sub_comp = icalcompiter_deref (&iter)) != NULL) {
			process_free_busy_comp (ia, sub_comp, priv->zone, tz_top_level);

			icalcompiter_next (&iter);
		}
		icalcomponent_free (tz_top_level);
	} else if (kind == ICAL_VFREEBUSY_COMPONENT) {
		process_free_busy_comp (ia, main_comp, priv->zone, NULL);
	} else {
		return;
	}
	
	icalcomponent_free (main_comp);
}

static void
async_close (GnomeVFSAsyncHandle *handle,
	     GnomeVFSResult result,
	     gpointer data)
{
	EMeetingModelRefreshData *r_data = data;
	EMeetingModelPrivate *priv;
	
	process_free_busy (r_data->im, r_data->attendee_data.ia, r_data->attendee_data.string->str);

	priv = r_data->im->priv;
	
	priv->refresh_count--;
	
	if (priv->refresh_count == 0)
		process_callbacks (r_data->im);
}

static void
async_read (GnomeVFSAsyncHandle *handle,
	    GnomeVFSResult result,
	    gpointer buffer,
	    GnomeVFSFileSize requested,
	    GnomeVFSFileSize read,
	    gpointer data)
{
	EMeetingModelRefreshData *r_data = data;
	GnomeVFSFileSize buf_size = BUF_SIZE - 1;

	if (result != GNOME_VFS_OK) {
		gnome_vfs_async_close (handle, async_close, r_data);
		return;
	}
	
	((char *)buffer)[read] = '\0';
	r_data->attendee_data.string = g_string_append (r_data->attendee_data.string, buffer);
	
	if (read < requested) {
		gnome_vfs_async_close (handle, async_close, r_data);
		return;
	}

	gnome_vfs_async_read (handle, r_data->attendee_data.buffer, buf_size, async_read, r_data);	
}

static void
async_open (GnomeVFSAsyncHandle *handle,
	    GnomeVFSResult result,
	    gpointer data)
{
	EMeetingModelRefreshData *r_data = data;
	GnomeVFSFileSize buf_size = BUF_SIZE - 1;

	gnome_vfs_async_read (handle, r_data->attendee_data.buffer, buf_size, async_read, r_data);
}

static void
cursor_cb (EBook *book, EBookStatus status, ECardCursor *cursor, gpointer data)
{
	EMeetingModel *im = E_MEETING_MODEL (data);
	EMeetingModelPrivate *priv;
	int length, i, j;

	if (status != E_BOOK_STATUS_SUCCESS)
		return;

	priv = im->priv;
	
	length = e_card_cursor_get_length (cursor);
	priv->refresh_count = 0;

	for (i = 0; i < length; i ++) {
		GnomeVFSAsyncHandle *handle;
		ECard *card = e_card_cursor_get_nth (cursor, i);
		EMeetingModelRefreshData *r_data = g_new0 (EMeetingModelRefreshData, 1);
		EMeetingAttendee *ia = NULL;

		if (card->fburl == NULL)
			continue;
		
		for (j = 0; j < priv->attendees->len; j++) {
			ia = g_ptr_array_index (priv->attendees, j);
			if (e_card_email_match_string (card, itip_strip_mailto (e_meeting_attendee_get_address (ia))))
				break;
		}
		if (ia == NULL)
			continue;
		
		r_data->im = im;
		r_data->attendee_data.string = g_string_new (NULL);
		r_data->attendee_data.ia = ia;
		
		priv->refresh_count++;
		
		/* Read in free/busy data from the url */
		gnome_vfs_async_open (&handle, card->fburl, GNOME_VFS_OPEN_READ, async_open, r_data);
	}
}

void
e_meeting_model_refresh_busy_periods (EMeetingModel *im, EMeetingModelRefreshCallback call_back, gpointer data)
{
	EMeetingModelPrivate *priv;
	GPtrArray *not_found;
	GString *string;
	int i;
	
	priv = im->priv;

	priv->refresh_callbacks = g_list_append (priv->refresh_callbacks, call_back);
	priv->refresh_data = g_list_append (priv->refresh_data, data);

	if (priv->refreshing)
		return;
	
	priv->refreshing = TRUE;

	/* To track what we don't find on the server */
	not_found = g_ptr_array_new ();
	g_ptr_array_set_size (not_found, priv->attendees->len);
	for (i = 0; i < priv->attendees->len; i++)
		g_ptr_array_index (not_found, i) = g_ptr_array_index (priv->attendees, i);

	/* Check the server for free busy data */	
	if (priv->client) {
		GList *fb_data, *users = NULL, *l;
		time_t start, end, now = time (NULL);
		
		start = now - 60 * 60 * 24;
		end = time_add_week (now, 6);
		
		for (i = 0; i < priv->attendees->len; i++) {
			EMeetingAttendee *ia = g_ptr_array_index (priv->attendees, i);
			const char *user;
			
			user = itip_strip_mailto (e_meeting_attendee_get_address (ia));
			users = g_list_append (users, g_strdup (user));
		}

		fb_data = cal_client_get_free_busy (priv->client, users, start, end);

		g_list_foreach (users, (GFunc)g_free, NULL);
		g_list_free (users);

		for (l = fb_data; l != NULL; l = l->next) {
			CalComponent *comp = l->data;
			EMeetingAttendee *ia = NULL;
			CalComponentOrganizer org;
			
			/* Process the data for any attendees found */
			cal_component_get_organizer (comp, &org);
			for (i = 0; i < priv->attendees->len; i++) {
				ia = g_ptr_array_index (priv->attendees, i);				
				if (org.value && !strcmp (org.value, e_meeting_attendee_get_address (ia))) {
					g_ptr_array_remove_fast (not_found, ia);
					break;
				}
				ia = NULL;
			}
			
			if (ia != NULL) {
				char *comp_str;
				
				comp_str = cal_component_get_as_string (comp);
				process_free_busy (im, ia, comp_str);
				g_free (comp_str);
			}
		}
	}

	/* Look for fburl's of attendee with no free busy info on server */
	if (!priv->book_loaded) {
		priv->book_load_wait = TRUE;
		gtk_main ();
	}

	string = g_string_new ("(or ");
	for (i = 0; i < not_found->len; i++) {
		EMeetingAttendee *ia = g_ptr_array_index (not_found, i);
		char *query;
		
		if (!e_meeting_attendee_is_set_address (ia))
			continue;
		
		e_meeting_attendee_clear_busy_periods (ia);

		query = g_strdup_printf ("(contains \"email\" \"%s\")", itip_strip_mailto (e_meeting_attendee_get_address (ia)));
		g_string_append (string, query);
		g_free (query);
	}
	g_string_append_c (string, ')');
	
	if (not_found->len > 0) 
		e_book_get_cursor (priv->ebook, string->str, cursor_cb, im);
	else
		process_callbacks (im);

	g_ptr_array_free (not_found, FALSE);
	g_string_free (string, TRUE);
}

ETableScrolled *
e_meeting_model_etable_from_model (EMeetingModel *im, const gchar *spec_file, const gchar *state_file)
{
	EMeetingModelPrivate *priv;
	
	g_return_val_if_fail (im != NULL, NULL);
	g_return_val_if_fail (E_IS_MEETING_MODEL (im), NULL);

	priv = im->priv;
	
	return build_etable (E_TABLE_MODEL (priv->without), spec_file, state_file);
}

int
e_meeting_model_etable_model_to_view_row (EMeetingModel *im, int model_row)
{
	EMeetingModelPrivate *priv;
	
	g_return_val_if_fail (im != NULL, -1);
	g_return_val_if_fail (E_IS_MEETING_MODEL (im), -1);
	
	priv = im->priv;
	
	return e_table_subset_model_to_view_row (priv->without, model_row);
}

int
e_meeting_model_etable_view_to_model_row (EMeetingModel *im, int view_row)
{
	EMeetingModelPrivate *priv;
	
	g_return_val_if_fail (im != NULL, -1);
	g_return_val_if_fail (E_IS_MEETING_MODEL (im), -1);
	
	priv = im->priv;
	
	return e_table_subset_view_to_model_row (priv->without, view_row);
}


static void
add_section (GNOME_Evolution_Addressbook_SelectNames corba_select_names, const char *name)
{
	CORBA_Environment ev;

	CORBA_exception_init (&ev);

	GNOME_Evolution_Addressbook_SelectNames_addSection (corba_select_names,
							    name, 
							    gettext (name),
							    &ev);

	CORBA_exception_free (&ev);
}

static gboolean
get_select_name_dialog (EMeetingModel *im) 
{
	EMeetingModelPrivate *priv;
	CORBA_Environment ev;
	int i;
	
	priv = im->priv;

	if (priv->corba_select_names != CORBA_OBJECT_NIL) {
		Bonobo_Control corba_control;
		GtkWidget *control_widget;
		int i;
		
		CORBA_exception_init (&ev);
		for (i = 0; sections[i] != NULL; i++) {			
			corba_control = GNOME_Evolution_Addressbook_SelectNames_getEntryBySection 
				(priv->corba_select_names, sections[i], &ev);
			if (BONOBO_EX (&ev)) {
				CORBA_exception_free (&ev);
				return FALSE;				
			}
			
			control_widget = bonobo_widget_new_control_from_objref (corba_control, CORBA_OBJECT_NIL);
			
			bonobo_widget_set_property (BONOBO_WIDGET (control_widget), "text", "", NULL);		
		}
		CORBA_exception_free (&ev);

		return TRUE;
	}
	
	CORBA_exception_init (&ev);

	priv->corba_select_names = oaf_activate_from_id (SELECT_NAMES_OAFID, 0, NULL, &ev);

	for (i = 0; sections[i] != NULL; i++)
		add_section (priv->corba_select_names, sections[i]);

	bonobo_event_source_client_add_listener (priv->corba_select_names,
						 select_names_ok_cb,
						 "GNOME/Evolution:ok:dialog",
						 NULL, im);
	
	if (BONOBO_EX (&ev)) {
		CORBA_exception_free (&ev);
		return FALSE;
	}

	CORBA_exception_free (&ev);

	return TRUE;
}

void
e_meeting_model_invite_others_dialog (EMeetingModel *im)
{
	EMeetingModelPrivate *priv;
	CORBA_Environment ev;

	priv = im->priv;
	
	if (!get_select_name_dialog (im))
		return;

	CORBA_exception_init (&ev);

	GNOME_Evolution_Addressbook_SelectNames_activateDialog (
		priv->corba_select_names, _("Required Participants"), &ev);

	CORBA_exception_free (&ev);
}

static void
process_section (EMeetingModel *im, EDestination **destv, icalparameter_role role)
{
	int i;
	
	for (i = 0; destv[i] != NULL; i++) {
		EMeetingAttendee *ia;
		const char *name, *address;
		
		name = e_destination_get_name (destv[i]);		
		address = e_destination_get_email (destv[i]);
		
		if (address == NULL || *address == '\0')
			continue;
		
		if (e_meeting_model_find_attendee (im, address, NULL) == NULL) {
			ia = e_meeting_model_add_attendee_with_defaults (im);

			e_meeting_attendee_set_address (ia, g_strdup_printf ("MAILTO:%s", address));
			e_meeting_attendee_set_role (ia, role);
			e_meeting_attendee_set_cn (ia, g_strdup (name));
		}
	}
}

static void
select_names_ok_cb (BonoboListener    *listener,
		    char              *event_name,
		    CORBA_any         *arg,
		    CORBA_Environment *ev,
		    gpointer           data)
{
	EMeetingModel *im = data;
	EMeetingModelPrivate *priv;
	Bonobo_Control corba_control;
	GtkWidget *control_widget;
	EDestination **destv;
	char *string = NULL;
	int i;
	
	priv = im->priv;

	for (i = 0; sections[i] != NULL; i++) {
		corba_control = GNOME_Evolution_Addressbook_SelectNames_getEntryBySection 
			(priv->corba_select_names, sections[i], ev);
		control_widget = bonobo_widget_new_control_from_objref
			(corba_control, CORBA_OBJECT_NIL);
		
		bonobo_widget_get_property (BONOBO_WIDGET (control_widget), "destinations", &string, NULL);
		destv = e_destination_importv (string);
		if (destv != NULL) {
			process_section (im, destv, roles[i]);
			e_destination_freev (destv);
		}		
	}
}

static void
attendee_changed_cb (EMeetingAttendee *ia, gpointer data) 
{
	EMeetingModel *im = E_MEETING_MODEL (data);
	EMeetingModelPrivate *priv;
	gint row = -1, i;
	
	priv = im->priv;

	for (i = 0; i < priv->attendees->len; i++) {
		if (ia == g_ptr_array_index (priv->attendees, i)) {
			row = 1;
			break;
		}
	}
	
	if (row == -1)
		return;
	
	e_table_model_row_changed (E_TABLE_MODEL (im), row);
}

static void
table_destroy_cb (ETableScrolled *etable, gpointer data)
{
	ETable *real_table;
	char *filename = data;
	
	real_table = e_table_scrolled_get_table (etable);
	e_table_save_state (real_table, filename);

	g_free (data);
}

