/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* evolution-shell-component-client.c
 *
 * Copyright (C) 2000, 2001 Ximian, Inc.
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
#include <gtk/gtksignal.h>
#include <gtk/gtktypeutils.h>

#include <liboaf/liboaf.h>

#include <bonobo/bonobo-main.h>
#include <bonobo/bonobo-widget.h>

#include <gal/util/e-util.h>

#include "evolution-shell-component-client.h"


char *evolution_debug_log;

#define PARENT_TYPE BONOBO_OBJECT_CLIENT_TYPE
static BonoboObjectClass *parent_class = NULL;

struct _EvolutionShellComponentClientPrivate {
	char *id;

	EvolutionShellComponentClientCallback callback;
	void *callback_data;

	GNOME_Evolution_ShellComponentListener listener_interface;
	PortableServer_Servant listener_servant;

	GNOME_Evolution_ShellComponentDnd_SourceFolder dnd_source_folder_interface;
	GNOME_Evolution_ShellComponentDnd_DestinationFolder dnd_destination_folder_interface;
	GNOME_Evolution_Offline offline_interface;
};


#define RETURN_ERROR_IF_FAIL(cond) \
	g_return_val_if_fail ((cond), EVOLUTION_SHELL_COMPONENT_INVALIDARG)


/* Utility functions.  */

static EvolutionShellComponentResult
corba_exception_to_result (const CORBA_Environment *ev)
{
	if (ev->_major == CORBA_NO_EXCEPTION)
		return EVOLUTION_SHELL_COMPONENT_OK;

	if (ev->_major == CORBA_USER_EXCEPTION) {
		if (strcmp (ev->_repo_id, ex_GNOME_Evolution_ShellComponent_AlreadyOwned) == 0)
			return EVOLUTION_SHELL_COMPONENT_ALREADYOWNED;
		if (strcmp (ev->_repo_id, ex_GNOME_Evolution_ShellComponent_OldOwnerHasDied) == 0)
			return EVOLUTION_SHELL_COMPONENT_OLDOWNERHASDIED;
		if (strcmp (ev->_repo_id, ex_GNOME_Evolution_ShellComponent_NotOwned) == 0)
			return EVOLUTION_SHELL_COMPONENT_NOTOWNED;
		if (strcmp (ev->_repo_id, ex_GNOME_Evolution_ShellComponent_NotFound) == 0)
			return EVOLUTION_SHELL_COMPONENT_NOTFOUND;
		if (strcmp (ev->_repo_id, ex_GNOME_Evolution_ShellComponent_UnsupportedType) == 0)
			return EVOLUTION_SHELL_COMPONENT_UNSUPPORTEDTYPE;
		if (strcmp (ev->_repo_id, ex_GNOME_Evolution_ShellComponent_InternalError) == 0)
			return EVOLUTION_SHELL_COMPONENT_INTERNALERROR;
		if (strcmp (ev->_repo_id, ex_GNOME_Evolution_ShellComponent_Busy) == 0)
			return EVOLUTION_SHELL_COMPONENT_BUSY;
		if (strcmp (ev->_repo_id, ex_GNOME_Evolution_ShellComponent_UnsupportedSchema) == 0)
			return EVOLUTION_SHELL_COMPONENT_UNSUPPORTEDSCHEMA;

		return EVOLUTION_SHELL_COMPONENT_UNKNOWNERROR;
	} else {
		/* FIXME maybe we need something more specific here.  */
		return EVOLUTION_SHELL_COMPONENT_CORBAERROR;
	}
}

static EvolutionShellComponentResult
shell_component_result_from_corba_exception (const CORBA_Environment *ev)
{
	if (ev->_major == CORBA_NO_EXCEPTION)
		return EVOLUTION_SHELL_COMPONENT_OK;
	if (ev->_major == CORBA_SYSTEM_EXCEPTION)
		return EVOLUTION_SHELL_COMPONENT_CORBAERROR;
	return EVOLUTION_SHELL_COMPONENT_CORBAERROR; /* FIXME? */
}


/* CORBA listener interface implementation.  */

