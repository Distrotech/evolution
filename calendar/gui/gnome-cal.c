/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* Evolution calendar - Main calendar view widget
 *
 * Copyright (C) 1998 The Free Software Foundation
 * Copyright (C) 2000 Ximian, Inc.
 * Copyright (C) 2001 Ximian, Inc.
 *
 * Authors: Miguel de Icaza <miguel@ximian.com>
 *          Federico Mena-Quintero <federico@ximian.com>
 *          Seth Alves <alves@hungry.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
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

#include <config.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <glib.h>
#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-i18n.h>
#include <libgnome/gnome-util.h>
#include <libgnomeui/gnome-dialog.h>
#include <libgnomeui/gnome-dialog-util.h>
#include <libgnomevfs/gnome-vfs-types.h>
#include <libgnomevfs/gnome-vfs-uri.h>
#include <liboaf/liboaf.h>
#include <gal/e-paned/e-hpaned.h>
#include <gal/e-paned/e-vpaned.h>
#include <cal-util/timeutil.h>
#include "widgets/menus/gal-view-menus.h"
#include "dialogs/event-editor.h"
#include "dialogs/task-editor.h"
#include "e-calendar-table.h"
#include "e-day-view.h"
#include "e-week-view.h"
#include "evolution-calendar.h"
#include "gnome-cal.h"
#include "component-factory.h"
#include "cal-search-bar.h"
#include "calendar-commands.h"
#include "calendar-config.h"
#include "calendar-view.h"
#include "calendar-view-factory.h"
#include "tag-calendar.h"



/* Private part of the GnomeCalendar structure */
struct _GnomeCalendarPrivate {
	/*
	 * The Calendar Folder.
	 */

	/* The calendar client object we monitor */
	CalClient *client;

	/* Set of categories from the calendar client */
	GPtrArray *cal_categories;

	/*
	 * The TaskPad Folder.
	 */

	/* The calendar client object we monitor */
	CalClient   *task_pad_client;

	/* Set of categories from the tasks client */
	GPtrArray *tasks_categories;

	/*
	 * Fields for the calendar view
	 */

	/* Mapping of component UIDs to event editors */
	GHashTable  *object_editor_hash;

	/* This is the last selection explicitly selected by the user. We try
	   to keep it the same when we switch views, but we may have to alter
	   it depending on the view (e.g. the week views only select days, so
	   any times are lost. */
	time_t      selection_start_time;
	time_t      selection_end_time;

	/* Widgets */

	GtkWidget   *search_bar;

	GtkWidget   *hpane;
	GtkWidget   *notebook;
	GtkWidget   *vpane;
	ECalendar   *date_navigator;
	GtkWidget   *todo;

	GtkWidget   *day_view;
	GtkWidget   *work_week_view;
	GtkWidget   *week_view;
	GtkWidget   *month_view;

	/* Calendar query for the date navigator */
	CalQuery    *dn_query;
	char        *sexp;

	/* This is the view currently shown. We use it to keep track of the
	   positions of the panes. range_selected is TRUE if a range of dates
	   was selected in the date navigator to show the view. */
	GnomeCalendarViewType current_view_type;
	gboolean range_selected;

	/* These are the saved positions of the panes. They are multiples of
	   calendar month widths & heights in the date navigator, so that they
	   will work OK after theme changes. */
	gfloat	     hpane_pos;
	gfloat	     vpane_pos;
	gfloat	     hpane_pos_month_view;
	gfloat	     vpane_pos_month_view;

	/* The signal handler id for our GtkCalendar "day_selected" handler. */
	guint	     day_selected_id;

	/* View collection and menus for the control */
	GalViewCollection *view_collection;
	GalViewMenus *view_menus;

	/* Whether we are being destroyed and should not mess with the object
	 * editor hash table.
	 */
	guint in_destroy : 1;

	/* Our current timezone. */
	icaltimezone *zone;

	/* The dates currently shown. If they are -1 then we have no dates
	   shown. We only use these to check if we need to emit a
	   'dates-shown-changed' signal.*/
	time_t visible_start;
	time_t visible_end;
};

/* Signal IDs */

enum {
	DATES_SHOWN_CHANGED,
	SELECTION_CHANGED,
	LAST_SIGNAL
};

static guint gnome_calendar_signals[LAST_SIGNAL];




static void gnome_calendar_class_init (GnomeCalendarClass *class);
static void gnome_calendar_init (GnomeCalendar *gcal);
static void gnome_calendar_destroy (GtkObject *object);

static void gnome_calendar_set_pane_positions	(GnomeCalendar	*gcal);
static void gnome_calendar_update_view_times (GnomeCalendar *gcal);
static void gnome_calendar_update_date_navigator (GnomeCalendar *gcal);

static void gnome_calendar_on_date_navigator_style_set (GtkWidget *widget,
							GtkStyle  *previous_style,
							gpointer data);
static void gnome_calendar_update_paned_quanta (GnomeCalendar	*gcal);
static void gnome_calendar_on_date_navigator_size_allocate (GtkWidget     *widget,
							    GtkAllocation *allocation,
							    gpointer data);
static void gnome_calendar_on_date_navigator_date_range_changed (ECalendarItem *calitem,
								 GnomeCalendar *gcal);
static void gnome_calendar_on_date_navigator_selection_changed (ECalendarItem    *calitem,
								GnomeCalendar    *gcal);
static void gnome_calendar_notify_dates_shown_changed (GnomeCalendar *gcal);

static void update_query (GnomeCalendar *gcal);


static GtkVBoxClass *parent_class;




GtkType
gnome_calendar_get_type (void)
{
	static GtkType gnome_calendar_type = 0;

	if (!gnome_calendar_type) {
		static const GtkTypeInfo gnome_calendar_info = {
			"GnomeCalendar",
			sizeof (GnomeCalendar),
			sizeof (GnomeCalendarClass),
			(GtkClassInitFunc) gnome_calendar_class_init,
			(GtkObjectInitFunc) gnome_calendar_init,
			NULL, /* reserved_1 */
			NULL, /* reserved_2 */
			(GtkClassInitFunc) NULL
		};

		gnome_calendar_type = gtk_type_unique (GTK_TYPE_VBOX, &gnome_calendar_info);
	}

	return gnome_calendar_type;
}

/* Class initialization function for the gnome calendar */
static void
gnome_calendar_class_init (GnomeCalendarClass *class)
{
	GtkObjectClass *object_class;

	object_class = (GtkObjectClass *) class;

	parent_class = gtk_type_class (GTK_TYPE_VBOX);

	gnome_calendar_signals[DATES_SHOWN_CHANGED] =
		gtk_signal_new ("dates_shown_changed",
				GTK_RUN_LAST,
				object_class->type,
				GTK_SIGNAL_OFFSET (GnomeCalendarClass,
						   dates_shown_changed),
				gtk_marshal_NONE__NONE,
				GTK_TYPE_NONE, 0);

	gnome_calendar_signals[SELECTION_CHANGED] =
		gtk_signal_new ("selection_changed",
				GTK_RUN_LAST,
				object_class->type,
				GTK_SIGNAL_OFFSET (GnomeCalendarClass,
						   selection_changed),
				gtk_marshal_NONE__NONE,
				GTK_TYPE_NONE, 0);

	gtk_object_class_add_signals (object_class,
				      gnome_calendar_signals,
				      LAST_SIGNAL);

	object_class->destroy = gnome_calendar_destroy;

	class->dates_shown_changed = NULL;
	class->selection_changed = NULL;
}

/* Callback used when the calendar query reports of an updated object */
static void
dn_query_obj_updated_cb (CalQuery *query, const char *uid,
			 gboolean query_in_progress, int n_scanned, int total,
			 gpointer data)
{
	GnomeCalendar *gcal;
	GnomeCalendarPrivate *priv;
	CalComponent *comp;
	CalClientGetStatus status;

	gcal = GNOME_CALENDAR (data);
	priv = gcal->priv;

	status = cal_client_get_object (priv->client, uid, &comp);

	switch (status) {
	case CAL_CLIENT_GET_SUCCESS:
		/* Everything is fine */
		break;

	case CAL_CLIENT_GET_SYNTAX_ERROR:
		g_message ("dn_query_obj_updated_cb(): Syntax error while getting object `%s'", uid);
		break;

	case CAL_CLIENT_GET_NOT_FOUND:
		/* The object is no longer in the server, so do nothing */
		break;

	default:
		g_assert_not_reached ();
		return;
	}

	tag_calendar_by_comp (priv->date_navigator, comp, priv->client, FALSE,
			      TRUE);
	gtk_object_unref (GTK_OBJECT (comp));
}

/* Callback used when the calendar query reports of a removed object */
static void
dn_query_obj_removed_cb (CalQuery *query, const char *uid, gpointer data)
{
	GnomeCalendar *gcal;

	gcal = GNOME_CALENDAR (data);

	/* Just retag the whole thing */
	update_query (gcal);
}

/* Callback used when the calendar query is done */
static void
dn_query_query_done_cb (CalQuery *query, CalQueryDoneStatus status, const char *error_str,
			gpointer data)
{
	GnomeCalendar *gcal;

	gcal = GNOME_CALENDAR (data);

	/* FIXME */

	if (status != CAL_QUERY_DONE_SUCCESS)
		fprintf (stderr, "query done: %s\n", error_str);
}

/* Callback used when the calendar query reports an evaluation error */
static void
dn_query_eval_error_cb (CalQuery *query, const char *error_str, gpointer data)
{
	GnomeCalendar *gcal;

	gcal = GNOME_CALENDAR (data);

	/* FIXME */

	fprintf (stderr, "eval error: %s\n", error_str);
}

/* Returns the current view widget, a EDayView or EWeekView. */
static GtkWidget*
gnome_calendar_get_current_view_widget (GnomeCalendar *gcal)
{
	GnomeCalendarPrivate *priv;
	GtkWidget *retval = NULL;

	priv = gcal->priv;

	switch (priv->current_view_type) {
	case GNOME_CAL_DAY_VIEW:
		retval = priv->day_view;
		break;
	case GNOME_CAL_WORK_WEEK_VIEW:
		retval = priv->work_week_view;
		break;
	case GNOME_CAL_WEEK_VIEW:
		retval = priv->week_view;
		break;
	case GNOME_CAL_MONTH_VIEW:
		retval = priv->month_view;
		break;
	default:
		g_assert_not_reached ();
	}

	return retval;
}

