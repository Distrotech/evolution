/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * 
 * Authors: Peter Williams <peterw@ximian.com>
 *	    Michael Zucchi <notzed@ximian.com>
 *
 * Copyright 2000,2001 Ximian, Inc. (www.ximian.com)
 *
 * This program is free software; you can redistribute it and/or 
 * modify it under the terms of version 2 of the GNU General Public 
 * License as published by the Free Software Foundation.
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
#include "config.h"
#endif

#ifdef G_LOG_DOMAIN
#undef G_LOG_DOMAIN
#endif
#define G_LOG_DOMAIN "folder tree"

#include <pthread.h>

#include <bonobo/bonobo-exception.h>
#include <camel/camel-store.h>
#include <camel/camel-folder.h>
#include <camel/camel-vtrash-folder.h>
#include <camel/camel-vee-store.h>
#include <gal/util/e-unicode-i18n.h>

#include "mail-mt.h"
#include "mail-folder-cache.h"
#include "mail-ops.h"

/* For notifications of changes */
#include "mail-vfolder.h"
#include "mail-autofilter.h"

#define w(x)
#define d(x) 

/* note that many things are effectively serialised by having them run in
   the main loop thread which they need to do because of corba/gtk calls */
#define LOCK(x) pthread_mutex_lock(&x)
#define UNLOCK(x) pthread_mutex_unlock(&x)

static pthread_mutex_t info_lock = PTHREAD_MUTEX_INITIALIZER;

struct _folder_info {
	struct _store_info *store_info;	/* 'parent' link */

	char *path;		/* shell path */
	char *full_name;	/* full name of folder/folderinfo */
	char *uri;		/* uri of folder */

	CamelFolder *folder;	/* if known */
};

/* pending list of updates */
struct _folder_update {
	struct _folder_update *next;
	struct _folder_update *prev;

	unsigned int remove:1;	/* removing from vfolders */
	unsigned int delete:1;	/* deleting as well? */
	unsigned int add:1;	/* add to vfolder */

	char *path;
	char *name;
	char *uri;
	char *oldpath;
	char *olduri;

	int unread;
	CamelStore *store;
};

struct _store_info {
	GHashTable *folders;	/* by full_name */
	GHashTable *folders_uri; /* by uri */

	CamelStore *store;	/* the store for these folders */

	/* only 1 should be set */
	EvolutionStorage *storage;
	GNOME_Evolution_Storage corba_storage;

	/* Outstanding folderinfo requests */
	EDList folderinfo_updates;
};

static void folder_changed(CamelObject *o, gpointer event_data, gpointer user_data);
static void folder_deleted(CamelObject *o, gpointer event_data, gpointer user_data);
static void folder_renamed(CamelObject *o, gpointer event_data, gpointer user_data);
static void folder_finalised(CamelObject *o, gpointer event_data, gpointer user_data);

/* Store to storeinfo table, active stores */
static GHashTable *stores;

/* List of folder changes to be executed in gui thread */
static EDList updates = E_DLIST_INITIALISER(updates);
static int update_id = -1;

/* hack for people who LIKE to have unsent count */
static int count_sent = FALSE;
static int count_trash = FALSE;

static void
free_update(struct _folder_update *up)
{
	g_free(up->path);
	g_free(up->name);
	g_free(up->uri);
	if (up->store)
		camel_object_unref((CamelObject *)up->store);
	g_free(up->oldpath);
	g_free(up->olduri);
	g_free(up);
}

