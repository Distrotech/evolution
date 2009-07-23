/*
 * e-cal-shell-backend.c
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

#include "e-cal-shell-backend.h"

#include <string.h>
#include <glib/gi18n.h>
#include <libecal/e-cal.h>
#include <libedataserver/e-url.h>
#include <libedataserver/e-source.h>
#include <libedataserver/e-source-group.h>

#include "e-util/e-import.h"
#include "shell/e-shell.h"
#include "shell/e-shell-backend.h"
#include "shell/e-shell-window.h"
#include "widgets/misc/e-preferences-window.h"

#include "calendar/common/authentication.h"
#include "calendar/gui/calendar-config.h"
#include "calendar/gui/comp-util.h"
#include "calendar/gui/e-attachment-handler-calendar.h"
#include "calendar/gui/e-cal-config.h"
#include "calendar/gui/e-cal-event.h"
#include "calendar/gui/dialogs/cal-prefs-dialog.h"
#include "calendar/gui/dialogs/calendar-setup.h"
#include "calendar/gui/dialogs/event-editor.h"
#include "calendar/importers/evolution-calendar-importer.h"

#include "e-cal-shell-migrate.h"
#include "e-cal-shell-settings.h"
#include "e-cal-shell-view.h"

#define E_CAL_SHELL_BACKEND_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_CAL_SHELL_BACKEND, ECalShellBackendPrivate))

#define CONTACTS_BASE_URI	"contacts://"
#define WEATHER_BASE_URI	"weather://"
#define WEB_BASE_URI		"webcal://"
#define PERSONAL_RELATIVE_URI	"system"

struct _ECalShellBackendPrivate {
	ESourceList *source_list;
};

enum {
	PROP_0,
	PROP_SOURCE_LIST
};

static gpointer parent_class;
static GType cal_shell_backend_type;

static void
cal_shell_backend_ensure_sources (EShellBackend *shell_backend)
{
	/* XXX This is basically the same algorithm across all backends.
	 *     Maybe we could somehow integrate this into EShellBackend? */

	ECalShellBackendPrivate *priv;
	ESourceGroup *on_this_computer;
	ESourceGroup *on_the_web;
	ESourceGroup *contacts;
	ESourceGroup *weather;
	ESource *birthdays;
	ESource *personal;
	EShell *shell;
	EShellSettings *shell_settings;
	GSList *groups, *iter;
	const gchar *data_dir;
	const gchar *name;
	gchar *base_uri;
	gchar *filename;
	gchar *property;

	on_this_computer = NULL;
	on_the_web = NULL;
	contacts = NULL;
	weather = NULL;
	birthdays = NULL;
	personal = NULL;

	priv = E_CAL_SHELL_BACKEND_GET_PRIVATE (shell_backend);

	shell = e_shell_backend_get_shell (shell_backend);
	shell_settings = e_shell_get_shell_settings (shell);

	if (!e_cal_get_sources (&priv->source_list, E_CAL_SOURCE_TYPE_EVENT, NULL)) {
		g_warning ("Could not get calendar sources from GConf!");
		return;
	}

	data_dir = e_shell_backend_get_data_dir (shell_backend);
	filename = g_build_filename (data_dir, "local", NULL);
	base_uri = g_filename_to_uri (filename, NULL, NULL);
	g_free (filename);

	groups = e_source_list_peek_groups (priv->source_list);
	for (iter = groups; iter != NULL; iter = iter->next) {
		ESourceGroup *source_group = iter->data;
		const gchar *group_base_uri;

		group_base_uri = e_source_group_peek_base_uri (source_group);

		/* Compare only "file://" part.  if the user's home
		 * changes, we do not want to create another group. */
		if (on_this_computer == NULL &&
			strncmp (base_uri, group_base_uri, 7) == 0)
			on_this_computer = source_group;

		else if (on_the_web == NULL &&
			strcmp (WEB_BASE_URI, group_base_uri) == 0)
			on_the_web = source_group;

		else if (contacts == NULL &&
			strcmp (CONTACTS_BASE_URI, group_base_uri) == 0)
			contacts = source_group;

		else if (weather == NULL &&
			strcmp (WEATHER_BASE_URI, group_base_uri) == 0)
			weather = source_group;
	}

	name = _("On This Computer");

	if (on_this_computer != NULL) {
		GSList *sources;
		const gchar *group_base_uri;

		/* Force the group name to the current locale. */
		e_source_group_set_name (on_this_computer, name);

		sources = e_source_group_peek_sources (on_this_computer);
		group_base_uri = e_source_group_peek_base_uri (on_this_computer);

		/* Make sure this group includes a "Personal" source. */
		for (iter = sources; iter != NULL; iter = iter->next) {
			ESource *source = iter->data;
			const gchar *relative_uri;

			relative_uri = e_source_peek_relative_uri (source);
			if (relative_uri == NULL)
				continue;

			if (strcmp (PERSONAL_RELATIVE_URI, relative_uri) != 0)
				continue;

			personal = source;
			break;
		}

		/* Make sure we have the correct base URI.  This can
		 * change when the user's home directory changes. */
		if (strcmp (base_uri, group_base_uri) != 0) {
			e_source_group_set_base_uri (
				on_this_computer, base_uri);

			/* XXX We shouldn't need this sync call here as
			 *     set_base_uri() results in synching to GConf,
			 *     but that happens in an idle loop and too late
			 *     to prevent the user from seeing a "Cannot
			 *     Open ... because of invalid URI" error. */
			e_source_list_sync (priv->source_list, NULL);
		}

	} else {
		ESourceGroup *source_group;

		source_group = e_source_group_new (name, base_uri);
		e_source_list_add_group (priv->source_list, source_group, -1);
		g_object_unref (source_group);
	}

	name = _("Personal");

	if (personal == NULL) {
		ESource *source;
		GSList *selected;
		gchar *primary;

		source = e_source_new (name, PERSONAL_RELATIVE_URI);
		e_source_group_add_source (on_this_computer, source, -1);
		g_object_unref (source);

		primary = e_shell_settings_get_string (
			shell_settings, "cal-primary-calendar");

		selected = calendar_config_get_calendars_selected ();

		if (primary == NULL && selected == NULL) {
			const gchar *uid;

			uid = e_source_peek_uid (source);
			selected = g_slist_prepend (NULL, g_strdup (uid));

			e_shell_settings_set_string (
				shell_settings, "cal-primary-calendar", uid);
			calendar_config_set_calendars_selected (selected);
		}

		g_slist_foreach (selected, (GFunc) g_free, NULL);
		g_slist_free (selected);
		g_free (primary);
	} else {
		/* Force the source name to the current locale. */
		e_source_set_name (personal, name);
	}

	name = _("On The Web");

	if (on_the_web == NULL) {
		ESourceGroup *source_group;

		source_group = e_source_group_new (name, WEB_BASE_URI);
		e_source_list_add_group (priv->source_list, source_group, -1);
		g_object_unref (source_group);
	} else {
		/* Force the group name to the current locale. */
		e_source_group_set_name (on_the_web, name);
	}

	name = _("Contacts");

	if (contacts != NULL) {
		GSList *sources;

		/* Force the group name to the current locale. */
		e_source_group_set_name (contacts, name);

		sources = e_source_group_peek_sources (contacts);

		if (sources != NULL) {
			GSList *trash;

			/* There is only one source under Contacts. */
			birthdays = E_SOURCE (sources->data);
			sources = g_slist_next (sources);

			/* Delete any other sources in this group.
			 * Earlier versions allowed you to create
			 * additional sources under Contacts. */
			trash = g_slist_copy (sources);
			while (trash != NULL) {
				ESource *source = trash->data;
				e_source_group_remove_source (contacts, source);
				trash = g_slist_delete_link (trash, trash);
			}

		}
	} else {
		ESourceGroup *source_group;

		source_group = e_source_group_new (name, CONTACTS_BASE_URI);
		e_source_list_add_group (priv->source_list, source_group, -1);
		g_object_unref (source_group);

		/* This is now a borrowed reference. */
		contacts = source_group;
	}

	/* XXX e_source_group_get_property() returns a newly-allocated
	 *     string when it could just as easily return a const string.
	 *     Unfortunately, fixing that would break the API. */
	property = e_source_group_get_property (contacts, "create_source");
	if (property == NULL)
		e_source_group_set_property (contacts, "create_source", "no");
	g_free (property);

	name = _("Birthdays & Anniversaries");

	if (birthdays == NULL) {
		ESource *source;
		const gchar *name;

		name = _("Birthdays & Anniversaries");
		source = e_source_new (name, "/");
		e_source_group_add_source (contacts, source, -1);
		g_object_unref (source);

		/* This is now a borrowed reference. */
		birthdays = source;
	} else {
		/* Force the source name to the current locale. */
		e_source_set_name (birthdays, name);
	}

	if (e_source_get_property (birthdays, "delete") == NULL)
		e_source_set_property (birthdays, "delete", "no");

	if (e_source_peek_color_spec (birthdays) == NULL)
		e_source_set_color_spec (birthdays, "#DDBECE");

	name = _("Weather");

	if (weather == NULL) {
		ESourceGroup *source_group;

		source_group = e_source_group_new (name, WEATHER_BASE_URI);
		e_source_list_add_group (priv->source_list, source_group, -1);
		g_object_unref (source_group);
	} else {
		/* Force the group name to the current locale. */
		e_source_group_set_name (weather, name);
	}

	g_free (base_uri);
}

