/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camelFolder.c : Abstract class for an email folder */

/* 
 *
 * Copyright (C) 1999 Bertrand Guiheneuf <Bertrand.Guiheneuf@inria.fr> .
 *
 * This program is free software; you can redistribute it and/or 
 * modify it under the terms of the GNU General Public License as 
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */
#include <config.h>
#include "camel-folder.h"
#include "camel-log.h"
#include "string-utils.h"

static GtkObjectClass *parent_class=NULL;

/* Returns the class for a CamelFolder */
#define CF_CLASS(so) CAMEL_FOLDER_CLASS (GTK_OBJECT(so)->klass)

static void _init_with_store (CamelFolder *folder, CamelStore *parent_store);
static void _open (CamelFolder *folder, CamelFolderOpenMode mode);
static void _close (CamelFolder *folder, gboolean expunge);
static void _set_name (CamelFolder *folder, const gchar *name);
/*  static void _set_full_name (CamelFolder *folder, const gchar *name); */
static const gchar *_get_name (CamelFolder *folder);
static const gchar *_get_full_name (CamelFolder *folder);
static gboolean _can_hold_folders (CamelFolder *folder);
static gboolean _can_hold_messages(CamelFolder *folder);
static gboolean _exists (CamelFolder  *folder);
static gboolean _is_open (CamelFolder *folder);
static CamelFolder *_get_folder (CamelFolder *folder, const gchar *folder_name);
static gboolean _create (CamelFolder *folder);
static gboolean _delete (CamelFolder *folder, gboolean recurse);
static gboolean _delete_messages (CamelFolder *folder);
static CamelFolder *_get_parent_folder (CamelFolder *folder);
static CamelStore *_get_parent_store (CamelFolder *folder);
static CamelFolderOpenMode _get_mode (CamelFolder *folder);
static GList *_list_subfolders (CamelFolder *folder);
static GList *_expunge (CamelFolder *folder);
static CamelMimeMessage *_get_message (CamelFolder *folder, gint number);
static gint _get_message_count (CamelFolder *folder);
static gint _append_message (CamelFolder *folder, CamelMimeMessage *message);

static void _finalize (GtkObject *object);

static void
camel_folder_class_init (CamelFolderClass *camel_folder_class)
{
	GtkObjectClass *gtk_object_class = GTK_OBJECT_CLASS (camel_folder_class);
	
	parent_class = gtk_type_class (gtk_object_get_type ());
	
	/* virtual method definition */
	camel_folder_class->init_with_store = _init_with_store;
	camel_folder_class->open = _open;
	camel_folder_class->close = _close;
	camel_folder_class->set_name = _set_name;
	camel_folder_class->get_name = _get_name;
	camel_folder_class->can_hold_folders = _can_hold_folders;
	camel_folder_class->can_hold_messages = _can_hold_messages;
	camel_folder_class->exists = _exists;
	camel_folder_class->is_open = _is_open;
	camel_folder_class->get_folder = _get_folder;
	camel_folder_class->create = _create;
	camel_folder_class->delete = _delete;
	camel_folder_class->delete_messages = _delete_messages;
	camel_folder_class->get_parent_folder = _get_parent_folder;
	camel_folder_class->get_parent_store = _get_parent_store;
	camel_folder_class->get_mode = _get_mode;
	camel_folder_class->list_subfolders = _list_subfolders;
	camel_folder_class->expunge = _expunge;
	camel_folder_class->get_message = _get_message;
	camel_folder_class->get_message_count = _get_message_count;
	camel_folder_class->append_message = _append_message;

	/* virtual method overload */
	gtk_object_class->finalize = _finalize;
}







GtkType
camel_folder_get_type (void)
{
	static GtkType camel_folder_type = 0;
	
	if (!camel_folder_type)	{
		GtkTypeInfo camel_folder_info =	
		{
			"CamelFolder",
			sizeof (CamelFolder),
			sizeof (CamelFolderClass),
			(GtkClassInitFunc) camel_folder_class_init,
			(GtkObjectInitFunc) NULL,
				/* reserved_1 */ NULL,
				/* reserved_2 */ NULL,
			(GtkClassInitFunc) NULL,
		};
		
		camel_folder_type = gtk_type_unique (gtk_object_get_type (), &camel_folder_info);
	}
	
	return camel_folder_type;
}


