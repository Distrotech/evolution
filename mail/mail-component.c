/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* mail-component.c
 *
 * Copyright (C) 2003  Ximian Inc.
 *
 * Authors: Ettore Perazzoli <ettore@ximian.com>
 *	    Michael Zucchi <notzed@ximian.com>
 *	    Jeffrey Stedfast <fejj@ximian.com>
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
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "em-utils.h"
#include "em-composer-utils.h"
#include "em-format.h"
#include "em-folder-tree.h"
#include "em-folder-browser.h"
#include "em-message-browser.h"
#include "em-folder-selector.h"
#include "em-folder-selection.h"
#include "em-migrate.h"

#include "widgets/misc/e-info-label.h"
#include "widgets/misc/e-error.h"

#include "em-search-context.h"
#include "mail-config.h"
#include "mail-component.h"
#include "mail-folder-cache.h"
#include "mail-vfolder.h"
#include "mail-mt.h"
#include "mail-ops.h"
#include "mail-tools.h"
#include "mail-send-recv.h"
#include "mail-session.h"
#include "mail-offline-handler.h"
#include "message-list.h"

#include "e-activity-handler.h"
#include "shell/e-user-creatable-items-handler.h"

#include "composer/e-msg-composer.h"

#include "e-task-bar.h"

#include <gtk/gtklabel.h>

#include <e-util/e-mktemp.h>

#include <gal/e-table/e-tree.h>
#include <gal/e-table/e-tree-memory.h>
#include <libgnome/gnome-i18n.h>

#include <camel/camel-file-utils.h>
#include <camel/camel-vtrash-folder.h>

#include <bonobo/bonobo-control.h>
#include <bonobo/bonobo-widget.h>

#define d(x) 

static void create_local_item_cb(EUserCreatableItemsHandler *handler, const char *item_type_name, void *data);

#define MAIL_COMPONENT_DEFAULT(mc) if (mc == NULL) mc = mail_component_peek();

#define PARENT_TYPE bonobo_object_get_type ()
static BonoboObjectClass *parent_class = NULL;

struct _store_info {	
	CamelStore *store;
	char *name;

	/* we keep a reference to these so they remain around for the session */
	CamelFolder *vtrash;
	CamelFolder *vjunk;

	/* for setup only */
	void (*done)(CamelStore *store, CamelFolderInfo *info, void *data);
	void *done_data;

	int ref_count:31;
	int removed:1;
};

struct _MailComponentPrivate {
	GMutex *lock;

	/* states/data used during shutdown */
	enum { MC_QUIT_START, MC_QUIT_SYNC } quit_state;
	int quit_count;
	int quit_expunge;	/* expunge on quit this time around? */

	char *base_directory;
	
	EMFolderTreeModel *model;

	EActivityHandler *activity_handler;
	
	MailAsyncEvent *async_event;
	GHashTable *store_hash; /* stores store_info objects by store */
	
	RuleContext *search_context;
	
	char *context_path;	/* current path for right-click menu */
	
	CamelStore *local_store;
};

/* indexed by _mail_component_folder_t */
static struct {
	char *name;
	char *uri;
	CamelFolder *folder;
} mc_default_folders[] = {
	/* translators: standard local mailbox names */
	{ N_("Inbox"), },
	{ N_("Drafts"), },
	{ N_("Outbox"), },
	{ N_("Sent"), },
	{ "Inbox", },		/* 'always local' inbox */
};

static struct _store_info *
store_info_new(CamelStore *store, const char *name)
{
	struct _store_info *si;

	si = g_malloc0(sizeof(*si));
	si->ref_count = 1;
	if (name == NULL)
		si->name = camel_service_get_name((CamelService *)store, TRUE);
	else
		si->name = g_strdup(name);
	si->store = store;
	camel_object_ref(store);
	/* If these are vfolders then they need to be opened now,
	 * otherwise they wont keep track of all folders */
	if ((store->flags & CAMEL_STORE_VTRASH) != 0)
		si->vtrash = camel_store_get_trash(store, NULL);
	if ((store->flags & CAMEL_STORE_VJUNK) != 0)
		si->vjunk = camel_store_get_junk(store, NULL);

	return si;
}

static void
store_info_ref(struct _store_info *si)
{
	si->ref_count++;
}

static void
store_info_unref(struct _store_info *si)
{
	if (si->ref_count > 1) {
		si->ref_count--;
		return;
	}

	if (si->vtrash)
		camel_object_unref(si->vtrash);
	if (si->vjunk)
		camel_object_unref(si->vjunk);
	camel_object_unref(si->store);
	g_free(si->name);
	g_free(si);
}

