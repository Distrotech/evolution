/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* Evolution calendar - Main page of the task editor dialog
 *
 * Copyright (C) 2001 Ximian, Inc.
 *
 * Authors: Federico Mena-Quintero <federico@ximian.com>
 *          Miguel de Icaza <miguel@ximian.com>
 *          Seth Alves <alves@hungry.com>
 *          JP Rosevear <jpr@ximian.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib.h>
#include <gtk/gtkcombo.h>
#include <gtk/gtksignal.h>
#include <gtk/gtktogglebutton.h>
#include <gtk/gtkvbox.h>
#include <gtk/gtkwindow.h>
#include <libgnome/gnome-i18n.h>
#include <glade/glade.h>
#include <libgnomeui/gnome-stock-icons.h>
#include <misc/e-gui-utils.h>
#include <misc/e-dateedit.h>
#include <e-util/e-dialog-utils.h>
#include <e-util/e-dialog-widgets.h>

#include "../calendar-component.h"
#include "../e-meeting-attendee.h"
#include "../e-meeting-store.h"
#include "../e-meeting-list-view.h"
#include "../itip-utils.h"
#include "comp-editor-util.h"
#include "e-delegate-dialog.h"
#include "meeting-page.h"
#include "../e-cal-popup.h"

/* Private part of the MeetingPage structure */
struct _MeetingPagePrivate {
	/* Lists of attendees */
	GPtrArray *deleted_attendees;

	/* To use in case of cancellation */
	ECalComponent *comp;
	
	/* List of identities */
	EAccountList *accounts;
	EMeetingAttendee *ia;
	char *default_address;
	char *user_add;
	
	/* Glade XML data */
	GladeXML *xml;

	/* Widgets from the Glade file */
	GtkWidget *main;
	GtkWidget *list_box;
	GtkWidget *organizer_table;
	GtkWidget *organizer;
	GtkWidget *existing_organizer_table;
	GtkWidget *existing_organizer;
	GtkWidget *existing_organizer_btn;
	GtkWidget *add;
	GtkWidget *remove;
	GtkWidget *invite;
	GtkWidget *att_label;
	GtkWidget *org_label;

	/* ListView stuff */
	EMeetingStore *model;
	EMeetingListView *list_view;
	gint row;
	
	/* For handling who the organizer is */
	gboolean user_org;
	gboolean existing;
	
        gboolean updating;
};



static void meeting_page_finalize (GObject *object);

static GtkWidget *meeting_page_get_widget (CompEditorPage *page);
static void meeting_page_focus_main_widget (CompEditorPage *page);
static gboolean meeting_page_fill_widgets (CompEditorPage *page, ECalComponent *comp);
static gboolean meeting_page_fill_component (CompEditorPage *page, ECalComponent *comp);
static void attendee_added_cb (EMeetingListView *emlv, EMeetingAttendee *attendee, gpointer user_data);
G_DEFINE_TYPE (MeetingPage, meeting_page, TYPE_COMP_EDITOR_PAGE);

/* Class initialization function for the task page */
static void
meeting_page_class_init (MeetingPageClass *class)
{
	CompEditorPageClass *editor_page_class;
	GObjectClass *object_class;

	editor_page_class = (CompEditorPageClass *) class;
	object_class = (GObjectClass *) class;

	editor_page_class->get_widget = meeting_page_get_widget;
	editor_page_class->focus_main_widget = meeting_page_focus_main_widget;
	editor_page_class->fill_widgets = meeting_page_fill_widgets;
	editor_page_class->fill_component = meeting_page_fill_component;
	editor_page_class->set_summary = NULL;
	editor_page_class->set_dates = NULL;

	object_class->finalize = meeting_page_finalize;
}

/* Object initialization function for the task page */
static void
meeting_page_init (MeetingPage *mpage)
{
	MeetingPagePrivate *priv;
	
	priv = g_new0 (MeetingPagePrivate, 1);
	mpage->priv = priv;
	
	priv->deleted_attendees = g_ptr_array_new ();

	priv->comp = NULL;

	priv->accounts = NULL;
	priv->ia = NULL;
	priv->default_address = NULL;
	
	priv->xml = NULL;
	priv->main = NULL;
	priv->invite = NULL;
	
	priv->model = NULL;
	priv->list_view = NULL;
	
	priv->updating = FALSE;
}