static void           
_finalize (GtkObject *object)
{
	CamelFolder *camel_folder = CAMEL_FOLDER (object);
	CAMEL_LOG_FULL_DEBUG ("Entering CamelFolder::finalize\n");

	if (camel_folder->name) g_free (camel_folder->name);
	if (camel_folder->full_name) g_free (camel_folder->full_name);
	
	GTK_OBJECT_CLASS (parent_class)->finalize (object);
	CAMEL_LOG_FULL_DEBUG ("Leaving CamelFolder::finalize\n");
}


/**
 * _init_with_store: init the folder by setting its parent store.
 * @folder: folder object to initialize
 * @parent_store: parent store object of the folder
 * 
 * 
 **/
static void 
_init_with_store (CamelFolder *folder, CamelStore *parent_store)
{
	g_assert(folder);
	g_assert(parent_store);
	
	if (folder->parent_store) gtk_object_unref (GTK_OBJECT (folder->parent_store));
	folder->parent_store = parent_store;
	if (parent_store) gtk_object_ref (GTK_OBJECT (parent_store));
}




/**
 * _open: Open a folder
 * @folder: The folder object
 * @mode: open mode (R/W/RW ?)
 * 
 * 
 **/
static void
_open (CamelFolder *folder, CamelFolderOpenMode mode)
{
	folder->open_state = FOLDER_OPEN;
	folder->open_mode = mode;
}


/**
 * _close:Close a folder.
 * @folder: 
 * @expunge: if TRUE, the flagged message are deleted.
 * 
 * Put a folder in its closed state, and possibly 
 * expunge the flagged messages.
 **/
static void
_close (CamelFolder *folder, gboolean expunge)
{
	if (expunge) CF_CLASS(folder)->expunge(folder);
	folder->open_state = FOLDER_CLOSE;
}





static void
_set_name (CamelFolder *folder, const gchar *name)
{
	gchar separator;
	gchar *full_name;
	const gchar *parent_full_name;
	
	g_assert (folder);
	g_assert (name);
	g_assert (folder->parent_store);
	
	if (folder->name) g_free(folder->name);
	if (folder->full_name) g_free (folder->full_name);
	
	separator = camel_store_get_separator (folder->parent_store);
	
	if (folder->parent_folder) {
		parent_full_name = camel_folder_get_full_name (folder->parent_folder);
		full_name = g_strdup_printf ("%s%d%s", parent_full_name, separator, name);
		
	} else {
		full_name = g_strdup (name);
	}
	
	folder->name = g_strdup (name);
	folder->full_name = full_name;
	
}


/**
 * camel_folder_set_name:set the (short) name of the folder
 * @folder: folder
 * @name: new name of the folder
 * 
 * set the name of the folder. 
 * 
 * 
 **/
void 
camel_folder_set_name (CamelFolder *folder, const gchar *name)
{
	CF_CLASS(folder)->set_name (folder, name);
}


#if 0
static void
_set_full_name (CamelFolder *folder, const gchar *name)
{
	if (folder->full_name) g_free(folder->full_name);
	folder->full_name = g_strdup (name);
}


/**
 * camel_folder_set_full_name:set the (full) name of the folder
 * @folder: folder
 * @name: new name of the folder
 * 
 * set the name of the folder. 
 * 
 **/
void 
camel_folder_set_full_name (CamelFolder *folder, const gchar *name)
{
	CF_CLASS(folder)->set_full_name (folder, name);
}
#endif

static const gchar *
_get_name (CamelFolder *folder)
{
	return folder->name;
}


/**
 * camel_folder_get_name: get the (short) name of the folder
 * @folder: 
 * 
 * get the name of the folder. The fully qualified name
 * can be obtained with the get_full_ame method (not implemented)
 *
 * Return value: name of the folder
 **/
