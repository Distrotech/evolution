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
#include "mail-ops.h"

#define d(x)

/* sigh, required for passing around to some functions */
static GNOME_Evolution_Storage local_corba_storage = CORBA_OBJECT_NIL;

/* ** MailLocalStore ** (protos) ************************************************** */

#define MAIL_LOCAL_STORE_TYPE     (mail_local_store_get_type ())
#define MAIL_LOCAL_STORE(obj)     (CAMEL_CHECK_CAST((obj), MAIL_LOCAL_STORE_TYPE, MailLocalStore))
#define MAIL_LOCAL_STORE_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), MAIL_LOCAL_STORE_TYPE, MailLocalStoreClass))
#define MAIL_IS_LOCAL_STORE(o)    (CAMEL_CHECK_TYPE((o), MAIL_LOCAL_STORE_TYPE))

typedef struct {
	CamelStore parent_object;

	/* stores CamelFolderInfo's of the folders we're supposed to know about, by uri */
	GHashTable *folder_infos;
	GMutex *folder_info_lock;

} MailLocalStore;

typedef struct {
	CamelStoreClass parent_class;
} MailLocalStoreClass;

static CamelType mail_local_store_get_type (void);

static MailLocalStore *global_local_store;

/* ** MailLocalFolder ** (protos) ************************************************* */

#define MAIL_LOCAL_FOLDER_TYPE     (mail_local_folder_get_type ())
#define MAIL_LOCAL_FOLDER(obj)     (CAMEL_CHECK_CAST((obj), MAIL_LOCAL_FOLDER_TYPE, MailLocalFolder))
#define MAIL_LOCAL_FOLDER_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), MAIL_LOCAL_FOLDER_TYPE, MailLocalFolderClass))
#define MAIL_IS_LOCAL_FOLDER(o)    (CAMEL_CHECK_TYPE((o), MAIL_LOCAL_FOLDER_TYPE))

#define LOCAL_STORE_LOCK(folder)   (g_mutex_lock   (((MailLocalStore *)folder)->folder_info_lock))
#define LOCAL_STORE_UNLOCK(folder) (g_mutex_unlock (((MailLocalStore *)folder)->folder_info_lock))

struct _local_meta {
	char *path;		/* path of metainfo */

	char *format;		/* format of mailbox */
	char *name;		/* name of actual mbox */
	int indexed;		/* is body indexed? */
};

typedef struct {
	CamelFolder parent_object;

	CamelFolder *real_folder;
	CamelStore *real_store;

	char *real_path;

	struct _local_meta *meta;

	GMutex *real_folder_lock; /* no way to use the CamelFolder's lock, so... */
} MailLocalFolder;

typedef struct {
	CamelFolderClass parent_class;
} MailLocalFolderClass;

static CamelType mail_local_folder_get_type (void);

#ifdef ENABLE_THREADS
#define LOCAL_FOLDER_LOCK(folder)   (g_mutex_lock   (((MailLocalFolder *)folder)->real_folder_lock))
#define LOCAL_FOLDER_UNLOCK(folder) (g_mutex_unlock (((MailLocalFolder *)folder)->real_folder_lock))
#else
#define LOCAL_FOLDER_LOCK(folder)
#define LOCAL_FOLDER_UNLOCK(folder)
#endif

/* ** MailLocalFolder ************************************************************* */

static struct _local_meta *
load_metainfo(const char *path)
{
	xmlDocPtr doc;
	xmlNodePtr node;
	struct _local_meta *meta;

	d(printf("Loading folder metainfo from : %s\n", path));

	meta = g_malloc0(sizeof(*meta));
	meta->path = g_strdup(path);

	doc = xmlParseFile(path);
	if (doc == NULL)
		goto dodefault;

	node = doc->root;
	if (strcmp(node->name, "folderinfo"))
		goto dodefault;

	node = node->childs;
	while (node) {
		if (!strcmp(node->name, "folder")) {
			char *index, *txt;
			
			txt = xmlGetProp(node, "type");
			meta->format = g_strdup(txt?txt:"mbox");
			xmlFree(txt);
			
			txt = xmlGetProp(node, "name");
			meta->name = g_strdup(txt?txt:"mbox");
			xmlFree(txt);
			
			index = xmlGetProp(node, "index");
			if (index) {
				meta->indexed = atoi(index);
				xmlFree(index);
			} else
				meta->indexed = TRUE;
			
		}
		node = node->next;
	}
	xmlFreeDoc(doc);
	return meta;

 dodefault:
	meta->format = g_strdup("mbox"); /* defaults */
	meta->name = g_strdup("mbox");
	meta->indexed = TRUE;
	xmlFreeDoc(doc);
	return meta;
}

