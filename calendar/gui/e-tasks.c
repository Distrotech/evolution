/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-tasks.c
 *
 * Copyright (C) 2001-2003  Ximian, Inc.
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
 * Authors: Federico Mena Quintero <federico@ximian.com>
 *	    Damon Chaplin <damon@ximian.com>
 *          Rodrigo Moya <rodrigo@ximian.com>
 */

#include <config.h>
#include <gnome.h>
#include <libgnomevfs/gnome-vfs-ops.h>
#include <gal/util/e-util.h>
#include <gal/e-table/e-table-scrolled.h>
#include <gal/menus/gal-view-instance.h>
#include <gal/menus/gal-view-factory-etable.h>
#include <gal/menus/gal-view-etable.h>
#include <gtkhtml/gtkhtml.h>
#include <gtkhtml/gtkhtml-stream.h>

#include "e-util/e-categories-config.h"
#include "e-util/e-time-utils.h"
#include "e-util/e-url.h"
#include <libecal/e-cal-time-util.h>
#include "widgets/menus/gal-view-menus.h"
#include "dialogs/delete-error.h"
#include "dialogs/task-editor.h"
#include "cal-search-bar.h"
#include "calendar-config.h"
#include "calendar-component.h"
#include "comp-util.h"
#include "e-calendar-table-config.h"
#include "misc.h"
#include "tasks-component.h"
#include "e-tasks.h"
#include "common/authentication.h"


/* Private part of the GnomeCalendar structure */
struct _ETasksPrivate {
	/* The task lists for display */
	GHashTable *clients;
	GList *clients_list;

	ECalView *query;
	
	/* The ECalendarTable showing the tasks. */
	GtkWidget   *tasks_view;
	ECalendarTableConfig *tasks_view_config;
	
	/* Calendar search bar for tasks */
	GtkWidget *search_bar;

	/* The HTML widget to display the task's details */
	GtkWidget *html;
	gchar *current_uid;

	/* View instance and the view menus handler */
	GalViewInstance *view_instance;
	GalViewMenus *view_menus;

	GList *notifications;
};


static void e_tasks_class_init (ETasksClass *class);
static void e_tasks_init (ETasks *tasks);
static void setup_widgets (ETasks *tasks);
static void e_tasks_destroy (GtkObject *object);

static void backend_error_cb (ECal *client, const char *message, gpointer data);

/* Signal IDs */
enum {
	SELECTION_CHANGED,
	LAST_SIGNAL
};

static GtkTableClass *parent_class;
static guint e_tasks_signals[LAST_SIGNAL] = { 0 };


E_MAKE_TYPE (e_tasks, "ETasks", ETasks,
	     e_tasks_class_init, e_tasks_init,
	     GTK_TYPE_TABLE)

/* Converts a time_t to a string, relative to the specified timezone */
static char *
timet_to_str_with_zone (time_t t, icaltimezone *zone)
{
        struct icaltimetype itt;
        struct tm tm;
        char buf[256];
                                                                                              
        if (t == -1)
                return g_strdup (_("invalid time"));
                                                                                              
        itt = icaltime_from_timet_with_zone (t, FALSE, zone);
        tm = icaltimetype_to_tm (&itt);
                                                                                              
        e_time_format_date_and_time (&tm, calendar_config_get_24_hour_format (),
                                     FALSE, FALSE, buf, sizeof (buf));
        return g_strdup (buf);
}