const gchar *
camel_folder_get_name (CamelFolder *folder)
{
	return CF_CLASS(folder)->get_name (folder);
}



static const gchar *
_get_full_name (CamelFolder *folder)
{
	return folder->full_name;
}

/**
 * camel_folder_get_full_name:get the (full) name of the folder
 * @folder: folder to get the name 
 * 
 * get the name of the folder. 
 * 
 * Return value: full name of the folder
 **/
const gchar *
camel_folder_get_full_name (CamelFolder *folder)
{
	return CF_CLASS(folder)->get_full_name (folder);
}


/**
 * _can_hold_folders: tests if the folder can contain other folders
 * @folder: The folder object 
 * 
 * Tests if a folder can contain other folder 
 * (as for example MH folders)
 * 
 * Return value: 
 **/
static gboolean
_can_hold_folders (CamelFolder *folder)
{
	return folder->can_hold_folders;
}




/**
 * _can_hold_messages: tests if the folder can contain messages
 * @folder: The folder object
 * 
 * Tests if a folder object can contain messages. 
 * In the case it can not, it most surely can only 
 * contain folders (rare).
 * 
 * Return value: true if it can contain messages false otherwise
 **/
static gboolean
_can_hold_messages (CamelFolder *folder)
{
	return folder->can_hold_messages;
}



static gboolean
_exists (CamelFolder *folder)
{
	return FALSE;
}


/**
 * _exists: tests if the folder object exists in its parent store.
 * @folder: folder object
 * 
 * Test if a folder exists on a store. A folder can be 
 * created without physically on a store. In that case, 
 * use CamelFolder::create to create it 
 * 
 * Return value: true if the folder exists on the store false otherwise 
 **/
gboolean
camel_folder_exists (CamelFolder *folder)
{
	return (CF_CLASS(folder)->exists (folder));
}



/**
 * _is_open: test if the folder is open 
 * @folder: The folder object
 * 
 * Tests if a folder is open. If not open it can be opened 
 * CamelFolder::open
 * 
 * Return value: true if the folder exists, false otherwise
 **/
static gboolean
_is_open (CamelFolder *folder)
{
	return (folder->open_state == FOLDER_OPEN);
} 





static CamelFolder *
_get_folder (CamelFolder *folder, const gchar *folder_name)
{
	CamelFolder *new_folder;
	gchar *full_name;
	const gchar *current_folder_full_name;
	gchar separator;
	
	g_assert (folder);
	g_assert (folder_name);
	
	if (!folder->parent_store) return NULL;
	
	current_folder_full_name = camel_folder_get_full_name (folder);
	if (!current_folder_full_name) return NULL;
	
	separator = camel_store_get_separator (folder->parent_store);
	full_name = g_strdup_printf ("%s%d%s", current_folder_full_name, separator, folder_name);
	
	new_folder = camel_store_get_folder (folder->parent_store, full_name);
	return new_folder;
}



/**
 * camel_folder_get_folder: return the (sub)folder object that is specified
 * @folder: the folder
 * @folder_name: subfolder path
 * 
 * This method returns a folder objects. This folder
 * is necessarily a subfolder of the current folder. 
 * It is an error to ask a folder begining with the 
 * folder separator character.  
 * 
 * Return value: Required folder. NULL if the subfolder object  could not be obtained
 **/
CamelFolder *
camel_folder_get_folder (CamelFolder *folder, gchar *folder_name)
{
	return (CF_CLASS(folder)->get_folder(folder,folder_name));
}




/**
 * _create: creates a folder on its store
 * @folder: a CamelFolder object.
 * 
 * this routine handles the recursion mechanism.
 * Children classes have to implement the actual
 * creation mechanism. They must call this method
 * before physically creating the folder in order
 * to be sure the parent folder exists.
 * Calling this routine on an existing folder is
 * not an error, and returns %TRUE.
 * 
 * Return value: %TRUE if the folder exists, %FALSE otherwise 
 **/