/* Computes the range of time that the date navigator is showing */
static void
get_date_navigator_range (GnomeCalendar *gcal, time_t *start_time, time_t *end_time)
{
	GnomeCalendarPrivate *priv;
	gint start_year, start_month, start_day;
	gint end_year, end_month, end_day;
	struct icaltimetype start_tt;
	struct icaltimetype end_tt;

	priv = gcal->priv;

	start_tt = icaltime_null_time ();
	end_tt = icaltime_null_time ();

	if (!e_calendar_item_get_date_range (priv->date_navigator->calitem,
					     &start_year, &start_month, &start_day,
					     &end_year, &end_month, &end_day)) {
		*start_time = -1;
		*end_time = -1;
		return;
	}

	start_tt.year = start_year;
	start_tt.month = start_month + 1;
	start_tt.day = start_day;

	end_tt.year = end_year;
	end_tt.month = end_month + 1;
	end_tt.day = end_day;

	icaltime_adjust (&end_tt, 1, 0, 0, 0);

	*start_time = icaltime_as_timet_with_zone (start_tt, priv->zone);
	*end_time = icaltime_as_timet_with_zone (end_tt, priv->zone);
}

/* Adjusts a given query sexp with the time range of the date navigator */
static char *
adjust_query_sexp (GnomeCalendar *gcal, const char *sexp)
{
	time_t start_time, end_time;
	char *start, *end;
	char *new_sexp;

	get_date_navigator_range (gcal, &start_time, &end_time);
	if (start_time == -1 || end_time == -1)
		return NULL;

	start = isodate_from_time_t (start_time);
	end = isodate_from_time_t (end_time);

	new_sexp = g_strdup_printf ("(and (= (get-vtype) \"VEVENT\")"
				    "     (occur-in-time-range? (make-time \"%s\")"
				    "                           (make-time \"%s\"))"
				    "     %s)",
				    start, end,
				    sexp);

	g_free (start);
	g_free (end);

	return new_sexp;
}

/* Restarts a query for the date navigator in the calendar */
static void
update_query (GnomeCalendar *gcal)
{
	GnomeCalendarPrivate *priv;
	CalQuery *old_query;
	char *real_sexp;

	priv = gcal->priv;

	e_calendar_item_clear_marks (priv->date_navigator->calitem);

	if (!(priv->client
	      && cal_client_get_load_state (priv->client) == CAL_CLIENT_LOAD_LOADED))
		return;

	old_query = priv->dn_query;
	priv->dn_query = NULL;

	if (old_query) {
		gtk_signal_disconnect_by_data (GTK_OBJECT (old_query), gcal);
		gtk_object_unref (GTK_OBJECT (old_query));
	}

	g_assert (priv->sexp != NULL);

	real_sexp = adjust_query_sexp (gcal, priv->sexp);
	if (!real_sexp)
		return; /* No time range is set, so don't start a query */

	priv->dn_query = cal_client_get_query (priv->client, real_sexp);
	g_free (real_sexp);

	if (!priv->dn_query) {
		g_message ("update_query(): Could not create the query");
		return;
	}

	gtk_signal_connect (GTK_OBJECT (priv->dn_query), "obj_updated",
			    GTK_SIGNAL_FUNC (dn_query_obj_updated_cb), gcal);
	gtk_signal_connect (GTK_OBJECT (priv->dn_query), "obj_removed",
			    GTK_SIGNAL_FUNC (dn_query_obj_removed_cb), gcal);
	gtk_signal_connect (GTK_OBJECT (priv->dn_query), "query_done",
			    GTK_SIGNAL_FUNC (dn_query_query_done_cb), gcal);
	gtk_signal_connect (GTK_OBJECT (priv->dn_query), "eval_error",
			    GTK_SIGNAL_FUNC (dn_query_eval_error_cb), gcal);
}

/**
 * gnome_calendar_set_query:
 * @gcal: A calendar.
 * @sexp: Sexp that defines the query.
 * 
 * Sets the query sexp for all the views in a calendar.
 **/
void
gnome_calendar_set_query (GnomeCalendar *gcal, const char *sexp)
{
	GnomeCalendarPrivate *priv;
	CalendarModel *model;

	g_return_if_fail (gcal != NULL);
	g_return_if_fail (GNOME_IS_CALENDAR (gcal));
	g_return_if_fail (sexp != NULL);

	priv = gcal->priv;

	/* Set the query on the date navigator */

	if (priv->sexp)
		g_free (priv->sexp);

	priv->sexp = g_strdup (sexp);

	update_query (gcal);

	/* Set the query on the main view */

	switch (priv->current_view_type) {
	case GNOME_CAL_DAY_VIEW:
		e_day_view_set_query (E_DAY_VIEW (priv->day_view), sexp);
		break;

	case GNOME_CAL_WORK_WEEK_VIEW:
		e_day_view_set_query (E_DAY_VIEW (priv->work_week_view), sexp);
		break;

	case GNOME_CAL_WEEK_VIEW:
		e_week_view_set_query (E_WEEK_VIEW (priv->week_view), sexp);
		break;

	case GNOME_CAL_MONTH_VIEW:
		e_week_view_set_query (E_WEEK_VIEW (priv->month_view), sexp);
		break;

	default:
		g_warning ("A penguin bit my hand!");
		g_assert_not_reached ();
	}

	/* Set the query on the task pad */

	model = e_calendar_table_get_model (E_CALENDAR_TABLE (priv->todo));
	calendar_model_set_query (model, sexp);
}

/* Returns the current time, for the ECalendarItem. */
static struct tm
get_current_time (ECalendarItem *calitem, gpointer data)
{
	GnomeCalendar *cal = data;
	struct tm tmp_tm = { 0 };
	struct icaltimetype tt;

	g_return_val_if_fail (cal != NULL, tmp_tm);
	g_return_val_if_fail (GNOME_IS_CALENDAR (cal), tmp_tm);

	tt = icaltime_from_timet_with_zone (time (NULL), FALSE,
					    cal->priv->zone);

	/* Now copy it to the struct tm and return it. */
	tmp_tm.tm_year  = tt.year - 1900;
	tmp_tm.tm_mon   = tt.month - 1;
	tmp_tm.tm_mday  = tt.day;
	tmp_tm.tm_hour  = tt.hour;
	tmp_tm.tm_min   = tt.minute;
	tmp_tm.tm_sec   = tt.second;
	tmp_tm.tm_isdst = -1;

	return tmp_tm;
}

/* Callback used when the sexp changes in the calendar search bar */
static void
search_bar_sexp_changed_cb (CalSearchBar *cal_search, const char *sexp, gpointer data)
{
	GnomeCalendar *gcal;

	gcal = GNOME_CALENDAR (data);
	gnome_calendar_set_query (gcal, sexp);
}

/* Callback used when the selected category in the search bar changes */
static void
search_bar_category_changed_cb (CalSearchBar *cal_search, const char *category, gpointer data)
{
	GnomeCalendar *gcal;
	GnomeCalendarPrivate *priv;
	CalendarModel *model;

	gcal = GNOME_CALENDAR (data);
	priv = gcal->priv;

	e_day_view_set_default_category (E_DAY_VIEW (priv->day_view), category);
	e_day_view_set_default_category (E_DAY_VIEW (priv->work_week_view), category);
	e_week_view_set_default_category (E_WEEK_VIEW (priv->week_view), category);
	e_week_view_set_default_category (E_WEEK_VIEW (priv->month_view), category);

	model = e_calendar_table_get_model (E_CALENDAR_TABLE (priv->todo));
	calendar_model_set_default_category (model, category);
}

static void
view_selection_changed_cb (GtkWidget *view, GnomeCalendar *gcal)
{
	g_print ("In view_selection_changed_cb\n");

	gtk_signal_emit (GTK_OBJECT (gcal),
			 gnome_calendar_signals[SELECTION_CHANGED]);
}