static void
real_flush_updates(void *o, void *event_data, void *data)
{
	struct _folder_update *up;
	struct _store_info *si;
	EvolutionStorage *storage;
	GNOME_Evolution_Storage corba_storage;
	CORBA_Environment ev;

	LOCK(info_lock);
	while ((up = (struct _folder_update *)e_dlist_remhead(&updates))) {

		si = g_hash_table_lookup(stores, up->store);
		if (si) {
			storage = si->storage;
			if (storage)
				bonobo_object_ref((BonoboObject *)storage);
			corba_storage = si->corba_storage;
		} else {
			storage = NULL;
			corba_storage = CORBA_OBJECT_NIL;
		}

		UNLOCK(info_lock);

		if (up->remove) {
			if (up->delete) {
				mail_vfolder_delete_uri(up->store, up->uri);
				mail_filter_delete_uri(up->store, up->uri);
			} else
				mail_vfolder_add_uri(up->store, up->uri, TRUE);
		} else {
			/* Its really a rename, but we have no way of telling the shell that, so remove it */
			if (up->oldpath) {
				if (storage != NULL) {
					d(printf("Removing old folder (rename?) '%s'\n", up->oldpath));
					evolution_storage_removed_folder(storage, up->oldpath);
				}
				/* ELSE? Shell supposed to handle the local snot case */
			}

			/* We can tell the vfolder code though */
			if (up->olduri && up->add) {
				d(printf("renaming folder '%s' to '%s'\n", up->olduri, up->uri));
				mail_vfolder_rename_uri(up->store, up->olduri, up->uri);
				mail_filter_rename_uri(up->store, up->olduri, up->uri);
			}
				
			if (up->name == NULL) {
				if (storage != NULL) {
					d(printf("Updating existing folder: %s (%d unread)\n", up->path, up->unread));
					evolution_storage_update_folder(storage, up->path, up->unread);
				} else if (corba_storage != CORBA_OBJECT_NIL) {
					d(printf("Updating existing (local) folder: %s (%d unread)\n", up->path, up->unread));
					CORBA_exception_init(&ev);
					GNOME_Evolution_Storage_updateFolder(corba_storage, up->path, up->unread, &ev);
					CORBA_exception_free(&ev);
				}
			} else if (storage != NULL) {
				char *type = (strncmp(up->uri, "vtrash:", 7)==0)?"vtrash":"mail";
			
				d(printf("Adding new folder: %s\n", up->path));
				evolution_storage_new_folder(storage, up->path, up->name, type, up->uri, up->name, up->unread);
			}

			if (!up->olduri && up->add)
				mail_vfolder_add_uri(up->store, up->uri, FALSE);
		}

		free_update(up);

		if (storage)
			bonobo_object_unref((BonoboObject *)storage);
		
		LOCK(info_lock);
	}
	update_id = -1;
	UNLOCK(info_lock);
}

static void
flush_updates(void)
{
	if (update_id == -1 && !e_dlist_empty(&updates))
		update_id = mail_async_event_emit(mail_async_event, MAIL_ASYNC_GUI, (MailAsyncFunc)real_flush_updates, 0, 0, 0);
}

static void
unset_folder_info(struct _folder_info *mfi, int delete)
{
	struct _folder_update *up;

	if (mfi->folder) {
		CamelFolder *folder = mfi->folder;

		camel_object_unhook_event((CamelObject *)folder, "folder_changed", folder_changed, mfi);
		camel_object_unhook_event((CamelObject *)folder, "message_changed", folder_changed, mfi);
		camel_object_unhook_event((CamelObject *)folder, "deleted", folder_deleted, mfi);
		camel_object_unhook_event((CamelObject *)folder, "renamed", folder_renamed, mfi);
		camel_object_unhook_event((CamelObject *)folder, "finalize", folder_finalised, mfi);
	}

	if (strstr(mfi->uri, ";noselect") == NULL) {
		up = g_malloc0(sizeof(*up));

		up->remove = TRUE;
		up->delete = delete;
		up->store = mfi->store_info->store;
		camel_object_ref((CamelObject *)up->store);
		up->uri = g_strdup(mfi->uri);

		e_dlist_addtail(&updates, (EDListNode *)up);
		flush_updates();
	}
}

static void
free_folder_info(struct _folder_info *mfi)
{
	g_free(mfi->path);
	g_free(mfi->full_name);
	g_free(mfi->uri);
	g_free(mfi);
}