static void
write_html (GtkHTMLStream *stream, ECalComponent *comp)
{
	ECalComponentText text;
	ECalComponentDateTime dt;
	gchar *buf, *str;
	icaltimezone *current_zone;
	GSList *l;
	icalproperty_status status;
	int *priority_value;

	g_return_if_fail (E_IS_CAL_COMPONENT (comp));

	str = calendar_config_get_timezone ();
	if (str && str[0]) {
		current_zone = icaltimezone_get_builtin_timezone (str);
	} else
		current_zone = icaltimezone_get_utc_timezone ();

	/* write document header */
	e_cal_component_get_summary (comp, &text);
	gtk_html_stream_printf (stream,
				"<HTML><BODY><H1>%s</H1>",
				text.value);

	/* write icons for the categories */
	e_cal_component_get_categories_list (comp, &l);
	if (l) {
		GSList *node;

		for (node = l; node != NULL; node = node->next) {
			const char *icon_file;

			icon_file = e_categories_config_get_icon_file_for ((const char *) node->data);
			if (icon_file) {
				gtk_html_stream_printf (stream, "<IMG ALT=\"%s\" SRC=\"file://%s\">",
							(const char *) node->data, icon_file);
			}
		}

		e_cal_component_free_categories_list (l);
	}
	
	/* write summary */
	gtk_html_stream_printf (stream,
				"<BR><BR><BR><TABLE BORDER=\"0\" WIDTH=\"80%%\">"
				"<TR><TD VALIGN=\"TOP\" ALIGN=\"RIGHT\" WIDTH=\"15%%\"><B>%s</B></TD><TD>%s</TD></TR>",
				_("Summary:"), text.value);

	/* write start date */
	e_cal_component_get_dtstart (comp, &dt);
	if (dt.value != NULL) {
		buf = timet_to_str_with_zone (icaltime_as_timet (*dt.value), current_zone);
		str = g_locale_to_utf8 (buf, -1, NULL, NULL, NULL);

		g_free (buf);
	} else
		str = g_strdup ("");

	gtk_html_stream_printf (stream, "<TR><TD VALIGN=\"TOP\" ALIGN=\"RIGHT\"><B>%s</B></TD><TD>%s</TD></TR>",
				_("Start Date:"), str);

	e_cal_component_free_datetime (&dt);
	g_free (str);

	/* write Due Date */
	e_cal_component_get_due (comp, &dt);
	if (dt.value != NULL) {
		buf = timet_to_str_with_zone (icaltime_as_timet (*dt.value), current_zone);
		str = g_locale_to_utf8 (buf, -1, NULL, NULL, NULL);

		g_free (buf);
	} else
		str = g_strdup ("");

	gtk_html_stream_printf (stream, "<TR><TD VALIGN=\"TOP\" ALIGN=\"RIGHT\"><B>%s</B></TD><TD>%s</TD></TR>",
				_("Due Date:"), str);

	e_cal_component_free_datetime (&dt);
	g_free (str);

	/* write status */
	gtk_html_stream_printf (stream, "<TR><TD VALIGN=\"TOP\" ALIGN=\"RIGHT\"><B>%s</B></TD>", _("Status:"));
	e_cal_component_get_status (comp, &status);
	switch (status) {
	case ICAL_STATUS_INPROCESS :
		str = g_strdup (_("In Progress"));
		break;
	case ICAL_STATUS_COMPLETED :
		str = g_strdup (_("Completed"));
		break;
	case ICAL_STATUS_CANCELLED :
		str = g_strdup (_("Cancelled"));
		break;
	case ICAL_STATUS_NONE :
	default :
		str = g_strdup (_("Not Started"));
		break;
	}

	gtk_html_stream_printf (stream, "<TD>%s</TD></TR>", str);
	g_free (str);

	/* write priority */
	gtk_html_stream_printf (stream, "<TR><TD VALIGN=\"TOP\" ALIGN=\"RIGHT\"><B>%s</B></TD>", _("Priority:"));
	e_cal_component_get_priority (comp, &priority_value);
	if (priority_value) {
		if (*priority_value == 0)
			str = g_strdup ("");
		else if (*priority_value <= 4)
			str = g_strdup (_("High"));
		else if (*priority_value == 5)
			str = g_strdup (_("Normal"));
		else
			str = g_strdup (_("Low"));

		gtk_html_stream_printf (stream, "<TD>%s</TD></TR>", str);

		g_free (str);
		e_cal_component_free_priority (priority_value);
	} else
		gtk_html_stream_printf (stream, "<TD></TD></TR>");
	
	/* write description and URL */
	gtk_html_stream_printf (stream, "<TR><TD COLSPAN=\"2\"><HR></TD></TR>");

	gtk_html_stream_printf (stream, "<TR><TD VALIGN=\"TOP\" ALIGN=\"RIGHT\"><B>%s</B></TD>", _("Description:"));
	e_cal_component_get_description_list (comp, &l);
	if (l) {
		GSList *node;

		gtk_html_stream_printf (stream, "<TD>");

		for (node = l; node != NULL; node = node->next) {
			gint i;
			GString *str = g_string_new ("");;

			text = * (ECalComponentText *) node->data;
			for (i = 0; i < strlen (text.value ? text.value : 0); i++) {
				if (text.value[i] == '\n')
					str = g_string_append (str, "<BR>");
				else if (text.value[i] == '<')
					str = g_string_append (str, "&lt;");
				else if (text.value[i] == '>')
					str = g_string_append (str, "&gt;");
				else
					str = g_string_append_c (str, text.value[i]);
			}

			gtk_html_stream_printf (stream, str->str);
			g_string_free (str, TRUE);
		}

		gtk_html_stream_printf (stream, "</TD></TR>");

		e_cal_component_free_text_list (l);
	} else
		gtk_html_stream_printf (stream, "<TD></TD></TR>");

	/* URL */
	gtk_html_stream_printf (stream, "<TR><TD VALIGN=\"TOP\" ALIGN=\"RIGHT\"><B>%s</B></TD>", _("Web Page:"));
	e_cal_component_get_url (comp, (const char **) &str);
	if (str)
		gtk_html_stream_printf (stream, "<TD><A HREF=\"%s\">%s</A></TD></TR>", str, str);
	else
		gtk_html_stream_printf (stream, "<TD></TD></TR>");

	gtk_html_stream_printf (stream, "</TABLE>");

	/* close document */
	gtk_html_stream_printf (stream, "</BODY></HTML>");
}

