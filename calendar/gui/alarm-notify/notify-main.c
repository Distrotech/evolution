/* Evolution calendar - Alarm notification service main file
 *
 * Copyright (C) 2000 Ximian, Inc.
 * Copyright (C) 2000 Ximian, Inc.
 *
 * Author: Federico Mena-Quintero <federico@ximian.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <glib.h>
#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-i18n.h>
#include <libgnomeui/gnome-init.h>
#include <libgnomevfs/gnome-vfs-init.h>
#include <glade/glade.h>
#include <bonobo/bonobo-main.h>
#include <bonobo/bonobo-generic-factory.h>
#include <liboaf/liboaf.h>
#include "alarm.h"
#include "alarm-queue.h"
#include "alarm-notify.h"



static BonoboGenericFactory *factory;

static AlarmNotify *alarm_notify_service;


/* Factory function for the alarm notify service; just creates and references a
 * singleton service object.
 */
static BonoboObject *
alarm_notify_factory_fn (BonoboGenericFactory *factory, void *data)
{
	if (!alarm_notify_service) {
		alarm_notify_service = alarm_notify_new ();
		if (!alarm_notify_service)
			return NULL;
	}

	bonobo_object_ref (BONOBO_OBJECT (alarm_notify_service));
	return BONOBO_OBJECT (alarm_notify_service);
}

int
main (int argc, char **argv)
{
	free (malloc (8));

	bindtextdomain (PACKAGE, EVOLUTION_LOCALEDIR);
	textdomain (PACKAGE);

	if (gnome_init_with_popt_table ("evolution-alarm-notify", VERSION, argc, argv,
					oaf_popt_options, 0, NULL) != 0)
		g_error (_("Could not initialize GNOME"));

	oaf_init (argc, argv);

	if (bonobo_init (CORBA_OBJECT_NIL, CORBA_OBJECT_NIL, CORBA_OBJECT_NIL) == FALSE)
		g_error (_("Could not initialize Bonobo"));

	if (!gnome_vfs_init ())
		g_error (_("Could not initialize gnome-vfs"));

	glade_gnome_init ();

	alarm_queue_init ();

	factory = bonobo_generic_factory_new ("OAFIID:GNOME_Evolution_Calendar_AlarmNotify_Factory",
					      alarm_notify_factory_fn, NULL);
	if (!factory)
		g_error (_("Could not create the alarm notify service factory"));

	bonobo_main ();

	bonobo_object_unref (BONOBO_OBJECT (factory));
	factory = NULL;

	alarm_queue_done ();
	alarm_done ();

	gnome_vfs_shutdown ();

	return 0;
}