static void
mc_add_store_done(CamelStore *store, CamelFolderInfo *info, void *data)
{
	struct _store_info *si = data;

	if (si->done)
		si->done(store, info, si);

	if (!si->removed) {
		/* let the counters know about the already opened junk/trash folders */
		if (si->vtrash)
			mail_note_folder(si->vtrash);
		if (si->vjunk)
			mail_note_folder(si->vjunk);
	}

	store_info_unref(si);
}

/* Utility functions.  */
static void
mc_add_store(MailComponent *component, CamelStore *store, const char *name, void (*done)(CamelStore *store, CamelFolderInfo *info, void *data))
{
	struct _store_info *si;
	
	MAIL_COMPONENT_DEFAULT(component);

	si = store_info_new(store, name);
	si->done = done;
	g_hash_table_insert(component->priv->store_hash, store, si);
	em_folder_tree_model_add_store(component->priv->model, store, si->name);
	store_info_ref(si);
	mail_note_store(store, NULL, mc_add_store_done, si);
}

static void
mc_add_local_store_done(CamelStore *store, CamelFolderInfo *info, void *data)
{
	/*MailComponent *mc = data;*/
	int i;

	for (i=0;i<sizeof(mc_default_folders)/sizeof(mc_default_folders[0]);i++) {
		if (mc_default_folders[i].folder)
			mail_note_folder(mc_default_folders[i].folder);
	}
}

static void
mc_add_local_store(CamelStore *store, const char *name, MailComponent *mc)
{
	mc_add_store(mc, store, name, mc_add_local_store_done);
	camel_object_unref(store);
	g_object_unref(mc);
}

static void
mc_setup_local_store(MailComponent *mc)
{
	MailComponentPrivate *p = mc->priv;
	CamelURL *url;
	char *tmp;
	CamelException ex;
	int i;

	g_mutex_lock(p->lock);
	if (p->local_store != NULL) {
		g_mutex_unlock(p->lock);
		return;
	}

	camel_exception_init(&ex);

	url = camel_url_new("mbox:", NULL);
	tmp = g_strdup_printf("%s/mail/local", p->base_directory);
	camel_url_set_path(url, tmp);
	g_free(tmp);
	tmp = camel_url_to_string(url, 0);
	p->local_store = (CamelStore *)camel_session_get_service(session, tmp, CAMEL_PROVIDER_STORE, &ex);
	g_free(tmp);
	if (p->local_store == NULL)
		goto fail;

	for (i=0;i<sizeof(mc_default_folders)/sizeof(mc_default_folders[0]);i++) {
		/* FIXME: should this uri be account relative? */
		camel_url_set_fragment(url, mc_default_folders[i].name);
		mc_default_folders[i].uri = camel_url_to_string(url, 0);
		mc_default_folders[i].folder = camel_store_get_folder(p->local_store, mc_default_folders[i].name,
								      CAMEL_STORE_FOLDER_CREATE, &ex);
		camel_exception_clear(&ex);
	}

	camel_url_free(url);
	g_mutex_unlock(p->lock);

	g_object_ref(mc);
	camel_object_ref(p->local_store);
	mail_async_event_emit(p->async_event, MAIL_ASYNC_GUI, (MailAsyncFunc)mc_add_local_store, p->local_store, _("On This Computer"), mc);

	return;
fail:
	g_mutex_unlock(p->lock);

	g_warning("Could not setup local store/folder: %s", ex.desc);

	camel_url_free(url);
	camel_exception_clear(&ex);
}

static void
load_accounts (MailComponent *component, EAccountList *accounts)
{
	EIterator *iter;

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

		/* HACK: mbox url's are handled by the local store setup above,
		   any that come through as account sources are really movemail sources! */
		if (account->enabled
		    && service->url != NULL
		    && strncmp(service->url, "mbox:", 5) != 0)
			mail_component_load_store_by_uri (component, service->url, name);
		
		e_iterator_next (iter);
	}
	
	g_object_unref (iter);
}

static void
setup_search_context (MailComponent *component)
{
	MailComponentPrivate *priv = component->priv;

	if (priv->search_context == NULL) {
		char *user = g_build_filename(component->priv->base_directory, "mail/searches.xml", NULL);
		char *system = g_strdup (EVOLUTION_PRIVDATADIR "/searchtypes.xml");
		
		priv->search_context = (RuleContext *)em_search_context_new ();
		g_object_set_data_full (G_OBJECT (priv->search_context), "user", user, g_free);
		g_object_set_data_full (G_OBJECT (priv->search_context), "system", system, g_free);
		rule_context_load (priv->search_context, system, user);
	}
}

static void
mc_startup(MailComponent *mc)
{
	static int started = 0;

	if (started)
		return;
	started = 1;

	mc_setup_local_store(mc);
	load_accounts(mc, mail_config_get_accounts());
	vfolder_load_storage();
}