static void
on_link_clicked (GtkHTML *html, const char *url, gpointer data)
{
        GError *err = NULL;

        gnome_url_show (url, &err);

	if (err) {
		g_warning ("gnome_url_show: %s", err->message);
                g_error_free (err);
        }
}

static void
on_url_cb (GtkHTML *html, const char *url, gpointer data)
{
	char *msg;
	ETasks *tasks = data;

	if (url && *url) {
		msg = g_strdup_printf (_("Click to open %s"), url);
		e_calendar_table_set_status_message (e_tasks_get_calendar_table (tasks), msg);
		g_free (msg);
	} else
		e_calendar_table_set_status_message (e_tasks_get_calendar_table (tasks), NULL);
}

/* Callback used when the cursor changes in the table */
static void
table_cursor_change_cb (ETable *etable, int row, gpointer data)
{
	ETasks *tasks;
	ETasksPrivate *priv;
	int n_selected;

	tasks = E_TASKS (data);
	priv = tasks->priv;

	n_selected = e_table_selected_count (etable);

	/* update the HTML widget */
	if (n_selected == 1) {
		GtkHTMLStream *stream;
		ECalModel *model;
		ECalModelComponent *comp_data;
		ECalComponent *comp;
		const char *uid;

		model = e_calendar_table_get_model (E_CALENDAR_TABLE (priv->tasks_view));

		stream = gtk_html_begin (GTK_HTML (priv->html));

		comp_data = e_cal_model_get_component_at (model, e_table_get_cursor_row (etable));
		comp = e_cal_component_new ();
		e_cal_component_set_icalcomponent (comp, icalcomponent_new_clone (comp_data->icalcomp));
		write_html (stream, comp);

		gtk_html_stream_close (stream, GTK_HTML_STREAM_OK);

		e_cal_component_get_uid (comp, &uid);
		if (priv->current_uid)
			g_free (priv->current_uid);
		priv->current_uid = g_strdup (uid);

		g_object_unref (comp);
	} else
		gtk_html_load_empty (GTK_HTML (priv->html));
}

/* Callback used when the selection changes in the table. */
static void
table_selection_change_cb (ETable *etable, gpointer data)
{
	ETasks *tasks;
	int n_selected;

	tasks = E_TASKS (data);

	n_selected = e_table_selected_count (etable);
	gtk_signal_emit (GTK_OBJECT (tasks), e_tasks_signals[SELECTION_CHANGED],
			 n_selected);
}

/* Callback used when the sexp in the search bar changes */
static void
search_bar_sexp_changed_cb (CalSearchBar *cal_search, const char *sexp, gpointer data)
{
	ETasks *tasks;
	ETasksPrivate *priv;
	ECalModel *model;

	tasks = E_TASKS (data);
	priv = tasks->priv;

	model = e_calendar_table_get_model (E_CALENDAR_TABLE (priv->tasks_view));
	e_cal_model_set_query (model, sexp);
}

/* Callback used when the selected category in the search bar changes */
static void
search_bar_category_changed_cb (CalSearchBar *cal_search, const char *category, gpointer data)
{
	ETasks *tasks;
	ETasksPrivate *priv;
	ECalModel *model;

	tasks = E_TASKS (data);
	priv = tasks->priv;

	model = e_calendar_table_get_model (E_CALENDAR_TABLE (priv->tasks_view));
	e_cal_model_set_default_category (model, category);
}

/* Callback used when the user selects a URL in the HTML widget */
static void
url_requested_cb (GtkHTML *html, const char *url, GtkHTMLStream *stream, gpointer data)
{
	if (!strncmp ("file:///", url, strlen ("file:///"))) {
		GnomeVFSHandle *handle;
		GnomeVFSResult result;
		char buffer[4096];

		if (gnome_vfs_open (&handle, url, GNOME_VFS_OPEN_READ) == GNOME_VFS_OK) {
			do {
				GnomeVFSFileSize bread;

				result = gnome_vfs_read (handle, buffer, sizeof (buffer), &bread);
				if (result == GNOME_VFS_OK)
					gtk_html_stream_write (stream, buffer, bread);
			} while (result == GNOME_VFS_OK);

			gnome_vfs_close (handle);
		}
	}
}

static gboolean
vpaned_resized_cb (GtkWidget *widget, GdkEventButton *event, ETasks *tasks)
{
	calendar_config_set_task_vpane_pos (gtk_paned_get_position (GTK_PANED (widget)));

	return FALSE;
}

