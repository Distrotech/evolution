/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-groupwise-store.c : class for an groupwise store */

/*
 *  Authors:
 *  Sivaiah Nallagatla <snallagatla@novell.com>
 *  Parthasarathi Susarla <sparthasarathi@novell.com>
 *
 *  Copyright 2004 Novell, Inc.
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
 */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "e-util/e-path.h"

#include "camel-groupwise-store.h"
#include "camel-groupwise-summary.h"
#include "camel-groupwise-store-summary.h"
#include "camel-groupwise-folder.h"

#include "camel-session.h"
#include "camel-debug.h"
#include "camel-i18n.h" 
#include "camel-disco-diary.h"
#include "camel-types.h"
#include "camel-folder.h" 


#define d(x) printf(x);

struct _CamelGroupwiseStorePrivate {
	char *server_name;
	char *port;
	char *user;
	char *use_ssl;

	char *base_url ;
	char *storage_path ;

	GHashTable *id_hash ;
	GHashTable *name_hash ;
	EGwConnection *cnc;
};

static CamelDiscoStoreClass *parent_class = NULL;

extern CamelServiceAuthType camel_groupwise_password_authtype; /*for the query_auth_types function*/

/*prototypes*/
static void camel_groupwise_rename_folder (CamelStore *store, 
					const char *old_name, 
					const char *new_name, 
					CamelException *ex);

CamelFolderInfo *camel_groupwise_create_folder(CamelStore *store,
				 	       const char *parent_name,
					       const char *folder_name,
					       CamelException *ex) ;

static void camel_groupwise_store_construct (CamelService *service, CamelSession *session,
					CamelProvider *provider, CamelURL *url,
					CamelException *ex) ;

static gboolean camel_groupwise_store_connect (CamelService *service, CamelException *ex) ;

static  GList *camel_groupwise_store_query_auth_types (CamelService *service, CamelException *ex) ;

CamelFolder * camel_groupwise_get_folder( CamelStore *store,
					  const char *folder_name,
					  guint32 flags,
					  CamelException *ex) ;

static CamelFolderInfo *gw_build_folder_info(CamelGroupwiseStore *gw_store, const char *parent_name, const char *folder_name) ;

static GPtrArray *get_folders (CamelStore *store, const char *top, guint32 flags, CamelException *ex) ;

static CamelFolderInfo *gw_get_folder_info_online (CamelStore *store, const char *top, guint32 flags, CamelException *ex) ;

static CamelFolderInfo *camel_groupwise_store_get_folder_info (CamelStore *store,
						     const char *top,
						     guint32 flags,
						     CamelException *ex) ;

CamelFolderInfo *camel_groupwise_create_folder(CamelStore *store,
				 	       const char *parent_name,
					       const char *folder_name,
					       CamelException *ex) ;

void camel_groupwise_delete_folder(CamelStore *store,
				     const char *folder_name,
				     CamelException *ex) ;

static void camel_groupwise_rename_folder(CamelStore *store,
					  const char *old_name,
					  const char *new_name,
					  CamelException *ex) ;

char * groupwise_get_name(CamelService *service, gboolean brief) ;

static CamelFolder *gw_get_folder_online (CamelStore *store, const char *folder_name, guint32 flags, CamelException *ex) ;

static void gw_forget_folder (CamelGroupwiseStore *gw_store, const char *folder_name, CamelException *ex) ;
/*End of prototypes*/