static void
cal_shell_backend_event_new_cb (ECal *cal,
                                ECalendarStatus status,
                                EShell *shell)
{
	ECalComponent *comp;
	CompEditor *editor;
	CompEditorFlags flags = 0;

	/* XXX Handle errors better. */
	if (status != E_CALENDAR_STATUS_OK)
		return;

	flags |= COMP_EDITOR_NEW_ITEM;
	flags |= COMP_EDITOR_USER_ORG;

	editor = event_editor_new (cal, shell, flags);
	comp = cal_comp_event_new_with_current_time (cal, FALSE);
	comp_editor_edit_comp (editor, comp);

	gtk_window_present (GTK_WINDOW (editor));

	g_object_unref (comp);
	g_object_unref (cal);
}

static void
cal_shell_backend_event_all_day_new_cb (ECal *cal,
                                        ECalendarStatus status,
                                        EShell *shell)
{
	ECalComponent *comp;
	CompEditor *editor;
	CompEditorFlags flags = 0;

	/* XXX Handle errors better. */
	if (status != E_CALENDAR_STATUS_OK)
		return;

	flags |= COMP_EDITOR_NEW_ITEM;
	flags |= COMP_EDITOR_USER_ORG;

	editor = event_editor_new (cal, shell, flags);
	comp = cal_comp_event_new_with_current_time (cal, TRUE);
	comp_editor_edit_comp (editor, comp);

	gtk_window_present (GTK_WINDOW (editor));

	g_object_unref (comp);
	g_object_unref (cal);
}

