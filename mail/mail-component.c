/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* mail-component.c
 *
 * Copyright (C) 2003  Ettore Perazzoli
 *
 * This  program is free  software; you  can redistribute  it and/or
 * modify it under the terms of version 2  of the GNU General Public
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
 * Author: Ettore Perazzoli <ettore@ximian.com>
 */

#include <config.h>

#include "e-storage.h"
#include "e-storage-set.h"
#include "e-storage-browser.h"

#include "folder-browser-factory.h"
#include "mail-config.h"
#include "mail-component.h"
#include "mail-folder-cache.h"
#include "mail-mt.h"
#include "mail-ops.h"
#include "mail-send-recv.h"
#include "mail-session.h"

#include <camel/camel.h>

#include <bonobo/bonobo-control.h>
#include <bonobo/bonobo-widget.h>

#include <gtk/gtklabel.h>

#include <string.h>


#define PARENT_TYPE bonobo_object_get_type ()
static BonoboObjectClass *parent_class = NULL;

struct _MailComponentPrivate {
	char *base_directory;

	MailAsyncEvent *async_event;
	GHashTable *storages_hash;

	EFolderTypeRegistry *folder_type_registry;
	EStorageSet *storage_set;
};


/* Utility functions.  */

/* EPFIXME: Eeek, this totally sucks.  See comment in e-storage.h,
   async_open_folder() should NOT be a signal.  */

struct _StorageConnectedData {
	EStorage *storage;
	char *path;
	EStorageDiscoveryCallback callback;
	void *callback_data;
};
typedef struct _StorageConnectedData StorageConnectedData;

static void
storage_connected_callback (CamelStore *store,
			    CamelFolderInfo *info,
			    StorageConnectedData *data)
{
	EStorageResult result;

	if (info != NULL)
		result = E_STORAGE_OK;
	else
		result = E_STORAGE_GENERICERROR;

	(* data->callback) (data->storage, result, data->path, data->callback_data);

	g_object_unref (data->storage);
	g_free (data->path);
	g_free (data);
}

static void
storage_async_open_folder_callback (EStorage *storage,
				    const char *path,
				    EStorageDiscoveryCallback callback,
				    void *callback_data,
				    CamelStore *store)
{
	StorageConnectedData *storage_connected_data = g_new0 (StorageConnectedData, 1);

	g_object_ref (storage);

	storage_connected_data->storage = storage;
	storage_connected_data->path = g_strdup (path);
	storage_connected_data->callback = callback;
	storage_connected_data->callback_data = callback_data;

	mail_note_store (store, storage, (void *) storage_connected_callback, storage_connected_data);
}

static void
add_storage (MailComponent *component,
	     const char *name,
	     const char *uri,
	     CamelService *store,
	     CamelException *ex)
{
	EStorage *storage;
	EFolder *root_folder;

	camel_object_ref (CAMEL_OBJECT (store));
	g_hash_table_insert (component->priv->storages_hash, store, storage);

	root_folder = e_folder_new (name, "noselect", "");
	storage = e_storage_new (name, root_folder);
	e_storage_declare_has_subfolders (storage, "/", _("Connecting..."));

	g_signal_connect(storage, "async_open_folder",
			 G_CALLBACK (storage_async_open_folder_callback), store);

#if 0
	/* EPFIXME these are not needed anymore.  */
	g_signal_connect(storage, "create_folder", G_CALLBACK(storage_create_folder), store);
	g_signal_connect(storage, "remove_folder", G_CALLBACK(storage_remove_folder), store);
	g_signal_connect(storage, "xfer_folder", G_CALLBACK(storage_xfer_folder), store);
#endif

	e_storage_set_add_storage (component->priv->storage_set, storage);

	g_object_unref (storage);
}