static void
folder_selected_cb (EMFolderTree *emft, const char *path, const char *uri, guint32 flags, EMFolderView *view)
{
	EMFolderTreeModel *model;
	
	if ((flags & CAMEL_FOLDER_NOSELECT) || !path) {
		em_folder_view_set_folder (view, NULL, NULL);
	} else {
		model = em_folder_tree_get_model (emft);
		em_folder_tree_model_set_selected (model, uri);
		em_folder_tree_model_save_state (model);
		
		em_folder_view_set_folder_uri (view, uri);
	}
}

static int
check_autosave(void *data)
{
	e_msg_composer_check_autosave(NULL);

	return FALSE;
}

static void
view_control_activate_cb (BonoboControl *control, gboolean activate, EMFolderView *view)
{
	BonoboUIComponent *uic;
	static int recover = 0;

	uic = bonobo_control_get_ui_component (control);
	g_assert (uic != NULL);
	
	if (activate) {
		Bonobo_UIContainer container;
		
		container = bonobo_control_get_remote_ui_container (control, NULL);
		bonobo_ui_component_set_container (uic, container, NULL);
		bonobo_object_release_unref (container, NULL);
		
		g_assert (container == bonobo_ui_component_get_container(uic));
		g_return_if_fail (container != CORBA_OBJECT_NIL);
		
		em_folder_view_activate (view, uic, activate);
		e_user_creatable_items_handler_activate(g_object_get_data((GObject *)view, "e-creatable-items-handler"), uic);
	} else {
		em_folder_view_activate (view, uic, activate);
		bonobo_ui_component_unset_container (uic, NULL);
	}

	/* This is a weird place to put it, but createControls does it too early.
	   I also think we should wait to do it until we actually visit the mailer.
	   The delay is arbitrary - without it it shows up before the main window */
	if (!recover) {
		recover = 1;
		g_timeout_add(1000, check_autosave, NULL);
	}
}

/* GObject methods.  */

static void
impl_dispose (GObject *object)
{
	MailComponentPrivate *priv = MAIL_COMPONENT (object)->priv;

	if (priv->activity_handler != NULL) {
		g_object_unref (priv->activity_handler);
		priv->activity_handler = NULL;
	}

	if (priv->search_context != NULL) {
		g_object_unref (priv->search_context);
		priv->search_context = NULL;
	}
	
	if (priv->local_store != NULL) {
		camel_object_unref (priv->local_store);
		priv->local_store = NULL;
	}
	
	(* G_OBJECT_CLASS (parent_class)->dispose) (object);
}

static void
store_hash_free (CamelStore *store, struct _store_info *si, void *data)
{
	si->removed = 1;
	store_info_unref(si);
}

static void
impl_finalize (GObject *object)
{
	MailComponentPrivate *priv = MAIL_COMPONENT (object)->priv;

	g_free (priv->base_directory);
	
	mail_async_event_destroy (priv->async_event);
	
	g_hash_table_foreach (priv->store_hash, (GHFunc)store_hash_free, NULL);
	g_hash_table_destroy (priv->store_hash);
	
	if (mail_async_event_destroy (priv->async_event) == -1) {
		g_warning("Cannot destroy async event: would deadlock");
		g_warning(" system may be unstable at exit");
	}
	
	g_free (priv->context_path);
	g_mutex_free(priv->lock);
	g_free (priv);
	
	(* G_OBJECT_CLASS (parent_class)->finalize) (object);
}

static void
view_on_url (GObject *emitter, const char *url, const char *nice_url, MailComponent *mail_component)
{
	MailComponentPrivate *priv = mail_component->priv;
	
	e_activity_handler_set_message (priv->activity_handler, nice_url);
}

