/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Michael Zucchi <NotZed@ximian.com>
 *
 *  Copyright 2001 Ximian, Inc. (www.ximian.com)
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
#include <string.h>

/* for the dialogue stuff */
#include <glib.h>
#include <gtk/gtkmain.h>
#include <libgnomeui/gnome-stock.h>
#include <libgnomeui/gnome-dialog.h>
#include <libgnomeui/gnome-window-icon.h>

#include "filter/filter-filter.h"
#include "camel/camel-filter-driver.h"
#include "camel/camel-folder.h"
#include "camel/camel-operation.h"

#include "evolution-storage.h"

#include "mail.h"
#include "mail-mt.h"
#include "mail-config.h"
#include "mail-session.h"
#include "mail-tools.h"
#include "mail-ops.h"
#include "mail-send-recv.h"
#include "mail-folder-cache.h"

#define d(x)

/* ms between status updates to the gui */
#define STATUS_TIMEOUT (250)

/* send/receive email */

/* ********************************************************************** */
/*  This stuff below is independent of the stuff above */

/* this stuff is used to keep track of which folders filters have accessed, and
   what not. the thaw/refreeze thing doesn't really seem to work though */
struct _folder_info {
	char *uri;
	CamelFolder *folder;
	time_t update;
	int count;		/* how many times updated, to slow it down as we go, if we have lots */
};

struct _send_data {
	GList *infos;

	GnomeDialog *gd;
	int cancelled;

	CamelFolder *inbox;	/* since we're never asked to update this one, do it ourselves */
	time_t inbox_update;

	GMutex *lock;
	GHashTable *folders;

	GHashTable *active;	/* send_info's by uri */
};

typedef enum {
	SEND_RECEIVE,		/* receiver */
	SEND_SEND,		/* sender */
	SEND_UPDATE,		/* imap-like 'just update folder info' */
	SEND_INVALID
} send_info_t ;

typedef enum {
	SEND_ACTIVE,
	SEND_CANCELLED,
	SEND_COMPLETE
} send_state_t;

struct _send_info {
	send_info_t type;		/* 0 = fetch, 1 = send */
	CamelOperation *cancel;
	char *uri;
	int keep;
	send_state_t state;
	GtkProgressBar *bar;
	GtkButton *stop;
	GtkLabel *status;

	int timeout_id;
	char *what;
	int pc;

	/*time_t update;*/
	struct _send_data *data;
};

static struct _send_data *send_data = NULL;
static GtkWidget *send_recv_dialogue = NULL;

static struct _send_data *setup_send_data(void)
{
	struct _send_data *data;
	
	if (send_data == NULL) {
		send_data = data = g_malloc0(sizeof(*data));
		data->lock = g_mutex_new();
		data->folders = g_hash_table_new(g_str_hash, g_str_equal);
		data->inbox = mail_tool_get_local_inbox(NULL);
		data->active = g_hash_table_new(g_str_hash, g_str_equal);
	}
	return send_data;
}

static void
receive_cancel(GtkButton *button, struct _send_info *info)
{
	if (info->state == SEND_ACTIVE) {
		camel_operation_cancel(info->cancel);
		if (info->status)
			gtk_label_set_text(info->status, _("Cancelling..."));
		info->state = SEND_CANCELLED;
	}
	if (info->stop)
		gtk_widget_set_sensitive((GtkWidget *)info->stop, FALSE);
}

static void
free_folder_info(void *key, struct _folder_info *info, void *data)
{
	/*camel_folder_thaw (info->folder);	*/
	mail_sync_folder(info->folder, NULL, NULL);
	camel_object_unref((CamelObject *)info->folder);
	g_free(info->uri);
}

static void free_send_info(void *key, struct _send_info *info, void *data)
{
	d(printf("Freeing send info %p\n", info));
	g_free(info->uri);
	camel_operation_unref(info->cancel);
	if (info->timeout_id != 0)
		gtk_timeout_remove(info->timeout_id);
	g_free(info->what);
	g_free(info);
}