static void
camel_groupwise_store_class_init (CamelGroupwiseStoreClass *camel_groupwise_store_class)
{
/*	CamelObjectClass *camel_object_class =
		CAMEL_OBJECT_CLASS (camel_groupwise_store_class);*/
	CamelServiceClass *camel_service_class =
		CAMEL_SERVICE_CLASS (camel_groupwise_store_class);
	CamelStoreClass *camel_store_class =
		CAMEL_STORE_CLASS (camel_groupwise_store_class);
	CamelDiscoStoreClass *camel_disco_store_class =
		CAMEL_DISCO_STORE_CLASS (camel_groupwise_store_class) ;
	
	parent_class = CAMEL_DISCO_STORE_CLASS (camel_type_get_global_classfuncs (camel_disco_store_get_type ()));
	
	camel_service_class->construct = camel_groupwise_store_construct;
	camel_service_class->connect = camel_groupwise_store_connect;
	camel_service_class->query_auth_types = camel_groupwise_store_query_auth_types;
	camel_service_class->get_name = groupwise_get_name ;
	
	camel_store_class->get_folder_info = camel_groupwise_store_get_folder_info;
	camel_store_class->get_folder = camel_groupwise_get_folder ;
	camel_store_class->create_folder = camel_groupwise_create_folder ;
	camel_store_class->delete_folder = camel_groupwise_delete_folder ;
	camel_store_class->rename_folder = camel_groupwise_rename_folder ;

/*	camel_disco_store_class->can_work_offline = gw_can_work_offline ;
	camel_disco_store_class->get_folder_online = gw_get_folder_online ;
	camel_disco_store_class->get_folder_offline = gw_get_folder_offline ;
	camel_disco_store_class->get_folder_resyncing = gw_get_folder_online ;*/
	camel_disco_store_class->get_folder_info_online = gw_get_folder_info_online ;
/*	camel_disco_store_class->get_folder_info_offline = gw_get_folder_info_offline ;
	camel_disco_store_class->get_folder_info_resyncing = gw_get_folder_info_online ;*/

}


/*This frees the private structure*/
static void
camel_groupwise_store_finalize (CamelObject *object)
{
	CamelGroupwiseStore *groupwise_store = CAMEL_GROUPWISE_STORE (object) ;
	CamelGroupwiseStorePrivate *priv = groupwise_store->priv ;

	if (groupwise_store->summary) {
		camel_store_summary_save ((CamelStoreSummary *)groupwise_store->summary) ;
		camel_object_unref (groupwise_store->summary) ;
	}
	
	if (priv) {
		if (priv->user) {
			g_free (priv->user);
			priv->user = NULL;
		}
		if (priv->server_name) {
			g_free (priv->server_name);
			priv->server_name = NULL;
		}
		if (priv->port) {
			g_free (priv->port);
			priv->port = NULL;
		}
		if (priv->use_ssl) {
			g_free (priv->use_ssl);
			priv->use_ssl = NULL;
		}
		if (priv->base_url) {
			g_free (priv->base_url) ;
			priv->base_url = NULL ;
		}
		
		if (E_IS_GW_CONNECTION (priv->cnc)) {
			g_object_unref (priv->cnc);
			priv->cnc = NULL;
		}
		g_free (groupwise_store->priv);
		groupwise_store->priv = NULL;

		if (groupwise_store->storage_path)
			g_free(groupwise_store->storage_path) ;
		
		if (priv->id_hash)
			g_hash_table_destroy (priv->id_hash);
		if (priv->name_hash)	
			g_hash_table_destroy (priv->name_hash) ;
	}

}

static void
camel_groupwise_store_init (gpointer object, gpointer klass)
{
	CamelGroupwiseStore *groupwise_store = CAMEL_GROUPWISE_STORE (object);
	CamelGroupwiseStorePrivate *priv = g_new0 (CamelGroupwiseStorePrivate, 1);
	
	d("in groupwise store init\n");
	printf("in groupwise store init\n");
	priv->server_name = NULL;
	priv->port = NULL;
	priv->use_ssl = NULL;
	priv->user = NULL;
	priv->cnc = NULL;
	groupwise_store->priv = priv;
	
}