static EAccount *
get_current_account (MeetingPage *mpage)
{	
	MeetingPagePrivate *priv;
	EIterator *it;
	const char *str;
	
	priv = mpage->priv;

	str = gtk_entry_get_text (GTK_ENTRY (GTK_COMBO (priv->organizer)->entry));
	if (!str)
		return NULL;
	
	for (it = e_list_get_iterator((EList *)priv->accounts); e_iterator_is_valid(it); e_iterator_next(it)) {
		EAccount *a = (EAccount *)e_iterator_get(it);
		char *full = g_strdup_printf("%s <%s>", a->id->name, a->id->address);

		if (!strcmp (full, str)) {
			g_free (full);
			g_object_unref (it);

			return a;
		}
	
		g_free (full);
	}
	g_object_unref (it);
	
	return NULL;	
}

static void
set_attendees (ECalComponent *comp, const GPtrArray *attendees)
{
	GSList *comp_attendees = NULL, *l;
	int i;
	
	for (i = 0; i < attendees->len; i++) {
		EMeetingAttendee *ia = g_ptr_array_index (attendees, i);
		ECalComponentAttendee *ca;
		
		ca = e_meeting_attendee_as_e_cal_component_attendee (ia);
		
		comp_attendees = g_slist_prepend (comp_attendees, ca);
		
	}
	comp_attendees = g_slist_reverse (comp_attendees);
	
	e_cal_component_set_attendee_list (comp, comp_attendees);
	
	for (l = comp_attendees; l != NULL; l = l->next)
		g_free (l->data);	
	g_slist_free (comp_attendees);
}

static void
cleanup_attendees (GPtrArray *attendees)
{
	int i;
	
	for (i = 0; i < attendees->len; i++)
		g_object_unref (g_ptr_array_index (attendees, i));
}

/* Destroy handler for the task page */
static void
meeting_page_finalize (GObject *object)
{
	MeetingPage *mpage;
	MeetingPagePrivate *priv;
	
	g_return_if_fail (object != NULL);
	g_return_if_fail (IS_MEETING_PAGE (object));

	mpage = MEETING_PAGE (object);
	priv = mpage->priv;

	if (priv->comp != NULL)
		g_object_unref (priv->comp);
	
	cleanup_attendees (priv->deleted_attendees);
	g_ptr_array_free (priv->deleted_attendees, TRUE);

	if (priv->ia != NULL)
		g_object_unref (priv->ia);
	
	g_object_unref (priv->model);
	
	if (priv->main)
		gtk_widget_unref (priv->main);

	if (priv->xml) {
		g_object_unref (priv->xml);
		priv->xml = NULL;
	}

	if (priv->default_address) {
		g_free (priv->default_address);
		priv->default_address = NULL;
	}

	if (priv->user_add) {
		g_free (priv->user_add);
		priv->user_add = NULL;
	}
	g_free (priv);
	mpage->priv = NULL;

	if (G_OBJECT_CLASS (meeting_page_parent_class)->finalize)
		(* G_OBJECT_CLASS (meeting_page_parent_class)->finalize) (object);
}



/* get_widget handler for the task page */
static GtkWidget *
meeting_page_get_widget (CompEditorPage *page)
{
	MeetingPage *mpage;
	MeetingPagePrivate *priv;

	mpage = MEETING_PAGE (page);
	priv = mpage->priv;

	return priv->main;
}

/* focus_main_widget handler for the task page */
static void
meeting_page_focus_main_widget (CompEditorPage *page)
{
	MeetingPage *mpage;
	MeetingPagePrivate *priv;

	mpage = MEETING_PAGE (page);
	priv = mpage->priv;

	gtk_widget_grab_focus (priv->organizer);
}

/* Fills the widgets with default values */
static void
clear_widgets (MeetingPage *mpage)
{
	MeetingPagePrivate *priv;

	priv = mpage->priv;

	if (COMP_EDITOR_PAGE (mpage)->flags & COMP_EDITOR_PAGE_DELEGATE) {
		gtk_label_set_markup_with_mnemonic (priv->att_label, _("<b>Dele_gatees</b>"));
	}

	if (e_cal_get_static_capability (COMP_EDITOR_PAGE (mpage)->client, CAL_STATIC_CAPABILITY_NO_ORGANIZER)) {
		gtk_label_set_markup (GTK_LABEL (priv->org_label), _("<b>From:</b>"));
		gtk_widget_hide (priv->existing_organizer_btn);
	}
				
	gtk_entry_set_text (GTK_ENTRY (GTK_COMBO (priv->organizer)->entry), priv->default_address);

	gtk_label_set_text (GTK_LABEL (priv->existing_organizer), _("None"));

	gtk_widget_show (priv->organizer_table);
	gtk_widget_hide (priv->existing_organizer_table);	

	priv->existing = FALSE;
	priv->user_org = TRUE;
}