static void
free_send_data(void)
{
	struct _send_data *data = send_data;

	g_assert(g_hash_table_size(data->active) == 0);

	if (data->inbox) {
		mail_sync_folder(data->inbox, NULL, NULL);
		/*camel_folder_thaw (data->inbox);		*/
		camel_object_unref((CamelObject *)data->inbox);
	}
	g_list_free(data->infos);
	g_hash_table_foreach(data->active, (GHFunc)free_send_info, NULL);
	g_hash_table_destroy(data->active);
	g_hash_table_foreach(data->folders, (GHFunc)free_folder_info, NULL);
	g_hash_table_destroy(data->folders);
	g_mutex_free(data->lock);
	g_free(data);
	send_data = NULL;
}

static void cancel_send_info(void *key, struct _send_info *info, void *data)
{
	receive_cancel(info->stop, info);
}

static void hide_send_info(void *key, struct _send_info *info, void *data)
{
	info->stop = NULL;
	info->bar = NULL;
	info->status = NULL;

	if (info->timeout_id != 0) {
		gtk_timeout_remove (info->timeout_id);
		info->timeout_id = 0;
	}
}

static void
dialog_destroy (GtkProgressBar *bar, struct _send_data *data)
{
	g_hash_table_foreach (data->active, (GHFunc) hide_send_info, NULL);
	data->gd = NULL;
}

static void
dialogue_clicked(GnomeDialog *gd, int button, struct _send_data *data)
{
	switch(button) {
	case 0:
		d(printf("cancelled whole thing\n"));
		if (!data->cancelled) {
			data->cancelled = TRUE;
			g_hash_table_foreach(data->active, (GHFunc)cancel_send_info, NULL);
		}
		gnome_dialog_set_sensitive(gd, 0, FALSE);
		break;
	case -1:		/* dialogue vanished, so make out its just hidden */
		d(printf("hiding dialogue\n"));
		g_hash_table_foreach(data->active, (GHFunc)hide_send_info, NULL);
		data->gd = NULL;
		break;
	}
}

static void operation_status(CamelOperation *op, const char *what, int pc, void *data);
static int operation_status_timeout(void *data);

static char *
format_url(const char *internal_url)
{
	CamelURL *url;
       	char *pretty_url;

	url = camel_url_new(internal_url, NULL);
	if (url->host)
		pretty_url = g_strdup_printf(_("Server: %s, Type: %s"), url->host, url->protocol);
	else if (url->path)
		pretty_url = g_strdup_printf(_("Path: %s, Type: %s"), url->path, url->protocol);
	else 
		pretty_url = g_strdup_printf(_("Type: %s"), url->protocol);

	camel_url_free(url);

        return pretty_url;
}

static send_info_t get_receive_type(const char *url)
{
	CamelProvider *provider;

	provider = camel_session_get_provider (session, url, NULL);
	if (!provider)
		return SEND_INVALID;
	
	if (provider->flags & CAMEL_PROVIDER_IS_STORAGE)
		return SEND_UPDATE;
	else
		return SEND_RECEIVE;
}