static void
view_changed_cb(EMFolderView *emfv, EInfoLabel *el)
{
	if (emfv->folder) {
		char *name;
		guint32 visible, unread, deleted, junked;
		GString *tmp = g_string_new("");

		camel_object_get(emfv->folder, NULL,
				 CAMEL_FOLDER_NAME, &name,
				 CAMEL_FOLDER_DELETED, &deleted,
				 CAMEL_FOLDER_VISIBLE, &visible,
				 CAMEL_FOLDER_JUNKED, &junked,
				 CAMEL_FOLDER_UNREAD, &unread, NULL);

		if (CAMEL_IS_VTRASH_FOLDER(emfv->folder)) {
			if (((CamelVTrashFolder *)emfv->folder)->type == CAMEL_VTRASH_FOLDER_TRASH)
				g_string_append_printf(tmp, ngettext ("%d deleted", "%d deleted", deleted), deleted);
			else
				g_string_append_printf(tmp, ngettext ("%d junk", "%d junk", junked), junked);
		} else {
			int bits = 0;
			GPtrArray *selected;

			/* This is so that if any of these are
			 * shared/reused, we fallback to the standard
			 * display behaviour */

			selected = message_list_get_selected(emfv->list);

			if (em_utils_folder_is_drafts(emfv->folder, emfv->folder_uri))
				bits |= 1;
			if (em_utils_folder_is_sent(emfv->folder, emfv->folder_uri))
				bits |= 2;
			if (em_utils_folder_is_outbox(emfv->folder, emfv->folder_uri))
				bits |= 4;
			/* HACK: hardcoded inbox or maildir '.' folder */
			if (g_ascii_strcasecmp(emfv->folder->full_name, "inbox") == 0
			    || g_ascii_strcasecmp(emfv->folder->full_name, ".") == 0)
				bits |= 8;

			if (bits == 1)
				g_string_append_printf(tmp, ngettext ("%d draft", "%d drafts", visible), visible);
			else if (bits == 2)
				g_string_append_printf(tmp, ngettext ("%d sent", "%d sent", visible), visible);
			else if (bits == 4)
				g_string_append_printf(tmp, ngettext ("%d unsent", "%d unsent", visible), visible);
			else {
				if (!emfv->hide_deleted)
					visible += deleted;
				g_string_append_printf(tmp, ngettext ("%d total", "%d total", visible), visible);
				if (unread && selected->len <=1)
					g_string_append_printf(tmp, ngettext (", %d unread", ", %d unread", unread), unread);
			}

			if (selected->len > 1)
				g_string_append_printf(tmp, ngettext (", %d selected", ", %d selected", selected->len), selected->len);
			message_list_free_uids(emfv->list, selected);
		}

		if (emfv->folder->parent_store == mail_component_peek_local_store(NULL)
		    && (!strcmp (name, "Drafts") || !strcmp (name, "Inbox")
			|| !strcmp (name, "Outbox") || !strcmp (name, "Sent")))
			e_info_label_set_info(el, _(name), tmp->str);
		else
			e_info_label_set_info(el, name, tmp->str);
		g_string_free(tmp, TRUE);
		camel_object_free(emfv->folder, CAMEL_FOLDER_NAME, name);
	} else {
		e_info_label_set_info(el, _("Mail"), "");
	}
}

/* Evolution::Component CORBA methods.  */

static void
impl_createControls (PortableServer_Servant servant,
		     Bonobo_Control *corba_tree_control,
		     Bonobo_Control *corba_view_control,
		     Bonobo_Control *corba_statusbar_control,
		     CORBA_Environment *ev)
{
	MailComponent *mail_component = MAIL_COMPONENT (bonobo_object_from_servant (servant));
	MailComponentPrivate *priv = mail_component->priv;
	BonoboControl *tree_control;
	BonoboControl *view_control;
	BonoboControl *statusbar_control;
	GtkWidget *tree_widget, *vbox, *info;
	GtkWidget *view_widget;
	GtkWidget *statusbar_widget;
	char *uri;
	
	mail_session_set_interactive(TRUE);
	mc_startup(mail_component);

	view_widget = em_folder_browser_new ();
	
	tree_widget = (GtkWidget *) em_folder_tree_new_with_model (priv->model);
	em_folder_tree_set_excluded ((EMFolderTree *) tree_widget, 0);
	em_folder_tree_enable_drag_and_drop ((EMFolderTree *) tree_widget);
	
	if ((uri = em_folder_tree_model_get_selected (priv->model))) {
		em_folder_tree_set_selected ((EMFolderTree *) tree_widget, uri);
		g_free (uri);
	}
	
	em_format_set_session ((EMFormat *) ((EMFolderView *) view_widget)->preview, session);
	
	g_signal_connect (view_widget, "on-url", G_CALLBACK (view_on_url), mail_component);
	em_folder_view_set_statusbar ((EMFolderView*)view_widget, FALSE);
	
	statusbar_widget = e_task_bar_new ();
	e_activity_handler_attach_task_bar (priv->activity_handler, E_TASK_BAR (statusbar_widget));

	gtk_widget_show (tree_widget);
	gtk_widget_show (view_widget);
	gtk_widget_show (statusbar_widget);

	vbox = gtk_vbox_new(FALSE, 0);
	info = e_info_label_new("stock_mail");
	e_info_label_set_info((EInfoLabel *)info, _("Mail"), "");
	gtk_box_pack_start((GtkBox *)vbox, info, FALSE, TRUE, 0);
	gtk_box_pack_start((GtkBox *)vbox, tree_widget, TRUE, TRUE, 0);

	gtk_widget_show(info);
	gtk_widget_show(vbox);

	tree_control = bonobo_control_new (vbox);
	view_control = bonobo_control_new (view_widget);
	statusbar_control = bonobo_control_new (statusbar_widget);
	
	*corba_tree_control = CORBA_Object_duplicate (BONOBO_OBJREF (tree_control), ev);
	*corba_view_control = CORBA_Object_duplicate (BONOBO_OBJREF (view_control), ev);
	*corba_statusbar_control = CORBA_Object_duplicate (BONOBO_OBJREF (statusbar_control), ev);

	g_object_set_data_full((GObject *)view_widget, "e-creatable-items-handler",
			       e_user_creatable_items_handler_new("mail", create_local_item_cb, tree_widget),
			       (GDestroyNotify)g_object_unref);

	g_signal_connect (view_control, "activate", G_CALLBACK (view_control_activate_cb), view_widget);
	g_signal_connect (tree_widget, "folder-selected", G_CALLBACK (folder_selected_cb), view_widget);

	g_signal_connect(view_widget, "changed", G_CALLBACK(view_changed_cb), info);
	g_signal_connect(view_widget, "loaded", G_CALLBACK(view_changed_cb), info);
}