static void
setup_widgets (GnomeCalendar *gcal)
{
	GnomeCalendarPrivate *priv;
	GtkWidget *w;
	gchar *filename;
	CalendarModel *model;

	priv = gcal->priv;

	priv->search_bar = cal_search_bar_new ();
	gtk_signal_connect (GTK_OBJECT (priv->search_bar), "sexp_changed",
			    GTK_SIGNAL_FUNC (search_bar_sexp_changed_cb), gcal);
	gtk_signal_connect (GTK_OBJECT (priv->search_bar), "category_changed",
			    GTK_SIGNAL_FUNC (search_bar_category_changed_cb), gcal);

	gtk_widget_show (priv->search_bar);
	gtk_box_pack_start (GTK_BOX (gcal), priv->search_bar, FALSE, FALSE, 0);

	/* The main HPaned, with the notebook of calendar views on the left
	   and the ECalendar and ToDo list on the right. */
	priv->hpane = e_hpaned_new ();
	gtk_widget_show (priv->hpane);
	gtk_box_pack_start (GTK_BOX (gcal), priv->hpane, TRUE, TRUE, 0);

	/* The Notebook containing the 4 calendar views. */
	priv->notebook = gtk_notebook_new ();
	gtk_notebook_set_show_border (GTK_NOTEBOOK (priv->notebook), FALSE);
	gtk_notebook_set_show_tabs (GTK_NOTEBOOK (priv->notebook), FALSE);
	gtk_widget_show (priv->notebook);
	e_paned_pack1 (E_PANED (priv->hpane), priv->notebook, TRUE, TRUE);

	/* The VPaned widget, to contain the GtkCalendar & ToDo list. */
	priv->vpane = e_vpaned_new ();
	gtk_widget_show (priv->vpane);
	e_paned_pack2 (E_PANED (priv->hpane), priv->vpane, FALSE, TRUE);

	/* The ECalendar. */
	w = e_calendar_new ();
	priv->date_navigator = E_CALENDAR (w);
	e_calendar_item_set_days_start_week_sel (priv->date_navigator->calitem, 9);
	e_calendar_item_set_max_days_sel (priv->date_navigator->calitem, 42);
	gtk_widget_show (w);
	e_calendar_item_set_get_time_callback (priv->date_navigator->calitem,
					       (ECalendarItemGetTimeCallback) get_current_time,
					       gcal, NULL);

	e_paned_pack1 (E_PANED (priv->vpane), w, FALSE, TRUE);
	gtk_signal_connect (GTK_OBJECT (priv->date_navigator),
			    "style_set",
			    GTK_SIGNAL_FUNC (gnome_calendar_on_date_navigator_style_set),
			    gcal);
	gtk_signal_connect_after (GTK_OBJECT (priv->date_navigator),
				  "size_allocate",
				  (GtkSignalFunc) gnome_calendar_on_date_navigator_size_allocate,
				  gcal);
	gtk_signal_connect (GTK_OBJECT (priv->date_navigator->calitem),
			    "selection_changed",
			    (GtkSignalFunc) gnome_calendar_on_date_navigator_selection_changed,
			    gcal);
	gtk_signal_connect (GTK_OBJECT (priv->date_navigator->calitem),
			    "date_range_changed",
			    GTK_SIGNAL_FUNC (gnome_calendar_on_date_navigator_date_range_changed),
			    gcal);

	/* The ToDo list. */
	priv->todo = e_calendar_table_new ();
	calendar_config_configure_e_calendar_table (E_CALENDAR_TABLE (priv->todo));
	model = e_calendar_table_get_model (E_CALENDAR_TABLE (priv->todo));
	calendar_model_set_new_comp_vtype (model, CAL_COMPONENT_TODO);
	e_paned_pack2 (E_PANED (priv->vpane), priv->todo, TRUE, TRUE);
	gtk_widget_show (priv->todo);

	filename = g_strdup_printf ("%s/config/TaskPad", evolution_dir);
	e_calendar_table_load_state (E_CALENDAR_TABLE (priv->todo), filename);
	g_free (filename);

	/* The Day View. */
	priv->day_view = e_day_view_new ();
	e_day_view_set_calendar (E_DAY_VIEW (priv->day_view), gcal);
	gtk_widget_show (priv->day_view);
	gtk_notebook_append_page (GTK_NOTEBOOK (priv->notebook),
				  priv->day_view, gtk_label_new (""));
	gtk_signal_connect (GTK_OBJECT (priv->day_view), "selection_changed",
			    GTK_SIGNAL_FUNC (view_selection_changed_cb), gcal);

	/* The Work Week View. */
	priv->work_week_view = e_day_view_new ();
	e_day_view_set_work_week_view (E_DAY_VIEW (priv->work_week_view),
				       TRUE);
	e_day_view_set_days_shown (E_DAY_VIEW (priv->work_week_view), 5);
	e_day_view_set_calendar (E_DAY_VIEW (priv->work_week_view), gcal);
	gtk_widget_show (priv->work_week_view);
	gtk_notebook_append_page (GTK_NOTEBOOK (priv->notebook),
				  priv->work_week_view, gtk_label_new (""));
	gtk_signal_connect (GTK_OBJECT (priv->work_week_view), "selection_changed",
			    GTK_SIGNAL_FUNC (view_selection_changed_cb), gcal);

	/* The Week View. */
	priv->week_view = e_week_view_new ();
	e_week_view_set_calendar (E_WEEK_VIEW (priv->week_view), gcal);
	gtk_widget_show (priv->week_view);
	gtk_notebook_append_page (GTK_NOTEBOOK (priv->notebook),
				  priv->week_view, gtk_label_new (""));
	gtk_signal_connect (GTK_OBJECT (priv->week_view), "selection_changed",
			    GTK_SIGNAL_FUNC (view_selection_changed_cb), gcal);

	/* The Month View. */
	priv->month_view = e_week_view_new ();
	e_week_view_set_calendar (E_WEEK_VIEW (priv->month_view), gcal);
	e_week_view_set_multi_week_view (E_WEEK_VIEW (priv->month_view), TRUE);
	gtk_widget_show (priv->month_view);
	gtk_notebook_append_page (GTK_NOTEBOOK (priv->notebook),
				  priv->month_view, gtk_label_new (""));
	gtk_signal_connect (GTK_OBJECT (priv->month_view), "selection_changed",
			    GTK_SIGNAL_FUNC (view_selection_changed_cb), gcal);

	gnome_calendar_update_config_settings (gcal, TRUE);
}

/* Object initialization function for the gnome calendar */
static void
gnome_calendar_init (GnomeCalendar *gcal)
{
	GnomeCalendarPrivate *priv;

	priv = g_new0 (GnomeCalendarPrivate, 1);
	gcal->priv = priv;

	priv->cal_categories = NULL;
	priv->tasks_categories = NULL;

	priv->object_editor_hash = g_hash_table_new (g_str_hash, g_str_equal);

	priv->current_view_type = GNOME_CAL_DAY_VIEW;
	priv->range_selected = FALSE;

	setup_widgets (gcal);
	priv->dn_query = NULL;
	priv->sexp = g_strdup ("#t"); /* Match all */

	priv->selection_start_time = time_day_begin_with_zone (time (NULL),
							       priv->zone);
	priv->selection_end_time = time_add_day_with_zone (priv->selection_start_time, 1, priv->zone);

	priv->view_collection = NULL;
	priv->view_menus = NULL;

	priv->visible_start = -1;
	priv->visible_end = -1;
}

/* Frees a set of categories */
static void
free_categories (GPtrArray *categories)
{
	int i;

	if (!categories)
		return;

	for (i = 0; i < categories->len; i++)
		g_free (categories->pdata[i]);

	g_ptr_array_free (categories, TRUE);
}

/* Used from g_hash_table_foreach(); frees an UID string */
static void
destroy_editor_cb (gpointer key, gpointer value, gpointer data)
{
	EventEditor *ee;

	ee = EVENT_EDITOR (value);
	gtk_object_unref (GTK_OBJECT (ee));
}

static void
gnome_calendar_destroy (GtkObject *object)
{
	GnomeCalendar *gcal;
	GnomeCalendarPrivate *priv;
	gchar *filename;

	g_return_if_fail (object != NULL);
	g_return_if_fail (GNOME_IS_CALENDAR (object));

	gcal = GNOME_CALENDAR (object);
	priv = gcal->priv;

	free_categories (priv->cal_categories);
	priv->cal_categories = NULL;

	free_categories (priv->tasks_categories);
	priv->tasks_categories = NULL;

	/* Save the TaskPad layout. */
	filename = g_strdup_printf ("%s/config/TaskPad", evolution_dir);
	e_calendar_table_save_state (E_CALENDAR_TABLE (priv->todo), filename);
	g_free (filename);

	if (priv->dn_query) {
		gtk_signal_disconnect_by_data (GTK_OBJECT (priv->dn_query), gcal);
		gtk_object_unref (GTK_OBJECT (priv->dn_query));
		priv->dn_query = NULL;
	}

	if (priv->sexp) {
		g_free (priv->sexp);
		priv->sexp = NULL;
	}

	if (priv->client) {
		gtk_object_unref (GTK_OBJECT (priv->client));
		priv->client = NULL;
	}

	if (priv->task_pad_client) {
		gtk_object_unref (GTK_OBJECT (priv->task_pad_client));
		priv->task_pad_client = NULL;
	}

	priv->in_destroy = TRUE;
	g_hash_table_foreach (priv->object_editor_hash, destroy_editor_cb, NULL);
	g_hash_table_destroy (priv->object_editor_hash);
	priv->object_editor_hash = NULL;

	if (priv->view_collection) {
		gtk_object_unref (GTK_OBJECT (priv->view_collection));
		priv->view_collection = NULL;
	}

	if (priv->view_menus) {
		gtk_object_unref (GTK_OBJECT (priv->view_menus));
		priv->view_menus = NULL;
	}

	g_free (priv);
	gcal->priv = NULL;

	if (GTK_OBJECT_CLASS (parent_class)->destroy)
		(* GTK_OBJECT_CLASS (parent_class)->destroy) (object);
}

void
gnome_calendar_goto (GnomeCalendar *gcal, time_t new_time)
{
	GnomeCalendarPrivate *priv;

	g_return_if_fail (gcal != NULL);
	g_return_if_fail (GNOME_IS_CALENDAR (gcal));
	g_return_if_fail (new_time != -1);

	priv = gcal->priv;

	priv->selection_start_time = time_day_begin_with_zone (new_time,
							       priv->zone);
	priv->selection_end_time = time_add_day_with_zone (priv->selection_start_time, 1, priv->zone);

	gnome_calendar_update_view_times (gcal);
	gnome_calendar_update_date_navigator (gcal);
	gnome_calendar_notify_dates_shown_changed (gcal);
}


static void
gnome_calendar_update_view_times (GnomeCalendar *gcal)
{
	GnomeCalendarPrivate *priv;

	priv = gcal->priv;

	switch (priv->current_view_type) {
	case GNOME_CAL_DAY_VIEW:
		e_day_view_set_selected_time_range (E_DAY_VIEW (priv->day_view),
						    priv->selection_start_time,
						    priv->selection_end_time);
		break;

	case GNOME_CAL_WORK_WEEK_VIEW:
		e_day_view_set_selected_time_range (E_DAY_VIEW (priv->work_week_view),
						    priv->selection_start_time,
						    priv->selection_end_time);
		break;

	case GNOME_CAL_WEEK_VIEW:
		e_week_view_set_selected_time_range (E_WEEK_VIEW (priv->week_view),
						     priv->selection_start_time,
						     priv->selection_end_time);
		break;

	case GNOME_CAL_MONTH_VIEW:
		e_week_view_set_selected_time_range (E_WEEK_VIEW (priv->month_view),
						     priv->selection_start_time,
						     priv->selection_end_time);
		break;

	default:
		g_warning ("My penguin is gone!");
		g_assert_not_reached ();
	}
}

static void
gnome_calendar_direction (GnomeCalendar *gcal, int direction)
{
	GnomeCalendarPrivate *priv;
	time_t start_time, end_time;

	priv = gcal->priv;

	start_time = priv->selection_start_time;
	end_time = priv->selection_end_time;

	switch (priv->current_view_type) {
	case GNOME_CAL_DAY_VIEW:
		start_time = time_add_day_with_zone (start_time, direction,
						     priv->zone);
		end_time = time_add_day_with_zone (end_time, direction,
						   priv->zone);
		break;

	case GNOME_CAL_WORK_WEEK_VIEW:
		start_time = time_add_week_with_zone (start_time, direction,
						      priv->zone);
		end_time = time_add_week_with_zone (end_time, direction,
						    priv->zone);
		break;

	case GNOME_CAL_WEEK_VIEW:
		start_time = time_add_week_with_zone (start_time, direction,
						      priv->zone);
		end_time = time_add_week_with_zone (end_time, direction,
						    priv->zone);
		break;

	case GNOME_CAL_MONTH_VIEW:
		start_time = time_add_month_with_zone (start_time, direction,
						       priv->zone);
		end_time = time_add_month_with_zone (end_time, direction,
						     priv->zone);
		break;

	default:
		g_warning ("Weee!  Where did the penguin go?");
		g_assert_not_reached ();
		return;
	}

	priv->selection_start_time = start_time;
	priv->selection_end_time = end_time;

	gnome_calendar_update_view_times (gcal);
	gnome_calendar_update_date_navigator (gcal);
	gnome_calendar_notify_dates_shown_changed (gcal);
}

void
gnome_calendar_next (GnomeCalendar *gcal)
{
	g_return_if_fail (gcal != NULL);
	g_return_if_fail (GNOME_IS_CALENDAR (gcal));

	gnome_calendar_direction (gcal, 1);
}