CamelType
camel_groupwise_store_get_type (void)
{
	static CamelType camel_groupwise_store_type = CAMEL_INVALID_TYPE;
	
	if (camel_groupwise_store_type == CAMEL_INVALID_TYPE)	{
		camel_groupwise_store_type =
			camel_type_register (CAMEL_DISCO_STORE_TYPE,
					     "CamelGroupwiseStore",
					     sizeof (CamelGroupwiseStore),
					     sizeof (CamelGroupwiseStoreClass),
					     (CamelObjectClassInitFunc) camel_groupwise_store_class_init,
					     NULL,
					     (CamelObjectInitFunc) camel_groupwise_store_init,
					     (CamelObjectFinalizeFunc) camel_groupwise_store_finalize);
	}
	
	return camel_groupwise_store_type;
}


static void
camel_groupwise_store_construct (CamelService *service, CamelSession *session,
	   CamelProvider *provider, CamelURL *url,
	   CamelException *ex)
{
	CamelGroupwiseStore *groupwise_store = CAMEL_GROUPWISE_STORE (service);
	CamelStore *store = CAMEL_STORE (service);
	const char *property_value, *base_url ;
	CamelGroupwiseStorePrivate *priv = groupwise_store->priv ;
	
	d("in groupwise store constrcut\n");
	printf("in groupwise store constrcut\n");

	CAMEL_SERVICE_CLASS (parent_class)->construct (service, session, provider, url, ex);
	if (camel_exception_is_set (ex))
		return;
	
	if (!(url->host || url->user)) {
		camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_INVALID,
				     _("Host or user not availbale in url"));
	}

	/*store summary*/

	/*storage path*/
	priv->storage_path = camel_session_get_storage_path (session, service, ex) ;
	if (!priv->storage_path)
		return ;
	
	/*host and user*/
	priv->server_name = g_strdup (url->host);
	priv->user = g_strdup (url->user);
	printf("server_name:%s \n user:%s \n",url->host,url->user) ;

	/*base url*/
	base_url = camel_url_to_string (service->url, (CAMEL_URL_HIDE_PASSWORD |
						       CAMEL_URL_HIDE_PARAMS   |
						       CAMEL_URL_HIDE_AUTH)  ) ;
	priv->base_url = g_strdup (base_url) ;								       

	printf("<<< groupwise_store base url :%s >>>\n", priv->base_url) ;
	/*soap port*/
	property_value =  camel_url_get_param (url, "soap_port");
	if (property_value == NULL)
		priv->port = g_strdup ("7181");
	else if(strlen(property_value) == 0)
		priv->port = g_strdup ("7181");
	else
		priv->port = g_strdup (property_value);

	/*Hash Table*/	
	priv->id_hash = g_hash_table_new (g_str_hash, g_str_equal) ;
	priv->name_hash = g_hash_table_new (g_str_hash, g_str_equal) ;

	/*ssl*/
	priv->use_ssl = g_strdup (camel_url_get_param (url, "soap_ssl"));
	printf("||| priv->use_ssl : %s\n", priv->use_ssl) ;
	
	store->flags = 0; //XXX: Shouldnt do this....
	
}

static gboolean
camel_groupwise_store_connect (CamelService *service, CamelException *ex)
{
	CamelGroupwiseStore *groupwise_store = CAMEL_GROUPWISE_STORE (service);
	CamelGroupwiseStorePrivate *priv = groupwise_store->priv;
	CamelSession *session = camel_service_get_session (service);
	char *uri;
	char *prompt;

	d("in groupwise store connect\n");
	printf("in groupwise store connect\n");
	if (priv->use_ssl) 
		uri = g_strconcat ("https://", priv->server_name, ":", priv->port, "/soap", NULL);
	else 
		uri = g_strconcat ("http://", priv->server_name, ":", priv->port, "/soap", NULL);
	
	prompt = g_strdup_printf (_("Please enter the Groupwise password for %s@%s"),
						service->url->user,
						service->url->host);
	
	service->url->passwd = camel_session_get_password (session, service, "Groupwise",
							   prompt, "password", CAMEL_SESSION_PASSWORD_SECRET, ex);
	
	g_free(prompt) ;
	//When do we free service->url->passwd??

	priv->cnc = e_gw_connection_new (uri, priv->user, service->url->passwd);
	
	service->url->passwd = NULL;
	if (E_IS_GW_CONNECTION (priv->cnc)) {
		return TRUE;
	}

	return FALSE;

}


