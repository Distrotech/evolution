/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* mail-local.c: Local mailbox support. */

/* 
 * Authors: 
 *  Michael Zucchi <NotZed@ximian.com>
 *  Peter Williams <peterw@ximian.com>
 *  Ettore Perazzoli <ettore@ximian.com>
 *  Dan Winship <danw@ximian.com>
 *
 * Copyright 2000 Ximian, Inc. (www.ximian.com)
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <unistd.h>
#include <errno.h>

#include <gnome-xml/xmlmemory.h>
#include <libgnomeui/gnome-dialog.h>
#include <libgnomeui/gnome-dialog-util.h>
#include <glade/glade.h>

#include "gal/widgets/e-gui-utils.h"
#include "e-util/e-path.h"

#include "Evolution.h"
#include "evolution-storage.h"
#include "evolution-shell-component.h"
#include "evolution-storage-listener.h"

#include "camel/camel.h"

#include "mail.h"
#include "mail-local.h"
#include "mail-tools.h"
#include "folder-browser.h"
#include "mail-mt.h"
#include "mail-folder-cache.h"
#include "mail-vfolder.h"

#define d(x) x

/* ** MailLocalStore ** (protos) ************************************************** */

/* If this is passed to get_folder, then don't actually open the inner folder;
 * just load the metadata. This is a hack for delete_folder; we don't want to
 * create the actual folder just to retrieve the appropriate CamelStore.
 */

#define MAIL_LOCAL_STORE_LIGHTWEIGHT (CAMEL_STORE_FOLDER_INFO_SUBSCRIBED << 4)

#define MAIL_LOCAL_STORE_TYPE     (mail_local_store_get_type ())
#define MAIL_LOCAL_STORE(obj)     (CAMEL_CHECK_CAST((obj), MAIL_LOCAL_STORE_TYPE, MailLocalStore))
#define MAIL_LOCAL_STORE_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), MAIL_LOCAL_STORE_TYPE, MailLocalStoreClass))
#define MAIL_IS_LOCAL_STORE(o)    (CAMEL_CHECK_TYPE((o), MAIL_LOCAL_STORE_TYPE))

typedef struct {
	CamelStore parent_object;	
} MailLocalStore;

typedef struct {
	CamelStoreClass parent_class;
} MailLocalStoreClass;

CamelType mail_local_store_get_type (void);

/* ** MailLocalFolder ** (protos) ************************************************* */

#define MAIL_LOCAL_FOLDER_TYPE     (mail_local_folder_get_type ())
#define MAIL_LOCAL_FOLDER(obj)     (CAMEL_CHECK_CAST((obj), MAIL_LOCAL_FOLDER_TYPE, MailLocalFolder))
#define MAIL_LOCAL_FOLDER_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), MAIL_LOCAL_FOLDER_TYPE, MailLocalFolderClass))
#define MAIL_IS_LOCAL_FOLDER(o)    (CAMEL_CHECK_TYPE((o), MAIL_LOCAL_FOLDER_TYPE))

typedef struct {
	CamelFolder parent_object;

	CamelFolder *real_folder;

	gchar *metadata_path; /* where our metadata is */
	gchar *file_path; /* absolute pathname of mailbox */
	gchar *format; /* mbox, maildir, mh... */
	gchar *store_name; /* the name of the mailbox in the 'real' store */
	gchar *url;
	int    indexed;

	GMutex *real_folder_lock; /* no way to use the CamelFolder's lock, so... */
} MailLocalFolder;

typedef struct {
	CamelFolderClass parent_class;
} MailLocalFolderClass;

CamelType        mail_local_folder_get_type       (void);
MailLocalFolder *mail_local_folder_construct      (MailLocalFolder *mlf,
						   MailLocalStore  *parent_store,
						   const char      *full_name,
						   const char      *file_path);

gboolean         mail_local_folder_set_folder     (MailLocalFolder *mlf, 
						   guint32          flags, 
						   CamelException  *ex);

CamelStore      *mail_local_folder_get_real_store (MailLocalFolder *mlf);

gboolean         mail_local_folder_reconfigure    (MailLocalFolder *mlf, 
						   const char      *new_format, 
						   CamelException  *ex);

#ifdef ENABLE_THREADS
#define LOCAL_FOLDER_LOCK(folder)   (g_mutex_lock   (((MailLocalFolder *)folder)->real_folder_lock))
#define LOCAL_FOLDER_UNLOCK(folder) (g_mutex_unlock (((MailLocalFolder *)folder)->real_folder_lock))
#else
#define LOCAL_FOLDER_LOCK(folder)
#define LOCAL_FOLDER_UNLOCK(folder)
#endif

/* ** Misc ************************************************************************ */

void mail_local_provider_init (void);

/* ** MailLocalFolder ************************************************************* */