static PortableServer_ServantBase__epv            ShellComponentListener_base_epv;
static POA_GNOME_Evolution_ShellComponentListener__epv  ShellComponentListener_epv;
static POA_GNOME_Evolution_ShellComponentListener__vepv ShellComponentListener_vepv;
static gboolean ShellComponentListener_vepv_initialized = FALSE;

static void ShellComponentListener_vepv_initialize (void);
static void dispatch_callback (EvolutionShellComponentClient *shell_component_client,
			       EvolutionShellComponentResult result);

struct _ShellComponentListenerServant {
	POA_GNOME_Evolution_ShellComponentListener servant;
	EvolutionShellComponentClient *component_client;
};
typedef struct _ShellComponentListenerServant ShellComponentListenerServant;

static PortableServer_Servant *
create_ShellComponentListener_servant (EvolutionShellComponentClient *component_client)
{
	ShellComponentListenerServant *servant;

	if (! ShellComponentListener_vepv_initialized)
		ShellComponentListener_vepv_initialize ();

	servant = g_new0 (ShellComponentListenerServant, 1);
	servant->servant.vepv     = &ShellComponentListener_vepv;
	servant->component_client = component_client;

	return (PortableServer_Servant) servant;
}

static void
free_ShellComponentListener_servant (PortableServer_Servant servant)
{
	g_free (servant);
}

static EvolutionShellComponentClient *
component_client_from_ShellComponentListener_servant (PortableServer_Servant servant)
{
	ShellComponentListenerServant *listener_servant;

	listener_servant = (ShellComponentListenerServant *) servant;
	return listener_servant->component_client;
}

static EvolutionShellComponentResult
result_from_async_corba_result (GNOME_Evolution_ShellComponentListener_Result async_corba_result)
{
	switch (async_corba_result) {
	case GNOME_Evolution_ShellComponentListener_OK:
		return EVOLUTION_SHELL_COMPONENT_OK;
	case GNOME_Evolution_ShellComponentListener_CANCEL:
		return EVOLUTION_SHELL_COMPONENT_CANCEL;
	case GNOME_Evolution_ShellComponentListener_UNSUPPORTED_OPERATION:
		return EVOLUTION_SHELL_COMPONENT_UNSUPPORTEDOPERATION;
	case GNOME_Evolution_ShellComponentListener_EXISTS:
		return EVOLUTION_SHELL_COMPONENT_EXISTS;
	case GNOME_Evolution_ShellComponentListener_INVALID_URI:
		return EVOLUTION_SHELL_COMPONENT_INVALIDURI;
	case GNOME_Evolution_ShellComponentListener_PERMISSION_DENIED:
		return EVOLUTION_SHELL_COMPONENT_PERMISSIONDENIED;
	case GNOME_Evolution_ShellComponentListener_HAS_SUBFOLDERS:
		return EVOLUTION_SHELL_COMPONENT_HASSUBFOLDERS;
	case GNOME_Evolution_ShellComponentListener_NO_SPACE:
		return EVOLUTION_SHELL_COMPONENT_NOSPACE;
	default:
		return EVOLUTION_SHELL_COMPONENT_UNKNOWNERROR;
	}
}

static void
impl_ShellComponentListener_report_result (PortableServer_Servant servant,
					   const GNOME_Evolution_ShellComponentListener_Result result,
					   CORBA_Environment *ev)
{
	EvolutionShellComponentClient *component_client;

	component_client = component_client_from_ShellComponentListener_servant (servant);
	dispatch_callback (component_client, result_from_async_corba_result (result));
}

static void
ShellComponentListener_vepv_initialize (void)
{
	ShellComponentListener_base_epv._private = NULL;
	ShellComponentListener_base_epv.finalize = NULL;
	ShellComponentListener_base_epv.default_POA = NULL;

	ShellComponentListener_epv.notifyResult = impl_ShellComponentListener_report_result;

	ShellComponentListener_vepv._base_epv = & ShellComponentListener_base_epv;
	ShellComponentListener_vepv.GNOME_Evolution_ShellComponentListener_epv = & ShellComponentListener_epv;

	ShellComponentListener_vepv_initialized = TRUE;
}