static CORBA_boolean
impl_requestQuit(PortableServer_Servant servant, CORBA_Environment *ev)
{
	/*MailComponent *mc = MAIL_COMPONENT(bonobo_object_from_servant(servant));*/
	CamelFolder *folder;
	guint32 unsent;

	if (!e_msg_composer_request_close_all())
		return FALSE;
	
	folder = mc_default_folders[MAIL_COMPONENT_FOLDER_OUTBOX].folder;
	if (folder != NULL
	    && camel_session_is_online(session)
	    && camel_object_get(folder, NULL, CAMEL_FOLDER_VISIBLE, &unsent, 0) == 0
	    && unsent > 0
	    && e_error_run(NULL, "mail:exit-unsaved", NULL) != GTK_RESPONSE_YES)
		return FALSE;

	return TRUE;
}

static void
mc_quit_sync_done(CamelStore *store, void *data)
{
	MailComponent *mc = data;

	mc->priv->quit_count--;
}

static void
mc_quit_sync(CamelStore *store, struct _store_info *si, MailComponent *mc)
{
	mc->priv->quit_count++;
	mail_sync_store(store, mc->priv->quit_expunge, mc_quit_sync_done, mc);
}

static CORBA_boolean
impl_quit(PortableServer_Servant servant, CORBA_Environment *ev)
{
	MailComponent *mc = MAIL_COMPONENT(bonobo_object_from_servant(servant));

	switch (mc->priv->quit_state) {
	case MC_QUIT_START: {
		int now = time(NULL)/60/60/24, days;
		GConfClient *gconf = mail_config_get_gconf_client();

		mail_vfolder_shutdown();

		mc->priv->quit_expunge = gconf_client_get_bool(gconf, "/apps/evolution/mail/trash/empty_on_exit", NULL)
			&& ((days = gconf_client_get_int(gconf, "/apps/evolution/mail/trash/empty_on_exit_days", NULL)) == 0
			    || (days + gconf_client_get_int(gconf, "/apps/evolution/mail/trash/empty_date", NULL)) <= now);

		g_hash_table_foreach(mc->priv->store_hash, (GHFunc)mc_quit_sync, mc);

		if (mc->priv->quit_expunge)
			gconf_client_set_int(gconf, "/apps/evolution/mail/trash/empty_date", now, NULL);

		mc->priv->quit_state = MC_QUIT_SYNC;
	}
		/* Falls through */
	case MC_QUIT_SYNC:
		return mc->priv->quit_count == 0;
		/* What else do we need to do at quit time? */
	}

	return TRUE;
}

static GNOME_Evolution_CreatableItemTypeList *
impl__get_userCreatableItems (PortableServer_Servant servant, CORBA_Environment *ev)
{
	GNOME_Evolution_CreatableItemTypeList *list = GNOME_Evolution_CreatableItemTypeList__alloc ();

	list->_length  = 2;
	list->_maximum = list->_length;
	list->_buffer  = GNOME_Evolution_CreatableItemTypeList_allocbuf (list->_length);

	CORBA_sequence_set_release (list, FALSE);

	list->_buffer[0].id = "message";
	list->_buffer[0].description = _("New Mail Message");
	list->_buffer[0].menuDescription = _("_Mail Message");
	list->_buffer[0].tooltip = _("Compose a new mail message");
	list->_buffer[0].menuShortcut = 'm';
	list->_buffer[0].iconName = "stock_mail-compose";
	list->_buffer[0].type = GNOME_Evolution_CREATABLE_OBJECT;

	list->_buffer[1].id = "folder";
	list->_buffer[1].description = _("New Mail Folder");
	list->_buffer[1].menuDescription = _("Mail _Folder");
	list->_buffer[1].tooltip = _("Create a new mail folder");
	list->_buffer[1].menuShortcut = 'f';
	list->_buffer[1].iconName = "stock_new-dir";
	list->_buffer[1].type = GNOME_Evolution_CREATABLE_FOLDER;

	return list;
}