static void
mlf_load_metainfo (MailLocalFolder *mlf)
{
	xmlDocPtr doc;
	xmlNodePtr node;
	
	d(printf("Loading folder metainfo from : %s\n", mlf->metadata_path));
	
	doc = xmlParseFile (mlf->metadata_path);
	if (doc == NULL)
		goto dodefault;

	node = doc->root;
	if (strcmp (node->name, "folderinfo"))
		goto dodefault;

	node = node->childs;
	while (node) {
		if (!strcmp (node->name, "folder")) {
			char *index, *txt;
			
			txt = xmlGetProp (node, "type");
			mlf->format = g_strdup (txt ? txt : "mbox");
			xmlFree (txt);
			
			txt = xmlGetProp (node, "name");
			mlf->store_name = g_strdup (txt ? txt : "mbox");
			xmlFree (txt);
			
			index = xmlGetProp (node, "index");
			if (index) {
				mlf->indexed = atoi (index);
				xmlFree (index);
			} else
				mlf->indexed = TRUE;
			
		}
		node = node->next;
	}
	xmlFreeDoc (doc);
	return;

 dodefault:
	mlf->format = g_strdup ("mbox"); /* defaults */
	mlf->store_name = g_strdup ("mbox");
	mlf->indexed = TRUE;
	xmlFreeDoc (doc);
}

static gboolean
mlf_save_metainfo(MailLocalFolder *mlf)
{
	xmlDocPtr doc;
	xmlNodePtr root, node;
	int ret;

	d(printf("Saving folder metainfo to : %s\n", mlf->metadata_path));

	doc = xmlNewDoc("1.0");
	root = xmlNewDocNode(doc, NULL, "folderinfo", NULL);
	xmlDocSetRootElement(doc, root);

	node  = xmlNewChild(root, NULL, "folder", NULL);
	xmlSetProp(node, "type", mlf->format);
	xmlSetProp(node, "name", mlf->store_name);
	xmlSetProp(node, "index", mlf->indexed?"1":"0");

	ret = xmlSaveFile(mlf->metadata_path, doc);
	xmlFreeDoc(doc);
	if (ret == -1)
		return FALSE;
	return TRUE;
}

/* forward a bunch of functions to the real folder. This pretty
 * much sucks but I haven't found a better way of doing it.
 */

static void
mlf_refresh_info (CamelFolder *folder, CamelException *ex)
{
	MailLocalFolder *mlf = MAIL_LOCAL_FOLDER (folder);

	LOCAL_FOLDER_LOCK (mlf);
	camel_folder_refresh_info (mlf->real_folder, ex);
	LOCAL_FOLDER_UNLOCK (mlf);
}

static void
mlf_sync (CamelFolder *folder, gboolean expunge, CamelException *ex)
{
	MailLocalFolder *mlf = MAIL_LOCAL_FOLDER (folder);

/*
	if (mlf_save_metainfo (mlf) == FALSE) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Couldn't save metainfo file %s for folder %s"),
				      mlf->metadata_path,
				      CAMEL_FOLDER (mlf)->full_name);
		return;
	}
*/

	LOCAL_FOLDER_LOCK (mlf);
	camel_folder_sync (mlf->real_folder, expunge, ex);
	LOCAL_FOLDER_UNLOCK (mlf);
}

static void
mlf_expunge (CamelFolder *folder, CamelException *ex)
{
	MailLocalFolder *mlf = MAIL_LOCAL_FOLDER (folder);

	LOCAL_FOLDER_LOCK (mlf);
	camel_folder_expunge (mlf->real_folder, ex);
	LOCAL_FOLDER_UNLOCK (mlf);
}

static void
mlf_append_message (CamelFolder *folder, CamelMimeMessage *message, 
		    const CamelMessageInfo *info, CamelException *ex)
{
	MailLocalFolder *mlf = MAIL_LOCAL_FOLDER (folder);

	LOCAL_FOLDER_LOCK (mlf);
	camel_folder_append_message (mlf->real_folder, message, 
				     info, ex);
	LOCAL_FOLDER_UNLOCK (mlf);
}

static CamelMimeMessage *
mlf_get_message (CamelFolder *folder, const char *uid, CamelException *ex)
{
	MailLocalFolder *mlf = MAIL_LOCAL_FOLDER (folder);
	CamelMimeMessage *ret;

	LOCAL_FOLDER_LOCK (mlf);
	ret = camel_folder_get_message (mlf->real_folder, uid, ex);
	LOCAL_FOLDER_UNLOCK (mlf);
	return ret;
}

static GPtrArray *
mlf_search_by_expression (CamelFolder *folder, const char *expression, CamelException *ex)
{
	MailLocalFolder *mlf = MAIL_LOCAL_FOLDER (folder);
	GPtrArray *ret;

	LOCAL_FOLDER_LOCK (mlf);
	ret = camel_folder_search_by_expression (mlf->real_folder, expression, ex);
	LOCAL_FOLDER_UNLOCK (mlf);
	return ret;
}

