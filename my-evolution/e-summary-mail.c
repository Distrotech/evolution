/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-summary-mail.c
 *
 * Copyright (C) 2001 Ximian, Inc.
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
 * Author: Iain Holmes  <iain@ximian.com>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <liboaf/liboaf.h>
#include <gal/widgets/e-unicode.h>

#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-i18n.h>
#include <libgnome/gnome-util.h> /* gnome_util_prepend_user_home */
#include <gtk/gtksignal.h>
#include <bonobo/bonobo-exception.h>
#include <bonobo/bonobo-listener.h>
#include <bonobo/bonobo-property-bag-client.h>

#include <Evolution.h>
#include <evolution-storage-listener.h>

#include "Mailer.h"
#include "e-summary.h"
#include "e-summary-mail.h"
#include "e-summary-table.h"
#include "e-summary-preferences.h"

#include "e-util/e-path.h"

#define MAIL_IID "OAFIID:GNOME_Evolution_FolderInfo"

typedef struct _FolderStore {
	GNOME_Evolution_Shell shell;
	GNOME_Evolution_FolderInfo folder_info;
	GNOME_Evolution_StorageRegistry registry;
	Bonobo_Listener corba_listener;
	EvolutionStorageListener *storage_listener;
	
	GSList *storage_list;

	GHashTable *path_to_folder;
	GHashTable *physical_uri_to_folder;

	GList *shown;
} FolderStore;

struct _ESummaryMail {
#if 0
	GNOME_Evolution_FolderInfo folder_info;
	GNOME_Evolution_StorageRegistry registry;
	BonoboListener *listener;
	EvolutionStorageListener *storage_listener;

	GSList *storage_list;
	
	GHashTable *folders;
	GList *shown;
	ESummaryMailMode mode;
#endif
	char *html;
};

typedef struct _StorageInfo {
	char *name;
	char *toplevel;
	
	GNOME_Evolution_Storage storage;
	EvolutionStorageListener *listener;
/*  	ESummary *summary; */
	GList *folders;
} StorageInfo;

typedef struct _ESummaryMailFolder {
	char *path;
	char *uri;
	char *physical_uri;

	int count;
	int unread;

	gboolean init; /* Has this folder been initialised? */
	StorageInfo *si;
} ESummaryMailFolder;

static FolderStore *folder_store = NULL;

/* Work out what to do with folder names */
static char *
make_pretty_foldername (ESummary *summary,
			const char *foldername)
{
	char *pretty;

	if (summary->preferences->show_full_path == FALSE) {
		if ((pretty = strrchr (foldername, '/'))) {
			return g_strdup (pretty + 1);
		} else {
			return g_strdup (foldername);
		}
	} else {
		return g_strdup (foldername);
	}
}

static void
folder_gen_html (ESummary *summary,
		 ESummaryMailFolder *folder,
		 GString *string)
{
	char *str, *pretty_name;
	
	pretty_name = make_pretty_foldername (summary, folder->path);
	str = g_strdup_printf ("<tr><td><a href=\"%s\"><pre>%s</pre></a></td><td align=\"Left\"><pre>%d/%d</pre></td></tr>", 
			       folder->uri, pretty_name, folder->unread, folder->count);
	g_string_append (string, str);
	g_free (pretty_name);
	g_free (str);
}

static void
e_summary_mail_generate_html (ESummary *summary)
{
	ESummaryMail *mail;
	GString *string;
	GList *p;
	char *s, *old;

	g_return_if_fail (summary != NULL);
	g_return_if_fail (IS_E_SUMMARY (summary));

	mail = summary->mail;
	string = g_string_new ("<dl><dt><img src=\"myevo-mail-summary.png\" "
	                       "align=\"middle\" alt=\"\" width=\"48\" "
	                       "height=\"48\"> <b><a href=\"evolution:/local/Inbox\">");
	s = e_utf8_from_locale_string (_("Mail summary"));
	g_string_append (string, s);
	g_free (s);
	g_string_append (string, "</a></b></dt><dd><table numcols=\"2\" width=\"100%\">");
	
	for (p = folder_store->shown; p; p = p->next) {
		ESummaryMailFolder *mail_folder = p->data;

		folder_gen_html (summary, mail_folder, string);
	}

	g_string_append (string, "</table></dd></dl>");

	old = mail->html;
	mail->html = string->str;

	g_free (old);

	g_string_free (string, FALSE);
}

