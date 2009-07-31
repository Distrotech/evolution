/*
 * e-cal-shell-content.c
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#include "e-cal-shell-content.h"

#include <string.h>
#include <glib/gi18n.h>

#include "e-util/e-binding.h"
#include "e-util/gconf-bridge.h"

#include "calendar/gui/calendar-config.h"
#include "calendar/gui/calendar-view.h"
#include "calendar/gui/e-cal-list-view.h"
#include "calendar/gui/e-cal-model-calendar.h"
#include "calendar/gui/e-calendar-table.h"
#include "calendar/gui/e-calendar-table-config.h"
#include "calendar/gui/e-calendar-view.h"
#include "calendar/gui/e-memo-table-config.h"

#include "widgets/menus/gal-view-etable.h"

#define E_CAL_SHELL_CONTENT_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_CAL_SHELL_CONTENT, ECalShellContentPrivate))

struct _ECalShellContentPrivate {
	GtkWidget *hpaned;
	GtkWidget *notebook;
	GtkWidget *vpaned;

	GtkWidget *calendar;
	GtkWidget *task_table;
	GtkWidget *memo_table;

	ECalendarTableConfig *task_table_config;
	EMemoTableConfig *memo_table_config;

	GalViewInstance *view_instance;

	guint paned_binding_id;
};

enum {
	PROP_0
};

/* Used to indicate who has the focus within the calendar view. */
typedef enum {
	FOCUS_CALENDAR,
	FOCUS_MEMO_TABLE,
	FOCUS_TASK_TABLE,
	FOCUS_OTHER
} FocusLocation;

static gpointer parent_class;
static GType cal_shell_content_type;

static gboolean
transform_week_start_day (const GValue *src_value,
                          GValue *dst_value)
{
	gint v_int;

	/* Transform day numbering from 0 = Sunday to 0 = Monday. */
	v_int = g_value_get_int (src_value);
	g_value_set_int (dst_value, (v_int + 6) % 7);

	return TRUE;
}

static void
cal_shell_content_display_view_cb (ECalShellContent *cal_shell_content,
                                   GalView *gal_view)
{
	GnomeCalendar *calendar;
	CalendarView *gal_calendar_view;
	GnomeCalendarViewType view_type;

	/* XXX This is confusing: we have CalendarView and ECalendarView.
	 *     ECalendarView is an abstract base class for calendar view
	 *     widgets (day view, week view, etc).  CalendarView is a
	 *     simple GalView subclass that represents a calendar view. */

	calendar = e_cal_shell_content_get_calendar (cal_shell_content);

	if (GAL_IS_VIEW_ETABLE (gal_view)) {
		ECalendarView *calendar_view;
		ETable *table;

		view_type = GNOME_CAL_LIST_VIEW;
		calendar_view = gnome_calendar_get_calendar_view (
			calendar, view_type);
		table = e_table_scrolled_get_table (
			E_CAL_LIST_VIEW (calendar_view)->table_scrolled);
		gal_view_etable_attach_table (
			GAL_VIEW_ETABLE (gal_view), table);
	} else {
		view_type = calendar_view_get_view_type (
			CALENDAR_VIEW (gal_view));
	}
}

static void
cal_shell_content_notify_view_id_cb (ECalShellContent *cal_shell_content)
{
	EShellContent *shell_content;
	EShellView *shell_view;
	GConfBridge *bridge;
	GtkWidget *paned;
	guint binding_id;
	const gchar *key;
	const gchar *view_id;

	bridge = gconf_bridge_get ();
	paned = cal_shell_content->priv->hpaned;
	binding_id = cal_shell_content->priv->paned_binding_id;

	shell_content = E_SHELL_CONTENT (cal_shell_content);
	shell_view = e_shell_content_get_shell_view (shell_content);
	view_id = e_shell_view_get_view_id (shell_view);

	if (binding_id > 0)
		gconf_bridge_unbind (bridge, binding_id);

	if (view_id != NULL && strcmp (view_id, "Month_View") == 0)
		key = "/apps/evolution/calendar/display/month_hpane_position";
	else
		key = "/apps/evolution/calendar/display/hpane_position";

	binding_id = gconf_bridge_bind_property_delayed (
		bridge, key, G_OBJECT (paned), "position");

	cal_shell_content->priv->paned_binding_id = binding_id;
}