static void
create_listener_interface (EvolutionShellComponentClient *shell_component_client)
{
	EvolutionShellComponentClientPrivate *priv;
	PortableServer_Servant listener_servant;
	GNOME_Evolution_ShellComponentListener corba_interface;
	CORBA_Environment ev;

	priv = shell_component_client->priv;

	listener_servant = create_ShellComponentListener_servant (shell_component_client);

	CORBA_exception_init (&ev);

	POA_GNOME_Evolution_ShellComponentListener__init (listener_servant, &ev);
	if (ev._major != CORBA_NO_EXCEPTION) {
		free_ShellComponentListener_servant (listener_servant);
		return;
	}

	CORBA_free (PortableServer_POA_activate_object (bonobo_poa (), listener_servant, &ev));

	corba_interface = PortableServer_POA_servant_to_reference (bonobo_poa (), listener_servant, &ev);
	if (ev._major != CORBA_NO_EXCEPTION) {
		corba_interface = CORBA_OBJECT_NIL;
		free_ShellComponentListener_servant (listener_servant);
	}

	CORBA_exception_free (&ev);

	priv->listener_servant   = listener_servant;
	priv->listener_interface = corba_interface;
}

static void
destroy_listener_interface (EvolutionShellComponentClient *client)
{
	EvolutionShellComponentClientPrivate *priv;
	CORBA_Environment ev;
	PortableServer_ObjectId *oid;

	priv = client->priv;
	CORBA_exception_init (&ev);

	oid = PortableServer_POA_servant_to_id (bonobo_poa (), priv->listener_servant, &ev);
	PortableServer_POA_deactivate_object (bonobo_poa (), oid, &ev);
	POA_GNOME_Evolution_ShellComponentListener__fini (priv->listener_servant, &ev);
	CORBA_free (oid);

	CORBA_Object_release (priv->listener_interface, &ev);
	free_ShellComponentListener_servant (priv->listener_servant);

	CORBA_exception_free (&ev);
}

static void
dispatch_callback (EvolutionShellComponentClient *shell_component_client,
		   EvolutionShellComponentResult result)
{
	EvolutionShellComponentClientPrivate *priv;
	EvolutionShellComponentClientCallback callback;
	void *callback_data;

	priv = shell_component_client->priv;

	g_return_if_fail (priv->callback != NULL);
	g_return_if_fail (priv->listener_servant != NULL);

	/* Notice that we destroy the interface and reset the callback information before
           dispatching the callback so that the callback can generate another request.  */

	destroy_listener_interface (shell_component_client);

	priv->listener_servant   = NULL;
	priv->listener_interface = CORBA_OBJECT_NIL;

	callback      = priv->callback;
	callback_data = priv->callback_data;

	priv->callback      = NULL;
	priv->callback_data = NULL;

	(* callback) (shell_component_client, result, callback_data);
}


/* GtkObject methods.  */

static void
impl_destroy (GtkObject *object)
{
	EvolutionShellComponentClient *shell_component_client;
	EvolutionShellComponentClientPrivate *priv;
	CORBA_Environment ev;

	shell_component_client = EVOLUTION_SHELL_COMPONENT_CLIENT (object);
	priv = shell_component_client->priv;

	g_free (priv->id);

	if (priv->callback != NULL)
		dispatch_callback (shell_component_client, EVOLUTION_SHELL_COMPONENT_INTERRUPTED);

	CORBA_exception_init (&ev);

	if (priv->dnd_source_folder_interface != CORBA_OBJECT_NIL) {
		Bonobo_Unknown_unref (priv->dnd_source_folder_interface, &ev);
		CORBA_Object_release (priv->dnd_source_folder_interface, &ev);
	}

	if (priv->dnd_destination_folder_interface != CORBA_OBJECT_NIL) {
		Bonobo_Unknown_unref (priv->dnd_destination_folder_interface, &ev);
		CORBA_Object_release (priv->dnd_destination_folder_interface, &ev);
	}

	if (priv->offline_interface != CORBA_OBJECT_NIL) {
		Bonobo_Unknown_unref (priv->offline_interface, &ev);
		CORBA_Object_release (priv->offline_interface, &ev);
	}

	if (priv->listener_interface != CORBA_OBJECT_NIL)
		destroy_listener_interface (shell_component_client);

	CORBA_exception_free (&ev);

	g_free (priv);

	(* GTK_OBJECT_CLASS (parent_class)->destroy) (object);
}


