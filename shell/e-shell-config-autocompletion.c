/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-shell-config-autocompletion.c - Configuration page for addressbook autocompletion.
 *
 * Copyright (C) 2002 Ximian, Inc.
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
 * Authors: Chris Lahey <clahey@ximian.com>
 *          Chris Toshok <toshok@ximian.com>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif


#include "e-shell-config-autocompletion.h"

#include "e-folder-list.h"

#include "Evolution.h"

#include <bonobo/bonobo-exception.h>

#include <libgnome/gnome-i18n.h>
#include <gtk/gtkwidget.h>
#include <gtk/gtksignal.h>

#include <gconf/gconf-client.h>


typedef struct {
	EvolutionConfigControl *config_control;

	GtkWidget *control_widget;

	EvolutionShellClient *shell_client;
} EvolutionAutocompletionConfig;

static void
folder_list_changed_callback (EFolderList *efl,
			      EvolutionAutocompletionConfig *ac)
{
	evolution_config_control_changed (ac->config_control);
}

static void
config_control_destroy_notify (void *data,
			       GObject *where_the_config_control_was)
{
	EvolutionAutocompletionConfig *ac = (EvolutionAutocompletionConfig *) data;

	g_object_unref (ac->shell_client);

	g_free (ac);
}


static void
config_control_apply_callback (EvolutionConfigControl *config_control,
			       EvolutionAutocompletionConfig *ac)
{
	GConfClient *client;
	char *xml;

	client = gconf_client_get_default ();

	xml = e_folder_list_get_xml (E_FOLDER_LIST (ac->control_widget));
	gconf_client_set_string (client, "/apps/evolution/addressbook/completion/uris", xml, NULL);
	g_free (xml);

	g_object_unref (client);
}

GtkWidget *
e_shell_config_autocompletion_create_widget (EShell *shell, EvolutionConfigControl *config_control)
{
	EvolutionAutocompletionConfig *ac;
	CORBA_Environment ev;
	GConfClient *client;
	static const char *possible_types[] = { "contacts/*", NULL };
	char *xml;

	ac = g_new0 (EvolutionAutocompletionConfig, 1);

	CORBA_exception_init (&ev);

	ac->shell_client = evolution_shell_client_new (BONOBO_OBJREF (shell));

	client = gconf_client_get_default ();
	xml = gconf_client_get_string (client, "/apps/evolution/addressbook/completion/uris", NULL);
	g_object_unref (client);

	ac->control_widget = e_folder_list_new (ac->shell_client, xml);
	g_free (xml);

	g_object_set((ac->control_widget),
			"title", _("Extra Completion folders"),
			"possible_types", possible_types,
			NULL);

	gtk_widget_show (ac->control_widget);

	ac->config_control = config_control;

	g_signal_connect (ac->control_widget, "changed",
			  G_CALLBACK (folder_list_changed_callback), ac);
	g_signal_connect (ac->config_control, "apply",
			  G_CALLBACK (config_control_apply_callback), ac);

	g_object_weak_ref (G_OBJECT (ac->config_control), config_control_destroy_notify, ac);

	CORBA_exception_free (&ev);

	return ac->control_widget;
}