static void
sensitize_widgets (MeetingPage *mpage)
{
	gboolean read_only = FALSE;
	MeetingPagePrivate *priv = mpage->priv;
	GError *error = NULL;
	guint32 flags;
	gboolean delegate;
	
	flags = COMP_EDITOR_PAGE (mpage)->flags;
	delegate = flags & COMP_EDITOR_PAGE_DELEGATE;

	if (!e_cal_is_read_only (COMP_EDITOR_PAGE (mpage)->client, &read_only, &error)) {
		if (error->code != E_CALENDAR_STATUS_BUSY)
			read_only = TRUE;
		g_error_free (error);
	}	
	gtk_widget_set_sensitive (priv->organizer, !read_only);
	gtk_widget_set_sensitive (priv->existing_organizer_btn, (!read_only && priv->user_org));
	gtk_widget_set_sensitive (priv->add, (!read_only &&  priv->user_org) || delegate);
	gtk_widget_set_sensitive (priv->remove, (!read_only &&  priv->user_org) || delegate);
	gtk_widget_set_sensitive (priv->invite, (!read_only &&  priv->user_org) || delegate);
	gtk_widget_set_sensitive (GTK_WIDGET (priv->list_view), !read_only);
}

/* fill_widgets handler for the meeting page */
static gboolean
meeting_page_fill_widgets (CompEditorPage *page, ECalComponent *comp)
{
	MeetingPage *mpage;
	MeetingPagePrivate *priv;
	ECalComponentOrganizer organizer;
	
	mpage = MEETING_PAGE (page);
	priv = mpage->priv;

	priv->updating = TRUE;
	
	
	/* Clean out old data */
	if (priv->comp != NULL)
		g_object_unref (priv->comp);
	priv->comp = NULL;
	
	cleanup_attendees (priv->deleted_attendees);
	g_ptr_array_set_size (priv->deleted_attendees, 0);
	
	/* Clean the screen */
	clear_widgets (mpage);

	/* Component for cancellation */
	priv->comp = e_cal_component_clone (comp);

	priv->user_add = itip_get_comp_attendee (comp, COMP_EDITOR_PAGE (mpage)->client);	

	/* If there is an existing organizer show it properly */
	if (e_cal_component_has_organizer (comp)) {
		e_cal_component_get_organizer (comp, &organizer);
		if (organizer.value != NULL) {
			const gchar *strip = itip_strip_mailto (organizer.value);
			gchar *string;

			gtk_widget_hide (priv->organizer_table);
			gtk_widget_show (priv->existing_organizer_table);
			if (itip_organizer_is_user (comp, page->client)) {
				gtk_widget_set_sensitive (priv->invite, TRUE);
				gtk_widget_set_sensitive (priv->add, TRUE);
				gtk_widget_set_sensitive (priv->remove, TRUE);
				if (e_cal_get_static_capability (
					    page->client,
					    CAL_STATIC_CAPABILITY_ORGANIZER_NOT_EMAIL_ADDRESS))
					gtk_widget_hide (priv->existing_organizer_btn);
				priv->user_org = TRUE;
			} else {
				if (e_cal_get_static_capability (
					    page->client,
					    CAL_STATIC_CAPABILITY_ORGANIZER_NOT_EMAIL_ADDRESS))
					gtk_widget_hide (priv->existing_organizer_btn);
				gtk_widget_set_sensitive (priv->invite, FALSE);
				gtk_widget_set_sensitive (priv->add, FALSE);
				gtk_widget_set_sensitive (priv->remove, FALSE);
				priv->user_org = FALSE;
			}
			
			if (e_cal_get_static_capability (COMP_EDITOR_PAGE (mpage)->client, CAL_STATIC_CAPABILITY_NO_ORGANIZER) && (COMP_EDITOR_PAGE (mpage)->flags & COMP_EDITOR_PAGE_DELEGATE))
				string = g_strdup (priv->user_add);
			else if ( organizer.cn != NULL)
				string = g_strdup_printf ("%s <%s>", organizer.cn, strip);
			else
				string = g_strdup (strip);

			gtk_label_set_text (GTK_LABEL (priv->existing_organizer), string);
			g_free (string);

			priv->existing = TRUE;
		}
	} else {
		EAccount *a;
		
		a = get_current_account (mpage);
		if (a != NULL) {
			priv->ia = e_meeting_store_add_attendee_with_defaults (priv->model);
			g_object_ref (priv->ia);

			e_meeting_attendee_set_address (priv->ia, g_strdup_printf ("MAILTO:%s", a->id->address));
			e_meeting_attendee_set_cn (priv->ia, g_strdup (a->id->name));
			if (page->client && e_cal_get_organizer_must_accept (page->client))
				e_meeting_attendee_set_status (priv->ia, ICAL_PARTSTAT_NEEDSACTION);
			else
				e_meeting_attendee_set_status (priv->ia, ICAL_PARTSTAT_ACCEPTED);
		}
	}
	
	priv->updating = FALSE;

	sensitize_widgets (mpage);

	return TRUE;
}