static void
class_init (EvolutionShellComponentClientClass *klass)
{
	GtkObjectClass *object_class;

	object_class = GTK_OBJECT_CLASS (klass);
	parent_class = gtk_type_class (PARENT_TYPE);

	object_class->destroy = impl_destroy;
}

static void
init (EvolutionShellComponentClient *shell_component_client)
{
	EvolutionShellComponentClientPrivate *priv;

	priv = g_new (EvolutionShellComponentClientPrivate, 1);

	priv->id                               = NULL;

	priv->listener_interface               = CORBA_OBJECT_NIL;
	priv->listener_servant                 = NULL;

	priv->callback                         = NULL;
	priv->callback_data                    = NULL;

	priv->dnd_source_folder_interface      = CORBA_OBJECT_NIL;
	priv->dnd_destination_folder_interface = CORBA_OBJECT_NIL;
	priv->offline_interface                = CORBA_OBJECT_NIL;

	shell_component_client->priv = priv;
}


/* Construction.  */

void
evolution_shell_component_client_construct (EvolutionShellComponentClient *shell_component_client,
					    const char *id,
					    CORBA_Object corba_object)
{
	EvolutionShellComponentClientPrivate *priv;

	g_return_if_fail (shell_component_client != NULL);
	g_return_if_fail (EVOLUTION_IS_SHELL_COMPONENT_CLIENT (shell_component_client));
	g_return_if_fail (corba_object != CORBA_OBJECT_NIL);

	priv = shell_component_client->priv;
	priv->id = g_strdup (id);

	bonobo_object_client_construct (BONOBO_OBJECT_CLIENT (shell_component_client),
					corba_object);
}

EvolutionShellComponentClient *
evolution_shell_component_client_new (const char *id,
				      CORBA_Environment *ev)
{
	EvolutionShellComponentClient *new;
	CORBA_Object corba_object;
	CORBA_Environment *local_ev;
	CORBA_Environment static_ev;

	g_return_val_if_fail (id != NULL, NULL);

	CORBA_exception_init (&static_ev);

	if (ev == NULL)
		local_ev = &static_ev;
	else
		local_ev = ev;

	corba_object = oaf_activate_from_id ((char *) id, 0, NULL, ev);
	if (ev->_major != CORBA_NO_EXCEPTION || corba_object == NULL) {
		CORBA_exception_free (&static_ev);
		return NULL;
	}

	CORBA_exception_free (&static_ev);

	new = gtk_type_new (evolution_shell_component_client_get_type ());
	evolution_shell_component_client_construct (new, id, corba_object);

	return new;
}


/* Properties.  */

const char *
evolution_shell_component_client_get_id (EvolutionShellComponentClient *shell_component_client)
{
	EvolutionShellComponentClientPrivate *priv;

	g_return_val_if_fail (shell_component_client != NULL, NULL);
	g_return_val_if_fail (EVOLUTION_IS_SHELL_COMPONENT_CLIENT (shell_component_client), NULL);

	priv = shell_component_client->priv;

	return priv->id;
}


/* Querying DnD interfaces.  */

GNOME_Evolution_ShellComponentDnd_SourceFolder
evolution_shell_component_client_get_dnd_source_interface (EvolutionShellComponentClient *shell_component_client)
{
	EvolutionShellComponentClientPrivate *priv;
	GNOME_Evolution_ShellComponentDnd_SourceFolder interface;
	CORBA_Environment ev;

	g_return_val_if_fail (shell_component_client != NULL, CORBA_OBJECT_NIL);
	g_return_val_if_fail (EVOLUTION_IS_SHELL_COMPONENT_CLIENT (shell_component_client), CORBA_OBJECT_NIL);

	priv = shell_component_client->priv;

	if (priv->dnd_source_folder_interface != CORBA_OBJECT_NIL)
		return priv->dnd_source_folder_interface;

	CORBA_exception_init (&ev);

	interface = Bonobo_Unknown_queryInterface (bonobo_object_corba_objref (BONOBO_OBJECT (shell_component_client)),
						   "IDL:GNOME/Evolution/ShellComponentDnd/SourceFolder:1.0",
						   &ev);

	if (ev._major != CORBA_NO_EXCEPTION)
		interface = CORBA_OBJECT_NIL;

	CORBA_exception_free (&ev);

	priv->dnd_source_folder_interface = interface;
	return interface;
}