static  GList*
camel_groupwise_store_query_auth_types (CamelService *service, CamelException *ex)
{
	GList *auth_types = NULL;
	
	d("in query auth types\n");
	printf("in query auth types\n");
	auth_types = g_list_prepend (auth_types,  &camel_groupwise_password_authtype);
	return auth_types;
}


/*****************/
CamelFolder * camel_groupwise_get_folder( CamelStore *store,
					  const char *folder_name,
					  guint32 flags,
					  CamelException *ex)
{
	CamelGroupwiseStore *gw_store = CAMEL_GROUPWISE_STORE (store) ;
	CamelGroupwiseStorePrivate *priv = gw_store->priv ;
	CamelFolder *folder ;
	char *storage_path, *folder_dir, *temp_name,*temp_str,*container_id ;
	EGwConnectionStatus status ;
	GList *list ;

	printf("||| Get FOLDER : %s ||| \n",folder_name) ;
	temp_name = folder_name ;
	temp_str = strchr(folder_name,'/') ;
	if(temp_str == NULL) {
		container_id = 	g_strdup (g_hash_table_lookup (priv->name_hash, g_strdup(folder_name))) ;
	}
	else {
		temp_str++ ;
		container_id = 	g_strdup (g_hash_table_lookup (priv->name_hash, g_strdup(temp_str))) ;
	}
	printf("|| Container id: %s ||\n",container_id) ;

	camel_operation_start (NULL, _("Fetching summary information for new messages"));

	status = e_gw_connection_get_items (priv->cnc, container_id, NULL, NULL, &list) ;
	if (status != E_GW_CONNECTION_STATUS_OK) {
		camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_INVALID, _("Authentication failed"));
		return NULL;
	}
	storage_path = g_strdup_printf("%s/folders", priv->storage_path);
        folder_dir = e_path_to_physical (storage_path, folder_name);
	g_free(storage_path) ;
	
	
	folder = camel_gw_folder_new(store,folder_dir,folder_name,ex) ;
	if(folder) {
		CamelException local_ex ;
		int count ;

		gw_store->current_folder = folder ;
		camel_object_ref (folder) ;
		camel_exception_init (&local_ex) ;
		
		/*gw_folder_selected() ;*/
		
		gw_update_summary (folder, list,  ex) ;

		count = camel_folder_summary_count (folder->summary) ;
		printf(" |||| COUNT : %d ||||\n", count) ;
		/*gw_rescan() ;*/
		camel_folder_summary_save(folder->summary) ;
	}
	camel_operation_end (NULL);
	return folder ;
}


/*****************/


/*Build/populate CamelFolderInfo structure
  based on the imap_build_folder_info function*/
static CamelFolderInfo *
gw_build_folder_info(CamelGroupwiseStore *gw_store, const char *parent_name, const char *folder_name)
{
	CamelURL *url ;
	const char *name ;
	CamelFolderInfo *fi ;
	CamelGroupwiseStorePrivate *priv = gw_store->priv ;

	fi = g_malloc0(sizeof(*fi)) ;
	if (parent_name)
		fi->full_name = g_strconcat(parent_name,"/",g_strdup(folder_name), NULL) ;
	else	
		fi->full_name = g_strdup(folder_name) ;
	fi->unread = 0 ;
	fi->total = 0 ;
	
	printf("<<< FOLDER NAME : %s >>>\n",folder_name) ;
	printf("<<< GROUPWISE_STORE BASE URL : %s >>>\n", priv->base_url) ;
	url = camel_url_new(priv->base_url,NULL) ;
	g_free(url->path) ;
	//url->path = g_strdup_printf("/%s", folder_name) ;
	url->path = g_strdup_printf("/%s", fi->full_name) ;
	fi->uri = camel_url_to_string(url,CAMEL_URL_HIDE_ALL) ;
	camel_url_free(url) ;

	name = strchr(fi->full_name,'/') ;
	if(name == NULL)
		name = fi->full_name ;
	else
		name++ ;
	
	fi->name = g_strdup(name) ;

	return fi ;
}