static void
free_metainfo(struct _local_meta *meta)
{
	g_free(meta->path);
	g_free(meta->format);
	g_free(meta->name);
	g_free(meta);
}

static gboolean
save_metainfo(struct _local_meta *meta)
{
	xmlDocPtr doc;
	xmlNodePtr root, node;
	int ret;

	d(printf("Saving folder metainfo to : %s\n", meta->path));

	doc = xmlNewDoc("1.0");
	root = xmlNewDocNode(doc, NULL, "folderinfo", NULL);
	xmlDocSetRootElement(doc, root);

	node  = xmlNewChild(root, NULL, "folder", NULL);
	xmlSetProp(node, "type", meta->format);
	xmlSetProp(node, "name", meta->name);
	xmlSetProp(node, "index", meta->indexed?"1":"0");

	ret = xmlSaveFile(meta->path, doc);
	xmlFreeDoc(doc);

	return ret;
}

/* forward a bunch of functions to the real folder. This pretty
 * much sucks but I haven't found a better way of doing it.
 */

/* We need to do it without having locked our folder, otherwise
   we can get sync hangs with vfolders/trash */
static void
mlf_refresh_info(CamelFolder *folder, CamelException *ex)
{
	MailLocalFolder *mlf = MAIL_LOCAL_FOLDER(folder);
	CamelFolder *f;

	LOCAL_FOLDER_LOCK(mlf);
	f = mlf->real_folder;
	camel_object_ref((CamelObject *)f);
	LOCAL_FOLDER_UNLOCK(mlf);

	camel_folder_refresh_info(f, ex);
	camel_object_unref((CamelObject *)f);
}

static void
mlf_sync(CamelFolder *folder, gboolean expunge, CamelException *ex)
{
	MailLocalFolder *mlf = MAIL_LOCAL_FOLDER(folder);
	CamelFolder *f;

	LOCAL_FOLDER_LOCK(mlf);
	f = mlf->real_folder;
	camel_object_ref((CamelObject *)f);
	LOCAL_FOLDER_UNLOCK(mlf);

	camel_folder_sync(f, expunge, ex);
	camel_object_unref((CamelObject *)f);
}

static void
mlf_expunge(CamelFolder *folder, CamelException *ex)
{
	MailLocalFolder *mlf = MAIL_LOCAL_FOLDER(folder);
	CamelFolder *f;

	LOCAL_FOLDER_LOCK(mlf);
	f = mlf->real_folder;
	camel_object_ref((CamelObject *)f);
	LOCAL_FOLDER_UNLOCK(mlf);

	camel_folder_expunge(f, ex);
	camel_object_unref((CamelObject *)f);
}

static void
mlf_append_message(CamelFolder *folder, CamelMimeMessage *message, const CamelMessageInfo *info, CamelException *ex)
{
	MailLocalFolder *mlf = MAIL_LOCAL_FOLDER(folder);
	CamelFolder *f;

	LOCAL_FOLDER_LOCK(mlf);
	f = mlf->real_folder;
	camel_object_ref((CamelObject *)f);
	LOCAL_FOLDER_UNLOCK(mlf);

	camel_folder_append_message(f, message, info, ex);
	camel_object_unref((CamelObject *)f);
}

static CamelMimeMessage *
mlf_get_message(CamelFolder *folder, const char *uid, CamelException *ex)
{
	MailLocalFolder *mlf = MAIL_LOCAL_FOLDER(folder);
	CamelMimeMessage *ret;
	CamelFolder *f;

	LOCAL_FOLDER_LOCK(mlf);
	f = mlf->real_folder;
	camel_object_ref((CamelObject *)f);
	LOCAL_FOLDER_UNLOCK(mlf);

	ret = camel_folder_get_message(f, uid, ex);
	camel_object_unref((CamelObject *)f);

	return ret;
}