GNOME_Evolution_ShellComponentDnd_DestinationFolder
evolution_shell_component_client_get_dnd_destination_interface (EvolutionShellComponentClient *shell_component_client)
{
	EvolutionShellComponentClientPrivate *priv;
	GNOME_Evolution_ShellComponentDnd_DestinationFolder interface;
	CORBA_Environment ev;

	g_return_val_if_fail (shell_component_client != NULL, CORBA_OBJECT_NIL);
	g_return_val_if_fail (EVOLUTION_IS_SHELL_COMPONENT_CLIENT (shell_component_client), CORBA_OBJECT_NIL);

	priv = shell_component_client->priv;

	if (priv->dnd_destination_folder_interface != CORBA_OBJECT_NIL)
		return priv->dnd_destination_folder_interface;

	CORBA_exception_init (&ev);

	interface = Bonobo_Unknown_queryInterface (bonobo_object_corba_objref (BONOBO_OBJECT (shell_component_client)),
						   "IDL:GNOME/Evolution/ShellComponentDnd/DestinationFolder:1.0",
						   &ev);

	if (ev._major != CORBA_NO_EXCEPTION)
		interface = CORBA_OBJECT_NIL;

	CORBA_exception_free (&ev);

	priv->dnd_destination_folder_interface = interface;
	return interface;
}


/* Querying the offline interface.  */

GNOME_Evolution_Offline
evolution_shell_component_client_get_offline_interface (EvolutionShellComponentClient *shell_component_client)
{
	EvolutionShellComponentClientPrivate *priv;
	GNOME_Evolution_Offline interface;
	CORBA_Environment ev;

	priv = shell_component_client->priv;

	if (priv->offline_interface != CORBA_OBJECT_NIL)
		return priv->offline_interface;

	CORBA_exception_init (&ev);

	interface = Bonobo_Unknown_queryInterface (bonobo_object_corba_objref (BONOBO_OBJECT (shell_component_client)),
						   "IDL:GNOME/Evolution/Offline:1.0",
						   &ev);

	if (ev._major != CORBA_NO_EXCEPTION)
		interface = CORBA_OBJECT_NIL;

	CORBA_exception_free (&ev);

	priv->offline_interface = interface;
	return interface;
}


/* Synchronous operations.  */

EvolutionShellComponentResult
evolution_shell_component_client_set_owner (EvolutionShellComponentClient *shell_component_client,
					    GNOME_Evolution_Shell shell,
					    const char *evolution_homedir)
{
	EvolutionShellComponentResult result;
	CORBA_Environment ev;

	RETURN_ERROR_IF_FAIL (shell_component_client != NULL);
	RETURN_ERROR_IF_FAIL (EVOLUTION_IS_SHELL_COMPONENT_CLIENT (shell_component_client));
	RETURN_ERROR_IF_FAIL (shell != CORBA_OBJECT_NIL);

	CORBA_exception_init (&ev);

	GNOME_Evolution_ShellComponent_setOwner (bonobo_object_corba_objref (BONOBO_OBJECT (shell_component_client)),
					    shell, evolution_homedir, &ev);

	result = corba_exception_to_result (&ev);

	if (result == EVOLUTION_SHELL_COMPONENT_OK && evolution_debug_log)
		GNOME_Evolution_ShellComponent_debug (bonobo_object_corba_objref (BONOBO_OBJECT (shell_component_client)), evolution_debug_log, &ev);

	CORBA_exception_free (&ev);

	return result;
}

