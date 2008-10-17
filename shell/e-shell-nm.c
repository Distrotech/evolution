/*
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
 * Authors:
 *		Shreyas Srinivasan <sshreyas@novell.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#define DBUS_API_SUBJECT_TO_CHANGE 1

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <glib.h>
#include <e-shell.h>
#include <Evolution.h>
#include <dbus/dbus.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <dbus/dbus-glib.h>
#include <NetworkManager/NetworkManager.h>

typedef enum _ShellLineStatus {
	E_SHELL_LINE_DOWN,
	E_SHELL_LINE_UP
} ShellLineStatus;

gboolean e_shell_dbus_initialise (EShell *shell);

static DBusConnection *dbus_connection = NULL;

static gboolean
reinit_dbus (gpointer user_data)
{
	EShell *shell = user_data;

	if (e_shell_dbus_initialise (shell))
		return FALSE;

	/* keep trying to re-establish dbus connection */

	return TRUE;
}

static DBusHandlerResult
e_shell_network_monitor (DBusConnection *connection G_GNUC_UNUSED,
                         DBusMessage *message, void *user_data)
{
	const char *object;
	ShellLineStatus status;
	EShell *shell = user_data;
	GNOME_Evolution_ShellState shell_state;
	EShellLineStatus line_status;
	DBusError error = DBUS_ERROR_INIT;

	object = dbus_message_get_path (message);

	if (dbus_message_is_signal (message, DBUS_INTERFACE_LOCAL, "Disconnected") &&
		object && !strcmp (object, DBUS_PATH_LOCAL)) {
		dbus_connection_unref (dbus_connection);
		dbus_connection = NULL;

		g_timeout_add (3000, reinit_dbus, shell);

		return DBUS_HANDLER_RESULT_HANDLED;
	}

	if (dbus_message_is_signal (message, NM_DBUS_INTERFACE, "DeviceNoLongerActive"))
		status = E_SHELL_LINE_DOWN;
	else if (dbus_message_is_signal (message, NM_DBUS_INTERFACE, "DeviceNowActive"))
		status = E_SHELL_LINE_UP;
	else
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	if (!dbus_message_get_args (message, &error, DBUS_TYPE_OBJECT_PATH,
				    &object, DBUS_TYPE_INVALID))
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	line_status = e_shell_get_line_status (shell);

	if (line_status == E_SHELL_LINE_STATUS_ONLINE && status == E_SHELL_LINE_DOWN) {
		shell_state = GNOME_Evolution_FORCED_OFFLINE;
		e_shell_set_line_status (shell, shell_state);
	} else if (line_status == E_SHELL_LINE_STATUS_FORCED_OFFLINE && status == E_SHELL_LINE_UP) {
		shell_state = GNOME_Evolution_USER_ONLINE;
		e_shell_set_line_status (shell, shell_state);
	}

	return DBUS_HANDLER_RESULT_HANDLED;
}

gboolean
e_shell_dbus_initialise (EShell *shell)
{
	DBusError error = DBUS_ERROR_INIT;

	if (dbus_connection != NULL)
		return TRUE;

	dbus_connection = dbus_bus_get (DBUS_BUS_SYSTEM, &error);
	if (dbus_connection == NULL) {
		g_warning ("%s", error.message);
		dbus_error_free (&error);
		return FALSE;
	}

	dbus_connection_setup_with_g_main (dbus_connection, NULL);
	dbus_connection_set_exit_on_disconnect (dbus_connection, FALSE);

	if (!dbus_connection_add_filter (dbus_connection, e_shell_network_monitor, shell, NULL))
		goto exception;

	dbus_bus_add_match (dbus_connection,
			    "type='signal',"
			    "interface='" NM_DBUS_INTERFACE "',"
			    "sender='" NM_DBUS_SERVICE "',"
			    "path='" NM_DBUS_PATH "'", &error);
	if (dbus_error_is_set (&error)) {
		g_warning ("%s", error.message);
		dbus_error_free (&error);
		goto exception;
	}

	return TRUE;

exception:

	dbus_connection_unref (dbus_connection);
	dbus_connection = NULL;

	return FALSE;
}