static void
set_timezone (ETasks *tasks) 
{
	ETasksPrivate *priv;
	char *location;
	icaltimezone *zone;
	GList *l;
	
	priv = tasks->priv;
	
	location = calendar_config_get_timezone ();
	zone = icaltimezone_get_builtin_timezone (location);
	g_free (location);
	
	if (!zone)
		zone = icaltimezone_get_utc_timezone ();

	for (l = priv->clients_list; l != NULL; l = l->next) {
		ECal *client = l->data;
		
		if (e_cal_get_load_state (client) == E_CAL_LOAD_LOADED)
			/* FIXME Error checking */
			e_cal_set_default_timezone (client, zone, NULL);
	}
}

static void
timezone_changed_cb (GConfClient *client, guint id, GConfEntry *entry, gpointer data)
{
	ETasks *tasks = data;
	
	set_timezone (tasks);
}

static void
model_row_changed_cb (ETableModel *etm, int row, gpointer data)
{
	ETasks *tasks;
	ETasksPrivate *priv;
	ECalModelComponent *comp_data;

	tasks = E_TASKS (data);
	priv = tasks->priv;

	if (priv->current_uid) {
		const char *uid;

		comp_data = e_cal_model_get_component_at (E_CAL_MODEL (etm), row);
		if (comp_data) {
			uid = icalcomponent_get_uid (comp_data->icalcomp);
			if (!strcmp (uid ? uid : "", priv->current_uid)) {
				ETable *etable;

				etable = e_table_scrolled_get_table (
					E_TABLE_SCROLLED (E_CALENDAR_TABLE (priv->tasks_view)->etable));
				table_cursor_change_cb (etable, 0, tasks);
			}
		}
	}
}

static void
setup_config (ETasks *tasks)
{
	ETasksPrivate *priv;
	guint not;

	priv = tasks->priv;
	
	/* Timezone */
	set_timezone (tasks);
	
	not = calendar_config_add_notification_timezone (timezone_changed_cb, tasks);
	priv->notifications = g_list_prepend (priv->notifications, GUINT_TO_POINTER (not));
}

#define E_TASKS_TABLE_DEFAULT_STATE					\
	"<?xml version=\"1.0\"?>"					\
	"<ETableState>"							\
	"<column source=\"13\"/>"					\
	"<column source=\"14\"/>"					\
	"<column source=\"9\"/>"					\
	"<column source=\"5\"/>"					\
	"<grouping/>"							\
	"</ETableState>"

static void
setup_widgets (ETasks *tasks)
{
	ETasksPrivate *priv;
	ETable *etable;
	ECalModel *model;
	GtkWidget *paned, *scroll;

	priv = tasks->priv;

	priv->search_bar = cal_search_bar_new ();
	g_signal_connect (priv->search_bar, "sexp_changed",
			  G_CALLBACK (search_bar_sexp_changed_cb), tasks);
	g_signal_connect (priv->search_bar, "category_changed",
			  G_CALLBACK (search_bar_category_changed_cb), tasks);

	gtk_table_attach (GTK_TABLE (tasks), priv->search_bar, 0, 1, 0, 1,
			  GTK_EXPAND | GTK_FILL | GTK_SHRINK, 0, 0, 0);
	gtk_widget_show (priv->search_bar);

	/* add the paned widget for the task list and task detail areas */
	paned = gtk_vpaned_new ();
	gtk_paned_set_position (GTK_PANED (paned), calendar_config_get_task_vpane_pos ());
	g_signal_connect (G_OBJECT (paned), "button_release_event",
			  G_CALLBACK (vpaned_resized_cb), tasks);
	gtk_table_attach (GTK_TABLE (tasks), paned, 0, 1, 1, 2,
			  GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);
	gtk_widget_show (paned);

	/* create the task list */
	priv->tasks_view = e_calendar_table_new ();
	priv->tasks_view_config = e_calendar_table_config_new (E_CALENDAR_TABLE (priv->tasks_view));
	
	etable = e_table_scrolled_get_table (
		E_TABLE_SCROLLED (E_CALENDAR_TABLE (priv->tasks_view)->etable));
	e_table_set_state (etable, E_TASKS_TABLE_DEFAULT_STATE);
	gtk_paned_add1 (GTK_PANED (paned), priv->tasks_view);
	gtk_widget_show (priv->tasks_view);

	g_signal_connect (etable, "cursor_change", G_CALLBACK (table_cursor_change_cb), tasks);
	g_signal_connect (etable, "selection_change", G_CALLBACK (table_selection_change_cb), tasks);

	/* create the task detail */
	priv->html = gtk_html_new ();
	gtk_html_set_default_content_type (GTK_HTML (priv->html), "charset=utf-8");
	gtk_html_load_empty (GTK_HTML (priv->html));

	g_signal_connect (G_OBJECT (priv->html), "url_requested",
			  G_CALLBACK (url_requested_cb), NULL);
	g_signal_connect (G_OBJECT (priv->html), "link_clicked",
			  G_CALLBACK (on_link_clicked), tasks);
	g_signal_connect (G_OBJECT (priv->html), "on_url",
			  G_CALLBACK (on_url_cb), tasks);

	gtk_widget_pop_colormap ();
	scroll = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll),
					GTK_POLICY_AUTOMATIC,
					GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scroll), GTK_SHADOW_IN);
	gtk_container_add (GTK_CONTAINER (scroll), priv->html);
	gtk_paned_add2 (GTK_PANED (paned), scroll);
	gtk_widget_show_all (scroll);

	model = e_calendar_table_get_model (E_CALENDAR_TABLE (priv->tasks_view));
	g_signal_connect (G_OBJECT (model), "model_row_changed",
			  G_CALLBACK (model_row_changed_cb), tasks);
}

