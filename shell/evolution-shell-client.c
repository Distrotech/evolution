/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* evolution-shell-client.c
 *
 * Copyright (C) 2000  Ximian, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
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

#include <gtk/gtkmain.h>
#include <bonobo/bonobo-main.h>
#include <bonobo/bonobo-object.h>

#include <gal/util/e-util.h>

#include "evolution-shell-client.h"


struct _EvolutionShellClientPrivate {
	GNOME_Evolution_Activity activity_interface;
	GNOME_Evolution_Shortcuts shortcuts_interface;
};

#define PARENT_TYPE bonobo_object_client_get_type ()
static BonoboObjectClientClass *parent_class = NULL;


/* Easy-to-use wrapper for Evolution::user_select_folder.  */

static PortableServer_ServantBase__epv FolderSelectionListener_base_epv;
static POA_GNOME_Evolution_FolderSelectionListener__epv FolderSelectionListener_epv;
static POA_GNOME_Evolution_FolderSelectionListener__vepv FolderSelectionListener_vepv;
static gboolean FolderSelectionListener_vtables_initialized = FALSE;

struct _FolderSelectionListenerServant {
	POA_GNOME_Evolution_FolderSelectionListener servant;
	char **uri_return;
	char **physical_uri_return;
};
typedef struct _FolderSelectionListenerServant FolderSelectionListenerServant;


/* Helper functions.  */

