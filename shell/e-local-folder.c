/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-local-folder.c
 *
 * Copyright (C) 2000  Helix Code, Inc.
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

/* The metafile goes like this:

   <?xml version="1.0"?>
   <efolder>
           <type>mail</type>
	   <description>This is the folder where I store mail from my gf</description>
	   <homepage>http://www.somewhere.net</homepage>
   </efolder>

   FIXME: Do we want to use a namespace for this?
   FIXME: Do we want to have an internationalized description?
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <unistd.h>

#include <gnome-xml/parser.h>
#include <gnome-xml/xmlmemory.h>

#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-util.h>
#include <gal/util/e-util.h>
#include <gal/util/e-xml-utils.h>

#include <libgnome/gnome-util.h>

#include "e-local-folder.h"


#define PARENT_TYPE E_TYPE_FOLDER
static EFolderClass *parent_class = NULL;

#define URI_PREFIX     "file://"
#define URI_PREFIX_LEN 7

struct _ELocalFolderPrivate {
	int dummy;
};


static char *
get_string_value (xmlNode *node,
		  const char *name)
{
	xmlNode *p;
	xmlChar *xml_string;
	char *retval;

	p = e_xml_get_child_by_name (node, (xmlChar *) name);
	if (p == NULL)
		return NULL;

	p = e_xml_get_child_by_name (p, (xmlChar *) "text");
	if (p == NULL)
		return NULL;

	xml_string = xmlNodeListGetString (node->doc, p, 1);
	retval = g_strdup ((char *) xml_string);
	xmlFree (xml_string);

	return retval;
}

static gboolean
construct_loading_metadata (ELocalFolder *local_folder,
			    const char *path)
{
	EFolder *folder;
	xmlDoc *doc;
	xmlNode *root;
	char *type;
	char *description;
	char *metadata_path;
	char *physical_uri;

	folder = E_FOLDER (local_folder);

	metadata_path = g_concat_dir_and_file (path, E_LOCAL_FOLDER_METADATA_FILE_NAME);

	doc = xmlParseFile (metadata_path);
	if (doc == NULL) {
		g_free (metadata_path);
		return FALSE;
	}

	root = xmlDocGetRootElement (doc);
	if (root == NULL || strcmp (root->name, "efolder") != 0) {
		g_free (metadata_path);
		xmlFreeDoc (doc);
		return FALSE;
	}

	type = get_string_value (root, "type");
	description = get_string_value (root, "description");

	e_folder_construct (folder, g_basename (path), type, description);

	g_free (type);
	g_free (description);

	xmlFreeDoc (doc);

	physical_uri = g_strconcat (URI_PREFIX, path, NULL);
	e_folder_set_physical_uri (folder, physical_uri);
	g_free (physical_uri);

	g_free (metadata_path);

	return TRUE;
}

static gboolean
save_metadata (ELocalFolder *local_folder)
{
	EFolder *folder;
	xmlDoc *doc;
	xmlNode *root;
	const char *physical_directory;
	char *physical_path;

	folder = E_FOLDER (local_folder);

	doc = xmlNewDoc ((xmlChar *) "1.0");
	root = xmlNewDocNode (doc, NULL, (xmlChar *) "efolder", NULL);
	xmlDocSetRootElement (doc, root);

	xmlNewChild (root, NULL, (xmlChar *) "type",
		     (xmlChar *) e_folder_get_type_string (folder));

	if (e_folder_get_description (folder) != NULL)
		xmlNewChild (root, NULL, (xmlChar *) "description",
			     (xmlChar *) e_folder_get_description (folder));

	physical_directory = e_folder_get_physical_uri (folder) + URI_PREFIX_LEN - 1;
	physical_path = g_concat_dir_and_file (physical_directory, E_LOCAL_FOLDER_METADATA_FILE_NAME);

	if (xmlSaveFile (physical_path, doc) < 0) {
		unlink (physical_path);
		g_free (physical_path);
		xmlFreeDoc (doc);
		return FALSE;
	}

	g_free (physical_path);

	xmlFreeDoc (doc);
	return TRUE;
}


/* GtkObject methods.  */

static void
destroy (GtkObject *object)
{
	/* No ELocalFolder-specific data to free.  */

	(* GTK_OBJECT_CLASS (parent_class)->destroy) (object);
}


static void
class_init (ELocalFolderClass *klass)
{
	GtkObjectClass *object_class;

	parent_class = gtk_type_class (e_folder_get_type ());

	object_class = GTK_OBJECT_CLASS (klass);
	object_class->destroy = destroy;
}

static void
init (ELocalFolder *local_folder)
{
}


void
e_local_folder_construct (ELocalFolder *local_folder,
			  const char *name,
			  const char *type,
			  const char *description)
{
	g_return_if_fail (local_folder != NULL);
	g_return_if_fail (E_IS_LOCAL_FOLDER (local_folder));
	g_return_if_fail (name != NULL);
	g_return_if_fail (type != NULL);

	e_folder_construct (E_FOLDER (local_folder), name, type, description);
}

EFolder *
e_local_folder_new (const char *name,
		    const char *type,
		    const char *description)
{
	ELocalFolder *local_folder;

	g_return_val_if_fail (name != NULL, NULL);
	g_return_val_if_fail (type != NULL, NULL);

	local_folder = gtk_type_new (e_local_folder_get_type ());

	e_local_folder_construct (local_folder, name, type, description);

	return E_FOLDER (local_folder);
}

EFolder *
e_local_folder_new_from_path (const char *path)
{
	EFolder *folder;

	g_return_val_if_fail (g_path_is_absolute (path), NULL);

	folder = gtk_type_new (e_local_folder_get_type ());

	if (! construct_loading_metadata (E_LOCAL_FOLDER (folder), path)) {
		gtk_object_unref (GTK_OBJECT (folder));
		return NULL;
	}

	return folder;
}

gboolean
e_local_folder_save (ELocalFolder *local_folder)
{
	g_return_val_if_fail (local_folder != NULL, FALSE);
	g_return_val_if_fail (E_IS_LOCAL_FOLDER (local_folder), FALSE);
	g_return_val_if_fail (e_folder_get_physical_uri (E_FOLDER (local_folder)) != NULL, FALSE);

	return save_metadata (local_folder);
}


E_MAKE_TYPE (e_local_folder, "ELocalFolder", ELocalFolder, class_init, init, PARENT_TYPE)