static void
setup_account_storages (MailComponent *component,
			EAccountList *accounts)
{
	CamelException ex;
	EIterator *iter;
	
	camel_exception_init (&ex);
	
	/* Load each service (don't connect!). Check its provider and
	 * see if this belongs in the shell's folder list. If so, add
	 * it.
	 */
	
	iter = e_list_get_iterator ((EList *) accounts);
	while (e_iterator_is_valid (iter)) {
		EAccountService *service;
		EAccount *account;
		const char *name;
		
		account = (EAccount *) e_iterator_get (iter);
		service = account->source;
		name = account->name;
		
		if (account->enabled && service->url != NULL)
			mail_component_load_storage_by_uri (component, service->url, name);
		
		e_iterator_next (iter);
	}
	
	g_object_unref (iter);
}

static inline gboolean
type_is_mail (const char *type)
{
	return !strcmp (type, "mail") || !strcmp (type, "mail/public");
}

static inline gboolean
type_is_vtrash (const char *type)
{
	return !strcmp (type, "vtrash");
}

static void
storage_go_online (gpointer key, gpointer value, gpointer data)
{
	CamelStore *store = key;
	CamelService *service = CAMEL_SERVICE (store);

	if (! (service->provider->flags & CAMEL_PROVIDER_IS_REMOTE)
	    || (service->provider->flags & CAMEL_PROVIDER_IS_EXTERNAL))
		return;

	if ((CAMEL_IS_DISCO_STORE (service)
	     && camel_disco_store_status (CAMEL_DISCO_STORE (service)) == CAMEL_DISCO_STORE_OFFLINE)
	    || service->status != CAMEL_SERVICE_DISCONNECTED) {
		mail_store_set_offline (store, FALSE, NULL, NULL);
		mail_note_store (store, NULL, NULL, NULL);
	}
}

static void
go_online (MailComponent *component)
{
	camel_session_set_online (session, TRUE);
	mail_component_storages_foreach (component, storage_go_online, NULL);
}


/* EStorageBrowser callbacks.  */

static BonoboControl *
create_noselect_control (void)
{
	GtkWidget *label;

	label = gtk_label_new (_("This folder cannot contain messages."));
	gtk_widget_show (label);
	return bonobo_control_new (label);
}

static GtkWidget *
create_view_callback (EStorageBrowser *browser,
		      const char *path,
		      void *unused_data)
{
	BonoboControl *control;
	EFolder *folder;
	const char *folder_type;
	const char *physical_uri;

	folder = e_storage_set_get_folder (e_storage_browser_peek_storage_set (browser), path);
	if (folder == NULL) {
		g_warning ("No folder at %s", path);
		return gtk_label_new ("(You should not be seeing this label)");
	}

	folder_type  = e_folder_get_type_string (folder);
	physical_uri = e_folder_get_physical_uri (folder);

	if (type_is_mail (folder_type)) {
		const char *noselect;
		CamelURL *url;
		
		url = camel_url_new (physical_uri, NULL);
		noselect = url ? camel_url_get_param (url, "noselect") : NULL;
		if (noselect && !strcasecmp (noselect, "yes")) {
			control = create_noselect_control ();
		} else {
			/* FIXME: We are passing a CORBA_OBJECT_NIL where it expects a non-null shell
			   objref...  When we refactor FolderBrowser it should be able to do without
			   the shell.  */
			control = folder_browser_factory_new_control (physical_uri, CORBA_OBJECT_NIL);
		}
		camel_url_free (url);
	} else if (type_is_vtrash (folder_type)) {
		if (!strncasecmp (physical_uri, "file:", 5))
			control = folder_browser_factory_new_control ("vtrash:file:/", CORBA_OBJECT_NIL);
		else
			control = folder_browser_factory_new_control (physical_uri, CORBA_OBJECT_NIL);
	} else
		return NULL;
	
	if (!control)
		return NULL;

	/* FIXME: This leaks the control.  */
	return bonobo_widget_new_control_from_objref (BONOBO_OBJREF (control), CORBA_OBJECT_NIL);
}

