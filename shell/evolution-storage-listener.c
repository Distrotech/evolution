/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* evolution-storage-listener.c
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
 * Author: Ettore Perazzoli
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gtk/gtksignal.h>
#include <bonobo/bonobo-main.h>
#include <gal/util/e-util.h>

#include "evolution-storage-listener.h"

#include "e-shell-marshal.h"


#define PARENT_TYPE gtk_object_get_type ()
static GtkObjectClass *parent_class = NULL;

struct _EvolutionStorageListenerPrivate {
	GNOME_Evolution_StorageListener corba_objref;
	EvolutionStorageListenerServant *servant;
};


enum {
	DESTROYED,
	NEW_FOLDER,
	UPDATE_FOLDER,
	REMOVED_FOLDER,
	HAS_SUBFOLDERS,
	LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };


/* Evolution::StorageListener implementation.  */

static POA_GNOME_Evolution_StorageListener__vepv my_GNOME_Evolution_StorageListener_vepv;

static EvolutionStorageListener *
gtk_object_from_servant (PortableServer_Servant servant)
{
	EvolutionStorageListenerServant *my_servant;

	my_servant = (EvolutionStorageListenerServant *) servant;
	return my_servant->gtk_object;
}

static void
impl_GNOME_Evolution_StorageListener_notifyDestroyed (PortableServer_Servant servant,
						      CORBA_Environment *ev)
{
	EvolutionStorageListener *listener;
	EvolutionStorageListenerPrivate *priv;

	listener = gtk_object_from_servant (servant);
	priv = listener->priv;

	g_signal_emit (listener, signals[DESTROYED], 0);
}

static void
impl_GNOME_Evolution_StorageListener_notifyFolderCreated (PortableServer_Servant servant,
							  const CORBA_char *path,
							  const GNOME_Evolution_Folder *folder,
							  CORBA_Environment *ev)
{
	EvolutionStorageListener *listener;
	EvolutionStorageListenerPrivate *priv;

	listener = gtk_object_from_servant (servant);
	priv = listener->priv;

	g_signal_emit (listener, signals[NEW_FOLDER], 0, path, folder);
}

static void
impl_GNOME_Evolution_StorageListener_notifyFolderUpdated (PortableServer_Servant servant,
							  const CORBA_char *path,
							  CORBA_long unread_count,
							  CORBA_Environment *ev)
{
	EvolutionStorageListener *listener;
	EvolutionStorageListenerPrivate *priv;

	listener = gtk_object_from_servant (servant);
	priv = listener->priv;

	g_signal_emit (listener, signals[UPDATE_FOLDER], 0, path, unread_count);
}

static void
impl_GNOME_Evolution_StorageListener_notifyFolderRemoved (PortableServer_Servant servant,
							  const CORBA_char *path,
							  CORBA_Environment *ev)
{
	EvolutionStorageListener *listener;
	EvolutionStorageListenerPrivate *priv;

	listener = gtk_object_from_servant (servant);
	priv = listener->priv;

	g_signal_emit (listener, signals[REMOVED_FOLDER], 0, path);
}

static void
impl_GNOME_Evolution_StorageListener_notifyHasSubfolders (PortableServer_Servant servant,
							  const CORBA_char *path,
							  const CORBA_char *message,
							  CORBA_Environment *ev)
{
	EvolutionStorageListener *listener;
	EvolutionStorageListenerPrivate *priv;

	listener = gtk_object_from_servant (servant);
	priv = listener->priv;

	g_signal_emit (listener, signals[HAS_SUBFOLDERS], 0, path, message);
}

static EvolutionStorageListenerServant *
create_servant (EvolutionStorageListener *listener)
{
	EvolutionStorageListenerServant *servant;
	POA_GNOME_Evolution_StorageListener *corba_servant;
	CORBA_Environment ev;

	CORBA_exception_init (&ev);

	servant = g_new0 (EvolutionStorageListenerServant, 1);
	corba_servant = (POA_GNOME_Evolution_StorageListener *) servant;

	corba_servant->vepv = &my_GNOME_Evolution_StorageListener_vepv;
	POA_GNOME_Evolution_StorageListener__init ((PortableServer_Servant) corba_servant, &ev);
	if (ev._major != CORBA_NO_EXCEPTION) {
		g_free (servant);
		CORBA_exception_free (&ev);
		return NULL;
	}

	servant->gtk_object = listener;

	CORBA_exception_free (&ev);

	return servant;
}

