/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* evolution-intelligent-importer.c
 *
 * Copyright (C) 2000, 2001  Ximian, Inc.
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
 * Author: Iain Holmes  <iain@ximian.com>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "evolution-intelligent-importer.h"

#include <bonobo/bonobo-object.h>

#include "GNOME_Evolution_Importer.h"

#define PARENT_TYPE BONOBO_X_OBJECT_TYPE
static BonoboObjectClass *parent_class = NULL;

struct _EvolutionIntelligentImporterPrivate {
	EvolutionIntelligentImporterCanImportFn can_import_fn;
	EvolutionIntelligentImporterImportDataFn import_data_fn;

	char *importername;
	char *message;
	void *closure;
};


static inline EvolutionIntelligentImporter *
evolution_intelligent_importer_from_servant (PortableServer_Servant servant)
{
	return EVOLUTION_INTELLIGENT_IMPORTER (bonobo_object_from_servant (servant));
}

static CORBA_char *
impl_GNOME_Evolution_IntelligentImporter__get_importername (PortableServer_Servant servant,
							    CORBA_Environment *ev)
{
	EvolutionIntelligentImporter *ii;
	
	ii = evolution_intelligent_importer_from_servant (servant);

	return CORBA_string_dup (ii->priv->importername ? 
				 ii->priv->importername : "");
}

static CORBA_char *
impl_GNOME_Evolution_IntelligentImporter__get_message (PortableServer_Servant servant,
						       CORBA_Environment *ev)
{
	EvolutionIntelligentImporter *ii;

	ii = evolution_intelligent_importer_from_servant (servant);

	return CORBA_string_dup (ii->priv->message ?
				 ii->priv->message : "");
}

static CORBA_boolean
impl_GNOME_Evolution_IntelligentImporter_canImport (PortableServer_Servant servant,
						    CORBA_Environment *ev)
{
	EvolutionIntelligentImporter *ii;
	EvolutionIntelligentImporterPrivate *priv;

	ii = evolution_intelligent_importer_from_servant (servant);
	priv = ii->priv;
	
	if (priv->can_import_fn != NULL) 
		return (priv->can_import_fn) (ii, priv->closure);
	else
		return FALSE;
}

static void
impl_GNOME_Evolution_IntelligentImporter_importData (PortableServer_Servant servant,
						     CORBA_Environment *ev)
{
	EvolutionIntelligentImporter *ii;
	EvolutionIntelligentImporterPrivate *priv;

	ii = evolution_intelligent_importer_from_servant (servant);
	priv = ii->priv;

	if (priv->import_data_fn)
		(priv->import_data_fn) (ii, priv->closure);
}


static void
destroy (GtkObject *object)
{
	EvolutionIntelligentImporter *ii;
	
	ii = EVOLUTION_INTELLIGENT_IMPORTER (object);
	
	if (ii->priv == NULL)
		return;

	g_free (ii->priv->importername);
	g_free (ii->priv);
	ii->priv = NULL;

	GTK_OBJECT_CLASS (parent_class)->destroy (object);
}

static void
evolution_intelligent_importer_class_init (EvolutionIntelligentImporterClass *klass)
{
	GtkObjectClass *object_class;
	POA_GNOME_Evolution_IntelligentImporter__epv *epv = &klass->epv;

	object_class = GTK_OBJECT_CLASS (klass);
	object_class->destroy = destroy;
	
	parent_class = gtk_type_class (PARENT_TYPE);
	epv->_get_importername = impl_GNOME_Evolution_IntelligentImporter__get_importername;
	epv->_get_message = impl_GNOME_Evolution_IntelligentImporter__get_message;
	epv->canImport = impl_GNOME_Evolution_IntelligentImporter_canImport;
	epv->importData = impl_GNOME_Evolution_IntelligentImporter_importData;
}

static void
evolution_intelligent_importer_init (EvolutionIntelligentImporter *ii)
{
	ii->priv = g_new0 (EvolutionIntelligentImporterPrivate, 1);
}


static void
evolution_intelligent_importer_construct (EvolutionIntelligentImporter *ii,
					  EvolutionIntelligentImporterCanImportFn can_import_fn,
					  EvolutionIntelligentImporterImportDataFn import_data_fn,
					  const char *importername,
					  const char *message,
					  void *closure)
{
	g_return_if_fail (ii != NULL);
	ii->priv->importername = g_strdup (importername);
	ii->priv->message = g_strdup (message);

	ii->priv->can_import_fn = can_import_fn;
	ii->priv->import_data_fn = import_data_fn;
	ii->priv->closure = closure;
}

/**
 * evolution_intelligent_importer_new:
 * can_import_fn: The function that will be called to see if this importer can do
 * anything.
 * import_data_fn: The function that will be called when the importer should 
 * import the data.
 * importername: The name of this importer.
 * message: The message that will be displayed when the importer can import.
 * closure: The data to be passed to @can_import_fn and @import_data_fn.
 *
 * Creates a new IntelligentImporter.
 *
 * Returns: A newly allocated EvolutionIntelligentImporter.
 */
EvolutionIntelligentImporter *
evolution_intelligent_importer_new (EvolutionIntelligentImporterCanImportFn can_import_fn,
				    EvolutionIntelligentImporterImportDataFn import_data_fn,
				    const char *importername,
				    const char *message,
				    void *closure)
{
	EvolutionIntelligentImporter *ii;

	ii = gtk_type_new (evolution_intelligent_importer_get_type ());
	evolution_intelligent_importer_construct (ii, can_import_fn,
						  import_data_fn, importername,
						  message, closure);
	return ii;
}

BONOBO_X_TYPE_FUNC_FULL (EvolutionIntelligentImporter,
			 GNOME_Evolution_IntelligentImporter,
			 PARENT_TYPE,
			 evolution_intelligent_importer);
