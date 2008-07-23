/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Authors: 
 *  	Srinivasa Ragavan <sragavan@novell.com>
 * 	Suman Manjunath <msuman@novell.com>
 *  Copyright (C) 2007 Novell, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <exchange-account-listener.h>
#include <string.h>
#include <camel/camel-i18n.h>
#include <libedataserverui/e-passwords.h>
#include "e-util/e-error.h"
#include <libedataserver/e-account.h>
#include <libecal/e-cal.h>

#include <libmapi/libmapi.h>


/* FIXME: The mapi should not be needed in the include statement.
LIMBAPI_CFLAGS or something is going wrong */

#include <mapi/exchange-mapi-folder.h>
#include <mapi/exchange-mapi-connection.h>
#include <mapi/exchange-mapi-utils.h>

/* This definition should be in-sync with those in exchange-mapi-account-setup.c and camel-mapi-store.c */
#define E_PASSWORD_COMPONENT "ExchangeMAPI"

#define d(x) x

static 	GList *mapi_accounts = NULL;
static	GSList *folders_list = NULL;
struct _ExchangeAccountListenerPrivate {
	GConfClient *gconf_client;
	/* we get notification about mail account changes from this object */
	EAccountList *account_list;                  
};

/*stores some info about all currently existing mapi accounts 
  list of ExchangeAccountInfo structures */
struct _ExchangeAccountInfo {
	char *uid;
	char *name;
	char *source_url;
};

typedef struct _ExchangeAccountInfo ExchangeAccountInfo;

#define MAPI_URI_PREFIX   "mapi://" 
#define MAPI_PREFIX_LENGTH 7

#define PARENT_TYPE G_TYPE_OBJECT

static GObjectClass *parent_class = NULL;

static void dispose (GObject *object);
static void finalize (GObject *object);

void
exchange_account_listener_get_folder_list (void)
{
	if (folders_list)
		return;

	folders_list = exchange_mapi_peek_folder_list ();
}

static void 
exchange_account_listener_class_init (ExchangeAccountListenerClass *class)
{
	GObjectClass *object_class;
	
	parent_class =  g_type_class_ref (PARENT_TYPE);
	object_class = G_OBJECT_CLASS (class);
	
	/* virtual method override */
	object_class->dispose = dispose;
	object_class->finalize = finalize;
}

static void 
exchange_account_listener_init (ExchangeAccountListener *config_listener,  ExchangeAccountListenerClass *class)
{
	config_listener->priv = g_new0 (ExchangeAccountListenerPrivate, 1);	
}