static GPtrArray *
mlf_search_by_expression(CamelFolder *folder, const char *expression, CamelException *ex)
{
	MailLocalFolder *mlf = MAIL_LOCAL_FOLDER (folder);
	GPtrArray *ret;
	CamelFolder *f;

	LOCAL_FOLDER_LOCK(mlf);
	f = mlf->real_folder;
	camel_object_ref((CamelObject *)f);
	LOCAL_FOLDER_UNLOCK(mlf);

	ret = camel_folder_search_by_expression(f, expression, ex);
	camel_object_unref((CamelObject *)f);

	return ret;
}

static void
mlf_search_free(CamelFolder *folder, GPtrArray *result)
{
	MailLocalFolder *mlf = MAIL_LOCAL_FOLDER(folder);
	CamelFolder *f;

	LOCAL_FOLDER_LOCK(mlf);
	f = mlf->real_folder;
	camel_object_ref((CamelObject *)f);
	LOCAL_FOLDER_UNLOCK(mlf);

	camel_folder_search_free(f, result);
	camel_object_unref((CamelObject *)f);
}

static void
mlf_set_message_flags(CamelFolder *folder, const char *uid, guint32 flags, guint32 set)
{
	MailLocalFolder *mlf = MAIL_LOCAL_FOLDER(folder);
	CamelFolder *f;

	LOCAL_FOLDER_LOCK(mlf);
	f = mlf->real_folder;
	camel_object_ref((CamelObject *)f);
	LOCAL_FOLDER_UNLOCK(mlf);

	camel_folder_set_message_flags(mlf->real_folder, uid, flags, set);
	camel_object_unref((CamelObject *)f);
}

static void
mlf_set_message_user_flag(CamelFolder *folder, const char *uid, const char *name, gboolean value)
{
	MailLocalFolder *mlf = MAIL_LOCAL_FOLDER(folder);
	CamelFolder *f;

	LOCAL_FOLDER_LOCK(mlf);
	f = mlf->real_folder;
	camel_object_ref((CamelObject *)f);
	LOCAL_FOLDER_UNLOCK(mlf);

	camel_folder_set_message_user_flag(mlf->real_folder, uid, name, value);
	camel_object_unref((CamelObject *)f);
}

static void
mlf_set_message_user_tag(CamelFolder *folder, const char *uid, const char *name, const char *value)
{
	MailLocalFolder *mlf = MAIL_LOCAL_FOLDER(folder);
	CamelFolder *f;

	LOCAL_FOLDER_LOCK(mlf);
	f = mlf->real_folder;
	camel_object_ref((CamelObject *)f);
	LOCAL_FOLDER_UNLOCK(mlf);

	camel_folder_set_message_user_tag(mlf->real_folder, uid, name, value);
	camel_object_unref((CamelObject *)f);
}

/* and, conversely, forward the real folder's signals. */

static void
mlf_proxy_message_changed(CamelObject *real_folder, gpointer event_data, gpointer user_data)
{
	camel_object_trigger_event((CamelObject *)user_data, "message_changed", event_data);
}

static void
mlf_proxy_folder_changed(CamelObject *real_folder, gpointer event_data, gpointer user_data)
{
	camel_object_trigger_event((CamelObject *)user_data, "folder_changed", event_data);
}

static void
mlf_unset_folder (MailLocalFolder *mlf)
{
	CamelFolder *folder = (CamelFolder *)mlf;

	g_assert(mlf->real_folder);

	camel_object_unhook_event(CAMEL_OBJECT(mlf->real_folder),
				  "message_changed",
				  mlf_proxy_message_changed,
				  mlf);
	camel_object_unhook_event(CAMEL_OBJECT(mlf->real_folder),
				  "folder_changed",
				  mlf_proxy_folder_changed,
				  mlf);

	camel_object_unref((CamelObject *)folder->summary);
	folder->summary = NULL;
	camel_object_unref((CamelObject *)mlf->real_folder);
	mlf->real_folder = NULL;
	camel_object_unref((CamelObject *)mlf->real_store);
	mlf->real_store = NULL;

	folder->permanent_flags = 0;
	folder->has_summary_capability = 0;
	folder->has_search_capability = 0;
}

