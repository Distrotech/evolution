/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
 *
 *  Copyright 2002 Ximian, Inc. (www.ximian.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Street #330, Boston, MA 02111-1307, USA.
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glade/glade.h>

#include <libgnomeui/gnome-window-icon.h>
#include <libgnomeui/gnome-pixmap.h>

#include <gal/widgets/e-unicode.h>

#include "message-tag-followup.h"
#include "mail-config.h"

static void message_tag_followup_class_init (MessageTagFollowUpClass *class);
static void message_tag_followup_init (MessageTagFollowUp *followup);
static void message_tag_followup_finalise (GObject *obj);

static CamelTag *get_tag_list (MessageTagEditor *editor);
static void set_tag_list (MessageTagEditor *editor, CamelTag *tags);


#define DEFAULT_FLAG  2  /* Follow-Up */
static char *available_flags[] = {
	N_("Call"),
	N_("Do Not Forward"),
	N_("Follow-Up"),
	N_("For Your Information"),
	N_("Forward"),
	N_("No Response Necessary"),
	N_("Read"),
	N_("Reply"),
	N_("Reply to All"),
	N_("Review"),
};

static int num_available_flags = sizeof (available_flags) / sizeof (available_flags[0]);


static MessageTagEditorClass *parent_class = NULL;


GType
message_tag_followup_get_type (void)
{
	static GType type = 0;
	
	if (!type) {
		GTypeInfo type_info = {
			sizeof (MessageTagFollowUpClass),
			NULL, NULL,
			(GClassInitFunc) message_tag_followup_class_init,
			NULL, NULL,
			sizeof (MessageTagFollowUp),
			0,
			(GInstanceInitFunc) message_tag_followup_init,
		};
		
		type = g_type_register_static (message_tag_editor_get_type (), "MessageTagFollowUp", &type_info, 0);
	}
	
	return type;
}

static void
message_tag_followup_class_init (MessageTagFollowUpClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	MessageTagEditorClass *editor_class = (MessageTagEditorClass *) klass;
	
	parent_class = g_type_class_ref (message_tag_editor_get_type ());
	
	object_class->finalize = message_tag_followup_finalise;
	
	editor_class->get_tag_list = get_tag_list;
	editor_class->set_tag_list = set_tag_list;
}

static void
message_tag_followup_init (MessageTagFollowUp *editor)
{
	editor->combo = NULL;
	editor->target_date = NULL;
	editor->completed = NULL;
	editor->clear = NULL;
	editor->completed_date = 0;
}


static void
message_tag_followup_finalise (GObject *obj)
{
	MessageTagFollowUp *editor = (MessageTagFollowUp *) obj;
	
	editor->completed_date = 0;
	
        G_OBJECT_CLASS (parent_class)->finalize (obj);
}


static CamelTag *
get_tag_list (MessageTagEditor *editor)
{
	MessageTagFollowUp *followup = (MessageTagFollowUp *) editor;
	CamelTag *tags = NULL;
	time_t date;
	char *text;
	
	camel_tag_set (&tags, "follow-up", gtk_entry_get_text (GTK_ENTRY (followup->combo->entry)));
	
	date = e_date_edit_get_time (followup->target_date);
	if (date != (time_t) -1) {
		text = header_format_date (date, 0);
		camel_tag_set (&tags, "due-by", text);
		g_free (text);
	} else {
		camel_tag_set (&tags, "due-by", "");
	}
	
	if (gtk_toggle_button_get_active (followup->completed)) {
		text = header_format_date (followup->completed_date, 0);
		camel_tag_set (&tags, "completed-on", text);
		g_free (text);
	} else {
		camel_tag_set (&tags, "completed-on", "");
	}
	
	return tags;
}

static void
set_tag_list (MessageTagEditor *editor, CamelTag *tags)
{
	MessageTagFollowUp *followup = (MessageTagFollowUp *) editor;
	const char *text;
	time_t date;
	
	text = camel_tag_get (&tags, "follow-up");
	if (text)
		gtk_entry_set_text (GTK_ENTRY (followup->combo->entry), text);
	
	text = camel_tag_get (&tags, "due-by");
	if (text && *text) {
		date = header_decode_date (text, NULL);
		e_date_edit_set_time (followup->target_date, date);
	} else {
		e_date_edit_set_time (followup->target_date, (time_t) -1);
	}
	
	text = camel_tag_get (&tags, "completed-on");
	if (text && *text) {
		date = header_decode_date (text, NULL);
		if (date != (time_t) 0) {
			gtk_toggle_button_set_active (followup->completed, TRUE);
			followup->completed_date = date;
		}
	}
}

static void
clear_clicked (GtkButton *button, gpointer user_data)
{
	MessageTagFollowUp *followup = user_data;
	
	gtk_list_select_item (GTK_LIST (followup->combo->list), DEFAULT_FLAG);
	
	e_date_edit_set_time (followup->target_date, (time_t) -1);
	gtk_toggle_button_set_active (followup->completed, FALSE);
}

static void
completed_toggled (GtkToggleButton *button, gpointer user_data)
{
	MessageTagFollowUp *followup = user_data;
	
	if (gtk_toggle_button_get_active (followup->completed))
		followup->completed_date = time (NULL);
	else
		followup->completed_date = 0;
}

GtkWidget *target_date_new (const char *s1, const char *s2, int i1, int i2);

GtkWidget *
target_date_new (const char *s1, const char *s2, int i1, int i2)
{
	GtkWidget *widget;
	int start;

	widget = e_date_edit_new ();
	e_date_edit_set_show_date (E_DATE_EDIT (widget), TRUE);
	e_date_edit_set_show_time (E_DATE_EDIT (widget), TRUE);
	
	/* Note that this is 0 (Sun) to 6 (Sat), conver to 0 (mon) to 6 (sun) */
	start = (mail_config_get_week_start_day() + 6) % 7;
	
	e_date_edit_set_week_start_day (E_DATE_EDIT (widget), start);
	e_date_edit_set_use_24_hour_format (E_DATE_EDIT (widget), mail_config_get_time_24hour ());
	e_date_edit_set_allow_no_date_set (E_DATE_EDIT (widget), TRUE);
	e_date_edit_set_time_popup_range (E_DATE_EDIT (widget), 0, 24);
	
	return widget;
}

static void
construct (MessageTagEditor *editor)
{
	MessageTagFollowUp *followup = (MessageTagFollowUp *) editor;
	GtkCellRenderer *renderer;
	GtkListStore *model;
	GtkWidget *widget;
	GList *strings;
	GladeXML *gui;
	int i;
	
	gtk_window_set_title (GTK_WINDOW (editor), _("Flag to Follow Up"));
	gnome_window_icon_set_from_file (GTK_WINDOW (editor), EVOLUTION_IMAGES "/flag-for-followup-16.png");
	
	gui = glade_xml_new (EVOLUTION_GLADEDIR "/message-tags.glade", "followup_editor", NULL);
	
	widget = glade_xml_get_widget (gui, "toplevel");
	
	/* reparent */
	gtk_widget_reparent (widget, GTK_DIALOG (editor)->vbox);
	
	widget = glade_xml_get_widget (gui, "pixmap");
	gtk_image_set_from_file ((GtkImage *)widget, EVOLUTION_GLADEDIR "/flag-for-followup-48.png");
	
	followup->message_list = GTK_TREE_VIEW (glade_xml_get_widget (gui, "message_list"));
	model = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_STRING);
	gtk_tree_view_set_model (followup->message_list, (GtkTreeModel *) model);
	
	renderer = gtk_cell_renderer_text_new ();
	gtk_tree_view_insert_column_with_attributes (followup->message_list, -1, _("From"),
						     renderer, "text", 0, NULL);
	
	renderer = gtk_cell_renderer_text_new ();
	gtk_tree_view_insert_column_with_attributes (followup->message_list, -1, _("Subject"),
						     renderer, "text", 1, NULL);
	
	followup->combo = GTK_COMBO (glade_xml_get_widget (gui, "combo"));
	gtk_combo_set_case_sensitive (followup->combo, FALSE);
	strings = NULL;
	for (i = 0; i < num_available_flags; i++)
		strings = g_list_append (strings, (char *) _(available_flags[i]));
	gtk_combo_set_popdown_strings (followup->combo, strings);
	g_list_free (strings);
	gtk_list_select_item (GTK_LIST (followup->combo->list), DEFAULT_FLAG);
	
	followup->target_date = E_DATE_EDIT (glade_xml_get_widget (gui, "target_date"));
	/* glade bug, need to show this ourselves */
	gtk_widget_show ((GtkWidget *) followup->target_date);
	e_date_edit_set_time (followup->target_date, (time_t) -1);
	
	followup->completed = GTK_TOGGLE_BUTTON (glade_xml_get_widget (gui, "completed"));
	g_signal_connect (followup->completed, "toggled", G_CALLBACK (completed_toggled), followup);
	
	followup->clear = GTK_BUTTON (glade_xml_get_widget (gui, "clear"));
	g_signal_connect (followup->clear, "clicked", G_CALLBACK (clear_clicked), followup);
	
	g_object_unref (gui);
}

MessageTagEditor *
message_tag_followup_new (void)
{
	MessageTagEditor *editor;
	
	editor = (MessageTagEditor *) g_object_new (message_tag_followup_get_type (), NULL);
	construct (editor);
	
	return editor;
}

void
message_tag_followup_append_message (MessageTagFollowUp *editor, const char *from, const char *subject)
{
	GtkTreeIter iter;
	GtkListStore *model;
	
	g_return_if_fail (IS_MESSAGE_TAG_FOLLOWUP (editor));
	
	model = (GtkListStore *) gtk_tree_view_get_model (editor->message_list);
	
	gtk_list_store_append (model, &iter);
	gtk_list_store_set (model, &iter, 0, from, 1, subject, -1);
}