static void
cal_shell_backend_event_meeting_new_cb (ECal *cal,
                                        ECalendarStatus status,
                                        EShell *shell)
{
	ECalComponent *comp;
	CompEditor *editor;
	CompEditorFlags flags = 0;

	/* XXX Handle errors better. */
	if (status != E_CALENDAR_STATUS_OK)
		return;

	flags |= COMP_EDITOR_NEW_ITEM;
	flags |= COMP_EDITOR_USER_ORG;
	flags |= COMP_EDITOR_MEETING;

	editor = event_editor_new (cal, shell, flags);
	comp = cal_comp_event_new_with_current_time (cal, FALSE);
	comp_editor_edit_comp (editor, comp);

	gtk_window_present (GTK_WINDOW (editor));

	g_object_unref (comp);
	g_object_unref (cal);
}

static void
action_event_new_cb (GtkAction *action,
                     EShellWindow *shell_window)
{
	ECal *cal = NULL;
	ECalSourceType source_type;
	ESourceList *source_list;
	EShellSettings *shell_settings;
	EShell *shell;
	const gchar *action_name;
	gchar *uid;

	/* This callback is used for both appointments and meetings. */

	source_type = E_CAL_SOURCE_TYPE_EVENT;

	shell = e_shell_window_get_shell (shell_window);
	shell_settings = e_shell_get_shell_settings (shell);

	if (!e_cal_get_sources (&source_list, source_type, NULL)) {
		g_warning ("Could not get calendar sources from GConf!");
		return;
	}

	uid = e_shell_settings_get_string (
		shell_settings, "cal-primary-calendar");

	if (uid != NULL) {
		ESource *source;

		source = e_source_list_peek_source_by_uid (source_list, uid);
		if (source != NULL)
			cal = auth_new_cal_from_source (source, source_type);
		g_free (uid);
	}

	if (cal == NULL)
		cal = auth_new_cal_from_default (source_type);

	g_return_if_fail (cal != NULL);

	/* Connect the appropriate signal handler. */
	action_name = gtk_action_get_name (action);
	if (strcmp (action_name, "event-all-day-new") == 0)
		g_signal_connect (
			cal, "cal-opened",
			G_CALLBACK (cal_shell_backend_event_all_day_new_cb),
			shell);
	else if (strcmp (action_name, "event-meeting-new") == 0)
		g_signal_connect (
			cal, "cal-opened",
			G_CALLBACK (cal_shell_backend_event_meeting_new_cb),
			shell);
	else
		g_signal_connect (
			cal, "cal-opened",
			G_CALLBACK (cal_shell_backend_event_new_cb),
			shell);

	e_cal_open_async (cal, FALSE);
}