static struct _send_data *
build_dialogue (GSList *sources, CamelFolder *outbox, const char *destination)
{
	GnomeDialog *gd;
	GtkTable *table;
	int row;
	GList *list = NULL;
	struct _send_data *data;
        GtkWidget *send_icon, *recv_icon; 
	GtkLabel *label, *status_label;
	GtkProgressBar *bar;
	GtkButton *stop;
	GtkHSeparator *line;
	struct _send_info *info;
	char *pretty_url; 
	
	gd = (GnomeDialog *)send_recv_dialogue = gnome_dialog_new (_("Send & Receive Mail"), NULL);
	gtk_signal_connect((GtkObject *)gd, "destroy", gtk_widget_destroyed, &send_recv_dialogue);
	gnome_dialog_append_button_with_pixmap (gd, _("Cancel All"), GNOME_STOCK_BUTTON_CANCEL);
	
	gtk_window_set_policy (GTK_WINDOW (gd), FALSE, FALSE, FALSE);  
	gnome_window_icon_set_from_file (GTK_WINDOW (gd), EVOLUTION_ICONSDIR "/send-receive.xpm");
	
	table = (GtkTable *)gtk_table_new (g_slist_length (sources), 4, FALSE);
       	gtk_box_pack_start (GTK_BOX (gd->vbox), GTK_WIDGET (table), TRUE, TRUE, 0);

	/* must bet setup after send_recv_dialogue as it may re-trigger send-recv button */
	data = setup_send_data ();
	
	row = 0;
	while (sources) {
		MailConfigService *source = sources->data;
		
		if (!source->url || !source->enabled) {
			sources = sources->next;
			continue;
		}
		
		/* see if we have an outstanding download active */
		info = g_hash_table_lookup (data->active, source->url);
		if (info == NULL) {
			send_info_t type;
			
			type = get_receive_type (source->url);
			if (type == SEND_INVALID) {
				sources = sources->next;
				continue;
			}
			
			info = g_malloc0 (sizeof (*info));
			info->type = type;
			d(printf("adding source %s\n", source->url));
			
			info->uri = g_strdup (source->url);
			info->keep = source->keep_on_server;
			info->cancel = camel_operation_new (operation_status, info);
			info->state = SEND_ACTIVE;
			info->timeout_id = gtk_timeout_add (STATUS_TIMEOUT, operation_status_timeout, info);
			
			g_hash_table_insert (data->active, info->uri, info);
			list = g_list_prepend (list, info);
		} else if (info->bar != NULL) {
			/* incase we get the same source pop up again */
			sources = sources->next;
			continue;
		} else if (info->timeout_id == 0)
			info->timeout_id = gtk_timeout_add (STATUS_TIMEOUT, operation_status_timeout, info);
		
		recv_icon = gnome_pixmap_new_from_file (EVOLUTION_BUTTONSDIR "/receive-24.png");
		
	       	pretty_url = format_url (source->url);
		label = (GtkLabel *)gtk_label_new (pretty_url);
		g_free (pretty_url);
		
		bar = (GtkProgressBar *)gtk_progress_bar_new ();
		gtk_progress_set_show_text (GTK_PROGRESS (bar), FALSE);
		
		stop = (GtkButton *)gnome_stock_button (GNOME_STOCK_BUTTON_CANCEL);
		status_label = (GtkLabel *)gtk_label_new ((info->type == SEND_UPDATE) ? _("Updating...") :
							  _("Waiting..."));
		
		/* gtk_object_set (data->label, "bold", TRUE, NULL); */
		gtk_misc_set_alignment (GTK_MISC (label), 0, .5);
		gtk_misc_set_alignment (GTK_MISC (status_label), 0, .5);
		
	        gtk_table_attach (table, (GtkWidget *)recv_icon, 0, 1, row, row+2, GTK_EXPAND | GTK_FILL, 0, 3, 1);
		gtk_table_attach (table, (GtkWidget *)label, 1, 2, row, row+1, GTK_EXPAND | GTK_FILL, 0, 3, 1);
		gtk_table_attach (table, (GtkWidget *)bar, 2, 3, row, row+2, GTK_EXPAND | GTK_FILL, 0, 3, 1);
		gtk_table_attach (table, (GtkWidget *)stop, 3, 4, row, row+2, GTK_EXPAND | GTK_FILL, 0, 3, 1);
		gtk_table_attach (table, (GtkWidget *)status_label, 1, 2, row+1, row+2, GTK_EXPAND | GTK_FILL, 0, 3, 1);
		
		info->bar = bar;
		info->status = status_label;
		info->stop = stop;
		info->data = data;
                
		gtk_signal_connect (GTK_OBJECT (stop), "clicked", receive_cancel, info);
		sources = sources->next;
		row = row + 2;
	}
	
	line = (GtkHSeparator *)gtk_hseparator_new ();
	gtk_table_attach (table, GTK_WIDGET (line), 0, 4, row, row+1, GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 3, 3);
	row++;
	gtk_widget_show_all (GTK_WIDGET (table));
	
	if (outbox && destination) {
		info = g_hash_table_lookup (data->active, destination);
		if (info == NULL) {
			info = g_malloc0 (sizeof (*info));
			info->type = SEND_SEND;
			d(printf("adding dest %s\n", destination));
			
			info->uri = g_strdup (destination);
			info->keep = FALSE;
			info->cancel = camel_operation_new (operation_status, info);
			info->state = SEND_ACTIVE;
			info->timeout_id = gtk_timeout_add (STATUS_TIMEOUT, operation_status_timeout, info);
			
			g_hash_table_insert (data->active, info->uri, info);
			list = g_list_prepend (list, info);
		} else if (info->timeout_id == 0)
			info->timeout_id = gtk_timeout_add (STATUS_TIMEOUT, operation_status_timeout, info);
		
		send_icon  = gnome_pixmap_new_from_file (EVOLUTION_BUTTONSDIR "/send-24.png");
		
		pretty_url = format_url (destination);
		label = (GtkLabel *)gtk_label_new (pretty_url);
		g_free (pretty_url);
		
		bar = (GtkProgressBar *)gtk_progress_bar_new ();
		stop = (GtkButton *)gnome_stock_button (GNOME_STOCK_BUTTON_CANCEL);
		status_label = (GtkLabel *)gtk_label_new (_("Waiting..."));
		
		gtk_misc_set_alignment (GTK_MISC (label), 0, .5);
		gtk_misc_set_alignment (GTK_MISC (status_label), 0, .5);
		gtk_progress_set_show_text (GTK_PROGRESS (bar), FALSE);
		
		gtk_table_attach (table, GTK_WIDGET (send_icon), 0, 1, row, row+2, GTK_EXPAND | GTK_FILL, 0, 3, 1);
		gtk_table_attach (table, GTK_WIDGET (label), 1, 2, row, row+1, GTK_EXPAND | GTK_FILL, 0, 3, 1);
		gtk_table_attach (table, GTK_WIDGET (bar), 2, 3, row, row+2, GTK_EXPAND | GTK_FILL, 0, 3, 1);
		gtk_table_attach (table, GTK_WIDGET (stop), 3, 4, row, row+2, GTK_EXPAND | GTK_FILL, 0, 3, 1);
		gtk_table_attach (table, GTK_WIDGET (status_label), 1, 2, row+1, row+2, GTK_EXPAND | GTK_FILL, 0, 3, 1);
		
		info->bar = bar;
		info->stop = stop;
		info->data = data;
		info->status = status_label;
		
		gtk_signal_connect (GTK_OBJECT (stop), "clicked", receive_cancel, info);
		gtk_widget_show_all (GTK_WIDGET (table));
	}
	
	gtk_widget_show (GTK_WIDGET (gd));
	
	gtk_signal_connect (GTK_OBJECT (gd), "clicked", dialogue_clicked, data);
	gtk_signal_connect (GTK_OBJECT (gd), "destroy", dialog_destroy, data);
	
	data->infos = list;
	data->gd = gd;
	
	return data;
}