/* fill_component handler for the meeting page */
static gboolean
meeting_page_fill_component (CompEditorPage *page, ECalComponent *comp)
{
	MeetingPage *mpage;
	MeetingPagePrivate *priv;
	ECalComponentOrganizer organizer = {NULL, NULL, NULL, NULL};

	mpage = MEETING_PAGE (page);
	priv = mpage->priv;

	if (!priv->existing) {
		EAccount *a;
		gchar *addr = NULL;

		/* Find the identity for the organizer or sentby field */
		a = get_current_account (mpage);
		
		/* Sanity Check */
		if (a == NULL) {
			e_notice (page, GTK_MESSAGE_ERROR,
				  _("The organizer selected no longer has an account."));
			return FALSE;			
		}
		
		if (a->id->address == NULL || strlen (a->id->address) == 0) {
			e_notice (page, GTK_MESSAGE_ERROR,
				  _("An organizer is required."));
			return FALSE;
		} 

		addr = g_strdup_printf ("MAILTO:%s", a->id->address);
	
		organizer.value = addr;
		organizer.cn = a->id->name;
		e_cal_component_set_organizer (comp, &organizer);

		g_free (addr);
	}

	if (e_meeting_store_count_actual_attendees (priv->model) < 1) {
		e_notice (page, GTK_MESSAGE_ERROR,
			  _("At least one attendee is required."));
		return FALSE;
	}
	

	if (COMP_EDITOR_PAGE (mpage)->flags & COMP_EDITOR_PAGE_DELEGATE ) {
		GSList *attendee_list, *l;
		int i;
		GPtrArray *attendees = e_meeting_store_get_attendees (priv->model);

		e_cal_component_get_attendee_list (priv->comp, &attendee_list);
		
		for (i = 0; i < attendees->len; i++) {
			EMeetingAttendee *ia = g_ptr_array_index (attendees, i);
			ECalComponentAttendee *ca;
		
			/* Remove the duplicate user from the component if present */
			if (e_meeting_attendee_is_set_delto (ia)) {
				for (l = attendee_list; l; l = l->next) {
					ECalComponentAttendee *a = l->data;

					if (g_str_equal (a->value, e_meeting_attendee_get_address (ia))) {
						attendee_list = g_slist_remove (attendee_list, l->data);
						break;
					}
				}
			}
			
			ca = e_meeting_attendee_as_e_cal_component_attendee (ia);

			attendee_list = g_slist_append (attendee_list, ca);
		}
		e_cal_component_set_attendee_list (comp, attendee_list);
		e_cal_component_free_attendee_list (attendee_list);
	} else 
		set_attendees (comp, e_meeting_store_get_attendees (priv->model));

	
	return TRUE;
}