static void
browser_page_switched_callback (EStorageBrowser *browser,
				GtkWidget *old_page,
				GtkWidget *new_page,
				BonoboControl *parent_control)
{
	if (BONOBO_IS_WIDGET (old_page)) {
		BonoboControlFrame *control_frame = bonobo_widget_get_control_frame (BONOBO_WIDGET (old_page));

		bonobo_control_frame_control_deactivate (control_frame);
	}

	if (BONOBO_IS_WIDGET (new_page)) {
		BonoboControlFrame *control_frame = bonobo_widget_get_control_frame (BONOBO_WIDGET (new_page));
		Bonobo_UIContainer ui_container = bonobo_control_get_remote_ui_container (parent_control, NULL);

		/* This is necessary because we are not embedding the folder browser control
		   directly; we are putting the folder browser control into a notebook which
		   is then exported to the shell as a control.  So we need to forward the
		   notebook's UIContainer to the folder browser.  */
		bonobo_control_frame_set_ui_container (control_frame, ui_container, NULL);

		bonobo_control_frame_control_activate (control_frame);
	}
}


/* GObject methods.  */

static void
impl_dispose (GObject *object)
{
	MailComponentPrivate *priv = MAIL_COMPONENT (object)->priv;

	if (priv->storage_set != NULL) {
		g_object_unref (priv->storage_set);
		priv->storage_set = NULL;
	}

	if (priv->folder_type_registry != NULL) {
		g_object_unref (priv->folder_type_registry);
		priv->folder_type_registry = NULL;
	}

	(* G_OBJECT_CLASS (parent_class)->dispose) (object);
}

static void
impl_finalize (GObject *object)
{
	MailComponentPrivate *priv = MAIL_COMPONENT (object)->priv;

	g_free (priv->base_directory);

	mail_async_event_destroy (priv->async_event);

	g_hash_table_destroy (priv->storages_hash); /* FIXME free the data within? */

	if (mail_async_event_destroy (priv->async_event) == -1) {
		g_warning("Cannot destroy async event: would deadlock");
		g_warning(" system may be unstable at exit");
	}

	g_free (priv);

	(* G_OBJECT_CLASS (parent_class)->finalize) (object);
}


/* Evolution::Component CORBA methods.  */

static void
impl_createControls (PortableServer_Servant servant,
		     Bonobo_Control *corba_sidebar_control,
		     Bonobo_Control *corba_view_control,
		     CORBA_Environment *ev)
{
	MailComponent *mail_component = MAIL_COMPONENT (bonobo_object_from_servant (servant));
	MailComponentPrivate *priv = mail_component->priv;
	EStorageBrowser *browser;
	GtkWidget *tree_widget;
	GtkWidget *view_widget;
	BonoboControl *sidebar_control;
	BonoboControl *view_control;

	browser = e_storage_browser_new (priv->storage_set, "/", create_view_callback, NULL);

	tree_widget = e_storage_browser_peek_tree_widget (browser);
	view_widget = e_storage_browser_peek_view_widget (browser);

	gtk_widget_show (tree_widget);
	gtk_widget_show (view_widget);

	sidebar_control = bonobo_control_new (tree_widget);
	view_control = bonobo_control_new (view_widget);

	*corba_sidebar_control = CORBA_Object_duplicate (BONOBO_OBJREF (sidebar_control), ev);
	*corba_view_control = CORBA_Object_duplicate (BONOBO_OBJREF (view_control), ev);

	g_signal_connect_object (browser, "page_switched",
				 G_CALLBACK (browser_page_switched_callback), view_control, 0);
}


/* Initialization.  */

static void
mail_component_class_init (MailComponentClass *class)
{
	POA_GNOME_Evolution_Component__epv *epv = &class->epv;

	GObjectClass *object_class = G_OBJECT_CLASS (class);

	parent_class = g_type_class_peek_parent (class);

	object_class->dispose  = impl_dispose;
	object_class->finalize = impl_finalize;

	epv->createControls = impl_createControls;
}