EvolutionShellComponentResult
evolution_shell_component_client_unset_owner (EvolutionShellComponentClient *shell_component_client,
					      GNOME_Evolution_Shell shell)
{
	EvolutionShellComponentResult result;
	GNOME_Evolution_ShellComponent corba_component;
	CORBA_Environment ev;

	RETURN_ERROR_IF_FAIL (shell_component_client != NULL);
	RETURN_ERROR_IF_FAIL (EVOLUTION_IS_SHELL_COMPONENT_CLIENT (shell_component_client));
	RETURN_ERROR_IF_FAIL (shell != CORBA_OBJECT_NIL);

	CORBA_exception_init (&ev);

	corba_component = bonobo_object_corba_objref (BONOBO_OBJECT (shell_component_client));

	GNOME_Evolution_ShellComponent_unsetOwner (corba_component, &ev);

	result = corba_exception_to_result (&ev);

	CORBA_exception_free (&ev);

	return result;
}

EvolutionShellComponentResult
evolution_shell_component_client_create_view (EvolutionShellComponentClient *shell_component_client,
					      BonoboUIComponent *uih,
					      const char *physical_uri,
					      const char *type_string,
					      const char *view_info,
					      BonoboControl **control_return)
{
	EvolutionShellComponentResult result;
	CORBA_Environment ev;
	GNOME_Evolution_ShellComponent corba_component;
	Bonobo_Control corba_control;

	RETURN_ERROR_IF_FAIL (shell_component_client != NULL);
	RETURN_ERROR_IF_FAIL (EVOLUTION_IS_SHELL_COMPONENT_CLIENT (shell_component_client));
	RETURN_ERROR_IF_FAIL (uih != NULL);
	RETURN_ERROR_IF_FAIL (BONOBO_IS_UI_COMPONENT (uih));
	RETURN_ERROR_IF_FAIL (physical_uri != NULL);
	RETURN_ERROR_IF_FAIL (type_string != NULL);
	RETURN_ERROR_IF_FAIL (view_info != NULL);
	RETURN_ERROR_IF_FAIL (control_return != NULL);

	CORBA_exception_init (&ev);

	corba_component = bonobo_object_corba_objref (BONOBO_OBJECT (shell_component_client));
	corba_control = GNOME_Evolution_ShellComponent_createView (corba_component, physical_uri, type_string, view_info, &ev);

	result = corba_exception_to_result (&ev);

	if (result != EVOLUTION_SHELL_COMPONENT_OK) {
		*control_return = NULL;
	} else {
		Bonobo_UIContainer corba_uih;

		corba_uih = bonobo_object_corba_objref (BONOBO_OBJECT (uih));
		*control_return = BONOBO_CONTROL (bonobo_widget_new_control_from_objref (corba_control,
											 corba_uih));
	}

	CORBA_exception_free (&ev);

	return result;
}

EvolutionShellComponentResult
evolution_shell_component_client_handle_external_uri  (EvolutionShellComponentClient *shell_component_client,
						       const char *uri)
{
	GNOME_Evolution_ShellComponent corba_component;
	CORBA_Environment ev;
	EvolutionShellComponentResult result;

	RETURN_ERROR_IF_FAIL (shell_component_client != NULL);
	RETURN_ERROR_IF_FAIL (EVOLUTION_IS_SHELL_COMPONENT_CLIENT (shell_component_client));
	RETURN_ERROR_IF_FAIL (uri != NULL);

	CORBA_exception_init (&ev);

	corba_component = bonobo_object_corba_objref (BONOBO_OBJECT (shell_component_client));
	GNOME_Evolution_ShellComponent_handleExternalURI (corba_component, uri, &ev);

	result = corba_exception_to_result (&ev);

	CORBA_exception_free (&ev);

	return result;
}


/* Asyncronous operations.  */