static FocusLocation
cal_shell_content_get_focus_location (ECalShellContent *cal_shell_content)
{
        return FOCUS_OTHER;
#if 0  /* TEMPORARILY DISABLED */
	GtkWidget *widget;
	GnomeCalendar *calendar;
	GnomeCalendarViewType view_type;
	ECalendarView calendar_view;
	ECalendarTable *task_table;
	EMemoTable *memo_table;
	ETable *table;

	calendar = GNOME_CALENDAR (cal_shell_content->priv->calendar);
	view_type = gnome_calendar_get_view (calendar);
	calendar_view = gnome_calendar_get_calendar_view (calendar, view_type);

	memo_table = E_MEMO_TABLE (cal_shell_content->priv->memo_table);
	task_table = E_CALENDAR_TABLE (cal_shell_content->priv->task_table);

	table = e_memo_table_get_table (memo_table);
	if (GTK_WIDGET_HAS_FOCUS (table->table_canvas))
		return FOCUS_MEMO_TABLE;

	table = e_calendar_table_get_table (task_table);
	if (GTK_WIDGET_HAS_FOCUS (table->table_canvas))
		return FOCUS_TASK_TABLE;

	if (E_IS_DAY_VIEW (calendar_view)) {
		EDayView *day_view = E_DAY_VIEW (calendar_view);

		if (GTK_WIDGET_HAS_FOCUS (day_view->top_canvas))
			return FOCUS_CALENDAR;

		if (GNOME_CANVAS (day_view->top_canvas)->focused_item != NULL)
			return FOCUS_CALENDAR;

		if (GTK_WIDGET_HAS_FOCUS (day_view->main_canvas))
			return FOCUS_CALENDAR;

		if (GNOME_CANVAS (day_view->main_canvas)->focused_item != NULL)
			return FOCUS_CALENDAR;

	} else if (E_IS_WEEK_VIEW (calendar_view)) {
		EWeekView *week_view = E_WEEK_VIEW (calendar_view);

		if (GTK_WIDGET_HAS_FOCUS (week_view->main_canvas))
			return FOCUS_CALENDAR;

		if (GNOME_CANVAS (week_view->main_canvas)->focused_item != NULL)
			return FOCUS_CALENDAR;

	} else if (E_IS_CAL_LIST_VIEW (calendar_view)) {
		ECalListView *list_view = E_CAL_LIST_VIEW (widget);

		table = e_table_scrolled_get_table (list_view->table_scrolled);
		if (GTK_WIDGET_HAS_FOCUS (table))
			return FOCUS_CALENDAR;
	}

	return FOCUS_OTHER;
#endif
}