static void
action_calendar_new_cb (GtkAction *action,
                        EShellWindow *shell_window)
{
	calendar_setup_new_calendar (GTK_WINDOW (shell_window));
}

static GtkActionEntry item_entries[] = {

	{ "event-new",
	  "appointment-new",
	  NC_("New", "_Appointment"),
	  "<Shift><Control>a",
	  N_("Create a new appointment"),
	  G_CALLBACK (action_event_new_cb) },

	{ "event-all-day-new",
	  "stock_new-24h-appointment",
	  NC_("New", "All Day A_ppointment"),
	  NULL,
	  N_("Create a new all-day appointment"),
	  G_CALLBACK (action_event_new_cb) },

	{ "event-meeting-new",
	  "stock_new-meeting",
	  NC_("New", "M_eeting"),
	  "<Shift><Control>e",
	  N_("Create a new meeting request"),
	  G_CALLBACK (action_event_new_cb) }
};

static GtkActionEntry source_entries[] = {

	{ "calendar-new",
	  "x-office-calendar",
	  NC_("New", "Cale_ndar"),
	  NULL,
	  N_("Create a new calendar"),
	  G_CALLBACK (action_calendar_new_cb) }
};

static void
cal_shell_backend_init_hooks (void)
{
	e_plugin_hook_register_type (e_cal_config_hook_get_type ());
	e_plugin_hook_register_type (e_cal_event_hook_get_type ());
}

static void
cal_shell_backend_init_importers (void)
{
	EImportClass *import_class;
	EImportImporter *importer;

	import_class = g_type_class_ref (e_import_get_type ());

	importer = gnome_calendar_importer_peek ();
	e_import_class_add_importer (import_class, importer, NULL, NULL);

	importer = ical_importer_peek ();
	e_import_class_add_importer (import_class, importer, NULL, NULL);

	importer = vcal_importer_peek ();
	e_import_class_add_importer (import_class, importer, NULL, NULL);
}

static void
cal_shell_backend_init_preferences (EShell *shell)
{
	GtkWidget *preferences_window;

	preferences_window = e_shell_get_preferences_window (shell);

	e_preferences_window_add_page (
		E_PREFERENCES_WINDOW (preferences_window),
		"calendar-and-tasks",
		"preferences-calendar-and-tasks",
		_("Calendar and Tasks"),
		calendar_prefs_dialog_new (shell),
		600);
}

static gboolean
cal_shell_backend_handle_uri_cb (EShellBackend *shell_backend,
                                 const gchar *uri)
{
	/* FIXME */
	return FALSE;
}

static void
cal_shell_backend_window_created_cb (EShellBackend *shell_backend,
                                     GtkWindow *window)
{
	const gchar *backend_name;

	if (!E_IS_SHELL_WINDOW (window))
		return;

	backend_name = E_SHELL_BACKEND_GET_CLASS (shell_backend)->name;

	e_shell_window_register_new_item_actions (
		E_SHELL_WINDOW (window), backend_name,
		item_entries, G_N_ELEMENTS (item_entries));

	e_shell_window_register_new_source_actions (
		E_SHELL_WINDOW (window), backend_name,
		source_entries, G_N_ELEMENTS (source_entries));
}