static gboolean
mlf_set_folder(MailLocalFolder *mlf, guint32 flags, CamelException *ex)
{
	CamelFolder *folder = (CamelFolder *)mlf;
	char *uri;

	g_assert(mlf->real_folder == NULL);

	uri = g_strdup_printf("%s:%s%s", mlf->meta->format, ((CamelService *)folder->parent_store)->url->path, mlf->real_path);
	d(printf("opening real store: %s\n", uri));
	mlf->real_store = camel_session_get_store(session, uri, ex);
	g_free(uri);
	if (mlf->real_store == NULL)
		return FALSE;

	if (mlf->meta->indexed)
		flags |= CAMEL_STORE_FOLDER_BODY_INDEX;

	mlf->real_folder = camel_store_get_folder(mlf->real_store, mlf->meta->name, flags, ex);
	if (mlf->real_folder == NULL)
		return FALSE;

	if (mlf->real_folder->has_summary_capability) {
		folder->summary = mlf->real_folder->summary;
		camel_object_ref((CamelObject *)mlf->real_folder->summary);
	}

	folder->permanent_flags = mlf->real_folder->permanent_flags;
	folder->has_summary_capability = mlf->real_folder->has_summary_capability;
	folder->has_search_capability = mlf->real_folder->has_search_capability;

	camel_object_hook_event((CamelObject *)mlf->real_folder, "message_changed", mlf_proxy_message_changed, mlf);
	camel_object_hook_event((CamelObject *)mlf->real_folder, "folder_changed", mlf_proxy_folder_changed, mlf);

	return TRUE;
}

static void 
mlf_class_init (CamelObjectClass *camel_object_class)
{
	CamelFolderClass *camel_folder_class = CAMEL_FOLDER_CLASS (camel_object_class);

	/* override all the functions subclassed in providers/local/ */

	camel_folder_class->refresh_info = mlf_refresh_info;
	camel_folder_class->sync = mlf_sync;
	camel_folder_class->expunge = mlf_expunge;
	camel_folder_class->append_message = mlf_append_message;
	camel_folder_class->get_message = mlf_get_message;
	camel_folder_class->search_free = mlf_search_free;

	camel_folder_class->search_by_expression = mlf_search_by_expression;
	camel_folder_class->set_message_flags = mlf_set_message_flags;
	camel_folder_class->set_message_user_flag = mlf_set_message_user_flag;
	camel_folder_class->set_message_user_tag = mlf_set_message_user_tag;
}

static void
mlf_init (CamelObject *obj)
{
	MailLocalFolder *mlf = MAIL_LOCAL_FOLDER (obj);

#ifdef ENABLE_THREADS
	mlf->real_folder_lock = g_mutex_new();
#endif
}

static void
mlf_finalize (CamelObject *obj)
{
	MailLocalFolder *mlf = MAIL_LOCAL_FOLDER (obj);

	if (mlf->real_folder)
		mlf_unset_folder(mlf);

	free_metainfo(mlf->meta);
	
#ifdef ENABLE_THREADS
	g_mutex_free (mlf->real_folder_lock);
#endif
}