static GPtrArray *
get_folders (CamelStore *store, const char *top, guint32 flags, CamelException *ex)
{
	printf(" >> Get Folders - Not implemented <<\n") ;
}

static CamelFolderInfo *
gw_get_folder_info_online (CamelStore *store, const char *top, guint32 flags, CamelException *ex)
{
	CamelGroupwiseStore *groupwise_store = CAMEL_GROUPWISE_STORE (store);
	CamelGroupwiseStorePrivate  *priv = groupwise_store->priv;
	GPtrArray *folders ;

	if(top == NULL)
		top = "" ;

	/*LOCK*/
	folders = get_folders (store, top, flags, ex) ;
	if (folders == NULL)
		goto done;


done:
	/*UNLOCK*/
}

static CamelFolder *
gw_get_folder_online (CamelStore *store, const char *folder_name, guint32 flags, CamelException *ex) 
{
	CamelFolder *folder = NULL ;

	printf(" >>> Get Folder Online <<<\n\n\n\n") ;
	return folder ;
}

void print_entry (gpointer key, gpointer data, gpointer user_data)
{
//	g_print ("|| key :%-10s ||\n|| value: %-10s ||\n",(gchar*)key, (gchar*)data ) ;
	g_print ("|| value: %-10s ||\n", (gchar*)data ) ;
}

static 	CamelFolderInfo *
camel_groupwise_store_get_folder_info (CamelStore *store,
						     const char *top,
						     guint32 flags,
						     CamelException *ex)
{
	CamelGroupwiseStore *groupwise_store = CAMEL_GROUPWISE_STORE (store);
	CamelGroupwiseStorePrivate  *priv = groupwise_store->priv;
	int status;
	GPtrArray *folders;
	GList *folder_list = NULL, *temp_list = NULL ;
	const char *url;
	CamelFolderInfo *info ;

	if (!E_IS_GW_CONNECTION( priv->cnc)) {
		if (!camel_groupwise_store_connect (CAMEL_SERVICE(store), ex)) {
			camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_CANT_AUTHENTICATE, _("Authentication failed"));
			return NULL;
		}
	 }