void
evolution_shell_component_client_async_create_folder (EvolutionShellComponentClient *shell_component_client,
						      const char *physical_uri,
						      const char *type,
						      EvolutionShellComponentClientCallback callback,
						      void *data)
{
	EvolutionShellComponentClientPrivate *priv;
	GNOME_Evolution_ShellComponent corba_shell_component;
	CORBA_Environment ev;

	g_return_if_fail (shell_component_client != NULL);
	g_return_if_fail (EVOLUTION_IS_SHELL_COMPONENT_CLIENT (shell_component_client));
	g_return_if_fail (physical_uri != NULL);
	g_return_if_fail (type != NULL);
	g_return_if_fail (callback != NULL);

	priv = shell_component_client->priv;

	if (priv->callback != NULL) {
		(* callback) (shell_component_client, EVOLUTION_SHELL_COMPONENT_BUSY, data);
		return;
	}

	create_listener_interface (shell_component_client);

	CORBA_exception_init (&ev);

	corba_shell_component = bonobo_object_corba_objref (BONOBO_OBJECT (shell_component_client));

	priv->callback      = callback;
	priv->callback_data = data;

	GNOME_Evolution_ShellComponent_createFolderAsync (corba_shell_component,
							  priv->listener_interface,
							  physical_uri, type,
							  &ev);

	if (ev._major != CORBA_NO_EXCEPTION && priv->callback != NULL) {
		(* callback) (shell_component_client,
			      shell_component_result_from_corba_exception (&ev),
			      data);
		priv->callback = NULL;
		priv->callback_data = NULL;
	}

	CORBA_exception_free (&ev);
}

void
evolution_shell_component_client_async_remove_folder (EvolutionShellComponentClient *shell_component_client,
						      const char *physical_uri,
						      const char *type,
						      EvolutionShellComponentClientCallback callback,
						      void *data)
{
	EvolutionShellComponentClientPrivate *priv;
	GNOME_Evolution_ShellComponent corba_shell_component;
	CORBA_Environment ev;

	g_return_if_fail (shell_component_client != NULL);
	g_return_if_fail (EVOLUTION_IS_SHELL_COMPONENT_CLIENT (shell_component_client));
	g_return_if_fail (physical_uri != NULL);
	g_return_if_fail (callback != NULL);

	priv = shell_component_client->priv;

	if (priv->callback != NULL) {
		(* callback) (shell_component_client, EVOLUTION_SHELL_COMPONENT_BUSY, data);
		return;
	}

	create_listener_interface (shell_component_client);

	CORBA_exception_init (&ev);

	corba_shell_component = bonobo_object_corba_objref (BONOBO_OBJECT (shell_component_client));

	priv->callback = callback;
	priv->callback_data = data;

	GNOME_Evolution_ShellComponent_removeFolderAsync (corba_shell_component,
							  priv->listener_interface,
							  physical_uri,
							  type,
							  &ev);

	if (ev._major != CORBA_NO_EXCEPTION && priv->callback != NULL) {
		(* callback) (shell_component_client,
			      shell_component_result_from_corba_exception (&ev),
			      data);
		priv->callback = NULL;
		priv->callback_data = NULL;
	}

	CORBA_exception_free (&ev);
}

void
evolution_shell_component_client_async_xfer_folder (EvolutionShellComponentClient *shell_component_client,
						    const char *source_physical_uri,
						    const char *destination_physical_uri,
						    const char *type,
						    gboolean remove_source,
						    EvolutionShellComponentClientCallback callback,
						    void *data)
{
	EvolutionShellComponentClientPrivate *priv;
	GNOME_Evolution_ShellComponent corba_shell_component;
	CORBA_Environment ev;
	
	g_return_if_fail (shell_component_client != NULL);
	g_return_if_fail (EVOLUTION_IS_SHELL_COMPONENT_CLIENT (shell_component_client));
	g_return_if_fail (source_physical_uri != NULL);
	g_return_if_fail (destination_physical_uri != NULL);
	g_return_if_fail (data != NULL);

	priv = shell_component_client->priv;

	if (priv->callback != NULL) {
		(* callback) (shell_component_client, EVOLUTION_SHELL_COMPONENT_BUSY, data);
		return;
	}

	create_listener_interface (shell_component_client);

	CORBA_exception_init (&ev);

	corba_shell_component = bonobo_object_corba_objref (BONOBO_OBJECT (shell_component_client));

	priv->callback      = callback;
	priv->callback_data = data;

	GNOME_Evolution_ShellComponent_xferFolderAsync (corba_shell_component,
							priv->listener_interface,
							source_physical_uri,
							destination_physical_uri,
							type,
							remove_source,
							&ev);

	if (ev._major != CORBA_NO_EXCEPTION && priv->callback != NULL) {
		(* callback) (shell_component_client,
			      shell_component_result_from_corba_exception (&ev),
			      data);
		priv->callback = NULL;
		priv->callback_data = NULL;
	}

	CORBA_exception_free (&ev);
}