const char *
e_summary_mail_get_html (ESummary *summary)
{
	/* Only regenerate HTML when it's needed */
	e_summary_mail_generate_html (summary);

	if (summary->mail == NULL) {
		return NULL;
	}
	
	return summary->mail->html;
}

static void
e_summary_mail_get_info (const char *uri)
{
	CORBA_Environment ev;

	g_return_if_fail (folder_store->folder_info != CORBA_OBJECT_NIL);

	CORBA_exception_init (&ev);
	GNOME_Evolution_FolderInfo_getInfo (folder_store->folder_info, uri ? uri : "",
					    folder_store->corba_listener, &ev);
	if (BONOBO_EX (&ev)) {
		g_warning ("Error getting info for %s:\n%s", uri,
			   CORBA_exception_id (&ev));
		CORBA_exception_free (&ev);
		return;
	}
	
	CORBA_exception_free (&ev);
	return;
}

static gboolean
e_summary_mail_idle_get_info (gpointer user_data)
{
	char *uri = user_data;

	e_summary_mail_get_info (uri);
	g_free (uri);
	return FALSE;
}

static void
new_folder_cb (EvolutionStorageListener *listener,
	       const char *path,
	       const GNOME_Evolution_Folder *folder,
	       StorageInfo *si)
{
	ESummaryPrefs *global_preferences;
	ESummaryMailFolder *mail_folder;
	GList *p;

	if (strcmp (folder->type, "mail") != 0)
		return;

	if (strncmp (folder->evolutionUri, "evolution:", 10) != 0)
		return;

	mail_folder = g_new (ESummaryMailFolder, 1);
	mail_folder->si = si;
	mail_folder->uri = g_strdup (folder->evolutionUri);
	mail_folder->physical_uri = g_strdup (folder->physicalUri);
	mail_folder->path = g_strdup (path);
	mail_folder->count = -1;
	mail_folder->unread = -1;
	mail_folder->init = FALSE;

	g_hash_table_insert (folder_store->path_to_folder, mail_folder->path, mail_folder);
	g_hash_table_insert (folder_store->physical_uri_to_folder, mail_folder->physical_uri, mail_folder);

	si->folders = g_list_prepend (si->folders, mail_folder);

	global_preferences = e_summary_preferences_get_global ();
	for (p = global_preferences->display_folders; p; p = p->next) {
		ESummaryPrefsFolder *f = p->data;
		
		if (strcmp (f->physical_uri, folder->physicalUri) == 0) {
			folder_store->shown = g_list_append (folder_store->shown, mail_folder);
			g_idle_add (e_summary_mail_idle_get_info,
				    g_strdup (mail_folder->physical_uri));
		}
	}
}

static void
update_folder_cb (EvolutionStorageListener *listener,
		  const char *path,
		  int unread_count,
		  StorageInfo *si)
{
	ESummaryMailFolder *mail_folder;
	GList *p;

	mail_folder = g_hash_table_lookup (folder_store->path_to_folder, path);
	if (mail_folder == NULL) {
		return;
	}

	for (p = folder_store->shown; p; p = p->next) {
		if (p->data == mail_folder) {
			g_idle_add (e_summary_mail_idle_get_info,
				    g_strdup (mail_folder->physical_uri));
			return;
		}
	}
}

static void
remove_folder_cb (EvolutionStorageListener *listener,
		  const char *path,
		  StorageInfo *si)
{
	ESummaryMailFolder *mail_folder;
	GList *p;

	mail_folder = g_hash_table_lookup (folder_store->path_to_folder, path);
	if (mail_folder == NULL) {
		return;
	}

	/* Check if we're displaying it, because we can't display it if it
	   doesn't exist :) */
	for (p = folder_store->shown; p; p = p->next) {
		if (p->data == mail_folder) {
			folder_store->shown = g_list_remove_link (folder_store->shown, p);
			g_list_free (p);
		}
	}

	g_hash_table_remove (folder_store->path_to_folder, path);
	g_free (mail_folder->path);
	g_free (mail_folder->uri);
	g_free (mail_folder->physical_uri);
	g_free (mail_folder);
}