static void
mail_component_init (MailComponent *component)
{
	MailComponentPrivate *priv;
	EAccountList *accounts;

	priv = g_new0 (MailComponentPrivate, 1);
	component->priv = priv;

	/* EPFIXME: Move to a private directory.  */
	priv->base_directory = g_build_filename (g_get_home_dir (), "evolution", NULL);
	g_print ("base directory %s\n", priv->base_directory);

	/* EPFIXME: Turn into an object?  */
	mail_session_init (priv->base_directory);

	priv->async_event = mail_async_event_new();
	priv->storages_hash = g_hash_table_new (NULL, NULL);

	priv->folder_type_registry = e_folder_type_registry_new ();
	priv->storage_set = e_storage_set_new (priv->folder_type_registry);

#if 0				/* EPFIXME TODO somehow */
	for (i = 0; i < sizeof (standard_folders) / sizeof (standard_folders[0]); i++)
		*standard_folders[i].uri = g_strdup_printf ("file://%s/local/%s", evolution_dir, standard_folders[i].name);

	vfolder_load_storage(corba_shell);
#endif
	
	accounts = mail_config_get_accounts ();
	setup_account_storages (component, accounts);
	
#if 0
	/* FIXME?  */
	mail_local_storage_startup (shell_client, evolution_dir);
	mail_importer_init (shell_client);

	for (i = 0; i < sizeof (standard_folders) / sizeof (standard_folders[0]); i++) {
		mail_msg_wait (mail_get_folder (*standard_folders[i].uri, CAMEL_STORE_FOLDER_CREATE,
						got_folder, standard_folders[i].folder, mail_thread_new));
	}
#endif
	
	/* mail_autoreceive_setup (); FIXME keep it off for testing */

#if 0				/* FIXME todo */
	{
		/* setup the global quick-search context */
		char *user = g_strdup_printf ("%s/searches.xml", evolution_dir);
		char *system = g_strdup (EVOLUTION_PRIVDATADIR "/vfoldertypes.xml");
		
		search_context = rule_context_new ();
		g_object_set_data_full(G_OBJECT(search_context), "user", user, g_free);
		g_object_set_data_full(G_OBJECT(search_context), "system", system, g_free);
		
		rule_context_add_part_set (search_context, "partset", filter_part_get_type (),
					   rule_context_add_part, rule_context_next_part);
		
		rule_context_add_rule_set (search_context, "ruleset", filter_rule_get_type (),
					   rule_context_add_rule, rule_context_next_rule);
		
		rule_context_load (search_context, system, user);
	}
#endif

#if 0
	/* FIXME this shouldn't be here.  */
	if (mail_config_is_corrupt ()) {
		GtkWidget *dialog;
		
		dialog = gtk_message_dialog_new (NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_CLOSE,
						 _("Some of your mail settings seem corrupt, "
						   "please check that everything is in order."));
		g_signal_connect (dialog, "response", G_CALLBACK (gtk_widget_destroy), dialog);
		gtk_widget_show (dialog);
	}
#endif

#if 0
	/* FIXME if we nuke the summary this is not necessary anymore.  */

	/* Everything should be ready now */
	evolution_folder_info_notify_ready ();
#endif

	/* FIXME not sure about this.  */
	go_online (component);
}


/* Public API.  */

MailComponent *
mail_component_peek (void)
{
	static MailComponent *component = NULL;

	if (component == NULL)
		component = g_object_new (mail_component_get_type (), NULL);

	return component;
}


const char *
mail_component_peek_base_directory (MailComponent *component)
{
	return component->priv->base_directory;
}


void
mail_component_add_store (MailComponent *component,
			  CamelStore *store,
			  const char *name,
			  const char *uri)
{
	CamelException ex;

	camel_exception_init (&ex);
	
	if (name == NULL) {
		char *service_name;
		
		service_name = camel_service_get_name ((CamelService *) store, TRUE);
		add_storage (component, service_name, uri, (CamelService *) store, &ex);
		g_free (service_name);
	} else {
		add_storage (component, name, uri, (CamelService *) store, &ex);
	}
	
	camel_exception_clear (&ex);
}