void
evolution_shell_component_client_populate_folder_context_menu (EvolutionShellComponentClient *shell_component_client,
							       BonoboUIContainer *container,
							       const char *physical_uri,
							       const char *type)
{
	Bonobo_UIContainer corba_container;
	EvolutionShellComponentClientPrivate *priv;
	GNOME_Evolution_ShellComponent corba_shell_component;
	CORBA_Environment ev;

	g_return_if_fail (shell_component_client != NULL);
	g_return_if_fail (EVOLUTION_IS_SHELL_COMPONENT_CLIENT (shell_component_client));
	g_return_if_fail (physical_uri != NULL);
	g_return_if_fail (type != NULL);

	priv = shell_component_client->priv;

	CORBA_exception_init (&ev);

	corba_shell_component = bonobo_object_corba_objref (BONOBO_OBJECT (shell_component_client));
	corba_container = bonobo_object_corba_objref (BONOBO_OBJECT (container));

	GNOME_Evolution_ShellComponent_populateFolderContextMenu (corba_shell_component,
								  corba_container,
								  physical_uri,
								  type,
								  &ev);

	CORBA_exception_free (&ev);
}

void
evolution_shell_component_client_unpopulate_folder_context_menu (EvolutionShellComponentClient *shell_component_client,
								 BonoboUIContainer *container,
								 const char *physical_uri,
								 const char *type)
{
	Bonobo_UIContainer corba_container;
	EvolutionShellComponentClientPrivate *priv;
	GNOME_Evolution_ShellComponent corba_shell_component;
	CORBA_Environment ev;

	g_return_if_fail (shell_component_client != NULL);
	g_return_if_fail (EVOLUTION_IS_SHELL_COMPONENT_CLIENT (shell_component_client));
	g_return_if_fail (physical_uri != NULL);
	g_return_if_fail (type != NULL);

	priv = shell_component_client->priv;

	CORBA_exception_init (&ev);

	corba_shell_component = bonobo_object_corba_objref (BONOBO_OBJECT (shell_component_client));
	corba_container = bonobo_object_corba_objref (BONOBO_OBJECT (container));

	GNOME_Evolution_ShellComponent_unpopulateFolderContextMenu (corba_shell_component,
								  corba_container,
								  physical_uri,
								  type,
								  &ev);

	CORBA_exception_free (&ev);
}


void
evolution_shell_component_client_request_quit (EvolutionShellComponentClient *shell_component_client,
					       EvolutionShellComponentClientCallback callback,
					       void *data)
{
	EvolutionShellComponentClientPrivate *priv;
	GNOME_Evolution_ShellComponent corba_shell_component;
	CORBA_Environment ev;

	g_return_if_fail (EVOLUTION_IS_SHELL_COMPONENT_CLIENT (shell_component_client));
	g_return_if_fail (callback != NULL);

	priv = shell_component_client->priv;

	if (priv->callback != NULL) {
		(* callback) (shell_component_client, EVOLUTION_SHELL_COMPONENT_BUSY, data);
		return;
	}

	create_listener_interface (shell_component_client);

	CORBA_exception_init (&ev);

	corba_shell_component = BONOBO_OBJREF (shell_component_client);

	priv->callback = callback;
	priv->callback_data = data;

	GNOME_Evolution_ShellComponent_requestQuit (corba_shell_component, priv->listener_interface, &ev);

	if (ev._major != CORBA_NO_EXCEPTION && priv->callback != NULL) {
		(* callback) (shell_component_client,
			      shell_component_result_from_corba_exception (&ev),
			      data);
		priv->callback = NULL;
		priv->callback_data = NULL;
	}

	CORBA_exception_free (&ev);
}


E_MAKE_TYPE (evolution_shell_component_client, "EvolutionShellComponentClient",
	     EvolutionShellComponentClient, class_init, init, PARENT_TYPE)