static void
mlf_search_free (CamelFolder *folder, GPtrArray *result)
{
	MailLocalFolder *mlf = MAIL_LOCAL_FOLDER (folder);

	LOCAL_FOLDER_LOCK (mlf);
	camel_folder_search_free (mlf->real_folder, result);
	LOCAL_FOLDER_UNLOCK (mlf);
}

static void
mlf_set_message_user_flag (CamelFolder *folder, const char *uid, const char *name, gboolean value)
{
	MailLocalFolder *mlf = MAIL_LOCAL_FOLDER (folder);

	LOCAL_FOLDER_LOCK (mlf);
	camel_folder_set_message_user_flag (mlf->real_folder, uid, name, value);
	LOCAL_FOLDER_UNLOCK (mlf);
}

static void
mlf_set_message_user_tag (CamelFolder *folder, const char *uid, const char *name, const char *value)
{
	MailLocalFolder *mlf = MAIL_LOCAL_FOLDER (folder);

	LOCAL_FOLDER_LOCK (mlf);
	camel_folder_set_message_user_tag (mlf->real_folder, uid, name, value);
	LOCAL_FOLDER_UNLOCK (mlf);
}

/* and, conversely, forward the real folder's signals. */

static void
mlf_proxy_message_changed (CamelObject *real_folder, gpointer event_data, gpointer user_data)
{
	camel_object_trigger_event (CAMEL_OBJECT (user_data), "message_changed", event_data);
}

static void
mlf_proxy_folder_changed (CamelObject *real_folder, gpointer event_data, gpointer user_data)
{
	camel_object_trigger_event (CAMEL_OBJECT (user_data), "folder_changed", event_data);
}


static void
mlf_unset_folder (MailLocalFolder *mlf)
{
	g_return_if_fail (mlf->real_folder);

	camel_object_unhook_event (CAMEL_OBJECT (mlf->real_folder),
				   "message_changed",
				   mlf_proxy_message_changed,
				   mlf);
	camel_object_unhook_event (CAMEL_OBJECT (mlf->real_folder),
				   "folder_changed",
				   mlf_proxy_folder_changed,
				   mlf);

	camel_object_unref (CAMEL_OBJECT (CAMEL_FOLDER (mlf)->summary));
	CAMEL_FOLDER (mlf)->summary = NULL;
	camel_object_unref (CAMEL_OBJECT (mlf->real_folder));
	mlf->real_folder = NULL;

	CAMEL_FOLDER (mlf)->permanent_flags        = 0;
	CAMEL_FOLDER (mlf)->has_summary_capability = 0;
	CAMEL_FOLDER (mlf)->has_search_capability  = 0;
}

static gboolean
mlf_set_folder (MailLocalFolder *mlf, guint32 flags, CamelException *ex)
{
	CamelStore *store;

	g_return_val_if_fail (mlf->real_folder == NULL, FALSE);

	d(g_message ("Setting folder from url %s", mlf->url));
	store = camel_session_get_store (session, mlf->url, ex);
	if (store == NULL)
		return FALSE;

	if (mlf->indexed)
		flags |= CAMEL_STORE_FOLDER_BODY_INDEX;

	mlf->real_folder = camel_store_get_folder (store, mlf->store_name, flags, ex);
	camel_object_unref (CAMEL_OBJECT (store));
	if (mlf->real_folder == NULL)
		return FALSE;

	CAMEL_FOLDER (mlf)->summary = mlf->real_folder->summary;
	camel_object_ref (CAMEL_OBJECT (mlf->real_folder->summary));

	CAMEL_FOLDER (mlf)->permanent_flags        = mlf->real_folder->permanent_flags;
	CAMEL_FOLDER (mlf)->has_summary_capability = mlf->real_folder->has_summary_capability;
	CAMEL_FOLDER (mlf)->has_search_capability  = mlf->real_folder->has_search_capability;

	camel_object_hook_event (CAMEL_OBJECT (mlf->real_folder), "message_changed",
				 mlf_proxy_message_changed, mlf);
	camel_object_hook_event (CAMEL_OBJECT (mlf->real_folder), "folder_changed",
				 mlf_proxy_folder_changed, mlf);

	return TRUE;
}

static void 
mlf_class_init (CamelObjectClass *camel_object_class)
{
	CamelFolderClass *camel_folder_class = CAMEL_FOLDER_CLASS (camel_object_class);

	/* override all the functions subclassed in providers/local/ */

	camel_folder_class->refresh_info   = mlf_refresh_info;
	camel_folder_class->sync           = mlf_sync;
	camel_folder_class->expunge        = mlf_expunge;
	camel_folder_class->append_message = mlf_append_message;
	camel_folder_class->get_message    = mlf_get_message;
	camel_folder_class->search_free    = mlf_search_free;

	camel_folder_class->search_by_expression  = mlf_search_by_expression;
	camel_folder_class->set_message_user_flag = mlf_set_message_user_flag;
	camel_folder_class->set_message_user_tag  = mlf_set_message_user_tag;
}