/* Gets the widgets from the XML file and returns if they are all available. */
static gboolean
get_widgets (MeetingPage *mpage)
{
	CompEditorPage *page = COMP_EDITOR_PAGE (mpage);
	MeetingPagePrivate *priv;
	GSList *accel_groups;
	GtkWidget *toplevel;

	priv = mpage->priv;

#define GW(name) glade_xml_get_widget (priv->xml, name)

	priv->main = GW ("meeting-page");
	if (!priv->main)
		return FALSE;

	priv->list_box = GW ("list-box");

	/* Get the GtkAccelGroup from the toplevel window, so we can install
	   it when the notebook page is mapped. */
	toplevel = gtk_widget_get_toplevel (priv->main);
	accel_groups = gtk_accel_groups_from_object (G_OBJECT (toplevel));
	if (accel_groups) {
		page->accel_group = accel_groups->data;
		gtk_accel_group_ref (page->accel_group);
	}

	gtk_widget_ref (priv->main);
	gtk_container_remove (GTK_CONTAINER (priv->main->parent), priv->main);

	/* For making the user the organizer */
	priv->organizer_table = GW ("organizer-table");
	priv->organizer = GW ("organizer");
	gtk_combo_set_value_in_list (GTK_COMBO (priv->organizer), FALSE, FALSE);
	gtk_entry_set_editable (GTK_ENTRY (GTK_COMBO (priv->organizer)->entry), FALSE);

	/* For showing existing organizers */
	priv->existing_organizer_table = GW ("existing-organizer-table");
	priv->existing_organizer = GW ("existing-organizer");
	priv->existing_organizer_btn = GW ("existing-organizer-button");

	/* Buttons */
	priv->add = GW ("add-attendee");
	priv->remove = GW ("remove-attendee");
	priv->invite = GW ("invite");

	/* Attendees Label */
	priv->att_label = GW ("attendees-label");
	priv->org_label = GW ("org-label");

#undef GW

	return (priv->list_box
		&& priv->att_label
		&& priv->invite
		&& priv->add
		&& priv->remove
		&& priv->organizer_table
		&& priv->organizer
		&& priv->existing_organizer_table
		&& priv->existing_organizer
		&& priv->existing_organizer_btn);
}

static void
org_changed_cb (GtkWidget *widget, gpointer data)
{
	MeetingPage *mpage;
	MeetingPagePrivate *priv;
	
	mpage = MEETING_PAGE (data);
	priv = mpage->priv;

	if (priv->updating)
		return;
	
	if (!priv->existing && priv->ia != NULL) {
		EAccount *a;
		
		a = get_current_account (mpage);
		if (a != NULL) {
			e_meeting_attendee_set_address (priv->ia, g_strdup_printf ("MAILTO:%s", a->id->address));
			e_meeting_attendee_set_cn (priv->ia, g_strdup (a->id->name));
			
			if (!e_meeting_store_find_attendee (priv->model, e_meeting_attendee_get_address (priv->ia), NULL))
				e_meeting_store_add_attendee (priv->model, priv->ia);
		} else {
			e_meeting_store_remove_attendee (priv->model, priv->ia);
		}
	}
		
	comp_editor_page_notify_changed (COMP_EDITOR_PAGE (mpage));
}

/* Function called to change the organizer */
static void
change_clicked_cb (GtkWidget *widget, gpointer data) 
{
	MeetingPage *mpage;
	MeetingPagePrivate *priv;
	
	mpage = MEETING_PAGE (data);
	priv = mpage->priv;

	gtk_widget_show (priv->organizer_table);
	gtk_widget_hide (priv->existing_organizer_table);
	gtk_widget_set_sensitive (priv->invite, TRUE);
	gtk_widget_set_sensitive (priv->add, TRUE);
	gtk_widget_set_sensitive (priv->remove, TRUE);

	comp_editor_page_notify_changed (COMP_EDITOR_PAGE (mpage));
	
	priv->existing = FALSE;
	priv->user_org = TRUE;
}

static void
add_clicked_cb (GtkButton *btn, MeetingPage *mpage)
{
	EMeetingAttendee *attendee;
	
	attendee = e_meeting_store_add_attendee_with_defaults (mpage->priv->model);

	if (COMP_EDITOR_PAGE (mpage)->flags & COMP_EDITOR_PAGE_DELEGATE) {
		e_meeting_attendee_set_delfrom (attendee, g_strdup_printf ("MAILTO:%s", mpage->priv->user_add));
	}

	e_meeting_list_view_edit (mpage->priv->list_view, attendee);
}

static gboolean
existing_attendee (EMeetingAttendee *ia, ECalComponent *comp) 
{
	GSList *attendees, *l;
	const gchar *ia_address;
	
	ia_address = itip_strip_mailto (e_meeting_attendee_get_address (ia));
	if (!ia_address)
		return FALSE;
	
	e_cal_component_get_attendee_list (comp, &attendees);

	for (l = attendees; l; l = l->next) {
		ECalComponentAttendee *attendee = l->data;
		const char *address;
		
		address = itip_strip_mailto (attendee->value);
		if (address && !g_strcasecmp (ia_address, address)) {
			e_cal_component_free_attendee_list (attendees);
			return TRUE;
		}
	}
	
	e_cal_component_free_attendee_list (attendees);
	
	return FALSE;
}

