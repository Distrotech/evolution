/* Evolution calendar client - test program
 *
 * Copyright (C) 2000 Ximian, Inc.
 * Copyright (C) 2000 Ximian, Inc.
 *
 * Author: Federico Mena-Quintero <federico@ximian.com>
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

#include <config.h>
#include <stdlib.h>
#include <bonobo-activation/bonobo-activation.h>
#include <bonobo/bonobo-i18n.h>
#include <bonobo/bonobo-main.h>
#include "cal-client.h"
#include "cal-util/cal-component.h"

static CalClient *client1;
static CalClient *client2;

/* Prints a message with a client identifier */
static void
cl_printf (CalClient *client, const char *format, ...)
{
	va_list args;

	va_start (args, format);
	printf ("Client %s: ",
		client == client1 ? "1" :
		client == client2 ? "2" :
		"UNKNOWN");
	vprintf (format, args);
	va_end (args);
}

static void
objects_added_cb (GObject *object, GList *objects, gpointer data)
{
	GList *l;
	
	for (l = objects; l; l = l->next)
		cl_printf (data, "Object added %s\n", icalcomponent_get_uid (l->data));
}

static void
query_done_cb (GObject *object, ECalendarStatus status, gpointer data)
{
	cl_printf (data, "Query done\n");
}

/* Lists the UIDs of objects in a calendar, called as an idle handler */
static gboolean
list_uids (gpointer data)
{
	CalClient *client;
	GList *objects = NULL;
	GList *l;
	
	client = CAL_CLIENT (data);

	g_message ("Blah");
	
	if (!cal_client_get_object_list (client, "(contains? \"any\" \"Test4\")", &objects, NULL))
		return FALSE;
	
	cl_printf (client, "UIDS: ");

	if (!objects)
		printf ("none\n");
	else {
		for (l = objects; l; l = l->next) {
			const char *uid;

			uid = icalcomponent_get_uid (l->data);
			printf ("`%s' ", uid);
		}

		printf ("\n");

		for (l = objects; l; l = l->next) {
			printf ("------------------------------\n");
			printf ("%s", icalcomponent_as_ical_string (l->data));
			printf ("------------------------------\n");
		}
	}

	cal_client_free_object_list (objects);

	g_object_unref (client);

	return FALSE;
}

/* Callback used when a calendar is opened */
static void
cal_opened_cb (CalClient *client, CalClientOpenStatus status, gpointer data)
{
	CalQuery *query;
	
	cl_printf (client, "Load/create %s\n",
		   ((status == CAL_CLIENT_OPEN_SUCCESS) ? "success" :
		    (status == CAL_CLIENT_OPEN_ERROR) ? "error" :
		    (status == CAL_CLIENT_OPEN_NOT_FOUND) ? "not found" :
		    (status == CAL_CLIENT_OPEN_METHOD_NOT_SUPPORTED) ? "method not supported" :
		    "unknown status value"));

	if (status == CAL_CLIENT_OPEN_SUCCESS) {
		GList *comp_list;

#if 0
		/* get free/busy information */
		comp_list = cal_client_get_free_busy (client, NULL, 0, time (NULL));
		if (comp_list) {
			GList *l;

			for (l = comp_list; l; l = l->next) {
				char *comp_str;

				comp_str = cal_component_get_as_string (CAL_COMPONENT (l->data));
				g_object_unref (l->data);
				cl_printf (client, "Free/Busy -> %s\n", comp_str);
				g_free (comp_str);
			}
			g_list_free (comp_list);
		}

		g_message ("Idling");
#endif		
		if (!cal_client_get_query (client, "(contains? \"any\" \"Test4\")", &query, NULL))
			g_warning (G_STRLOC ": Unable to obtain query");

		g_signal_connect (G_OBJECT (query), "objects_added", objects_added_cb, client);
		g_signal_connect (G_OBJECT (query), "query_done", query_done_cb, client);

		cal_query_start (query);
		
		g_idle_add (list_uids, client);
	}
	else
		g_object_unref (client);
}

/* Callback used when a client is destroyed */
static void
client_destroy_cb (gpointer data, GObject *object)
{
	if (CAL_CLIENT (object) == client1)
		client1 = NULL;
	else if (CAL_CLIENT (object) == client2)
		client2 = NULL;
	else
		g_assert_not_reached ();

	if (!client1 && !client2)
		bonobo_main_quit ();
}

/* Creates a calendar client and tries to load the specified URI into it */
static void
create_client (CalClient **client, const char *uri, gboolean only_if_exists)
{
	gboolean result;

	*client = cal_client_new ();
	if (!*client) {
		g_message (G_STRLOC ": could not create the client");
		exit (1);
	}

	g_object_weak_ref (G_OBJECT (*client), client_destroy_cb, NULL);

	g_signal_connect (*client, "cal_opened",
			  G_CALLBACK (cal_opened_cb),
			  NULL);

	printf ("Calendar loading `%s'...\n", uri);

	result = cal_client_open_calendar (*client, uri, only_if_exists);

	if (!result) {
		g_message (G_STRLOC ": failure when issuing calendar open request `%s'",
			   uri);
		exit (1);
	}
}

int
main (int argc, char **argv)
{
	bindtextdomain (GETTEXT_PACKAGE, EVOLUTION_LOCALEDIR);
	textdomain (GETTEXT_PACKAGE);

	g_type_init ();
	bonobo_activation_init (argc, argv);

	if (!bonobo_init (&argc, argv)) {
		g_message ("main(): could not initialize Bonobo");
		exit (1);
	}

	create_client (&client1, "file:///home/gnome24-evolution-new-calendar/evolution/local/Calendar", FALSE);
//	create_client (&client2, "file:///tmp/tasks", TRUE);

	bonobo_main ();
	return 0;
}