/*	if (top == NULL)
		top = "";*/

	
	status = e_gw_connection_get_container_list (priv->cnc, &folder_list);
	if (status != E_GW_CONNECTION_STATUS_OK) {
		/*FIX ME set the camel exception id*/
		return NULL;
	}
	status = e_gw_connection_get_container_list (priv->cnc, &temp_list);
	if (status != E_GW_CONNECTION_STATUS_OK) {
		/*FIX ME set the camel exception id*/
		return NULL;
	}
	//temp_list = folder_list ;
	
	folders = g_ptr_array_new();
	
	url = camel_url_to_string (CAMEL_SERVICE(groupwise_store)->url,
					(CAMEL_URL_HIDE_PASSWORD|
					 CAMEL_URL_HIDE_PARAMS|
					 CAMEL_URL_HIDE_AUTH) );

	/*Populate the hash table for finding the mapping from container id <-> folder name*/
	for (;temp_list != NULL ; temp_list = g_list_next (temp_list) ) {
		char *name, *id ;
		 name = e_gw_container_get_name (E_GW_CONTAINER (temp_list->data));
		 id = e_gw_container_get_id(E_GW_CONTAINER(temp_list->data)) ;
		// printf("name : %s : id :  %s\n",name, id) ;

		g_hash_table_insert (priv->id_hash, g_strdup(id), g_strdup(name)) ; 
		g_hash_table_insert (priv->name_hash, g_strdup(name), g_strdup(id)) ;

		g_free (name) ;
		g_free (id) ;
	}

	/*g_hash_table_foreach (priv->id_hash,print_entry,NULL) ;
	g_hash_table_foreach (priv->name_hash,print_entry,NULL) ;*/
	

	for (; folder_list != NULL; folder_list = g_list_next(folder_list)) {
		CamelFolderInfo *fi;
		char *parent,  *orig_key ;
		gchar *par_name ;

		fi = g_new0 (CamelFolderInfo, 1);
		/*Arrgh!!! An ugly hack!! FIXME*/
		if (top != NULL) {
			char *folder_name  = g_strdup (e_gw_container_get_name (E_GW_CONTAINER (folder_list->data)) ) ;
			if (folder_name)
				if (strcmp(top,folder_name))
					continue ;
		}

		parent = e_gw_container_get_parent_id (E_GW_CONTAINER (folder_list->data)) ;
		par_name = g_strdup (g_hash_table_lookup (priv->id_hash, g_strdup(parent))) ;
		if (par_name != NULL) {
			fi->name = g_strdup (e_gw_container_get_name (E_GW_CONTAINER (folder_list->data)) ) ;
			fi->full_name = g_strconcat (par_name,"/",e_gw_container_get_name (E_GW_CONTAINER (folder_list->data)),NULL )  ;
			//fi->uri = g_strconcat (url, parent, "/",  e_gw_container_get_id (E_GW_CONTAINER (folder_list->data)), NULL)  ; 
			fi->uri = g_strconcat (url,par_name,"/",e_gw_container_get_name (E_GW_CONTAINER (folder_list->data)),NULL) ;
		}
		else {
			fi->name =  fi->full_name = g_strdup (e_gw_container_get_name (E_GW_CONTAINER (folder_list->data)));
			//fi->uri = g_strconcat (url, "", e_gw_container_get_id(E_GW_CONTAINER(folder_list->data)), NULL) ;
			fi->uri = g_strconcat (url, "", e_gw_container_get_name(E_GW_CONTAINER(folder_list->data)), NULL) ;

		}
		
		g_ptr_array_add (folders, fi);

		printf("|| Folder Name : %s || \n",fi->name) ;
		printf("|| Full  Name : %s || \n",fi->full_name) ;
		printf("|| Folder Uri  : %s || \n",fi->uri) ;
		printf("================\n") ;
		
		g_free (parent) ;
		g_free (par_name) ;
		fi = parent = par_name = NULL ;
		
	}
	return camel_folder_info_build (folders, NULL, '/', FALSE) ;
}


CamelFolderInfo *camel_groupwise_create_folder(CamelStore *store,
				 	       const char *parent_name,
					       const char *folder_name,
					       CamelException *ex)
{
	CamelGroupwiseStore *groupwise_store = CAMEL_GROUPWISE_STORE (store);
	CamelGroupwiseStorePrivate  *priv = groupwise_store->priv;
	CamelFolderInfo *root ;

	int status;
	/*GPtrArray *folders;
	GList *folder_list = NULL;
	const char *url;*/

	printf("||| In create folder |||\n") ;
	if(parent_name == NULL)
		parent_name = "" ;

	if (!E_IS_GW_CONNECTION( priv->cnc)) {
		if (!camel_groupwise_store_connect (CAMEL_SERVICE(store), ex)) {
			camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_CANT_AUTHENTICATE, _("Authentication failed"));
			return NULL;
		}
	 }
	status = e_gw_connection_create_folder(priv->cnc,parent_name,folder_name) ;

	root = gw_build_folder_info(groupwise_store, parent_name,folder_name) ;
	if (status == E_GW_CONNECTION_STATUS_OK) {
		printf(">>> Folder Successfully created <<<\n") ;
		camel_object_trigger_event (CAMEL_OBJECT (store), "folder_created", root);
	}
	return root ;
}