static void
update_folders(char *uri, struct _folder_info *info, void *data)
{
	time_t now = *((time_t *)data);

	d(printf("checking update for folder: %s\n", info->uri));

	/* let it flow through to the folders every 10 seconds */
	/* we back off slowly as we progress */
	if (now > info->update+10+info->count*5) {
		d(printf("upating a folder: %s\n", info->uri));
		/*camel_folder_thaw(info->folder);
		  camel_folder_freeze(info->folder);*/
		info->update = now;
		info->count++;
	}
}

static void set_send_status(struct _send_info *info, const char *desc, int pc)
{
	/* FIXME: LOCK */
	g_free(info->what);
	info->what = g_strdup(desc);
	info->pc = pc;
}

static void
receive_status (CamelFilterDriver *driver, enum camel_filter_status_t status, int pc, const char *desc, void *data)
{
	struct _send_info *info = data;
	time_t now = time(0);

	/* let it flow through to the folder, every now and then too? */
	g_hash_table_foreach(info->data->folders, (GHFunc)update_folders, &now);

	if (info->data->inbox && now > info->data->inbox_update+20) {
		d(printf("updating inbox too\n"));
		/* this doesn't seem to work right :( */
		/*camel_folder_thaw(info->data->inbox);
		  camel_folder_freeze(info->data->inbox);*/
		info->data->inbox_update = now;
	}

	/* we just pile them onto the port, assuming it can handle it.
	   We could also have a receiver port and see if they've been processed
	   yet, so if this is necessary its not too hard to add */
	/* the mail_gui_port receiver will free everything for us */
	switch (status) {
	case CAMEL_FILTER_STATUS_START:
	case CAMEL_FILTER_STATUS_END:
		set_send_status(info, desc, pc);
		break;
	default:
		break;
	}
}