static gboolean
_create(CamelFolder *folder)
{
	gchar *prefix;
	gchar dich_result;
	CamelFolder *parent;
	gchar sep;
	
	g_assert (folder);
	g_assert (folder->parent_store);
	g_assert (folder->name);
	
	if (CF_CLASS(folder)->exists (folder))
		return TRUE;
	
	sep = camel_store_get_separator (folder->parent_store);	
	if (folder->parent_folder)
		camel_folder_create (folder->parent_folder);
	else {   
		if (folder->full_name) {
			dich_result = string_dichotomy (
							folder->full_name, sep, &prefix, NULL,
							STRING_DICHOTOMY_STRIP_TRAILING | STRING_DICHOTOMY_RIGHT_DIR);
			if (dich_result!='o') {
				g_warning("I have to handle the case where the path is not OK\n"); 
				return FALSE;
			} else {
				parent = camel_store_get_folder (folder->parent_store, prefix);
				camel_folder_create (parent);
			}
		}
	}	
	return TRUE;
}


/**
 * camel_folder_create: create the folder object on the physical store
 * @folder: folder object to create
 * 
 * This routine physically creates the folder object on 
 * the store. Having created the  object does not
 * mean the folder physically exists. If it does not
 * exists, this routine will create it.
 * if the folder full name contains more than one level
 * of hierarchy, all folders between the current folder
 * and the last folder name will be created if not existing.
 * 
 * Return value: 
 **/
gboolean
camel_folder_create (CamelFolder *folder)
{
	return (CF_CLASS(folder)->create(folder));
}





/**
 * _delete: delete folder 
 * @folder: folder to delete
 * @recurse: true is subfolders must also be deleted
 * 
 * Delete a folder and its subfolders (if recurse is TRUE).
 * The scheme is the following:
 * 1) delete all messages in the folder
 * 2) if recurse is FALSE, and if there are subfolders
 *    return FALSE, else delete current folder and retuen TRUE
 *    if recurse is TRUE, delete subfolders, delete
 *    current folder and return TRUE
 * 
 * subclasses implementing a protocol with a different 
 * deletion behaviour must emulate this one or implement
 * empty folders deletion and call  this routine which 
 * will do all the works for them.
 * Opertions must be done in the folllowing order:
 *  - call this routine
 *  - delete empty folder
 * 
 * Return value: true if the folder has been deleted
 **/
static gboolean
_delete (CamelFolder *folder, gboolean recurse)
{
	GList *subfolders=NULL;
	GList *sf;
	gboolean ok;
	
	g_assert(folder);
	
	/* method valid only on closed folders */
	if (folder->open_state != FOLDER_CLOSE) return FALSE;
	
	/* delete all messages in the folder */
	CF_CLASS(folder)->delete_messages(folder);
	
	subfolders = CF_CLASS(folder)->list_subfolders(folder); 
	if (recurse) { /* delete subfolders */
		if (subfolders) {
			sf = subfolders;
			do {
				/*  CF_CLASS(sf->data)->delete(sf->data, TRUE); */
			} while (sf = sf->next);
		}
	} else if (subfolders) return FALSE;
	
	
	return TRUE;
}



/**
 * camel_folder_delete: delete a folder
 * @folder: folder to delete
 * @recurse: TRUE if subfolders must be deleted
 * 
 * Delete a folder. All messages in the folder 
 * are deleted before the folder is deleted. 
 * When recurse is true, all subfolders are
 * deleted too. When recurse is FALSE and folder 
 * contains subfolders, all messages are deleted,
 * but folder deletion fails. 
 * 
 * Return value: TRUE if deletion was successful
 **/
gboolean camel_folder_delete (CamelFolder *folder, gboolean recurse)
{
	return CF_CLASS(folder)->delete(folder, recurse);
}





/**
 * _delete_messages: delete all messages in the folder
 * @folder: 
 * 
 * 
 * 
 * Return value: 
 **/
static gboolean 
_delete_messages (CamelFolder *folder)
{
	return TRUE;
}


/**
 * camel_folder_delete_messages: delete all messages in the folder
 * @folder: folder 
 * 
 * delete all messages stored in a folder
 * 
 * Return value: TRUE if the messages could be deleted
 **/