/* Class initialization function for the gnome calendar */
static void
e_tasks_class_init (ETasksClass *class)
{
	GtkObjectClass *object_class;

	object_class = (GtkObjectClass *) class;

	parent_class = gtk_type_class (GTK_TYPE_TABLE);

	e_tasks_signals[SELECTION_CHANGED] =
		gtk_signal_new ("selection_changed",
				GTK_RUN_LAST,
				G_TYPE_FROM_CLASS (object_class), 
				GTK_SIGNAL_OFFSET (ETasksClass, selection_changed),
				gtk_marshal_NONE__INT,
				GTK_TYPE_NONE, 1,
				GTK_TYPE_INT);

	object_class->destroy = e_tasks_destroy;

	class->selection_changed = NULL;
}


/* Object initialization function for the gnome calendar */
static void
e_tasks_init (ETasks *tasks)
{
	ETasksPrivate *priv;

	priv = g_new0 (ETasksPrivate, 1);
	tasks->priv = priv;

	setup_config (tasks);
	setup_widgets (tasks);

	priv->clients = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);	
	priv->query = NULL;
	priv->view_instance = NULL;
	priv->view_menus = NULL;
	priv->current_uid = NULL;
}

/* Callback used when the set of categories changes in the calendar client */
static void
client_categories_changed_cb (ECal *client, GPtrArray *categories, gpointer data)
{
	ETasks *tasks;
	ETasksPrivate *priv;

	tasks = E_TASKS (data);
	priv = tasks->priv;

	cal_search_bar_set_categories (CAL_SEARCH_BAR (priv->search_bar), categories);
}

GtkWidget *
e_tasks_new (void)
{
	ETasks *tasks;

	tasks = g_object_new (e_tasks_get_type (), NULL);

	return GTK_WIDGET (tasks);
}


void
e_tasks_set_ui_component (ETasks *tasks,
			  BonoboUIComponent *ui_component)
{
	g_return_if_fail (E_IS_TASKS (tasks));
	g_return_if_fail (ui_component == NULL || BONOBO_IS_UI_COMPONENT (ui_component));

	e_search_bar_set_ui_component (E_SEARCH_BAR (tasks->priv->search_bar), ui_component);
}


static void
e_tasks_destroy (GtkObject *object)
{
	ETasks *tasks;
	ETasksPrivate *priv;

	g_return_if_fail (object != NULL);
	g_return_if_fail (E_IS_TASKS (object));

	tasks = E_TASKS (object);
	priv = tasks->priv;

	if (priv) {
		GList *l;

		/* disconnect from signals on all the clients */
		for (l = priv->clients_list; l != NULL; l = l->next) {
			g_signal_handlers_disconnect_matched (l->data, G_SIGNAL_MATCH_DATA,
							      0, 0, NULL, NULL, tasks);
		}

		g_hash_table_destroy (priv->clients);
		g_list_free (priv->clients_list);

		if (priv->current_uid) {
			g_free (priv->current_uid);
			priv->current_uid = NULL;
		}

		if (priv->tasks_view_config) {
			g_object_unref (priv->tasks_view_config);
			priv->tasks_view_config = NULL;
		}
		
		for (l = priv->notifications; l; l = l->next)
			calendar_config_remove_notification (GPOINTER_TO_UINT (l->data));
		priv->notifications = NULL;
		
		g_free (priv);
		tasks->priv = NULL;
	}

	if (GTK_OBJECT_CLASS (parent_class)->destroy)
		(* GTK_OBJECT_CLASS (parent_class)->destroy) (object);
}

