/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* tasks-control-factory.c
 *
 * Copyright (C) 2000  Ximian, Inc.
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
 * Authors: Ettore Perazzoli
 *	    Damon Chaplin <damon@ximian.com>
 */

#include <config.h>
#include <libgnomeui/gnome-dialog.h>
#include <libgnomeui/gnome-dialog-util.h>
#include <liboaf/liboaf.h>
#include <bonobo/bonobo-control.h>
#include <bonobo/bonobo-generic-factory.h>
#include <bonobo/bonobo-context.h>
#include "tasks-control-factory.h"
#include "tasks-control.h"


#define TASKS_CONTROL_FACTORY_ID   "OAFIID:GNOME_Evolution_Tasks_ControlFactory"


CORBA_Environment ev;
CORBA_ORB orb;

static BonoboObject *tasks_control_factory_fn	(BonoboGenericFactory	*Factory,
						 void			*data);


/* Registers the factory with Bonobo. Should be called on startup. */
void
tasks_control_factory_init		(void)
{
	static BonoboGenericFactory *factory = NULL;

	if (factory != NULL)
		return;

	factory = bonobo_generic_factory_new (TASKS_CONTROL_FACTORY_ID,
					      tasks_control_factory_fn, NULL);
	bonobo_running_context_auto_exit_unref (BONOBO_OBJECT (factory));

	if (factory == NULL)
		g_error ("I could not register a Tasks control factory.");
}


/* Callback factory function to create a tasks control. */
static BonoboObject *
tasks_control_factory_fn		(BonoboGenericFactory	*Factory,
					 void			*data)
{
	BonoboControl *control;

	control = tasks_control_new ();

	if (control)
		return BONOBO_OBJECT (control);
	else {
		gnome_warning_dialog (_("Could not create the tasks view.  Please check your "
					"ORBit and OAF setup."));
		return NULL;
	}
}