static void
mail_change_notify (BonoboListener *listener,
		    const char *name,
		    const BonoboArg *arg,
		    CORBA_Environment *ev,
		    gpointer data)
{
	GNOME_Evolution_FolderInfo_MessageCount *count;
	ESummaryMailFolder *folder;
	ESummaryPrefs *global_preferences;
	GList *p;

	count = arg->_value;
	folder = g_hash_table_lookup (folder_store->physical_uri_to_folder, count->path);

	if (folder == NULL) {
		return;
	}

	folder->count = count->count;
	folder->unread = count->unread;
	folder->init = TRUE;

	/* Are we displaying this folder? */
	global_preferences = e_summary_preferences_get_global ();
	for (p = global_preferences->display_folders; p; p = p->next) {
		ESummaryPrefsFolder *f = p->data;
		if (strcmp (f->physical_uri, folder->physical_uri) == 0) {
			e_summary_redraw_all (); /* All summaries should be redrawn, not just this one */
			return;
		}
	}
}

static void
e_summary_mail_protocol (ESummary *summary,
			 const char *uri,
			 void *closure)
{
}

	
	
static gboolean
e_summary_folder_register_storage (const char *name,
				   GNOME_Evolution_Storage corba_storage)
{
	GNOME_Evolution_StorageListener corba_listener;
	StorageInfo *si;
	CORBA_Environment ev;

	si = g_new (StorageInfo, 1);
	si->name = g_strdup (name);
	si->toplevel = NULL;
	si->storage = corba_storage;
	si->listener = evolution_storage_listener_new ();
	si->folders = NULL;

	folder_store->storage_list = g_slist_prepend (folder_store->storage_list, si);
	
	gtk_signal_connect (GTK_OBJECT (si->listener), "new-folder",
			    GTK_SIGNAL_FUNC (new_folder_cb), si);
	gtk_signal_connect (GTK_OBJECT (si->listener), "removed-folder",
			    GTK_SIGNAL_FUNC (remove_folder_cb), si);
	gtk_signal_connect (GTK_OBJECT (si->listener), "update_folder",
			    GTK_SIGNAL_FUNC (update_folder_cb), si);
	
	corba_listener = evolution_storage_listener_corba_objref (si->listener);

	CORBA_exception_init (&ev);
	GNOME_Evolution_Storage_addListener (corba_storage, corba_listener, &ev);
	if (BONOBO_EX (&ev)) {
		g_warning ("Exception adding listener: %s", 
			   CORBA_exception_id (&ev));
		CORBA_exception_free (&ev);
		
		return FALSE;
	}

	CORBA_exception_free (&ev);

	return TRUE;
}

static void
e_summary_folder_register_local_storage (void)
{
	GNOME_Evolution_Storage local_storage;
	CORBA_Environment ev;

	CORBA_exception_init (&ev);
	local_storage = GNOME_Evolution_Shell_getLocalStorage (folder_store->shell, &ev);
	if (BONOBO_EX (&ev) || local_storage == CORBA_OBJECT_NIL) {
		g_warning ("Error getting local storage: %s", CORBA_exception_id (&ev));
		CORBA_exception_free (&ev);
		return;
	}
	CORBA_exception_free (&ev);

	e_summary_folder_register_storage (_("Local Folders"), local_storage);
}

static void
storage_notify (BonoboListener *listener,
		const char *name,
		const BonoboArg *arg,
		CORBA_Environment *ev,
		gpointer data)
{
	GNOME_Evolution_StorageRegistry_NotifyResult *nr;
	GNOME_Evolution_Storage corba_storage;
	CORBA_Environment ev2;
	
	nr = arg->_value;

	switch (nr->type) {
	case GNOME_Evolution_StorageRegistry_STORAGE_CREATED:
		/* These need to be special cased because they're special */
		if (strcmp (nr->name, "summary") == 0) {
			break;
		}

		if (strcmp (nr->name, "local") == 0) {
			e_summary_folder_register_local_storage ();
			break;
		}
		
		CORBA_exception_init (&ev2);
		corba_storage = GNOME_Evolution_StorageRegistry_getStorageByName (folder_store->registry,
										  nr->name, &ev2);
		if (BONOBO_EX (&ev2) || corba_storage == CORBA_OBJECT_NIL) {
			g_warning ("Error getting storage %s\n%s", nr->name,
				   CORBA_exception_id (&ev2));
			CORBA_exception_free (&ev2);
			return;
		}

		CORBA_exception_free (&ev2);
		e_summary_folder_register_storage (nr->name, corba_storage);
		break;

	case GNOME_Evolution_StorageRegistry_STORAGE_DESTROYED:
		g_print ("%s removed\n", nr->name);
		break;

	default:
		g_print ("Unknown response %d\n", nr->type);
		break;
	}
}