void
mail_component_load_storage_by_uri (MailComponent *component,
				    const char *uri,
				    const char *name)
{
	CamelException ex;
	CamelService *store;
	CamelProvider *prov;
	
	camel_exception_init (&ex);
	
	/* Load the service (don't connect!). Check its provider and
	 * see if this belongs in the shell's folder list. If so, add
	 * it.
	 */
	
	prov = camel_session_get_provider (session, uri, &ex);
	if (prov == NULL) {
		/* FIXME: real error dialog */
		g_warning ("couldn't get service %s: %s\n", uri,
			   camel_exception_get_description (&ex));
		camel_exception_clear (&ex);
		return;
	}
	
	if (!(prov->flags & CAMEL_PROVIDER_IS_STORAGE) ||
	    (prov->flags & CAMEL_PROVIDER_IS_EXTERNAL))
		return;
	
	store = camel_session_get_service (session, uri, CAMEL_PROVIDER_STORE, &ex);
	if (store == NULL) {
		/* FIXME: real error dialog */
		g_warning ("couldn't get service %s: %s\n", uri,
			   camel_exception_get_description (&ex));
		camel_exception_clear (&ex);
		return;
	}
	
	if (name != NULL) {
		add_storage (component, name, uri, store, &ex);
	} else {
		char *service_name;
		
		service_name = camel_service_get_name (store, TRUE);
		add_storage (component, service_name, uri, store, &ex);
		g_free (service_name);
	}
	
	if (camel_exception_is_set (&ex)) {
		/* FIXME: real error dialog */
		g_warning ("Cannot load storage: %s",
			   camel_exception_get_description (&ex));
		camel_exception_clear (&ex);
	}
	
	camel_object_unref (CAMEL_OBJECT (store));
}


static void
store_disconnect (CamelStore *store,
		  void *event_data,
		  void *data)
{
	camel_service_disconnect (CAMEL_SERVICE (store), TRUE, NULL);
	camel_object_unref (CAMEL_OBJECT (store));
}

void
mail_component_remove_storage (MailComponent *component,
			       CamelStore *store)
{
	MailComponentPrivate *priv = component->priv;
	EStorage *storage;
	
	/* Because the storages_hash holds a reference to each store
	 * used as a key in it, none of them will ever be gc'ed, meaning
	 * any call to camel_session_get_{service,store} with the same
	 * URL will always return the same object. So this works.
	 */
	
	storage = g_hash_table_lookup (priv->storages_hash, store);
	if (!storage)
		return;
	
	g_hash_table_remove (priv->storages_hash, store);
	
	/* so i guess potentially we could have a race, add a store while one
	   being removed.  ?? */
	mail_note_store_remove (store);

	e_storage_set_remove_storage (priv->storage_set, storage);
	
	mail_async_event_emit(priv->async_event, MAIL_ASYNC_THREAD, (MailAsyncFunc) store_disconnect, store, NULL, NULL);
}


void
mail_component_remove_storage_by_uri (MailComponent *component,
				      const char *uri)
{
	CamelProvider *prov;
	CamelService *store;

	prov = camel_session_get_provider (session, uri, NULL);
	if (!prov)
		return;
	if (!(prov->flags & CAMEL_PROVIDER_IS_STORAGE) ||
	    (prov->flags & CAMEL_PROVIDER_IS_EXTERNAL))
		return;

	store = camel_session_get_service (session, uri, CAMEL_PROVIDER_STORE, NULL);
	if (store != NULL) {
		mail_component_remove_storage (component, CAMEL_STORE (store));
		camel_object_unref (CAMEL_OBJECT (store));
	}
}


EStorage *
mail_component_lookup_storage (MailComponent *component,
			       CamelStore *store)
{
	EStorage *storage;
	
	/* Because the storages_hash holds a reference to each store
	 * used as a key in it, none of them will ever be gc'ed, meaning
	 * any call to camel_session_get_{service,store} with the same
	 * URL will always return the same object. So this works.
	 */
	
	storage = g_hash_table_lookup (component->priv->storages_hash, store);
	if (storage)
		g_object_ref (storage);
	
	return storage;
}


int
mail_component_get_storage_count (MailComponent *component)
{
	return g_hash_table_size (component->priv->storages_hash);
}


void
mail_component_storages_foreach (MailComponent *component,
				 GHFunc func,
				 void *data)
{
	g_hash_table_foreach (component->priv->storages_hash, func, data);
}


BONOBO_TYPE_FUNC_FULL (MailComponent, GNOME_Evolution_Component, PARENT_TYPE, mail_component)