static void
mlf_init (CamelObject *obj)
{
	MailLocalFolder *mlf = MAIL_LOCAL_FOLDER (obj);

#ifdef ENABLE_THREADS
	mlf->real_folder_lock = g_mutex_new ();
#else
	choke on this
#endif
}

static void
mlf_finalize (CamelObject *obj)
{
	MailLocalFolder *mlf = MAIL_LOCAL_FOLDER (obj);

	if (mlf->real_folder)
		mlf_unset_folder (mlf);

	g_free (mlf->file_path);

	if (mlf->metadata_path)
		g_free (mlf->metadata_path);

	if (mlf->format) {
		g_free (mlf->format);
		g_free (mlf->store_name);
	}
	
#ifdef ENABLE_THREADS
	g_mutex_free (mlf->real_folder_lock);
#endif
}

CamelType
mail_local_folder_get_type (void)
{
	static CamelType mail_local_folder_type = CAMEL_INVALID_TYPE;

	if (mail_local_folder_type == CAMEL_INVALID_TYPE) {
		mail_local_folder_type = camel_type_register (CAMEL_FOLDER_TYPE,
							      "MailLocalFolder",
							      sizeof (MailLocalFolder),
							      sizeof (MailLocalFolderClass),
							      mlf_class_init,
							      NULL,
							      mlf_init,
							      mlf_finalize);
	}

	return mail_local_folder_type;
}

static void
mlf_set_url (MailLocalFolder *mlf)
{
	if (mlf->url)
		g_free (mlf->url);

	mlf->url = g_strdup_printf ("%s:%s#%s",
				    mlf->format,
				    mlf->file_path,
				    mlf->store_name);
}

MailLocalFolder *
mail_local_folder_construct (MailLocalFolder *mlf,
			     MailLocalStore  *parent_store,
			     const char      *full_name,
			     const char      *file_path)
{
	const char *name;

	if (file_path[0] != '/') {
		camel_object_unref (CAMEL_OBJECT (mlf));
		return NULL;
	}

	name = strrchr (full_name, '/');
	if (name == NULL)
		name = full_name;
	name = name + 1;

	d(g_message ("constructing local folder at \"%s\": full = %s, name = %s",
		     file_path, full_name, name));

	camel_folder_construct (CAMEL_FOLDER (mlf),
				CAMEL_STORE (parent_store),
				full_name,
				name);

	mlf->file_path = g_strdup (file_path);
	mlf->metadata_path = g_strconcat (file_path, "/local-metadata.xml", NULL);

	d(g_message ("local metadata at %s", mlf->metadata_path));

	mlf_load_metainfo (mlf);
	mlf_set_url (mlf);

	return mlf;
}

gboolean
mail_local_folder_set_folder (MailLocalFolder *mlf, guint32 flags, CamelException *ex)
{
	gboolean ret;

	LOCAL_FOLDER_LOCK (mlf);
	ret = mlf_set_folder (mlf, flags, ex);
	LOCAL_FOLDER_UNLOCK (mlf);
	return ret;
}

CamelStore *
mail_local_folder_get_real_store (MailLocalFolder *mlf)
{
	CamelStore *store;

	LOCAL_FOLDER_LOCK (mlf);

	if (mlf->real_folder)
		store = camel_folder_get_parent_store (mlf->real_folder);
	else
		store = CAMEL_STORE (camel_session_get_service (session, mlf->url, 
								CAMEL_PROVIDER_STORE,
								NULL));

	LOCAL_FOLDER_UNLOCK (mlf);

	return store;
}