static CamelType
mail_local_folder_get_type (void)
{
	static CamelType mail_local_folder_type = CAMEL_INVALID_TYPE;

	if (mail_local_folder_type == CAMEL_INVALID_TYPE) {
		mail_local_folder_type = camel_type_register(CAMEL_FOLDER_TYPE,
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

static MailLocalFolder *
mail_local_folder_construct(MailLocalFolder *mlf, MailLocalStore  *parent_store, const char *full_name, CamelException *ex)
{
	const char *name;
	char *metapath;

	name = strrchr(full_name, '/');
	if (name == NULL)
		name = full_name;
	name = name + 1;

	d(printf("constructing local folder: full = %s, name = %s\n", full_name, name));

	camel_folder_construct(CAMEL_FOLDER (mlf), CAMEL_STORE(parent_store), full_name, name);

	mlf->real_path = g_strdup(((CamelFolder *)mlf)->full_name);

	metapath = g_strdup_printf("%s/%s/local-metadata.xml", ((CamelService *)parent_store)->url->path, full_name);
	mlf->meta = load_metainfo(metapath);
	g_free(metapath);

	return mlf;
}

static gboolean
mail_local_folder_reconfigure (MailLocalFolder *mlf, const char *new_format, CamelException *ex)
{
	CamelStore *fromstore = NULL;
	CamelFolder *fromfolder = NULL;
	char *oldformat = NULL;
	char *tmpname;
	char *store_uri;
	GPtrArray *uids;
	int real_folder_frozen = FALSE;

	camel_operation_start(NULL, _("Reconfiguring folder"));

	/* first things first */
	g_assert (ex);
	LOCAL_FOLDER_LOCK (mlf);

	/* first, 'close' the old folder */
	if (mlf->real_folder) {
		camel_folder_sync(mlf->real_folder, FALSE, ex);
		if (camel_exception_is_set (ex))
			goto cleanup;
		mlf_unset_folder(mlf);
	}

	store_uri = g_strdup_printf("%s:%s%s", mlf->meta->format,
				    ((CamelService *)((CamelFolder *)mlf)->parent_store)->url->path, mlf->real_path);
	fromstore = camel_session_get_store(session, store_uri, ex);
	g_free(store_uri);
	if (fromstore == NULL)
		goto cleanup;

	oldformat = mlf->meta->format;
	mlf->meta->format = g_strdup(new_format);

	/* rename the old mbox and open it again, without indexing */
	tmpname = g_strdup_printf ("%s_reconfig", mlf->meta->name);
	d(printf("renaming %s to %s, and opening it\n", mlf->meta->name, tmpname));
	
	camel_store_rename_folder(fromstore, mlf->meta->name, tmpname, ex);
	if (camel_exception_is_set(ex))
		goto cleanup;
	
	/* we dont need to set the create flag ... or need an index if it has one */
	fromfolder = camel_store_get_folder(fromstore, tmpname, 0, ex);
	if (fromfolder == NULL || camel_exception_is_set(ex)) {
		/* try and recover ... */
		camel_exception_clear(ex);
		camel_store_rename_folder(fromstore, tmpname, mlf->meta->name, ex);
		goto cleanup;
	}
	
	/* create a new mbox */
	d(printf("Creating the destination mbox\n"));

	if (!mlf_set_folder(mlf, CAMEL_STORE_FOLDER_CREATE, ex)) {
		d(printf("cannot open destination folder\n"));
		/* try and recover ... */
		camel_exception_clear(ex);
		camel_store_rename_folder(fromstore, tmpname, mlf->meta->name, ex);
		goto cleanup;
	}

	real_folder_frozen = TRUE;
	camel_folder_freeze(mlf->real_folder);

	uids = camel_folder_get_uids(fromfolder);
	camel_folder_move_messages_to(fromfolder, uids, mlf->real_folder, ex);
	camel_folder_free_uids(fromfolder, uids);
	if (camel_exception_is_set(ex))
		goto cleanup;
	
	camel_folder_expunge(fromfolder, ex);
	
	d(printf("delete old mbox ...\n"));
	camel_object_unref(CAMEL_OBJECT(fromfolder));
	fromfolder = NULL;
	camel_store_delete_folder(fromstore, tmpname, ex);
	
	/* switch format */
	g_free(oldformat);
	oldformat = NULL;
	if (save_metainfo(mlf->meta) == FALSE) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Cannot save folder metainfo; "
					"you'll probably find you can't\n"
					"open this folder anymore: %s: %s"),
				      mlf->meta->path, strerror(errno));
	}
	
 cleanup:
	if (oldformat) {
		g_free(mlf->meta->format);
		mlf->meta->format = oldformat;
	}
	if (mlf->real_folder == NULL)
		mlf_set_folder (mlf, CAMEL_STORE_FOLDER_CREATE, ex);
	if (fromfolder)
		camel_object_unref((CamelObject *)fromfolder);
	if (fromstore)
		camel_object_unref((CamelObject *)fromstore);

	LOCAL_FOLDER_UNLOCK (mlf);

	if (real_folder_frozen)
		camel_folder_thaw(mlf->real_folder);

	camel_operation_end(NULL);

	return !camel_exception_is_set(ex);
}
		
/* ******************************************************************************** */

static CamelObjectClass *local_store_parent_class = NULL;

static CamelFolder *
mls_get_folder(CamelStore *store, const char *folder_name, guint32 flags, CamelException *ex)
{
	MailLocalStore *local_store = MAIL_LOCAL_STORE (store);
	MailLocalFolder *folder;

	d(printf("--LOCAL-- get_folder: %s\n", folder_name));

	folder = (MailLocalFolder *)camel_object_new(MAIL_LOCAL_FOLDER_TYPE);
	folder = mail_local_folder_construct(folder, local_store, folder_name, ex);
	if (folder == NULL)
		return NULL;

	if (!mlf_set_folder(folder, flags, ex)) {
		camel_object_unref(CAMEL_OBJECT(folder));
		return NULL;
	}

	if (flags & CAMEL_STORE_FOLDER_CREATE) {
		if (save_metainfo(folder->meta) == -1) {
			camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
					      _("Cannot save folder metainfo to %s: %s"),
					      folder->meta->path, strerror(errno));
			camel_object_unref(CAMEL_OBJECT (folder));
			return NULL;
		}
	}

	return (CamelFolder *)folder;
}