void camel_groupwise_delete_folder(CamelStore *store,
				     const char *folder_name,
				     CamelException *ex)
{
	CamelGroupwiseStore *groupwise_store = CAMEL_GROUPWISE_STORE (store);
	CamelGroupwiseStorePrivate  *priv = groupwise_store->priv;
	EGwConnectionStatus status ;
	const char * container = e_gw_connection_get_container_id(priv->cnc,folder_name) ;

	printf("||| DELETING FOLDER |||\n") ;
	
	status = e_gw_connection_remove_item (priv->cnc, container, folder_name) ;

	if (status == E_GW_CONNECTION_STATUS_OK) {
		printf(" ||| Deleted Successfully : %s|||\n", folder_name) ;
		gw_forget_folder(groupwise_store,folder_name,ex) ;
	}
}
						     



static void camel_groupwise_rename_folder(CamelStore *store,
					  const char *old_name,
					  const char *new_name,
					  CamelException *ex)
{
	CamelGroupwiseStore *groupwise_store = CAMEL_GROUPWISE_STORE (store);
/*	CamelGroupwiseStorePrivate  *priv = groupwise_store->priv;
	char *oldpath, *newpath, *storepath, *newname ;*/

	/*
		1. check if online(if online/offline modes are supported
		2. check if there are any subscriptions
	*/

	printf(" << Renaming the folder >>\n") ;
}

char * groupwise_get_name(CamelService *service, gboolean brief) 
{
	if(brief) 
		return g_strdup_printf(_("GroupWise server %s"), service->url->host) ;
	else
		return g_strdup_printf(_("GroupWise service for %s on %s"), 
					service->url->user, service->url->host) ;
}



static void
gw_forget_folder (CamelGroupwiseStore *gw_store, const char *folder_name, CamelException *ex)
{
	/**** IMPLEMENT MESSAGE CACHE *****/
	CamelFolderSummary *summary;
	CamelGroupwiseStorePrivate *priv = gw_store->priv ;
	CamelGroupwiseMessageCache *cache ;
	char *summary_file, *state_file;
	char *folder_dir, *storage_path;
	CamelFolderInfo *fi;
	const char *name;

	
	name = folder_name ;

	storage_path = g_strdup_printf ("%s/folders", priv->storage_path) ;
	folder_dir = g_strdup(e_path_to_physical (storage_path,folder_name)) ;

	if (access(folder_dir, F_OK) != 0) {
		g_free(folder_dir) ;
		printf("Folder  %s does not exist\n", folder_dir) ;
		return ;
	}

	summary_file = g_strdup_printf ("%s/summary", folder_dir) ;
	summary = camel_groupwise_summary_new(summary_file) ;
	if(!summary) {
		g_free(summary_file) ;
		g_free(folder_dir) ;
		return ;
	}

/*	cache = camel_groupwise_message_cache_new (folder_dir, summary, ex) ;
	if (cache) 
		camel_groupwise_message_cache_clear (cache) ;

	camel_object_unref (cache) ;*/
	camel_object_unref (summary) ;
	unlink (summary_file) ;
	g_free (summary_file) ;


	state_file = g_strdup_printf ("%s/cmeta", folder_dir) ;
	unlink (state_file) ;
	g_free (state_file) ;

	rmdir (folder_dir) ;
	g_free (folder_dir) ;

/*	camel_store_summary_remove_path ( (CamelStoreSummary *)gw_store->summary, folder_name) ;
	camel_store_summary_save ( (CamelStoreSummary *)gw_store->summary) ;*/


	fi = gw_build_folder_info(gw_store,NULL, folder_name) ;
	camel_object_trigger_event (CAMEL_OBJECT (gw_store), "folder_deleted", fi);
	camel_folder_info_free (fi) ;
}


char *
container_id_lookup (CamelGroupwiseStorePrivate *priv, const char *folder_name)
{
	return g_hash_table_lookup (priv->name_hash,folder_name) ;
}

EGwConnection *
cnc_lookup (CamelGroupwiseStorePrivate *priv)
{
	return priv->cnc ;
}