static void
set_status_message (ETasks *tasks, const char *message, ...)
{
	ETasksPrivate *priv;
	va_list args;
	char sz[2048];
	
	va_start (args, message);
	vsnprintf (sz, sizeof sz, message, args);
	va_end (args);

	priv = tasks->priv;
	
	e_calendar_table_set_status_message (E_CALENDAR_TABLE (priv->tasks_view), sz);
}

/* Callback from the calendar client when an error occurs in the backend */
static void
backend_error_cb (ECal *client, const char *message, gpointer data)
{
	ETasks *tasks;
	ETasksPrivate *priv;
	char *errmsg;
	char *urinopwd;

	tasks = E_TASKS (data);
	priv = tasks->priv;

	urinopwd = get_uri_without_password (e_cal_get_uri (client));
	errmsg = g_strdup_printf (_("Error on %s:\n %s"), urinopwd, message);
	gnome_error_dialog_parented (errmsg, GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (tasks))));
	g_free (errmsg);
	g_free (urinopwd);
}

/* Callback from the calendar client when the backend dies */
static void
backend_died_cb (ECal *client, gpointer data)
{
	ETasks *tasks;
	ETasksPrivate *priv;
	char *message;
	GtkWidget *dialog;

	tasks = E_TASKS (data);
	priv = tasks->priv;

	/* FIXME: this doesn't remove the task list from the list or anything */
	message = g_strdup_printf (_("The task backend for\n%s\n has crashed. "
				     "You will have to restart Evolution in order "
				     "to use it again"),
				   e_cal_get_uri (client));
	e_calendar_table_set_status_message (E_CALENDAR_TABLE (e_tasks_get_calendar_table (tasks)), NULL);

	dialog = gtk_message_dialog_new (GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (tasks))),
					 0, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
					 message);
	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);
	g_free (message);
}

void
e_tasks_open_task			(ETasks		*tasks)
{
	ECalendarTable *cal_table;
                                                                                
	cal_table = e_tasks_get_calendar_table (tasks);
	e_calendar_table_open_selected (cal_table);
}

void
e_tasks_new_task			(ETasks		*tasks)
{
	ETasksPrivate *priv;
	TaskEditor *tedit;
	ECalComponent *comp;
	const char *category;
	ECal *ecal;
	
	g_return_if_fail (E_IS_TASKS (tasks));

	priv = tasks->priv;

	/* FIXME What to do about no default client */
	ecal = e_tasks_get_default_client (tasks);
	if (!ecal)
		return;
	
	comp = cal_comp_task_new_with_defaults (ecal);

	category = cal_search_bar_get_category (CAL_SEARCH_BAR (priv->search_bar));
	e_cal_component_set_categories (comp, category);

	tedit = task_editor_new (ecal);
	comp_editor_edit_comp (COMP_EDITOR (tedit), comp);
	g_object_unref (comp);

	comp_editor_focus (COMP_EDITOR (tedit));
}

gboolean
e_tasks_add_todo_source (ETasks *tasks, ESource *source)
{
	ETasksPrivate *priv;
	ECal *client;
	ECalModel *model;
	char *str_uri;
	GError *error = NULL;

	g_return_val_if_fail (tasks != NULL, FALSE);
	g_return_val_if_fail (E_IS_TASKS (tasks), FALSE);
	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);

	priv = tasks->priv;

	str_uri = e_source_get_uri (source);
	client = g_hash_table_lookup (priv->clients, str_uri);
	if (client) {
		g_free (str_uri);
		return TRUE;
	}

	set_status_message (tasks, _("Opening tasks at %s"), str_uri);

	client = auth_new_cal_from_source (source, E_CAL_SOURCE_TYPE_TODO);
	if (!client) {
		g_free (str_uri);
		return FALSE;
	}

	g_hash_table_insert (priv->clients, str_uri, client);
	priv->clients_list = g_list_prepend (priv->clients_list, client);
	
	g_signal_connect (G_OBJECT (client), "backend_error", G_CALLBACK (backend_error_cb), tasks);
	g_signal_connect (G_OBJECT (client), "categories_changed", G_CALLBACK (client_categories_changed_cb), tasks);
	g_signal_connect (G_OBJECT (client), "backend_died", G_CALLBACK (backend_died_cb), tasks);

	if (!e_cal_open (client, FALSE, &error)) {
		GtkWidget *dialog;

		dialog = gtk_message_dialog_new (GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (tasks))),
						 GTK_DIALOG_NO_SEPARATOR,
						 GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
						 _("Error opening %s:\n%s"),
						 str_uri, error ? error->message : "");

		g_error_free (error);
		priv->clients_list = g_list_prepend (priv->clients_list, client);
		g_signal_handlers_disconnect_matched (client, G_SIGNAL_MATCH_DATA,
						      0, 0, NULL, NULL, tasks);	

		/* Do this last because it unrefs the client */
		g_hash_table_remove (priv->clients, str_uri);

		gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (dialog);

		set_status_message (tasks, NULL);

		return FALSE;
	}

	set_status_message (tasks, _("Loading tasks"));
	model = e_calendar_table_get_model (E_CALENDAR_TABLE (priv->tasks_view));
	e_cal_model_add_client (model, client);

	set_timezone (tasks);
	set_status_message (tasks, NULL);

	return TRUE;
}

