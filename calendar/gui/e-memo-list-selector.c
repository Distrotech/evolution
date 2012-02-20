/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-memo-list-selector.c
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
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
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "e-memo-list-selector.h"

#include <string.h>
#include <libecal/e-cal-client.h>
#include <libedataserverui/e-client-utils.h>
#include "e-util/e-selection.h"
#include "calendar/gui/comp-util.h"

#define E_MEMO_LIST_SELECTOR_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_MEMO_LIST_SELECTOR, EMemoListSelectorPrivate))

struct _EMemoListSelectorPrivate {
	gint dummy_value;
};

G_DEFINE_TYPE (
	EMemoListSelector,
	e_memo_list_selector,
	E_TYPE_SOURCE_SELECTOR)

static gboolean
memo_list_selector_update_single_object (ECalClient *client,
                                         icalcomponent *icalcomp)
{
	gchar *uid = NULL;
	icalcomponent *tmp_icalcomp;

	uid = (gchar *) icalcomponent_get_uid (icalcomp);

	if (e_cal_client_get_object_sync (client, uid, NULL, &tmp_icalcomp, NULL, NULL)) {
		icalcomponent_free (tmp_icalcomp);

		return e_cal_client_modify_object_sync (
			client, icalcomp, CALOBJ_MOD_ALL, NULL, NULL);
	}

	if (!e_cal_client_create_object_sync (client, icalcomp, &uid, NULL, NULL))
		return FALSE;

	if (uid)
		icalcomponent_set_uid (icalcomp, uid);

	g_free (uid);

	return TRUE;
}

static gboolean
memo_list_selector_update_objects (ECalClient *client,
                                   icalcomponent *icalcomp)
{
	icalcomponent *subcomp;
	icalcomponent_kind kind;

	kind = icalcomponent_isa (icalcomp);
	if (kind == ICAL_VJOURNAL_COMPONENT)
		return memo_list_selector_update_single_object (
			client, icalcomp);
	else if (kind != ICAL_VCALENDAR_COMPONENT)
		return FALSE;

	subcomp = icalcomponent_get_first_component (
		icalcomp, ICAL_ANY_COMPONENT);
	while (subcomp != NULL) {
		gboolean success;

		kind = icalcomponent_isa (subcomp);
		if (kind == ICAL_VTIMEZONE_COMPONENT) {
			icaltimezone *zone;
			GError *error = NULL;

			zone = icaltimezone_new ();
			icaltimezone_set_component (zone, subcomp);

			e_cal_client_add_timezone_sync (client, zone, NULL, &error);
			icaltimezone_free (zone, 1);
			if (error != NULL) {
				g_warning (
					"%s: Failed to add timezone: %s",
					G_STRFUNC, error->message);
				g_error_free (error);
				return FALSE;
			}
		} else if (kind == ICAL_VJOURNAL_COMPONENT) {
			success = memo_list_selector_update_single_object (
				client, subcomp);
			if (!success)
				return FALSE;
		}

		subcomp = icalcomponent_get_next_component (
			icalcomp, ICAL_ANY_COMPONENT);
	}

	return TRUE;
}

static void
client_opened_cb (GObject *source_object,
                  GAsyncResult *result,
                  gpointer user_data)
{
	ESource *source = E_SOURCE (source_object);
	EClient *client = NULL;
	gchar *uid = user_data;
	GError *error = NULL;

	g_return_if_fail (uid != NULL);

	e_client_utils_open_new_finish (source, result, &client, &error);

	if (error != NULL) {
		g_warn_if_fail (client == NULL);
		g_warning (
			"%s: Failed to open memo list: %s",
			G_STRFUNC, error->message);
		g_error_free (error);
		goto exit;
	}

	g_return_if_fail (E_IS_CLIENT (client));

	if (!e_client_is_readonly (client))
		e_cal_client_remove_object_sync (
			E_CAL_CLIENT (client), uid, NULL,
			CALOBJ_MOD_THIS, NULL, NULL);

	g_object_unref (client);

exit:
	g_free (uid);
}