void
gnome_calendar_previous (GnomeCalendar *gcal)
{
	g_return_if_fail (gcal != NULL);
	g_return_if_fail (GNOME_IS_CALENDAR (gcal));

	gnome_calendar_direction (gcal, -1);
}

void
gnome_calendar_dayjump (GnomeCalendar *gcal, time_t time)
{
	GnomeCalendarPrivate *priv;

	g_return_if_fail (gcal != NULL);
	g_return_if_fail (GNOME_IS_CALENDAR (gcal));

	priv = gcal->priv;

	priv->selection_start_time = time_day_begin_with_zone (time,
							       priv->zone);
	priv->selection_end_time = time_add_day_with_zone (priv->selection_start_time, 1, priv->zone);

	gnome_calendar_set_view (gcal, GNOME_CAL_DAY_VIEW, FALSE, TRUE);
}

static void
focus_current_view (GnomeCalendar *gcal)
{
	GnomeCalendarPrivate *priv;

	priv = gcal->priv;

	switch (priv->current_view_type) {
	case GNOME_CAL_DAY_VIEW:
		gtk_widget_grab_focus (priv->day_view);
		break;

	case GNOME_CAL_WORK_WEEK_VIEW:
		gtk_widget_grab_focus (priv->work_week_view);
		break;

	case GNOME_CAL_WEEK_VIEW:
		gtk_widget_grab_focus (priv->week_view);
		break;

	case GNOME_CAL_MONTH_VIEW:
		gtk_widget_grab_focus (priv->month_view);
		break;

	default:
		g_warning ("A penguin fell on its face!");
		g_assert_not_reached ();
	}
}

void
gnome_calendar_goto_today (GnomeCalendar *gcal)
{
	g_return_if_fail (gcal != NULL);
	g_return_if_fail (GNOME_IS_CALENDAR (gcal));

	gnome_calendar_goto (gcal, time (NULL));
	focus_current_view (gcal);
}

/**
 * gnome_calendar_get_view:
 * @gcal: A calendar.
 *
 * Queries the type of the view that is being shown in a calendar.
 *
 * Return value: Type of the view that is currently shown.
 **/
GnomeCalendarViewType
gnome_calendar_get_view (GnomeCalendar *gcal)
{
	GnomeCalendarPrivate *priv;

	g_return_val_if_fail (gcal != NULL, GNOME_CAL_DAY_VIEW);
	g_return_val_if_fail (GNOME_IS_CALENDAR (gcal), GNOME_CAL_DAY_VIEW);

	priv = gcal->priv;
	return priv->current_view_type;
}

/* Sets the view without changing the selection or updating the date
 * navigator. If a range of dates isn't selected it will also reset the number
 * of days/weeks shown to the default (i.e. 1 day for the day view or 5 weeks
 * for the month view).
 */
static void
set_view (GnomeCalendar	*gcal, GnomeCalendarViewType view_type,
	  gboolean range_selected, gboolean grab_focus)
{
	GnomeCalendarPrivate *priv;
	gboolean round_selection;
	GtkWidget *focus_widget;

	priv = gcal->priv;

	round_selection = FALSE;
	focus_widget = NULL;

	switch (view_type) {
	case GNOME_CAL_DAY_VIEW:
		focus_widget = priv->day_view;

		if (!range_selected)
			e_day_view_set_days_shown (E_DAY_VIEW (priv->day_view), 1);

		break;

	case GNOME_CAL_WORK_WEEK_VIEW:
		focus_widget = priv->work_week_view;
		break;

	case GNOME_CAL_WEEK_VIEW:
		focus_widget = priv->week_view;
		round_selection = TRUE;
		break;

	case GNOME_CAL_MONTH_VIEW:
		focus_widget = priv->month_view;

		if (!range_selected)
			e_week_view_set_weeks_shown (E_WEEK_VIEW (priv->month_view), 5);

		round_selection = TRUE;
		break;

	default:
		g_warning ("A penguin is loose!");
		g_assert_not_reached ();
		return;
	}

	priv->current_view_type = view_type;
	priv->range_selected = range_selected;

	g_assert (focus_widget != NULL);

	calendar_config_set_default_view (view_type);

	gtk_notebook_set_page (GTK_NOTEBOOK (priv->notebook), (int) view_type);

	if (grab_focus)
		gtk_widget_grab_focus (focus_widget);

	gnome_calendar_set_pane_positions (gcal);

	/* For the week & month views we want the selection in the date
	   navigator to be rounded to the nearest week when the arrow buttons
	   are pressed to move to the previous/next month. */
	gtk_object_set (GTK_OBJECT (priv->date_navigator->calitem),
			"round_selection_when_moving", round_selection,
			NULL);
}

/**
 * gnome_calendar_set_view:
 * @gcal: A calendar.
 * @view_type: Type of view to show.
 * @range_selected: If false, the range of days/weeks shown will be reset to the
 * default value (1 for day view, 5 for week view, respectively).  If true, the
 * currently displayed range will be kept.
 * @grab_focus: Whether the view widget should grab the focus.
 *
 * Sets the view that should be shown in a calendar.  If @reset_range is true,
 * this function will automatically set the number of days or weeks shown in
 * the view; otherwise the last configuration will be kept.
 **/
void
gnome_calendar_set_view (GnomeCalendar *gcal, GnomeCalendarViewType view_type,
			 gboolean range_selected, gboolean grab_focus)
{
	g_return_if_fail (gcal != NULL);
	g_return_if_fail (GNOME_IS_CALENDAR (gcal));

	set_view (gcal, view_type, range_selected, grab_focus);
	gnome_calendar_update_view_times (gcal);
	gnome_calendar_update_date_navigator (gcal);
	gnome_calendar_notify_dates_shown_changed (gcal);
}

/* Callback used when the view collection asks us to display a particular view */
static void
display_view_cb (GalViewCollection *view_collection, GalView *view, gpointer data)
{
	GnomeCalendar *gcal;
	CalendarView *cal_view;

	gcal = GNOME_CALENDAR (data);

	if (!IS_CALENDAR_VIEW (view))
		g_error ("display_view_cb(): Unknown type of view for GnomeCalendar");

	cal_view = CALENDAR_VIEW (view);

	gnome_calendar_set_view (gcal, calendar_view_get_view_type (cal_view), FALSE, TRUE);
}

/**
 * gnome_calendar_setup_view_menus:
 * @gcal: A calendar.
 * @uic: UI controller to use for the menus.
 * 
 * Sets up the #GalView menus for a calendar.  This function should be called
 * from the Bonobo control activation callback for this calendar.  Also, the
 * menus should be discarded using gnome_calendar_discard_view_menus().
 **/
void
gnome_calendar_setup_view_menus (GnomeCalendar *gcal, BonoboUIComponent *uic)
{
	GnomeCalendarPrivate *priv;
	char *path;
	CalendarViewFactory *factory;

	g_return_if_fail (gcal != NULL);
	g_return_if_fail (GNOME_IS_CALENDAR (gcal));
	g_return_if_fail (uic != NULL);
	g_return_if_fail (BONOBO_IS_UI_COMPONENT (uic));

	priv = gcal->priv;

	g_return_if_fail (priv->view_collection == NULL);

	g_assert (priv->view_collection == NULL);
	g_assert (priv->view_menus == NULL);

	/* Create the view collection */

	priv->view_collection = gal_view_collection_new ();

	path = gnome_util_prepend_user_home ("/evolution/views/calendar/");
	gal_view_collection_set_storage_directories (priv->view_collection,
						     EVOLUTION_DATADIR "/evolution/views/calendar/",
						     path);
	g_free (path);

	/* Create the views */

	factory = calendar_view_factory_new (GNOME_CAL_DAY_VIEW);
	gal_view_collection_add_factory (priv->view_collection, GAL_VIEW_FACTORY (factory));
	gtk_object_unref (GTK_OBJECT (factory));

	factory = calendar_view_factory_new (GNOME_CAL_WORK_WEEK_VIEW);
	gal_view_collection_add_factory (priv->view_collection, GAL_VIEW_FACTORY (factory));
	gtk_object_unref (GTK_OBJECT (factory));

	factory = calendar_view_factory_new (GNOME_CAL_WEEK_VIEW);
	gal_view_collection_add_factory (priv->view_collection, GAL_VIEW_FACTORY (factory));
	gtk_object_unref (GTK_OBJECT (factory));

	factory = calendar_view_factory_new (GNOME_CAL_MONTH_VIEW);
	gal_view_collection_add_factory (priv->view_collection, GAL_VIEW_FACTORY (factory));
	gtk_object_unref (GTK_OBJECT (factory));

	/* Load the collection and create the menus */

	gal_view_collection_load (priv->view_collection);

	priv->view_menus = gal_view_menus_new (priv->view_collection);
	gal_view_menus_apply (priv->view_menus, uic, NULL);
	gtk_signal_connect (GTK_OBJECT (priv->view_collection), "display_view",
			    GTK_SIGNAL_FUNC (display_view_cb), gcal);
}

/**
 * gnome_calendar_discard_view_menus:
 * @gcal: A calendar.
 * 
 * Discards the #GalView menus used by a calendar.  This function should be
 * called from the Bonobo control deactivation callback for this calendar.  The
 * menus should have been set up with gnome_calendar_setup_view_menus().
 **/
void
gnome_calendar_discard_view_menus (GnomeCalendar *gcal)
{
	GnomeCalendarPrivate *priv;

	g_return_if_fail (gcal != NULL);

	priv = gcal->priv;

	g_return_if_fail (priv->view_collection != NULL);

	g_assert (priv->view_collection != NULL);
	g_assert (priv->view_menus != NULL);

	gtk_object_unref (GTK_OBJECT (priv->view_collection));
	priv->view_collection = NULL;

	gtk_object_unref (GTK_OBJECT (priv->view_menus));
	priv->view_menus = NULL;
}

static void
gnome_calendar_set_pane_positions	(GnomeCalendar	*gcal)
{
	GnomeCalendarPrivate *priv;
	gint top_border, bottom_border, left_border, right_border;
	gint col_width, row_height;
	gfloat right_pane_width, top_pane_height;

	priv = gcal->priv;

	/* Get the size of the calendar month width & height. */
	e_calendar_get_border_size (priv->date_navigator,
				    &top_border, &bottom_border,
				    &left_border, &right_border);
	gtk_object_get (GTK_OBJECT (priv->date_navigator->calitem),
			"row_height", &row_height,
			"column_width", &col_width,
			NULL);

	if (priv->current_view_type == GNOME_CAL_MONTH_VIEW && !priv->range_selected) {
		right_pane_width = priv->hpane_pos_month_view;
		top_pane_height = priv->vpane_pos_month_view;
	} else {
		right_pane_width = priv->hpane_pos;
		top_pane_height = priv->vpane_pos;
	}

	/* We add the borders before multiplying due to the way we are using
	   the EPaned quantum feature. */
	if (right_pane_width < 0.001)
		right_pane_width = 0.0;
	else
		right_pane_width = (right_pane_width * (col_width + left_border + right_border)
				    + 0.5);
	if (top_pane_height < 0.001)
		top_pane_height = 0.0;
	else
		top_pane_height = (top_pane_height * (row_height + top_border + bottom_border)
				   + 0.5);

	e_paned_set_position (E_PANED (priv->hpane), -1);
	e_paned_set_position (E_PANED (priv->vpane), -1);

	/* We add one to each dimension since we can't use 0. */

	gtk_widget_set_usize (priv->vpane, right_pane_width + 1, -2);
	gtk_widget_set_usize (GTK_WIDGET (priv->date_navigator), -2, top_pane_height + 1);
}