gboolean
e_tasks_remove_todo_source (ETasks *tasks, ESource *source)
{
	ETasksPrivate *priv;
	ECal *client;
	ECalModel *model;
	char *str_uri;

	g_return_val_if_fail (tasks != NULL, FALSE);
	g_return_val_if_fail (E_IS_TASKS (tasks), FALSE);
	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);

	priv = tasks->priv;

	str_uri = e_source_get_uri (source);
	client = g_hash_table_lookup (priv->clients, str_uri);
	if (!client) {
		g_free (str_uri);
		return TRUE;
	}

	priv->clients_list = g_list_remove (priv->clients_list, client);
	g_signal_handlers_disconnect_matched (client, G_SIGNAL_MATCH_DATA,
					      0, 0, NULL, NULL, tasks);	

	model = e_calendar_table_get_model (E_CALENDAR_TABLE (priv->tasks_view));
	e_cal_model_remove_client (model, client);

	g_hash_table_remove (priv->clients, str_uri);
	g_free (str_uri);

	return TRUE;
}

gboolean
e_tasks_set_default_source (ETasks *tasks, ESource *source)
{
	ETasksPrivate *priv;
	ECal *ecal;
	ECalModel *model;
	char *str_uri;
	
	g_return_val_if_fail (tasks != NULL, FALSE);
	g_return_val_if_fail (E_IS_TASKS (tasks), FALSE);
	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);

	priv = tasks->priv;

	str_uri = e_source_get_uri (source);
	model = e_calendar_table_get_model (E_CALENDAR_TABLE (priv->tasks_view));
	ecal = e_cal_model_get_client_for_uri (model, str_uri);
	g_free (str_uri);
	if (!ecal)
		return FALSE;

	e_cal_model_set_default_client (model, ecal);

	return TRUE;
}

ECal *
e_tasks_get_default_client (ETasks *tasks)
{
	ETasksPrivate *priv;
	
	g_return_val_if_fail (tasks != NULL, NULL);
	g_return_val_if_fail (E_IS_TASKS (tasks), NULL);

	priv = tasks->priv;

	return e_cal_model_get_default_client (e_calendar_table_get_model (E_CALENDAR_TABLE (priv->tasks_view)));	
}

/**
 * e_tasks_complete_selected:
 * @tasks: A tasks control widget
 * 
 * Marks the selected tasks complete
 **/
void
e_tasks_complete_selected (ETasks *tasks)
{
	ETasksPrivate *priv;
	ECalendarTable *cal_table;

	g_return_if_fail (tasks != NULL);
	g_return_if_fail (E_IS_TASKS (tasks));

	priv = tasks->priv;

	cal_table = E_CALENDAR_TABLE (priv->tasks_view);

	set_status_message (tasks, _("Completing tasks..."));
	e_calendar_table_complete_selected (cal_table);
	set_status_message (tasks, NULL);
}

/**
 * e_tasks_delete_selected:
 * @tasks: A tasks control widget.
 * 
 * Deletes the selected tasks in the task list.
 **/
void
e_tasks_delete_selected (ETasks *tasks)
{
	ETasksPrivate *priv;
	ECalendarTable *cal_table;

	g_return_if_fail (tasks != NULL);
	g_return_if_fail (E_IS_TASKS (tasks));

	priv = tasks->priv;

	cal_table = E_CALENDAR_TABLE (priv->tasks_view);
	set_status_message (tasks, _("Deleting selected objects..."));
	e_calendar_table_delete_selected (cal_table);
	set_status_message (tasks, NULL);
}

/**
 * e_tasks_expunge:
 * @tasks: A tasks control widget
 * 
 * Removes all tasks marked as completed
 **/
void
e_tasks_delete_completed (ETasks *tasks)
{
	ETasksPrivate *priv;
	char *sexp;
	GList *l;
	
	g_return_if_fail (tasks != NULL);
	g_return_if_fail (E_IS_TASKS (tasks));

	priv = tasks->priv;

	sexp = g_strdup ("(is-completed?)");

	set_status_message (tasks, _("Expunging"));	

	for (l = priv->clients_list; l != NULL; l = l->next) {
		ECal *client = l->data;
		GList *objects, *m;
		gboolean read_only = TRUE;
		
		e_cal_is_read_only (client, &read_only, NULL);
		if (read_only)
			continue;
		
		if (!e_cal_get_object_list (client, sexp, &objects, NULL)) {
			g_warning (G_STRLOC ": Could not get the objects");
			
			continue;
		}
		
		for (m = objects; m; m = m->next) {
			/* FIXME Better error handling */
			e_cal_remove_object (client, icalcomponent_get_uid (m->data), NULL);
		}
	}

	set_status_message (tasks, NULL);

	g_free (sexp);
}

