/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* main.c
 *
 * Copyright (C) 2000, 2001, 2002, 2003  Ximian, Inc.
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
 * Author: Ettore Perazzoli
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib.h>
#include <libgnome/gnome-i18n.h>
#include <libgnome/gnome-init.h>
#include <libgnomevfs/gnome-vfs-init.h>
#include <glade/glade.h>

#include <bonobo/bonobo-main.h>
#include <bonobo/bonobo-shlib-factory.h>
#include <bonobo/bonobo-exception.h>

#include <gal/widgets/e-cursors.h>

#include <evolution-shell-client.h>

#include "dialogs/cal-prefs-dialog.h"
#include "calendar-commands.h"
#include "calendar-config.h"
#include "calendar-component.h"
#include "e-comp-editor-registry.h"
#include "comp-editor-factory.h"
#include "control-factory.h"
#include "itip-bonobo-control.h"
#include "tasks-control.h"


#define FACTORY_ID "OAFIID:GNOME_Evolution_Calendar_Factory"

#define CALENDAR_COMPONENT_ID  "OAFIID:GNOME_Evolution_Calendar_ShellComponent"
#define CALENDAR_CONTROL_ID    "OAFIID:GNOME_Evolution_Calendar_Control"
#define TASKS_CONTROL_ID       "OAFIID:GNOME_Evolution_Tasks_Control"
#define ITIP_CONTROL_ID        "OAFIID:GNOME_Evolution_Calendar_iTip_Control"
#define CONFIG_CONTROL_ID      "OAFIID:GNOME_Evolution_Calendar_ConfigControl"
#define COMP_EDITOR_FACTORY_ID "OAFIID:GNOME_Evolution_Calendar_CompEditorFactory"

ECompEditorRegistry *comp_editor_registry = NULL;

/* The component editor factory */
static CompEditorFactory *comp_editor_factory = NULL;


/* Factory function for the calendar component factory; just creates and
 * references a singleton service object.
 */
static BonoboObject *
comp_editor_factory_fn (void)
{
	if (!comp_editor_factory) {
		comp_editor_factory = comp_editor_factory_new ();
		if (!comp_editor_factory)
			return NULL;
	}

	bonobo_object_ref (BONOBO_OBJECT (comp_editor_factory));
	return BONOBO_OBJECT (comp_editor_factory);
}


/* Does a simple activation and unreffing of the alarm notification service so
 * that the daemon will be launched if it is not running yet.
 */
static gboolean
launch_alarm_daemon_cb (gpointer data)
{
	CORBA_Environment ev;
	GNOME_Evolution_Calendar_AlarmNotify an;
	guint *idle_id = (guint *) data;

	/* remove the idle function */
	g_source_remove (*idle_id);
	g_free (idle_id);

	/* activate the alarm daemon */
	CORBA_exception_init (&ev);
	an = bonobo_activation_activate_from_id ("OAFIID:GNOME_Evolution_Calendar_AlarmNotify", 0, NULL, &ev);

	if (BONOBO_EX (&ev)) {
		g_message ("launch_alarm_daemon_cb(): Could not activate the alarm notification service");
		CORBA_exception_free (&ev);
		return FALSE;
	}
	CORBA_exception_free (&ev);

	/* Just get rid of it; what we are interested in is that it gets launched */

	CORBA_exception_init (&ev);
	bonobo_object_release_unref (an, &ev);
	if (BONOBO_EX (&ev))
		g_message ("add_alarms(): Could not unref the alarm notification service");

	CORBA_exception_free (&ev);

	return FALSE;
}

static void
launch_alarm_daemon (void)
{
	guint *idle_id;

	idle_id = g_new0 (guint, 1);
	*idle_id = g_idle_add ((GSourceFunc) launch_alarm_daemon_cb, idle_id);
}

static void
initialize (void)
{
	comp_editor_registry = E_COMP_EDITOR_REGISTRY (e_comp_editor_registry_new ());
	
	calendar_config_init ();

#if 0
	itip_control_factory_init ();
	component_editor_factory_init ();
#endif

	launch_alarm_daemon ();
}


static BonoboObject *
factory (BonoboGenericFactory *factory,
	 const char *component_id,
	 void *closure)
{
	static gboolean initialized = FALSE;

	if (! initialized) {
		initialize ();
		initialized = TRUE;
	}

	if (strcmp (component_id, CALENDAR_COMPONENT_ID) == 0)
		return calendar_component_get_object ();
	if (strcmp (component_id, CALENDAR_CONTROL_ID) == 0)
		return BONOBO_OBJECT (control_factory_new_control ());
	if (strcmp (component_id, TASKS_CONTROL_ID) == 0)
		return BONOBO_OBJECT (tasks_control_new ());
	if (strcmp (component_id, ITIP_CONTROL_ID) == 0)
		return BONOBO_OBJECT (itip_bonobo_control_new ());
	if (strcmp (component_id, CONFIG_CONTROL_ID) == 0) {
		extern EvolutionShellClient *global_shell_client; /* FIXME ugly */

		if (global_shell_client == NULL)
			return NULL;
		else
			return BONOBO_OBJECT (cal_prefs_dialog_new ());
	}
	if (strcmp (component_id, COMP_EDITOR_FACTORY_ID) == 0)
		return BONOBO_OBJECT (comp_editor_factory_fn ());

	g_warning (FACTORY_ID ": Don't know what to do with %s", component_id);
	return NULL;
}

BONOBO_ACTIVATION_SHLIB_FACTORY (FACTORY_ID, "Evolution Calendar component factory", factory, NULL)