static void
remove_attendee (MeetingPage *mpage, EMeetingAttendee *ia) 
{
	MeetingPagePrivate *priv;
	int pos = 0;
	gboolean delegate = (COMP_EDITOR_PAGE (mpage)->flags & COMP_EDITOR_PAGE_DELEGATE);
	
	priv = mpage->priv;

	/* If the user deletes the organizer attendee explicitly,
	   assume they no longer want the organizer showing up */
	if (ia == priv->ia) {
		g_object_unref (priv->ia);
		priv->ia = NULL;
	}	
		
	/* If this was a delegatee, no longer delegate */
	if (e_meeting_attendee_is_set_delfrom (ia)) {
		EMeetingAttendee *ib;
		
		ib = e_meeting_store_find_attendee (priv->model, e_meeting_attendee_get_delfrom (ia), &pos);
		if (ib != NULL) {
			e_meeting_attendee_set_delto (ib, NULL);
			
			if (!delegate) 
				e_meeting_attendee_set_edit_level (ib,  E_MEETING_ATTENDEE_EDIT_FULL);
		}		
	}
	
	/* Handle deleting all attendees in the delegation chain */	
	while (ia != NULL) {
		EMeetingAttendee *ib = NULL;

		if (existing_attendee (ia, priv->comp)) {
			g_object_ref (ia);
			g_ptr_array_add (priv->deleted_attendees, ia);
		}
		
		if (e_meeting_attendee_get_delto (ia) != NULL)
			ib = e_meeting_store_find_attendee (priv->model, e_meeting_attendee_get_delto (ia), NULL);
		e_meeting_store_remove_attendee (priv->model, ia);

		ia = ib;
	}
	
	sensitize_widgets (mpage);
}

static void
remove_attendee_at_row (MeetingPage *mpage, int row) 
{
	MeetingPagePrivate *priv;
	EMeetingAttendee *ia;
	
	priv = mpage->priv;

	ia = e_meeting_store_find_attendee_at_row (priv->model, row);
	remove_attendee (mpage, ia);
}

static void
remove_clicked_cb (GtkButton *btn, MeetingPage *mpage)
{
	MeetingPagePrivate *priv;
	EMeetingAttendee *ia;
	GtkTreeSelection *selection;
	GtkTreeIter iter;
	GtkTreePath *path;
	gboolean valid_iter;
	char *address;
	
	priv = mpage->priv;

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (priv->list_view));
	if (!gtk_tree_selection_get_selected (selection, NULL, &iter)) {
		g_warning ("Could not get a selection to delete.");
		return;
	}
	path = gtk_tree_model_get_path (GTK_TREE_MODEL (priv->model), &iter);	
	
	gtk_tree_model_get (GTK_TREE_MODEL (priv->model), &iter, E_MEETING_STORE_ADDRESS_COL, &address, -1);
	ia = e_meeting_store_find_attendee (priv->model, address, NULL);
	g_free (address);
	if (!ia)
		return;
	else if (e_meeting_attendee_get_edit_level (ia) != E_MEETING_ATTENDEE_EDIT_FULL)
		return;
	
	remove_attendee (mpage, ia);

	/* Select closest item after removal */
	valid_iter = gtk_tree_model_get_iter (GTK_TREE_MODEL (priv->model), &iter, path);
	if (!valid_iter) {
		gtk_tree_path_prev (path);
		valid_iter = gtk_tree_model_get_iter (GTK_TREE_MODEL (priv->model), &iter, path);
	}

	if (valid_iter)
		gtk_tree_selection_select_iter (selection, &iter);

	gtk_tree_path_free (path);
}

/* Function called to invite more people */
static void
invite_cb (GtkWidget *widget, gpointer data) 
{
	MeetingPage *mpage;
	MeetingPagePrivate *priv;
	
	mpage = MEETING_PAGE (data);
	priv = mpage->priv;

	e_meeting_list_view_invite_others_dialog (priv->list_view);
}

/* Hooks the widget signals */
static void
init_widgets (MeetingPage *mpage)
{
	MeetingPagePrivate *priv;

	priv = mpage->priv;

	/* Organizer */
	g_signal_connect (GTK_COMBO (priv->organizer)->entry, "changed",
			  G_CALLBACK (org_changed_cb), mpage);

	g_signal_connect (priv->existing_organizer_btn, "clicked",
			  G_CALLBACK (change_clicked_cb), mpage);

	/* Add attendee button */
	g_signal_connect (priv->add, "clicked", G_CALLBACK (add_clicked_cb), mpage);

	/* Remove attendee button */
	g_signal_connect (priv->remove, "clicked", G_CALLBACK (remove_clicked_cb), mpage);

	/* Contacts button */
	g_signal_connect(priv->invite, "clicked", G_CALLBACK (invite_cb), mpage);

	/* Meeting List View */
	g_signal_connect (priv->list_view, "attendee_added", G_CALLBACK (attendee_added_cb), mpage);
}

