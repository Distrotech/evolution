/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Michael Zucchi <notzed@ximian.com>
 *
 *  Copyright 2003 Ximian, Inc. (www.ximian.com)
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

#include <gtk/gtkvbox.h>
#include <gtk/gtkscrolledwindow.h>
#include <gtk/gtkbutton.h>
#include <gtk/gtkvpaned.h>

#include <libgnomeprintui/gnome-print-dialog.h>

#include "mail-config.h"

#include <camel/camel-mime-message.h>
#include <camel/camel-stream.h>
#include <camel/camel-stream-filter.h>
#include <camel/camel-mime-filter.h>
#include <camel/camel-mime-filter-tohtml.h>
#include <camel/camel-mime-filter-enriched.h>
#include <camel/camel-multipart.h>
#include <camel/camel-stream-mem.h>
#include <camel/camel-url.h>

#include <bonobo/bonobo-main.h>
#include <bonobo/bonobo-object.h>
#include <bonobo/bonobo-generic-factory.h>
#include <bonobo/bonobo-control.h>
#include <bonobo/bonobo-ui-component.h>
#include <bonobo/bonobo-ui-util.h>

#include "em-format-html-display.h"
#include "em-format-html-print.h"
#include "em-folder-browser.h"
#include "em-subscribe-editor.h"
#include "message-list.h"

#include "evolution-shell-component-utils.h" /* Pixmap stuff, sigh */

struct _EMFolderBrowserPrivate {
	GtkWidget *preview;	/* container for message display */

	int show_preview:1;
	int show_list:1;
};


static void emfb_activate(EMFolderView *emfv, BonoboUIComponent *uic, int state);

static EMFolderViewClass *emfb_parent;

/* Needed since the paned wont take the position its given otherwise ... */
static void
paned_realised(GtkWidget *w, EMFolderBrowser *emfb)
{
	GConfClient *gconf;

	gconf = mail_config_get_gconf_client ();
	gtk_paned_set_position((GtkPaned *)emfb->vpane, gconf_client_get_int(gconf, "/apps/evolution/mail/display/paned_size", NULL));
}