/* Displays an error to indicate that opening a calendar failed */
static void
open_error (GnomeCalendar *gcal, const char *uri)
{
	char *msg;

	msg = g_strdup_printf (_("Could not open the folder in `%s'"), uri);
	gnome_error_dialog_parented (msg, GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (gcal))));
	g_free (msg);
}

/* Displays an error to indicate that the specified URI method is not supported */
static void
method_error (GnomeCalendar *gcal, const char *uri)
{
	char *msg;

	msg = g_strdup_printf (_("The method required to open `%s' is not supported"), uri);
	gnome_error_dialog_parented (msg, GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (gcal))));
	g_free (msg);
}

/* Callback from the calendar client when a calendar is loaded */
static void
client_cal_opened_cb (CalClient *client, CalClientOpenStatus status, gpointer data)
{
	GnomeCalendar *gcal;
	GnomeCalendarPrivate *priv;

	gcal = GNOME_CALENDAR (data);
	priv = gcal->priv;

	switch (status) {
	case CAL_CLIENT_OPEN_SUCCESS:
		if (client == priv->client)
			update_query (gcal);

		break;

	case CAL_CLIENT_OPEN_ERROR:
		open_error (gcal, cal_client_get_uri (client));
		break;

	case CAL_CLIENT_OPEN_NOT_FOUND:
		/* bullshit; we did not specify only_if_exists */
		g_assert_not_reached ();
		return;

	case CAL_CLIENT_OPEN_METHOD_NOT_SUPPORTED:
		method_error (gcal, cal_client_get_uri (client));
		break;

	default:
		g_assert_not_reached ();
		return;
	}
}

/* Duplicates an array of categories */
static GPtrArray *
copy_categories (GPtrArray *categories)
{
	GPtrArray *c;
	int i;

	c = g_ptr_array_new ();
	g_ptr_array_set_size (c, categories->len);

	for (i = 0; i < categories->len; i++)
		c->pdata[i] = g_strdup (categories->pdata[i]);

	return c;
}

/* Adds the categories from an array to a hash table if they don't exist there
 * already.
 */
static void
add_categories (GHashTable *categories, GPtrArray *c)
{
	int i;

	if (!c)
		return;

	for (i = 0; i < c->len; i++) {
		const char *cat;
		const char *str;

		cat = c->pdata[i];
		str = g_hash_table_lookup (categories, cat);

		if (!str)
			g_hash_table_insert (categories, (char *) cat, NULL);
	}
}

/* Used to append categories from a hash table to an array */
struct append_category_closure {
	GPtrArray *c;

	int i;
};

/* Appends a category from the hash table to the array */
static void
append_category_cb (gpointer key, gpointer value, gpointer data)
{
	struct append_category_closure *closure;
	const char *category;

	category = key;
	closure = data;

	closure->c->pdata[closure->i] = g_strdup (category);
	closure->i++;
}

/* Creates the union of two sets of categories */
static GPtrArray *
merge_categories (GPtrArray *a, GPtrArray *b)
{
	GHashTable *categories;
	int n;
	GPtrArray *c;
	struct append_category_closure closure;

	categories = g_hash_table_new (g_str_hash, g_str_equal);

	add_categories (categories, a);
	add_categories (categories, b);

	n = g_hash_table_size (categories);

	c = g_ptr_array_new ();
	g_ptr_array_set_size (c, n);

	closure.c = c;
	closure.i = 0;
	g_hash_table_foreach (categories, append_category_cb, &closure);
	g_hash_table_destroy (categories);

	return c;
}

/* Callback from the calendar client when the set of categories changes.  We
 * have to merge the categories of the calendar and tasks clients.
 */
static void
client_categories_changed_cb (CalClient *client, GPtrArray *categories, gpointer data)
{
	GnomeCalendar *gcal;
	GnomeCalendarPrivate *priv;
	GPtrArray *merged;

	gcal = GNOME_CALENDAR (data);
	priv = gcal->priv;

	if (client == priv->client) {
		free_categories (priv->cal_categories);
		priv->cal_categories = copy_categories (categories);
	} else if (client == priv->task_pad_client) {
		free_categories (priv->tasks_categories);
		priv->tasks_categories = copy_categories (categories);
	} else
		g_assert_not_reached ();

	merged = merge_categories (priv->cal_categories, priv->tasks_categories);
	cal_search_bar_set_categories (CAL_SEARCH_BAR (priv->search_bar), merged);
	free_categories (merged);
}

GtkWidget *
gnome_calendar_construct (GnomeCalendar *gcal)
{
	GnomeCalendarPrivate *priv;
	GnomeCalendarViewType view_type;
	CalendarModel *model;

	g_return_val_if_fail (gcal != NULL, NULL);
	g_return_val_if_fail (GNOME_IS_CALENDAR (gcal), NULL);

	priv = gcal->priv;

	/*
	 * Calendar Folder Client.
	 */
	priv->client = cal_client_new ();
	if (!priv->client)
		return NULL;

	gtk_signal_connect (GTK_OBJECT (priv->client), "cal_opened",
			    GTK_SIGNAL_FUNC (client_cal_opened_cb), gcal);
	gtk_signal_connect (GTK_OBJECT (priv->client), "categories_changed",
			    GTK_SIGNAL_FUNC (client_categories_changed_cb), gcal);

	e_day_view_set_cal_client (E_DAY_VIEW (priv->day_view),
				   priv->client);
	e_day_view_set_cal_client (E_DAY_VIEW (priv->work_week_view),
				   priv->client);
	e_week_view_set_cal_client (E_WEEK_VIEW (priv->week_view),
				    priv->client);
	e_week_view_set_cal_client (E_WEEK_VIEW (priv->month_view),
				    priv->client);

	/*
	 * TaskPad Folder Client.
	 */
	priv->task_pad_client = cal_client_new ();
	if (!priv->task_pad_client)
		return NULL;

	gtk_signal_connect (GTK_OBJECT (priv->task_pad_client), "cal_opened",
			    GTK_SIGNAL_FUNC (client_cal_opened_cb), gcal);
	gtk_signal_connect (GTK_OBJECT (priv->task_pad_client), "categories_changed",
			    GTK_SIGNAL_FUNC (client_categories_changed_cb), gcal);

	model = e_calendar_table_get_model (E_CALENDAR_TABLE (priv->todo));
	g_assert (model != NULL);

	calendar_model_set_cal_client (model, priv->task_pad_client, CALOBJ_TYPE_TODO);

	/* Get the default view to show. */
	view_type = calendar_config_get_default_view ();
	if (view_type < GNOME_CAL_DAY_VIEW || view_type > GNOME_CAL_MONTH_VIEW)
		view_type = GNOME_CAL_DAY_VIEW;

	gnome_calendar_set_view (gcal, view_type, FALSE, FALSE);

	return GTK_WIDGET (gcal);
}

GtkWidget *
gnome_calendar_new (void)
{
	GnomeCalendar *gcal;

	gcal = gtk_type_new (gnome_calendar_get_type ());

	if (!gnome_calendar_construct (gcal)) {
		g_message ("gnome_calendar_new(): Could not construct the calendar GUI");
		gtk_object_unref (GTK_OBJECT (gcal));
		return NULL;
	}

	return GTK_WIDGET (gcal);
}

/**
 * gnome_calendar_get_cal_client:
 * @gcal: A calendar view.
 *
 * Queries the calendar client interface object that a calendar view is using.
 *
 * Return value: A calendar client interface object.
 **/
CalClient *
gnome_calendar_get_cal_client (GnomeCalendar *gcal)
{
	GnomeCalendarPrivate *priv;

	g_return_val_if_fail (gcal != NULL, NULL);
	g_return_val_if_fail (GNOME_IS_CALENDAR (gcal), NULL);

	priv = gcal->priv;

	return priv->client;
}

/**
 * gnome_calendar_get_task_pad_cal_client:
 * @gcal: A calendar view.
 *
 * Queries the calendar client interface object that a calendar view is using
 * for the Task Pad.
 *
 * Return value: A calendar client interface object.
 **/
CalClient *
gnome_calendar_get_task_pad_cal_client (GnomeCalendar *gcal)
{
	GnomeCalendarPrivate *priv;

	g_return_val_if_fail (gcal != NULL, NULL);
	g_return_val_if_fail (GNOME_IS_CALENDAR (gcal), NULL);

	priv = gcal->priv;

	return priv->task_pad_client;
}