static gboolean
e_summary_folder_register_storages (GNOME_Evolution_Shell corba_shell)
{
	Bonobo_Listener corba_listener;
	BonoboListener *listener;
	CORBA_Environment ev;

	CORBA_exception_init (&ev);

	folder_store->registry = Bonobo_Unknown_queryInterface (corba_shell,
								"IDL:GNOME/Evolution/StorageRegistry:1.0",
								&ev);
	if (BONOBO_EX (&ev) || folder_store->registry == NULL) {
		g_warning ("No storage registry: %s", CORBA_exception_id (&ev));
		CORBA_exception_free (&ev);
		return FALSE;
	}

	listener = bonobo_listener_new (NULL, NULL);
	gtk_signal_connect (GTK_OBJECT (listener), "event-notify",
			    GTK_SIGNAL_FUNC (storage_notify), NULL);
	corba_listener = bonobo_object_corba_objref (BONOBO_OBJECT (listener));

	/* Storages will be added whenever the listener gets an event. */
	GNOME_Evolution_StorageRegistry_addListener (folder_store->registry, corba_listener, &ev);
	if (BONOBO_EX (&ev)) {
		g_warning ("Cannot add listener\n%s", CORBA_exception_id (&ev));
		CORBA_exception_free (&ev);
		return FALSE;
	}
	
	CORBA_exception_free (&ev);
	return TRUE;
}

void
e_summary_mail_init (ESummary *summary)
{
	ESummaryMail *mail;

	g_return_if_fail (summary != NULL);
	g_return_if_fail (IS_E_SUMMARY (summary));

	g_return_if_fail (folder_store != NULL);
	
	mail = g_new0 (ESummaryMail, 1);
	summary->mail = mail;

	mail->html = NULL;

	e_summary_add_protocol_listener (summary, "mail", e_summary_mail_protocol, mail);
	return;
}

void
e_summary_mail_reconfigure (void)
{
	ESummaryPrefs *preferences;
	GList *old, *p;

	old = folder_store->shown;
	folder_store->shown = NULL;

	preferences = e_summary_preferences_get_global ();
	for (p = g_list_last (preferences->display_folders); p; p = p->prev) {
		ESummaryMailFolder *folder;
		ESummaryPrefsFolder *f = p->data;
		char *uri;

#if 0
		if (strncmp (p->data, "file://", 7) == 0 ||
		    strncmp (p->data, "vfolder:", 8) == 0) {
			uri = g_strdup (p->data);
		} else {
			uri = g_strconcat ("file://", p->data, NULL);
		}
#endif
		uri = g_strdup (f->physical_uri);
		folder = g_hash_table_lookup (folder_store->physical_uri_to_folder, uri);
		if (folder != NULL) {
			if (folder->init == FALSE) {
				e_summary_mail_get_info (folder->physical_uri);
			}
			folder_store->shown = g_list_append (folder_store->shown, folder);
		}

		g_free (uri);
	}

	/* Free the old list */
	g_list_free (old);

/*  	e_summary_redraw_all (); */
}

#if 0
static int
str_compare (gconstpointer a,
	     gconstpointer b)
{
	ESummaryMailFolder *folder_a, *folder_b;

	folder_a = (ESummaryMailFolder *) a;
	folder_b = (ESummaryMailFolder *) b;
	return strcasecmp (folder_a->name, folder_b->name);
}

static int
sort_storages (gconstpointer a,
	       gconstpointer b)
{
	StorageInfo *si_a, *si_b;

	si_a = (StorageInfo *) a;
	si_b = (StorageInfo *) b;

	return strcasecmp (si_a->name, si_b->name);
}

static char *
get_parent_path (const char *path)
{
	char *last;

	if (strncmp (path, "vfolder", 7) == 0) {
		last = strrchr (path, '#');
		if (last == NULL) {
			return g_strdup (path);
		}
		return g_strndup (path, last - path);
	} else {
		last = strrchr (path, '/');
		return g_strndup (path, last - path);
	}
}

static gboolean
is_folder_shown (const char *path)
{
	GList *p;

	for (p = folder_store->shown; p; p = p->next) {
		ESummaryMailFolder *folder = p->data;
		if (strcmp (folder->path, path) == 0) {
			return TRUE;
		}
	}
	
	return FALSE;
}