/* This is how unread counts work (and don't work):
 *
 * camel_folder_unread_message_count() only gives a correct answer if
 * the store is paying attention to the folder. (Some stores always
 * pay attention to all folders, but IMAP can only pay attention to
 * one folder at a time.) But it doesn't have any way to know when
 * it's lying, so it's only safe to call it when you know for sure
 * that the store is paying attention to the folder, such as when it's
 * just been created, or you get a folder_changed or message_changed
 * signal on it.
 *
 * camel_store_get_folder_info() always gives correct answers for the
 * folders it checks, but it can also return -1 for a folder, meaning
 * it didn't check, and so you should stick with your previous answer.
 *
 * update_1folder is called from three places: with info != NULL when
 * the folder is created (or get_folder_info), with info == NULL when
 * a folder changed event is emitted.
 *
 * So if info is NULL, camel_folder_unread_message_count is correct,
 * and if it's not NULL and its unread_message_count isn't -1, then
 * it's correct.  */

static void
update_1folder(struct _folder_info *mfi, CamelFolderInfo *info)
{
	struct _store_info *si;
	struct _folder_update *up;
	CamelFolder *folder;
	int unread = -1;
	extern CamelFolder *outbox_folder, *sent_folder;

	si  = mfi->store_info;

	folder = mfi->folder;
	if (folder) {
		if ((count_trash && CAMEL_IS_VTRASH_FOLDER (folder))
		    || folder == outbox_folder
		    || (count_sent && folder == sent_folder)) {
			unread = camel_folder_get_message_count(folder);
		} else {
			if (info)
				unread = info->unread_message_count;
			else
				unread = camel_folder_get_unread_message_count (folder);
		}
	} else if (info)
		unread = info->unread_message_count;

	if (unread == -1)
		return;

	up = g_malloc0(sizeof(*up));
	up->path = g_strdup(mfi->path);
	up->unread = unread;
	up->store = mfi->store_info->store;
	camel_object_ref((CamelObject *)up->store);
	e_dlist_addtail(&updates, (EDListNode *)up);
	flush_updates();
}

static void
setup_folder(CamelFolderInfo *fi, struct _store_info *si)
{
	struct _folder_info *mfi;
	struct _folder_update *up;

	mfi = g_hash_table_lookup(si->folders, fi->full_name);
	if (mfi) {
		update_1folder(mfi, fi);
	} else {
		/* always 'add it', but only 'add it' to non-local stores */
		/*d(printf("Adding new folder: %s (%s) %d unread\n", fi->path, fi->url, fi->unread_message_count));*/
		mfi = g_malloc0(sizeof(*mfi));
		mfi->path = g_strdup(fi->path);
		mfi->full_name = g_strdup(fi->full_name);
		mfi->uri = g_strdup(fi->url);
		mfi->store_info = si;
		g_hash_table_insert(si->folders, mfi->full_name, mfi);
		g_hash_table_insert(si->folders_uri, mfi->uri, mfi);

		up = g_malloc0(sizeof(*up));
		up->path = g_strdup(mfi->path);
		if (si->storage != NULL) {
			up->name = g_strdup(fi->name);
		}
		up->uri = g_strdup(fi->url);
		up->unread = (fi->unread_message_count==-1)?0:fi->unread_message_count;
		up->store = si->store;
		camel_object_ref((CamelObject *)up->store);
		if (strstr(fi->url, ";noselect") == NULL)
			up->add = TRUE;

		e_dlist_addtail(&updates, (EDListNode *)up);
		flush_updates();
	}
}

static void
create_folders(CamelFolderInfo *fi, struct _store_info *si)
{
	d(printf("Setup new folder: %s\n", fi->url));

	setup_folder(fi, si);

	if (fi->child)
		create_folders(fi->child, si);
	if (fi->sibling)
		create_folders(fi->sibling, si);
}