static void
emc_new_folder_response(EMFolderSelector *emfs, int response, void *dummy)
{
	const char *uri, *path;
	
	if (response != GTK_RESPONSE_OK) {
		gtk_widget_destroy((GtkWidget *)emfs);
		return;
	}
	
	uri = em_folder_selector_get_selected_uri(emfs);
	path = em_folder_selector_get_selected_path(emfs);
	
	if (em_folder_tree_create_folder(emfs->emft, path, uri))
		gtk_widget_destroy((GtkWidget *)emfs);
}

static int
create_item(const char *type, EMFolderTreeModel *model, const char *uri)
{
	if (strcmp(type, "message") == 0) {
		if (!em_utils_check_user_can_send_mail(NULL))
			return 0;
	
		em_utils_compose_new_message(uri);
	} else if (strcmp(type, "folder") == 0) {
		EMFolderTree *folder_tree;
		GtkWidget *dialog;
		
		folder_tree = (EMFolderTree *)em_folder_tree_new_with_model(model);
		dialog = em_folder_selector_create_new (folder_tree, 0, _("Create folder"), _("Specify where to create the folder:"));
		if (uri)
			em_folder_selector_set_selected ((EMFolderSelector *) dialog, uri);
		g_signal_connect (dialog, "response", G_CALLBACK(emc_new_folder_response), NULL);
		gtk_widget_show(dialog);
	} else
		return -1;

	return 0;
}

static void
create_local_item_cb(EUserCreatableItemsHandler *handler, const char *item_type_name, void *data)
{
	EMFolderTree *tree = data;
	
	create_item(item_type_name, em_folder_tree_get_model(tree), em_folder_tree_get_selected_uri(tree));
}

static void
impl_requestCreateItem (PortableServer_Servant servant,
			const CORBA_char *item_type_name,
			CORBA_Environment *ev)
{
	MailComponent *mc = MAIL_COMPONENT(bonobo_object_from_servant(servant));

	if (create_item(item_type_name, mc->priv->model, NULL) == -1) {
		CORBA_exception_set (ev, CORBA_USER_EXCEPTION,
				     ex_GNOME_Evolution_Component_UnknownType, NULL);
	}
}

static void
handleuri_got_folder(char *uri, CamelFolder *folder, void *data)
{
	CamelURL *url = data;
	EMMessageBrowser *emmb;

	if (folder != NULL) {
		const char *reply = camel_url_get_param(url, "reply");

		if (reply) {
			int mode;

			if (!strcmp(reply, "all"))
				mode = REPLY_MODE_ALL;
			else if (!strcmp(reply, "list"))
				mode = REPLY_MODE_LIST;
			else /* if "sender" or anything else */
				mode = REPLY_MODE_SENDER;

			em_utils_reply_to_message(folder, camel_url_get_param(url, "uid"), NULL, mode, NULL);
		} else {
			emmb = (EMMessageBrowser *)em_message_browser_window_new();
			/*message_list_set_threaded(((EMFolderView *)emmb)->list, emfv->list->threaded);*/
			/* FIXME: session needs to be passed easier than this */
			em_format_set_session((EMFormat *)((EMFolderView *)emmb)->preview, session);
			em_folder_view_set_folder((EMFolderView *)emmb, folder, uri);
			em_folder_view_set_message((EMFolderView *)emmb, camel_url_get_param(url, "uid"), FALSE);
			gtk_widget_show(emmb->window);
		}
	} else {
		g_warning("Couldn't open folder '%s'", uri);
	}
	camel_url_free(url);
}

static void
impl_handleURI (PortableServer_Servant servant, const char *uri, CORBA_Environment *ev)
{
	if (!strncmp (uri, "mailto:", 7)) {
		if (!em_utils_check_user_can_send_mail(NULL))
			return;

		em_utils_compose_new_message_with_mailto (uri, NULL);
	} else if (!strncmp(uri, "email:", 6)) {
		CamelURL *url = camel_url_new(uri, NULL);

		if (camel_url_get_param(url, "uid") != NULL) {
			char *curi = em_uri_to_camel(uri);

			mail_get_folder(curi, 0, handleuri_got_folder, url, mail_thread_new);
			g_free(curi);
		} else {
			g_warning("email uri's must include a uid parameter");
			camel_url_free(url);
		}
	}
}