static int operation_status_timeout(void *data)
{
	struct _send_info *info = data;

	if (info->bar) {
		gtk_progress_set_percentage((GtkProgress *)info->bar, (gfloat)(info->pc/100.0));
		gtk_label_set_text(info->status, info->what);
		return TRUE;
	}

	return FALSE;
}

/* for camel operation status */
static void operation_status(CamelOperation *op, const char *what, int pc, void *data)
{
	struct _send_info *info = data;

	/*printf("Operation '%s', percent %d\n");*/
	switch (pc) {
	case CAMEL_OPERATION_START:
		pc = 0;
		break;
	case CAMEL_OPERATION_END:
		pc = 100;
		break;
	}

	set_send_status(info, what, pc);
}

/* when receive/send is complete */
static void
receive_done (char *uri, void *data)
{
	struct _send_info *info = data;

	if (info->bar) {
		gtk_progress_set_percentage((GtkProgress *)info->bar, (gfloat)1.0);

		switch(info->state) {
		case SEND_CANCELLED:
			gtk_label_set_text(info->status, _("Cancelled."));
			break;
		default:
			info->state = SEND_COMPLETE;
			gtk_label_set_text(info->status, _("Complete."));
		}
	}

	if (info->stop)
		gtk_widget_set_sensitive((GtkWidget *)info->stop, FALSE);

	/* remove/free this active download */
	d(printf("%s: freeing info %p\n", __FUNCTION__, info));
	g_hash_table_remove(info->data->active, info->uri);
	info->data->infos = g_list_remove(info->data->infos, info);

	if (g_hash_table_size(info->data->active) == 0) {
		if (info->data->gd)
			gnome_dialog_close(info->data->gd);
		free_send_data();
	}

	free_send_info(NULL, info, NULL);
}

/* same for updating */
static void
receive_update_done(CamelStore *store, CamelFolderInfo *info, void *data)
{
	receive_done("", data);
}

/* although we dont do anythign smart here yet, there is no need for this interface to
   be available to anyone else.
   This can also be used to hook into which folders are being updated, and occasionally
   let them refresh */