static void
folder_changed(CamelObject *o, gpointer event_data, gpointer user_data)
{
	struct _folder_info *mfi = user_data;

	if (mfi->folder != CAMEL_FOLDER(o))
		return;

	LOCK(info_lock);
	update_1folder(mfi, NULL);
	UNLOCK(info_lock);
}

static void
folder_finalised(CamelObject *o, gpointer event_data, gpointer user_data)
{
	struct _folder_info *mfi = user_data;

	d(printf("Folder finalised '%s'!\n", ((CamelFolder *)o)->full_name));
	LOCK(info_lock);
	mfi->folder = NULL;
	UNLOCK(info_lock);
}

static void
folder_deleted(CamelObject *o, gpointer event_data, gpointer user_data)
{
	struct _folder_info *mfi = user_data;

	d(printf("Folder deleted '%s'!\n", ((CamelFolder *)o)->full_name));
	LOCK(info_lock);
	mfi->folder = NULL;
	UNLOCK(info_lock);
}

static void
folder_renamed(CamelObject *o, gpointer event_data, gpointer user_data)
{
	struct _folder_info *mfi = user_data;
	CamelFolder *folder = (CamelFolder *)o;
	char *old = event_data;

	d(printf("Folder renamed from '%s' to '%s'\n", old, folder->full_name));

	mfi = mfi;
	old = old;
	folder = folder;
	/* Dont do anything, do it from the store rename event? */
}

void mail_note_folder(CamelFolder *folder)
{
	CamelStore *store = folder->parent_store;
	struct _store_info *si;
	struct _folder_info *mfi;

	if (stores == NULL) {
		g_warning("Adding a folder `%s' to a store which hasn't been added yet?\n", folder->full_name);
		return;
	}

	LOCK(info_lock);
	si = g_hash_table_lookup(stores, store);
	if (si == NULL) {
		/*g_warning("Adding a folder `%s' to a store %p which hasn't been added yet?", folder->full_name, store);*/
		UNLOCK(info_lock);
		return;
	}

	mfi = g_hash_table_lookup(si->folders, folder->full_name);
	if (mfi == NULL) {
		w(g_warning("Adding a folder `%s' that I dont know about yet?", folder->full_name));
		UNLOCK(info_lock);
		return;
	}

	/* dont do anything if we already have this */
	if (mfi->folder == folder) {
		UNLOCK(info_lock);
		return;
	}

	mfi->folder = folder;

	camel_object_hook_event((CamelObject *)folder, "folder_changed", folder_changed, mfi);
	camel_object_hook_event((CamelObject *)folder, "message_changed", folder_changed, mfi);
	camel_object_hook_event((CamelObject *)folder, "deleted", folder_deleted, mfi);
	camel_object_hook_event((CamelObject *)folder, "renamed", folder_renamed, mfi);
	camel_object_hook_event((CamelObject *)folder, "finalize", folder_finalised, mfi);

	update_1folder(mfi, NULL);

	UNLOCK(info_lock);
}

static void
store_folder_subscribed(CamelObject *o, void *event_data, void *data)
{
	struct _store_info *si;
	CamelFolderInfo *fi = event_data;

	d(printf("Store folder subscribed '%s' store '%s' \n", fi->full_name, camel_url_to_string(((CamelService *)o)->url, 0)));

	LOCK(info_lock);
	si = g_hash_table_lookup(stores, o);
	if (si)
		setup_folder(fi, si);
	UNLOCK(info_lock);
}

static void
store_folder_created(CamelObject *o, void *event_data, void *data)
{
	/* we only want created events to do more work if we dont support subscriptions */
	if (!camel_store_supports_subscriptions(CAMEL_STORE(o)))
		store_folder_subscribed(o, event_data, data);
}