gboolean
mail_local_folder_reconfigure (MailLocalFolder *mlf, const char *new_format, CamelException *ex)
{
	CamelException local_ex;
	CamelStore *fromstore = NULL;
	CamelFolder *fromfolder = NULL;
	gchar *oldformat = NULL;
	gchar *tmpname;
	GPtrArray *uids;
	gboolean real_folder_frozen = FALSE;

	/* first things first */

	LOCAL_FOLDER_LOCK (mlf);
	camel_exception_init (&local_ex);

	/* first, 'close' the old folder */

	if (mlf->real_folder) {
		camel_folder_sync (mlf->real_folder, FALSE, &local_ex);
		if (camel_exception_is_set (&local_ex))
			goto cleanup;
		mlf_unset_folder (mlf);
	}

	fromstore = camel_session_get_store (session, mlf->url, &local_ex);
	if (fromstore == NULL)
		goto cleanup;

	oldformat = mlf->format;
	mlf->format = g_strdup (new_format);
	mlf_set_url (mlf);

	/* rename the old mbox and open it again, without indexing */
	tmpname = g_strdup_printf ("%s_reconfig", mlf->store_name);
	d(printf("renaming %s to %s, and opening it\n", mlf->store_name, tmpname));
	
	camel_store_rename_folder (fromstore, mlf->store_name, tmpname, &local_ex);
	if (camel_exception_is_set (&local_ex)) {
		goto cleanup;
	}
	
	/* we dont need to set the create flag ... or need an index if it has one */
	fromfolder = camel_store_get_folder (fromstore, tmpname, 0, &local_ex);
	if (fromfolder == NULL || camel_exception_is_set (&local_ex)) {
		/* try and recover ... */
		camel_exception_clear (&local_ex);
		camel_store_rename_folder (fromstore, tmpname, mlf->store_name, &local_ex);
		goto cleanup;
	}
	
	/* create a new mbox */
	d(printf("Creating the destination mbox\n"));

	mlf_set_folder (mlf, CAMEL_STORE_FOLDER_CREATE, &local_ex);
	if (camel_exception_is_set (&local_ex)) {
		d(printf("cannot open destination folder\n"));
		/* try and recover ... */
		camel_exception_clear (&local_ex);
		camel_store_rename_folder (fromstore, tmpname, mlf->store_name, &local_ex);
		goto cleanup;
	}

	real_folder_frozen = TRUE;
	camel_folder_freeze (mlf->real_folder);

	uids = camel_folder_get_uids (fromfolder);
	camel_folder_move_messages_to (fromfolder, uids, mlf->real_folder, &local_ex);
	camel_folder_free_uids (fromfolder, uids);
	if (camel_exception_is_set (&local_ex))
		goto cleanup;
	
	camel_folder_expunge (fromfolder, &local_ex);
	
	d(printf("delete old mbox ...\n"));
	camel_object_unref (CAMEL_OBJECT (fromfolder));
	fromfolder = NULL;
	camel_store_delete_folder (fromstore, tmpname, &local_ex);
	
	/* switch format */
	g_free (oldformat);
	oldformat = NULL;
	if (mlf_save_metainfo (mlf) == FALSE) {
		camel_exception_setv (&local_ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Cannot save folder metainfo; "
					"you'll probably find you can't\n"
					"open this folder anymore: %s"),
				      mlf->url);
	}
	
 cleanup:
	if (oldformat) {
		g_free (mlf->format);
		mlf->format = oldformat;
		mlf_set_url (mlf);
	}
	if (mlf->real_folder == NULL)
		mlf_set_folder (mlf, CAMEL_STORE_FOLDER_CREATE, &local_ex);
	if (fromfolder)
		camel_object_unref (CAMEL_OBJECT (fromfolder));
	if (fromstore)
		camel_object_unref (CAMEL_OBJECT (fromstore));

	LOCAL_FOLDER_UNLOCK (mlf);

	if (real_folder_frozen)
		camel_folder_thaw (mlf->real_folder);

	if (camel_exception_is_set (&local_ex)) {
		camel_exception_xfer (ex, &local_ex);
		return FALSE;
	}
	
	return TRUE;
}
		
/* ******************************************************************************** */

static CamelObjectClass *local_store_parent_class = NULL;

static gchar *
mls_make_file_path (MailLocalStore *local_store, const char *folder_name)
{
	char *path = CAMEL_SERVICE (local_store)->url->path;

	return g_strconcat (path, folder_name, NULL);
}

static gchar *
mls_make_physical_uri (MailLocalStore *local_store, const char *folder_name)
{
	char *path = CAMEL_SERVICE (local_store)->url->path;

	return g_strconcat ("file://", path, folder_name, NULL);
}

static CamelFolder *
mls_get_folder (CamelStore *store,
		const char *folder_name,
		guint32 flags,
		CamelException *ex)
{
	MailLocalStore *local_store = MAIL_LOCAL_STORE (store);
	MailLocalFolder *folder;
	gchar *file_path, *physical_uri;

	d(g_message ("get_folder: %s", folder_name));

	file_path = mls_make_file_path (local_store, folder_name);

	folder = MAIL_LOCAL_FOLDER (camel_object_new (MAIL_LOCAL_FOLDER_TYPE));

	folder = mail_local_folder_construct (folder,
					      local_store,
					      folder_name,
					      file_path);

	g_free (file_path);

	if ((flags & MAIL_LOCAL_STORE_LIGHTWEIGHT) == 0) {
		if (mail_local_folder_set_folder (folder, flags, ex) == FALSE) {
			camel_object_unref (CAMEL_OBJECT (folder));
			return NULL;
		}

		physical_uri = mls_make_physical_uri (local_store, folder_name);
		mail_folder_cache_note_folder (physical_uri, CAMEL_FOLDER (folder));
		g_free (physical_uri);
	}

	if (flags & CAMEL_STORE_FOLDER_CREATE) {
		if (mlf_save_metainfo (folder) == FALSE) {
			camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
					      _("Cannot save folder metainfo to \%s\""),
					      folder->metadata_path);
			camel_object_unref (CAMEL_OBJECT (folder));
			return NULL;
		}
	}

	return (CamelFolder *) folder;
}