static void
mls_delete_folder(CamelStore *store, const char *folder_name, CamelException *ex)
{
	CamelStore *real_store;
	char *metapath, *uri;
	CamelException local_ex;
	struct _local_meta *meta;

	d(printf("Deleting folder: %s %s\n", ((CamelService *)store)->url->path, folder_name));

	camel_exception_init(&local_ex);

	/* find the real store for this folder, and proxy the call */
	metapath = g_strdup_printf("%s%s/local-metadata.xml", ((CamelService *)store)->url->path, folder_name);
	meta = load_metainfo(metapath);
	uri = g_strdup_printf("%s:%s%s", meta->format, ((CamelService *)store)->url->path, folder_name);
	real_store = (CamelStore *)camel_session_get_service(session, uri, CAMEL_PROVIDER_STORE, ex);
	g_free(uri);
	if (real_store == NULL) {
		g_free(metapath);
		free_metainfo(meta);
		return;
	}

	camel_store_delete_folder(real_store, meta->name, &local_ex);
	if (camel_exception_is_set(&local_ex)) {
		camel_exception_xfer(ex, &local_ex);
		g_free(metapath);
		free_metainfo(meta);
		return;
	}

	free_metainfo(meta);

	if (unlink(metapath) == -1) {
		camel_exception_setv(ex, CAMEL_EXCEPTION_SYSTEM,
				     _("Cannot delete folder metadata %s: %s"),
				     metapath, strerror(errno));
	}

	g_free(metapath);
}

static char *
mls_get_name (CamelService *service, gboolean brief)
{
	if (brief)
		return g_strdup("local");

	return g_strdup("Local mail folders");
}

static void
mls_init (MailLocalStore *mls, MailLocalStoreClass *mlsclass)
{
	mls->folder_infos = g_hash_table_new(g_str_hash, g_str_equal);
	mls->folder_info_lock = g_mutex_new();
}

static void
free_info(void *key, void *value, void *data)
{
	CamelFolderInfo *info = value;

	camel_folder_info_free (info);
}

static void
mls_finalise(MailLocalStore *mls)
{
	g_hash_table_foreach(mls->folder_infos, (GHFunc)free_info, NULL);
	g_hash_table_destroy(mls->folder_infos);
	g_mutex_free(mls->folder_info_lock);
}

static void
mls_class_init (CamelObjectClass *camel_object_class)
{
	CamelStoreClass *camel_store_class = CAMEL_STORE_CLASS(camel_object_class);
	CamelServiceClass *camel_service_class = CAMEL_SERVICE_CLASS(camel_object_class);
	
	/* virtual method overload -- the bare minimum */
	camel_service_class->get_name    = mls_get_name;
	camel_store_class->get_folder    = mls_get_folder;
	camel_store_class->delete_folder = mls_delete_folder;

	local_store_parent_class = camel_type_get_global_classfuncs (CAMEL_STORE_TYPE);
}

static CamelType
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
			(CamelObjectInitFunc) mls_init,
			(CamelObjectFinalizeFunc) mls_finalise);
	}

	return mail_local_store_type;
}