static void
store_folder_unsubscribed(CamelObject *o, void *event_data, void *data)
{
	struct _store_info *si;
	CamelFolderInfo *fi = event_data;
	struct _folder_info *mfi;
	CamelStore *store = (CamelStore *)o;

	d(printf("Folder deleted: %s\n", fi->full_name));

	LOCK(info_lock);
	si = g_hash_table_lookup(stores, store);
	if (si) {
		mfi = g_hash_table_lookup(si->folders, fi->full_name);
		if (mfi) {
			g_hash_table_remove(si->folders, mfi->full_name);
			g_hash_table_remove(si->folders_uri, mfi->uri);
			unset_folder_info(mfi, TRUE);
			free_folder_info(mfi);
		}
	}
	UNLOCK(info_lock);
}

static void
store_folder_deleted(CamelObject *o, void *event_data, void *data)
{
	/* we only want deleted events to do more work if we dont support subscriptions */
	if (!camel_store_supports_subscriptions(CAMEL_STORE(o)))
		store_folder_unsubscribed(o, event_data, data);
}

static void
rename_folders(struct _store_info *si, const char *oldbase, const char *newbase, CamelFolderInfo *fi)
{
	char *old;
	struct _folder_info *mfi;
	struct _folder_update *up;

	up = g_malloc0(sizeof(*up));

	d(printf("oldbase '%s' newbase '%s' new '%s'\n", oldbase, newbase, fi->full_name));

	/* Form what was the old name, and try and look it up */
	old = g_strdup_printf("%s%s", oldbase, fi->full_name + strlen(newbase));
	mfi = g_hash_table_lookup(si->folders, old);
	if (mfi) {
		d(printf("Found old folder '%s' renaming to '%s'\n", mfi->full_name, fi->full_name));

		up->oldpath = mfi->path;
		up->olduri = mfi->uri;

		/* Its a rename op */
		g_hash_table_remove(si->folders, mfi->full_name);
		g_hash_table_remove(si->folders, mfi->uri);
		g_free(mfi->full_name);
		mfi->path = g_strdup(fi->path);
		mfi->full_name = g_strdup(fi->full_name);
		mfi->uri = g_strdup(fi->url);
		g_hash_table_insert(si->folders, mfi->full_name, mfi);
		g_hash_table_insert(si->folders_uri, mfi->uri, mfi);
	} else {
		d(printf("Rename found a new folder? old '%s' new '%s'\n", old, fi->full_name));
		/* Its a new op */
		mfi = g_malloc0(sizeof(*mfi));
		mfi->path = g_strdup(fi->path);
		mfi->full_name = g_strdup(fi->full_name);
		mfi->uri = g_strdup(fi->url);
		mfi->store_info = si;
		g_hash_table_insert(si->folders, mfi->full_name, mfi);
		g_hash_table_insert(si->folders_uri, mfi->uri, mfi);
	}

	g_free(old);

	up->path = g_strdup(mfi->path);
	if (si->storage)
		up->name = g_strdup(fi->name);
	up->uri = g_strdup(mfi->uri);
	up->unread = fi->unread_message_count==-1?0:fi->unread_message_count;
	up->store = si->store;
	camel_object_ref((CamelObject *)up->store);
	if (strstr(fi->url, ";noselect") == NULL)
		up->add = TRUE;

	e_dlist_addtail(&updates, (EDListNode *)up);
	flush_updates();
#if 0
	if (fi->sibling)
		rename_folders(si, oldbase, newbase, fi->sibling, folders);
	if (fi->child)
		rename_folders(si, oldbase, newbase, fi->child, folders);
#endif
}

static void
get_folders(CamelFolderInfo *fi, GPtrArray *folders)
{
	g_ptr_array_add(folders, fi);

	if (fi->child)
		get_folders(fi->child, folders);
	if (fi->sibling)
		get_folders(fi->sibling, folders);
}

static int
folder_cmp(const void *ap, const void *bp)
{
	const CamelFolderInfo *a = ((CamelFolderInfo **)ap)[0];
	const CamelFolderInfo *b = ((CamelFolderInfo **)bp)[0];

	return strcmp(a->path, b->path);
}