static void
mls_delete_folder (CamelStore *store, const char *folder_name, CamelException *ex)
{
	MailLocalFolder *mlf;
	CamelStore *real_store;
	gchar *metadata_path, *store_name;
	CamelException local_ex;

	d(g_message ("delete_folder: %s", folder_name));

	/* temporarily create the folder so that we can grab
	 * its store, metadata path, and store name. */

	mlf = (MailLocalFolder *) mls_get_folder (store, folder_name, MAIL_LOCAL_STORE_LIGHTWEIGHT, ex);
	real_store = mail_local_folder_get_real_store (mlf);

	metadata_path = g_strdup (mlf->metadata_path);
	store_name    = g_strdup (mlf->store_name);

	camel_object_ref   (CAMEL_OBJECT (real_store));
	camel_object_unref (CAMEL_OBJECT (mlf));

	/* delete the actual folder */

	camel_exception_init (&local_ex);
	camel_store_delete_folder (real_store, store_name, &local_ex);
	camel_object_unref (CAMEL_OBJECT (real_store));
	g_free (store_name);

	if (camel_exception_is_set (&local_ex)) {
		camel_exception_xfer (ex, &local_ex);
		g_free (metadata_path);
		return;
	}

	/* now delete the metadata */

	if (unlink (metadata_path)) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Error unlinking metadata file at %s"),
				      metadata_path);
	}

	g_free (metadata_path);
	return;
}

static char *
mls_get_name (CamelService *service, gboolean brief)
{
	if (brief)
		return g_strdup ("local");
	return g_strdup ("Local mail folders");
}

static void
mls_class_init (CamelObjectClass *camel_object_class)
{
	CamelStoreClass *camel_store_class =
		CAMEL_STORE_CLASS (camel_object_class);
	CamelServiceClass *camel_service_class =
		CAMEL_SERVICE_CLASS (camel_object_class);
	
	/* virtual method overload -- the bare minimum */
	camel_service_class->get_name    = mls_get_name;
	camel_store_class->get_folder    = mls_get_folder;
	camel_store_class->delete_folder = mls_delete_folder;

	local_store_parent_class = camel_type_get_global_classfuncs (CAMEL_STORE_TYPE);
}

CamelType
mail_local_store_get_type (void)
{
	static CamelType mail_local_store_type = CAMEL_INVALID_TYPE;

	if (mail_local_store_type == CAMEL_INVALID_TYPE) {
		mail_local_store_type = camel_type_register (
			CAMEL_STORE_TYPE, "MailLocalStore",
			sizeof (MailLocalStore),
			sizeof (MailLocalStoreClass),
			(CamelObjectClassInitFunc) mls_class_init,
			NULL,
			NULL,
			NULL);
	}

	return mail_local_store_type;
}

/* ** Local Provider ************************************************************** */

static CamelProvider local_provider = {
	"file", "Local mail", NULL, "mail",
	CAMEL_PROVIDER_IS_STORAGE, CAMEL_URL_NEED_PATH,
	/* ... */
};

/* There's only one "file:" store. */
static guint
non_hash (gconstpointer key)
{
	return 0;
}

static gint
non_equal (gconstpointer a, gconstpointer b)
{
	return TRUE;
}

void
mail_local_provider_init (void)
{
	/* Register with Camel to handle file: URLs */
	local_provider.object_types[CAMEL_PROVIDER_STORE] = MAIL_LOCAL_STORE_TYPE;

	local_provider.service_cache = g_hash_table_new (non_hash, non_equal);
	camel_session_register_provider (session, &local_provider);
}

/* ** Local Storage Listener ****************************************************** */

static void
local_storage_destroyed_cb (EvolutionStorageListener *storage_listener,
			    void *data)
{
	CORBA_Environment ev;

	CORBA_exception_init (&ev);
	bonobo_object_release_unref (data, &ev);
	CORBA_exception_free (&ev);
}


static void
local_storage_new_folder_cb (EvolutionStorageListener *storage_listener,
			     const char *path,
			     const GNOME_Evolution_Folder *folder,
			     void *data)
{
	if (strcmp (folder->type, "mail") != 0)
		return;

	mail_folder_cache_set_update_lstorage (folder->physicalUri,
					       data,
					       path);
	mail_folder_cache_note_name (folder->physicalUri, folder->displayName);
}