/* Adds the specified URI to the alarm notification service */
static void
add_alarms (const char *uri)
{
	CORBA_Environment ev;
	GNOME_Evolution_Calendar_AlarmNotify an;

	/* Activate the alarm notification service */

	CORBA_exception_init (&ev);
	an = oaf_activate_from_id ("OAFIID:GNOME_Evolution_Calendar_AlarmNotify", 0, NULL, &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {
		g_message ("add_alarms(): Could not activate the alarm notification service");
		CORBA_exception_free (&ev);
		return;
	}
	CORBA_exception_free (&ev);

	/* Ask the service to load the URI */

	CORBA_exception_init (&ev);
	GNOME_Evolution_Calendar_AlarmNotify_addCalendar (an, uri, &ev);

	if (ev._major == CORBA_USER_EXCEPTION) {
		char *ex_id;

		ex_id = CORBA_exception_id (&ev);
		if (strcmp (ex_id, ex_GNOME_Evolution_Calendar_AlarmNotify_InvalidURI) == 0)
			g_message ("add_calendar(): Invalid URI reported from the "
				   "alarm notification service");
		else if (strcmp (ex_id,
				 ex_GNOME_Evolution_Calendar_AlarmNotify_BackendContactError) == 0)
			g_message ("add_calendar(): The alarm notification service could "
				   "not contact the backend");
	} else if (ev._major != CORBA_NO_EXCEPTION)
		g_message ("add_calendar(): Could not issue the addCalendar request");

	CORBA_exception_free (&ev);

	/* Get rid of the service */

	CORBA_exception_init (&ev);
	bonobo_object_release_unref (an, &ev);
	if (ev._major != CORBA_NO_EXCEPTION)
		g_message ("add_alarms(): Could not unref the alarm notification service");

	CORBA_exception_free (&ev);
}

gboolean
gnome_calendar_open (GnomeCalendar *gcal, const char *str_uri)
{
	GnomeCalendarPrivate *priv;
	char *tasks_uri;
	gboolean success;
	GnomeVFSURI *uri;

	g_return_val_if_fail (gcal != NULL, FALSE);
	g_return_val_if_fail (GNOME_IS_CALENDAR (gcal), FALSE);
	g_return_val_if_fail (str_uri != NULL, FALSE);

	priv = gcal->priv;

	g_return_val_if_fail (
		cal_client_get_load_state (priv->client) == CAL_CLIENT_LOAD_NOT_LOADED,
		FALSE);

	g_return_val_if_fail (
		cal_client_get_load_state (priv->task_pad_client) == CAL_CLIENT_LOAD_NOT_LOADED,
		FALSE);

	if (!cal_client_open_calendar (priv->client, str_uri, FALSE)) {
		g_message ("gnome_calendar_open(): Could not issue the request");
		return FALSE;
	}

	add_alarms (str_uri);

	/* Open the appropriate Tasks folder to show in the TaskPad */

	uri = gnome_vfs_uri_new_private (str_uri, TRUE, TRUE, TRUE);
	if (!uri) {
		tasks_uri = g_strdup_printf ("%s/local/Tasks/tasks.ics", evolution_dir);
		success = cal_client_open_calendar (priv->task_pad_client, tasks_uri, FALSE);

		add_alarms (tasks_uri);
		g_free (tasks_uri);
	}
	else {
		if (!g_strcasecmp (uri->method_string, "file")) {
			tasks_uri = g_strdup_printf ("%s/local/Tasks/tasks.ics", evolution_dir);
			success = cal_client_open_calendar (priv->task_pad_client, tasks_uri, FALSE);

			add_alarms (tasks_uri);
			g_free (tasks_uri);
		}
		else {
			CalendarModel *model;

			/* we use the same CalClient for tasks than for events */
			gtk_object_unref (GTK_OBJECT (priv->task_pad_client));
			gtk_object_ref (GTK_OBJECT (priv->client));
			priv->task_pad_client = priv->client;

			gtk_signal_connect (GTK_OBJECT (priv->task_pad_client), "cal_opened",
					    GTK_SIGNAL_FUNC (client_cal_opened_cb), gcal);
			gtk_signal_connect (GTK_OBJECT (priv->task_pad_client), "categories_changed",
					    GTK_SIGNAL_FUNC (client_categories_changed_cb), gcal);

			model = e_calendar_table_get_model (E_CALENDAR_TABLE (priv->todo));
			g_assert (model != NULL);

			calendar_model_set_cal_client (model, priv->task_pad_client, CALOBJ_TYPE_TODO);

			success = TRUE;
		}

		gnome_vfs_uri_unref (uri);
	}

	if (!success) {
		g_message ("gnome_calendar_open(): Could not issue the request");
		return FALSE;
	}

	return TRUE;
}

/* Tells the calendar to reload all config settings.
   If initializing is TRUE it sets the pane positions as well. (We don't
   want to reset the pane positions after the user clicks 'Apply' in the
   preferences dialog.) */
void
gnome_calendar_update_config_settings (GnomeCalendar *gcal,
				       gboolean	      initializing)
{
	GnomeCalendarPrivate *priv;
	CalWeekdays working_days;
	gint week_start_day, time_divisions;
	gint start_hour, start_minute, end_hour, end_minute;
	gboolean use_24_hour, show_event_end, compress_weekend;
	char *location;

	g_return_if_fail (GNOME_IS_CALENDAR (gcal));

	priv = gcal->priv;

	working_days = calendar_config_get_working_days ();
	/* CalWeekdays and EDayViewDays use the same bit-masks, so we can
	   use the same value. */
	e_day_view_set_working_days (E_DAY_VIEW (priv->day_view),
				     (EDayViewDays) working_days);
	e_day_view_set_working_days (E_DAY_VIEW (priv->work_week_view),
				     (EDayViewDays) working_days);

	/* Note that this is 0 (Sun) to 6 (Sat). */
	week_start_day = calendar_config_get_week_start_day ();

	/* Convert it to 0 (Mon) to 6 (Sun), which is what we use. */
	week_start_day = (week_start_day + 6) % 7;

	e_day_view_set_week_start_day (E_DAY_VIEW (priv->day_view),
					week_start_day);
	e_day_view_set_week_start_day (E_DAY_VIEW (priv->work_week_view),
					week_start_day);
	e_week_view_set_week_start_day (E_WEEK_VIEW (priv->week_view),
					week_start_day);
	e_week_view_set_week_start_day (E_WEEK_VIEW (priv->month_view),
					week_start_day);

	start_hour = calendar_config_get_day_start_hour ();
	start_minute = calendar_config_get_day_start_minute ();
	end_hour = calendar_config_get_day_end_hour ();
	end_minute = calendar_config_get_day_end_minute ();
	e_day_view_set_working_day (E_DAY_VIEW (priv->day_view),
				    start_hour, start_minute,
				    end_hour, end_minute);
	e_day_view_set_working_day (E_DAY_VIEW (priv->work_week_view),
				    start_hour, start_minute,
				    end_hour, end_minute);

	use_24_hour = calendar_config_get_24_hour_format ();
	e_day_view_set_24_hour_format (E_DAY_VIEW (priv->day_view),
				       use_24_hour);
	e_day_view_set_24_hour_format (E_DAY_VIEW (priv->work_week_view),
				       use_24_hour);
	e_week_view_set_24_hour_format (E_WEEK_VIEW (priv->week_view),
					use_24_hour);
	e_week_view_set_24_hour_format (E_WEEK_VIEW (priv->month_view),
					use_24_hour);

	time_divisions = calendar_config_get_time_divisions ();
	e_day_view_set_mins_per_row (E_DAY_VIEW (priv->day_view),
				     time_divisions);
	e_day_view_set_mins_per_row (E_DAY_VIEW (priv->work_week_view),
				     time_divisions);

	show_event_end = calendar_config_get_show_event_end ();
	e_day_view_set_show_event_end_times (E_DAY_VIEW (priv->day_view),
					     show_event_end);
	e_day_view_set_show_event_end_times (E_DAY_VIEW (priv->work_week_view),
					     show_event_end);
	e_week_view_set_show_event_end_times (E_WEEK_VIEW (priv->week_view),
					      show_event_end);
	e_week_view_set_show_event_end_times (E_WEEK_VIEW (priv->month_view),
					      show_event_end);

	compress_weekend = calendar_config_get_compress_weekend ();
	e_week_view_set_compress_weekend (E_WEEK_VIEW (priv->month_view),
					  compress_weekend);

	calendar_config_configure_e_calendar (E_CALENDAR (priv->date_navigator));

	calendar_config_configure_e_calendar_table (E_CALENDAR_TABLE (priv->todo));

	location = calendar_config_get_timezone ();
	priv->zone = icaltimezone_get_builtin_timezone (location);

	e_day_view_set_timezone (E_DAY_VIEW (priv->day_view), priv->zone);
	e_day_view_set_timezone (E_DAY_VIEW (priv->work_week_view), priv->zone);
	e_week_view_set_timezone (E_WEEK_VIEW (priv->week_view), priv->zone);
	e_week_view_set_timezone (E_WEEK_VIEW (priv->month_view), priv->zone);

	if (initializing) {
		priv->hpane_pos = calendar_config_get_hpane_pos ();
		priv->vpane_pos = calendar_config_get_vpane_pos ();
		priv->hpane_pos_month_view = calendar_config_get_month_hpane_pos ();
		priv->vpane_pos_month_view = calendar_config_get_month_vpane_pos ();
	} else {
		gnome_calendar_update_paned_quanta (gcal);
	}

	/* The range of days shown may have changed, so we update the date
	   navigator if needed. */
	gnome_calendar_update_date_navigator (gcal);
	gnome_calendar_notify_dates_shown_changed (gcal);
}


void
gnome_calendar_set_selected_time_range (GnomeCalendar *gcal,
					time_t	       start_time,
					time_t	       end_time)
{
	GnomeCalendarPrivate *priv;

	priv = gcal->priv;

	priv->selection_start_time = start_time;
	priv->selection_end_time = end_time;

	gnome_calendar_update_date_navigator (gcal);
	gnome_calendar_notify_dates_shown_changed (gcal);
}

/**
 * gnome_calendar_get_selected_time_range:
 * @gcal: A calendar view.
 * @start_time: Return value for the start of the time selection.
 * @end_time: Return value for the end of the time selection.
 *
 * Queries the time selection range on the calendar view.
 **/
void
gnome_calendar_get_selected_time_range (GnomeCalendar *gcal,
					time_t	 *start_time,
					time_t	 *end_time)
{
	GnomeCalendarPrivate *priv;

	g_return_if_fail (gcal != NULL);
	g_return_if_fail (GNOME_IS_CALENDAR (gcal));

	priv = gcal->priv;

	if (start_time)
		*start_time = priv->selection_start_time;

	if (end_time)
		*end_time = priv->selection_end_time;
}


/* Callback used when an event editor dialog is closed */
struct editor_closure
{
	GnomeCalendar *gcal;
	char *uid;
};

static void
editor_closed_cb (GtkWidget *widget, gpointer data)
{
	GnomeCalendar *gcal;
	GnomeCalendarPrivate *priv;
	struct editor_closure *ec;
	gboolean result;
	gpointer orig_key;
	char *orig_uid;

	ec = (struct editor_closure *) data;
	gcal = ec->gcal;
	priv = gcal->priv;

	result = g_hash_table_lookup_extended (priv->object_editor_hash, ec->uid, &orig_key, NULL);
	g_assert (result != FALSE);

	orig_uid = orig_key;

	if (!priv->in_destroy)
		g_hash_table_remove (priv->object_editor_hash, orig_uid);

	g_free (orig_uid);

	g_free (ec);
}

void
gnome_calendar_edit_object (GnomeCalendar *gcal, CalComponent *comp)
{
	GnomeCalendarPrivate *priv;
	EventEditor *ee;
	struct editor_closure *ec;
	const char *uid;

	g_return_if_fail (gcal != NULL);
	g_return_if_fail (GNOME_IS_CALENDAR (gcal));
	g_return_if_fail (comp != NULL);

	priv = gcal->priv;

	cal_component_get_uid (comp, &uid);

	ee = g_hash_table_lookup (priv->object_editor_hash, uid);
	if (!ee) {
		ec = g_new0 (struct editor_closure, 1);

		ee = event_editor_new ();
		if (!ee) {
			g_message ("gnome_calendar_edit_object(): Could not create the event editor");
			return;
		}

		ec->gcal = gcal;
		ec->uid = g_strdup (uid);

		g_hash_table_insert (priv->object_editor_hash, ec->uid, ee);

		gtk_signal_connect (GTK_OBJECT (ee), "destroy",
				    GTK_SIGNAL_FUNC (editor_closed_cb),
				    ec);

		comp_editor_set_cal_client (COMP_EDITOR (ee), priv->client);
		comp_editor_edit_comp (COMP_EDITOR (ee), comp);
	}

	comp_editor_focus (COMP_EDITOR (ee));
}

/**
 * gnome_calendar_new_appointment_for:
 * @gcal: An Evolution calendar.
 * @dtstart: a Unix time_t that marks the beginning of the appointment.
 * @dtend: a Unix time_t that marks the end of the appointment.
 * @all_day: if true, the dtstart and dtend are expanded to cover the entire
 * day, and the event is set to TRANSPARENT.
 *
 * Opens an event editor dialog for a new appointment.
 *
 **/
void
gnome_calendar_new_appointment_for (GnomeCalendar *cal,
				    time_t dtstart, time_t dtend,
				    gboolean all_day)
{
	GnomeCalendarPrivate *priv;
	struct icaltimetype itt;
	CalComponentDateTime dt;
	CalComponent *comp;
	CalComponentTransparency transparency;
	const char *category;

	g_return_if_fail (cal != NULL);
	g_return_if_fail (GNOME_IS_CALENDAR (cal));

	priv = cal->priv;

	dt.value = &itt;
	dt.tzid = icaltimezone_get_tzid (priv->zone);

	/* Component type */

	comp = cal_component_new ();
	cal_component_set_new_vtype (comp, CAL_COMPONENT_EVENT);

	/* DTSTART, DTEND */

	itt = icaltime_from_timet_with_zone (dtstart, FALSE, priv->zone);
	if (all_day)
		itt.hour = itt.minute = itt.second = 0;
	cal_component_set_dtstart (comp, &dt);

	itt = icaltime_from_timet_with_zone (dtend, FALSE, priv->zone);
	if (all_day) {
		/* If we want an all-day event and the end time isn't on a
		   day boundary, we move it to the end of the day it is in. */
		if (itt.hour != 0 || itt.minute != 0 || itt.second != 0) {
			itt.hour = itt.minute = itt.second = 0;
			icaltime_adjust (&itt, 1, 0, 0, 0);
		}
	}
	cal_component_set_dtend (comp, &dt);

	transparency = all_day ? CAL_COMPONENT_TRANSP_TRANSPARENT
		: CAL_COMPONENT_TRANSP_OPAQUE;
	cal_component_set_transparency (comp, transparency);


	/* Category */

	category = cal_search_bar_get_category (CAL_SEARCH_BAR (priv->search_bar));
	cal_component_set_categories (comp, category);

	/* Edit! */

	cal_component_commit_sequence (comp);

	gnome_calendar_edit_object (cal, comp);
	gtk_object_unref (GTK_OBJECT (comp));
}

/**
 * gnome_calendar_new_appointment:
 * @gcal: An Evolution calendar.
 *
 * Opens an event editor dialog for a new appointment.  The appointment's start
 * and end times are set to the currently selected time range in the calendar
 * views.
 **/
void
gnome_calendar_new_appointment (GnomeCalendar *gcal)
{
	time_t dtstart, dtend;

	g_return_if_fail (gcal != NULL);
	g_return_if_fail (GNOME_IS_CALENDAR (gcal));

	gnome_calendar_get_current_time_range (gcal, &dtstart, &dtend);
	gnome_calendar_new_appointment_for (gcal, dtstart, dtend, FALSE);
}

/**
 * gnome_calendar_new_task:
 * @gcal: An Evolution calendar.
 *
 * Opens a task editor dialog for a new task.
 **/
void
gnome_calendar_new_task		(GnomeCalendar *gcal)
{
	GnomeCalendarPrivate *priv;
	TaskEditor *tedit;
	CalComponent *comp;

	g_print ("In gnome_calendar_new_task\n");

	g_return_if_fail (gcal != NULL);
	g_return_if_fail (GNOME_IS_CALENDAR (gcal));

	priv = gcal->priv;

	tedit = task_editor_new ();
	comp_editor_set_cal_client (COMP_EDITOR (tedit), priv->task_pad_client);

	comp = cal_component_new ();
	cal_component_set_new_vtype (comp, CAL_COMPONENT_TODO);

	comp_editor_edit_comp (COMP_EDITOR (tedit), comp);
	gtk_object_unref (GTK_OBJECT (comp));

	comp_editor_focus (COMP_EDITOR (tedit));
}


/* Returns the selected time range for the current view. Note that this may be
   different from the fields in the GnomeCalendar, since the view may clip
   this or choose a more appropriate time. */
void
gnome_calendar_get_current_time_range (GnomeCalendar *gcal,
				       time_t	 *start_time,
				       time_t	 *end_time)
{
	GnomeCalendarPrivate *priv;

	priv = gcal->priv;

	switch (priv->current_view_type) {
	case GNOME_CAL_DAY_VIEW:
		e_day_view_get_selected_time_range (E_DAY_VIEW (priv->day_view),
						    start_time, end_time);
		break;

	case GNOME_CAL_WORK_WEEK_VIEW:
		e_day_view_get_selected_time_range (E_DAY_VIEW (priv->work_week_view),
						    start_time, end_time);
		break;

	case GNOME_CAL_WEEK_VIEW:
		e_week_view_get_selected_time_range (E_WEEK_VIEW (priv->week_view),
						     start_time, end_time);
		break;

	case GNOME_CAL_MONTH_VIEW:
		e_week_view_get_selected_time_range (E_WEEK_VIEW (priv->month_view),
						     start_time, end_time);
		break;

	default:
		g_message ("My penguin is gone!");
		g_assert_not_reached ();
	}
}


/* Gets the visible time range for the current view. Returns FALSE if no
   time range has been set yet. */
gboolean
gnome_calendar_get_visible_time_range (GnomeCalendar *gcal,
				       time_t	 *start_time,
				       time_t	 *end_time)
{
	GnomeCalendarPrivate *priv;
	gboolean retval = FALSE;

	g_return_val_if_fail (GNOME_IS_CALENDAR (gcal), FALSE);

	priv = gcal->priv;

	switch (priv->current_view_type) {
	case GNOME_CAL_DAY_VIEW:
		retval = e_day_view_get_visible_time_range (E_DAY_VIEW (priv->day_view), start_time, end_time);
		break;

	case GNOME_CAL_WORK_WEEK_VIEW:
		retval = e_day_view_get_visible_time_range (E_DAY_VIEW (priv->work_week_view), start_time, end_time);
		break;

	case GNOME_CAL_WEEK_VIEW:
		retval = e_week_view_get_visible_time_range (E_WEEK_VIEW (priv->week_view), start_time, end_time);
		break;

	case GNOME_CAL_MONTH_VIEW:
		retval = e_week_view_get_visible_time_range (E_WEEK_VIEW (priv->month_view), start_time, end_time);
		break;

	default:
		g_assert_not_reached ();
	}

	return retval;
}



static void
get_days_shown (GnomeCalendar *gcal, GDate *start_date, gint *days_shown)
{
	GnomeCalendarPrivate *priv;

	priv = gcal->priv;

	switch (priv->current_view_type) {
	case GNOME_CAL_DAY_VIEW:
		time_to_gdate_with_zone (start_date,
					 E_DAY_VIEW (priv->day_view)->lower,
					 priv->zone);
		*days_shown = e_day_view_get_days_shown (E_DAY_VIEW (priv->day_view));
		break;

	case GNOME_CAL_WORK_WEEK_VIEW:
		time_to_gdate_with_zone (start_date,
					 E_DAY_VIEW (priv->work_week_view)->lower,
					 priv->zone);
		*days_shown = e_day_view_get_days_shown (E_DAY_VIEW (priv->work_week_view));
		break;

	case GNOME_CAL_WEEK_VIEW:
		*start_date = E_WEEK_VIEW (priv->week_view)->first_day_shown;
		if (e_week_view_get_multi_week_view (E_WEEK_VIEW (priv->week_view)))
			*days_shown = e_week_view_get_weeks_shown (
				E_WEEK_VIEW (priv->week_view)) * 7;
		else
			*days_shown = 7;

		break;

	case GNOME_CAL_MONTH_VIEW:
		*start_date = E_WEEK_VIEW (priv->month_view)->first_day_shown;
		if (e_week_view_get_multi_week_view (E_WEEK_VIEW (priv->month_view)))
			*days_shown = e_week_view_get_weeks_shown (
				E_WEEK_VIEW (priv->month_view)) * 7;
		else
			*days_shown = 7;

		break;

	default:
		g_assert_not_reached ();
	}
}


/* This updates the month shown and the days selected in the calendar, if
   necessary. */
static void
gnome_calendar_update_date_navigator (GnomeCalendar *gcal)
{
	GnomeCalendarPrivate *priv;
	GDate start_date, end_date;
	gint days_shown;

	priv = gcal->priv;

	/* If the ECalendar isn't visible, we just return. */
	if (!GTK_WIDGET_VISIBLE (priv->date_navigator))
		return;

	get_days_shown (gcal, &start_date, &days_shown);

	end_date = start_date;
	g_date_add_days (&end_date, days_shown - 1);

	g_print ("Updating date navigator selection\n");
	e_calendar_item_set_selection (priv->date_navigator->calitem,
				       &start_date, &end_date);
}


static void
gnome_calendar_on_date_navigator_selection_changed (ECalendarItem    *calitem,
						    GnomeCalendar    *gcal)
{
	GnomeCalendarPrivate *priv;
	GDate start_date, end_date, new_start_date, new_end_date;
	gint days_shown, new_days_shown;
	gboolean starts_on_week_start_day;

	priv = gcal->priv;

	starts_on_week_start_day = FALSE;

	get_days_shown (gcal, &start_date, &days_shown);

	end_date = start_date;
	g_date_add_days (&end_date, days_shown - 1);

	e_calendar_item_get_selection (calitem, &new_start_date, &new_end_date);

	/* If the selection hasn't changed just return. */
	if (!g_date_compare (&start_date, &new_start_date)
	    && !g_date_compare (&end_date, &new_end_date))
		return;

	new_days_shown = g_date_julian (&new_end_date) - g_date_julian (&new_start_date) + 1;

	/* If a complete week is selected we show the Week view.
	   Note that if weekends are compressed and the week start day is set
	   to Sunday we don't actually show complete weeks in the Week view,
	   so this may need tweaking. */
	if (g_date_weekday (&new_start_date) % 7 == calendar_config_get_week_start_day ())
		starts_on_week_start_day = TRUE;

	/* Switch views as appropriate, and change the number of days or weeks
	   shown. */
	if (new_days_shown > 9) {
		e_week_view_set_weeks_shown (E_WEEK_VIEW (priv->month_view),
					     (new_days_shown + 6) / 7);
		e_week_view_set_first_day_shown (E_WEEK_VIEW (priv->month_view), &new_start_date);

		set_view (gcal, GNOME_CAL_MONTH_VIEW, TRUE, FALSE);
		gnome_calendar_update_date_navigator (gcal);
		gnome_calendar_notify_dates_shown_changed (gcal);
	} else if (new_days_shown == 7 && starts_on_week_start_day) {
		e_week_view_set_first_day_shown (E_WEEK_VIEW (priv->week_view), &new_start_date);

		set_view (gcal, GNOME_CAL_WEEK_VIEW, TRUE, FALSE);
		gnome_calendar_update_date_navigator (gcal);
		gnome_calendar_notify_dates_shown_changed (gcal);
	} else {
		gint start_year, start_month, start_day;
		gint end_year, end_month, end_day;
		struct icaltimetype tt;

		start_year = g_date_year (&new_start_date);
		start_month = g_date_month (&new_start_date);
		start_day = g_date_day (&new_start_date);
		end_year = g_date_year (&new_end_date);
		end_month = g_date_month (&new_end_date);
		end_day = g_date_day (&new_end_date);

		tt = icaltime_null_time ();
		tt.year = start_year;
		tt.month  = start_month;
		tt.day = start_day;
		priv->selection_start_time = icaltime_as_timet_with_zone (tt, priv->zone);

		tt = icaltime_null_time ();
		tt.year = end_year;
		tt.month  = end_month;
		tt.day = end_day;
		icaltime_adjust (&tt, 1, 0, 0, 0);
		priv->selection_end_time = icaltime_as_timet_with_zone (tt, priv->zone);

		e_day_view_set_days_shown (E_DAY_VIEW (priv->day_view), new_days_shown);
		gnome_calendar_set_view (gcal, GNOME_CAL_DAY_VIEW, TRUE, FALSE);
	}

	focus_current_view (gcal);
}


static void
gnome_calendar_on_date_navigator_date_range_changed (ECalendarItem *calitem,
						     GnomeCalendar *gcal)
{
	g_print ("In gnome_calendar_on_date_navigator_date_range_changed\n");
	update_query (gcal);
}


static void
gnome_calendar_on_date_navigator_style_set (GtkWidget     *widget,
					    GtkStyle      *previous_style,
					    gpointer       data)
{
	gnome_calendar_update_paned_quanta (GNOME_CALENDAR (data));
}


static void
gnome_calendar_update_paned_quanta (GnomeCalendar	*gcal)
{
	GnomeCalendarPrivate *priv;
	gint row_height, col_width;
	gint top_border, bottom_border, left_border, right_border;

	priv = gcal->priv;

	e_calendar_get_border_size (priv->date_navigator,
				    &top_border, &bottom_border,
				    &left_border, &right_border);
	gtk_object_get (GTK_OBJECT (priv->date_navigator->calitem),
			"row_height", &row_height,
			"column_width", &col_width,
			NULL);

	/* The EPaned quantum feature works better if we add on the calendar
	   borders to the quantum size. Otherwise if you shrink the date
	   navigator you get left with the border widths/heights which looks
	   bad. EPaned should be more flexible really. */
	col_width += left_border + right_border;
	row_height += top_border + bottom_border;

	/* We don't have to use the EPaned quantum feature. We could just let
	   the calendar expand to fill the allocated space, showing as many
	   months as will fit. But for that to work nicely the EPaned should
	   resize the widgets as the bar is dragged. Otherwise the user has
	   to mess around to get the number of months that they want. */
#if 1
	gtk_object_set (GTK_OBJECT (priv->hpane),
			"quantum", (guint) col_width,
			NULL);
	gtk_object_set (GTK_OBJECT (priv->vpane),
			"quantum", (guint) row_height,
			NULL);
#endif

	gnome_calendar_set_pane_positions (gcal);
}


static void
gnome_calendar_on_date_navigator_size_allocate (GtkWidget     *widget,
						GtkAllocation *allocation,
						gpointer data)
{
	GnomeCalendar *gcal;
	GnomeCalendarPrivate *priv;
	gint width, height, row_height, col_width;
	gint top_border, bottom_border, left_border, right_border;
	gfloat hpane_pos, vpane_pos;

	gcal = GNOME_CALENDAR (data);
	priv = gcal->priv;

	e_calendar_get_border_size (priv->date_navigator,
				    &top_border, &bottom_border,
				    &left_border, &right_border);
	gtk_object_get (GTK_OBJECT (priv->date_navigator->calitem),
			"row_height", &row_height,
			"column_width", &col_width,
			NULL);

	/* We subtract one from each dimension since we added 1 in
	   set_view(). */
	width = allocation->width - 1;
	height = allocation->height - 1;

	/* We add the border sizes to work around the EPaned
	   quantized feature. */
	col_width += left_border + right_border;
	row_height += top_border + bottom_border;

	hpane_pos = (gfloat) width / col_width;
	vpane_pos = (gfloat) height / row_height;

	if (priv->current_view_type == GNOME_CAL_MONTH_VIEW && !priv->range_selected) {
		priv->hpane_pos_month_view = hpane_pos;
		priv->vpane_pos_month_view = vpane_pos;
		calendar_config_set_month_hpane_pos (hpane_pos);
		calendar_config_set_month_vpane_pos (vpane_pos);
	} else {
		priv->hpane_pos = hpane_pos;
		priv->vpane_pos = vpane_pos;
		calendar_config_set_hpane_pos (hpane_pos);
		calendar_config_set_vpane_pos (vpane_pos);
	}
}

void
gnome_calendar_cut_clipboard (GnomeCalendar *gcal)
{
	GnomeCalendarPrivate *priv;

	priv = gcal->priv;

	switch (priv->current_view_type) {
	case GNOME_CAL_DAY_VIEW :
		e_day_view_cut_clipboard (E_DAY_VIEW (priv->day_view));
		break;
	case GNOME_CAL_WORK_WEEK_VIEW :
		e_day_view_cut_clipboard (E_DAY_VIEW (priv->work_week_view));
		break;
	case GNOME_CAL_WEEK_VIEW :
		e_week_view_cut_clipboard (E_WEEK_VIEW (priv->week_view));
		break;
	case GNOME_CAL_MONTH_VIEW :
		e_week_view_cut_clipboard (E_WEEK_VIEW (priv->month_view));
		break;
	default:
		g_assert_not_reached ();
	}
}

void
gnome_calendar_copy_clipboard (GnomeCalendar *gcal)
{
	GnomeCalendarPrivate *priv;

	priv = gcal->priv;

	switch (priv->current_view_type) {
	case GNOME_CAL_DAY_VIEW :
		e_day_view_copy_clipboard (E_DAY_VIEW (priv->day_view));
		break;
	case GNOME_CAL_WORK_WEEK_VIEW :
		e_day_view_copy_clipboard (E_DAY_VIEW (priv->work_week_view));
		break;
	case GNOME_CAL_WEEK_VIEW :
		e_week_view_copy_clipboard (E_WEEK_VIEW (priv->week_view));
		break;
	case GNOME_CAL_MONTH_VIEW :
		e_week_view_copy_clipboard (E_WEEK_VIEW (priv->month_view));
		break;
	default:
		g_assert_not_reached ();
	}
}

void
gnome_calendar_paste_clipboard (GnomeCalendar *gcal)
{
	GnomeCalendarPrivate *priv;

	priv = gcal->priv;

	switch (priv->current_view_type) {
	case GNOME_CAL_DAY_VIEW :
		e_day_view_paste_clipboard (E_DAY_VIEW (priv->day_view));
		break;
	case GNOME_CAL_WORK_WEEK_VIEW :
		e_day_view_paste_clipboard (E_DAY_VIEW (priv->work_week_view));
		break;
	case GNOME_CAL_WEEK_VIEW :
		e_week_view_paste_clipboard (E_WEEK_VIEW (priv->week_view));
		break;
	case GNOME_CAL_MONTH_VIEW :
		e_week_view_paste_clipboard (E_WEEK_VIEW (priv->month_view));
		break;
	}
}


/* Get the current timezone. */
icaltimezone*
gnome_calendar_get_timezone	(GnomeCalendar	*gcal)
{
	g_return_val_if_fail (gcal != NULL, NULL);
	g_return_val_if_fail (GNOME_IS_CALENDAR (gcal), NULL);

	return gcal->priv->zone;
}


static void
gnome_calendar_notify_dates_shown_changed (GnomeCalendar *gcal)
{
	GnomeCalendarPrivate *priv;
	time_t start_time, end_time;

	g_return_if_fail (GNOME_IS_CALENDAR (gcal));

	priv = gcal->priv;

	gnome_calendar_get_visible_time_range (gcal, &start_time, &end_time);

	/* We check if the visible date range has changed, and only emit the
	   signal if it has. (This makes sure we only change the folder title
	   bar label in the shell when we need to.) */
	if (priv->visible_start != start_time
	    || priv->visible_end != end_time) {
		priv->visible_start = start_time;
		priv->visible_end = end_time;

		gtk_signal_emit (GTK_OBJECT (gcal),
				 gnome_calendar_signals[DATES_SHOWN_CHANGED]);
	}
}


/* Returns the number of selected events (0 or 1 at present). */
gint
gnome_calendar_get_num_events_selected (GnomeCalendar *gcal)
{
	GtkWidget *view;
	gint retval = 0;

	g_return_val_if_fail (GNOME_IS_CALENDAR (gcal), 0);

	view = gnome_calendar_get_current_view_widget (gcal);
	if (E_IS_DAY_VIEW (view))
		retval = e_day_view_get_num_events_selected (E_DAY_VIEW (view));
	else
		retval = e_week_view_get_num_events_selected (E_WEEK_VIEW (view));

	return retval;
}


void
gnome_calendar_delete_event		(GnomeCalendar  *gcal)
{
	GtkWidget *view;

	g_return_if_fail (GNOME_IS_CALENDAR (gcal));

	view = gnome_calendar_get_current_view_widget (gcal);
	if (E_IS_DAY_VIEW (view))
		e_day_view_delete_event (E_DAY_VIEW (view));
	else
		e_week_view_delete_event (E_WEEK_VIEW (view));
}


