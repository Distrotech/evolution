/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Michael Zucchi <notzed@ximian.com>
 *
 *  Copyright 2000-2003 Ximian, Inc. (www.ximian.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Street #330, Boston, MA 02111-1307, USA.
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib.h>
#include <string.h>
#include <libgnome/gnome-i18n.h>

#include "Evolution.h"
#include "evolution-storage.h"

#include "evolution-shell-component.h"
#include "folder-browser.h"
#include "mail-vfolder.h"
#include "mail-tools.h"
#include "mail-autofilter.h"
#include "mail-folder-cache.h"
#include "mail.h"
#include "mail-ops.h"
#include "mail-mt.h"

#include "e-util/e-dialog-utils.h"

#include "camel/camel.h"
#include "camel/camel-vee-folder.h"
#include "camel/camel-vee-store.h"

#include "filter/vfolder-context.h"
#include "filter/vfolder-editor.h"

#define d(x) /*(printf("%s(%d):%s: ",  __FILE__, __LINE__, __PRETTY_FUNCTION__), (x))*/

static VfolderContext *context;	/* context remains open all time */
static CamelStore *vfolder_store; /* the 1 static vfolder store */

/* lock for accessing shared resources (below) */
static pthread_mutex_t vfolder_lock = PTHREAD_MUTEX_INITIALIZER;

static GList *source_folders_remote;	/* list of source folder uri's - remote ones */
static GList *source_folders_local;	/* list of source folder uri's - local ones */
static GHashTable *vfolder_hash;

extern EvolutionShellClient *global_shell_client;

/* more globals ... */
extern char *evolution_dir;
extern CamelSession *session;

static void rule_changed(FilterRule *rule, CamelFolder *folder);

#define LOCK() pthread_mutex_lock(&vfolder_lock);
#define UNLOCK() pthread_mutex_unlock(&vfolder_lock);

/* ********************************************************************** */

struct _setup_msg {
	struct _mail_msg msg;

	CamelFolder *folder;
	char *query;
	GList *sources_uri;
	GList *sources_folder;
};

static char *
vfolder_setup_desc(struct _mail_msg *mm, int done)
{
	struct _setup_msg *m = (struct _setup_msg *)mm;

	return g_strdup_printf(_("Setting up vfolder: %s"), m->folder->full_name);
}

static void
vfolder_setup_do(struct _mail_msg *mm)
{
	struct _setup_msg *m = (struct _setup_msg *)mm;
	GList *l, *list = NULL;
	CamelFolder *folder;

	d(printf("Setting up vfolder: %s\n", m->folder->full_name));

	camel_vee_folder_set_expression((CamelVeeFolder *)m->folder, m->query);

	l = m->sources_uri;
	while (l) {
		d(printf(" Adding uri: %s\n", (char *)l->data));
		folder = mail_tool_uri_to_folder (l->data, 0, &mm->ex);
		if (folder) {
			list = g_list_append(list, folder);
		} else {
			g_warning("Could not open vfolder source: %s", (char *)l->data);
			camel_exception_clear(&mm->ex);
		}
		l = l->next;
	}

	l = m->sources_folder;
	while (l) {
		d(printf(" Adding folder: %s\n", ((CamelFolder *)l->data)->full_name));
		camel_object_ref(l->data);
		list = g_list_append(list, l->data);
		l = l->next;
	}

	camel_vee_folder_set_folders((CamelVeeFolder *)m->folder, list);

	l = list;
	while (l) {
		camel_object_unref(l->data);
		l = l->next;
	}
	g_list_free(list);
}

static void
vfolder_setup_done(struct _mail_msg *mm)
{
	struct _setup_msg *m = (struct _setup_msg *)mm;

	m = m;
}

static void
vfolder_setup_free (struct _mail_msg *mm)
{
	struct _setup_msg *m = (struct _setup_msg *)mm;
	GList *l;

	camel_object_unref(m->folder);
	g_free(m->query);

	l = m->sources_uri;
	while (l) {
		g_free(l->data);
		l = l->next;
	}
	g_list_free(m->sources_uri);

	l = m->sources_folder;
	while (l) {
		camel_object_unref(l->data);
		l = l->next;
	}
	g_list_free(m->sources_folder);
}

static struct _mail_msg_op vfolder_setup_op = {
	vfolder_setup_desc,
	vfolder_setup_do,
	vfolder_setup_done,
	vfolder_setup_free,
};

static int
vfolder_setup(CamelFolder *folder, const char *query, GList *sources_uri, GList *sources_folder)
{
	struct _setup_msg *m;
	int id;
	
	m = mail_msg_new(&vfolder_setup_op, NULL, sizeof (*m));
	m->folder = folder;
	camel_object_ref(folder);
	m->query = g_strdup(query);
	m->sources_uri = sources_uri;
	m->sources_folder = sources_folder;
	
	id = m->msg.seq;
	e_thread_put(mail_thread_queued_slow, (EMsg *)m);

	return id;
}

/* ********************************************************************** */

struct _adduri_msg {
	struct _mail_msg msg;

	char *uri;
	GList *folders;
	int remove;
};

static char *
vfolder_adduri_desc(struct _mail_msg *mm, int done)
{
	struct _adduri_msg *m = (struct _adduri_msg *)mm;

	return g_strdup_printf(_("Updating vfolders for uri: %s"), m->uri);
}

static void
vfolder_adduri_do(struct _mail_msg *mm)
{
	struct _adduri_msg *m = (struct _adduri_msg *)mm;
	GList *l;
	CamelFolder *folder = NULL;

	d(printf("%s uri to vfolder: %s\n", m->remove?"Removing":"Adding", m->uri));

	/* we dont try lookup the cache if we are removing it, its no longer there */
	if (!m->remove && !mail_note_get_folder_from_uri(m->uri, &folder)) {
		g_warning("Folder '%s' disappeared while I was adding/remove it to/from my vfolder", m->uri);
		return;
	}

	if (folder == NULL)
		folder = mail_tool_uri_to_folder (m->uri, 0, &mm->ex);

	if (folder != NULL) {
		l = m->folders;
		while (l) {
			if (m->remove)
				camel_vee_folder_remove_folder((CamelVeeFolder *)l->data, folder);
			else
				camel_vee_folder_add_folder((CamelVeeFolder *)l->data, folder);
			l = l->next;
		}
		camel_object_unref(folder);
	}
}

static void
vfolder_adduri_done(struct _mail_msg *mm)
{
	struct _adduri_msg *m = (struct _adduri_msg *)mm;

	m = m;
}

static void
vfolder_adduri_free (struct _mail_msg *mm)
{
	struct _adduri_msg *m = (struct _adduri_msg *)mm;

	g_list_foreach(m->folders, (GFunc)camel_object_unref, NULL);
	g_list_free(m->folders);
	g_free(m->uri);
}

static struct _mail_msg_op vfolder_adduri_op = {
	vfolder_adduri_desc,
	vfolder_adduri_do,
	vfolder_adduri_done,
	vfolder_adduri_free,
};

static int
vfolder_adduri(const char *uri, GList *folders, int remove)
{
	struct _adduri_msg *m;
	int id;
	
	m = mail_msg_new(&vfolder_adduri_op, NULL, sizeof (*m));
	m->folders = folders;
	m->uri = g_strdup(uri);
	m->remove = remove;
	
	id = m->msg.seq;
	e_thread_put(mail_thread_queued_slow, (EMsg *)m);

	return id;
}

/* ********************************************************************** */

/* So, uh, apparently g_list_find_custom expect the compare func to return 0 to mean true? */
static GList *
my_list_find(GList *l, const char *uri, GCompareFunc cmp)
{
	while (l) {
		if (cmp(l->data, uri))
			break;
		l = l->next;
	}
	return l;
}

static int
uri_is_ignore(const char *uri, GCompareFunc uri_cmp)
{
	extern char *default_outbox_folder_uri, *default_sent_folder_uri, *default_drafts_folder_uri;
	EAccountList *accounts;
	EAccount *account;
	EIterator *iter;
	int found = FALSE;
	
	d(printf("checking '%s' against:\n  %s\n  %s\n  %s\n", uri, default_outbox_folder_uri, default_sent_folder_uri, default_drafts_folder_uri));
	
	found = (default_outbox_folder_uri && uri_cmp(default_outbox_folder_uri, uri))
		|| (default_sent_folder_uri && uri_cmp(default_sent_folder_uri, uri))
		|| (default_drafts_folder_uri && uri_cmp(default_drafts_folder_uri, uri));
	
	accounts = mail_config_get_accounts ();
	iter = e_list_get_iterator ((EList *) accounts);
	while (e_iterator_is_valid (iter)) {
		account = (EAccount *) e_iterator_get (iter);
		
		d(printf("checking sent_folder_uri '%s' == '%s'\n",
			 account->sent_folder_uri ? account->sent_folder_uri : "empty", uri));
		
		found = (account->sent_folder_uri && uri_cmp (account->sent_folder_uri, uri))
			|| (account->drafts_folder_uri && uri_cmp (account->drafts_folder_uri, uri));
		
		if (found)
			break;
		
		e_iterator_next (iter);
	}
	
	g_object_unref (iter);
	
	return found;
}

/* called when a new uri becomes (un)available */
void
mail_vfolder_add_uri(CamelStore *store, const char *uri, int remove)
{
	FilterRule *rule;
	const char *source;
	CamelVeeFolder *vf;
	GList *folders = NULL, *link;
	int remote = (((CamelService *)store)->provider->flags & CAMEL_PROVIDER_IS_REMOTE) != 0;
	GCompareFunc uri_cmp = CAMEL_STORE_CLASS(CAMEL_OBJECT_GET_CLASS(store))->compare_folder_name;
	int is_ignore;

	if (CAMEL_IS_VEE_STORE(store) || !strncmp(uri, "vtrash:", 7) || context == NULL)
		return;

	g_assert(pthread_self() == mail_gui_thread);

	is_ignore = uri_is_ignore(uri, uri_cmp);

	LOCK();

	d(printf("%s uri to check: %s\n", remove?"Removing":"Adding", uri));

	/* maintain the source folders lists for changed rules later on */
	if (remove) {
		if (remote) {
			if ((link = my_list_find(source_folders_remote, (void *)uri, uri_cmp)) != NULL) {
				g_free(link->data);
				source_folders_remote = g_list_remove_link(source_folders_remote, link);
			}
		} else {
			if ((link = my_list_find(source_folders_local, (void *)uri, uri_cmp)) != NULL) {
				g_free(link->data);
				source_folders_local = g_list_remove_link(source_folders_local, link);
			}
		}
	} else if (!is_ignore) {
		/* we ignore drafts/sent/outbox here */
		if (remote) {
			if (my_list_find(source_folders_remote, (void *)uri, uri_cmp) == NULL)
				source_folders_remote = g_list_prepend(source_folders_remote, g_strdup(uri));
		} else {
			if (my_list_find(source_folders_local, (void *)uri, uri_cmp) == NULL)
				source_folders_local = g_list_prepend(source_folders_local, g_strdup(uri));
		}
	}

 	rule = NULL;
	while ((rule = rule_context_next_rule((RuleContext *)context, rule, NULL))) {
		int found = FALSE;
		
		if (!rule->name) {
			d(printf("invalid rule (%p): rule->name is set to NULL\n", rule));
			continue;
		}

		/* dont auto-add any sent/drafts folders etc, they must be explictly listed as a source */
		if (rule->source
		    && !is_ignore
		    && ((!strcmp(rule->source, "local") && !remote)
			|| (!strcmp(rule->source, "remote_active") && remote)
			|| (!strcmp(rule->source, "local_remote_active"))))
			found = TRUE;
		
		/* we check using the store uri_cmp since its more accurate */
		source = NULL;
		while (!found && (source = vfolder_rule_next_source((VfolderRule *)rule, source)))
			found = uri_cmp(uri, source);
		
		if (found) {
			vf = g_hash_table_lookup(vfolder_hash, rule->name);
			g_assert(vf);
			camel_object_ref(vf);
			folders = g_list_prepend(folders, vf);
		}
	}
	
	UNLOCK();
	
	if (folders != NULL)
		vfolder_adduri(uri, folders, remove);
}

/* called when a uri is deleted from a store */
void
mail_vfolder_delete_uri(CamelStore *store, const char *uri)
{
	GCompareFunc uri_cmp = CAMEL_STORE_CLASS(CAMEL_OBJECT_GET_CLASS(store))->compare_folder_name;
	FilterRule *rule;
	const char *source;
	CamelVeeFolder *vf;
	GString *changed;

	if (context == NULL || !strncmp(uri, "vtrash:", 7))
		return;
	
	d(printf ("Deleting uri to check: %s\n", uri));
	
	g_assert (pthread_self() == mail_gui_thread);
	
	changed = g_string_new ("");
	
	LOCK();
	
	/* see if any rules directly reference this removed uri */
 	rule = NULL;
	while ((rule = rule_context_next_rule ((RuleContext *) context, rule, NULL))) {
		source = NULL;
		while ((source = vfolder_rule_next_source ((VfolderRule *) rule, source))) {
			/* Remove all sources that match, ignore changed events though
			   because the adduri call above does the work async */
			if (uri_cmp (uri, source)) {
				vf = g_hash_table_lookup (vfolder_hash, rule->name);
				g_assert (vf != NULL);
				g_signal_handlers_disconnect_matched (rule, G_SIGNAL_MATCH_FUNC|G_SIGNAL_MATCH_DATA, 0,
								      0, NULL, rule_changed, vf);
				vfolder_rule_remove_source ((VfolderRule *)rule, source);
				g_signal_connect (rule, "changed", G_CALLBACK(rule_changed), vf);
				g_string_append_printf (changed, "    %s\n", rule->name);
				source = NULL;
			}
		}
	}
	
	UNLOCK();
	
	if (changed->str[0]) {
		GtkWidget *dialog;
		char *user;
		
		dialog = gtk_message_dialog_new (NULL, 0, GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
						 _("The following vFolder(s):\n%s"
						   "Used the removed folder:\n    '%s'\n"
						   "And have been updated."),
						 changed->str, uri);
		g_signal_connect_swapped (dialog, "response", G_CALLBACK (gtk_widget_destroy), dialog);
		gtk_widget_show (dialog);
		
		user = g_strdup_printf ("%s/vfolders.xml", evolution_dir);
		rule_context_save ((RuleContext *) context, user);
		g_free (user);
	}
	
	g_string_free (changed, TRUE);
}

/* called when a uri is renamed in a store */
void
mail_vfolder_rename_uri(CamelStore *store, const char *from, const char *to)
{
	GCompareFunc uri_cmp = CAMEL_STORE_CLASS(CAMEL_OBJECT_GET_CLASS(store))->compare_folder_name;
	FilterRule *rule;
	const char *source;
	CamelVeeFolder *vf;
	int changed = 0;

	d(printf("vfolder rename uri: %s to %s\n", from, to));

	if (context == NULL || !strncmp(from, "vtrash:", 7) || !strncmp(to, "vtrash:", 7))
		return;

	g_assert(pthread_self() == mail_gui_thread);

	LOCK();

	/* see if any rules directly reference this removed uri */
 	rule = NULL;
	while ( (rule = rule_context_next_rule((RuleContext *)context, rule, NULL)) ) {
		source = NULL;
		while ( (source = vfolder_rule_next_source((VfolderRule *)rule, source)) ) {
			/* Remove all sources that match, ignore changed events though
			   because the adduri call above does the work async */
			if (uri_cmp(from, source)) {
				d(printf("Vfolder '%s' used '%s' ('%s') now uses '%s'\n", rule->name, source, from, to));
				vf = g_hash_table_lookup(vfolder_hash, rule->name);
				g_assert(vf);
				g_signal_handlers_disconnect_matched(rule, G_SIGNAL_MATCH_FUNC|G_SIGNAL_MATCH_DATA, 0,
								     0, NULL, rule_changed, vf);
				vfolder_rule_remove_source((VfolderRule *)rule, source);
				vfolder_rule_add_source((VfolderRule *)rule, to);
				g_signal_connect(rule, "changed", G_CALLBACK(rule_changed), vf);
				changed++;
				source = NULL;
			}
		}
	}

	UNLOCK();

	if (changed) {
		char *user;

		d(printf("Vfolders updated from renamed folder\n"));
		user = g_strdup_printf("%s/vfolders.xml", evolution_dir);
		rule_context_save((RuleContext *)context, user);
		g_free(user);
	}
}

/* ********************************************************************** */

static void context_rule_added(RuleContext *ctx, FilterRule *rule);

static void
rule_changed(FilterRule *rule, CamelFolder *folder)
{
	const char *sourceuri;
	GList *l;
	GList *sources_uri = NULL, *sources_folder = NULL;
	GString *query;
	int i;
	CamelFolder *newfolder;

	/* if the folder has changed name, then add it, then remove the old manually */
	if (strcmp(folder->full_name, rule->name) != 0) {
		char *key, *oldname;
		CamelFolder *old;

		LOCK();
		d(printf("Changing folder name in hash table to '%s'\n", rule->name));
		if (g_hash_table_lookup_extended(vfolder_hash, folder->full_name, (void **)&key, (void **)&old)) {
			g_hash_table_remove(vfolder_hash, key);
			g_free(key);
			g_hash_table_insert(vfolder_hash, g_strdup(rule->name), folder);
			UNLOCK();
		} else {
			UNLOCK();
			g_warning("couldn't find a vfolder rule in our table? %s", folder->full_name);
		}

		/* TODO: make the folder->full_name var thread accessible */
		oldname = g_strdup(folder->full_name);
		camel_store_rename_folder(vfolder_store, oldname, rule->name, NULL);
		g_free(oldname);
	}

	d(printf("Filter rule changed? for folder '%s'!!\n", folder->name));

	/* find any (currently available) folders, and add them to the ones to open */
	sourceuri = NULL;
	while ( (sourceuri = vfolder_rule_next_source((VfolderRule *)rule, sourceuri)) ) {
		if (mail_note_get_folder_from_uri(sourceuri, &newfolder)) {
			if (newfolder)
				sources_folder = g_list_append(sources_folder, newfolder);
			else
				sources_uri = g_list_append(sources_uri, g_strdup(sourceuri));
		}
	}

	/* check the remote/local uri lists for any other uri's that should be looked at */
	if (rule->source) {
		LOCK();
		for (i=0;i<2;i++) {
			if (i==0 && (!strcmp(rule->source, "local") || !strcmp(rule->source, "local_remote_active")))
				l = source_folders_local;
			else if (i==1 && (!strcmp(rule->source, "remote_active") || !strcmp(rule->source, "local_remote_active")))
				l = source_folders_remote;
			else
				l = NULL;

			while (l) {
				if (mail_note_get_folder_from_uri(l->data, &newfolder)) {
					if (newfolder)
						sources_folder = g_list_append(sources_folder, newfolder);
					else
						sources_uri = g_list_append(sources_uri, g_strdup(l->data));
				} else {
					d(printf("  -> No such folder?\n"));
				}
				l = l->next;
			}
		}
		UNLOCK();
	}

	query = g_string_new("");
	filter_rule_build_code(rule, query);

	vfolder_setup(folder, query->str, sources_uri, sources_folder);

	g_string_free(query, TRUE);
}

static void context_rule_added(RuleContext *ctx, FilterRule *rule)
{
	CamelFolder *folder;

	d(printf("rule added: %s\n", rule->name));

	/* this always runs quickly */
	folder = camel_store_get_folder(vfolder_store, rule->name, 0, NULL);
	if (folder) {
		g_signal_connect(rule, "changed", G_CALLBACK(rule_changed), folder);

		LOCK();
		g_hash_table_insert(vfolder_hash, g_strdup(rule->name), folder);
		UNLOCK();

		mail_note_folder(folder);
		rule_changed(rule, folder);
	}
}

static void context_rule_removed(RuleContext *ctx, FilterRule *rule)
{
	char *key, *path;
	CamelFolder *folder = NULL;

	d(printf("rule removed; %s\n", rule->name));

	/* TODO: remove from folder info cache? */

	path = g_strdup_printf("/%s", rule->name);
	evolution_storage_removed_folder(mail_lookup_storage(vfolder_store), path);
	g_free(path);

	LOCK();
	if (g_hash_table_lookup_extended(vfolder_hash, rule->name, (void **)&key, (void **)&folder)) {
		g_hash_table_remove(vfolder_hash, key);
		g_free(key);
	}
	UNLOCK();

	camel_store_delete_folder(vfolder_store, rule->name, NULL);
	/* this must be unref'd after its deleted */
	if (folder)
		camel_object_unref(folder);
}

static void
store_folder_created(CamelObject *o, void *event_data, void *data)
{
	CamelStore *store = (CamelStore *)o;
	CamelFolderInfo *info = event_data;

	store = store;
	info = info;
}

static void
store_folder_deleted(CamelObject *o, void *event_data, void *data)
{
	CamelStore *store = (CamelStore *)o;
	CamelFolderInfo *info = event_data;
	FilterRule *rule;
	char *user;

	d(printf("Folder deleted: %s\n", info->name));
	store = store;

	/* Warning not thread safe, but might be enough */

	LOCK();

	/* delete it from our list */
	rule = rule_context_find_rule((RuleContext *)context, info->full_name, NULL);
	if (rule) {
		/* We need to stop listening to removed events, otherwise we'll try and remove it again */
		g_signal_handlers_disconnect_matched(context, G_SIGNAL_MATCH_FUNC|G_SIGNAL_MATCH_DATA, 0,
						     0, NULL, context_rule_removed, context);
		rule_context_remove_rule((RuleContext *)context, rule);
		g_object_unref(rule);
		g_signal_connect(context, "rule_removed", G_CALLBACK(context_rule_removed), context);

		user = g_strdup_printf("%s/vfolders.xml", evolution_dir);
		rule_context_save((RuleContext *)context, user);
		g_free(user);
	} else {
		g_warning("Cannot find rule for deleted vfolder '%s'", info->name);
	}

	UNLOCK();
}

static void
store_folder_renamed(CamelObject *o, void *event_data, void *data)
{
	CamelStore *store = (CamelStore *)o;
	CamelRenameInfo *info = event_data;
	FilterRule *rule;
	char *user;
	char *key;
	CamelFolder *folder;

	store = store;

	/* This should be more-or-less thread-safe */

	d(printf("Folder renamed to '%s' from '%s'\n", info->new->full_name, info->old_base));

	/* Folder is already renamed? */
	LOCK();
	d(printf("Changing folder name in hash table to '%s'\n", info->new->full_name));
	if (g_hash_table_lookup_extended(vfolder_hash, info->old_base, (void **)&key, (void **)&folder)) {
		g_hash_table_remove(vfolder_hash, key);
		g_free(key);
		g_hash_table_insert(vfolder_hash, g_strdup(info->new->full_name), folder);

		rule = rule_context_find_rule((RuleContext *)context, info->old_base, NULL);
		g_assert(rule);
		g_signal_handlers_disconnect_matched(rule, G_SIGNAL_MATCH_FUNC|G_SIGNAL_MATCH_DATA, 0,
						     0, NULL, rule_changed, folder);
		filter_rule_set_name(rule, info->new->full_name);
		g_signal_connect(rule, "changed", G_CALLBACK(rule_changed), folder);

		user = g_strdup_printf("%s/vfolders.xml", evolution_dir);
		rule_context_save((RuleContext *)context, user);
		g_free(user);

		UNLOCK();
	} else {
		UNLOCK();
		g_warning("couldn't find a vfolder rule in our table? %s", info->new->full_name);
	}
}

void
vfolder_load_storage(GNOME_Evolution_Shell shell)
{
	char *user, *storeuri;
	FilterRule *rule;

	vfolder_hash = g_hash_table_new(g_str_hash, g_str_equal);

	/* first, create the vfolder store, and set it up */
	storeuri = g_strdup_printf("vfolder:%s/vfolder", evolution_dir);
	vfolder_store = camel_session_get_store(session, storeuri, NULL);
	if (vfolder_store == NULL) {
		g_warning("Cannot open vfolder store - no vfolders available");
		return;
	}

	camel_object_hook_event(vfolder_store, "folder_created",
				(CamelObjectEventHookFunc)store_folder_created, NULL);
	camel_object_hook_event(vfolder_store, "folder_deleted",
				(CamelObjectEventHookFunc)store_folder_deleted, NULL);
	camel_object_hook_event(vfolder_store, "folder_renamed",
				(CamelObjectEventHookFunc)store_folder_renamed, NULL);

	d(printf("got store '%s' = %p\n", storeuri, vfolder_store));
	mail_load_storage_by_uri(shell, storeuri, _("VFolders"));

	/* load our rules */
	user = g_strdup_printf ("%s/vfolders.xml", evolution_dir);
	context = vfolder_context_new ();
	if (rule_context_load ((RuleContext *)context,
			       EVOLUTION_PRIVDATADIR "/vfoldertypes.xml", user) != 0) {
		g_warning("cannot load vfolders: %s\n", ((RuleContext *)context)->error);
	}
	g_free (user);
	
	g_signal_connect(context, "rule_added", G_CALLBACK(context_rule_added), context);
	g_signal_connect(context, "rule_removed", G_CALLBACK(context_rule_removed), context);

	/* and setup the rules we have */
	rule = NULL;
	while ( (rule = rule_context_next_rule((RuleContext *)context, rule, NULL)) ) {
		if (rule->name)
			context_rule_added((RuleContext *)context, rule);
		else
			d(printf("invalid rule (%p) encountered: rule->name is NULL\n", rule));
	}

	g_free(storeuri);
}

static GtkWidget *vfolder_editor = NULL;

static void
vfolder_editor_response (GtkWidget *dialog, int button, void *data)
{
	char *user;

	user = alloca(strlen(evolution_dir)+16);
	sprintf(user, "%s/vfolders.xml", evolution_dir);

	switch(button) {
	case GTK_RESPONSE_ACCEPT:
		rule_context_save((RuleContext *)context, user);
		break;
	default:
		rule_context_revert((RuleContext *)context, user);
	}

	vfolder_editor = NULL;

	gtk_widget_destroy(dialog);
}

void
vfolder_edit (void)
{
	if (vfolder_editor) {
		gdk_window_raise (GTK_WIDGET (vfolder_editor)->window);
		return;
	}
	
	vfolder_editor = GTK_WIDGET (vfolder_editor_new (context));
	gtk_window_set_title (GTK_WINDOW (vfolder_editor), _("vFolders"));
	g_signal_connect(vfolder_editor, "response", G_CALLBACK(vfolder_editor_response), NULL);
	
	gtk_widget_show (vfolder_editor);
}

static void
edit_rule_response(GtkWidget *w, int button, void *data)
{
	if (button == GTK_RESPONSE_OK) {
		char *user;
		FilterRule *rule = g_object_get_data (G_OBJECT (w), "rule");
		FilterRule *orig = g_object_get_data (G_OBJECT (w), "orig");

		filter_rule_copy(orig, rule);
		user = g_strdup_printf("%s/vfolders.xml", evolution_dir);
		rule_context_save((RuleContext *)context, user);
		g_free(user);
	}

	gtk_widget_destroy(w);
}

void
vfolder_edit_rule(const char *uri)
{
	GtkWidget *w;
	GtkDialog *gd;
	FilterRule *rule, *newrule;
	CamelURL *url;

	url = camel_url_new(uri, NULL);
	if (url && url->fragment
	    && (rule = rule_context_find_rule((RuleContext *)context, url->fragment, NULL))) {
		g_object_ref((GtkObject *)rule);
		newrule = filter_rule_clone(rule);

		w = filter_rule_get_widget((FilterRule *)newrule, (RuleContext *)context);

		gd = (GtkDialog *)gtk_dialog_new_with_buttons(_("Edit VFolder"), NULL,
							      GTK_DIALOG_DESTROY_WITH_PARENT,
							      GTK_STOCK_CANCEL,
							      GTK_RESPONSE_CANCEL,
							      GTK_STOCK_OK,
							      GTK_RESPONSE_OK,
							      NULL);
		gtk_container_set_border_width ((GtkWindow *) gd, 6);
		gtk_box_set_spacing ((GtkBox *) gd->vbox, 6);
		gtk_dialog_set_default_response(gd, GTK_RESPONSE_OK);
		g_object_set(gd, "allow_shrink", FALSE, "allow_grow", TRUE, NULL);
		gtk_window_set_default_size (GTK_WINDOW (gd), 500, 500);
		gtk_box_pack_start((GtkBox *)gd->vbox, w, TRUE, TRUE, 0);
		gtk_widget_show((GtkWidget *)gd);
		g_object_set_data_full(G_OBJECT(gd), "rule", newrule, (GtkDestroyNotify)g_object_unref);
		g_object_set_data_full(G_OBJECT(gd), "orig", rule, (GtkDestroyNotify)g_object_unref);
		g_signal_connect(gd, "response", G_CALLBACK(edit_rule_response), NULL);
		gtk_widget_show((GtkWidget *)gd);
	} else {
		e_notice (NULL, GTK_MESSAGE_WARNING,
			  _("Trying to edit a vfolder '%s' which doesn't exist."), uri);
	}

	if (url)
		camel_url_free(url);
}

static void
new_rule_clicked(GtkWidget *w, int button, void *data)
{
	if (button == GTK_RESPONSE_OK) {
		char *user;
		FilterRule *rule = g_object_get_data((GObject *)w, "rule");

		if (!filter_rule_validate(rule)) {
			/* no need to popup a dialog because the validate code does that. */
			return;
		}

		if (rule_context_find_rule ((RuleContext *)context, rule->name, rule->source)) {
			GtkWidget *dialog =
				gtk_message_dialog_new ((GtkWindow *) w, GTK_DIALOG_DESTROY_WITH_PARENT,
							GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
							_("Rule name '%s' is not unique, choose another."),
							rule->name);

			gtk_dialog_run ((GtkDialog *) dialog);
			gtk_widget_destroy (dialog);

			return;
		}


		g_object_ref(rule);
		rule_context_add_rule((RuleContext *)context, rule);
		user = g_strdup_printf("%s/vfolders.xml", evolution_dir);
		rule_context_save((RuleContext *)context, user);
		g_free(user);
	}

	gtk_widget_destroy(w);
}

FilterPart *
vfolder_create_part(const char *name)
{
	return rule_context_create_part((RuleContext *)context, name);
}

/* clones a filter/search rule into a matching vfolder rule (assuming the same system definitions) */
FilterRule *
vfolder_clone_rule(FilterRule *in)
{
	FilterRule *rule = (FilterRule *)vfolder_rule_new();
	xmlNodePtr xml;

	xml = filter_rule_xml_encode(in);
	filter_rule_xml_decode(rule, xml, (RuleContext *)context);
	xmlFreeNodeList(xml);

	return rule;
}

/* adds a rule with a gui */
void
vfolder_gui_add_rule(VfolderRule *rule)
{
	GtkWidget *w;
	GtkDialog *gd;

	w = filter_rule_get_widget((FilterRule *)rule, (RuleContext *)context);

	gd = (GtkDialog *)gtk_dialog_new_with_buttons(_("New VFolder"),
						      NULL,
						      GTK_DIALOG_DESTROY_WITH_PARENT,
						      GTK_STOCK_CANCEL,
						      GTK_RESPONSE_CANCEL,
						      GTK_STOCK_OK,
						      GTK_RESPONSE_OK,
						      NULL);
	gtk_dialog_set_default_response(gd, GTK_RESPONSE_OK);
	gtk_container_set_border_width ((GtkWindow *) gd, 6);
	gtk_box_set_spacing ((GtkBox *) gd->vbox, 6);
	g_object_set(gd, "allow_shrink", FALSE, "allow_grow", TRUE, NULL);
	gtk_window_set_default_size (GTK_WINDOW (gd), 500, 500);
	gtk_box_pack_start((GtkBox *)gd->vbox, w, TRUE, TRUE, 0);
	gtk_widget_show((GtkWidget *)gd);
	g_object_set_data_full(G_OBJECT(gd), "rule", rule, (GtkDestroyNotify)g_object_unref);
	g_signal_connect(gd, "response", G_CALLBACK(new_rule_clicked), NULL);
	gtk_widget_show((GtkWidget *)gd);
}

void
vfolder_gui_add_from_message(CamelMimeMessage *msg, int flags, const char *source)
{
	VfolderRule *rule;

	g_return_if_fail (msg != NULL);

	rule = (VfolderRule*)vfolder_rule_from_message(context, msg, flags, source);
	vfolder_gui_add_rule(rule);
}

static void
vfolder_foreach_cb (gpointer key, gpointer data, gpointer user_data)
{
	CamelFolder *folder = CAMEL_FOLDER (data);
	
	if (folder)
		camel_object_unref(folder);
	
	g_free (key);
}

void
mail_vfolder_shutdown (void)
{
	g_hash_table_foreach (vfolder_hash, vfolder_foreach_cb, NULL);
	g_hash_table_destroy (vfolder_hash);
	vfolder_hash = NULL;

	if (vfolder_store) {
		camel_object_unref (vfolder_store);
		vfolder_store = NULL;
	}
	
	if (context) {
		g_object_unref(context);
		context = NULL;
	}
}