static gboolean
memo_list_selector_process_data (ESourceSelector *selector,
                                 ECalClient *client,
                                 const gchar *source_uid,
                                 icalcomponent *icalcomp,
                                 GdkDragAction action)
{
	ESourceList *source_list;
	ESource *source;
	icalcomponent *tmp_icalcomp = NULL;
	const gchar *uid;
	gchar *old_uid = NULL;
	gboolean success = FALSE;
	GError *error = NULL;

	/* FIXME Deal with GDK_ACTION_ASK. */
	if (action == GDK_ACTION_COPY) {
		old_uid = g_strdup (icalcomponent_get_uid (icalcomp));
		uid = e_cal_component_gen_uid ();
		icalcomponent_set_uid (icalcomp, uid);
	}

	uid = icalcomponent_get_uid (icalcomp);
	if (old_uid == NULL)
		old_uid = g_strdup (uid);

	if (e_cal_client_get_object_sync (client, uid, NULL, &tmp_icalcomp, NULL, &error)) {
		icalcomponent_free (tmp_icalcomp);
		success = TRUE;
		goto exit;
	}

	if (error != NULL && !g_error_matches (error, E_CAL_CLIENT_ERROR, E_CAL_CLIENT_ERROR_OBJECT_NOT_FOUND)) {
		g_message (
			"Failed to search the object in destination "
			"task list: %s", error->message);
		g_error_free (error);
		goto exit;
	}

	if (error)
		g_error_free (error);

	success = memo_list_selector_update_objects (client, icalcomp);

	if (!success || action != GDK_ACTION_MOVE)
		goto exit;

	source_list = e_source_selector_get_source_list (selector);
	source = e_source_list_peek_source_by_uid (source_list, source_uid);

	if (!E_IS_SOURCE (source) || e_source_get_readonly (source))
		goto exit;

	e_client_utils_open_new (source, E_CLIENT_SOURCE_TYPE_MEMOS, TRUE, NULL,
		e_client_utils_authenticate_handler, NULL,
		client_opened_cb, g_strdup (old_uid));

 exit:
	g_free (old_uid);

	return success;
}

struct DropData
{
	ESourceSelector *selector;
	GdkDragAction action;
	GSList *list;
};

static void
client_opened_for_drop_cb (GObject *source_object,
                           GAsyncResult *result,
                           gpointer user_data)
{
	ESource *source = E_SOURCE (source_object);
	struct DropData *dd = user_data;
	EClient *client = NULL;
	ECalClient *cal_client;
	GSList *iter;
	GError *error = NULL;

	g_return_if_fail (dd != NULL);

	e_client_utils_open_new_finish (source, result, &client, &error);

	if (error != NULL) {
		g_warn_if_fail (client == NULL);
		g_warning (
			"%s: Failed to open memo list: %s",
			G_STRFUNC, error->message);
		g_error_free (error);
		goto exit;
	}

	g_return_if_fail (E_IS_CLIENT (client));

	cal_client = E_CAL_CLIENT (client);

	for (iter = dd->list; iter != NULL; iter = iter->next) {
		gchar *source_uid = iter->data;
		icalcomponent *icalcomp;
		gchar *component_string;

		/* Each string is "source_uid\ncomponent_string". */
		component_string = strchr (source_uid, '\n');
		if (component_string == NULL)
			continue;

		*component_string++ = '\0';
		icalcomp = icalparser_parse_string (component_string);
		if (icalcomp == NULL)
			continue;

		memo_list_selector_process_data (
			dd->selector, cal_client, source_uid,
			icalcomp, dd->action);

		icalcomponent_free (icalcomp);
	}

	g_object_unref (client);

exit:
	g_slist_foreach (dd->list, (GFunc) g_free, NULL);
	g_slist_free (dd->list);
	g_object_unref (dd->selector);
	g_free (dd);
}

static gboolean
memo_list_selector_data_dropped (ESourceSelector *selector,
                                 GtkSelectionData *selection_data,
                                 ESource *destination,
                                 GdkDragAction action,
                                 guint info)
{
	struct DropData *dd;

	dd = g_new0 (struct DropData, 1);
	dd->selector = g_object_ref (selector);
	dd->action = action;
	dd->list = cal_comp_selection_get_string_list (selection_data);

	e_client_utils_open_new (destination, E_CLIENT_SOURCE_TYPE_MEMOS, TRUE, NULL,
		e_client_utils_authenticate_handler, NULL,
		client_opened_for_drop_cb, dd);

	return TRUE;
}

static void
e_memo_list_selector_class_init (EMemoListSelectorClass *class)
{
	ESourceSelectorClass *source_selector_class;

	g_type_class_add_private (class, sizeof (EMemoListSelectorPrivate));

	source_selector_class = E_SOURCE_SELECTOR_CLASS (class);
	source_selector_class->data_dropped = memo_list_selector_data_dropped;
}

static void
e_memo_list_selector_init (EMemoListSelector *selector)
{
	selector->priv = E_MEMO_LIST_SELECTOR_GET_PRIVATE (selector);

	gtk_drag_dest_set (
		GTK_WIDGET (selector), GTK_DEST_DEFAULT_ALL,
		NULL, 0, GDK_ACTION_COPY | GDK_ACTION_MOVE);

	e_drag_dest_add_calendar_targets (GTK_WIDGET (selector));
}

GtkWidget *
e_memo_list_selector_new (ESourceList *source_list)
{
	g_return_val_if_fail (E_IS_SOURCE_LIST (source_list), NULL);

	return g_object_new (
		E_TYPE_MEMO_LIST_SELECTOR,
		"source-list", source_list, NULL);
}