static void
attendee_added_cb (EMeetingListView *emlv, EMeetingAttendee *ia, gpointer user_data)
{
   MeetingPage *mpage = MEETING_PAGE (user_data);	
   MeetingPagePrivate *priv;
   gboolean delegate = (COMP_EDITOR_PAGE (mpage)->flags & COMP_EDITOR_PAGE_DELEGATE);

   priv = mpage->priv;

   if (delegate) {
	   if (existing_attendee (ia, priv->comp))
		   e_meeting_store_remove_attendee (priv->model, ia);
	   else {
		   if (!e_cal_get_static_capability (COMP_EDITOR_PAGE(mpage)->client, 
					   CAL_STATIC_CAPABILITY_DELEGATE_TO_MANY)) {
			   const char *delegator_id = e_meeting_attendee_get_delfrom (ia);
			   EMeetingAttendee *delegator;

			   delegator = e_meeting_store_find_attendee (priv->model, delegator_id, NULL);
			   e_meeting_attendee_set_delto (delegator, 
					   g_strdup (e_meeting_attendee_get_address (ia)));

			   gtk_widget_set_sensitive (priv->invite, FALSE);
			   gtk_widget_set_sensitive (priv->add, FALSE);
		   }
	   }
   }

}

static void
client_changed_cb (CompEditorPage *page, ECal *client, gpointer user_data)
{
	MeetingPage *mpage = MEETING_PAGE (page);

	sensitize_widgets (mpage);
}

static void
popup_delete_cb (EPopup *ep, EPopupItem *pitem, void *data)
{
	MeetingPage *mpage = data;
	MeetingPagePrivate *priv;
	
	priv = mpage->priv;

	remove_attendee_at_row (mpage, priv->row);
}

enum {
	CAN_DELEGATE = 2,
	CAN_DELETE = 4
};

static EPopupItem context_menu_items[] = {
#if 0
	{ E_POPUP_ITEM, "00.delegate", N_("_Delegate To..."), popup_delegate_cb, NULL, NULL, CAN_DELEGATE },
	{ E_POPUP_BAR, "05.bar" },
#endif
	{ E_POPUP_ITEM, "10.delete", N_("_Remove"), popup_delete_cb, NULL, GTK_STOCK_REMOVE, CAN_DELETE },
};

static void
context_popup_free(EPopup *ep, GSList *items, void *data)
{
	g_slist_free(items);
}

static gint
button_press_event (GtkWidget *widget, GdkEventButton *event, MeetingPage *mpage)
{
	MeetingPagePrivate *priv;
	GtkMenu *menu;
	EMeetingAttendee *ia;
	GtkTreePath *path;
	GtkTreeIter iter;
	char *address;
	int disable_mask = 0;
	GSList *menus = NULL;
	ECalPopup *ep;
	int i;

	priv = mpage->priv;

	/* only process right-clicks */
	if (event->button != 3 || event->type != GDK_BUTTON_PRESS)
		return FALSE;

	/* only if we right-click on an attendee */
	if (!gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW (priv->list_view), event->x, event->y, &path, NULL, NULL, NULL))
		return FALSE;
	if (!gtk_tree_model_get_iter (GTK_TREE_MODEL (priv->model), &iter, path))
		return FALSE;
	
	gtk_tree_model_get (GTK_TREE_MODEL (priv->model), &iter, E_MEETING_STORE_ADDRESS_COL, &address, -1);
	ia = e_meeting_store_find_attendee (priv->model, address, &priv->row);
	g_free (address);
	if (!ia)
		return FALSE;
	
 	if (e_meeting_attendee_get_edit_level (ia) != E_MEETING_ATTENDEE_EDIT_FULL)
 		disable_mask = CAN_DELETE;

	ep = e_cal_popup_new("org.gnome.evolution.calendar.meeting.popup");
	for (i=0;i<sizeof(context_menu_items)/sizeof(context_menu_items[0]);i++)
		menus = g_slist_prepend(menus, &context_menu_items[i]);

	e_popup_add_items((EPopup *)ep, menus, NULL, context_popup_free, mpage);
	menu = e_popup_create_menu_once((EPopup *)ep, NULL, disable_mask);
	gtk_menu_popup (menu, NULL, NULL, NULL, NULL, event->button, event->time);

	return TRUE;
}