static void
impl_sendAndReceive (PortableServer_Servant servant, CORBA_Environment *ev)
{
	mail_send_receive ();
}

static void
impl_upgradeFromVersion (PortableServer_Servant servant, const short major, const short minor, const short revision, CORBA_Environment *ev)
{
	MailComponent *component;
	CamelException ex;

	component = mail_component_peek ();
	
	camel_exception_init (&ex);
	if (em_migrate (component->priv->base_directory, major, minor, revision, &ex) == -1) {
		GNOME_Evolution_Component_UpgradeFailed *failedex;

		failedex = GNOME_Evolution_Component_UpgradeFailed__alloc();
		failedex->what = CORBA_string_dup(_("Failed upgrading Mail settings or folders."));
		failedex->why = CORBA_string_dup(ex.desc);
		CORBA_exception_set(ev, CORBA_USER_EXCEPTION, ex_GNOME_Evolution_Component_UpgradeFailed, failedex);
	}

	camel_exception_clear (&ex);
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
	
	epv->createControls          = impl_createControls;
	epv->requestQuit = impl_requestQuit;
	epv->quit = impl_quit;
	epv->_get_userCreatableItems = impl__get_userCreatableItems;
	epv->requestCreateItem       = impl_requestCreateItem;
	epv->handleURI               = impl_handleURI;
	epv->sendAndReceive          = impl_sendAndReceive;
	epv->upgradeFromVersion      = impl_upgradeFromVersion;
}

static void
mail_component_init (MailComponent *component)
{
	MailComponentPrivate *priv;
	MailOfflineHandler *offline;
	
	priv = g_new0 (MailComponentPrivate, 1);
	component->priv = priv;
	
	priv->lock = g_mutex_new();

	priv->base_directory = g_build_filename (g_get_home_dir (), ".evolution", NULL);
	if (camel_mkdir (priv->base_directory, 0777) == -1 && errno != EEXIST)
		abort ();
	
	priv->model = em_folder_tree_model_new (priv->base_directory);
	
	priv->activity_handler = e_activity_handler_new ();
	
	mail_session_init (priv->base_directory);
	
	priv->async_event = mail_async_event_new();
	priv->store_hash = g_hash_table_new (NULL, NULL);
	
	mail_autoreceive_init();
	
	offline = mail_offline_handler_new();
	bonobo_object_add_interface((BonoboObject *)component, (BonoboObject *)offline);
}

/* Public API.  */
MailComponent *
mail_component_peek (void)
{
	static MailComponent *component = NULL;

	if (component == NULL)
		component = g_object_new(mail_component_get_type(), NULL);

	return component;
}

const char *
mail_component_peek_base_directory (MailComponent *component)
{
	MAIL_COMPONENT_DEFAULT(component);

	return component->priv->base_directory;
}

RuleContext *
mail_component_peek_search_context (MailComponent *component)
{
	MAIL_COMPONENT_DEFAULT(component);

	setup_search_context(component);

	return component->priv->search_context;
}

EActivityHandler *
mail_component_peek_activity_handler (MailComponent *component)
{
	MAIL_COMPONENT_DEFAULT(component);

	return component->priv->activity_handler;
}

void
mail_component_add_store (MailComponent *component, CamelStore *store, const char *name)
{
	mc_add_store(component, store, name, NULL);
}

/**
 * mail_component_load_store_by_uri:
 * @component: mail component
 * @uri: uri of store
 * @name: name of store (used for display purposes)
 * 
 * Return value: Pointer to the newly added CamelStore.  The caller is supposed
 * to ref the object if it wants to store it.
 **/
CamelStore *
mail_component_load_store_by_uri (MailComponent *component, const char *uri, const char *name)
{
	CamelException ex;
	CamelStore *store;
	CamelProvider *prov;

	MAIL_COMPONENT_DEFAULT(component);
	
	camel_exception_init (&ex);
	
	/* Load the service (don't connect!). Check its provider and
	 * see if this belongs in the shell's folder list. If so, add
	 * it.
	 */
	
	prov = camel_provider_get(uri, &ex);
	if (prov == NULL) {
		/* EPFIXME: real error dialog */
		g_warning ("couldn't get service %s: %s\n", uri,
			   camel_exception_get_description (&ex));
		camel_exception_clear (&ex);
		return NULL;
	}
	
	if (!(prov->flags & CAMEL_PROVIDER_IS_STORAGE))
		return NULL;
	
	store = (CamelStore *) camel_session_get_service (session, uri, CAMEL_PROVIDER_STORE, &ex);
	if (store == NULL) {
		/* EPFIXME: real error dialog */
		g_warning ("couldn't get service %s: %s\n", uri,
			   camel_exception_get_description (&ex));
		camel_exception_clear (&ex);
		return NULL;
	}

	mail_component_add_store(component, store, name);
	camel_object_unref (store);
	
	return store;
}