static void mail_local_store_add_folder(MailLocalStore *mls, const char *uri, const char *path, const char *name)
{
	CamelFolderInfo *info = NULL;
	CamelURL *url;

	d(printf("Shell adding folder: '%s' path = '%s'\n", uri, path));

	url = camel_url_new(uri, NULL);
	if (url == NULL) {
		g_warning("Shell trying to add invalid folder url: %s", uri);
		return;
	}
	if (url->path == NULL || url->path[0] == 0) {
		g_warning("Shell trying to add invalid folder url: %s", uri);
		camel_url_free(url);
		return;
	}

	LOCAL_STORE_LOCK(mls);

	if (g_hash_table_lookup(mls->folder_infos, uri)) {
		g_warning("Shell trying to add a folder I already have!");
	} else {
		info = g_malloc0(sizeof(*info));
		info->url = g_strdup(uri);
		info->full_name = g_strdup(url->path+1);
		info->name = g_strdup(name);
		info->unread_message_count = -1;
		info->path = g_strdup (path);
		g_hash_table_insert(mls->folder_infos, info->url, info);
	}

	LOCAL_STORE_UNLOCK(mls);

	camel_url_free(url);

	if (info) {
		/* FIXME: should copy info, so we dont get a removed while we're using it? */
		camel_object_trigger_event((CamelObject *)mls, "folder_created", info);

		/* this is just so the folder is opened at least once to setup the folder
		   counts etc in the display.  Joy eh?   The result is discarded. */
		mail_get_folder(uri, NULL, NULL, mail_thread_queued_slow);
	}
}

struct _search_info {
	const char *path;
	CamelFolderInfo *info;
};

static void
remove_find_path(char *uri, CamelFolderInfo *info, struct _search_info *data)
{
	if (!strcmp(info->path, data->path))
		data->info = info;
}

static void mail_local_store_remove_folder(MailLocalStore *mls, const char *path)
{
	struct _search_info data = { path, NULL };

	/* we're keyed on uri, not path, so have to search for it manually */

	LOCAL_STORE_LOCK(mls);
	g_hash_table_foreach(mls->folder_infos, (GHFunc)remove_find_path, &data);
	if (data.info)
		g_hash_table_remove(mls->folder_infos, data.info->url);
	LOCAL_STORE_UNLOCK(mls);

	if (data.info) {
		camel_object_trigger_event((CamelObject *)mls, "folder_deleted", data.info);

		g_free(data.info->url);
		g_free(data.info->full_name);
		g_free(data.info->name);
		g_free(data.info);
	}
}

/* ** Local Provider ************************************************************** */