#if 0
static void
local_storage_removed_folder_cb (EvolutionStorageListener *storage_listener,
				 const char *path,
				 void *data)
{
	if (strcmp (folder->type, "mail") != 0)
		return;

	/* anything to do? */
}
#endif

static void
storage_listener_startup (EvolutionShellClient *shellclient)
{
	EvolutionStorageListener *local_storage_listener;
	GNOME_Evolution_StorageListener corba_local_storage_listener;
	GNOME_Evolution_Storage corba_storage;
	CORBA_Environment ev;

	corba_storage = evolution_shell_client_get_local_storage (shellclient);
	if (corba_storage == CORBA_OBJECT_NIL) {
		g_warning ("No local storage available from shell client!");
		return;
	}
	
	local_storage_listener = evolution_storage_listener_new ();
	corba_local_storage_listener = evolution_storage_listener_corba_objref (
		local_storage_listener);

	gtk_signal_connect (GTK_OBJECT (local_storage_listener),
			    "destroyed",
			    GTK_SIGNAL_FUNC (local_storage_destroyed_cb),
			    corba_storage);
	gtk_signal_connect (GTK_OBJECT (local_storage_listener),
			    "new_folder",
			    GTK_SIGNAL_FUNC (local_storage_new_folder_cb),
			    corba_storage);
	/*gtk_signal_connect (GTK_OBJECT (local_storage_listener),
	 *		    "removed_folder",
	 *		    GTK_SIGNAL_FUNC (local_storage_removed_folder_cb),
	 *		    corba_storage);
	 */

	CORBA_exception_init (&ev);
	GNOME_Evolution_Storage_addListener (corba_storage,
					     corba_local_storage_listener, &ev);
	if (ev._major != CORBA_NO_EXCEPTION) {
		g_warning ("Cannot add a listener to the Local Storage.");
		CORBA_exception_free (&ev);
		return;
	}
	CORBA_exception_free (&ev);
}

/* ** The rest ******************************************************************** */

static MailLocalStore *global_local_store;

void
mail_local_storage_startup (EvolutionShellClient *shellclient,
			    const char *evolution_path)
{
	mail_local_provider_init ();

	global_local_store = MAIL_LOCAL_STORE (camel_session_get_service (session, "file:/", CAMEL_PROVIDER_STORE, NULL));

	if (!global_local_store) {
		g_warning ("No local store!");
		return;
	}

	storage_listener_startup (shellclient);
}


/*----------------------------------------------------------------------
 * Local folder reconfiguration stuff
 *----------------------------------------------------------------------*/

/*
   open new
   copy old->new
   close old
   rename old oldsave
   rename new old
   open oldsave
   delete oldsave

   close old
   rename oldtmp
   open new
   open oldtmp
   copy oldtmp new
   close oldtmp
   close oldnew

*/

/* we should have our own progress bar for this */

struct _reconfigure_msg {
	struct _mail_msg msg;

	FolderBrowser *fb;
	char *newtype;
	GtkWidget *frame;
	GtkWidget *apply;
	GtkWidget *cancel;
	GtkOptionMenu *optionlist;
	CamelFolder *folder_out;
};

static char *
reconfigure_folder_describe (struct _mail_msg *mm, int done)
{
	struct _reconfigure_msg *m = (struct _reconfigure_msg *)mm;
	
	return g_strdup_printf (_("Changing folder \"%s\" to \"%s\" format"),
				m->fb->uri,
				m->newtype);
}

static void
reconfigure_folder_reconfigure (struct _mail_msg *mm)
{
	struct _reconfigure_msg *m = (struct _reconfigure_msg *)mm;
	CamelFolder *local_folder = NULL;

	d(printf("reconfiguring folder: %s to type %s\n", m->fb->uri, m->newtype));
	
	if (strncmp (m->fb->uri, "file:", 5)) {
		camel_exception_setv (&mm->ex, CAMEL_EXCEPTION_SERVICE_UNAVAILABLE,
				      _("%s may not be reconfigured because it is not a local folder"), 
				      m->fb->uri);
		return;
	}

	local_folder = mail_tool_uri_to_folder (m->fb->uri, &mm->ex);
	if (camel_exception_is_set (&mm->ex)) {
		g_warning ("Can't resolve URI \"%s\" for reconfiguration!", m->fb->uri);
		return;
	}

	mail_local_folder_reconfigure (MAIL_LOCAL_FOLDER (local_folder), m->newtype, &mm->ex);
	m->folder_out = local_folder;
}

static void
reconfigure_folder_reconfigured (struct _mail_msg *mm)
{
	struct _reconfigure_msg *m = (struct _reconfigure_msg *)mm;
	/*char *uri;*/
	
	if (camel_exception_is_set (&mm->ex)) {
		gnome_error_dialog (_("If you can no longer open this mailbox, then\n"
				      "you may need to repair it manually."));
	}

	message_list_set_folder (m->fb->message_list, m->folder_out, FALSE);
}

