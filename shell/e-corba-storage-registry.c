/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-corba-storage-registry.c
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

#include <gal/util/e-util.h>

#include "e-corba-storage.h"

#include "e-corba-storage-registry.h"


#define PARENT_TYPE BONOBO_OBJECT_TYPE
static BonoboObjectClass *parent_class = NULL;

struct _ECorbaStorageRegistryPrivate {
	EStorageSet *storage_set;
};


/* CORBA interface implementation.  */

static POA_GNOME_Evolution_StorageRegistry__vepv storage_registry_vepv;

static POA_GNOME_Evolution_StorageRegistry *
create_servant (void)
{
	POA_GNOME_Evolution_StorageRegistry *servant;
	CORBA_Environment ev;

	servant = (POA_GNOME_Evolution_StorageRegistry *) g_new0 (BonoboObjectServant, 1);
	servant->vepv = &storage_registry_vepv;

	CORBA_exception_init (&ev);

	POA_GNOME_Evolution_StorageRegistry__init ((PortableServer_Servant) servant, &ev);
	if (ev._major != CORBA_NO_EXCEPTION) {
		g_free (servant);
		CORBA_exception_free (&ev);
		return NULL;
	}

	CORBA_exception_free (&ev);

	return servant;
}

static GNOME_Evolution_StorageListener
impl_StorageRegistry_addStorage (PortableServer_Servant servant,
				 const GNOME_Evolution_Storage storage_interface,
				 const CORBA_char *name,
				 const CORBA_char *toplevel_node_uri,
				 const CORBA_char *toplevel_node_type,
				 CORBA_Environment *ev)
{
	BonoboObject *bonobo_object;
	ECorbaStorageRegistry *storage_registry;
	ECorbaStorageRegistryPrivate *priv;
	EStorage *storage;
	GNOME_Evolution_StorageListener listener_interface;

	bonobo_object = bonobo_object_from_servant (servant);
	storage_registry = E_CORBA_STORAGE_REGISTRY (bonobo_object);
	priv = storage_registry->priv;

	if (toplevel_node_uri[0] == '\0')
		toplevel_node_uri = NULL;
	if (toplevel_node_type[0] == '\0')
		toplevel_node_type = NULL;

	storage = e_corba_storage_new (toplevel_node_uri,
				       toplevel_node_type,
				       storage_interface,
				       name);

	if (! e_storage_set_add_storage (priv->storage_set, storage)) {
		CORBA_exception_set (ev,
				     CORBA_USER_EXCEPTION,
				     ex_GNOME_Evolution_StorageRegistry_Exists,
				     NULL);
		return CORBA_OBJECT_NIL;
	}

	gtk_object_unref (GTK_OBJECT (storage));

	listener_interface = CORBA_Object_duplicate (e_corba_storage_get_StorageListener
						     (E_CORBA_STORAGE (storage)), ev);

	return listener_interface;
}

static void
impl_StorageRegistry_removeStorageByName (PortableServer_Servant servant,
					  const CORBA_char *name,
					  CORBA_Environment *ev)
{
	BonoboObject *bonobo_object;
	ECorbaStorageRegistry *storage_registry;
	ECorbaStorageRegistryPrivate *priv;
	EStorage *storage;

	bonobo_object = bonobo_object_from_servant (servant);
	storage_registry = E_CORBA_STORAGE_REGISTRY (bonobo_object);
	priv = storage_registry->priv;

	storage = e_storage_set_get_storage (priv->storage_set, name);
	if (storage == NULL) {
		CORBA_exception_set (ev, CORBA_USER_EXCEPTION,
				     ex_GNOME_Evolution_StorageRegistry_NotFound,
				     NULL);
		return;
	}

	/* FIXME: Yucky to get the storage by name and then remove it.  */
	/* FIXME: Check failure.  */
	e_storage_set_remove_storage (priv->storage_set, storage);
}


/* GtkObject methods.  */

static void
destroy (GtkObject *object)
{
	ECorbaStorageRegistry *corba_storage_registry;
	ECorbaStorageRegistryPrivate *priv;

	corba_storage_registry = E_CORBA_STORAGE_REGISTRY (object);
	priv = corba_storage_registry->priv;

	if (priv->storage_set != NULL)
		gtk_object_unref (GTK_OBJECT (priv->storage_set));
	g_free (priv);

	(* GTK_OBJECT_CLASS (parent_class)->destroy) (object);
}


/* Initialization.  */

static void
corba_class_init (void)
{
	POA_GNOME_Evolution_StorageRegistry__vepv *vepv;
	POA_GNOME_Evolution_StorageRegistry__epv *epv;
	PortableServer_ServantBase__epv *base_epv;

	base_epv = g_new0 (PortableServer_ServantBase__epv, 1);
	base_epv->_private    = NULL;
	base_epv->finalize    = NULL;
	base_epv->default_POA = NULL;

	epv = g_new0 (POA_GNOME_Evolution_StorageRegistry__epv, 1);
	epv->addStorage          = impl_StorageRegistry_addStorage;
	epv->removeStorageByName = impl_StorageRegistry_removeStorageByName;

	vepv = &storage_registry_vepv;
	vepv->_base_epv                     = base_epv;
	vepv->Bonobo_Unknown_epv            = bonobo_object_get_epv ();
	vepv->GNOME_Evolution_StorageRegistry_epv = epv;
}

static void
class_init (ECorbaStorageRegistryClass *klass)
{
	GtkObjectClass *object_class;

	object_class = GTK_OBJECT_CLASS (klass);
	object_class->destroy = destroy;

	parent_class = gtk_type_class (PARENT_TYPE);

	corba_class_init ();
}

static void
init (ECorbaStorageRegistry *corba_storage_registry)
{
	ECorbaStorageRegistryPrivate *priv;

	priv = g_new (ECorbaStorageRegistryPrivate, 1);
	priv->storage_set = NULL;

	corba_storage_registry->priv = priv;
}


void
e_corba_storage_registry_construct (ECorbaStorageRegistry *corba_storage_registry,
				    GNOME_Evolution_StorageRegistry corba_object,
				    EStorageSet *storage_set)
{
	ECorbaStorageRegistryPrivate *priv;

	g_return_if_fail (corba_storage_registry != NULL);
	g_return_if_fail (E_IS_CORBA_STORAGE_REGISTRY (corba_storage_registry));
	g_return_if_fail (corba_object != CORBA_OBJECT_NIL);

	bonobo_object_construct (BONOBO_OBJECT (corba_storage_registry), corba_object);

	priv = corba_storage_registry->priv;

	gtk_object_ref (GTK_OBJECT (storage_set));
	priv->storage_set = storage_set;
}

ECorbaStorageRegistry *
e_corba_storage_registry_new (EStorageSet *storage_set)
{
	ECorbaStorageRegistry *corba_storage_registry;
	POA_GNOME_Evolution_StorageRegistry *servant;
	GNOME_Evolution_StorageRegistry corba_object;

	g_return_val_if_fail (storage_set != NULL, NULL);
	g_return_val_if_fail (E_IS_STORAGE_SET (storage_set), NULL);

	servant = create_servant ();
	if (servant == NULL)
		return NULL;

	corba_storage_registry = gtk_type_new (e_corba_storage_registry_get_type ());

	corba_object = bonobo_object_activate_servant (BONOBO_OBJECT (corba_storage_registry),
						       servant);

	e_corba_storage_registry_construct (corba_storage_registry, corba_object, storage_set);

	return corba_storage_registry;
}


E_MAKE_TYPE (e_corba_storage_registry, "ECorbaStorageRegistry", ECorbaStorageRegistry, class_init, init, PARENT_TYPE)