static CORBA_Object
query_shell_interface (EvolutionShellClient *shell_client,
		       const char *interface_name)
{
	CORBA_Environment ev;
	CORBA_Object interface_object;
	EvolutionShellClientPrivate *priv;

	priv = shell_client->priv;

	CORBA_exception_init (&ev);

 	interface_object = Bonobo_Unknown_queryInterface (bonobo_object_corba_objref (BONOBO_OBJECT (shell_client)),
							  interface_name, &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {
		g_warning ("EvolutionShellClient: Error querying interface %s on %p -- %s",
			   interface_name, shell_client, ev._repo_id);
		interface_object = CORBA_OBJECT_NIL;
	} else if (CORBA_Object_is_nil (interface_object, &ev)) {
		g_warning ("No interface %s for ShellClient %p", interface_name, shell_client);
	}

	CORBA_exception_free (&ev);

	return interface_object;
}


static void
impl_FolderSelectionListener_selected (PortableServer_Servant servant,
				       const CORBA_char *uri,
				       const CORBA_char *physical_uri,
				       CORBA_Environment *ev)
{
	FolderSelectionListenerServant *listener_servant;

	listener_servant = (FolderSelectionListenerServant *) servant;

	if (listener_servant->uri_return != NULL)
		* (listener_servant->uri_return) = g_strdup (uri);

	if (listener_servant->physical_uri_return != NULL)
		* (listener_servant->physical_uri_return) = g_strdup (physical_uri);

	gtk_main_quit ();
}

static void
impl_FolderSelectionListener_cancel (PortableServer_Servant servant,
				     CORBA_Environment *ev)
{
	FolderSelectionListenerServant *listener_servant;

	listener_servant = (FolderSelectionListenerServant *) servant;

	if (listener_servant->uri_return != NULL)
		* (listener_servant->uri_return) = NULL;

	if (listener_servant->physical_uri_return != NULL)
		* (listener_servant->physical_uri_return) = NULL;

	gtk_main_quit ();
}	

static void
init_FolderSelectionListener_vtables (void)
{
	FolderSelectionListener_base_epv._private    = NULL;
	FolderSelectionListener_base_epv.finalize    = NULL;
	FolderSelectionListener_base_epv.default_POA = NULL;

	FolderSelectionListener_epv.notifySelected = impl_FolderSelectionListener_selected;
	FolderSelectionListener_epv.notifyCanceled = impl_FolderSelectionListener_cancel;

	FolderSelectionListener_vepv._base_epv                             = &FolderSelectionListener_base_epv;
	FolderSelectionListener_vepv.GNOME_Evolution_FolderSelectionListener_epv = &FolderSelectionListener_epv;
		
	FolderSelectionListener_vtables_initialized = TRUE;
}

static GNOME_Evolution_FolderSelectionListener
create_folder_selection_listener_interface (char **result,
					    char **uri_return,
					    char **physical_uri_return)
{
	GNOME_Evolution_FolderSelectionListener corba_interface;
	CORBA_Environment ev;
	FolderSelectionListenerServant *servant;
	PortableServer_Servant listener_servant;

	if (! FolderSelectionListener_vtables_initialized)
		init_FolderSelectionListener_vtables ();

	servant = g_new0 (FolderSelectionListenerServant, 1);
	servant->servant.vepv        = &FolderSelectionListener_vepv;
	servant->uri_return          = uri_return;
	servant->physical_uri_return = physical_uri_return;

	listener_servant = (PortableServer_Servant) servant;

	CORBA_exception_init (&ev);

	POA_GNOME_Evolution_FolderSelectionListener__init (listener_servant, &ev);
	if (ev._major != CORBA_NO_EXCEPTION) {
		g_free(servant);
		return CORBA_OBJECT_NIL;
	}

	CORBA_free (PortableServer_POA_activate_object (bonobo_poa (), listener_servant, &ev));

	corba_interface = PortableServer_POA_servant_to_reference (bonobo_poa (), listener_servant, &ev);
	if (ev._major != CORBA_NO_EXCEPTION)
		corba_interface = CORBA_OBJECT_NIL;

	CORBA_exception_free (&ev);

	return corba_interface;
}

static int
count_string_items (const char *list[])
{
	int i;

	if (list == NULL)
		return 0;

	for (i = 0; list[i] != NULL; i++)
		;

	return i;
}

static void
user_select_folder (EvolutionShellClient *shell_client,
		    const char *title,
		    const char *default_folder,
		    const char *possible_types[],
		    char **uri_return,
		    char **physical_uri_return)
{
	GNOME_Evolution_FolderSelectionListener listener_interface;
	GNOME_Evolution_Shell corba_shell;
	CORBA_Environment ev;
	GNOME_Evolution_Shell_FolderTypeNameList corba_type_name_list;
	int num_possible_types;
	char *result;

	result = NULL;

	if (uri_return != NULL)
		*uri_return = NULL;
	if (physical_uri_return != NULL)
		*physical_uri_return = NULL;

	listener_interface = create_folder_selection_listener_interface (&result, uri_return, 
									 physical_uri_return);
	if (listener_interface == CORBA_OBJECT_NIL)
		return;

	CORBA_exception_init (&ev);

	corba_shell = bonobo_object_corba_objref (BONOBO_OBJECT (shell_client));

	num_possible_types = count_string_items (possible_types);

	corba_type_name_list._length  = num_possible_types;
	corba_type_name_list._maximum = num_possible_types;
	corba_type_name_list._buffer  = (CORBA_char **) possible_types;

	GNOME_Evolution_Shell_selectUserFolder (corba_shell, listener_interface,
						title, default_folder, &corba_type_name_list,
						"", &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {
		CORBA_exception_free (&ev);
		return;
	}

	gtk_main();

	CORBA_Object_release (listener_interface, &ev);

	CORBA_exception_free (&ev);
}


/* GtkObject methods.  */

static void
destroy (GtkObject *object)
{
	EvolutionShellClient *shell_client;
	EvolutionShellClientPrivate *priv;
	CORBA_Environment ev;

	shell_client = EVOLUTION_SHELL_CLIENT (object);
	priv = shell_client->priv;

	CORBA_exception_init (&ev);

	if (priv->activity_interface != CORBA_OBJECT_NIL) {
		Bonobo_Unknown_unref (priv->activity_interface, &ev);
		if (ev._major != CORBA_NO_EXCEPTION)
			g_warning ("EvolutionShellClient::destroy: "
				   "Error unreffing the ::Activity interface -- %s\n",
				   ev._repo_id);
		CORBA_Object_release (priv->activity_interface, &ev);
	}

	if (priv->shortcuts_interface != CORBA_OBJECT_NIL) {
		Bonobo_Unknown_unref (priv->shortcuts_interface, &ev);
		if (ev._major != CORBA_NO_EXCEPTION)
			g_warning ("EvolutionShellClient::destroy: "
				   "Error unreffing the ::Shortcuts interface -- %s\n",
				   ev._repo_id);
		CORBA_Object_release (priv->shortcuts_interface, &ev);
	}

	CORBA_exception_free (&ev);

	g_free (priv);

	(* GTK_OBJECT_CLASS (parent_class)->destroy) (object);
}


static void
class_init (EvolutionShellClientClass *klass)
{
	GtkObjectClass *object_class;

	parent_class = gtk_type_class (bonobo_object_get_type ());

	object_class = GTK_OBJECT_CLASS (klass);

	object_class->destroy = destroy;
}

static void
init (EvolutionShellClient *shell_client)
{
	EvolutionShellClientPrivate *priv;

	priv = g_new (EvolutionShellClientPrivate, 1);
	priv->activity_interface  = CORBA_OBJECT_NIL;
	priv->shortcuts_interface = CORBA_OBJECT_NIL;

	shell_client->priv = priv;
}


/**
 * evolution_shell_client_construct:
 * @shell_client: 
 * @corba_shell: 
 * 
 * Construct @shell_client associating it to @corba_shell.
 **/
void
evolution_shell_client_construct (EvolutionShellClient *shell_client,
				  GNOME_Evolution_Shell corba_shell)
{
	EvolutionShellClientPrivate *priv;

	g_return_if_fail (shell_client != NULL);
	g_return_if_fail (EVOLUTION_IS_SHELL_CLIENT (shell_client));
	g_return_if_fail (corba_shell != CORBA_OBJECT_NIL);

	bonobo_object_construct (BONOBO_OBJECT (shell_client), (CORBA_Object) corba_shell);

	priv = shell_client->priv;
	g_return_if_fail (priv->activity_interface == CORBA_OBJECT_NIL);

	priv->activity_interface = query_shell_interface (shell_client, "IDL:GNOME/Evolution/Activity:1.0");
	priv->shortcuts_interface = query_shell_interface (shell_client, "IDL:GNOME/Evolution/Shortcuts:1.0");
}

/**
 * evolution_shell_client_new:
 * @corba_shell: A pointer to the CORBA Evolution::Shell interface.
 * 
 * Create a new client object for @corba_shell.
 * 
 * Return value: A pointer to the Evolution::Shell client BonoboObject.
 **/
EvolutionShellClient *
evolution_shell_client_new (GNOME_Evolution_Shell corba_shell)
{
	EvolutionShellClient *shell_client;

	shell_client = gtk_type_new (evolution_shell_client_get_type ());

	evolution_shell_client_construct (shell_client, corba_shell);

	if (bonobo_object_corba_objref (BONOBO_OBJECT (shell_client)) == CORBA_OBJECT_NIL) {
		bonobo_object_unref (BONOBO_OBJECT (shell_client));
		return NULL;
	}

	return shell_client;
}


/**
 * evolution_shell_client_user_select_folder:
 * @shell_client: A EvolutionShellClient object
 * @title: The title for the folder selection dialog
 * @default_folder: URI (physical or evolution:) of the folder initially selected on the dialog
 * @uri_return: 
 * @physical_uri_return: 
 * 
 * Pop up the shell's folder selection dialog with the specified @title and
 * @default_folder as the initially selected folder.  On return, set *@uri and
 * *@physical_uri to the evolution: URI and the physical URI of the selected
 * folder (or %NULL if the user cancelled the dialog).  (The dialog is modal.)
 **/
void
evolution_shell_client_user_select_folder (EvolutionShellClient *shell_client,
					   const char *title,
					   const char *default_folder,
					   const char *possible_types[],
					   char **uri_return,
					   char **physical_uri_return)
{
	g_return_if_fail (shell_client != NULL);
	g_return_if_fail (EVOLUTION_IS_SHELL_CLIENT (shell_client));
	g_return_if_fail (title != NULL);
	g_return_if_fail (default_folder != NULL);

	user_select_folder (shell_client, title, default_folder, possible_types,
			    uri_return, physical_uri_return);
}


/**
 * evolution_shell_client_get_activity_interface:
 * @shell_client: An EvolutionShellClient object
 * 
 * Get the GNOME::Evolution::Activity for the shell associated to
 * @shell_client.
 * 
 * Return value: A CORBA Object represeting the GNOME::Evolution::Activity
 * interface.
 **/
GNOME_Evolution_Activity
evolution_shell_client_get_activity_interface (EvolutionShellClient *shell_client)
{
	g_return_val_if_fail (shell_client != NULL, CORBA_OBJECT_NIL);
	g_return_val_if_fail (EVOLUTION_IS_SHELL_CLIENT (shell_client), CORBA_OBJECT_NIL);

	return shell_client->priv->activity_interface;
}

/**
 * evolution_shell_client_get_activity_interface:
 * @shell_client: An EvolutionShellClient object
 * 
 * Get the GNOME::Evolution::Shortcuts for the shell associated to
 * @shell_client.
 * 
 * Return value: A CORBA Object represeting the GNOME::Evolution::Shortcuts
 * interface.
 **/
GNOME_Evolution_Shortcuts
evolution_shell_client_get_shortcuts_interface  (EvolutionShellClient *shell_client)
{
	g_return_val_if_fail (shell_client != NULL, CORBA_OBJECT_NIL);
	g_return_val_if_fail (EVOLUTION_IS_SHELL_CLIENT (shell_client), CORBA_OBJECT_NIL);

	return shell_client->priv->shortcuts_interface;
}


/**
 * evolution_shell_client_get_local_storage:
 * @shell_client: An EvolutionShellClient object
 * 
 * Retrieve the local storage interface for this shell.
 * 
 * Return value: a pointer to the CORBA object implementing the local storage
 * in the shell associated with @shell_client.
 **/
GNOME_Evolution_Storage
evolution_shell_client_get_local_storage (EvolutionShellClient *shell_client)
{
	GNOME_Evolution_Shell corba_shell;
	GNOME_Evolution_Storage corba_local_storage;
	CORBA_Environment ev;

	g_return_val_if_fail (shell_client != NULL, CORBA_OBJECT_NIL);
	g_return_val_if_fail (EVOLUTION_IS_SHELL_CLIENT (shell_client), CORBA_OBJECT_NIL);

	CORBA_exception_init (&ev);

	corba_shell = bonobo_object_corba_objref (BONOBO_OBJECT (shell_client));
	if (corba_shell == CORBA_OBJECT_NIL) {
		g_warning ("evolution_shell_client_get_local_storage() invoked on an "
			   "EvolutionShellClient that doesn't have a CORBA objref???");
		CORBA_exception_free (&ev);
		return CORBA_OBJECT_NIL;
	}

	corba_local_storage = GNOME_Evolution_Shell_getLocalStorage (corba_shell, &ev);
	if (ev._major != CORBA_NO_EXCEPTION) {
		g_warning ("evolution_shell_client_get_local_storage() failing -- %s ???", ev._repo_id);
		CORBA_exception_free (&ev);
		return CORBA_OBJECT_NIL;
	}

	CORBA_exception_free (&ev);

	return corba_local_storage;
}

void
evolution_shell_client_set_line_status (EvolutionShellClient *shell_client,
					gboolean              line_status)
{
	GNOME_Evolution_Shell corba_shell;
	CORBA_Environment ev;

	g_return_if_fail (shell_client != NULL);
	g_return_if_fail (EVOLUTION_IS_SHELL_CLIENT (shell_client));

	CORBA_exception_init (&ev);

	corba_shell = bonobo_object_corba_objref (BONOBO_OBJECT (shell_client));
	if (corba_shell == CORBA_OBJECT_NIL)
		return;

	GNOME_Evolution_Shell_setLineStatus (corba_shell, line_status, &ev);

	CORBA_exception_free (&ev);
}


E_MAKE_TYPE (evolution_shell_client, "EvolutionShellClient", EvolutionShellClient, class_init, init, PARENT_TYPE)