static CamelProvider local_provider = {
	"file", "Local mail", NULL, "mail",
	CAMEL_PROVIDER_IS_STORAGE | CAMEL_PROVIDER_IS_EXTERNAL,
	CAMEL_URL_NEED_PATH,
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

static void
mail_local_provider_init (void)
{
	/* Register with Camel to handle file: URLs */
	local_provider.object_types[CAMEL_PROVIDER_STORE] = MAIL_LOCAL_STORE_TYPE;

	local_provider.service_cache = g_hash_table_new (non_hash, non_equal);
	local_provider.url_hash = non_hash;
	local_provider.url_equal = non_equal;
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

	d(printf("Local folder new:\n"));
	d(printf(" path = '%s'\n uri = '%s'\n display = '%s'\n",
		 path, folder->physicalUri, folder->displayName));

	mail_local_store_add_folder(global_local_store, folder->physicalUri, path, folder->displayName);

	/* are we supposed to say anything about it? */
}


static void
local_storage_removed_folder_cb (EvolutionStorageListener *storage_listener,
				 const char *path,
				 void *data)
{
	d(printf("Local folder remove:\n"));
	d(printf(" path = '%s'\n", path));

	mail_local_store_remove_folder(global_local_store, path);
}

static void
storage_listener_startup (EvolutionShellClient *shellclient)
{
	EvolutionStorageListener *local_storage_listener;
	GNOME_Evolution_StorageListener corba_local_storage_listener;
	GNOME_Evolution_Storage corba_storage;
	CORBA_Environment ev;

	d(printf("---- CALLING STORAGE LISTENER STARTUP ---\n"));

	local_corba_storage = corba_storage = evolution_shell_client_get_local_storage (shellclient);
	if (corba_storage == CORBA_OBJECT_NIL) {
		g_warning ("No local storage available from shell client!");
		return;
	}

	/* setup to record this store's changes */
	mail_note_store((CamelStore *)global_local_store, NULL, local_corba_storage, NULL, NULL);

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
	gtk_signal_connect (GTK_OBJECT (local_storage_listener),
			    "removed_folder",
	 		    GTK_SIGNAL_FUNC (local_storage_removed_folder_cb),
			    corba_storage);

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

void
mail_local_storage_startup (EvolutionShellClient *shellclient, const char *evolution_path)
{
	mail_local_provider_init ();

	global_local_store = MAIL_LOCAL_STORE(camel_session_get_service (session, "file:/", CAMEL_PROVIDER_STORE, NULL));

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

/* hash table of folders that the user has a reconfig-folder dialog for */
static GHashTable *reconfigure_folder_hash = NULL;

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

	/* remove this folder from our hash since we are done with it */
	g_hash_table_remove (reconfigure_folder_hash, m->fb->folder);
	if (g_hash_table_size (reconfigure_folder_hash) == 0) {
		/* additional cleanup */
		g_hash_table_destroy (reconfigure_folder_hash);
		reconfigure_folder_hash = NULL;
	}

	if (m->folder_out)
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

static void
reconfigure_clicked (GnomeDialog *dialog, int button, struct _reconfigure_msg *m)
{
	if (button == 0) {
		GtkWidget *menu, *item;
		
		/* hack to clear the message list during update */
		/* we need to do this because the message list caches
		 * CamelMessageInfos from the old folder. */
		message_list_set_folder(m->fb->message_list, NULL, FALSE);
		
		menu = gtk_option_menu_get_menu(m->optionlist);
		item = gtk_menu_get_active(GTK_MENU(menu));
		m->newtype = g_strdup(gtk_object_get_data((GtkObject *)item, "type"));

		gtk_widget_set_sensitive (m->frame, FALSE);
		gtk_widget_set_sensitive (m->apply, FALSE);
		gtk_widget_set_sensitive (m->cancel, FALSE);
		
		e_thread_put (mail_thread_queued, (EMsg *)m);
	} else
		mail_msg_free ((struct _mail_msg *)m);
	
	if (button != -1)
		gnome_dialog_close (dialog);
}

void
mail_local_reconfigure_folder (FolderBrowser *fb)
{
	GladeXML *gui;
	GnomeDialog *gd;
	struct _reconfigure_msg *m;
	char *title;
	GList *p;
	GtkWidget *menu;
	char *currentformat;
	int index=0, history=0;

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
	
	title = g_strdup_printf (_("Reconfigure /%s"),
				 camel_folder_get_full_name (fb->folder));
	gtk_window_set_title (GTK_WINDOW (gd), title);
	g_free (title);
	
	m->frame = glade_xml_get_widget (gui, "frame_format");
	m->apply = glade_xml_get_widget (gui, "apply_format");
	m->cancel = glade_xml_get_widget (gui, "cancel_format");
	m->optionlist = (GtkOptionMenu *)glade_xml_get_widget (gui, "option_format");
	m->newtype = NULL;
	m->fb = fb;
	m->folder_out = NULL;
	gtk_object_ref (GTK_OBJECT (fb));

	/* dynamically create the folder type list from camel */
	/* we assume the list is static and never freed */
	currentformat = MAIL_LOCAL_FOLDER (fb->folder)->meta->format;
	p = camel_session_list_providers(session, TRUE);
	menu = gtk_menu_new();
	while (p) {
		CamelProvider *cp = p->data;

		/* we only want local providers */
		if (cp->flags & CAMEL_PROVIDER_IS_LOCAL) {
			GtkWidget *item;
			char *label;

			if (strcmp(cp->protocol, currentformat) == 0)
				history = index;

			label = g_strdup_printf("%s (%s)", cp->protocol, _(cp->name));
			item = gtk_menu_item_new_with_label(label);
			g_free(label);
			gtk_object_set_data((GtkObject *)item, "type", cp->protocol);
			gtk_widget_show(item);
			gtk_menu_append(GTK_MENU(menu), item);
			index++;
		}
		p = p->next;
	}
	gtk_option_menu_remove_menu (GTK_OPTION_MENU(m->optionlist));
	gtk_option_menu_set_menu (GTK_OPTION_MENU(m->optionlist), menu);
	gtk_option_menu_set_history(GTK_OPTION_MENU(m->optionlist), history);

	gtk_label_set_text ((GtkLabel *)glade_xml_get_widget (gui, "label_format"),
			    MAIL_LOCAL_FOLDER (fb->folder)->meta->format);
	
	gtk_signal_connect (GTK_OBJECT (gd), "clicked", reconfigure_clicked, m);
	gtk_object_unref (GTK_OBJECT (gui));
	
	g_hash_table_insert (reconfigure_folder_hash, (gpointer) fb->folder, (gpointer) gd);
	
	gnome_dialog_run (GNOME_DIALOG (gd));
}