static void
cal_shell_content_set_property (GObject *object,
                                guint property_id,
                                const GValue *value,
                                GParamSpec *pspec)
{
	switch (property_id) {
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
cal_shell_content_get_property (GObject *object,
                                guint property_id,
                                GValue *value,
                                GParamSpec *pspec)
{
	switch (property_id) {
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
cal_shell_content_dispose (GObject *object)
{
	ECalShellContentPrivate *priv;

	priv = E_CAL_SHELL_CONTENT_GET_PRIVATE (object);

	if (priv->hpaned != NULL) {
		g_object_unref (priv->hpaned);
		priv->hpaned = NULL;
	}

	if (priv->notebook != NULL) {
		g_object_unref (priv->notebook);
		priv->notebook = NULL;
	}

	if (priv->vpaned != NULL) {
		g_object_unref (priv->vpaned);
		priv->vpaned = NULL;
	}

	if (priv->calendar != NULL) {
		g_object_unref (priv->calendar);
		priv->calendar = NULL;
	}

	if (priv->task_table != NULL) {
		g_object_unref (priv->task_table);
		priv->task_table = NULL;
	}

	if (priv->memo_table != NULL) {
		g_object_unref (priv->memo_table);
		priv->memo_table = NULL;
	}

	if (priv->task_table_config != NULL) {
		g_object_unref (priv->task_table_config);
		priv->task_table_config = NULL;
	}

	if (priv->memo_table_config != NULL) {
		g_object_unref (priv->memo_table_config);
		priv->memo_table_config = NULL;
	}

	if (priv->view_instance != NULL) {
		g_object_unref (priv->view_instance);
		priv->view_instance = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
cal_shell_content_finalize (GObject *object)
{
	ECalShellContentPrivate *priv;

	priv = E_CAL_SHELL_CONTENT_GET_PRIVATE (object);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
cal_shell_content_constructed (GObject *object)
{
	ECalShellContentPrivate *priv;
	ECalendarView *calendar_view;
	ECalModel *calendar_model;
	ECalModel *memo_model;
	ECalModel *task_model;
	EShell *shell;
	EShellContent *shell_content;
	EShellBackend *shell_backend;
	EShellSettings *shell_settings;
	EShellView *shell_view;
	EShellWindow *shell_window;
	EShellContent *foreign_content;
	EShellView *foreign_view;
	GnomeCalendar *calendar;
	GnomeCalendarViewType view_type;
	GalViewInstance *view_instance;
	GConfBridge *bridge;
	GtkWidget *container;
	GtkWidget *widget;
	const gchar *config_dir;
	const gchar *key;
	gchar *filename;
	gchar *markup;
	gint page_num;
	gint ii;

	priv = E_CAL_SHELL_CONTENT_GET_PRIVATE (object);

	/* Chain up to parent's constructed() method. */
	G_OBJECT_CLASS (parent_class)->constructed (object);

	shell_content = E_SHELL_CONTENT (object);
	shell_view = e_shell_content_get_shell_view (shell_content);
	shell_window = e_shell_view_get_shell_window (shell_view);

	shell_backend = e_shell_view_get_shell_backend (shell_view);
	config_dir = e_shell_backend_get_config_dir (shell_backend);

	shell = e_shell_window_get_shell (shell_window);
	shell_settings = e_shell_get_shell_settings (shell);

	/* We borrow the memopad and taskpad models from the memo
	 * and task views, loading the views if necessary. */

	foreign_view = e_shell_window_get_shell_view (shell_window, "memos");
	foreign_content = e_shell_view_get_shell_content (foreign_view);
	g_object_get (foreign_content, "model", &memo_model, NULL);

	foreign_view = e_shell_window_get_shell_view (shell_window, "tasks");
	foreign_content = e_shell_view_get_shell_content (foreign_view);
	g_object_get (foreign_content, "model", &task_model, NULL);

	/* Build content widgets. */

	container = GTK_WIDGET (object);

	/* FIXME Need to deal with saving and restoring the position.
	 *       Month view has its own position. */
	widget = gtk_hpaned_new ();
	gtk_container_add (GTK_CONTAINER (container), widget);
	priv->hpaned = g_object_ref (widget);
	gtk_widget_show (widget);

	container = priv->hpaned;

	widget = gtk_notebook_new ();
	gtk_notebook_set_show_tabs (GTK_NOTEBOOK (widget), FALSE);
	gtk_notebook_set_show_border (GTK_NOTEBOOK (widget), FALSE);
	gtk_paned_pack1 (GTK_PANED (container), widget, FALSE, TRUE);
	priv->notebook = g_object_ref (widget);
	gtk_widget_show (widget);

	/* FIXME Need to deal with saving and restoring the position.
	 *       Month view has its own position. */
	widget = gtk_vpaned_new ();
	gtk_paned_pack2 (GTK_PANED (container), widget, TRUE, TRUE);
	priv->vpaned = g_object_ref (widget);
	gtk_widget_show (widget);

	container = priv->notebook;

	/* Add views in the order defined by GnomeCalendarViewType, such
	 * that the notebook page number corresponds to the view type. */

	calendar = GNOME_CALENDAR (priv->calendar);

	for (ii = 0; ii < GNOME_CAL_LAST_VIEW; ii++) {
		calendar_view = gnome_calendar_get_calendar_view (calendar, ii);

		gtk_notebook_append_page (
			GTK_NOTEBOOK (container),
			GTK_WIDGET (calendar_view), NULL);
		gtk_widget_show (GTK_WIDGET (calendar_view));
	}

	container = priv->vpaned;

	widget = gtk_vbox_new (FALSE, 0);
	gtk_paned_pack1 (GTK_PANED (container), widget, FALSE, FALSE);
	gtk_widget_show (widget);

	container = widget;

	widget = gtk_hseparator_new ();
	gtk_box_pack_start (GTK_BOX (container), widget, FALSE, TRUE, 0);
	gtk_widget_show (widget);

	widget = gtk_label_new (NULL);
	markup = g_strdup_printf ("<b>%s</b>", _("Tasks"));
	gtk_label_set_markup (GTK_LABEL (widget), markup);
	gtk_box_pack_start (GTK_BOX (container), widget, FALSE, TRUE, 0);
	gtk_widget_show (widget);
	g_free (markup);

	widget = e_calendar_table_new (shell_view, task_model);
	gtk_box_pack_start (GTK_BOX (container), widget, TRUE, TRUE, 0);
	priv->task_table = g_object_ref (widget);
	gtk_widget_show (widget);

	filename = g_build_filename (config_dir, "TaskPad", NULL);
	e_calendar_table_load_state (E_CALENDAR_TABLE (widget), filename);
	g_free (filename);

	container = priv->vpaned;

	widget = gtk_vbox_new (FALSE, 0);
	gtk_paned_pack2 (GTK_PANED (container), widget, TRUE, FALSE);
	gtk_widget_show (widget);

	container = widget;

	widget = gtk_label_new (NULL);
	markup = g_strdup_printf ("<b>%s</b>", _("Memos"));
	gtk_label_set_markup (GTK_LABEL (widget), markup);
	gtk_box_pack_start (GTK_BOX (container), widget, FALSE, TRUE, 0);
	gtk_widget_show (widget);
	g_free (markup);

	widget = e_memo_table_new (shell_view, memo_model);
	gtk_box_pack_start (GTK_BOX (container), widget, TRUE, TRUE, 0);
	priv->memo_table = g_object_ref (widget);
	gtk_widget_show (widget);

	filename = g_build_filename (config_dir, "MemoPad", NULL);
	e_memo_table_load_state (E_MEMO_TABLE (widget), filename);
	g_free (filename);

	/* Configuration managers for task and memo tables. */
	priv->task_table_config = e_calendar_table_config_new (
		E_CALENDAR_TABLE (priv->task_table));
	priv->memo_table_config = e_memo_table_config_new (
		E_MEMO_TABLE (priv->memo_table));

	/* Load the view instance. */

	view_instance = e_shell_view_new_view_instance (shell_view, NULL);
	g_signal_connect_swapped (
		view_instance, "display-view",
		G_CALLBACK (cal_shell_content_display_view_cb),
		object);
	gal_view_instance_load (view_instance);
	priv->view_instance = view_instance;

	g_signal_connect_swapped (
		shell_view, "notify::view-id",
		G_CALLBACK (cal_shell_content_notify_view_id_cb),
		object);

	/* Bind GObject properties to GConf keys. */

	bridge = gconf_bridge_get ();

	object = G_OBJECT (priv->vpaned);
	key = "/apps/evolution/calendar/display/vpane_position";
	gconf_bridge_bind_property_delayed (bridge, key, object, "position");

	/* Bind day view properties to EShellSettings. */

	view_type = GNOME_CAL_DAY_VIEW;
	calendar_view = gnome_calendar_get_calendar_view (calendar, view_type);

	e_binding_new (
		G_OBJECT (shell_settings), "cal-marcus-bains-show-line",
		G_OBJECT (calendar_view), "marcus-bains-show-line");

	e_binding_new (
		G_OBJECT (shell_settings), "cal-marcus-bains-day-view-color",
		G_OBJECT (calendar_view), "marcus-bains-day-view-color");

	e_binding_new (
		G_OBJECT (shell_settings), "cal-marcus-bains-time-bar-color",
		G_OBJECT (calendar_view), "marcus-bains-time-bar-color");

	e_binding_new (
		G_OBJECT (shell_settings), "cal-time-divisions",
		G_OBJECT (calendar_view), "mins-per-row");

	e_binding_new_full (
		G_OBJECT (shell_settings), "cal-week-start-day",
		G_OBJECT (calendar_view), "week-start-day",
		(EBindingTransform) transform_week_start_day,
		(GDestroyNotify) NULL, NULL);

	e_binding_new (
		G_OBJECT (shell_settings), "cal-work-day-end-hour",
		G_OBJECT (calendar_view), "work-day-end-hour");

	e_binding_new (
		G_OBJECT (shell_settings), "cal-work-day-end-minute",
		G_OBJECT (calendar_view), "work-day-end-minute");

	e_binding_new (
		G_OBJECT (shell_settings), "cal-work-day-start-hour",
		G_OBJECT (calendar_view), "work-day-start-hour");

	e_binding_new (
		G_OBJECT (shell_settings), "cal-work-day-start-minute",
		G_OBJECT (calendar_view), "work-day-start-minute");

	e_binding_new (
		G_OBJECT (shell_settings), "cal-working-days",
		G_OBJECT (calendar_view), "working-days");

	/* Bind work week view properties to EShellSettings. */

	view_type = GNOME_CAL_WORK_WEEK_VIEW;
	calendar_view = gnome_calendar_get_calendar_view (calendar, view_type);

	e_binding_new (
		G_OBJECT (shell_settings), "cal-marcus-bains-show-line",
		G_OBJECT (calendar_view), "marcus-bains-show-line");

	e_binding_new (
		G_OBJECT (shell_settings), "cal-marcus-bains-day-view-color",
		G_OBJECT (calendar_view), "marcus-bains-day-view-color");

	e_binding_new (
		G_OBJECT (shell_settings), "cal-marcus-bains-time-bar-color",
		G_OBJECT (calendar_view), "marcus-bains-time-bar-color");

	e_binding_new (
		G_OBJECT (shell_settings), "cal-time-divisions",
		G_OBJECT (calendar_view), "mins-per-row");

	e_binding_new_full (
		G_OBJECT (shell_settings), "cal-week-start-day",
		G_OBJECT (calendar_view), "week-start-day",
		(EBindingTransform) transform_week_start_day,
		(GDestroyNotify) NULL, NULL);

	e_binding_new (
		G_OBJECT (shell_settings), "cal-work-day-end-hour",
		G_OBJECT (calendar_view), "work-day-end-hour");

	e_binding_new (
		G_OBJECT (shell_settings), "cal-work-day-end-minute",
		G_OBJECT (calendar_view), "work-day-end-minute");

	e_binding_new (
		G_OBJECT (shell_settings), "cal-work-day-start-hour",
		G_OBJECT (calendar_view), "work-day-start-hour");

	e_binding_new (
		G_OBJECT (shell_settings), "cal-work-day-start-minute",
		G_OBJECT (calendar_view), "work-day-start-minute");

	e_binding_new (
		G_OBJECT (shell_settings), "cal-working-days",
		G_OBJECT (calendar_view), "working-days");

	/* Bind week view properties to EShellSettings. */

	view_type = GNOME_CAL_WEEK_VIEW;
	calendar_view = gnome_calendar_get_calendar_view (calendar, view_type);

	e_binding_new (
		G_OBJECT (shell_settings), "cal-compress-weekend",
		G_OBJECT (calendar_view), "compress-weekend");

	e_binding_new (
		G_OBJECT (shell_settings), "cal-show-event-end-times",
		G_OBJECT (calendar_view), "show-event-end-times");

	e_binding_new_full (
		G_OBJECT (shell_settings), "cal-week-start-day",
		G_OBJECT (calendar_view), "week-start-day",
		(EBindingTransform) transform_week_start_day,
		(GDestroyNotify) NULL, NULL);

	/* Bind month view properties to EShellSettings. */

	view_type = GNOME_CAL_MONTH_VIEW;
	calendar_view = gnome_calendar_get_calendar_view (calendar, view_type);

	e_binding_new (
		G_OBJECT (shell_settings), "cal-compress-weekend",
		G_OBJECT (calendar_view), "compress-weekend");

	e_binding_new (
		G_OBJECT (shell_settings), "cal-show-event-end-times",
		G_OBJECT (calendar_view), "show-event-end-times");

	e_binding_new_full (
		G_OBJECT (shell_settings), "cal-week-start-day",
		G_OBJECT (calendar_view), "week-start-day",
		(EBindingTransform) transform_week_start_day,
		(GDestroyNotify) NULL, NULL);

	/* Bind calendar model properties to EShellSettings.
	 * Note, does not matter from which view we get the
	 * model, since it's shared across all of them. */

	calendar_model = e_calendar_view_get_model (calendar_view);

	e_binding_new (
		G_OBJECT (shell_settings), "cal-use-24-hour-format",
		G_OBJECT (calendar_model), "use-24-hour-format");

	g_object_unref (memo_model);
	g_object_unref (task_model);
}

static void
cal_shell_content_class_init (ECalShellContentClass *class)
{
	GObjectClass *object_class;

	parent_class = g_type_class_peek_parent (class);
	g_type_class_add_private (class, sizeof (ECalShellContentPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = cal_shell_content_set_property;
	object_class->get_property = cal_shell_content_get_property;
	object_class->dispose = cal_shell_content_dispose;
	object_class->finalize = cal_shell_content_finalize;
	object_class->constructed = cal_shell_content_constructed;
}

static void
cal_shell_content_init (ECalShellContent *cal_shell_content)
{
	cal_shell_content->priv =
		E_CAL_SHELL_CONTENT_GET_PRIVATE (cal_shell_content);

	/* XXX GnomeCalendar is a widget, but we don't pack it.
	 *     Maybe it should just be a GObject instead? */
	cal_shell_content->priv->calendar = gnome_calendar_new ();
	g_object_ref_sink (cal_shell_content->priv->calendar);

	/* Postpone widget construction until we have a shell view. */
}

GType
e_cal_shell_content_get_type (void)
{
	return cal_shell_content_type;
}

void
e_cal_shell_content_register_type (GTypeModule *type_module)
{
	static const GTypeInfo type_info = {
		sizeof (ECalShellContentClass),
		(GBaseInitFunc) NULL,
		(GBaseFinalizeFunc) NULL,
		(GClassInitFunc) cal_shell_content_class_init,
		(GClassFinalizeFunc) NULL,
		NULL,  /* class_data */
		sizeof (ECalShellContent),
		0,     /* n_preallocs */
		(GInstanceInitFunc) cal_shell_content_init,
		NULL   /* value_table */
	};

	cal_shell_content_type = g_type_module_register_type (
		type_module, E_TYPE_SHELL_CONTENT,
		"ECalShellContent", &type_info, 0);
}

GtkWidget *
e_cal_shell_content_new (EShellView *shell_view)
{
	g_return_val_if_fail (E_IS_SHELL_VIEW (shell_view), NULL);

	return g_object_new (
		E_TYPE_CAL_SHELL_CONTENT,
		"shell-view", shell_view, NULL);
}

GnomeCalendar *
e_cal_shell_content_get_calendar (ECalShellContent *cal_shell_content)
{
	g_return_val_if_fail (
		E_IS_CAL_SHELL_CONTENT (cal_shell_content), NULL);

        /* FIXME */
	/*return GNOME_CALENDAR (cal_shell_content->priv->calendar);*/
        return NULL;
}

EMemoTable *
e_cal_shell_content_get_memo_table (ECalShellContent *cal_shell_content)
{
	g_return_val_if_fail (
		E_IS_CAL_SHELL_CONTENT (cal_shell_content), NULL);

	return E_MEMO_TABLE (cal_shell_content->priv->memo_table);
}

ECalendarTable *
e_cal_shell_content_get_task_table (ECalShellContent *cal_shell_content)
{
	g_return_val_if_fail (
		E_IS_CAL_SHELL_CONTENT (cal_shell_content), NULL);

	return E_CALENDAR_TABLE (cal_shell_content->priv->task_table);
}

icaltimezone *
e_cal_shell_content_get_timezone (ECalShellContent *cal_shell_content)
{
	g_return_val_if_fail (
		E_IS_CAL_SHELL_CONTENT (cal_shell_content), NULL);

        /* FIXME */
	/*return cal_shell_content->priv->timezone;*/
        return NULL;
}

GalViewInstance *
e_cal_shell_content_get_view_instance (ECalShellContent *cal_shell_content)
{
	g_return_val_if_fail (
		E_IS_CAL_SHELL_CONTENT (cal_shell_content), NULL);

	return cal_shell_content->priv->view_instance;
}

void
e_cal_shell_content_copy_clipboard (ECalShellContent *cal_shell_content)
{
#if 0
	GnomeCalendar *calendar;
	EMemoTable *memo_table;
	ECalendarTable *task_table;

	g_return_if_fail (E_IS_CAL_SHELL_CONTENT (cal_shell_content));

	calendar = e_cal_shell_content_get_calendar (cal_shell_content);
	memo_table = e_cal_shell_content_get_memo_table (cal_shell_content);
	task_table = e_cal_shell_content_get_task_table (cal_shell_content);

	switch (cal_shell_content_get_focus_location (cal_shell_content)) {
		case FOCUS_CALENDAR:
			gnome_calendar_copy_clipboard (calendar);
			break;

		case FOCUS_MEMO_TABLE:
			e_memo_table_copy_clipboard (memo_table);
			break;

		case FOCUS_TASK_TABLE:
			e_calendar_table_copy_clipboard (task_table);
			break;

		default:
			g_return_if_reached ();
	}
#endif
}

void
e_cal_shell_content_cut_clipboard (ECalShellContent *cal_shell_content)
{
#if 0
	GnomeCalendar *calendar;
	EMemoTable *memo_table;
	ECalendarTable *task_table;

	g_return_if_fail (E_IS_CAL_SHELL_CONTENT (cal_shell_content));

	calendar = e_cal_shell_content_get_calendar (cal_shell_content);
	memo_table = e_cal_shell_content_get_memo_table (cal_shell_content);
	task_table = e_cal_shell_content_get_task_table (cal_shell_content);

	switch (cal_shell_content_get_focus_location (cal_shell_content)) {
		case FOCUS_CALENDAR:
			gnome_calendar_cut_clipboard (calendar);
			break;

		case FOCUS_MEMO_TABLE:
			e_memo_table_copy_clipboard (memo_table);
			break;

		case FOCUS_TASK_TABLE:
			e_calendar_table_copy_clipboard (task_table);
			break;

		default:
			g_return_if_reached ();
	}
#endif
}

void
e_cal_shell_content_paste_clipboard (ECalShellContent *cal_shell_content)
{
#if 0
	GnomeCalendar *calendar;
	EMemoTable *memo_table;
	ECalendarTable *task_table;

	g_return_if_fail (E_IS_CAL_SHELL_CONTENT (cal_shell_content));

	calendar = e_cal_shell_content_get_calendar (cal_shell_content);
	memo_table = e_cal_shell_content_get_memo_table (cal_shell_content);
	task_table = e_cal_shell_content_get_task_table (cal_shell_content);

	switch (cal_shell_content_get_focus_location (cal_shell_content)) {
		case FOCUS_CALENDAR:
			gnome_calendar_paste_clipboard (calendar);
			break;

		case FOCUS_MEMO_TABLE:
			e_memo_table_copy_clipboard (memo_table);
			break;

		case FOCUS_TASK_TABLE:
			e_calendar_table_copy_clipboard (task_table);
			break;

		default:
			g_return_if_reached ();
	}
#endif
}

void
e_cal_shell_content_delete_selection (ECalShellContent *cal_shell_content)
{
#if 0
	GnomeCalendar *calendar;
	EMemoTable *memo_table;
	ECalendarTable *task_table;

	g_return_if_fail (E_IS_CAL_SHELL_CONTENT (cal_shell_content));

	calendar = e_cal_shell_content_get_calendar (cal_shell_content);
	memo_table = e_cal_shell_content_get_memo_table (cal_shell_content);
	task_table = e_cal_shell_content_get_task_table (cal_shell_content);

	switch (cal_shell_content_get_focus_location (cal_shell_content)) {
		case FOCUS_CALENDAR:
			gnome_calendar_delete_selection (calendar);
			break;

		case FOCUS_MEMO_TABLE:
			e_memo_table_delete_selected (memo_table);
			break;

		case FOCUS_TASK_TABLE:
			e_calendar_table_delete_selected (task_table);
			break;

		default:
			g_return_if_reached ();
	}
#endif
}

void
e_cal_shell_content_delete_selected_occurrence (ECalShellContent *cal_shell_content)
{
#if 0
	GnomeCalendar *calendar;
	FocusLocation focus;

	g_return_if_fail (E_IS_CAL_SHELL_CONTENT (cal_shell_content));

	focus = cal_shell_content_get_focus_location (cal_shell_content);
	calendar = e_cal_shell_content_get_calendar (cal_shell_content);

	if (focus == FOCUS_CALENDAR)
		gnome_calendar_delete_selected_occurrence (calendar);
#endif
}