gboolean
camel_folder_delete_messages (CamelFolder *folder)
{
	return CF_CLASS(folder)->delete_messages(folder);
}






/**
 * _get_parent_folder: return parent folder
 * @folder: folder to get the parent
 * 
 * 
 * 
 * Return value: 
 **/
static CamelFolder *
_get_parent_folder (CamelFolder *folder)
{
	return folder->parent_folder;
}


/**
 * camel_folder_get_parent_folder:return parent folder
 * @folder: folder to get the parent
 * 
 * 
 * 
 * Return value: 
 **/
CamelFolder *
camel_folder_get_parent_folder (CamelFolder *folder)
{
	return CF_CLASS(folder)->get_parent_folder(folder);
}


/**
 * _get_parent_store: return parent store
 * @folder: folder to get the parent
 * 
 * 
 * 
 * Return value: 
 **/
static CamelStore *
_get_parent_store (CamelFolder *folder)
{
	return folder->parent_store;
}


/**
 * camel_folder_get_parent_store:return parent store
 * @folder: folder to get the parent
 * 
 * 
 * 
 * Return value: 
 **/
CamelStore *
camel_folder_get_parent_store (CamelFolder *folder)
{
	return CF_CLASS(folder)->get_parent_store(folder);
}



/**
 * _get_mode: return the open mode of a folder
 * @folder: 
 * 
 * 
 * 
 * Return value:  open mode of the folder
 **/
static CamelFolderOpenMode
_get_mode (CamelFolder *folder)
{
	return folder->open_mode;
}


/**
 * camel_folder_get_mode: return the open mode of a folder
 * @folder: 
 * 
 * 
 * 
 * Return value:  open mode of the folder
 **/
CamelFolderOpenMode
camel_folder_get_mode (CamelFolder *folder)
{
	return CF_CLASS(folder)->get_mode(folder);
}




static GList *
_list_subfolders (CamelFolder *folder)
{
	return NULL;
}


/**
 * camel_folder_list_subfolders: list subfolders in a folder
 * @folder: the folder
 * 
 * List subfolders in a folder. 
 * 
 * Return value: list of subfolders
 **/
GList *
camel_folder_list_subfolders (CamelFolder *folder)
{
	return CF_CLASS(folder)->list_subfolders(folder);
}




static GList *
_expunge (CamelFolder *folder)
{
	return NULL;
}


/**
 * camel_folder_expunge: physically delete messages marked as DELETED
 * @folder: the folder
 * 
 * Delete messages which have been marked as deleted. 
 * 
 * 
 * Return value: list of expunged message objects.
 **/
GList *
camel_folder_expunge (CamelFolder *folder)
{
	return CF_CLASS (folder)->expunge (folder);
}




static CamelMimeMessage *
_get_message (CamelFolder *folder, gint number)
{
	return NULL;
}




/**
 * _get_message: return the message corresponding to that number in the folder
 * @folder: a CamelFolder object
 * @number: the number of the message within the folder.
 * 
 * Return the message corresponding to that number within the folder.
 * 
 * Return value: A pointer on the corresponding message or NULL if no corresponding message exists
 **/
CamelMimeMessage *
camel_folder_get_message (CamelFolder *folder, gint number)
{
	return CF_CLASS (folder)->get_message (folder, number);
}


static gint
_get_message_count (CamelFolder *folder)
{
	return -1;
}



/**
 * camel_folder_get_message_count: get the number of messages in the folder
 * @folder: A CamelFolder object
 * 
 * Returns the number of messages in the folder.
 * 
 * Return value: the number of messages or -1 if unknown.
 **/
gint
camel_folder_get_message_count (CamelFolder *folder)
{
	return CF_CLASS (folder)->get_message_count (folder);
}


static gint
_append_message (CamelFolder *folder, CamelMimeMessage *message)
{
	return -1;
}


gint camel_folder_append_message (CamelFolder *folder, CamelMimeMessage *message)
{
	return CF_CLASS (folder)->append_message (folder, message);
}