/**
 * meeting_page_construct:
 * @mpage: An task details page.
 * 
 * Constructs an task page by loading its Glade data.
 * 
 * Return value: The same object as @mpage, or NULL if the widgets could not 
 * be created.
 **/
MeetingPage *
meeting_page_construct (MeetingPage *mpage, EMeetingStore *ems,
			ECal *client)
{
	MeetingPagePrivate *priv;
	char *backend_address;
	EIterator *it;
	EAccount *def_account;
	GList *address_strings = NULL, *l;
	GtkWidget *sw;
	EAccount *a;
	
	priv = mpage->priv;

	priv->xml = glade_xml_new (EVOLUTION_GLADEDIR 
				   "/meeting-page.glade", NULL, NULL);
	if (!priv->xml) {
		g_message (G_STRLOC ": Could not load the Glade XML file!");
		return NULL;
	}

	if (!get_widgets (mpage)) {
		g_message (G_STRLOC ": Could not find all widgets in the XML file!");
		return NULL;
	}

	/* Address information */
	if (!e_cal_get_cal_address (client, &backend_address, NULL))
		return NULL;

	priv->accounts = itip_addresses_get ();
	def_account = itip_addresses_get_default();
	for (it = e_list_get_iterator((EList *)priv->accounts);
	     e_iterator_is_valid(it);
	     e_iterator_next(it)) {
		a = (EAccount *)e_iterator_get(it);
		char *full;
		
		full = g_strdup_printf("%s <%s>", a->id->name, a->id->address);

		address_strings = g_list_append(address_strings, full);

		/* Note that the address specified by the backend gets
		 * precedence over the default mail address.
		 */
		if (backend_address && !strcmp (backend_address, a->id->address)) {
			if (priv->default_address)
				g_free (priv->default_address);
			
			priv->default_address = g_strdup (full);
		} else if (a == def_account && !priv->default_address) {
			priv->default_address = g_strdup (full);
		}
	}
	
	g_object_unref(it);

	if (address_strings)
		gtk_combo_set_popdown_strings (GTK_COMBO (priv->organizer), address_strings);
	else
		g_warning ("No potential organizers!");

	for (l = address_strings; l != NULL; l = l->next)
		g_free (l->data);
	g_list_free (address_strings);
	
	/* The etable displaying attendees and their status */
	g_object_ref (ems);
	priv->model = ems;

	priv->list_view = e_meeting_list_view_new (priv->model); 
	g_signal_connect (G_OBJECT (priv->list_view), "button_press_event", G_CALLBACK (button_press_event), mpage);

	gtk_widget_show (GTK_WIDGET (priv->list_view));
	sw = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (sw), GTK_SHADOW_IN);
	gtk_widget_show (sw);
	gtk_container_add (GTK_CONTAINER (sw), GTK_WIDGET (priv->list_view));
	gtk_box_pack_start (GTK_BOX (priv->list_box), sw, TRUE, TRUE, 0);
	
	/* Set the mnemonic widget for the Attendees label */
	gtk_label_set_mnemonic_widget (GTK_LABEL (priv->att_label), GTK_WIDGET (priv->list_view));

	/* Init the widget signals */
	init_widgets (mpage);

	g_signal_connect_after (G_OBJECT (mpage), "client_changed",
				G_CALLBACK (client_changed_cb), NULL);

	return mpage;
}

/**
 * meeting_page_new:
 * 
 * Creates a new task details page.
 * 
 * Return value: A newly-created task details page, or NULL if the page could
 * not be created.
 **/
MeetingPage *
meeting_page_new (EMeetingStore *ems, ECal *client)
{
	MeetingPage *mpage;

	mpage = g_object_new (TYPE_MEETING_PAGE, NULL);
	if (!meeting_page_construct (mpage, ems, client)) {
		g_object_unref (mpage);
		return NULL;
	}

	return mpage;
}

/**
 * meeting_page_get_cancel_comp:
 * @mpage: 
 * 
 * 
 * 
 * Return value: 
 **/
ECalComponent *
meeting_page_get_cancel_comp (MeetingPage *mpage)
{
	MeetingPagePrivate *priv;

	g_return_val_if_fail (mpage != NULL, NULL);
	g_return_val_if_fail (IS_MEETING_PAGE (mpage), NULL);

	priv = mpage->priv;

	if (priv->deleted_attendees->len == 0)
		return NULL;
	
	set_attendees (priv->comp, priv->deleted_attendees);
	
	return e_cal_component_clone (priv->comp);
}