static void
store_folder_renamed(CamelObject *o, void *event_data, void *data)
{
	CamelStore *store = (CamelStore *)o;
	CamelRenameInfo *info = event_data;
	struct _store_info *si;

	d(printf("Folder renamed: oldbase = '%s' new->full = '%s'\n", info->old_base, info->new->full_name));

	LOCK(info_lock);
	si = g_hash_table_lookup(stores, store);
	if (si) {
		GPtrArray *folders = g_ptr_array_new();
		CamelFolderInfo *top;
		int i;

		/* Ok, so for some reason the folderinfo we have comes in all messed up from
		   imap, should find out why ... this makes it workable */
		get_folders(info->new, folders);
		qsort(folders->pdata, folders->len, sizeof(folders->pdata[0]), folder_cmp);

		top = folders->pdata[0];
		for (i=0;i<folders->len;i++) {
			rename_folders(si, info->old_base, top->full_name, folders->pdata[i]);
		}

		g_ptr_array_free(folders, TRUE);

	}
	UNLOCK(info_lock);
}

struct _update_data {
	struct _update_data *next;
	struct _update_data *prev;

	int id;			/* id for cancellation */

	void (*done)(CamelStore *store, CamelFolderInfo *info, void *data);
	void *data;
};

static void
unset_folder_info_hash(char *path, struct _folder_info *mfi, void *data)
{
	unset_folder_info(mfi, FALSE);
}

static void
free_folder_info_hash(char *path, struct _folder_info *mfi, void *data)
{
	free_folder_info(mfi);
}

void
mail_note_store_remove(CamelStore *store)
{
	struct _update_data *ud;
	struct _store_info *si;

	g_assert(CAMEL_IS_STORE(store));

	if (stores == NULL)
		return;

	d(printf("store removed!!\n"));
	LOCK(info_lock);
	si = g_hash_table_lookup(stores, store);
	if (si) {
		g_hash_table_remove(stores, store);

		camel_object_unhook_event((CamelObject *)store, "folder_created", store_folder_created, NULL);
		camel_object_unhook_event((CamelObject *)store, "folder_deleted", store_folder_deleted, NULL);
		camel_object_unhook_event((CamelObject *)store, "folder_renamed", store_folder_renamed, NULL);
		camel_object_unhook_event((CamelObject *)store, "folder_subscribed", store_folder_subscribed, NULL);
		camel_object_unhook_event((CamelObject *)store, "folder_unsubscribed", store_folder_unsubscribed, NULL);
		g_hash_table_foreach(si->folders, (GHFunc)unset_folder_info_hash, NULL);

		ud = (struct _update_data *)si->folderinfo_updates.head;
		while (ud->next) {
			d(printf("Cancelling outstanding folderinfo update %d\n", ud->id));
			mail_msg_cancel(ud->id);
			ud = ud->next;
		}

		/* This is the only gtk object we need to unref */
		mail_async_event_emit(mail_async_event, MAIL_ASYNC_GUI, (MailAsyncFunc)bonobo_object_unref, si->storage, 0, 0);

		camel_object_unref((CamelObject *)si->store);
		g_hash_table_foreach(si->folders, (GHFunc)free_folder_info_hash, NULL);
		g_hash_table_destroy(si->folders);
		g_hash_table_destroy(si->folders_uri);
		g_free(si);
	}
	UNLOCK(info_lock);
}

static void
update_folders(CamelStore *store, CamelFolderInfo *fi, void *data)
{
	struct _update_data *ud = data;
	struct _store_info *si;

	d(printf("Got folderinfo for store\n"));

	LOCK(info_lock);
	si = g_hash_table_lookup(stores, store);
	if (si) {
		/* the 'si' is still there, so we can remove ourselves from its list */
		/* otherwise its not, and we're on our own and free anyway */
		e_dlist_remove((EDListNode *)ud);

		if (fi) {
			if (si->storage)
				gtk_object_set_data (GTK_OBJECT (si->storage), "connected", GINT_TO_POINTER (TRUE));
			create_folders(fi, si);
		}
	}
	UNLOCK(info_lock);

	if (ud->done)
		ud->done(store, fi, ud->data);
	g_free(ud);
}