static CamelFolder *
receive_get_folder(CamelFilterDriver *d, const char *uri, void *data, CamelException *ex)
{
	struct _send_info *info = data;
	CamelFolder *folder;
	struct _folder_info *oldinfo;
	char *oldkey;

	g_mutex_lock(info->data->lock);
	oldinfo = g_hash_table_lookup(info->data->folders, uri);
	g_mutex_unlock(info->data->lock);
	if (oldinfo) {
		camel_object_ref((CamelObject *)oldinfo->folder);
		return oldinfo->folder;
	}
	folder = mail_tool_uri_to_folder (uri, 0, ex);
	if (!folder)
		return NULL;

	/* we recheck that the folder hasn't snuck in while we were loading it... */
	/* and we assume the newer one is the same, but unref the old one anyway */
	g_mutex_lock(info->data->lock);
	
	if (g_hash_table_lookup_extended(info->data->folders, uri, (void **)&oldkey, (void **)&oldinfo)) {
		camel_object_unref((CamelObject *)oldinfo->folder);
		oldinfo->folder = folder;
	} else {
		/*camel_folder_freeze (folder);		*/
		oldinfo = g_malloc0(sizeof(*oldinfo));
		oldinfo->folder = folder;
		oldinfo->uri = g_strdup(uri);
		g_hash_table_insert(info->data->folders, oldinfo->uri, oldinfo);
	}
	
	camel_object_ref (CAMEL_OBJECT (folder));
	
	g_mutex_unlock(info->data->lock);
	
	return folder;
}

static void
receive_update_got_store (char *uri, CamelStore *store, void *data)
{
	struct _send_info *info = data;
	
	if (store) {
		EvolutionStorage *storage = mail_lookup_storage (store);
		
		if (storage) {
			mail_note_store(store, storage, CORBA_OBJECT_NIL, receive_update_done, info);
			/*bonobo_object_unref (BONOBO_OBJECT (storage));*/
		} else {
			receive_done ("", info);
		}
	} else {
		receive_done ("", info);
	}
}

void mail_send_receive (void)
{
	GSList *sources;
	GList *scan;
	struct _send_data *data;
	extern CamelFolder *outbox_folder;
	const MailConfigAccount *account;

	if (send_recv_dialogue != NULL) {
		if (GTK_WIDGET_REALIZED(send_recv_dialogue)) {
			gdk_window_show(send_recv_dialogue->window);
			gdk_window_raise(send_recv_dialogue->window);
		}
		return;
	}

	sources = mail_config_get_sources();
	if (!sources)
		return;
	account = mail_config_get_default_account();
	if (!account || !account->transport)
		return;
	
	/* what to do about pop before smtp ?
	   Well, probably hook into receive_done or receive_status on
	   the right pop account, and when it is, then kick off the
	   smtp one. */
	data = build_dialogue(sources, outbox_folder, account->transport->url);
	scan = data->infos;
	while (scan) {
		struct _send_info *info = scan->data;

		switch(info->type) {
		case SEND_RECEIVE:
			mail_fetch_mail(info->uri, info->keep,
					FILTER_SOURCE_INCOMING,
					info->cancel,
					receive_get_folder, info,
					receive_status, info,
					receive_done, info);
			break;
		case SEND_SEND:
			/* todo, store the folder in info? */
			mail_send_queue(outbox_folder, info->uri,
					FILTER_SOURCE_OUTGOING,
					info->cancel,
					receive_get_folder, info,
					receive_status, info,
					receive_done, info);
			break;
		case SEND_UPDATE:
			/* FIXME: error reporting? */
			mail_get_store(info->uri, receive_update_got_store, info);
			break;
		default:
			g_assert_not_reached ();
		}
		scan = scan->next;
	}
}

struct _auto_data {
	char *uri;
	int keep;		/* keep on server flag */
	int period;		/* in seconds */
	int timeout_id;
};

static GHashTable *auto_active;

static gboolean
auto_timeout(void *data)
{
	struct _auto_data *info = data;

	if (camel_session_is_online(session))
		mail_receive_uri(info->uri, info->keep);

	return TRUE;
}

static void auto_setup_set(void *key, struct _auto_data *info, GHashTable *set)
{
	g_hash_table_insert(set, info->uri, info);
}

static void auto_clean_set(void *key, struct _auto_data *info, GHashTable *set)
{
	d(printf("removing auto-check for %s %p\n", info->uri, info));
	g_hash_table_remove(set, info->uri);
	gtk_timeout_remove(info->timeout_id);
	g_free(info->uri);
	g_free(info);
}