static void
store_disconnect (CamelStore *store, void *event_data, void *user_data)
{
	camel_service_disconnect (CAMEL_SERVICE (store), TRUE, NULL);
	camel_object_unref (store);
}

void
mail_component_remove_store (MailComponent *component, CamelStore *store)
{
	MailComponentPrivate *priv;
	struct _store_info *si;

	MAIL_COMPONENT_DEFAULT(component);

	priv = component->priv;

	/* Because the store_hash holds a reference to each store
	 * used as a key in it, none of them will ever be gc'ed, meaning
	 * any call to camel_session_get_{service,store} with the same
	 * URL will always return the same object. So this works.
	 */
	
	if (!(si = g_hash_table_lookup (priv->store_hash, store)))
		return;
	
	g_hash_table_remove (priv->store_hash, store);
	si->removed = 1;
	store_info_unref(si);
	
	/* so i guess potentially we could have a race, add a store while one
	   being removed.  ?? */
	mail_note_store_remove (store);
	
	em_folder_tree_model_remove_store (priv->model, store);

	camel_object_ref(store);
	mail_async_event_emit (priv->async_event, MAIL_ASYNC_THREAD, (MailAsyncFunc) store_disconnect, store, NULL, NULL);
}

void
mail_component_remove_store_by_uri (MailComponent *component, const char *uri)
{
	CamelProvider *prov;
	CamelStore *store;

	MAIL_COMPONENT_DEFAULT(component);
	
	if (!(prov = camel_provider_get(uri, NULL)))
		return;
	
	if (!(prov->flags & CAMEL_PROVIDER_IS_STORAGE))
		return;
	
	store = (CamelStore *) camel_session_get_service (session, uri, CAMEL_PROVIDER_STORE, NULL);
	if (store != NULL) {
		mail_component_remove_store (component, store);
		camel_object_unref (store);
	}
}

int
mail_component_get_store_count (MailComponent *component)
{
	MAIL_COMPONENT_DEFAULT(component);

	return g_hash_table_size (component->priv->store_hash);
}

/* need to map from internal struct to external api */
struct _store_foreach_data {
	GHFunc func;
	void *data;
};

static void
mc_stores_foreach(CamelStore *store, struct _store_info *si, struct _store_foreach_data *data)
{
	data->func((void *)store, (void *)si->name, data->data);
}

void
mail_component_stores_foreach (MailComponent *component, GHFunc func, void *user_data)
{
	struct _store_foreach_data data = { func, user_data };

	MAIL_COMPONENT_DEFAULT(component);

	g_hash_table_foreach (component->priv->store_hash, (GHFunc)mc_stores_foreach, &data);
}

void
mail_component_remove_folder (MailComponent *component, CamelStore *store, const char *path)
{
	MAIL_COMPONENT_DEFAULT(component);

	/* FIXME: implement me. but first, am I really even needed? */
}

EMFolderTreeModel *
mail_component_peek_tree_model (MailComponent *component)
{
	MAIL_COMPONENT_DEFAULT(component);

	return component->priv->model;
}

CamelStore *
mail_component_peek_local_store (MailComponent *mc)
{
	MAIL_COMPONENT_DEFAULT (mc);
	mc_setup_local_store (mc);
	
	return mc->priv->local_store;
}

/**
 * mail_component_get_folder:
 * @mc: 
 * @id: 
 * 
 * Get a standard/default folder by id.  This call is thread-safe.
 * 
 * Return value: 
 **/
struct _CamelFolder *
mail_component_get_folder(MailComponent *mc, enum _mail_component_folder_t id)
{
	g_assert(id <= MAIL_COMPONENT_FOLDER_LOCAL_INBOX);

	MAIL_COMPONENT_DEFAULT(mc);
	mc_setup_local_store(mc);

	return mc_default_folders[id].folder;
}

/**
 * mail_component_get_folder_uri:
 * @mc: 
 * @id: 
 * 
 * Get a standard/default folder's uri.  This call is thread-safe.
 * 
 * Return value: 
 **/
const char *
mail_component_get_folder_uri(MailComponent *mc, enum _mail_component_folder_t id)
{
	g_assert(id <= MAIL_COMPONENT_FOLDER_LOCAL_INBOX);

	MAIL_COMPONENT_DEFAULT(mc);
	mc_setup_local_store(mc);

	return mc_default_folders[id].uri;
}

BONOBO_TYPE_FUNC_FULL (MailComponent, GNOME_Evolution_Component, PARENT_TYPE, mail_component)