/* Callback used from the view collection when we need to display a new view */
static void
display_view_cb (GalViewInstance *instance, GalView *view, gpointer data)
{
	ETasks *tasks;

	tasks = E_TASKS (data);

	if (GAL_IS_VIEW_ETABLE (view)) {
		gal_view_etable_attach_table (GAL_VIEW_ETABLE (view), e_table_scrolled_get_table (E_TABLE_SCROLLED (E_CALENDAR_TABLE (tasks->priv->tasks_view)->etable)));
	}
}

/**
 * e_tasks_setup_view_menus:
 * @tasks: A tasks widget.
 * @uic: UI controller to use for the menus.
 * 
 * Sets up the #GalView menus for a tasks control.  This function should be
 * called from the Bonobo control activation callback for this tasks control.
 * Also, the menus should be discarded using e_tasks_discard_view_menus().
 **/
void
e_tasks_setup_view_menus (ETasks *tasks, BonoboUIComponent *uic)
{
	ETasksPrivate *priv;
	GalViewFactory *factory;
	ETableSpecification *spec;
	char *dir;
	static GalViewCollection *collection = NULL;

	g_return_if_fail (tasks != NULL);
	g_return_if_fail (E_IS_TASKS (tasks));
	g_return_if_fail (uic != NULL);
	g_return_if_fail (BONOBO_IS_UI_COMPONENT (uic));

	priv = tasks->priv;

	g_return_if_fail (priv->view_instance == NULL);

	g_assert (priv->view_instance == NULL);
	g_assert (priv->view_menus == NULL);

	/* Create the view instance */

	if (collection == NULL) {
		collection = gal_view_collection_new ();

		gal_view_collection_set_title (collection, _("Tasks"));

		dir = g_build_filename (tasks_component_peek_base_directory (tasks_component_peek ()), 
					"tasks", "views", NULL);		
		gal_view_collection_set_storage_directories (collection,
							     EVOLUTION_GALVIEWSDIR "/tasks/",
							     dir);
		g_free (dir);

		/* Create the views */

		spec = e_table_specification_new ();
		e_table_specification_load_from_file (spec, 
						      EVOLUTION_ETSPECDIR "/e-calendar-table.etspec");

		factory = gal_view_factory_etable_new (spec);
		g_object_unref (spec);
		gal_view_collection_add_factory (collection, factory);
		g_object_unref (factory);

		/* Load the collection and create the menus */

		gal_view_collection_load (collection);
	}

	priv->view_instance = gal_view_instance_new (collection, e_cal_get_uri (e_tasks_get_default_client (tasks)));

	priv->view_menus = gal_view_menus_new (priv->view_instance);
	gal_view_menus_apply (priv->view_menus, uic, NULL);
	g_signal_connect (priv->view_instance, "display_view", G_CALLBACK (display_view_cb), tasks);
	display_view_cb (priv->view_instance, gal_view_instance_get_current_view (priv->view_instance), tasks);
}

/**
 * e_tasks_discard_view_menus:
 * @tasks: A tasks widget.
 * 
 * Discards the #GalView menus used by a tasks control.  This function should be
 * called from the Bonobo control deactivation callback for this tasks control.
 * The menus should have been set up with e_tasks_setup_view_menus().
 **/
void
e_tasks_discard_view_menus (ETasks *tasks)
{
	ETasksPrivate *priv;

	g_return_if_fail (tasks != NULL);
	g_return_if_fail (E_IS_TASKS (tasks));

	priv = tasks->priv;

	g_return_if_fail (priv->view_instance != NULL);

	g_assert (priv->view_instance != NULL);
	g_assert (priv->view_menus != NULL);

	g_object_unref (priv->view_instance);
	priv->view_instance = NULL;

	g_object_unref (priv->view_menus);
	priv->view_menus = NULL;
}

/**
 * e_tasks_get_calendar_table:
 * @tasks: A tasks widget.
 * 
 * Queries the #ECalendarTable contained in a tasks widget.
 * 
 * Return value: The #ECalendarTable that the tasks widget uses to display its
 * information.
 **/
ECalendarTable *
e_tasks_get_calendar_table (ETasks *tasks)
{
	ETasksPrivate *priv;

	g_return_val_if_fail (tasks != NULL, NULL);
	g_return_val_if_fail (E_IS_TASKS (tasks), NULL);

	priv = tasks->priv;
	return E_CALENDAR_TABLE (priv->tasks_view);
}