static void
reconfigure_folder_free (struct _mail_msg *mm)
{
	struct _reconfigure_msg *m = (struct _reconfigure_msg *)mm;

	camel_object_unref (CAMEL_OBJECT (m->folder_out));
	gtk_object_unref (GTK_OBJECT (m->fb));
	g_free (m->newtype);
}

static struct _mail_msg_op reconfigure_folder_op = {
	reconfigure_folder_describe,
	reconfigure_folder_reconfigure,
	reconfigure_folder_reconfigured,
	reconfigure_folder_free,
};

/* hash table of folders that the user has a reconfig-folder dialog for */
static GHashTable *reconfigure_folder_hash = NULL;

static void
reconfigure_clicked (GnomeDialog *dialog, int button, struct _reconfigure_msg *m)
{
	if (button == 0) {
		GtkWidget *menu;
		int type;
		char *types[] = { "mbox", "maildir", "mh" };
		
		/* hack to clear the message list during update */
		/* we need to do this because the message list caches
		 * CamelMessageInfos from the old folder. */
		message_list_set_folder (m->fb->message_list, NULL, FALSE);
		
		menu = gtk_option_menu_get_menu (m->optionlist);
		type = g_list_index (GTK_MENU_SHELL (menu)->children,
				     gtk_menu_get_active (GTK_MENU (menu)));
		if (type < 0 || type > 2)
			type = 0;
		
		gtk_widget_set_sensitive (m->frame, FALSE);
		gtk_widget_set_sensitive (m->apply, FALSE);
		gtk_widget_set_sensitive (m->cancel, FALSE);
		
		m->newtype = g_strdup (types[type]);
		e_thread_put (mail_thread_queued, (EMsg *)m);
	} else
		mail_msg_free ((struct _mail_msg *)m);
	
	if (button != -1) {
		/* remove this folder from our hash since we are done with it */
		g_hash_table_remove (reconfigure_folder_hash, m->fb->folder);
		if (g_hash_table_size (reconfigure_folder_hash) == 0) {
			/* additional cleanup */
			g_hash_table_destroy (reconfigure_folder_hash);
			reconfigure_folder_hash = NULL;
		}

		gnome_dialog_close (dialog);
	}
}

void
mail_local_reconfigure_folder (FolderBrowser *fb)
{
	GladeXML *gui;
	GnomeDialog *gd;
	struct _reconfigure_msg *m;
	char *name, *title;
	
	if (fb->folder == NULL) {
		g_warning ("Trying to reconfigure nonexistant folder");
		return;
	}
	
	if (!reconfigure_folder_hash)
		reconfigure_folder_hash = g_hash_table_new (NULL, NULL);
	
	if ((gd = g_hash_table_lookup (reconfigure_folder_hash, fb->folder))) {
		gdk_window_raise (GTK_WIDGET (gd)->window);
		return;
	}
	
	/* check if we can work on this folder */
	if (!MAIL_IS_LOCAL_FOLDER (fb->folder)) {
		e_notice (NULL, GNOME_MESSAGE_BOX_WARNING,
			  _("You cannot change the format of a non-local folder."));
		return;
	}
	
	m = mail_msg_new (&reconfigure_folder_op, NULL, sizeof (*m));
	
	gui = glade_xml_new (EVOLUTION_GLADEDIR "/local-config.glade", "dialog_format");
	gd = (GnomeDialog *)glade_xml_get_widget (gui, "dialog_format");
	
	name = mail_tool_get_folder_name (fb->folder);
	title = g_strdup_printf (_("Reconfigure %s"), name);
	gtk_window_set_title (GTK_WINDOW (gd), title);
	g_free (title);
	g_free (name);
	
	m->frame = glade_xml_get_widget (gui, "frame_format");
	m->apply = glade_xml_get_widget (gui, "apply_format");
	m->cancel = glade_xml_get_widget (gui, "cancel_format");
	m->optionlist = (GtkOptionMenu *)glade_xml_get_widget (gui, "option_format");
	m->newtype = NULL;
	m->fb = fb;
	m->folder_out = NULL;
	gtk_object_ref (GTK_OBJECT (fb));
	
	gtk_label_set_text ((GtkLabel *)glade_xml_get_widget (gui, "label_format"),
			    MAIL_LOCAL_FOLDER (fb->folder)->format);
	
	gtk_signal_connect (GTK_OBJECT (gd), "clicked", reconfigure_clicked, m);
	gtk_object_unref (GTK_OBJECT (gui));
	
	g_hash_table_insert (reconfigure_folder_hash, (gpointer) fb->folder, (gpointer) gd);
	
	gnome_dialog_run (GNOME_DIALOG (gd));
}