static ETreePath
insert_path_recur (ESummaryTable *est,
		   StorageInfo *si,
		   GHashTable *hash_table,
		   const char *path)
{
	char *parent_path, *name;
	ETreePath parent_node, node;
	ESummaryTableModelEntry *entry;
	int children;
	
	parent_path = get_parent_path (path);

	parent_node = g_hash_table_lookup (hash_table, parent_path);
	if (parent_node == NULL) {
		if (si->toplevel == NULL) {
			char *third_slash;
			/* Generate the toplevel from the path */

			if (strncmp (path, "imap://", 7) == 0) {
				/* IMAP */
				third_slash = strchr (path + 8, '/');
				if (third_slash == NULL) {
					/* EEk */
					si->toplevel = g_strdup (path + 8);
				} else {
					si->toplevel = g_strndup (path, third_slash - path);
				}
			} else {
				/* FIXME: Not sure I like this, but... */
				si->toplevel = g_strdup (path);
			}
		}
		
		if (strcmp (si->toplevel, path) == 0) {
			/* Insert root */
			children = e_summary_table_get_num_children (est, NULL);
			node = e_summary_table_add_node (est, NULL, children, NULL);
			entry = g_new (ESummaryTableModelEntry, 1);
			entry->path = node;
			entry->location = NULL;
			entry->name = g_strdup (si->name);
			entry->editable = FALSE;
			entry->removable = FALSE;
			entry->shown = FALSE;

			g_hash_table_insert (est->model, entry->path, entry);
			g_hash_table_insert (hash_table, g_strdup (path), node);
			return node;
		} else {
			parent_node = insert_path_recur (est, si, hash_table, parent_path);
		}
	}

	g_free (parent_path);
	if (strcmp (si->name, "VFolders") == 0) {
		name = strrchr (path, '#');
	} else {
		name = strrchr (path, '/');
	}

	/* Leave out folders called "subfolder" */
	if (strcmp (name + 1, "subfolders") == 0) {
		return parent_node;
	}

	children = e_summary_table_get_num_children (est, parent_node);
	node = e_summary_table_add_node (est, parent_node, children, NULL);
	entry = g_new (ESummaryTableModelEntry, 1);
	entry->path = node;
	entry->location = g_strdup (path);
	entry->name = g_strdup (name + 1);
	entry->editable = TRUE;
	entry->removable = FALSE;

	/* Check if shown */
	entry->shown = is_folder_shown (path);
	g_hash_table_insert (est->model, entry->path, entry);
	g_hash_table_insert (hash_table, g_strdup (path), node);

	return node;
}

static void
free_path_hash (gpointer key,
		gpointer value,
		gpointer data)
{
	g_free (key);
}

static void
add_storage_to_table (ESummaryTable *est,
		      StorageInfo *si)
{
	GHashTable *path_hash;
	GList *p;
	
	path_hash = g_hash_table_new (g_str_hash, g_str_equal);
	si->folders = g_list_sort (si->folders, str_compare); 

	for (p = si->folders; p; p = p->next) {
		ESummaryMailFolder *folder = p->data;

		insert_path_recur (est, si, path_hash, folder->path);
	}

	g_hash_table_foreach (path_hash, free_path_hash, NULL);
	g_hash_table_destroy (path_hash);
}

static void
make_toplevel (StorageInfo *si)
{
	if (si->toplevel != NULL) {
		return;
	}

	if (strcmp (si->name, _("VFolders")) == 0) {
		si->toplevel = g_strdup_printf ("vfolder:%s/evolution/vfolder",
						g_get_home_dir ());
	} else if (strcmp (si->name, _("Local Folders")) == 0) {
		si->toplevel = g_strdup_printf ("file://%s/evolution/local",
						g_get_home_dir ());
	} else {
		si->toplevel = NULL;
	}
}

void
e_summary_mail_fill_list (ESummaryTable *est)
{
	GSList *p;

	g_return_if_fail (IS_E_SUMMARY_TABLE (est));

	g_return_if_fail (folder_store != NULL);
	
	folder_store->storage_list = g_slist_sort (folder_store->storage_list,
						   sort_storages);
	for (p = folder_store->storage_list; p; p = p->next) {
		StorageInfo *si = p->data;
		
		if (si->toplevel == NULL) {
			make_toplevel (si);
		}
		
		add_storage_to_table (est, si);
	}
}
#endif

#if 0
static void
free_folder (gpointer key,
	     gpointer value,
	     gpointer data)
{
	ESummaryMailFolder *folder = value;
	
	g_free (folder->name);
	g_free (folder->path);
	g_free (folder->uri);
	g_free (folder);
}
#endif