static GNOME_Evolution_StorageListener
activate_servant (EvolutionStorageListener *listener,
		  POA_GNOME_Evolution_StorageListener *servant)
{
	GNOME_Evolution_StorageListener corba_object;
	CORBA_Environment ev;

	CORBA_exception_init (&ev);

	CORBA_free (PortableServer_POA_activate_object (bonobo_poa (), servant, &ev));

	corba_object = PortableServer_POA_servant_to_reference (bonobo_poa(), servant, &ev);

	if (ev._major == CORBA_NO_EXCEPTION && ! CORBA_Object_is_nil (corba_object, &ev)) {
		CORBA_exception_free (&ev);
		return corba_object;
	}

	CORBA_exception_free (&ev);

	return CORBA_OBJECT_NIL;
}


/* GObject methods.  */

static void
impl_finalize (GObject *object)
{
	EvolutionStorageListener *storage_listener;
	EvolutionStorageListenerPrivate *priv;
	CORBA_Environment ev;

	storage_listener = EVOLUTION_STORAGE_LISTENER (object);
	priv = storage_listener->priv;

	CORBA_exception_init (&ev);

	if (priv->corba_objref != CORBA_OBJECT_NIL)
		CORBA_Object_release (priv->corba_objref, &ev);

	if (priv->servant != NULL) {
		PortableServer_ObjectId *object_id;

		object_id = PortableServer_POA_servant_to_id (bonobo_poa(), priv->servant, &ev);
		PortableServer_POA_deactivate_object (bonobo_poa (), object_id, &ev);
		CORBA_free (object_id);
	}

	CORBA_exception_free (&ev);

	g_free (priv);

	(* G_OBJECT_CLASS (parent_class)->finalize) (object);
}


static void
corba_class_init (void)
{
	POA_GNOME_Evolution_StorageListener__vepv *vepv;
	POA_GNOME_Evolution_StorageListener__epv *epv;
	PortableServer_ServantBase__epv *base_epv;

	base_epv = g_new0 (PortableServer_ServantBase__epv, 1);
	base_epv->_private    = NULL;
	base_epv->finalize    = NULL;
	base_epv->default_POA = NULL;

	epv = g_new0 (POA_GNOME_Evolution_StorageListener__epv, 1);
	epv->notifyDestroyed     = impl_GNOME_Evolution_StorageListener_notifyDestroyed;
	epv->notifyFolderCreated = impl_GNOME_Evolution_StorageListener_notifyFolderCreated;
	epv->notifyFolderUpdated = impl_GNOME_Evolution_StorageListener_notifyFolderUpdated;
	epv->notifyFolderRemoved = impl_GNOME_Evolution_StorageListener_notifyFolderRemoved;
	epv->notifyHasSubfolders = impl_GNOME_Evolution_StorageListener_notifyHasSubfolders;

	vepv = & my_GNOME_Evolution_StorageListener_vepv;
	vepv->_base_epv                     = base_epv;
	vepv->GNOME_Evolution_StorageListener_epv = epv;
}