/* call to setup initial, and after changes are made to the config */
/* FIXME: Need a cleanup funciton for when object is deactivated */
void
mail_autoreceive_setup (void)
{
	GHashTable *set_hash;
	GSList *sources;
	
	sources = mail_config_get_sources();
	
	if (!sources)
		return;

	if (auto_active == NULL)
		auto_active = g_hash_table_new(g_str_hash, g_str_equal);

	set_hash = g_hash_table_new(g_str_hash, g_str_equal);
	g_hash_table_foreach(auto_active, (GHFunc)auto_setup_set, set_hash);

	while (sources) {
		MailConfigService *source = sources->data;
		if (source->url && source->auto_check && source->enabled) {
			struct _auto_data *info;

			d(printf("setting up auto-receive mail for : %s\n", source->url));

			g_hash_table_remove(set_hash, source->url);
			info = g_hash_table_lookup(auto_active, source->url);
			if (info) {
				info->keep = source->keep_on_server;
				if (info->period != source->auto_check_time*60) {
					info->period = source->auto_check_time*60;
					gtk_timeout_remove(info->timeout_id);
					info->timeout_id = gtk_timeout_add(info->period*1000, auto_timeout, info);
				}
			} else {
				info = g_malloc0(sizeof(*info));
				info->uri = g_strdup(source->url);
				info->keep = source->keep_on_server;
				info->period = source->auto_check_time*60;
				info->timeout_id = gtk_timeout_add(info->period*1000, auto_timeout, info);
				g_hash_table_insert(auto_active, info->uri, info);
				/* If we do this at startup, it can cause the logon dialogue to be hidden,
				   so lets not */
				/*mail_receive_uri(source->url, source->keep_on_server);*/
			}
		}

		sources = sources->next;
	}

	g_hash_table_foreach(set_hash, (GHFunc)auto_clean_set, auto_active);
	g_hash_table_destroy(set_hash);
}

/* we setup the download info's in a hashtable, if we later need to build the gui, we insert
   them in to add them. */
void
mail_receive_uri (const char *uri, int keep)
{
	struct _send_info *info;
	struct _send_data *data;
	extern CamelFolder *outbox_folder;
	send_info_t type;
	
	data = setup_send_data();
	info = g_hash_table_lookup(data->active, uri);
	if (info != NULL) {
		d(printf("download of %s still in progress\n", uri));
		return;
	}
	
	d(printf("starting non-interactive download of '%s'\n", uri));
	
	type = get_receive_type (uri);
	if (type == SEND_INVALID) {
		d(printf ("unsupported provider: '%s'\n", uri));
		return;
	}
	
	info = g_malloc0 (sizeof (*info));
	info->type = type;
	info->bar = NULL;
	info->status = NULL;
	info->uri = g_strdup (uri);
	info->keep = keep;
	info->cancel = camel_operation_new (operation_status, info);
	info->stop = NULL;
	info->data = data;
	info->state = SEND_ACTIVE;
	info->timeout_id = 0;
	
	d(printf("Adding new info %p\n", info));
	
	g_hash_table_insert (data->active, info->uri, info);
	
	switch (info->type) {
	case SEND_RECEIVE:
		mail_fetch_mail (info->uri, info->keep,
				 FILTER_SOURCE_INCOMING,
				 info->cancel,
				 receive_get_folder, info,
				 receive_status, info,
				 receive_done, info);
		break;
	case SEND_SEND:
		/* todo, store the folder in info? */
		mail_send_queue (outbox_folder, info->uri,
				 FILTER_SOURCE_OUTGOING,
				 info->cancel,
				 receive_get_folder, info,
				 receive_status, info,
				 receive_done, info);
		break;
	case SEND_UPDATE:
		/* FIXME: error reporting? */
		mail_get_store (info->uri, receive_update_got_store, info);
		break;
	default:
		g_assert_not_reached ();
	}
}