void
e_summary_mail_free (ESummary *summary)
{
	ESummaryMail *mail;

	g_return_if_fail (summary != NULL);
	g_return_if_fail (IS_E_SUMMARY (summary));

	mail = summary->mail;

#if 0
	g_hash_table_foreach (mail->folders, free_folder, NULL);
	g_hash_table_destroy (mail->folders);
#endif
	g_free (mail->html);

#if 0
	gtk_signal_disconnect_by_func (GTK_OBJECT (mail->storage_listener),
				       GTK_SIGNAL_FUNC (new_folder_cb), summary);
	gtk_signal_disconnect_by_func (GTK_OBJECT (mail->storage_listener),
				       GTK_SIGNAL_FUNC (remove_folder_cb), summary);
	gtk_signal_disconnect_by_func (GTK_OBJECT (mail->storage_listener),
				       GTK_SIGNAL_FUNC (update_folder_cb), summary);
#endif
	
	g_free (mail);
	summary->mail = NULL;
}

static void
folder_info_pb_changed (BonoboListener *listener,
			const char *name,
			const BonoboArg *arg,
			CORBA_Environment *ev,
			gpointer data)
{
	e_summary_folder_register_storages (folder_store->shell); 
}

static void
lazy_register_storages (void)
{
	Bonobo_PropertyBag pb;
	Bonobo_EventSource event;
	BonoboListener *listener;
	Bonobo_Listener corba_listener;
	CORBA_Environment ev;
	gboolean ready;
	
	/* Get the PropertyBag */
	CORBA_exception_init (&ev);
	pb = Bonobo_Unknown_queryInterface (folder_store->folder_info,
					    "IDL:Bonobo/PropertyBag:1.0", &ev);
	if (BONOBO_EX (&ev)) {
		g_warning ("Error getting propertybag interface: %s",
			   CORBA_exception_id (&ev));
		CORBA_exception_free (&ev);
		return;
	}

	/* Check the initial value */
	ready = bonobo_property_bag_client_get_value_gboolean (pb,
							       "folder-info-ready",
							       NULL);
	if (ready == TRUE) {
		/* Register storages */
		e_summary_folder_register_storages (folder_store->shell); 
		return;
	}
	
	/* Get thh event source for the bag */
	event = Bonobo_Unknown_queryInterface (pb,
					       "IDL:Bonobo/EventSource:1.0", &ev);
	if (BONOBO_EX (&ev)) {
		g_warning ("Error getting event source interface: %s",
			   CORBA_exception_id (&ev));
		CORBA_exception_free (&ev);
		return;
	}

	/* Connect a listener to it */
	listener = bonobo_listener_new (NULL, NULL);
	gtk_signal_connect (GTK_OBJECT (listener), "event-notify",
			    GTK_SIGNAL_FUNC (folder_info_pb_changed), NULL);
	
	corba_listener = bonobo_object_corba_objref (BONOBO_OBJECT (listener));
	Bonobo_EventSource_addListener (event, corba_listener, &ev);
	if (BONOBO_EX (&ev)) {
		g_warning ("Error adding listener: %s\n",
			   CORBA_exception_id (&ev));
		CORBA_exception_free (&ev);
		bonobo_object_unref (BONOBO_OBJECT (listener));
		return;
	}
}

gboolean
e_summary_folder_init_folder_store (GNOME_Evolution_Shell shell)
{
	CORBA_Environment ev;
	BonoboListener *listener;

	if (folder_store != NULL) {
		return TRUE;
	}

	folder_store = g_new0 (FolderStore, 1);
	folder_store->shell = shell;
	
	CORBA_exception_init (&ev);
	folder_store->folder_info = oaf_activate_from_id (MAIL_IID, 0, NULL, &ev);
	if (BONOBO_EX (&ev) || folder_store->folder_info == NULL) {
		g_warning ("Exception creating folderinfo: %s\n",
			   CORBA_exception_id (&ev) ? CORBA_exception_id (&ev) : "(null)");
		CORBA_exception_free (&ev);
		return FALSE;
	}

	CORBA_exception_free (&ev);

	listener = bonobo_listener_new (NULL, NULL);
	gtk_signal_connect (GTK_OBJECT (listener), "event-notify",
			    GTK_SIGNAL_FUNC (mail_change_notify), NULL);
	folder_store->corba_listener = bonobo_object_corba_objref (BONOBO_OBJECT (listener));
	
	/* Create a hash table for the folders */
	folder_store->path_to_folder = g_hash_table_new (g_str_hash, g_str_equal);
	folder_store->physical_uri_to_folder = g_hash_table_new (g_str_hash, g_str_equal);

	/* Wait for the mailer to tell us we're ready to register */
	lazy_register_storages ();
	return TRUE;
}