static void
emfb_init(GObject *o)
{
	EMFolderBrowser *emfb = (EMFolderBrowser *)o;
	struct _EMFolderBrowserPrivate *p;

	printf("em folder browser init\n");

	p = emfb->priv = g_malloc0(sizeof(struct _EMFolderBrowserPrivate));

	emfb->view.preview = (EMFormatHTMLDisplay *)em_format_html_display_new();

	g_slist_free(emfb->view.ui_files);
	emfb->view.ui_files = g_slist_append(NULL, EVOLUTION_UIDIR "/evolution-mail-global.xml");
	emfb->view.ui_files = g_slist_append(emfb->view.ui_files, EVOLUTION_UIDIR "/evolution-mail-list.xml");
	emfb->view.ui_files = g_slist_append(emfb->view.ui_files, EVOLUTION_UIDIR "/evolution-mail-message.xml");

	/* FIXME: setup search bar */

	emfb->vpane = gtk_vpaned_new();
	g_signal_connect(emfb->vpane, "realize", G_CALLBACK(paned_realised), emfb);
	gtk_widget_show(emfb->vpane);

	gtk_box_pack_start_defaults((GtkBox *)emfb, emfb->vpane);
	
	gtk_paned_add1((GtkPaned *)emfb->vpane, (GtkWidget *)emfb->view.list);
	gtk_widget_show((GtkWidget *)emfb->view.list);

	/* currently: just use a scrolledwindow for preview widget */
	p->preview = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy((GtkScrolledWindow *)p->preview, GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type((GtkScrolledWindow *)p->preview, GTK_SHADOW_IN);
	gtk_widget_show(p->preview);

	gtk_container_add((GtkContainer *)p->preview, (GtkWidget *)emfb->view.preview->formathtml.html);
	gtk_widget_show((GtkWidget *)emfb->view.preview->formathtml.html);

	gtk_paned_add2((GtkPaned *)emfb->vpane, p->preview);
	gtk_widget_show(p->preview);

	/* FIXME: setup selection */
	/* FIXME: setup dnd */
}

static void
emfb_finalise(GObject *o)
{
	EMFolderBrowser *emfb = (EMFolderBrowser *)o;

	g_free(emfb->priv);

	((GObjectClass *)emfb_parent)->finalize(o);
}

static void
emfb_class_init(GObjectClass *klass)
{
	klass->finalize = emfb_finalise;
	((EMFolderViewClass *)klass)->activate = emfb_activate;
}

GType
em_folder_browser_get_type(void)
{
	static GType type = 0;

	if (type == 0) {
		static const GTypeInfo info = {
			sizeof(EMFolderBrowserClass),
			NULL, NULL,
			(GClassInitFunc)emfb_class_init,
			NULL, NULL,
			sizeof(EMFolderBrowser), 0,
			(GInstanceInitFunc)emfb_init
		};
		emfb_parent = g_type_class_ref(em_folder_view_get_type());
		type = g_type_register_static(em_folder_view_get_type(), "EMFolderBrowser", &info, 0);
	}

	return type;
}

GtkWidget *em_folder_browser_new(void)
{
	EMFolderBrowser *emfb = g_object_new(em_folder_browser_get_type(), 0);

	return (GtkWidget *)emfb;
}

void em_folder_browser_show_preview(EMFolderBrowser *emfb, gboolean state)
{
	if (emfb->priv->show_preview
	    || emfb->view.list == NULL)
		return;
	
	emfb->priv->show_preview = state;
	
	if (state) {
		GConfClient *gconf = mail_config_get_gconf_client ();
		int paned_size /*, y*/;

		paned_size = gconf_client_get_int(gconf, "/apps/evolution/mail/display/paned_size", NULL);

		/*y = save_cursor_pos (emfb);*/
		gtk_paned_set_position (GTK_PANED (emfb->vpane), paned_size);
		gtk_widget_show (GTK_WIDGET (emfb->priv->preview));
		/* need to load/show the current message? */
		/*do_message_selected (emfb);*/
		/*set_cursor_pos (emfb, y);*/
	} else {
		gtk_widget_hide(emfb->priv->preview);
		/*
		mail_display_set_message (emfb->mail_display, NULL, NULL, NULL);
		emfb_ui_message_loaded (emfb);*/
	}
}

/* ********************************************************************** */

static void
emfb_edit_cut(BonoboUIComponent *uid, void *data, const char *path)
{
	printf("editcut\n");
}

static void
emfb_edit_copy(BonoboUIComponent *uid, void *data, const char *path)
{
	printf("editcopy\n");
}

static void
emfb_edit_paste(BonoboUIComponent *uid, void *data, const char *path)
{
	printf("editpaste\n");
}

static void
emfb_edit_invert_selection(BonoboUIComponent *uid, void *data, const char *path)
{
	EMFolderView *emfv = data;

	printf("editinvertselection: %s\n", path);
	message_list_invert_selection(emfv->list);
}

static void
emfb_edit_select_all(BonoboUIComponent *uid, void *data, const char *path)
{
	EMFolderView *emfv = data;
	printf("editselectall\n");
	message_list_select_all(emfv->list);
}

static void
emfb_edit_select_thread(BonoboUIComponent *uid, void *data, const char *path)
{
	EMFolderView *emfv = data;

	printf("editselectthread\n");
	message_list_select_thread(emfv->list);
}

static void
emfb_folder_properties(BonoboUIComponent *uid, void *data, const char *path)
{
	/* If only we could remove this ... */
	/* Should it be part of the factory? */
	printf("folderproperties\n");
}

static void
emfb_folder_expunge(BonoboUIComponent *uid, void *data, const char *path)
{
	/* This is a lot trickier than it should be ... */
	printf("folderexpunge\n");
}

static void
emfb_mark_all_read(BonoboUIComponent *uid, void *data, const char *path)
{
	/* FIXME: make a 'mark messages' function? */

	EMFolderView *emfv = data;
	GPtrArray *uids;
	int i;

	printf("markallread\n");

	uids = camel_folder_get_uids(emfv->folder);
	camel_folder_freeze(emfv->folder);
	for (i=0;i<uids->len;i++)
		camel_folder_set_message_flags(emfv->folder, uids->pdata[i], CAMEL_MESSAGE_SEEN, CAMEL_MESSAGE_SEEN);
	camel_folder_thaw(emfv->folder);
	camel_folder_free_uids(emfv->folder, uids);
}

static void
emfb_view_hide_read(BonoboUIComponent *uid, void *data, const char *path)
{
	EMFolderView *emfv = data;

	printf("viewhideread\n");
	message_list_hide_add(emfv->list, "(match-all (system-flag \"seen\"))", ML_HIDE_SAME, ML_HIDE_SAME);
}

static void
emfb_view_hide_selected(BonoboUIComponent *uid, void *data, const char *path)
{
	EMFolderView *emfv = data;
	GPtrArray *uids;

	/* TODO: perhaps this should sit directly on message_list? */
	/* is it worth it, it's so trivial */
	printf("viewhideselected\n");
	uids = message_list_get_selected(emfv->list);
	message_list_hide_uids(emfv->list, uids);
	message_list_free_uids(emfv->list, uids);
}

static void
emfb_view_show_all(BonoboUIComponent *uid, void *data, const char *path)
{
	EMFolderView *emfv = data;

	printf("viewshowall\n");
	message_list_hide_clear(emfv->list);
}

/* ********************************************************************** */

static void
emfb_empty_trash(BonoboUIComponent *uid, void *data, const char *path)
{
	EMFolderView *emfv = data;
	emfv = emfv;
	/* FIXME: rename/refactor empty_trash to empty_trash(parent window) */
}

static void
emfb_forget_passwords(BonoboUIComponent *uid, void *data, const char *path)
{
	e_passwords_forget_passwords();
}

static void
emfb_mail_compose(BonoboUIComponent *uid, void *data, const char *path)
{
	EMFolderView *emfv = data;
	emfv = emfv;
}

static void
emfb_mail_stop(BonoboUIComponent *uid, void *data, const char *path)
{
	camel_operation_cancel(NULL);
}

static void
emfb_mail_post(BonoboUIComponent *uid, void *data, const char *path)
{
	EMFolderView *emfv = data;
	emfv = emfv;
}

static void
emfb_tools_filters(BonoboUIComponent *uid, void *data, const char *path)
{
	EMFolderView *emfv = data;
	emfv = emfv;

	/* re-do filter_edit to be a generic callback */
}

static void
emfb_tools_subscriptions(BonoboUIComponent *uid, void *data, const char *path)
{
	GtkWidget *w;

	/* FIXME: must stop multiple instances */
	w = em_subscribe_editor_new();
	gtk_widget_show(w);
}

static void
emfb_tools_vfolders(BonoboUIComponent *uid, void *data, const char *path)
{
	/* FIXME: rename/refactor this */
	vfolder_edit();
}

static BonoboUIVerb emfb_verbs[] = {
	BONOBO_UI_UNSAFE_VERB ("EditCut", emfb_edit_cut),
	BONOBO_UI_UNSAFE_VERB ("EditCopy", emfb_edit_copy),
	BONOBO_UI_UNSAFE_VERB ("EditPaste", emfb_edit_paste),
	BONOBO_UI_UNSAFE_VERB ("EditInvertSelection", emfb_edit_invert_selection),
	BONOBO_UI_UNSAFE_VERB ("EditSelectAll", emfb_edit_select_all),
        BONOBO_UI_UNSAFE_VERB ("EditSelectThread", emfb_edit_select_thread),
	BONOBO_UI_UNSAFE_VERB ("ChangeFolderProperties", emfb_folder_properties),
	BONOBO_UI_UNSAFE_VERB ("FolderExpunge", emfb_folder_expunge),
	/* HideDeleted is a toggle */
	BONOBO_UI_UNSAFE_VERB ("MessageMarkAllAsRead", emfb_mark_all_read),
	BONOBO_UI_UNSAFE_VERB ("ViewHideRead", emfb_view_hide_read),
	BONOBO_UI_UNSAFE_VERB ("ViewHideSelected", emfb_view_hide_selected),
	BONOBO_UI_UNSAFE_VERB ("ViewShowAll", emfb_view_show_all),
	/* ViewThreaded is a toggle */

	BONOBO_UI_UNSAFE_VERB ("EmptyTrash", emfb_empty_trash),
	BONOBO_UI_UNSAFE_VERB ("ForgetPasswords", emfb_forget_passwords),
	BONOBO_UI_UNSAFE_VERB ("MailCompose", emfb_mail_compose),
	BONOBO_UI_UNSAFE_VERB ("MailPost", emfb_mail_post),
	BONOBO_UI_UNSAFE_VERB ("MailStop", emfb_mail_stop),
	BONOBO_UI_UNSAFE_VERB ("ToolsFilters", emfb_tools_filters),
	BONOBO_UI_UNSAFE_VERB ("ToolsSubscriptions", emfb_tools_subscriptions),
	BONOBO_UI_UNSAFE_VERB ("ToolsVFolders", emfb_tools_vfolders),
	/* ViewPreview is a toggle */

	BONOBO_UI_VERB_END
};

static EPixmap emfb_pixmaps[] = {
	E_PIXMAP ("/commands/ChangeFolderProperties", "configure_16_folder.xpm"),
	E_PIXMAP ("/commands/ViewHideRead", "hide_read_messages.xpm"),
	E_PIXMAP ("/commands/ViewHideSelected", "hide_selected_messages.xpm"),
	E_PIXMAP ("/commands/ViewShowAll", "show_all_messages.xpm"),
	
	E_PIXMAP ("/commands/EditCut", "16_cut.png"),
	E_PIXMAP ("/commands/EditCopy", "16_copy.png"),
	E_PIXMAP ("/commands/EditPaste", "16_paste.png"),

	E_PIXMAP ("/commands/MailCompose", "new-message.xpm"),

	E_PIXMAP_END
};

static void
emfb_hide_deleted(BonoboUIComponent *uic, const char *path, Bonobo_UIComponent_EventType type, const char *state, void *data)
{
	GConfClient *gconf;
	EMFolderView *emfv = data;

	if (type != Bonobo_UIComponent_STATE_CHANGED)
		return;

	gconf = gconf_client_get_default();
	gconf_client_set_bool(gconf, "/apps/evolution/mail/display/show_deleted", state[0] == '0', NULL);
	if (!(emfv->folder && (emfv->folder->folder_flags & CAMEL_FOLDER_IS_TRASH)))
		message_list_set_hidedeleted(emfv->list, state[0] != '0');
	g_object_unref(gconf);
}

static void
emfb_view_threaded(BonoboUIComponent *uic, const char *path, Bonobo_UIComponent_EventType type, const char *state, void *data)
{
	GConfClient *gconf;
	EMFolderView *emfv = data;

	if (type != Bonobo_UIComponent_STATE_CHANGED)
		return;

	/* ??
	bstate = atoi(state);
	e_meta_set_bool(fb->meta, "thread_list", bstate);*/

	gconf = gconf_client_get_default();
	gconf_client_set_bool(gconf, "/apps/evolution/mail/display/thread_list", state[0] != '0', NULL);
	message_list_set_threaded(emfv->list, state[0] != '0');
	g_object_unref(gconf);

	/* FIXME: update selection state? */
}

static void
emfb_view_preview(BonoboUIComponent *uic, const char *path, Bonobo_UIComponent_EventType type, const char *state, void *data)
{
	GConfClient *gconf;
	EMFolderView *emfv = data;

	if (type != Bonobo_UIComponent_STATE_CHANGED)
		return;

	/*bstate = atoi(state);
	  e_meta_set_bool(fb->meta, "show_preview", bstate);*/

	gconf = gconf_client_get_default();
	gconf_client_set_bool(gconf, "/apps/evolution/mail/display/show_preview", state[0] != '0', NULL);
	g_object_unref(gconf);

	em_folder_browser_show_preview((EMFolderBrowser *)emfv, state[0] != '0');
}

static void
emfb_activate(EMFolderView *emfv, BonoboUIComponent *uic, int state)
{
	if (state) {
		gboolean state;
		GConfClient *gconf = gconf_client_get_default();

		/* parent loads all ui files via ui_files */
		emfb_parent->activate(emfv, uic, state);

		bonobo_ui_component_add_verb_list_with_data(uic, emfb_verbs, emfv);
		e_pixmaps_update(uic, emfb_pixmaps);

#if 0
		/* FIXME: finish */
		/* (Pre)view pane size (do this first because it affects the
	           preview settings - see folder_browser_set_message_preview()
	           internals for details) */
		g_signal_handler_block(emfb->vpane, emfb->priv->vpane_resize_id);
		gtk_paned_set_position((GtkPaned *)emfb->vpane, gconf_client_get_int (gconf, "/apps/evolution/mail/display/paned_size", NULL));
		g_signal_handler_unblock(emfb->vpane, emfb->priv->vpane_resize_id);
#endif
	
		/* (Pre)view toggle */
		state = gconf_client_get_bool(gconf, "/apps/evolution/mail/display/show_preview", NULL);
		/*if (fb->meta)
		  show_preview = e_meta_get_bool(fb->meta, "show_preview", show_preview);*/
		bonobo_ui_component_set_prop(uic, "/commands/ViewPreview", "state", state?"1":"0", NULL);
		em_folder_browser_show_preview((EMFolderBrowser *)emfv, state);
		bonobo_ui_component_add_listener(uic, "ViewPreview", emfb_view_preview, emfv);
	
		/* Stop button */
		state = mail_msg_active((unsigned int)-1);
		bonobo_ui_component_set_prop(uic, "/commands/MailStop", "sensitive", state?"1":"0", NULL);

		/* HideDeleted */
		state = !gconf_client_get_bool(gconf, "/apps/evolution/mail/display/show_deleted", NULL);
		bonobo_ui_component_set_prop(uic, "/commands/HideDeleted", "state", state ? "1" : "0", NULL);
		bonobo_ui_component_add_listener(uic, "HideDeleted", emfb_hide_deleted, emfv);
		if (!(emfv->folder && (emfv->folder->folder_flags & CAMEL_FOLDER_IS_TRASH)))
			message_list_set_hidedeleted (emfv->list, state);
		else
			bonobo_ui_component_set_prop(uic, "/commands/HideDeleted", "sensitive", state?"1":"0", NULL);
	
		/* ViewThreaded */
		state = gconf_client_get_bool(gconf, "/apps/evolution/mail/display/thread_list", NULL);
		/* FIXME: what to do about the messy 'meta' stuff ... */
		/*if (fb->meta)
		  state = e_meta_get_bool(fb->meta, "thread_list", state);*/
		bonobo_ui_component_set_prop(uic, "/commands/ViewThreaded", "state", state?"1":"0", NULL);
		bonobo_ui_component_add_listener(uic, "ViewThreaded", emfb_view_threaded, emfv);
		message_list_set_threaded(emfv->list, state);

		/* FIXME: Selection state */

		/* FIXME: property menu customisation */
		/*folder_browser_setup_property_menu (fb, fb->uicomp);*/
	
		/* FIXME: GalView menus?  Also requires a message_list_display_view(ml, GalView) call */
		/*if (fb->view_instance == NULL)
		  folder_browser_ui_setup_view_menus (fb);
		  FIXME: The galview instance should be setup earlier, when we have the folder, when we setup a meta? */

		g_object_unref(gconf);
	} else {
		const BonoboUIVerb *v;
		
		for (v = &emfb_verbs[0]; v->cname; v++)
			bonobo_ui_component_remove_verb(uic, v->cname);

		emfb_parent->activate(emfv, uic, state);
	}
}