void
mail_note_store(CamelStore *store, EvolutionStorage *storage, GNOME_Evolution_Storage corba_storage,
		void (*done)(CamelStore *store, CamelFolderInfo *info, void *data), void *data)
{
	struct _store_info *si;
	struct _update_data *ud;

	g_assert(CAMEL_IS_STORE(store));
	g_assert(pthread_self() == mail_gui_thread);
	g_assert(storage != NULL || corba_storage != CORBA_OBJECT_NIL);

	LOCK(info_lock);

	if (stores == NULL) {
		stores = g_hash_table_new(NULL, NULL);
		count_sent = getenv("EVOLUTION_COUNT_SENT") != NULL;
		count_trash = getenv("EVOLUTION_COUNT_TRASH") != NULL;
	}

	si = g_hash_table_lookup(stores, store);
	if (si == NULL) {

		d(printf("Noting a new store: %p: %s\n", store, camel_url_to_string(((CamelService *)store)->url, 0)));

		/* FIXME: Need to ref the storages & store or something?? */

		si = g_malloc0(sizeof(*si));
		si->folders = g_hash_table_new(g_str_hash, g_str_equal);
		si->folders_uri = g_hash_table_new(CAMEL_STORE_CLASS(CAMEL_OBJECT_GET_CLASS(store))->hash_folder_name,
						   CAMEL_STORE_CLASS(CAMEL_OBJECT_GET_CLASS(store))->compare_folder_name);
		si->storage = storage;
		if (storage != NULL)
			bonobo_object_ref((BonoboObject *)storage);
		si->corba_storage = corba_storage;
		si->store = store;
		camel_object_ref((CamelObject *)store);
		g_hash_table_insert(stores, store, si);
		e_dlist_init(&si->folderinfo_updates);

		camel_object_hook_event((CamelObject *)store, "folder_created", store_folder_created, NULL);
		camel_object_hook_event((CamelObject *)store, "folder_deleted", store_folder_deleted, NULL);
		camel_object_hook_event((CamelObject *)store, "folder_renamed", store_folder_renamed, NULL);
		camel_object_hook_event((CamelObject *)store, "folder_subscribed", store_folder_subscribed, NULL);
		camel_object_hook_event((CamelObject *)store, "folder_unsubscribed", store_folder_unsubscribed, NULL);
	}

	ud = g_malloc(sizeof(*ud));
	ud->done = done;
	ud->data = data;
	ud->id = mail_get_folderinfo(store, update_folders, ud);

	e_dlist_addtail(&si->folderinfo_updates, (EDListNode *)ud);

	UNLOCK(info_lock);
}

struct _find_info {
	const char *uri;
	struct _folder_info *fi;
};

/* look up on each storeinfo using proper hash function for that stores uri's */
static void storeinfo_find_folder_info(CamelStore *store, struct _store_info *si, struct _find_info *fi)
{
	if (fi->fi == NULL)
		fi->fi = g_hash_table_lookup(si->folders_uri, fi->uri);
}

/* returns TRUE if the uri is available, folderp is set to a
   reffed folder if the folder has also already been opened */
int mail_note_get_folder_from_uri(const char *uri, CamelFolder **folderp)
{
	struct _find_info fi = { uri, NULL };

	if (stores == NULL)
		return FALSE;

	LOCK(info_lock);
	g_hash_table_foreach(stores, (GHFunc)storeinfo_find_folder_info, &fi);
	if (folderp) {
		if (fi.fi && fi.fi->folder) {
			*folderp = fi.fi->folder;
			camel_object_ref((CamelObject *)*folderp);
		} else {
			*folderp = NULL;
		}
	}
	UNLOCK(info_lock);

	return fi.fi != NULL;
}