static void
class_init (EvolutionStorageListenerClass *klass)
{
	GObjectClass *object_class;

	parent_class = g_type_class_ref(PARENT_TYPE);

	object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = impl_finalize;

	signals[DESTROYED]
		= g_signal_new ("destroyed",
				G_OBJECT_CLASS_TYPE (object_class),
				G_SIGNAL_RUN_FIRST,
				G_STRUCT_OFFSET (EvolutionStorageListenerClass, destroyed),
				NULL, NULL,
				e_shell_marshal_NONE__NONE,
				G_TYPE_NONE, 0);

	signals[NEW_FOLDER]  
		= g_signal_new ("new_folder",
				G_OBJECT_CLASS_TYPE (object_class),
				G_SIGNAL_RUN_FIRST,
				G_STRUCT_OFFSET (EvolutionStorageListenerClass, new_folder),
				NULL, NULL,
				e_shell_marshal_NONE__STRING_POINTER,
				G_TYPE_NONE, 2,
				G_TYPE_STRING,
				G_TYPE_POINTER);

	signals[UPDATE_FOLDER]  
		= g_signal_new ("update_folder",
				G_OBJECT_CLASS_TYPE (object_class),
				G_SIGNAL_RUN_FIRST,
				G_STRUCT_OFFSET (EvolutionStorageListenerClass, update_folder),
				NULL, NULL,
				e_shell_marshal_NONE__STRING_INT,
				G_TYPE_NONE, 2,
				G_TYPE_STRING,
				G_TYPE_INT);

	signals[REMOVED_FOLDER] 
		= g_signal_new ("removed_folder",
				G_OBJECT_CLASS_TYPE (object_class),
				G_SIGNAL_RUN_FIRST,
				G_STRUCT_OFFSET (EvolutionStorageListenerClass, removed_folder),
				NULL, NULL,
				e_shell_marshal_NONE__STRING,
				G_TYPE_NONE, 1,
				G_TYPE_STRING);

	signals[HAS_SUBFOLDERS] 
		= g_signal_new ("has_subfolders",
				G_OBJECT_CLASS_TYPE (object_class),
				G_SIGNAL_RUN_FIRST,
				G_STRUCT_OFFSET (EvolutionStorageListenerClass, has_subfolders),
				NULL, NULL,
				e_shell_marshal_NONE__STRING_STRING,
				G_TYPE_NONE, 2,
				G_TYPE_STRING,
				G_TYPE_STRING);

	corba_class_init ();
}

static void
init (EvolutionStorageListener *storage_listener)
{
	EvolutionStorageListenerPrivate *priv;

	priv = g_new (EvolutionStorageListenerPrivate, 1);
	priv->corba_objref = CORBA_OBJECT_NIL;

	storage_listener->priv = priv;
}


void
evolution_storage_listener_construct (EvolutionStorageListener *listener,
				      GNOME_Evolution_StorageListener corba_objref)
{
	EvolutionStorageListenerPrivate *priv;

	g_return_if_fail (listener != NULL);
	g_return_if_fail (corba_objref != CORBA_OBJECT_NIL);

	priv = listener->priv;

	g_return_if_fail (priv->corba_objref == CORBA_OBJECT_NIL);

	priv->corba_objref = corba_objref;

	GTK_OBJECT_UNSET_FLAGS (GTK_OBJECT (listener), GTK_FLOATING);
}

EvolutionStorageListener *
evolution_storage_listener_new (void)
{
	EvolutionStorageListener *new;
	EvolutionStorageListenerPrivate *priv;
	GNOME_Evolution_StorageListener corba_objref;

	new = g_object_new (evolution_storage_listener_get_type (), NULL);
	priv = new->priv;

	priv->servant = create_servant (new);
	corba_objref = activate_servant (new, (POA_GNOME_Evolution_StorageListener *) priv->servant);

	evolution_storage_listener_construct (new, corba_objref);

	return new;
}


/**
 * evolution_storage_listener_corba_objref:
 * @listener: A pointer to an EvolutionStorageListener
 * 
 * Get the CORBA object reference for the interface embedded in this GTK+
 * object wrapper.
 * 
 * Return value: A pointer to the CORBA object reference.
 **/
GNOME_Evolution_StorageListener
evolution_storage_listener_corba_objref (EvolutionStorageListener *listener)
{
	EvolutionStorageListenerPrivate *priv;

	g_return_val_if_fail (listener != NULL, CORBA_OBJECT_NIL);
	g_return_val_if_fail (EVOLUTION_IS_STORAGE_LISTENER (listener), CORBA_OBJECT_NIL);

	priv = listener->priv;
	return priv->corba_objref;
}


E_MAKE_TYPE (evolution_storage_listener, "EvolutionStorageListener", EvolutionStorageListener,
	     class_init, init, PARENT_TYPE)