static void
cal_shell_backend_get_property (GObject *object,
                                guint property_id,
                                GValue *value,
                                GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_SOURCE_LIST:
			g_value_set_object (
				value,
				e_cal_shell_backend_get_source_list (
				E_CAL_SHELL_BACKEND (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
cal_shell_backend_dispose (GObject *object)
{
	ECalShellBackendPrivate *priv;

	priv = E_CAL_SHELL_BACKEND_GET_PRIVATE (object);

	if (priv->source_list != NULL) {
		g_object_unref (priv->source_list);
		priv->source_list = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
cal_shell_backend_constructed (GObject *object)
{
	EShell *shell;
	EShellBackend *shell_backend;

	shell_backend = E_SHELL_BACKEND (object);
	shell = e_shell_backend_get_shell (shell_backend);

	cal_shell_backend_ensure_sources (shell_backend);

	g_signal_connect_swapped (
		shell, "handle-uri",
		G_CALLBACK (cal_shell_backend_handle_uri_cb),
		shell_backend);

	g_signal_connect_swapped (
		shell, "window-created",
		G_CALLBACK (cal_shell_backend_window_created_cb),
		shell_backend);

	cal_shell_backend_init_hooks ();
	cal_shell_backend_init_importers ();

	/* Initialize settings before initializing preferences,
	 * since the preferences bind to the shell settings. */
	e_cal_shell_backend_init_settings (shell);
	cal_shell_backend_init_preferences (shell);

	e_attachment_handler_calendar_get_type ();
}

static void
cal_shell_backend_class_init (ECalShellBackendClass *class)
{
	GObjectClass *object_class;
	EShellBackendClass *shell_backend_class;

	parent_class = g_type_class_peek_parent (class);
	g_type_class_add_private (class, sizeof (ECalShellBackendPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->get_property = cal_shell_backend_get_property;
	object_class->dispose = cal_shell_backend_dispose;
	object_class->constructed = cal_shell_backend_constructed;

	shell_backend_class = E_SHELL_BACKEND_CLASS (class);
	shell_backend_class->shell_view_type = E_TYPE_CAL_SHELL_VIEW;
	shell_backend_class->name = "calendar";
	shell_backend_class->aliases = "";
	shell_backend_class->schemes = "calendar";
	shell_backend_class->sort_order = 400;
	shell_backend_class->start = NULL;
	shell_backend_class->migrate = e_cal_shell_backend_migrate;

	g_object_class_install_property (
		object_class,
		PROP_SOURCE_LIST,
		g_param_spec_object (
			"source-list",
			_("Source List"),
			_("The registry of calendars"),
			E_TYPE_SOURCE_LIST,
			G_PARAM_READABLE));
}

static void
cal_shell_backend_init (ECalShellBackend *cal_shell_backend)
{
	cal_shell_backend->priv =
		E_CAL_SHELL_BACKEND_GET_PRIVATE (cal_shell_backend);
}

GType
e_cal_shell_backend_get_type (void)
{
	return cal_shell_backend_type;
}

void
e_cal_shell_backend_register_type (GTypeModule *type_module)
{
	const GTypeInfo type_info = {
		sizeof (ECalShellBackendClass),
		(GBaseInitFunc) NULL,
		(GBaseFinalizeFunc) NULL,
		(GClassInitFunc) cal_shell_backend_class_init,
		(GClassFinalizeFunc) NULL,
		NULL,  /* class_data */
		sizeof (ECalShellBackend),
		0,     /* n_preallocs */
		(GInstanceInitFunc) cal_shell_backend_init,
		NULL   /* value_table */
	};

	cal_shell_backend_type = g_type_module_register_type (
		type_module, E_TYPE_SHELL_BACKEND,
		"ECalShellBackend", &type_info, 0);
}

ESourceList *
e_cal_shell_backend_get_source_list (ECalShellBackend *cal_shell_backend)
{
	g_return_val_if_fail (
		E_IS_CAL_SHELL_BACKEND (cal_shell_backend), NULL);

	return cal_shell_backend->priv->source_list;
}