static void 
dispose (GObject *object)
{
	ExchangeAccountListener *config_listener = EXCHANGE_ACCOUNT_LISTENER (object);
	
	g_object_unref (config_listener->priv->gconf_client);
	g_object_unref (config_listener->priv->account_list);

	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void 
finalize (GObject *object)
{
	ExchangeAccountListener *config_listener = EXCHANGE_ACCOUNT_LISTENER (object);
	GList *list;
	ExchangeAccountInfo *info;

	if (config_listener->priv) {
		g_free (config_listener->priv);
	}

	for (list = g_list_first (mapi_accounts); list ; list = g_list_next (list)) {
	       
		info = (ExchangeAccountInfo *) (list->data);

		if (info) {
			g_free (info->uid);
			g_free (info->name);
			g_free (info->source_url);
			g_free (info);
		}
	}
	
	g_list_free (mapi_accounts);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

/*determines whehter the passed in account is exchange or not by looking at source url */

static gboolean
is_mapi_account (EAccount *account)
{
	if (account->source->url != NULL) {
		return (strncmp (account->source->url,  MAPI_URI_PREFIX, MAPI_PREFIX_LENGTH ) == 0);
	} else {
		return FALSE;
	}
}

/* looks up for an existing exchange account info in the mapi_accounts list based on uid */

static ExchangeAccountInfo* 
lookup_account_info (const char *key)
{
	GList *list;
        ExchangeAccountInfo *info ;
	int found = 0;
                                                                      
        if (!key)
                return NULL;

	info = NULL;

        for (list = g_list_first (mapi_accounts);  list;  list = g_list_next (list)) {
                info = (ExchangeAccountInfo *) (list->data);
                found = (strcmp (info->uid, key) == 0);
		if (found)
			break;
	}

	if (found)
		return info;

	return NULL;
}

#define CALENDAR_SOURCES 	"/apps/evolution/calendar/sources"
#define TASK_SOURCES 		"/apps/evolution/tasks/sources"
#define JOURNAL_SOURCES 	"/apps/evolution/memos/sources"
#define SELECTED_CALENDARS 	"/apps/evolution/calendar/display/selected_calendars"
#define SELECTED_TASKS 		"/apps/evolution/calendar/tasks/selected_tasks"
#define SELECTED_JOURNALS 	"/apps/evolution/calendar/memos/selected_memos"


GSList *
exchange_account_listener_peek_folder_list ()
{
	if (!folders_list)
		folders_list = exchange_mapi_peek_folder_list ();
	
	return folders_list;
}

void
exchange_account_listener_free_folder_list ()
{
	
	exchange_mapi_folder_list_free ();
	folders_list = NULL;
}

static void
add_cal_esource (EAccount *account, GSList *folders, ExchangeMAPIFolderType folder_type, CamelURL *url)
{
	ESourceList *source_list = NULL;
	ESourceGroup *group = NULL;
	const gchar *conf_key = NULL, *source_selection_key = NULL;
 	GSList *temp_list = NULL;
	GConfClient* client;
	GSList *ids, *temp ;
	gchar *base_uri = NULL;

	if (folder_type ==  MAPI_FOLDER_TYPE_APPOINTMENT) { 
		conf_key = CALENDAR_SOURCES;
		source_selection_key = SELECTED_CALENDARS;
	} else if (folder_type == MAPI_FOLDER_TYPE_TASK) { 
		conf_key = TASK_SOURCES;
		source_selection_key = SELECTED_TASKS;
	} else if (folder_type == MAPI_FOLDER_TYPE_MEMO) {
		conf_key = JOURNAL_SOURCES;
		source_selection_key = SELECTED_JOURNALS;
	} else {
		g_warning ("%s(%d): %s: Unknown ExchangeMAPIFolderType\n", __FILE__, __LINE__, __PRETTY_FUNCTION__);
		return;
	} 

	client = gconf_client_get_default ();
	source_list = e_source_list_new_for_gconf (client, conf_key);
	base_uri = g_strdup_printf ("%s%s@%s/", MAPI_URI_PREFIX, url->user, url->host);
	group = e_source_group_new (account->name, base_uri);
	g_free (base_uri);
	e_source_group_set_property (group, "create_source", "yes");
	e_source_group_set_property (group, "username", url->user);
	e_source_group_set_property (group, "host", url->host);
	e_source_group_set_property (group, "profile", camel_url_get_param (url, "profile"));
	e_source_group_set_property (group, "domain", camel_url_get_param (url, "domain"));

	/* We set these because on new folder creation - these are required. */
	e_source_group_set_property (group, "acl-user-name", account->id->name);
	e_source_group_set_property (group, "acl-user-email", account->id->address);
	e_source_group_set_property (group, "acl-owner-name", account->id->name);
	e_source_group_set_property (group, "acl-owner-email", account->id->address);

	for (temp_list = folders; temp_list != NULL; temp_list = g_slist_next (temp_list)) {
 		ExchangeMAPIFolder *folder = temp_list->data;
		ESource *source = NULL;
		gchar *relative_uri = NULL, *fid = NULL;

		if (folder->container_class != folder_type)
			continue;

		fid = exchange_mapi_util_mapi_id_to_string (folder->folder_id);
		relative_uri = g_strconcat (";", fid, NULL);
		source = e_source_new (folder->folder_name, relative_uri);
		e_source_set_property (source, "auth", "1");
		e_source_set_property (source, "auth-domain", E_PASSWORD_COMPONENT);
		e_source_set_property (source, "auth-type", "plain/password");
		e_source_set_property (source, "username", url->user);
		e_source_set_property (source, "host", url->host);
		e_source_set_property (source, "profile", camel_url_get_param (url, "profile"));
		e_source_set_property (source, "domain", camel_url_get_param (url, "domain"));
		e_source_set_property (source, "folder-id", fid);
		e_source_set_property (source, "use_ssl", camel_url_get_param (url, "use_ssl"));
		e_source_set_property (source, "offline_sync", 
					       camel_url_get_param (url, "offline_sync") ? "1" : "0");

		if (folder->is_default) 
			e_source_set_property (source, "delete", "no");

		if (folder->parent_folder_id) {
			gchar *tmp = exchange_mapi_util_mapi_id_to_string (folder->parent_folder_id);
			e_source_set_property (source, "parent-fid", tmp);
			g_free (tmp);
		}

		e_source_set_property (source, "acl-user-name", account->id->name);
		e_source_set_property (source, "acl-user-email", account->id->address);
		/* FIXME: this would change after foreign folders/delegation is implemented */
		e_source_set_property (source, "acl-owner-name", account->id->name);
		e_source_set_property (source, "acl-owner-email", account->id->address);

		e_source_group_add_source (group, source, -1);

		if (source_selection_key && folder->is_default) {
			ids = gconf_client_get_list (client, source_selection_key , GCONF_VALUE_STRING, NULL);
			ids = g_slist_append (ids, g_strdup (e_source_peek_uid (source)));
			gconf_client_set_list (client,  source_selection_key, GCONF_VALUE_STRING, ids, NULL);

			for (temp = ids; temp != NULL; temp = g_slist_next (temp))
				g_free (temp->data);

			g_slist_free (ids);
		}

		g_object_unref (source);
		g_free (relative_uri);
		g_free (fid);
	}

	if (!e_source_list_add_group (source_list, group, -1))
		return;

	if (!e_source_list_sync (source_list, NULL))
		return;

	g_object_unref (group);
	g_object_unref (source_list);
	g_object_unref (client);
}

static void 
remove_cal_esource (EAccount *existing_account_info, ExchangeMAPIFolderType folder_type, CamelURL *url)
{
	ESourceList *list;
	const gchar *conf_key = NULL, *source_selection_key = NULL;
        GSList *groups;
	gboolean found_group;
	GConfClient* client;
	GSList *ids;
	GSList *node_tobe_deleted;
	gchar *base_uri;

	if (folder_type ==  MAPI_FOLDER_TYPE_APPOINTMENT) { 
		conf_key = CALENDAR_SOURCES;
		source_selection_key = SELECTED_CALENDARS;
	} else if (folder_type == MAPI_FOLDER_TYPE_TASK) { 
		conf_key = TASK_SOURCES;
		source_selection_key = SELECTED_TASKS;
	} else if (folder_type == MAPI_FOLDER_TYPE_MEMO) {
		conf_key = JOURNAL_SOURCES;
		source_selection_key = SELECTED_JOURNALS;
	} else {
		g_warning ("%s(%d): %s: Unknown ExchangeMAPIFolderType\n", __FILE__, __LINE__, __PRETTY_FUNCTION__);
		return;
	} 

        client = gconf_client_get_default();
        list = e_source_list_new_for_gconf (client, conf_key);
	groups = e_source_list_peek_groups (list); 

	base_uri = g_strdup_printf ("mapi://%s@%s/", url->user, url->host);

	found_group = FALSE;

	for ( ; groups != NULL && !found_group; groups = g_slist_next (groups)) {
		ESourceGroup *group = E_SOURCE_GROUP (groups->data);

		if (strcmp (e_source_group_peek_name (group), existing_account_info->name) == 0 && 
		   strcmp (e_source_group_peek_base_uri (group), base_uri) == 0) {
			GSList *sources = e_source_group_peek_sources (group);
			
			for( ; sources != NULL; sources = g_slist_next (sources)) {
				ESource *source = E_SOURCE (sources->data);

				if (source_selection_key) {
					ids = gconf_client_get_list (client, source_selection_key , 
								     GCONF_VALUE_STRING, NULL);
					node_tobe_deleted = g_slist_find_custom (ids, e_source_peek_uid (source), (GCompareFunc) strcmp);
					if (node_tobe_deleted) {
						g_free (node_tobe_deleted->data);
						ids = g_slist_delete_link (ids, node_tobe_deleted);
					}
					gconf_client_set_list (client, source_selection_key, 
							       GCONF_VALUE_STRING, ids, NULL);
				}
			}
			e_source_list_remove_group (list, group);
			e_source_list_sync (list, NULL);	
			found_group = TRUE;
			break;
		}
	}

	g_free (base_uri);
	g_object_unref (list);
	g_object_unref (client);		
}

static void 
modify_cal_esource (EAccount *account, ExchangeMAPIFolderType folder_type, CamelURL *url, ExchangeAccountInfo *existing_account_info)
{
	ESourceList *list;
        GSList *groups;
	gboolean found_group;
      	GConfClient* client;
	const gchar *conf_key = NULL;
	gchar *base_uri;

	if (folder_type ==  MAPI_FOLDER_TYPE_APPOINTMENT) { 
		conf_key = CALENDAR_SOURCES;
	} else if (folder_type == MAPI_FOLDER_TYPE_TASK) { 
		conf_key = TASK_SOURCES;
	} else if (folder_type == MAPI_FOLDER_TYPE_MEMO) {
		conf_key = JOURNAL_SOURCES;
	} else {
		g_warning ("%s(%d): %s: Unknown ExchangeMAPIFolderType\n", __FILE__, __LINE__, __PRETTY_FUNCTION__);
		return;
	}

        client = gconf_client_get_default();
        list = e_source_list_new_for_gconf (client, conf_key);
	groups = e_source_list_peek_groups (list); 

	base_uri = g_strdup_printf ("mapi://%s@%s/", url->user, url->host);

	found_group = FALSE;

	for ( ; groups != NULL && !found_group; groups = g_slist_next (groups)) {
		ESourceGroup *group = E_SOURCE_GROUP (groups->data);

		if (strcmp (e_source_group_peek_name (group), existing_account_info->name) == 0 && 
		    strcmp (e_source_group_peek_base_uri (group), base_uri) == 0) {
			found_group = TRUE;
			e_source_group_set_name (group, account->name);
			e_source_list_sync (list, NULL);
		}
	}

	g_free (base_uri);
	g_object_unref (list);
	g_object_unref (client);
}

/* add sources for calendar and tasks if the account added is exchange account
   adds the new account info to  mapi_accounts list */

static void 
add_calendar_sources (EAccount *account, GSList *folders, ExchangeAccountInfo *info)
{
	CamelURL *url;

	url = camel_url_new (info->source_url, NULL);

	if (url) {
		add_cal_esource (account, folders, MAPI_FOLDER_TYPE_APPOINTMENT, url);
		add_cal_esource (account, folders, MAPI_FOLDER_TYPE_TASK, url);
		add_cal_esource (account, folders, MAPI_FOLDER_TYPE_MEMO, url);
	}

	camel_url_free (url);
}

/* removes calendar and tasks sources if the account removed is exchange account 
   removes the the account info from mapi_account list */

static void 
remove_calendar_sources (EAccount *account, ExchangeAccountInfo *info)
{
	CamelURL *url;

	url = camel_url_new (info->source_url, NULL);

	if (url) {
		remove_cal_esource (account, MAPI_FOLDER_TYPE_APPOINTMENT, url);
		remove_cal_esource (account, MAPI_FOLDER_TYPE_TASK, url);
		remove_cal_esource (account, MAPI_FOLDER_TYPE_MEMO, url);
	}

	camel_url_free (url);
}

/* This is called only when the source-group name is to be changed */
static void 
modify_calendar_sources (EAccount *account, ExchangeAccountInfo *existing_account_info)
{
	CamelURL *url;

	url = camel_url_new (account->source->url, NULL);

	if (url) {
		modify_cal_esource (account, MAPI_FOLDER_TYPE_APPOINTMENT, url, existing_account_info);
		modify_cal_esource (account, MAPI_FOLDER_TYPE_TASK, url, existing_account_info);
		modify_cal_esource (account, MAPI_FOLDER_TYPE_MEMO, url, existing_account_info);
	}

	camel_url_free (url);
}

static gboolean
add_addressbook_sources (EAccount *account, GSList *folders)
{
	CamelURL *url;
	ESourceList *list;
        ESourceGroup *group;
        ESource *source;
       	char *base_uri;
	GSList *books_list, *temp_list;
	GConfClient* client;
	const char* use_ssl;
	gboolean is_frequent_contacts = FALSE, is_writable = FALSE;

        url = camel_url_new (account->source->url, NULL);
	if (url == NULL) {
		return FALSE;
	}

	base_uri =  g_strdup_printf ("mapi://%s@%s/", url->user, url->host);
	client = gconf_client_get_default ();
	list = e_source_list_new_for_gconf (client, "/apps/evolution/addressbook/sources" );
	group = e_source_group_new (account->name, base_uri);
	e_source_group_set_property (group, "user", url->user);
	e_source_group_set_property (group, "host", url->host);
	e_source_group_set_property (group, "profile", camel_url_get_param (url, "profile"));
	e_source_group_set_property (group, "domain", camel_url_get_param (url, "domain"));

	for (temp_list = folders; temp_list != NULL; temp_list = g_slist_next (temp_list)) {
 		ExchangeMAPIFolder *folder = temp_list->data;
		char *tmp = NULL;
		if (folder->container_class != MAPI_FOLDER_TYPE_CONTACT)
			continue;

		source = e_source_new (folder->folder_name, g_strconcat (";",folder->folder_name, NULL));
		e_source_set_property (source, "auth", "plain/password");
		e_source_set_property (source, "auth-domain", E_PASSWORD_COMPONENT);
		e_source_set_property(source, "user", url->user);
		e_source_set_property(source, "host", url->host);
		e_source_set_property(source, "profile", camel_url_get_param (url, "profile"));
		e_source_set_property(source, "domain", camel_url_get_param (url, "domain"));
		tmp = exchange_mapi_util_mapi_id_to_string (folder->folder_id);
		e_source_set_property(source, "folder-id", tmp);
		g_free (tmp);
		e_source_set_property (source, "offline_sync", 
					       camel_url_get_param (url, "offline_sync") ? "1" : "0");
		e_source_set_property (source, "completion", "true");
		e_source_group_add_source (group, source, -1);
		g_object_unref (source);
	}

	//Add GAL
	{
		char *uri;
		uri = g_strdup_printf("galldap://%s@%s/;Global Address List", url->user, url->host);
		source = e_source_new_with_absolute_uri ("Global Address List", uri);
//		source = e_source_new ("Global Address List", g_strconcat (";","Global Address List" , NULL));
		e_source_set_property (source, "auth", "plain/password");
		e_source_set_property (source, "auth-domain", "GALLDAP");
		e_source_set_property(source, "user", url->user);
		e_source_set_property(source, "host", camel_url_get_param (url, "ad_server"));
		e_source_set_property(source, "view-limit", camel_url_get_param (url, "ad_limit"));		
		e_source_set_property(source, "profile", camel_url_get_param (url, "profile"));
		e_source_set_property(source, "domain", camel_url_get_param (url, "domain"));
//		e_source_set_property (source, "offline_sync", 
//					       camel_url_get_param (url, "offline_sync") ? "1" : "0");
		e_source_set_property(source, "offline_sync", "1");
		e_source_set_property (source, "completion", "true");
		e_source_group_add_source (group, source, -1);
		g_object_unref (source);		
	}
	e_source_list_add_group (list, group, -1);
	e_source_list_sync (list, NULL);
	g_object_unref (group);
	g_object_unref (list);
	g_object_unref (client);
	g_free (base_uri);

	return TRUE;
}

static void 
remove_addressbook_sources (ExchangeAccountInfo *existing_account_info)
{
	ESourceList *list;
        ESourceGroup *group;
	GSList *groups;
       	gboolean found_group;
	CamelURL *url;
	char *base_uri;
	GConfClient *client;

	url = camel_url_new (existing_account_info->source_url, NULL);
	if (url == NULL) {
		return;
	}

	base_uri =  g_strdup_printf ("mapi://%s@%s/", url->user,  url->host);
	client = gconf_client_get_default ();
	list = e_source_list_new_for_gconf (client, "/apps/evolution/addressbook/sources" );
	groups = e_source_list_peek_groups (list); 

	found_group = FALSE;

	for ( ; groups != NULL &&  !found_group; groups = g_slist_next (groups)) {

		group = E_SOURCE_GROUP (groups->data);
		if ( strcmp ( e_source_group_peek_base_uri (group), base_uri) == 0 && strcmp (e_source_group_peek_name (group), existing_account_info->name) == 0) {

			e_source_list_remove_group (list, group);
			e_source_list_sync (list, NULL);
			found_group = TRUE;
		}
	}

	g_object_unref (list);
	g_object_unref (client);
	g_free (base_uri);
	camel_url_free (url);
}

static void 
modify_addressbook_sources (EAccount *account, ExchangeAccountInfo *existing_account_info)
{
	CamelURL *url;
	ESourceList *list;
        ESourceGroup *group;
	GSList *groups;
       	gboolean found_group;
	char *old_base_uri;
	GConfClient *client;

	url = camel_url_new (existing_account_info->source_url, NULL);
	if (url == NULL) 
		return;

	old_base_uri =  g_strdup_printf ("mapi://%s@%s/", url->user, url->host);
	camel_url_free (url);

	client = gconf_client_get_default ();
	list = e_source_list_new_for_gconf (client, "/apps/evolution/addressbook/sources");
	groups = e_source_list_peek_groups (list); 

	group = NULL;
	found_group = FALSE;
	for ( ; groups != NULL &&  !found_group; groups = g_slist_next (groups)) {

		group = E_SOURCE_GROUP (groups->data);
		if (strcmp (e_source_group_peek_base_uri(group), old_base_uri) == 0 && strcmp (e_source_group_peek_name (group), existing_account_info->name) == 0) {
			found_group = TRUE;
			e_source_group_set_name (group, account->name);
			e_source_list_sync (list, NULL);
		}
	}

	g_free (old_base_uri);
	g_object_unref (list);
	g_object_unref (client);
}

static void
account_added (EAccountList *account_listener, EAccount *account)
{
	ExchangeAccountInfo *info;
	EAccount *parent;
	gboolean status;
	CamelURL *parent_url;
	
	d(printf("account added\n"));
	if (!is_mapi_account (account))
		return;
	
	info = g_new0 (ExchangeAccountInfo, 1);
	info->uid = g_strdup (account->uid);
	info->name = g_strdup (account->name);
	info->source_url = g_strdup (account->source->url);

	/* Fetch the folders into a global list for future use.*/
	exchange_account_listener_get_folder_list ();

	add_addressbook_sources (account, folders_list);
	add_calendar_sources (account, folders_list, info);
	/*FIXME: Maybe the folders_list above should be freed */

	mapi_accounts = g_list_append (mapi_accounts, info);
}

static void 
account_removed (EAccountList *account_listener, EAccount *account)
{
       	ExchangeAccountInfo *info;
	CamelURL *url;
	
	d(printf("Account removed\n"));
	if (!is_mapi_account (account))
		return;
	
	info = lookup_account_info (account->uid);
	if (info == NULL) 
		return;

	exchange_account_listener_get_folder_list ();

	/* This foo needs a lotta work.. at present, using this to remove calendar sources */

	remove_addressbook_sources (info);
	remove_calendar_sources (account, info);

	mapi_accounts = g_list_remove (mapi_accounts, info);
        url = camel_url_new (info->source_url, NULL);
	if (url != NULL) {
		char *profile = camel_url_get_param (url, "profile");
		exchange_mapi_delete_profile (profile);
		camel_url_free (url);
	}

	g_free (info->uid);
	g_free (info->name);
	g_free (info->source_url);
        g_free (info);
}

#define CMP(parm) strcmp(camel_url_get_param(old_url, parm), camel_url_get_param(new_url, parm))

static void
account_changed (EAccountList *account_listener, EAccount *account)
{
	gboolean bis_mapi_account;
	CamelURL *old_url, *new_url;
	ExchangeAccountInfo *existing_account_info;

	d(printf("account changed\n"));
	bis_mapi_account = is_mapi_account (account);
	
	existing_account_info = lookup_account_info (account->uid);
       
	exchange_account_listener_get_folder_list ();

	if (existing_account_info == NULL && bis_mapi_account) {
		if (!account->enabled)
			return;

		/* some account of other type is changed to MAPI */
		account_added (account_listener, account);
	} else if (existing_account_info != NULL && !bis_mapi_account) {
		/*MAPI account is changed to some other type */
		account_removed (account_listener, account);
	} else if (existing_account_info != NULL && bis_mapi_account) {
		gboolean bnew = FALSE;
		gboolean modified = FALSE;
		if (!account->enabled) {
			remove_addressbook_sources (existing_account_info);
			remove_calendar_sources (account, existing_account_info);
			return;
		}

		/* some info of mapi account is changed . update the sources with new info if required */
		old_url = camel_url_new (existing_account_info->source_url, NULL);
		new_url = camel_url_new (account->source->url, NULL);

		if (CMP("domain") || strcmp (old_url->user, new_url->user)|| strcmp (old_url->host, new_url->host)) {
			/* Need to recreate the profile */
			char *password, *key;
			gboolean status;

			key = camel_url_to_string (new_url, CAMEL_URL_HIDE_PASSWORD | CAMEL_URL_HIDE_PARAMS);
			password = e_passwords_get_password (E_PASSWORD_COMPONENT, key);
			if (!password) {
				gboolean remember = FALSE;
				gchar *title;
		
				title = g_strdup_printf (_("Enter Password for %s"), new_url->user);
				password = e_passwords_ask_password (title, E_PASSWORD_COMPONENT, key, title,
								     E_PASSWORDS_REMEMBER_FOREVER|E_PASSWORDS_SECRET,
								     &remember, NULL);
				g_free (title);

				if (!password) {
					g_free (key);
					g_warning ("Password canceled");
					return;
				}
			} 
			g_free (key);
			status = exchange_mapi_create_profile (new_url->user, password, camel_url_get_param (new_url, "domain"), new_url->host);		
			if (!status) {
				//FIXME: Give a warning and forget password.
				g_warning ("Unable to create profile");
				return;
			}

			bnew = TRUE;
		}

		if (!bnew && strcmp (existing_account_info->name, account->name)) {
			/* just the source group names have to be modified.. no sweat.. */
			modify_addressbook_sources (account, existing_account_info);
			modify_calendar_sources (account, existing_account_info);
			modified = TRUE;
		} else {
			remove_addressbook_sources (existing_account_info);
			remove_calendar_sources (account, existing_account_info);
		}

		g_free (existing_account_info->name);
		g_free (existing_account_info->source_url);
		existing_account_info->name = g_strdup (account->name);
		existing_account_info->source_url = g_strdup (account->source->url);
		if (bnew || !modified) {
			/* Free the old folderlist and get a new one */
			exchange_account_listener_free_folder_list ();
			exchange_account_listener_peek_folder_list ();
			add_addressbook_sources (account, folders_list);
			add_calendar_sources (account, folders_list, existing_account_info);
		}

		//FIXME: Update the profile about domain/server/user
		camel_url_free (old_url);
		camel_url_free (new_url);
	}	
} 

static void
exchange_account_listener_construct (ExchangeAccountListener *config_listener)
{
	EIterator *iter;
	EAccount *account;
	ExchangeAccountInfo *info ;

       	config_listener->priv->account_list = e_account_list_new (config_listener->priv->gconf_client);

	for ( iter = e_list_get_iterator (E_LIST ( config_listener->priv->account_list) ) ; e_iterator_is_valid (iter); e_iterator_next (iter) ) {
		
		account = E_ACCOUNT (e_iterator_get (iter));

		if ( is_mapi_account (account) && account->enabled) {
			
		        info = g_new0 (ExchangeAccountInfo, 1);
			info->uid = g_strdup (account->uid);
			info->name = g_strdup (account->name);
			info->source_url = g_strdup (account->source->url);
			mapi_accounts = g_list_append (mapi_accounts, info);
			
		}
			
	}

	printf ("\n\alistener is constructed \n\a");

	g_signal_connect (config_listener->priv->account_list, "account_added", G_CALLBACK (account_added), NULL);
	g_signal_connect (config_listener->priv->account_list, "account_changed", G_CALLBACK (account_changed), NULL);
	g_signal_connect (config_listener->priv->account_list, "account_removed", G_CALLBACK (account_removed), NULL);    
}

GType
exchange_account_listener_get_type (void)
{
	static GType exchange_account_listener_type  = 0;

	if (!exchange_account_listener_type) {
		static GTypeInfo info = {
                        sizeof (ExchangeAccountListenerClass),
                        (GBaseInitFunc) NULL,
                        (GBaseFinalizeFunc) NULL,
                        (GClassInitFunc) exchange_account_listener_class_init,
                        NULL, NULL,
                        sizeof (ExchangeAccountListener),
                        0,
                        (GInstanceInitFunc) exchange_account_listener_init
                };
		exchange_account_listener_type = g_type_register_static (PARENT_TYPE, "ExchangeAccountListener", &info, 0);
	}

	return exchange_account_listener_type;
}

ExchangeAccountListener*
exchange_account_listener_new ()
{
	ExchangeAccountListener *config_listener;
       
	config_listener = g_object_new (EXCHANGE_TYPE_ACCOUNT_LISTENER, NULL);
	config_listener->priv->gconf_client = gconf_client_get_default();
	
	exchange_account_listener_construct (config_listener);

	return config_listener;
}
